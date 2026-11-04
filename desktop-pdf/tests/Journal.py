"""Local synthetic/portable consumer regressions; no Windows/printing evidence."""
import copy
import hashlib
import json
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import journal
import journal_capture
import journal_v1 as v1

A, J = "a"*32, "b"*32
POINTS = ("run-enter deferral-acquired trigger-before trigger-acquired session-before session-subscribed "
          "session-start-before session-start-returned data-enter endpoint-checked content-checked "
          "configuration-checked selector-checked registration-checked submission-checked metadata-validated "
          "job-created complete-job-before complete-job-returned finish").split()


def wire(value):
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True).encode()


def records():
    return [wire(dict(schema="4d-pdf-printer.activation-diagnostic", version=1, activation=A, sequence=i,
                      checkpoint=point, hresult=0, process=100, thread=9,
                      tick_ms=101 if point == "finish" else i+1, job=J if point == "job-created" else "",
                      identity_api="GetCurrentApplicationUserModelId", identity_status="available",
                      identity_error=0, identity_validation_error=0,
                      app_user_model_id="Miyako.DesktopPdfIntegration_hjw4whvmzr08m!SaveAs"))
            for i, point in enumerate(POINTS)]


def terminal():
    f = [70, 9, 0, True, 0, None, True]
    o = dict(v=1, pid=100, span=[111, 112], c=[[0, False, None], [0, False, None]], r=[0, None],
             a=[None, None], f=[[0, False, None] for _ in range(4)] + [[2, False, f]],
             w=[[2, [4, True, f]], [2, [80, 9]], [2, [90, 0]]], i=None,
             x=[[[0, False, None], [0, None], [0, None]] for _ in range(3)], journal_status=None)
    return dict(schema="4d-pdf-printer.activation-journal-terminal", version=2, activation=A, job=J,
                finalize_reason="normal", drain_state="drained", accepted_count=len(POINTS),
                dequeued_count=len(POINTS), rejected_before_seal=0, rejected_after_seal_at_decision=0,
                last_published_sequence=len(POINTS)-1, finish_sequence=len(POINTS)-1, admission_loss=False,
                sealed_tick_ms=100, decision_tick_ms=110, deadline_tick_ms=2100,
                outstanding_io_at_decision=False, deferral_acquired=True, deferral_release_planned=True,
                deferral_release_observed=False, inflight=None, first_failure=None, observations=o)


def canceled():
    t = terminal()
    f = [77, 9, 4, False, 2, 4, True]
    t.update(job="", finalize_reason="canceled", drain_state="canceled")
    o = t["observations"]
    o.update(c=[[2, False, [70, 9, 0xffffffff]], [0, False, None]],
             r=[2, [0, True, 70, 9, 75, 4]],
             a=[[0, True, 70, 9, 0xffffffff, 76], [0, True, 70, 9, 0xffffffff, 76]],
             f=[[0, False, None] for _ in range(5)],
             w=[[2, [3, True, f]], [2, [80, 9]], [2, [90, 4]]])
    o["f"][3] = [2, False, f]
    return t


