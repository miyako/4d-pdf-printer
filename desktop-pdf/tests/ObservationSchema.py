"""Finite R3 component alternatives against production consumers, not Task executions."""
import argparse
import itertools
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import journal as j
from Journal import A, terminal, wire

N, U, P, H, K, Q = 9007199254740991, 0xffffffff, 0xffffffff, 0xffffffff, 65535, 31
rows = {}
def branch(name, values, check):
    values = list(values)
    for value in values:
        check(value)
    rows[name] = [len(wire(value)) for value in values]
    return max(values, key=lambda value: len(wire(value)))
def phases(payload, first=False):
    if first:
        return [[0, False, None], [1, False, None], [1, True, None],
                [2, False, payload], [2, True, payload]]
    return [[0, None], [1, None], [2, payload]]
def cell(name, payload, check, first=False):
    return branch(name, phases(payload, first), lambda value: j.cell(value, check, first))
def finish_values(caller, gate=None):
    for cas, expected in ([(1, 0)]+[(2, r) for r in range(1, 5)] if caller < 3 else
                          [(2, r) for r in range(1, 5)] if caller == 3 else [(0, None)]):
        for won in (False, True) if gate is None else (gate,):
            yield [N, P, j.REQUESTS[caller], caller == 4, cas, expected, won]
def operation(value):
    if value is not None:
        j.seq(value, 3)
        j.optional_number(value[0], K, 1)
        j.number(value[1], 9)
        j.optional_number(value[2], Q)
def root_check(key, value):
    t = terminal()
    t["accepted_count"] = 32
    if key == "optional-event-pair":
        t.update(outstanding_io_at_decision=True, inflight=dict(inflight))
        t["inflight"].update(sequence=value[0], checkpoint=value[1])
    elif key in ("stage-string", "temporary-leaf", "inflight.cancel-requested"):
        t.update(outstanding_io_at_decision=True, inflight=dict(inflight))
        t["inflight"][{"stage-string": "stage", "temporary-leaf": "temporary_leaf",
                       "inflight.cancel-requested": "cancel_requested"}[key]] = value
    elif key in ("inflight", "outstanding"):
        present = value if key == "outstanding" else value is not None
        t.update(outstanding_io_at_decision=present, inflight=dict(inflight) if present else None)
    elif key == "admission-loss":
        t.update(admission_loss=value, rejected_before_seal=int(value))
    elif key.startswith("deferral-"):
        t.update(deferral_acquired=value, deferral_release_planned=value)
    else:
        field = {"first-failure": "first_failure", "finalize-reason": "finalize_reason",
                 "drain-state": "drain_state", "last-published": "last_published_sequence",
                 "finish-sequence": "finish_sequence"}[key]
        t[field] = value
        if key == "finalize-reason":
            t["observations"]["w"][2][1][1] = j.v1.REASONS.index(value)
        if key == "drain-state" and value == "canceled":
            t["observations"]["a"] = [[0, False, N, P, U, N]]*2
    j.general_v2(wire(t), A)

for name, lo, hi in (("N", 0, N), ("U", 0, U), ("P", 1, P), ("H", 0x80000000, H),
                     ("K", 1, K), ("Q", 0, Q), ("count", 0, 32)):
    branch("primitive."+name, [lo, hi], lambda value, lo=lo, hi=hi: j.number(value, hi, lo))
framework = [N, P, U]
cell("c.framework", framework, lambda value: j.cancel(value, 0), True)
host = branch("c.host.mask", ([N, P, m, v] for m, v in j.HOST), lambda value: j.cancel(value, 1))
cell("c.host", host, lambda value: j.cancel(value, 1), True)
r = branch("r.payload", ([o, b, N, P, N, (4, 4, 1, 3, 2)[o]]
                         for o, b in itertools.product(range(5), (True, False))), j.reason)
