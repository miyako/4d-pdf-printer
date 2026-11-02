"""Actual portable C++ encoder output; hypothetical fixtures, never live Task evidence."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import journal
from Journal import A, J, records

parser = argparse.ArgumentParser()
parser.add_argument("--producer", type=Path, required=True)
parser.add_argument("--majorant", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(exist_ok=False)
majorant = json.loads(args.majorant.read_bytes())
def encoded(value):
    return json.dumps(value, separators=(",", ":"), ensure_ascii=True).encode("ascii")
assert len(encoded(majorant)) == 2300
report = []
for variant in ("maximum", "bound", "unbound", "32"):
    raw = subprocess.check_output([str(args.producer), "--observation-wire-"+variant])
    activation = "f"*32 if variant == "maximum" else A
    value = journal.general_v2(raw, activation)
    assert len(raw) <= 2300
    terms = {}
    for key, item in value.items():
        length = len(encoded(item))
        maximum = len(encoded(majorant[key]))
        assert length <= maximum, (key, length, maximum)
        terms[key] = dict(actual=length, component_majorant=maximum)
    if variant == "bound":
        assert journal.journal_admission(records(), raw, A, J, (), expected_version=2)["published_through_finish"]
    if variant == "unbound":
        assert value["job"] == "" and value["drain_state"] == "drained" and value["finalize_reason"] == "normal"
        assert journal.assess(raw, A, expected_version=2)["decision_admission"] == "not-evaluable"
        assert journal.assess(raw, A, expected_version=2, job=J)["decision_admission"] == "fail"
    (args.output/(variant+".json")).write_bytes(raw)
    report.append(dict(variant=variant, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                       strict_general_v2_shape=True, terms=terms))
(args.output/"proof.json").write_bytes(json.dumps(dict(
    source="actual portable production ReceiptWire/ObservationWire/Journal/PublicationState",
    hypothetical=True, live_task=False, native_windows=False, bound=2300, backend_cap=4096,
    margin=1796, cases=report), indent=2).encode()+b"\n")
print("PASS actual C++ emitted v2 bytes -> strict general/bound/unbound consumer and per-field2300 majorant.")