class Consumer(unittest.TestCase):
    def test_normal_bound_and_immutable_v1_predicate(self):
        t = terminal()
        answer = journal.journal_admission(records(), wire(t), A, J, (), expected_version=2)
        self.assertTrue(answer["published_through_finish"])
        t.pop("observations")
        t["version"] = 1
        self.assertEqual(v1.journal_admission(records(), wire(t), A, J, ()),
                         journal.journal_admission(records(), wire(t), A, J, (), expected_version=1))
        with self.assertRaises(ValueError):
            journal.terminal_shape(wire(t), A, 2)

    def test_general_unbound_normal_drained_is_not_job_success(self):
        t = terminal()
        t["job"] = ""
        self.assertEqual(journal.general_v2(wire(t), A)["job"], "")
        self.assertEqual(journal.assess(wire(t), A, expected_version=2)["decision_admission"], "not-evaluable")
        self.assertEqual(journal.assess(wire(t), A, expected_version=2, job=J)["decision_admission"], "fail")
        with self.assertRaises(ValueError):
            journal.journal_admission(records(), wire(t), A, J, (), expected_version=2)
        with self.assertRaises(ValueError):
            journal.terminal_shape(wire(t), A, 2, job=J)

    def test_prejob_framework_context_and_finalizer(self):
        t = canceled()
        result = journal.assess(wire(t), A, expected_version=2)
        self.assertEqual(result["receipt_shape"], "valid-v2")
        self.assertEqual(result["question"], "first-min-framework-recorded")
        self.assertEqual(result["framework_reasons_raw"], [0xffffffff]*3)
        self.assertEqual(result["finalizer"], 3)
        self.assertEqual(result["decision_admission"], "not-evaluable")
        self.assertFalse(result["technical_success_promoted"])
        self.assertEqual(len(result), 17)

    def test_activation_failure_unbound(self):
        t = terminal()
        t.update(job="", finalize_reason="activation-error")
        f = [70, 9, 1, False, 1, 0, True]
        t["observations"].update(r=[2, [2, True, 70, 9, 75, 1]],
                                 f=[[2, False, f]]+[[0, False, None] for _ in range(4)],
                                 w=[[2, [0, True, f]], [2, [80, 9]], [2, [90, 1]]])
        self.assertEqual(journal.general_v2(wire(t), A)["finalize_reason"], "activation-error")

    def test_late_signal_and_partial_are_not_normalized(self):
        t = terminal()
        t["observations"]["c"][0] = [2, False, [111, 9, 0]]
        r = journal.assess(wire(t), A, expected_version=2, job=J, raw_records=records())
        self.assertEqual(r["decision_admission"], "pass")
        self.assertEqual(r["question"], "sampled-signal-unbound")
        self.assertEqual(r["sample_flags"], 128)
        self.assertEqual(r["review_disposition"], "review-only")
        self.assertFalse(r["technical_success_promoted"])
        t["observations"]["c"][0] = [1, True, None]
        r = journal.assess(wire(t), A, expected_version=2)
        self.assertEqual(r["sample_flags"], 3)

    def test_discriminated_finish_domains(self):
        for caller, request in enumerate(journal.REQUESTS):
            allowed = (1, 2) if caller < 3 else (2,) if caller == 3 else (0,)
            for cas in range(3):
                value = [70, 9, request, caller == 4, cas, None if cas == 0 else 0 if cas == 1 else 4, False]
                if cas in allowed:
                    journal.finish(value, caller)
                else:
                    with self.assertRaises(ValueError):
                        journal.finish(value, caller)

    def test_shape_negatives(self):
        paths = [
            ("version",), ("accepted_count",), ("sealed_tick_ms",), ("deferral_acquired",),
            ("observations", "v"), ("observations", "pid"), ("observations", "span"),
            ("observations", "c"), ("observations", "r"), ("observations", "a"),
            ("observations", "f"), ("observations", "w"), ("observations", "i"),
            ("observations", "x"), ("observations", "journal_status")]
        for path in paths:
            for bad in ((False, -1, 1.0, {}, "bad") if path == ("deferral_acquired",) else (True, -1, 1.0, {}, "bad")):
                with self.subTest(path=path, bad=bad):
                    t = terminal()
                    obj = t
                    for part in path[:-1]:
                        obj = obj[part]
                    obj[path[-1]] = bad
                    with self.assertRaises(ValueError):
                        journal.general_v2(wire(t), A)

    def test_wire_and_binding_negatives(self):
        raw = wire(terminal())
        for bad in (b"\xef\xbb\xbf"+raw, raw+b"\n", raw.replace(b'"version":2', b'"version":2,"version":2'),
                    raw.replace(b'"pid"', b'"\\u0070id"'), raw.replace(b'"pid":100', b'"pid":1e2')):
            with self.assertRaises(ValueError):
                journal.general_v2(bad, A)
        for field in ("job", "activation"):
            t = terminal()
            t[field] = "A"*32
            with self.assertRaises(ValueError):
                journal.general_v2(wire(t), A)

    def test_source_links_and_phase_dependencies(self):
        t = canceled()
        for change in (
            lambda o: o["a"][0].__setitem__(4, 9),
            lambda o: o["w"][0][1].__setitem__(0, 4),
            lambda o: o["w"].__setitem__(0, [0, None]),
            lambda o: o["r"][1].__setitem__(5, 1),
        ):
            v = copy.deepcopy(t)
            change(v["observations"])
            with self.assertRaises(ValueError):
                journal.general_v2(wire(v), A)
        t["observations"]["c"][0] = [1, True, None]
        r = journal.assess(wire(t), A, expected_version=2)
        self.assertTrue(r["sample_flags"] & 64)

    def test_cancel_api_outcomes_are_not_acknowledgement(self):
        t = terminal()
        t["observations"]["x"][1] = [[2, False, [111, 10, 1, 9, 17, None]],
                                     [2, [112, 10, 0, None]], [0, None]]
        r = journal.assess(wire(t), A, expected_version=2)
        self.assertTrue(r["sample_flags"] & 32)
        self.assertIsNone(r["journal_status"])
        self.assertFalse(r["physical_stop_proved"])
        t["observations"]["x"][1][1] = [2, [112, 10, 0, 0]]
        with self.assertRaises(ValueError):
            journal.general_v2(wire(t), A)

    def test_expired_normal_is_shape_valid_but_not_admitted(self):
        t = terminal()
        t["deadline_tick_ms"] = 110
        journal.general_v2(wire(t), A)
        with self.assertRaises(ValueError):
            journal.journal_admission(records(), wire(t), A, J, (), expected_version=2)
        t = canceled()
        t["deadline_tick_ms"] = 0
        journal.general_v2(wire(t), A)

    def test_remaining_sample_flags_and_question_classifications(self):
        t = terminal()
        self.assertEqual(journal.assess(wire(t), A, expected_version=2)["sample_flags"], 0)
        t["observations"]["w"] = [[0, None]]*3
        self.assertEqual(journal.assess(wire(t), A, expected_version=2)["sample_flags"], 4)
        t = terminal()
        t["finalize_reason"] = "activation-error"
        t["observations"]["w"][2][1][1] = 1
        self.assertEqual(journal.assess(wire(t), A, expected_version=2)["sample_flags"], 8)
        t = terminal()
        t["observations"]["x"][0] = [[2, False, [100, 9, None, 9, 17, None]],
                                     [2, [101, 9, 0, None]], [2, [102, 9, 0, None]]]
        self.assertEqual(journal.assess(wire(t), A, expected_version=2)["sample_flags"], 16)
        for origins, question in (((1, 1), "first-min-host-recorded"),
                                  ((0, 1), "first-min-mixed-recorded")):
            t = canceled()
            for index, origin in enumerate(origins):
                if origin == 1:
                    t["observations"]["a"][index] = [1, False, 70, 9, [11, 2], 76]
            self.assertEqual(journal.assess(wire(t), A, expected_version=2)["question"], question)
        t = canceled()
        t["observations"]["r"] = [1, None]
        self.assertEqual(journal.assess(wire(t), A, expected_version=2)["question"], "bound-context-incomplete")
        for version, expected, label in ((4, 2, "unsupported"), (2, 1, "unexpected-version")):
            t = terminal()
            t["version"] = version
            result = journal.assess(wire(t), A, expected_version=expected)
            self.assertEqual(result["receipt_shape"], label)
            self.assertEqual(result["review_disposition"], "blocked")

