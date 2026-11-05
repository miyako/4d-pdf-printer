"""Portable source/parser models only; no WinRT calls, concurrency or native evidence."""
import copy
import itertools
import unittest
from pathlib import Path

from Journal import A, J, canceled, journal, records, terminal, wire

ROOT = Path(__file__).resolve().parents[1]


def receipt(base=None):
    value = copy.deepcopy(base if base is not None else terminal())
    value["version"] = 3
    value["observations"]["v"] = 2
    value["observations"]["j"] = [[[0, False, None], [0, None]], [0, None]]
    return value


def call(status=0, failure=None):
    return [[2, False, [60, 9, status]], [2, [65, 9, int(failure is not None), failure]]]


class CompleteJob(unittest.TestCase):
    def assess(self, value):
        return journal.assess(wire(value), A, expected_version=3)

    def test_explicit_versions_and_original_admission(self):
        old = terminal()
        self.assertEqual(journal.general_v2(wire(old), A), old)
        new = receipt()
        self.assertEqual(journal.general_v3(wire(new), A), new)
        self.assertTrue(journal.journal_admission(records(), wire(new), A, J, (),
                                                expected_version=3)["published_through_finish"])
        for value, expected in ((old, 3), (new, 2)):
            with self.assertRaises(ValueError):
                journal.terminal_shape(wire(value), A, expected)
        new["version"] = 2
        with self.assertRaises(ValueError):
            journal.general_v2(wire(new), A)
        new["version"] = 3
        new["observations"]["v"] = 1
        with self.assertRaises(ValueError):
            journal.general_v3(wire(new), A)
        old.pop("observations")
        old["version"] = 1
        self.assertEqual(journal.terminal_shape(wire(old), A, 1, J), old)

    def test_status_return_failure_and_not_attempted(self):
        value = receipt()
        result = self.assess(value)
        self.assertEqual(result["complete_job"]["snapshot_phase"], "not-attempted-at-entry-read")
        for status, failure in itertools.product(range(3), (None, 0x80004005, 0xffffffff)):
            value["observations"]["j"][0] = call(status, failure)
            result = self.assess(value)
            observed = result["complete_job"]
            self.assertEqual(observed["attempted_status"], status)
            self.assertEqual(observed["snapshot_phase"], "returned" if failure is None else "failed")
            self.assertEqual(observed["outcome"][3], failure)
            self.assertEqual(observed["os_call_order"], "not-established")
            self.assertFalse(result["technical_success_promoted"])

    def test_first_framework_sample_does_not_upgrade_canceled(self):
        value = receipt(canceled())
        value["observations"]["c"][0][2][2] = 7
        for applied in value["observations"]["a"]:
            applied[4] = 7
        value["job"] = J
        before = call()
        before[1] = [0, None]
        value["observations"]["j"] = [call(), [2, [[70, 9], [71, 9], before]]]
        result = journal.assess(wire(value), A, expected_version=3, job=J, raw_records=records())
        self.assertEqual(result["receipt_shape"], "valid-v3")
        self.assertEqual(result["decision_admission"], "fail")
        self.assertEqual(result["framework_reasons_raw"], [7, 7, 7])
        self.assertEqual(result["complete_job"]["framework_sample_phase"], "entry-recorded-return-unobserved")
        self.assertEqual(result["complete_job"]["snapshot_phase"], "returned")
        self.assertFalse(result["technical_success_promoted"])

    def test_nonatomic_read_models_do_not_infer_execution_order(self):
        # Return read precedes entry read: an absent return does not mean the OS call is in progress.
        for status in range(3):
            value = receipt()
            value["observations"]["j"][0] = [call(status)[0], [0, None]]
            result = self.assess(value)
            self.assertEqual(result["complete_job"]["snapshot_phase"], "entry-recorded-return-unobserved")
            self.assertTrue(result["sample_flags"] & 256)
        value = receipt(canceled())
        # Equal millisecond ticks cannot order callback entry, call return and sample reads.
        value["observations"]["j"] = [call(), [2, [[70, 9], [70, 9], call()]]]
        result = self.assess(value)
        self.assertEqual(result["complete_job"]["framework_sample_phase"], "returned")
        self.assertEqual(result["complete_job"]["os_call_order"], "not-established")

    def test_publishing_and_repeated_cells(self):
        for first, returned in (
            ([1, False, None], [0, None]),
            (call()[0], [1, None]),
            ([1, True, None], [0, None]),
            ([2, True, [60, 9, 0]], call()[1]),
        ):
            value = receipt()
            value["observations"]["j"][0] = [first, returned]
            result = self.assess(value)
            self.assertEqual(result["receipt_shape"], "valid-v3")
            expected = "repeated-ambiguous" if first[1] else "partial"
            self.assertEqual(result["complete_job"]["snapshot_phase"], expected)
            self.assertTrue(result["sample_flags"] & (512 if first[1] else 1))
        value = receipt(canceled())
        for sample, expected in (([0, None], "not-recorded"), ([1, None], "partial")):
            value["observations"]["j"][1] = sample
            result = self.assess(value)
            self.assertEqual(result["complete_job"]["framework_sample_phase"], expected)
            self.assertTrue(result["sample_flags"] & 256)

    def test_negative_shapes_errors_and_immutable_copy_binding(self):
        good = receipt(canceled())
        good["observations"]["j"] = [call(), [2, [[70, 9], [71, 9], call()]]]
        journal.general_v3(wire(good), A)
        mutations = (
            lambda j: j.__setitem__(0, [call()[0], [2, [65, 9, 0, 0]]]),
            lambda j: j[0][0][2].__setitem__(2, 3),
            lambda j: j[0][0][2].__setitem__(2, True),
            lambda j: j[0][1].__setitem__(1, [65, 9, 1, 0]),
            lambda j: j[0][1].__setitem__(1, [65, 9, 1, 0x7fffffff]),
            lambda j: j[0].__setitem__(0, [0, False, None]),
            lambda j: j[0][1][1].__setitem__(0, 59),
            lambda j: j[1][1][0].__setitem__(0, 69),
            lambda j: j[1][1][1].__setitem__(0, 69),
            lambda j: j[1][1][1].__setitem__(1, 10),
            lambda j: j[1][1][2][0][2].__setitem__(2, 1),
            lambda j: j[1][1][2][1][1].__setitem__(3, 0x80004005),
            lambda j: j[0].__setitem__(1, [1, None]),
            lambda j: j[1][1][2][0].__setitem__(1, True),
        )
        for mutate in mutations:
            value = copy.deepcopy(good)
            mutate(value["observations"]["j"])
            with self.assertRaises(ValueError):
                journal.general_v3(wire(value), A)
        for bad in (None, {}, [], [0], [True, None], [2, None]):
            value = receipt()
            value["observations"]["j"] = bad
            with self.assertRaises(ValueError):
                journal.general_v3(wire(value), A)
        value = receipt()
        value["observations"]["j"][1] = good["observations"]["j"][1]
        with self.assertRaises(ValueError):
            journal.general_v3(wire(value), A)

    def test_wire_fail_closed_and_conservative_capacity(self):
        n, p = 9007199254740991, 0xffffffff
        maximum = [[2, False, [n, p, 2]], [2, [n, p, 1, p]]]
        # Every nullable/publishing alternative is shorter; integer domains bound each position.
        extra = {"j": [maximum, [2, [[n, p], [n, p], maximum]]]}
        self.assertLessEqual(len(wire(extra)), 1000)
        self.assertLessEqual(2300 + len(wire(extra)), 3300)
        raw = wire(receipt())
        for invalid in (raw + b"\n", b"\xef\xbb\xbf" + raw,
                        raw.replace(b'"v":2', b'"v":2,"v":2'),
                        raw.replace(b'"j":', b'"\\u006a":'), b" " * 4097):
            with self.assertRaises(ValueError):
                journal.general_v3(invalid, A)

    def test_source_boundary_and_no_policy_changes(self):
        task = (ROOT / "Task.cpp").read_text()
        observations = (ROOT / "ActivationObservations.h").read_text()
        wire_source = (ROOT / "ObservationWire.h").read_text()
        primary = task.split("bool completeOwned = false;", 1)[1].split("        if (job)", 1)[0]
        self.assertLess(primary.index("CompleteEnter("), primary.index("args.CompleteJob(status);"))
        self.assertLess(primary.index("args.CompleteJob(status);"), primary.index("CompleteReturn("))
        self.assertLess(primary.index("CompleteReturn("), primary.index("Point::CompleteReturned"))
        catch = primary.split("} catch (...) {", 1)[1]
        self.assertLess(catch.index("primary = to_hresult();"), catch.index("CompleteReturn("))
        self.assertLess(catch.index("CompleteReturn("), catch.index("Point::CompleteFailure"))
        self.assertEqual(task.count("args.CompleteJob("), 2)
        callback = task.split("context.entry = activation::ObservationStamp();", 1)[1].split(
            "self->Trace(activation::Point::Canceled)", 1)[0]
        self.assertLess(callback.index("ReadComplete()"), callback.index("auto sampled"))
        self.assertIn("CompleteCanceled(context, complete, sampled)", callback)
        read = observations.split("CompleteSample ReadComplete()", 1)[1].split("void CompleteCanceled", 1)[0]
        self.assertLess(read.index("completeReturned.Read()"), read.index("completeEntry.Read()"))
        snapshot = observations.split("Snapshot Read()", 1)[1]
        self.assertLess(snapshot.index("completeCancellation.Read()"), snapshot.index("ReadComplete()"))
        self.assertIn('{"v", version == 2 ? "1" : version == 3 ? "2" : version == 4 ? "3" : "4"}', wire_source)
        self.assertIn("std::uint8_t version = 5", (ROOT / "ActivationDiagnostics.h").read_text())
        self.assertLess(primary.index("Point::CompleteBefore"), primary.index("FlushBeforeComplete()"))
        self.assertLess(primary.index("FlushBeforeComplete()"), primary.index("CompleteEnter("))
        self.assertEqual(task.count("FlushBeforeComplete()"), 1)


if __name__ == "__main__":
    unittest.main()
