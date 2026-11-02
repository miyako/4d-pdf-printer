"""Explicit PS7/model or native PS5.1 synthetic-file bridge; never a print operator."""
import argparse
import copy
import hashlib
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import journal
import journal_capture
import journal_v1 as v1
from Journal import A, J, POINTS, wire

OPERATOR_SHA = "3dc3e83da6f8f36d7d70cf2c7105fffe9cb1ed48591607f7d64b3581fe3e8822"
CASES = {"positive": None, "gap": "Journal numeric sequence gap",
         "duplicate": "Duplicate journal source sequence", "reparse": "New namespace reparse point",
         "staging": "Unknown/staging/missing journal artifact", "copy-fault": "Injected guarded read failure"}
PS_SCRIPT = Path(__file__).with_suffix(".ps1")


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def save(path, value):
    with path.open("xb") as stream:
        stream.write(json.dumps(value, indent=2, allow_nan=False).encode()+b"\n")


def execute(command, output, stem):
    try:
        completed = subprocess.run(command, capture_output=True)
    except OSError as error:
        save(output/(stem+".process.json"), dict(command=command, started=False, exit_code=None, error=str(error)))
        raise
    (output/(stem+".stdout.bin")).write_bytes(completed.stdout)
    (output/(stem+".stderr.bin")).write_bytes(completed.stderr)
    save(output/(stem+".process.json"), dict(command=command, started=True, exit_code=completed.returncode))
    return completed


def must_reject(action, phrase):
    try:
        action()
    except ValueError as error:
        if phrase not in str(error):
            raise
        return str(error)
    raise RuntimeError("expected precise rejection: "+phrase)