class Capture(unittest.TestCase):
    def test_lexical_slot_association_and_scrambled_negative(self):
        data = records()
        t = wire(terminal())
        names = sorted(range(len(data)), key=lambda n: str(n)+".json")
        slots = {}
        mappings = {}
        source = [dict(path="ActivationDiagnostics\\"+A, directory=True, attributes=16)]
        for slot, sequence in enumerate(names):
            name = f"journal-slot-{slot:02}.bin"
            slots[name] = data[sequence]
            mappings[sequence] = dict(sequence=sequence, slot=name, bytes=len(data[sequence]),
                                      sha256=hashlib.sha256(data[sequence]).hexdigest())
            source.append(dict(path=f"ActivationDiagnostics\\{A}\\{sequence}.json",
                               directory=False, attributes=32, bytes=len(data[sequence])))
        marker = "Jobs\\"+J+"\\accepted"
        source.append(dict(path=marker, directory=True, attributes=16))
        terminal_item = dict(slot="journal-terminal.bin", bytes=len(t), sha256=hashlib.sha256(t).hexdigest())
        slots["journal-terminal.bin"] = t
        siblings = [dict(name=f"DesktopPdfJournal-{A}.json", metadata=dict(directory=False, attributes=32, bytes=len(t)))]
        raw = dict(rows=source, metadata_errors=[], enumerated_directories=[marker], terminal_siblings=siblings)
        observed = dict(cache=source, terminal_siblings=siblings, grace_complete=True)
        capture = dict(collection_attempted=True, activation=A, records=[mappings[i] for i in range(len(data))],
                       terminal=terminal_item, unresolved=[], deferral_return_proved=False,
                       process_exit_proved=False, physical_unload_proved=False)
        grace = dict(fixed_grace_ms=3000, first_completion_ms=4000, observed_ms=7001, physical_finish_guaranteed=False)
        facts = dict(old_jobs=[], old_registrations=[], old_activations=[])
        def check():
            return journal_capture.check(capture, raw, observed, grace, facts, A, J, slots, expected_version=2)
        self.assertTrue(check()["published_through_finish"])
        facts["old_jobs"] = [J]
        with self.assertRaisesRegex(ValueError, "fresh producer"):
            check()
        facts["old_jobs"] = []
        saved = copy.deepcopy(capture["records"][0])
        capture["records"][0]["slot"] = "journal-slot-99.bin"
        slots["journal-slot-99.bin"] = slots[saved["slot"]]
        with self.assertRaisesRegex(ValueError, "evidence slot"):
            check()
        capture["records"][0] = saved
        slots.pop("journal-slot-99.bin")
        selected = capture["records"][1]
        original_bytes, original_hash = slots[selected["slot"]], selected["sha256"]
        bad_record = json.loads(original_bytes)
        bad_record["sequence"] = 7
        slots[selected["slot"]] = wire(bad_record)
        selected["sha256"] = hashlib.sha256(slots[selected["slot"]]).hexdigest()
        with self.assertRaisesRegex(ValueError, "diagnostic source binding"):
            check()
        slots[selected["slot"]], selected["sha256"] = original_bytes, original_hash
        original = capture["records"]
        capture["records"] = [mappings[i] for i in (3, 4, 17, 2, 5, 8, 9, 6, 7, 10, 11, 0, 1, 12, 15, 16, 13, 14, 18, 19)]
        with self.assertRaisesRegex(ValueError, "contiguous numeric"):
            check()
        capture["records"] = original
        for bad in (0x400, 0x410):
            source[-1]["attributes"] = bad
            with self.assertRaises(ValueError):
                check()
        source[-1]["attributes"] = 16
        source.append(dict(path=marker+"\\unexpected", directory=False, attributes=32))
        with self.assertRaises(ValueError):
            check()


if __name__ == "__main__":
    unittest.main()
