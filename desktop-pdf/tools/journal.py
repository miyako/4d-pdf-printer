"""Version-bound, read-only journal validation. No collection, retry or Windows calls."""
import json
import logging

import journal_v1 as v1

need = v1.need
HOST = {(9, 1), (9, 9), (11, 2), (11, 10), (15, 4), (15, 8), (15, 12)}
OBS_KEYS = {"v", "pid", "span", "c", "r", "a", "f", "w", "i", "x", "journal_status"}
REQUESTS = (1, 3, 2, 4, 0)

class ObservationShapeError(ValueError):
    pass


def number(value, maximum=9007199254740991, minimum=0):
    need(v1.integer(value, maximum) and value >= minimum, "observation integer")


def boolean(value):
    need(type(value) is bool, "observation boolean")


def seq(value, size):
    need(type(value) is list and len(value) == size, "exact observation tuple")
    return value


def stamp(tick, thread):
    number(tick)
    number(thread, 0xffffffff, 1)


def optional_number(value, maximum, minimum=0):
    if value is not None:
        number(value, maximum, minimum)


def cell(value, checker, first=False):
    seq(value, 3 if first else 2)
    number(value[0], 2)
    if first:
        boolean(value[1])
        need(value[0] != 0 or value[1] is False, "unseen first-entry repeat")
    payload = value[-1]
    need((value[0] == 2) == (payload is not None), "phase readiness/payload")
    if payload is not None:
        checker(payload)
    return payload


def ready(value):
    return value[-1] if value[0] == 2 else None


def cancel(value, origin):
    seq(value, 3 if origin == 0 else 4)
    stamp(*value[:2])
    if origin == 0:
        number(value[2], 0xffffffff)
    else:
        number(value[2], 15)
        number(value[3], 15)
        need(tuple(value[2:]) in HOST, "observed host short-circuit mask")


def finish(value, caller):
    seq(value, 7)
    stamp(*value[:2])
    number(value[2], 4)
    boolean(value[3])
    number(value[4], 2)
    boolean(value[6])
    need(value[2] == REQUESTS[caller] and value[3] == (caller == 4), "finish caller/request/owner")
    if caller == 4:
        need(value[4] == 0 and value[5] is None, "DataTail no CAS")
    elif caller == 3:
        need(value[4] == 2, "framework Finish must lose preceding reason CAS")
        number(value[5], 4, 1)
    else:
        need(value[4] in (1, 2), "nonNormal Finish must attempt CAS")
        number(value[5], 4)
        need(value[5] == 0 if value[4] == 1 else value[5] >= 1, "actual CAS expected")


def reason(value):
    seq(value, 6)
    number(value[0], 4)
    boolean(value[1])
    stamp(value[2], value[3])
    number(value[4])
    number(value[5], 4)
    need(value[2] <= value[4] and value[5] == (4, 4, 1, 3, 2)[value[0]], "actual reason setter")


def applied(value):
    seq(value, 6)
    number(value[0], 1)
    boolean(value[1])
    stamp(value[2], value[3])
    if value[0] == 0:
        number(value[4], 0xffffffff)
    else:
        seq(value[4], 2)
        cancel(value[2:4] + value[4], 1)
    number(value[5])
    need(value[2] <= value[5], "applied cancellation tick")


def winner(value):
    seq(value, 3)
    number(value[0], 4)
    boolean(value[1])
    finish(value[2], value[0])
    need(value[2][6] is True, "actual gate winner")


def outcome(value):
    seq(value, 4)
    stamp(*value[:2])
    number(value[2], 1)
    if value[2] == 0:
        need(value[3] is None, "normal projected API/await return has no HRESULT")
    else:
        number(value[3], 0xffffffff, 0x80000000)


