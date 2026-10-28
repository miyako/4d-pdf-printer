# Product proof harness (milestone A)

**Implementation is not yet Windows-native compiled or exercised. No product
size, fidelity or repeated-printing result is claimed.** The current machine is
macOS; Windows execution and a matching **locally available** renderer require
separate creator approval. No new CI, installation, signing, trust change,
runtime download or dependency build is authorized by this document.
The creator separately authorized **one own-code-only compile/unit CI batch**:
manual dispatch only, no automatic push/PR runs or historical activation steps.
That batch does not execute this document's Windows printing/worker experiment.

This is a standalone original-input experiment, **not a Save-As-only usable
printer**. Silent application-supplied output and the interactive endpoint both
remain mandatory for the later first usable prototype. Existing activation
failures and exact source SHAs remain in [validation.md](validation.md).

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

The initial metric target is aggregate worker PDF bytes at most 90% of Microsoft
PDF bytes across all three predefined fixtures. Individual growth is reported.
The comparator requires matching normalized selectable text/expected glyph
strings, embedded fonts, page count/media within 0.1pt, non-rasterized vector
paths and no unexpected full-page images. It renders both with the same local
Poppler tools at 150dpi: mean absolute normalized RGB difference at most 0.01,
and at most 0.5% of pixels with any channel difference above 0.10. Critical
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
./desktop/build.ps1 -Mode Proof -Architecture ARM64 -VisualStudioVersion 2026
# x64 uses -Architecture x64 -VisualStudioVersion 2022
```

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
handle peaks. Sampled peaks are lower bounds. The persistent observer must
finish within 4 handles and 16MiB private bytes of its post-warm-up baseline.
Every worker exits; native renderer caches are never reused in-process.
Successful PDFs are deliberately retained as named private experiment outputs;
unexpected staging entries fail the test.

Deadline, cancellation and forced-termination cases require an actually observed
stop/reap. If the worker finishes before injection, that case fails as
unobserved rather than claiming a cancellation test passed. These establish
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

Passing automated observations still returns `productUsable:false` and manual
review required. This does not test the new PSA-to-host bridge, silent output,
StorageFile publication, 30 repeated PSA prints, service support or upstream OS/
spooler/application memory cleanup. Those remain separate milestone-B gates.
No compression tuning, rasterization substitute, threshold relaxation or
renderer-scope edit is authorized when the product comparison fails.