cell("r", r, j.reason)
applied = []
for name in ("a.first", "a.minimum"):
    values = [[0, b, N, P, U, N] for b in (True, False)]
    values += [[1, b, N, P, [m, v], N] for b, (m, v) in itertools.product((True, False), j.HOST)]
    applied.append(branch(name, values, j.applied))
branch("a", [[None, None], applied],
       lambda value: [j.applied(item) for item in value if item is not None])
for caller in range(5):
    f = branch(f"f[{caller}].payload", finish_values(caller), lambda value, c=caller: j.finish(value, c))
    cell(f"f[{caller}]", f, lambda value, c=caller: j.finish(value, c), True)
winners = []
for caller in range(5):
    values = [[caller, owns, value] for value in finish_values(caller, True) for owns in (True, False)]
    branch(f"w.head.caller[{caller}]", values, j.winner)
    winners += values
w = branch("w.head.payload", winners, j.winner)
cell("w.head", w, j.winner)
cell("w.begin", [N, P], lambda value: j.stamp(*j.seq(value, 2)))
passed = branch("w.reason.payload", ([N, r] for r in range(5)),
                lambda value: (j.number(value[0]), j.number(value[1], 4)))
cell("w.reason", passed, lambda value: (j.number(value[0]), j.number(value[1], 4)))
ops = [[key, stage, event] for key, stage, event in itertools.product((None, K), range(10), (None, Q))]
branch("i", [None]+ops, operation)
for route in range(3):
    heads = [[N, P, *op, None] for op in ops] if route < 2 else [[N, P, 0, None, None, [N, P, 0]]]
    head = branch(f"x[{route}].head.payload", heads, lambda value, r=route: j.api_head(value, r))
    cell(f"x[{route}].head", head, lambda value, r=route: j.api_head(value, r), True)
    for phase in ("return", "await"):
        if route == 2 and phase == "await":
            branch("x[2].await", [[0, None]], lambda value: j.need(value == [0, None], "no launch await"))
        else:
            out = branch(f"x[{route}].{phase}.payload", [[N, P, 0, None], [N, P, 1, H]], j.outcome)
            cell(f"x[{route}].{phase}", out, j.outcome)
event = [Q, max(j.v1.POINTS, key=len)]
inflight = dict(sequence=event[0], checkpoint=event[1], stage=max(j.v1.STAGES, key=len),
                temporary_leaf="pending-"+"f"*32, cancel_requested=False)
failure = dict(sequence=event[0], checkpoint=event[1], stage=max(j.v1.STAGES, key=len),
               code_domain="hresult", code_u32=H)
root_values = {
    "optional-event-pair": [[None, None]]+[[Q, point] for point in j.v1.POINTS],
    "stage-string": j.v1.STAGES, "temporary-leaf": ["", "pending-"+"f"*32],
    "inflight.cancel-requested": [True, False], "inflight": [None, inflight],
    "first-failure": [None, failure], "finalize-reason": j.v1.REASONS, "drain-state": j.v1.STATES,
    "last-published": [None, Q], "finish-sequence": [None, Q], "admission-loss": [True, False],
    "outstanding": [True, False], "deferral-acquired": [True, False], "deferral-planned": [True, False]}
for key, values in root_values.items():
    branch("root."+key, values, lambda value, key=key: root_check(key, value))
assert len(rows) == 67 and sum(map(len, rows.values())) == 469
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--ledger", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    ledger = json.loads(args.ledger.read_bytes())["discriminated_branches"]
    assert {row["node"] for row in ledger} == set(rows)
    for row in ledger:
        assert sorted(rows[row["node"]]) == sorted(a["bytes"] for a in row["alternatives"]), row["node"]
    with args.output.open("x") as stream:
        json.dump(dict(component_nodes=67, alternatives=469, production_validators=True,
                       full_task_executions=False, byte_lengths=rows), stream, indent=2)
    print("PASS 67 finite component nodes/469 alternatives; production consumers, R3 byte-ledger parity.")