def api_head(value, route):
    seq(value, 6)
    stamp(*value[:2])
    if route == 2:
        number(value[2], 0)
        need(value[3] is value[4] is None, "launch distinct operation domain")
        seq(value[5], 3)
        stamp(*value[5][:2])
        number(value[5][2], 0)
        need(value[5][0] <= value[0], "existing launch guard sample")
    else:
        optional_number(value[2], 65535, 1)
        number(value[3], 9)
        optional_number(value[4], 31)
        need(value[5] is None, "no journal Status getter")


def complete_call(value):
    seq(value, 2)
    def entry(item):
        seq(item, 3)
        stamp(*item[:2])
        number(item[2], 2)
    head = cell(value[0], entry, True)
    returned = cell(value[1], outcome)
    need(returned is None or head is not None, "CompleteJob entry dependency")
    if returned is not None:
        need(head[0] <= returned[0], "CompleteJob stamp order")


def complete_shape(o):
    seq(o["j"], 2)
    current, cancellation = o["j"]
    complete_call(current)
    def sampled(value):
        seq(value, 3)
        stamp(*seq(value[0], 2))
        stamp(*seq(value[1], 2))
        need(value[0][0] <= value[1][0] and value[0][1] == value[1][1],
             "CompleteJob cancellation sample interval")
        context = ready(o["c"][0])
        need(context is not None and value[0] == context[:2], "CompleteJob framework context binding")
        complete_call(value[2])
        for before, after in zip(value[2], current):
            need(before[0] <= after[0], "CompleteJob immutable phase progression")
            if before[0] == 2:
                need(before[-1] == after[-1], "CompleteJob immutable payload")
                need(before[-1][0] <= value[1][0], "CompleteJob payload before sample end")
        need(not value[2][0][1] or current[0][1], "CompleteJob repeated is sticky")
    cell(cancellation, sampled)


def observation_shape(terminal):
    o = terminal["observations"]
    extended = terminal["version"] == 3
    need(type(o) is dict and set(o) == OBS_KEYS | ({"j"} if extended else set()), "exact observation keys")
    number(o["v"], 2 if extended else 1, 2 if extended else 1)
    number(o["pid"], 0xffffffff, 1)
    seq(o["span"], 2)
    for tick in o["span"]:
        number(tick)
    need(o["span"][0] <= o["span"][1] and o["journal_status"] is None, "snapshot interval/unknown status")
    seq(o["c"], 2)
    for origin in range(2):
        cell(o["c"][origin], lambda value, origin=origin: cancel(value, origin), True)
    r = cell(o["r"], reason)
    seq(o["a"], 2)
    need(all(value is not None for value in o["a"]) if terminal["drain_state"] == "canceled"
         else o["a"] == [None, None], "frozen applied cancellation presence")
    for value in o["a"]:
        if value is not None:
            applied(value)
            source = ready(o["c"][value[0]])
            if value[1] and source is not None:
                context = value[2:4] + ([value[4]] if value[0] == 0 else value[4])
                need(source == context, "applied first-entry identity")
    if o["a"][0] is not None:
        need(o["a"][1][5] <= o["a"][0][5], "minimum versus first applied tick")
    seq(o["f"], 5)
    for caller in range(5):
        cell(o["f"][caller], lambda value, caller=caller: finish(value, caller), True)
    if r is not None:
        source = ready(o["c"][r[0]]) if r[0] < 2 else ready(o["f"][r[0]-2])
        if r[1] and source is not None:
            need(source[:2] == r[2:4], "reason setter first-entry identity")
        if terminal["finalize_reason"] != "normal":
            need(v1.REASONS[r[5]] == terminal["finalize_reason"], "reason setter/decision reason")
    seq(o["w"], 3)
    head = cell(o["w"][0], winner)
    began = cell(o["w"][1], lambda value: stamp(*seq(value, 2)))
    def passed(value):
        seq(value, 2)
        number(value[0])
        number(value[1], 4)
    sampled = cell(o["w"][2], passed)
    need(began is None or head is not None, "began winner dependency")
    need(sampled is None or began is not None, "reason-load began dependency")
    if head is not None and head[1]:
        need(ready(o["f"][head[0]]) == head[2], "winner owns exact first Finish")
    winning = [(i, ready(value)) for i, value in enumerate(o["f"])
               if ready(value) is not None and ready(value)[6]]
    need(len(winning) <= 1, "one first-record gate winner")
    if head is not None and winning:
        need(winning[0] == (head[0], head[2]), "actual gate winner agreement")
    if began is not None:
        need(head[2][0] <= began[0], "winner before actual began")
    if sampled is not None:
        need(began[0] <= sampled[0] <= terminal["sealed_tick_ms"] and
             v1.REASONS[sampled[1]] == terminal["finalize_reason"], "actual finalizer reason load")
    if o["i"] is not None:
        seq(o["i"], 3)
        optional_number(o["i"][0], 65535, 1)
        number(o["i"][1], 9)
        optional_number(o["i"][2], 31)
        need(terminal["outstanding_io_at_decision"], "retained operation requires writer")
        inflight = terminal["inflight"]
        need(v1.STAGES[o["i"][1]] == inflight["stage"] and
             (o["i"][2] is None or o["i"][2] == inflight["sequence"]), "frozen exact operation stage/event")
    seq(o["x"], 3)
    for route, value in enumerate(o["x"]):
        seq(value, 3)
        api = cell(value[0], lambda item, route=route: api_head(item, route), True)
        returned = cell(value[1], outcome)
        awaited = cell(value[2], outcome)
        need((returned is None and awaited is None) or api is not None, "API phase head dependency")
        if route == 2:
            need(value[2] == [0, None], "no new launch await")
    if extended:
        complete_shape(o)
    return o