def run(args):
    if args.boundary == "native-windows-ps51":
        if sys.platform != "win32":
            raise ValueError("native boundary requires Windows; no subprocess fallback")
        if args.architecture is None:
            raise ValueError("native boundary requires explicit architecture")
    elif args.architecture is not None:
        raise ValueError("PS7/model cannot claim native architecture")
    for path in (args.producer, args.operator, args.powershell_path, args.output):
        if not path.is_absolute():
            raise ValueError("explicit absolute tool/input/output paths required")
    if digest(args.operator.read_bytes()) != OPERATOR_SHA:
        raise ValueError("frozen operator pin changed")
    interpreter = args.powershell_path.resolve(strict=True)
    if not interpreter.is_file() or not args.producer.is_file():
        raise ValueError("explicit preinstalled interpreter/producer files required")
    output = args.output.resolve()
    if "packages" in {part.lower() for part in output.parts}:
        raise ValueError("synthetic root must be outside package state")
    output.mkdir(exist_ok=False)
    pinned_paths = (Path(__file__).resolve(), PS_SCRIPT, args.operator, args.producer, interpreter)
    pinned_inputs = {str(path): digest(path.read_bytes()) for path in pinned_paths}
    save(output/"execution-inputs.json", dict(boundary=args.boundary, architecture=args.architecture,
         inputs=pinned_inputs, synthetic_context=True, native_ps51_run=args.boundary == "native-windows-ps51"))
    source = output/"input"
    root = source/"DesktopPdf"
    folder = root/"ActivationDiagnostics"/A
    folder.mkdir(parents=True)
    (root/"Jobs"/J/"accepted").mkdir(parents=True)
    producer = execute([str(args.producer), "--observation-wire-32"], output, "producer")
    if producer.returncode:
        raise RuntimeError("producer failed; original streams/exit retained")
    terminal = producer.stdout
    journal.general_v2(terminal, A)
    points = POINTS[:-1] + ["session-before"]*12 + ["finish"]
    expected_files = {}
    for index, point in enumerate(points):
        value = dict(schema="4d-pdf-printer.activation-diagnostic", version=1, activation=A, sequence=index,
                     checkpoint=point, hresult=0, process=100, thread=9,
                     tick_ms=101 if point == "finish" else index+1, job=J if point == "job-created" else "",
                     identity_api="GetCurrentApplicationUserModelId", identity_status="available",
                     identity_error=0, identity_validation_error=0,
                     app_user_model_id="Miyako.DesktopPdfIntegration_hjw4whvmzr08m!SaveAs")
        raw = wire(value)
        path = folder/f"{index}.json"
        path.write_bytes(raw)
        expected_files[index] = dict(bytes=len(raw), sha256=digest(raw))
    terminal_path = source/f"DesktopPdfJournal-{A}.json"
    terminal_path.write_bytes(terminal)
    rows, directories = [], []
    for path in sorted(root.rglob("*"), key=lambda p: str(p.relative_to(root))):
        name = str(path.relative_to(root)).replace("/", "\\")
        rows.append(dict(path=name, directory=path.is_dir(), attributes=16 if path.is_dir() else 32,
                         bytes=0 if path.is_dir() else path.stat().st_size))
        if path.is_dir():
            directories.append(name)
    siblings = [dict(name=terminal_path.name, metadata=dict(directory=False, attributes=32, bytes=len(terminal)))]
    raw = dict(root=str(root), cache=str(source), rows=rows, metadata_errors=[],
               enumerated_directories=directories, terminal_siblings=siblings, transactions=[])
    context = dict(schema="4d-pdf-printer.collector-test-input", version=1, fixture=str(source),
                   boundary=args.boundary, activation=A, job=J, expected_paths=[r["path"] for r in rows],
                   model_inventory=raw if args.boundary == "ps7-model" else None)
    context_path = output/"fixture-input.json"
    save(context_path, context)
    context_sha = digest(context_path.read_bytes())
    base = [str(interpreter), "-NoLogo", "-NoProfile", "-File", str(PS_SCRIPT),
            "-Fixture", str(source), "-FrozenOperator", str(args.operator), "-Boundary", args.boundary,
            "-InputSha256", context_sha]
    if args.architecture is not None:
        base += ["-Architecture", args.architecture]
    results = []
    for case, expected_error in CASES.items():
        out = output/case
        completed = execute(base+["-Output", str(out), "-Case", case], output, case)
        if completed.returncode:
            raise RuntimeError(case+" subprocess failed; original streams/exit retained")
        capture = v1.wire((out/"journal-capture.json").read_bytes(), 65536)
        report = v1.wire((out/"fixture-result.json").read_bytes(), 65536)
        provenance = v1.wire((out/"fixture-provenance.json").read_bytes(), 65536)
        if len(provenance["functions"]) != 18 or report["boundary"] != args.boundary:
            raise RuntimeError("exact generic imports/boundary provenance missing")
        if report["input_metadata_observed_native"] != (args.boundary == "native-windows-ps51"):
            raise RuntimeError("false native metadata label")
        if Path(report["runtime"]["image"]).resolve(strict=True) != interpreter:
            raise RuntimeError("executed interpreter differs from explicit selection")
        if report["runtime"]["image_sha256"] != digest(interpreter.read_bytes()):
            raise RuntimeError("interpreter image hash changed")
        if case == "positive":
            if report["error"] is not None:
                raise RuntimeError("positive capture error: "+report["error"])
            final = v1.wire((out/"final-raw-inventory.json").read_bytes(), 1024*1024)
            jr = v1.wire((out/"journal-raw-inventory.json").read_bytes(), 1024*1024)
            slots = {p.name: p.read_bytes() for p in out.glob("journal-*.bin")}
            facts = dict(old_jobs=[], old_registrations=[], old_activations=[])
            grace = dict(fixed_grace_ms=3000, first_completion_ms=4000, observed_ms=7001,
                         physical_finish_guaranteed=False)
            def check(c=capture, s=slots):
                return journal_capture.check(c, final, jr, grace, facts, A, J, s, expected_version=2)
            answer = check()
            if not answer["published_through_finish"] or [m["sequence"] for m in capture["records"]] != list(range(32)):
                raise RuntimeError("actual capture mapping/strict admission failed")
            for mapping in capture["records"]:
                if {k: mapping[k] for k in ("bytes", "sha256")} != expected_files[mapping["sequence"]]:
                    raise RuntimeError("original source-slot-byte-SHA association changed")
            bad = copy.deepcopy(capture)
            bad["records"][:18] = [capture["records"][n] for n in
                                  (3, 4, 17, 2, 5, 8, 9, 6, 7, 10, 11, 0, 1, 12, 15, 16, 13, 14)]
            negatives = [must_reject(lambda: check(bad), "contiguous numeric")]
            corrupted = dict(slots)
            corrupted[capture["records"][1]["slot"]] += b" "
            negatives.append(must_reject(lambda: check(s=corrupted), "exact bytes/SHA"))
            modified = json.loads(terminal)
            modified["job"] = ""
            unbound = wire(modified)
            if journal.assess(unbound, A, expected_version=2)["decision_admission"] != "not-evaluable":
                raise RuntimeError("unbound activation promoted")
            negatives.append(must_reject(lambda: journal.journal_admission(
                [slots[m["slot"]] for m in capture["records"]], unbound, A, J, (), expected_version=2),
                "expected job binding"))
            modified["version"] = 1
            negatives.append(must_reject(lambda: journal.general_v2(wire(modified), A), "unexpected terminal version"))
            save(out/"strict-checks.json", dict(positive=True, negative_errors=negatives,
                 context="SYNTHETIC records/identity/producer/grace/history, not real print admission"))
        elif type(report["error"]) is not str or expected_error not in report["error"]:
            raise RuntimeError(case+" did not retain the expected precise rejection")
        if case == "gap" and capture["terminal"] is None:
            raise RuntimeError("independent terminal lost on gap")
        if case == "reparse" and report["read_entries"]:
            raise RuntimeError("reparse case invoked guarded reader")
        if any("pending-never-open" in p for p in report["read_entries"]):
            raise RuntimeError("staging case invoked guarded reader")
        if not (out/"input-raw-inventory.json").exists() or not (out/"journal-raw-inventory.json").exists():
            raise RuntimeError("raw metadata preservation missing")
        results.append(dict(case=case, expected_result=True, report=report))
    if args.boundary == "ps7-model":
        policy = execute(base+["-Output", str(output/"policy-unused"), "-PolicyChecksOnly"], output, "policy")
        if policy.returncode:
            raise RuntimeError("pure policy checks failed")
        save(output/"policy-checks.json", json.loads(policy.stdout.decode("utf-8-sig")))
    if any(digest(Path(path).read_bytes()) != sha for path, sha in pinned_inputs.items()):
        raise RuntimeError("execution source/tool/input bytes changed during fixture")
    summary = dict(results=results, records=32, strict_version=2, boundary=args.boundary,
                   producer_sha256=digest(args.producer.read_bytes()), production_cpp_wire_sha256=digest(terminal),
                   selected_interpreter=str(args.powershell_path), resolved_interpreter=str(interpreter),
                   native_windows=args.boundary == "native-windows-ps51", native_ps51=args.boundary == "native-windows-ps51",
                   metadata="observed owned Windows files" if args.boundary == "native-windows-ps51" else "MODELED Windows metadata",
                   negative_reparse_staging_read_fault="INJECTED, not physical filesystem faults",
                   producer_identity_grace_history="SYNTHETIC", pypdf_used=False, real_print=False,
                   technical_success_promoted=False, physical_stop_proved=False)
    save(output/"summary.json", summary)
    print("PASS actual collector/serialized32/strict v2;five precise fault/finalizer cases; boundary="+args.boundary)
    print("Producer/identity/grace/history SYNTHETIC; injected negatives are not physical reparse/share-denial evidence.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--producer", type=Path, required=True)
    parser.add_argument("--operator", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--powershell-path", type=Path, required=True)
    parser.add_argument("--boundary", choices=("ps7-model", "native-windows-ps51"), required=True)
    parser.add_argument("--architecture", choices=("x64", "ARM64"))
    args = parser.parse_args()
    try:
        run(args)
    except Exception as error:
        print("Fixture FAILED (not native proof): "+str(error), file=sys.stderr)
        raise
