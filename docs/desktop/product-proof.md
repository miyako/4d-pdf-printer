# Product proof harness (milestone A)

**The own-code harness is Windows-native compiled and unit-checked. A creator-
reported real desktop attempt reached `worker-lifecycle` after all three
diagnostic controls, then hit the script JSON-write bug below. Retained files
have not yet been inspected; no comparison or worker-lifecycle result is
claimed. The retained fixtures were subsequently found to be physically
mis-mapped and clipped; they are diagnostic input, not the intended fidelity
corpus.** Any new host run requires separate creator/host approval.
No installation,
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

## Script-only JSON correction and reported desktop stop

The reported desktop run failed at `worker-lifecycle` because `Set-Content`
does **not** support `-NoClobber`. The same invalid call also affected
`comparison-inputs.json` and the final `batch.json`, masking the initial failure.
This is a harness bug, not evidence of failed PSA activation or rendering.
Preserve the complete original attempt, including any incomplete files/errors;
do not rerun the six print submissions, delete artifacts or recreate its
`batch.json` as though the attempt had succeeded.

All three writes now use one small UTF-8/no-BOM JSON writer with exclusive
`FileMode.CreateNew`, following the existing printer reference-control write
pattern. It never opens an existing file for truncation/overwrite. A failed
write may leave a new partial file: stop and preserve it, never retry over it.
If final reporting also fails, the terminal exception retains the primary
exception and typed `ProofError`; a separate typed `ReportWriteError` records
the secondary failure. A report-only failure also remains a real failure.
Normal terminal messages do not print private paths or raw upstream errors.

**Coverage gap:** the earlier seven consent/subprocess tests and native compile
checks did not execute these post-control JSON/report paths. They did not test
this fix. Targeted local PowerShell 7.2.6 tests now execute the production
post-control body and its actual catch/finally/terminal block, with only native
worker/comparator calls substituted. Nine cases cover all three real writes,
UTF-8/Unicode/depth/parse integrity, each existing-file collision with exact
bytes unchanged, and primary worker/comparator/write failures plus reporting
failure. `Check-ProductProof.ps1` runs those nine cases alongside its seven
existing cases. No new native build, CI dispatch or user-host action is part of
this script-only correction; native sources/binaries and historical receipts
are unchanged.

## Fixture producer physical-units correction

The creator inspected read-only copies of the original attempt: EMF hashes and
fixture records match, all control/baseline submissions succeeded, and the
script failed before creating worker inputs/state. However, the original
fixture producer mapped its .01mm logical coordinates to a fixed 2100-by-2970
pixel viewport, despite the EMF recording a different reference device's
physical resolution. Thus the intended drawings exceeded the 210-by-297mm
frame: even the Microsoft baseline and OXPS already clip headings/lower-page
content. This is a producer bug, not evidence of converter fidelity loss.
Keep those files and receipts unchanged; do not rescale/rewrite old files or
weaken expected full headings/page numbers/graphics to match their clipping.

The correction acquires one explicit display reference DC, uses it for both
`CreateEnhMetaFileW` and `GetDeviceCaps`, and derives the anisotropic mapping
from its actual pixel and millimeter spans. The logical window is that physical
span in hundredths of a millimeter; the viewport is that same device's pixel
span. Nonpositive dimensions and integer overflow are rejected explicitly.
No hardcoded display DPI, global DPI/profile/printer change or postprocessing.

Sources: [CreateEnhMetaFileW reference device and .01mm frame](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createenhmetafilew),
[window/viewport extents](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setwindowextex)
and [emitted EMF reference metrics/frame/bounds](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-enhmetaheader).

Portable local metric guards are not generated-EMF evidence. The new native
CTest actually calls the production fixture generator for all six pages and
inspects the emitted headers and drawing records: reference/map physical
consistency, the unchanged A4 frame, complete headings/body text, expected
rectangle/ellipse/line/gradient/alpha/image records, and full/lower content
bounds inside the frame. It checks generated hashes and cleans its own new
synthetic scratch files. Its stdout receipt contains synthetic metadata only;
CI uploads neither EMFs/fonts/rasters nor binaries. A separately authorized
single manual x64/native-ARM64 own-code compile/unit batch includes this test;
it performs **no printing, PSA activation or renderer/dependency/runtime build**.