def terminal_shape(raw, activation, expected_version, job=None):
    need(type(expected_version) is int and expected_version in (1, 2, 3), "explicit expected source version")
    need(v1.identifier(activation), "expected activation binding")
    value = v1.wire(raw, 4096)
    need(type(value.get("version")) is int and value["version"] == expected_version, "unexpected terminal version")
    if expected_version == 1:
        return v1.terminal_shape(raw, activation, job)
    need(job is None, "general shape is not expected-job admission")
    need(set(value) == v1.TERMINAL_KEYS | {"observations"}, "exact23 terminal fields")
    need(value["schema"] == "4d-pdf-printer.activation-journal-terminal", "terminal schema")
    need(value["activation"] == activation and type(value["job"]) is str and
         (value["job"] == "" or v1.identifier(value["job"])), "activation/unbound job domain")
    need(json.dumps(value, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii") == raw,
         "compact ASCII integer wire encoding")
    need(value["finalize_reason"] in v1.REASONS and value["drain_state"] in v1.STATES, "terminal enums")
    for key in ("accepted_count", "dequeued_count"):
        number(value[key], 32)
    need(value["dequeued_count"] <= value["accepted_count"], "dequeued exceeds accepted")
    for key in ("rejected_before_seal", "rejected_after_seal_at_decision"):
        number(value[key], 0xffffffff)
    for key in ("last_published_sequence", "finish_sequence"):
        optional_number(value[key], 31)
        need(value[key] is None or value[key] < value["accepted_count"], "sequence beyond admitted count")
    for key in ("admission_loss", "outstanding_io_at_decision", "deferral_acquired",
                "deferral_release_planned", "deferral_release_observed"):
        boolean(value[key])
    need(value["admission_loss"] == (value["rejected_before_seal"] != 0), "loss/counter mismatch")
    need(value["deferral_acquired"] == value["deferral_release_planned"] and
         value["deferral_release_observed"] is False, "before-deferral receipt boundary")
    for key in ("sealed_tick_ms", "decision_tick_ms", "deadline_tick_ms"):
        number(value[key])
    need(value["sealed_tick_ms"] <= value["decision_tick_ms"], "decision before seal")
    inflight, failure = value["inflight"], value["first_failure"]
    need((inflight is not None) == value["outstanding_io_at_decision"], "inflight presence")
    if inflight is not None:
        need(type(inflight) is dict and set(inflight) ==
             {"sequence", "checkpoint", "stage", "temporary_leaf", "cancel_requested"}, "inflight shape")
        v1.optional_event(inflight)
        boolean(inflight["cancel_requested"])
        need(inflight["stage"] in v1.STAGES and type(inflight["temporary_leaf"]) is str and
             (inflight["temporary_leaf"] == "" or v1.re.fullmatch("pending-[0-9a-f]{32}", inflight["temporary_leaf"])),
             "inflight fields")
    if failure is not None:
        need(type(failure) is dict and set(failure) ==
             {"sequence", "checkpoint", "stage", "code_domain", "code_u32"}, "first failure shape")
        v1.optional_event(failure)
        need(failure["stage"] in v1.STAGES and failure["code_domain"] == "hresult", "first failure fields")
        number(failure["code_u32"], 0xffffffff)
    try:
        observation_shape(value)
    except ValueError as error:
        raise ObservationShapeError(str(error)) from error
    return value


def sample_flags(terminal):
    o = terminal["observations"]
    phases = list(o["c"]) + [o["r"]] + list(o["f"]) + list(o["w"])
    phases += [phase for api in o["x"] for phase in api]
    if terminal["version"] == 3:
        phases += o["j"][0] + [o["j"][1]]
        sampled = ready(o["j"][1])
        if sampled is not None:
            phases += sampled[2]
    flags = 1 if any(value[0] == 1 for value in phases) else 0
    firsts = o["c"] + o["f"] + [api[0] for api in o["x"]]
    if any(value[1] for value in firsts):
        flags |= 2
    if any(value[0] != 2 for value in o["w"]):
        flags |= 4
    if (terminal["finalize_reason"] != "normal" or terminal["drain_state"] == "canceled") and o["r"][0] != 2:
        flags |= 8
    if (o["i"] is not None and o["i"][0] is None) or any(
            ready(api[0]) is not None and ready(api[0])[2] is None for api in o["x"][:2]):
        flags |= 16
    if any(ready(api[0]) is not None and (api[1][0] != 2 or api[2][0] != 2) for api in o["x"][:2]):
        flags |= 32
    r = ready(o["r"])
    if r is not None and r[1]:
        source = o["c"][r[0]] if r[0] < 2 else o["f"][r[0]-2]
        if source[0] != 2:
            flags |= 64
    if any(value is not None and value[1] and o["c"][value[0]][0] != 2 for value in o["a"]):
        flags |= 64
    ticks = [ready(value)[0] for value in o["c"] + o["f"] if ready(value) is not None]
    if r is not None:
        ticks += [r[2], r[4]]
    if ready(o["w"][0]) is not None:
        ticks.append(ready(o["w"][0])[2][0])
    ticks += [ready(value)[0] for value in o["w"][1:] if ready(value) is not None]
    for api in o["x"]:
        for phase in api:
            if ready(phase) is not None:
                ticks.append(ready(phase)[0])
        if ready(api[0]) is not None and ready(api[0])[5] is not None:
            ticks.append(ready(api[0])[5][0])
    if any(tick > terminal["decision_tick_ms"] for tick in ticks) or (
            terminal["finalize_reason"] == "normal" and r is not None) or (
            terminal["drain_state"] != "canceled" and any(ready(c) is not None for c in o["c"])):
        flags |= 128
    if terminal["version"] == 3:
        call, cancellation = o["j"]
        if (call[0][0] != 0 and call[1][0] != 2) or (o["c"][0][0] != 0 and cancellation[0] != 2):
            flags |= 256
        if call[0][1] or (ready(cancellation) is not None and ready(cancellation)[2][0][1]):
            flags |= 512
    return flags


def _normal_decision(terminal, activation, job, old_activations):
    need(v1.identifier(activation) and activation not in old_activations, "fresh activation")
    need(v1.identifier(job) and terminal["job"] == job, "exact genuine expected job binding")
    need(terminal["finalize_reason"] == "normal" and terminal["drain_state"] == "drained", "normal drained journal")
    need(terminal["decision_tick_ms"] < terminal["deadline_tick_ms"], "normal decision must precede receipt deadline")
    need(terminal["admission_loss"] is False and terminal["rejected_before_seal"] ==
         terminal["rejected_after_seal_at_decision"] == 0, "no admission losses at decision")
    need(terminal["outstanding_io_at_decision"] is False and terminal["inflight"] is None and
         terminal["first_failure"] is None, "no recorded I/O outstanding/failure at decision")
    need(terminal["deferral_acquired"] is True and terminal["deferral_release_planned"] is True,
         "actual Task deferral acquired/planned, never observed release")
    count = terminal["accepted_count"]
    need(1 <= count <= 32 and terminal["dequeued_count"] == count and
         terminal["last_published_sequence"] == terminal["finish_sequence"] == count-1,
         "terminal published-through-finish")
    return count


def _normal_admission(terminal, raw_records, activation, job, old_activations):
    """Same decision/record predicate as immutable v1, without rewriting v2 bytes."""
    count = _normal_decision(terminal, activation, job, old_activations)
    need(type(raw_records) is list and 1 <= count == len(raw_records) <= 32 and
         terminal["dequeued_count"] == count and terminal["last_published_sequence"] ==
         terminal["finish_sequence"] == count - 1, "published-through-finish, not merely consumed")
    records = []
    for index, raw in enumerate(raw_records):
        record = v1.wire(raw, 8192, allow_bom=True)
        need(set(record) == v1.DIAGNOSTIC_KEYS and record["schema"] == "4d-pdf-printer.activation-diagnostic" and
             type(record["version"]) is int and record["version"] == 1, "diagnostic shape/schema")
        need(type(record["sequence"]) is int and record["sequence"] == index and
             record["activation"] == activation and record["job"] in ("", job), "diagnostic source binding")
        need(record["checkpoint"] in v1.POINTS and v1.integer(record["tick_ms"]), "diagnostic event/tick")
        for key in ("hresult", "process", "thread", "identity_error", "identity_validation_error"):
            need(v1.integer(record[key], 0xffffffff), "diagnostic native integer")
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
    need(records[-1]["job"] == "" and terminal["sealed_tick_ms"] <= records[-1]["tick_ms"] <=
         terminal["decision_tick_ms"] < terminal["deadline_tick_ms"], "source-defined finish/decision before deadline")
    return dict(published_through_finish=True, terminal_decision_verified=True, activation=activation,
                job=job, count=count, task_process=records[0]["process"], terminal=terminal,
                deferral_return_proved=False, process_exit_proved=False, physical_unload_proved=False,
                root_host_identity_proved=False, correlation_is_authentication=False)


def general_v2(raw, activation):
    """Activation-level parse; never job admission, even for normal/drained journals."""
    return terminal_shape(raw, activation, 2)


def general_v3(raw, activation):
    """Explicit CompleteJob observations; unchanged journal admission predicate."""
    return terminal_shape(raw, activation, 3)


def complete_assessment(o):
    def phase(call):
        if call[0][1]:
            return "repeated-ambiguous"
        if any(value[0] == 1 for value in call):
            return "partial"
        if call[0][0] == 0:
            return "not-attempted-at-entry-read"
        returned = ready(call[1])
        if returned is None:
            return "entry-recorded-return-unobserved"
        return "returned" if returned[2] == 0 else "failed"
    call, cancellation = o["j"]
    sampled = ready(cancellation)
    return dict(snapshot_phase=phase(call), attempted_status=ready(call[0])[2] if ready(call[0]) else None,
                outcome=ready(call[1]),
                framework_sample_phase=phase(sampled[2]) if sampled is not None else
                "partial" if cancellation[0] == 1 else "not-recorded",
                framework_sample=sampled, os_call_order="not-established")


def journal_admission(raw_records, raw_terminal, activation, job, old_activations, *, expected_version):
    need(v1.identifier(job), "genuine expected job context required")
    if expected_version == 1 and type(expected_version) is int:
        return v1.journal_admission(raw_records, raw_terminal, activation, job, old_activations)
    terminal = terminal_shape(raw_terminal, activation, expected_version)
    return _normal_admission(terminal, raw_records, activation, job, old_activations)


def assess(raw_terminal, activation, *, expected_version, job=None, raw_records=None, old_activations=()):
    result = dict(schema="4d-pdf-printer.cancellation-assessment", version=1,
                  receipt_shape="invalid", observation_shape="not-evaluable",
                  decision_admission="not-evaluable", sample_state="not-evaluable", sample_flags=None,
                  question="not-evaluable", finalizer=None, framework_reasons_raw=[None, None, None],
                  journal_status=None, external_cause="unknown", review_disposition="blocked",
                  technical_success_promoted=False, physical_stop_proved=False,
                  deferral_return_proved=False, process_exit_proved=False)
    try:
        envelope = v1.wire(raw_terminal, 4096)
        version = envelope.get("version")
        if type(version) is not int or version not in (1, 2, 3):
            result["receipt_shape"] = "unsupported"
            raise ValueError("unsupported source terminal version")
        if type(expected_version) is not int or version != expected_version:
            result["receipt_shape"] = "unexpected-version"
            raise ValueError("unexpected source terminal version")
        try:
            terminal = terminal_shape(raw_terminal, activation, expected_version, job if version == 1 else None)
        except ObservationShapeError:
            result["observation_shape"] = "invalid"
            raise
    except ValueError as error:
        logging.getLogger(__name__).error("Journal assessment blocked: %s", error)
        return result
    result.update(receipt_shape=f"valid-v{version}", observation_shape="legacy-v1" if version == 1 else "valid",
                  review_disposition="review-only")
    if v1.identifier(job):
        if terminal["job"] != job:
            result["decision_admission"] = "fail"
        else:
            try:
                _normal_decision(terminal, activation, job, old_activations)
                if raw_records is not None:
                    journal_admission(raw_records, raw_terminal, activation, job, old_activations,
                                      expected_version=expected_version)
                    result["decision_admission"] = "pass"
            except ValueError as error:
                logging.getLogger(__name__).error("Normal journal admission failed: %s", error)
                result["decision_admission"] = "fail"
    if version == 1:
        return result
    o = terminal["observations"]
    if version == 3:
        result["complete_job"] = complete_assessment(o)
    flags = sample_flags(terminal)
    result.update(sample_flags=flags, sample_state="partial-sample" if flags else "ready-sample")
    head = ready(o["w"][0])
    if head is not None:
        result["finalizer"] = head[0]
    first = ready(o["c"][0])
    result["framework_reasons_raw"] = [first[2] if first is not None else None] + [
        value[4] if value is not None and value[0] == 0 else None for value in o["a"]]
    r = ready(o["r"])
    if terminal["drain_state"] == "canceled":
        if r is not None and all(ready(phase) is not None for phase in o["w"]):
            origins = {value[0] for value in o["a"]}
            result["question"] = ("first-min-framework-recorded" if origins == {0} else
                                  "first-min-host-recorded" if origins == {1} else "first-min-mixed-recorded")
        else:
            result["question"] = "bound-context-incomplete"
    elif any(ready(c) is not None for c in o["c"]) or (r is not None and r[5] == 4):
        result["question"] = "sampled-signal-unbound"
    else:
        result["question"] = "not-established"
    return result
