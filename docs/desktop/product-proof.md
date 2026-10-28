# Product proof harness (milestone A)

**The own-code harness is Windows-native compiled and unit-checked; the actual
product experiment is unexecuted. No product size, fidelity or repeated-printing
result is claimed.** Normal-desktop execution and a matching **locally
available** renderer require separate creator approval. No installation,
signing, trust change, runtime download or dependency build is authorized by this
document.
The creator separately authorized the initial own-code-only compile/unit batch
and **one narrow reporting-correction follow-up**: manual dispatch only, no
automatic push/PR runs or historical activation steps. Neither batch executes
this document's Windows printing/worker experiment.

This is a standalone original-input experiment, **not a Save-As-only usable
printer**. Silent application-supplied output and the interactive endpoint both
remain mandatory for the later first usable prototype. Existing activation
failures and exact source SHAs remain in [validation.md](validation.md).

## Historical initial compile/unit checkpoint

The initial authorized manual batch
[40735110227](https://github.com/miyako/4d-pdf-printer/actions/runs/40735110227)
passed at source `6cf816ee4a80cdc2319f77ecb29cbdaea7954a45`:

| Native target | Actual observations |
|---|---|
| x64, job122981444543 | Own-code MSVC/SDK26100 build and PE check; one CTest protocol/framing/hash unit; six PowerShell consent/subprocess tests; 45 Python tests discovered, 44 passed/one actual-Poppler smoke skipped. |
| ARM64, job122981445082 | Same own-code build/PE/native CTest and helper units on native Windows ARM64; same Python 44 passed/one skipped. |

[x64 build receipt](evidence/40735110227-x64.json) and
[ARM64 build receipt](evidence/40735110227-arm64.json) contain exact own-code
executable hashes, source identity and `sourceDirty:false`. Artifacts are
JSON/text evidence only, not downloadable proof executables or renderer trees.
The workflow is manual-only; the source commit's skip-ci marker suppressed
automatic push/PR builds of the older cumulative printer diff. At that checkpoint no second batch,
MIDL/MSIX creation, renderer/dependency build/cache, binary upload, signing/trust,
installation, GDI submission or PSA activation occurred.

Local macOS validation ran all 45 comparator tests, including a real Poppler
interoperability case with an original stdlib-generated Type3 glyph and vector
primitive. Identical PDFs passed the automated content/render checks but had
ratio1.0, which the historical code classified as a failed size gate. That
classification was incorrect: the numeric reference was unapproved, not a
product acceptance criterion. This historical source/receipt does **not** test
the reporting correction below. The run is tool interoperability, **not a
Microsoft comparison or product improvement**. Six local consent/subprocess
units and the existing 36 build-support cases also passed.

Not executed: original Windows GDI/EMF fixture generation/replay, old diagnostic
control, Microsoft PDF baseline, actual worker launch/repetitions/faults through
this supervisor, resource observations, manual fidelity review or either new
printing endpoint. Compiling Job Object code is not execution evidence for its
worker lifecycle. No result here closes the historical activation/service gates.

## Reporting correction

Size/raster/observer-growth references are explicitly **unapproved observations,
not pass/fail gates**. A complete valid measurement returns `result:completed`
and exit0, with `productUsable:false` and manual review required. Exit0 means
measurement completion, **not product success**, a size/lifecycle claim, host-run
approval or permission to progress to milestone B. Identical valid PDFs report
ratio1.0/no size improvement; larger PDFs and viewer-dependent pixel differences
remain visible. Dimensionally different rasters are explicitly uncomparable,
with null difference metrics, not invented zero differences.

Missing content, invalid PDFs/tool outputs, protocol/tool errors, surviving
processes, unsafe filesystem/publication state and actual resource/deadline
violations remain failures. Only actually safe, complete repetitions permit
bounded fault observations and downstream comparison. A missed numeric reference
does not stop them. Native units exercise that same continuation decision;
Python and subprocess units distinguish reference misses from real errors.
The follow-up below tests its own source; historical `6cf816e` receipts remain
unchanged.

### Exact follow-up compile/unit checkpoint

The **one** authorized reporting-correction follow-up
[40737957800](https://github.com/miyako/4d-pdf-printer/actions/runs/40737957800)
passed at source **`312e1c830e0a775656db89fa7096349b357bcb1d`**:

| Native target | Actual observations |
|---|---|
| x64, job122991497652 | Own-code MSVC/SDK26100 build and PE check; one native CTest covering protocol/framing/hash and reference-versus-safety continuation; seven consent/subprocess tests; 48 Python tests discovered, 47 passed/one actual-Poppler smoke skipped. |
| ARM64, job122991498268 | Same own-code compile/native PE and native CTest/helper checks on Windows ARM64; same Python 47 passed/one skipped. |

[x64 receipt](evidence/40737957800-x64.json) and
[ARM64 receipt](evidence/40737957800-arm64.json) preserve exact executable hashes
and clean tested source identity. Local macOS ran all 48 comparator tests,
including real Poppler identical-PDF interoperability: ratio1.0, **no size
improvement**, completed measurement requiring review, `productUsable:false`.
Seven local consent/subprocess cases also passed. Larger output/pixel-reference
misses complete measurements; missing content, invalid raster/tool output,
protocol/safety failures and real subprocess errors/deadlines remain nonzero.

Exactly one manual workflow ran for this corrected source, with the same
automatic-CI skip guard. No rerun, runtime/dependency/MSIX build or upload,
GDI/PSA action, signing/trust/install or user-host execution occurred. The final
checkpoint is documentation/evidence only relative to this tested source;
it is not another green run. Actual desktop comparison, real worker
repetitions/faults/resource behavior and manual fidelity remain **unexecuted**.
No size/lifecycle claim, product acceptance, milestone-B progression or host-run
permission follows from these compile/unit results.

## What this measures

The proof-only target records three original two-page GDI/EMF documents: Unicode
text, vector/clipping/gradient/alpha drawing, and an original generated bitmap
plus selectable text. It replays the same hashed page records, physical A4
page/orientation/100% settings, through Microsoft Print to PDF and the existing
`PSA Diagnostic OXPS (Save As)` queue. The worker takes that captured OXPS.
No PDF is printed back into another PDF or rewritten/recompressed.

The diagnostic control is the user's already-observed ordinary-desktop route,
not the newer prototype's unproven activation bridge. The script matches only
new diagnostic captures by title digest, requires its source/target copy and
CompleteJob-returned evidence, and checks the chosen OXPS equals the captured
source. It never edits old captures or package state. A failed control stops the
batch; it does not mean ordinary desktop printing is unsupported.

Microsoft output is a comparison baseline, not evidence that DOCINFO output
propagates on PSA. The native submission record explicitly says submitted, not
pipeline success. A missing/invalid baseline prevents comparison.

The unapproved exploratory size reference is aggregate worker PDF bytes at most
90% of Microsoft PDF bytes across all three predefined fixtures. Exact aggregate
and individual ratios/growth are reported; missing this reference does not fail
the measurement or suppress other safe observations.
The comparator requires matching normalized selectable text/expected glyph
strings, embedded fonts, page count/media within 0.1pt, non-rasterized vector
paths and no unexpected full-page images. It renders both with the same local
Poppler tools at 150dpi. Unapproved raster references are mean absolute normalized
RGB difference at most 0.01 and at most 0.5% of pixels with any channel difference
above 0.10. Those metrics/reference misses are observations, not hard gates. Critical
regions still require local manual inspection; automated averages do not
establish general fidelity. No promise every file is smaller.

## Local preconditions and proposed commands

These are instructions for a **subsequently authorized** observation, not a
request or permission to run on the user's Windows machine now:

- Ordinary signed-in Windows 11 24H2+, native x64 or ARM64 shell/toolchain.
- Existing diagnostic package/queue and Microsoft Print to PDF. No package is
  installed or re-signed by this harness.
- Existing matching-native renderer delivery tree with inventory v2,
  provenance, notices and native-test receipt, supplied explicitly. Native
  renderer CI proof does not install these files on the user's host; public
  artifacts contain evidence only. Do not fetch nonexistent runtime artifacts,
  emulate x64 on ARM64 or rebuild dependencies to fill a missing precondition.
- Existing Python and local Poppler tools `pdfinfo`, `pdftotext`, `pdffonts`,
  `pdfimages`, `pdftocairo`, `pdftoppm`. Inspection tools are not bundled with the
  product; none are installed automatically. Required Windows Segoe UI glyphs
  must be available; font substitution is not silently accepted.
- A new local private, non-cloud-synced output directory in a trusted parent.
  The script rejects unexpected parent writers and protects its newly created
  output directory to this user/SYSTEM. It changes no pre-existing folder ACL.

Own-code-only build, no MIDL/MSIX/libgxps/Cairo build:

```powershell
./desktop/build.ps1 -Mode Proof -Architecture ARM64
# Defaults to installed VS2022/v143 and SDK26100; x64 uses -Architecture x64.
```

VS2026 is the hosted ARM runner's toolchain choice, **not** a local ARM64
prerequisite. Use `-VisualStudioVersion 2026` only where it is already installed
(with its required CMake4.2+); do not install/select it for the user's existing
VS2022/v143/SDK26100 environment.

Only after separate host/run consent and existing runtime/tool availability:

```powershell
./desktop/tests/Invoke-ProductProof.ps1 `
  -ProofExecutable <absolute-native-DesktopPdfProof.exe> `
  -RuntimeRoot <existing-matching-renderer-root> `
  -PythonPath <existing-python.exe> -ToolsRoot <existing-Poppler-bin> `
  -PrivateOutputRoot <new-local-private-directory> `
  -AllowDesktopProof -ConfirmLocalUnsyncedDirectory -VerboseLocal
```

There are three deliberate diagnostic Save As interactions: use `text.oxps`,
`vectors.oxps`, `mixed.oxps` in that private output directory. Canceling stops the
batch. This is a measurement control, never an unattended product fallback.
No service, profile, COM caller surrogate, runner or foreground bridge is set up.
The whole batch has a 900s deadline; disposable GDI submitters have 120s bounds
including picker waits. These are safety bounds, not performance estimates.

## Process and publication ownership

`common/WorkerJob.*` validates the renderer-owned inventory/receipt and actual
native PE architecture, pins hashes and denies ordinary write/delete sharing
while executing. It uses only the explicit fixed worker path, controlled
environment/private working directory and whitelisted stdio handles. An
observational import inventory/native test receipt is not a signature, a
dynamic-load/config/font audit or OS-ownership attestation.

Each worker is suspended until assigned to a kill-on-close, 1GiB Job Object.
The observer drains both status pipes with caps, applies a 60s worker deadline,
terminates/reaps on stop and checks no children survive. Input is capped at
256MiB; output/private-directory growth is observed at polling intervals, not a
hard filesystem quota. Worker stdout remains exactly the v1 protocol; verbose
records are separate. Success requires normal exit0 AND exact valid v1 `ok`,
published/cleanup flags and matching file size. Info, file presence or missing
status never qualify.

The one warm-up and 30 serialized conversions record worker PID/creation,
wall/CPU time, kernel peak committed memory and sampled private/working-set/
handle peaks. Sampled peaks are lower bounds. Observer growth of 4 handles and
16MiB private bytes from its post-warm-up baseline are **unapproved reference
targets only**. Reports retain baseline/final values, signed growth and reference
misses; these values do not gate safe fault observations/comparison.
Every worker exits; native renderer caches are never reused in-process.
Successful PDFs are deliberately retained as named private experiment outputs;
unexpected staging entries fail the test.

Deadline, cancellation and forced-termination cases require an actually observed
stop/reap. If a valid worker finishes before injection and leaves accounted-for
staging, that case is an explicit **unobserved fault/review gap**, not a passed
cancellation test; remaining safe observations continue. Unsafe, unreaped or
failed conversions stop further worker launches. Observed faults establish
active-process supervision, not a proven render-stage injection or native AV.
Stopped experiment outputs are removed only from that exact newly owned private
root after reaping; failed/indeterminate ordinary conversions are retained with
reconciliation required. No user destination is ever published in milestone A.
Exit24/71 and broken status cannot silently authorize another attempt.

## Local evidence and limits

`batch.json` is the overall record; separate native records describe control,
baseline, runtime preflight and worker repetitions/faults. Comparisons retain all
three size ratios and read-only PDF hashes. Original fixture manifests/EMFs,
PDFs/OXPS and inspection rasters/SVGs remain private experiment data; nothing is
automatically uploaded or added to Git.

Verbose output is opt-in, at most 1MiB for this batch (stricter than the plan's
per-job cap), with explicit truncation reporting. Subprocess diagnostic pipes
are drained/discarded under fixed caps. Terminal typed errors remain visible
with verbose off. Reports/logs avoid private paths, raw titles/tokens, document
strings, font data and raw upstream stderr. Original fixture manifests contain
their own required synthetic text as test input, not diagnostic logs.

Completed automated measurements still return `productUsable:false` and manual
review required. This does not test the new PSA-to-host bridge, silent output,
StorageFile publication, 30 repeated PSA prints, service support or upstream OS/
spooler/application memory cleanup. Those remain separate milestone-B gates.
No compression tuning, rasterization substitute, acceptance-threshold decision,
renderer-scope edit or milestone-B work is authorized by measurement completion.