### First attempted producer-unit checkpoint -- historical compile failure

The authorized run
[40825481671](https://github.com/miyako/4d-pdf-printer/actions/runs/40825481671)
**failed compilation** at source
`0e01f53b28b453818737d621d4ff11dc286e8daa`:
x64 job123287431313 and ARM64 job123287431152 both report C3861 in the new
`CheckPhysicalMapping.cpp`, lines219/221: `CoCreateGuid` and `StringFromGUID2`
are undeclared. The unit omitted the explicit `objbase.h` declaration header
under `WIN32_LEAN_AND_MEAN`. The producer executable target itself compiled on
both architectures, but the fixture-test target did not, so **no CTest or
generated-EMF checks ran**. Subsequent PowerShell/Python workflow steps were
skipped; no success build receipt or artifact was produced. This is a new test
compile defect, not observed failure/success of the corrected EMF mapping.

The [failed-attempt record](evidence/40825481671.json) preserves the exact source,
jobs and diagnostic locations separately from historical successful receipts.
Local Clang metric guards and PowerShell seven-plus-nine cases passed before
dispatch; they do not compile the Windows-only GUID calls or substitute for
real EMF-generation evidence. One attempt only; the failure was reported to the
creator, no rerun or post-attempt source repair was performed. The documentation
checkpoint is not a tested source or green run. **At that checkpoint native
fixture validation remained blocked pending creator review**, and no user-host
build/reprints were requested.

The creator subsequently reviewed that failed attempt and authorized **one
fresh** follow-up after the surgical test-only `objbase.h` include. Microsoft's
[CoCreateGuid](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreateguid)
and [StringFromGUID2](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-stringfromguid2)
requirements specify that include and `Ole32.lib`; the fixture-test target
already explicitly links `ole32` in CMake. No mapping, expected content/bounds,
assertions or comparator changes accompany this repair. The follow-up's new
source/result must be identified separately; the failed attempt remains failed.

### Successful separately authorized producer-unit follow-up

The fresh manual run
[40825887654](https://github.com/miyako/4d-pdf-printer/actions/runs/40825887654)
passed at exact source **`b33b741f0a51fcbbbbb21e958014879facb5328e`**.
It is a new source/run, not a stale rerun or relabeling of 40825481671.

| Target | Actual executed checks |
|---|---|
| x64 job123288712939 | Own-code compile/native PE checks; both CTests passed, including actual generation/inspection of six EMFs; PowerShell seven consent/subprocess plus nine post-control JSON/report cases; Python 48 discovered, 47 passed/one actual-Poppler smoke skipped. |
| Native ARM64 job123288713299 | Same two native CTests and actual six-EMF generation/inspection; same seven-plus-nine PowerShell and Python 47 passed/one skipped. |

[x64 build receipt](evidence/40825887654-x64.json) and
[ARM64 build receipt](evidence/40825887654-arm64.json) record clean source and
hashes of both proof and fixture-test executables. The
[synthetic fixture-unit receipt](evidence/40825887654-fixtures.json) matches
both native CTest logs: their actual reference DCs were 1024-by-768 pixels and
271-by-203mm; emitted windows were 27100-by-20300 logical .01mm units, and
viewports 1024-by-768. All six pages retained full headings/page numbers,
expected text/graphics and physical content bounds inside the unchanged A4
frame. Text pages contained 21 text records each; vector/mixed pages two each.
Lower rectangle/gradient/alpha/image/ellipse coverage and manifest hashes were
checked; synthetic scratch cleanup completed.

The only code repair after the failed source was the explicit COM declaration
include; mapping and assertions were not changed or disabled. Local portable
guards passed and the existing explicit Ole32 linkage was verified before this
one separately authorized follow-up. No further dispatch/rerun followed it.
Final checkpoint changes are documentation/evidence only relative to the tested
source. Only synthetic metadata/JSON/text evidence was uploaded, **not EMFs,
fonts, renders, binaries or private copied input/report data**.

This establishes actual fixture production/record bounds on these two hosted
native architectures, not printing, PSA activation, PDF fidelity/size,
Windows worker memory/lifetime behavior or a user-host result. The original
clipped files remain diagnostic history, the create-new JSON fix is retained,
and no user-host build/reprint/continuation or milestone-B work follows.

**Paper-grid limitation, unchanged comparator:** the observed PSA path uses
300dpi and 2480 pixels across, represented as 793.6 XPS units / 595.2pt; Microsoft
uses 600dpi and 4961 pixels / 595.32pt. The same nominal A4 therefore differs by
0.12pt horizontally. This exceeds the current 0.1pt media comparison tolerance;
do not silently relax it, resize output or attribute that difference to backend
content loss. It needs explicit subsequent review. Private Mac conversions of
the clipped inputs are diagnostics only, not complete-corpus fidelity or
native-Windows memory/lifetime proof; those files/reports remain private.

### Paused no-reprint continuation proposal -- historical review only

There is **no resume switch/platform**, and this is not permission or a command
to continue. The creator has now inspected retained copies; no new listing is
requested. **Pause this proposal:** continuing the clipped fixtures must not be
presented as the intended-corpus proof. The earlier guard checklist below is
retained for context, not an instruction to build/run a continuation script.
Any future use requires explicit review and host consent:

- Require the existing ordinary signed-in native desktop and matching local
  proof executable/build receipt, renderer tree/native receipt and inspection
  tools. Compare executable hashes/architecture with the retained native
  reports, and the renderer inventory hash with `runtime.json`; retain the
  runtime validation/pinning performed by the existing `--repeat`. No rebuild,
  installation, elevation, package update or trust change.
- Read `fixtures-report.json` and `fixtures/fixtures.json`; require matching
  fixture records and exact `text`, `vectors`, `mixed` order. Check every
  original EMF against its recorded `source_sha256`, expected text and vector
  requirements. Reject changed/ambiguous source identity.
- Require all three control and baseline submission reports with successful raw
  StartDoc/EndDoc returns and matching executable/context/physical-page records.
  Bind each control's `title_sha256` to exactly one existing capture in the
  expected diagnostic package's LocalState. Require its successful
  `job.json`/`completion.txt`, interactive OXPS endpoint, source byte count and
  SHA256 equality between retained OXPS and that capture's `source.oxps`.
  Require all three nonempty Microsoft PDFs; submission/file presence alone
  never establishes valid PDF content or completed comparison.
- Stop for reconciliation if any prior worker/report/staging or live-process
  evidence is present or ambiguous. Do not blindly retry conversions. Preserve
  every old file and record a read-only hash/size inventory of the selected
  old reports, source files, captures and baselines in a **new** private,
  unsynced, user/SYSTEM-protected continuation directory under a validated
  trusted parent; reject reparse/collision/unexpected-writer paths.
- Write only new `inputs.json`/`comparison-inputs.json` there, pointing at the
  preserved OXPS/Microsoft PDFs and new worker outputs. Call the existing native
  `--repeat`, then the existing read-only comparator, with new worker, report,
  verbose and render paths. Reuse existing bounded invocation behavior: one
  900s continuation budget, native 60s/1GiB per-worker limits, bounded pipes,
  kill/reap and explicit stop on real errors. No `--fixtures` or `--print`.
- Keep a separate continuation record linking the failed attempt by hashes,
  without rewriting its outcome. Recheck old input hashes after inspection;
  preserve new failure/partial artifacts too. Completion remains
  `productUsable:false`, manual review required; no size/lifecycle claim or
  milestone-B progression.

No user-host rebuild/reprint/continuation is authorized by the producer unit
check. Do not guess, overwrite or build a general recovery mechanism.

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
