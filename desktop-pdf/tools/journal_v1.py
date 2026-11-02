"""Strict source-bound journal checks. No Windows calls, collection or polling."""
import json
import re

POINTS = "run-enter deferral-acquired trigger-before trigger-acquired session-before session-subscribed session-start-before session-start-returned data-enter duplicate-data endpoint-checked content-checked configuration-checked selector-checked registration-checked submission-checked metadata-validated job-created pre-job-failed data-failed complete-job-before complete-job-returned complete-job-failed canceled finish run-failed session-failed callback-deadline".split()
STAGES = "none enqueue initialize create-root create-parent create-activation-folder prepare-record create-pending write-pending rename-final".split()
REASONS = "normal activation-error session-error callback-deadline canceled".split()
STATES = "drained failed deadline canceled unavailable".split()
TERMINAL_KEYS = set("schema version activation job finalize_reason drain_state accepted_count dequeued_count rejected_before_seal rejected_after_seal_at_decision last_published_sequence finish_sequence admission_loss sealed_tick_ms decision_tick_ms deadline_tick_ms outstanding_io_at_decision deferral_acquired deferral_release_planned deferral_release_observed inflight first_failure".split())
DIAGNOSTIC_KEYS = set("schema version activation sequence checkpoint hresult process thread tick_ms job identity_api identity_status identity_error identity_validation_error app_user_model_id".split())


def need(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value, maximum=9007199254740991):
    return type(value) is int and 0 <= value <= maximum


def identifier(value):
    return type(value) is str and re.fullmatch(r"[0-9a-f]{32}", value) is not None


def object_pairs(items):
    result = {}
    for key, value in items:
        need(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def wire(raw, maximum, allow_bom=False):
    need(type(raw) is bytes and 0 < len(raw) <= maximum, "UTF8 wire byte bound")
    text = raw.decode("utf-8-sig" if allow_bom else "utf-8")
    need(not text.startswith("\ufeff"), "unexpected/repeated producer wire BOM")
    value = json.loads(text, object_pairs_hook=object_pairs,
                       parse_constant=lambda value: (_ for _ in ()).throw(ValueError("nonfinite JSON")))
    need(type(value) is dict, "exact object root")
    return value


def optional_event(value):
    sequence, checkpoint = value["sequence"], value["checkpoint"]
    need((sequence is None and checkpoint is None) or
         (integer(sequence, 31) and checkpoint in POINTS), "nullable event pair")


def terminal_shape(raw, activation, job):
    need(identifier(activation) and identifier(job), "expected binding")
    value = wire(raw, 4096)
    need(set(value) == TERMINAL_KEYS, "exact22 terminal fields")
    need(value["schema"] == "4d-pdf-printer.activation-journal-terminal" and
         type(value["version"]) is int and value["version"] == 1, "terminal schema/version")
    need(value["activation"] == activation and value["job"] == job, "terminal exact source binding")
    need(value["finalize_reason"] in REASONS and value["drain_state"] in STATES, "terminal enums")
    for key in ("accepted_count", "dequeued_count"):
        need(integer(value[key], 32), "journal bounded counter")
    need(value["dequeued_count"] <= value["accepted_count"], "dequeued exceeds accepted")
    for key in ("rejected_before_seal", "rejected_after_seal_at_decision"):
        need(integer(value[key], 0xffffffff), "rejection counter")
    for key in ("last_published_sequence", "finish_sequence"):
        need(value[key] is None or integer(value[key], 31), "nullable sequence")
        if value[key] is not None:
            need(value[key] < value["accepted_count"], "sequence beyond admitted count")
    for key in ("admission_loss", "outstanding_io_at_decision", "deferral_acquired",
                "deferral_release_planned", "deferral_release_observed"):
        need(type(value[key]) is bool, "exact terminal boolean")
    need(value["admission_loss"] == (value["rejected_before_seal"] != 0), "loss/counter mismatch")
    need(value["deferral_acquired"] == value["deferral_release_planned"] and
         value["deferral_release_observed"] is False, "before-deferral receipt boundary")
    for key in ("sealed_tick_ms", "decision_tick_ms", "deadline_tick_ms"):
        need(integer(value[key]), "JSON-safe monotonic tick")
    need(value["sealed_tick_ms"] <= value["decision_tick_ms"], "decision before seal")
    inflight = value["inflight"]
    need((inflight is not None) == value["outstanding_io_at_decision"], "inflight presence")
    if inflight is not None:
        need(type(inflight) is dict and set(inflight) ==
             {"sequence", "checkpoint", "stage", "temporary_leaf", "cancel_requested"}, "inflight shape")
        optional_event(inflight)
        need(inflight["stage"] in STAGES and type(inflight["cancel_requested"]) is bool, "inflight fields")
        need(type(inflight["temporary_leaf"]) is str and
             (inflight["temporary_leaf"] == "" or re.fullmatch("pending-[0-9a-f]{32}", inflight["temporary_leaf"])),
             "exact temporary leaf, never normalization")
    failure = value["first_failure"]
    if failure is not None:
        need(type(failure) is dict and set(failure) ==
             {"sequence", "checkpoint", "stage", "code_domain", "code_u32"}, "first failure shape")
        optional_event(failure)
        need(failure["stage"] in STAGES and failure["code_domain"] == "hresult" and
             integer(failure["code_u32"], 0xffffffff), "first raw HRESULT")
    return value


def journal_admission(raw_records, raw_terminal, activation, job, old_activations):
    need(identifier(activation) and activation not in old_activations, "fresh activation")
    terminal = terminal_shape(raw_terminal, activation, job)
    need(terminal["finalize_reason"] == "normal" and terminal["drain_state"] == "drained", "normal drained journal")
    need(terminal["decision_tick_ms"] < terminal["deadline_tick_ms"], "normal decision must precede receipt deadline")
    need(terminal["admission_loss"] is False and terminal["rejected_before_seal"] ==
         terminal["rejected_after_seal_at_decision"] == 0, "no admission losses at decision")
    need(terminal["outstanding_io_at_decision"] is False and terminal["inflight"] is None and
         terminal["first_failure"] is None, "no recorded I/O outstanding/failure at decision")
    need(terminal["deferral_acquired"] is True and terminal["deferral_release_planned"] is True,
         "actual Task deferral acquired/planned, never observed release")
    count = terminal["accepted_count"]
    need(type(raw_records) is list and 1 <= count == len(raw_records) <= 32 and
         terminal["dequeued_count"] == count and terminal["last_published_sequence"] ==
         terminal["finish_sequence"] == count - 1, "published-through-finish, not merely consumed")
    records = []
    for index, raw in enumerate(raw_records):
        record = wire(raw, 8192, allow_bom=True)
        need(set(record) == DIAGNOSTIC_KEYS and record["schema"] == "4d-pdf-printer.activation-diagnostic" and
             type(record["version"]) is int and record["version"] == 1, "diagnostic shape/schema")
        need(type(record["sequence"]) is int and record["sequence"] == index and
             record["activation"] == activation and record["job"] in ("", job), "diagnostic source binding")
        need(record["checkpoint"] in POINTS and integer(record["tick_ms"]), "diagnostic event/tick")
        for key in ("hresult", "process", "thread", "identity_error", "identity_validation_error"):
            need(integer(record[key], 0xffffffff), "diagnostic native integer")
        need(record["process"] > 0 and record["thread"] > 0 and record["hresult"] == 0, "normal Task checkpoint")
        need(record["identity_api"] == "GetCurrentApplicationUserModelId" and
             record["identity_status"] == "available" and
             record["identity_error"] == record["identity_validation_error"] == 0 and
             record["app_user_model_id"] == "Miyako.DesktopPdfIntegration_hjw4whvmzr08m!SaveAs",
             "observed Task identity only, not Host identity")
        records.append(record)
    need(len({record["process"] for record in records}) == 1, "one actual Task process")
    required = set("run-enter deferral-acquired trigger-acquired session-subscribed session-start-returned data-enter endpoint-checked content-checked configuration-checked selector-checked registration-checked submission-checked metadata-validated job-created complete-job-before complete-job-returned finish".split())
    points = [record["checkpoint"] for record in records]
    need(required <= set(points) and points[0] == "run-enter" and points[-1] == "finish" and
         points.count("finish") == points.count("job-created") == points.count("complete-job-before") ==
         points.count("complete-job-returned") == 1, "one normal lifecycle through reserved finish")
    need(not set(points) & set("pre-job-failed data-failed complete-job-failed canceled run-failed session-failed callback-deadline".split()),
         "no workflow failure checkpoints")
    need(points.index("job-created") < points.index("complete-job-before") <
         points.index("complete-job-returned") < len(points) - 1, "actual CompleteJob return precedes Finish")
    need(records[points.index("job-created")]["job"] == job, "actual new job correlation")
    # Finish is MakeEvent(Point::Finish) with the default EMPTY job. Terminal binds knownJob.
    need(records[-1]["job"] == "" and terminal["sealed_tick_ms"] <= records[-1]["tick_ms"] <=
         terminal["decision_tick_ms"] < terminal["deadline_tick_ms"], "source-defined finish/decision before deadline")
    return dict(published_through_finish=True, terminal_decision_verified=True, activation=activation,
                job=job, count=count, task_process=records[0]["process"], terminal=terminal,
                deferral_return_proved=False, process_exit_proved=False, physical_unload_proved=False,
                root_host_identity_proved=False, correlation_is_authentication=False)
