# Desktop PDF integration: local source-review checkpoint

**The extended process/publication and unsigned-package gate is natively UNRUN
at this source-review checkpoint. Activation and printing are unproven.**
The prior compile-only source `3fd66773a5b86bd94276c437de7ecd8d5b4c58f3`
passed both architectures in run41037488999 (two CTests/five source guards);
that result does not cover these new tests or packaging. Later exact-source
receipts establish the extended result without a result-only source push.
This target implements the two required source paths without
changing the diagnostic printer or any installed package/queue. Native APIs,
process containment and publication semantics still need actual Windows evidence.

## Selected product boundary

Windows 11 desktop, signed-in user, native x64 or ARM64; later 4D minimum 19.
System Windows Save As uses normal **explicit OS-confirmed replacement**.
Silent printing uses a registered **exact new destination and never overwrites**.
No recompression/postprocessing, resident broker, native file dialog, elevation,
services/session 0 or pre-login. No promise of smaller PDFs or fixes to upstream
Windows/driver/app bugs.

The separate manifest defines `Miyako.DesktopPdfIntegration` 0.1.0.0, publisher
`CN=DesktopPdfPrototypeDev`, and **4D PDF Integration (Save As)** /
**4D PDF Integration (Silent)**. These are source definitions, not authorization
to register identity, trust a certificate, create queues or install anything.
DesktopPdfPrototype0.2.1, diagnosticPsaFeasibility0.1.2, smoke0.1.2, the signed
but uninstalled 0.2.2, all old worktrees and existing print artifacts stay frozen.

## Components and contract

`Task.cpp` retains the OS callback/StorageFile and captures one closed OXPS.
`Host.cpp` accepts that job once, acquires one package-wide OS-released slot,
supervises the fresh worker and handles silent publication. `Client.cpp` supplies
short-lived registration/status/cancel operations; it is never prestarted for
generic interactive printing. `Job.h` contains small strict record/file helpers;
`WorkerProcess` owns runtime validation, handles and no-replace publication.
`Contract.h` is production-used portable argument/path/deadline/wire validation.

The same fixed full-trust host extension is attached to AppIDs **SaveAs** and
**Silent**. Launch arguments are `--mode <SaveAs|Silent> --job <32lowerhex>`.
The parser accepts that exact bare form or the exact corresponding
`/InvokerPRAID: SaveAs ...` / `/InvokerPRAID: Silent ...` form, with matching mode.
It rejects old `Printer`, arbitrary wrappers, mismatched AppID/mode and extras.
Only the old smoke wrapper was observed on Windows; the NEW manifest/wrappers
remain unproven and must fail explicitly if Windows supplies something different.
Generic interactive document titles are never parsed as registration selectors.

Client verbs below describe an interface, **not operator instructions or consent
to execute on Windows**:

| Verb | Meaning |
|---|---|
| `configure-runtime <root>` | Explicit one-time fixed runtime configuration, independently of any job. Validate/pin inventory, native receipt, provenance hashes and architecture. Existing configuration is not replaced implicitly. |
| `register <destination> [completion-ms] [registration-ms]` | Immutable current-user registration; returns ID and exact `4dpdf:<ID>` document-name selector. New local `.pdf` only. |
| `submitted <registration-ID>` | Immediately BEFORE starting submission/StartDoc, permanently starts the ONE completion budget. Cannot reopen or resubmit. |
| `wait` / `status <registration-ID>` | Read exact claim and PSA completion; exit0 only after acknowledged successful publication, exit1 for reported failure, exit2 for timeout/unknown, exit3 for pending status. |
| `cancel <registration-ID>` | Persistent cooperative cancellation, including before a callback. Does not itself prove a process exited or a file was not published. |
| `reconcile <SaveAs\|Silent> <exact-job-ID>` | Read-only explicit recovery observation: retain original print outcome and distinguish absent/current matching bytes from silent intent's confirmed file identity. Never retries, deletes or upgrades a failed/unknown acknowledgement. |

Default completion budget is **300000ms (five minutes) from submission BEGIN**:
includes StartDoc/spooling, callback, slot waiting, rendering, publication and
completion, not a fresh five minutes after EndDoc/host start. Registration has a
separate provisional **600000ms** default; submission must begin before expiry,
but a valid submission keeps its own completion deadline. Durations are
configurable positive milliseconds with a provisional 24-hour argument cap.
Absolute cross-process deadlines are checked with a clamped-in-effect remaining
monotonic wait per observer; phases do not reset them. Clock rollback beyond the
registered interval is rejected. This does **not** bound a stuck caller GDI/OS
operation. The caller must persist cancellation if StartDoc/submission fails and
must not retry/reopen the registration.

4D adaptation remains future work: prove selector propagation and serialize the
ENTIRE shared-settings lifecycle (save/change, register, submit, completion or
cancel, restore). No plugin or automatic DOCINFO output bypass is claimed.

## Ownership, publication and uncertainty

Each genuine callback creates a fresh job ID. Silent title is only a selector
into an immutable single-use registration; a create-new claim binds its actual
workflow SessionId, endpoint and destination to that job. Schema/fields/types,
sizes, paths and bindings are validated. Neither title nor nonce authenticates
against malicious same-account processes; those are explicitly out of scope.

OS-managed package LocalCache contains exact job state, source and private PDF.
Current-user OS separation is retained. The host rejects unsupported ACL grants,
reparse/cloud recall topology and unlisted runtime binaries; it does not change
ACLs or construct package SIDs. Parent directory handles prevent renaming during
use. Provisional path scope is **fixed local volumes, under the current user
profile, ordinary `.pdf` destinations and supported local StorageFile providers**.
UNC/device/relative/ADS/device-name and unsupported cloud/reparse paths fail
explicitly. Conservative ACL/provider checks still need real Windows testing;
no automatic safety downgrade, path substitution or unsupported-provider promise.

One conversion/publication slot waits cancelably within the existing deadline.
Host creates the worker suspended, assigns a noninherited non-breakaway
kill-on-close Job Object before resume, whitelists only standard pipe/NUL handles,
uses fixed runtime/working directory and bounded pipes, then observes exact child
exit and zero active children. Prototype guards reuse capture/output 256MiB,
capture 90s, launch 15s, cleanup observation 5s and prior supervisor 1GiB memory;
single-process and five-minute CPU guards are explicit provisional policy for
review, independent of configurable wall timeout. Job-area disk growth uses a
sampled source-plus-output guard, not a hard filesystem quota. No
throughput/coverage guarantee.
Worker observations record phase, OS exit and exit observation, not raw channels.

Worker writes **only new private staging**. Normal exit0 AND exactly matching
strict v1 `ok`, published and cleanup-complete status are mandatory. Exit24/71,
crash, kill, malformed/missing status may leave private output: retain exact owned
artifacts for reconciliation, never rerender from mere presence. Rendered input
remains untrusted; Job Objects are not a parser-exploit security sandbox.

Interactive: PSA checks staged bytes/hash and commits a transaction on the
actual retained system StorageFile. Silent: host copies validated bytes to an
owned same-volume transaction, flushes, records intent/file identity/hash and
moves without replacement to the exact destination. No worker final-path writes.
Only exact owned source/PDF/empty transaction paths are removed on confirmed
normal success; failures/uncertainty retain their exact state, no prefix sweeps.

Cancellation before the finalizer's commit decision suppresses publication.
After that boundary it can be too late. Async commit/move, terminal record and
CompleteJob are NOT atomic. On uncertainty, record Failed/indeterminate,
preserve final PDFs and reconcile exact intent/identity/bytes; existence/hash
alone is not ownership. No blind retry or deletion. Succeeded requires confirmed
publication, validated worker exit/status, required cleanup and CompleteJob
return. Saved PDF plus failed/unknown print acknowledgement is not caller success.
OS Save As may precreate/change a target BEFORE callback; no preservation,
provider-wide atomicity, crash rollback or power-loss guarantee is implied.

Root host is system-launched. PSA has no claimed OpenProcess/terminate/reap
privilege over it. Finite cooperative loops do not guarantee containment of
root-host/OS I/O hangs. Host death closes its private worker job; killing is not
proof that an external observer witnessed exit. No watchdog/runner/RPC platform.
The inherited diagnostic PDC currently advertises A4/portrait/300dpi/color only;
broader PrintTicket/layout/copies semantics are not claimed verified by this slice.

## Source provenance and review evidence

The dependency-only `renderer/` + `docs/renderer/` snapshot is byte-exact to
**8b34e5669bec822bb82768fd61b0483bbdd74567**, without importing its workflow or
mutating its refs.
Main baseline is **12845fdd31f9ecdae5964b7bd90f75548826eced**. PR8 launch/handle
patterns are adapted from **6b974f94d6aa5a574a96a899b34539061d7e4850**;
its test trust/completion protocol is not promoted. Only corrected
`PhysicalMapping.h` is extracted verbatim from PR6
**12cc27915d08fcf29c5b1f00c422e0a16cc23e1b**; none of its RPC/admission platform.

The first slice can reuse the already user-built verified native ARM64 PR7
development runtime via its fixed explicit root; not downloaded or bundled.
Receipt worker hash, native OS/PE architecture, every listed binary hash/size,
absence of unlisted binaries and pinned inventory/provenance/receipt are checked.
Inventory/receipt are observational evidence, not signed attestation. LGPL
source/patch/build/notices and replacement/relinking gates remain; process/DLL
separation is not automatic compliance. No binary cache/DLL/document uploads.

Portable `tests/Contract.cpp` exercises real production helpers, not Windows
stubs. Python `tests/Source.py` checks exact dependency/geometry provenance,
manifest/labels/staging wiring and the static workflow execution contract.
`tests/Native.cpp` contains the previously exercised WinRT record/status checks.
The controlled `Process.cpp`/`WorkerFixture.cpp` tests described below are
**natively unrun**, not real renderer, PDF, activation or printing evidence.

First post-native/user-approval goal is the **early corrected small real-PDF
comparison**, not more infrastructure: new original text/vector/mixed fixtures
with physical-reference-DC geometry, native EMF bounds and independent edge/text
markers; paired Microsoft/new-printer outputs; page/text/font/vector/image and
manual/raster fidelity before raw-byte ratios. Old six-job corpus stays frozen.
The comparator's existing **0.1pt** geometry gate versus known **0.12pt** grid
differences is explicit: investigate or obtain policy approval, never silently
relax/rescale. No size/raster reference promoted into an unapproved product gate.
The full corrected fixture generator/comparator is not duplicated into this
small source checkpoint; integrate only the necessary approved proof pieces.

Still unproven for this extension: native process/publication tests and unsigned
package validation; new installed identity/manifest/PRI behavior,
both actual activations/wrappers, root exit/worker Job Object behavior, runtime
DLL/font/config loading, finite concurrent waits, cancellation/reap/commit races,
disposable existing-file/failure tests for Save As replacement, silent no-replace,
both actual real-PDF modes, corrected measurements and the 30-job lifecycle.
No extended native run, packaging/signing/trust/install, queue action or print
had been performed when this extension checkpoint was prepared. Each requires approval;
no Windows operator commands are supplied here.

## Native CI contract: adapted source, execution pending

The existing `Renderer Windows` workflow is adapted to the pinned PR7 helper's
`Prepare` / `Build` / `Test` interface. This also fixes its old nonempty
`RuntimeRoot` mistake: runtime assembly belongs in a fresh separate
`build-runtime-<arch>`, not the worker's populated Release directory. No renderer
dependency/source snapshot or production implementation is changed by the CI
adaptation. Source changes are not a successful GitHub/PowerShell execution.

Manual dispatch uses a **branch/tag ref as transport**, a required approved
40-hex `expected_sha`, and `scope=own-code` or `scope=full` (default **full**).
Checkout is always the exact event commit, explicitly the PR head rather than
the synthetic merge ref; manual execution fails before compilation if event,
checkout and approved SHA disagree. A mismatch/failure does not authorize
repair, another push or another dispatch.

The proposed extended batch is **one own-code-only dispatch with both
native x64/VS2022 and ARM64/VS2026 jobs**. Both assert preinstalled native OS,
v143/SDK26100 and matching PE machines; ARM64 requires preinstalled CMake4.2+.
They compile all own targets, execute all25 CTests unfiltered, run the five
source/provenance guards and check selected-commit whitespace. Python source
checks may use an emulated host interpreter; C++ target/tests must be native.
Missing tools fail without installation/fallback. There is no renderer build,
real runtime loading, Host/Client execution or printing. Only after native tests
pass does each job assemble and validate an unsigned own-payload package.
A pass establishes compile/link/PE, exercised controlled production branches
and unsigned-package checks, not the packaged Windows behavior listed above.

Normal PR and **main-only push** events always select full regressions; their
existing renderer/doc paths remain and now also include `desktop-pdf/**` and its
reused printer App/PDC/build-helper files. Full renderer jobs depend on success
of the **entire** own-code native matrix, then run the proven two-architecture
helper stages with local ephemeral caches only. In explicit own-code scope
those renderer jobs are **skipped, not passed/tested**. Full dependency builds
are outside this proposed batch and need separate authorization.

Updating this feature branch without opening a PR does not match the main
push trigger or the other three existing workflows' unchanged printer filters.
One subsequent manual dispatch is a separate action. A later PR/main push can
start full regressions and is not authorized by an own-code gate approval.
No branch-specific skip directive, broad regression disablement or hidden
failure continuation is used.

Artifacts are exact-allowlisted JSON/text receipts, PE hashes and compiler/source/
CTest logs, including failed/skipped step outcomes. No EXE/DLL/LIB/MSIX/PRI,
runtime/cache tree or document upload. Receipts explicitly keep renderer,
activation, packaged publication and usable-product claims false.
Version2 receipts report actual `packaging_outcome` and success-only
`unsigned_package_verified`, distinct from controlled process/publication tests.
`Source.py` checks this execution contract statically while
retaining all33 dependency-source hashes, exact geometry extraction and manifest
guards; it is not a YAML parser, PowerShell executor or native API proof.
Process-scoped Git configuration disables automatic CRLF conversion so these
byte checks compare repository blobs, without changing global Git settings.
Post-run receipts are tied to the tested SHA and need no result-only source push.

## Controlled native cases and owned cleanup (execution pending)

The existing production `Render`, `PublishNew` and `Reconcile` are called
directly; no parallel supervisor/parser/finalizer or Host bypass is introduced.
The test driver constructs `Runtime` with an isolated fault-fixture root, NOT a
runtime accepted by `InspectRuntime`. Fixture bytes/status are labelled
**NOT real renderer/PDF**; no PR7 passed receipt/inventory is fabricated.
Missing/invalid metadata is rejected through actual runtime validation; positive
real runtime acceptance remains the later explicit Client configuration step.

The 23 added cases cover strict success/length, crash, hanging worker deadline,
pre/running cancellation, malformed/duplicate/trailing/missing status, length
mismatch, post-output exit24/71, descendants/breakaway, handle/environment
whitelisting, channel overflow, supervisor kill-on-close, new publication,
existing target, equal-byte competitor race, precommit cancel, post-move journal
failure and validation negatives. Faults rendezvous with exact live handles,
image and creation-time verification; returned production observations require
observed exit and zero active children. The test-only self-supervisor case
instead observes exact worker exit after supervisor death; it cannot report a
production observation from a dead supervisor or prove PSA root-host control.

Each case creates a new space/Unicode-named directory under the actual current profile. A finite
20s controller/15s fixture rendezvous and 60s CTest ceiling leave production
limits unchanged. Successful cases verify required retained staging/intent/final
files before inventorying exact files in owned directories and deleting them
individually, then removing empty owned directories. Failure preserves evidence
and terminates/reaps only known owned handles; cleanup failures remain failures.
No prefix sweep, recursive cleanup or process-name kill.

## Local helper phases (interfaces only, not operator commands)

PowerShell7 and preinstalled Git/VS/v143/SDK26100 are required; no tool installs,
downloads, automatic source fetches, certificate creation or trust changes.
`OutputRoot` is a mandatory absolute **new** directory with an existing parent,
under ignored `out` or outside the checkout. All phases require a clean exact
Git checkout and native target OS. Paths may contain spaces/Unicode.

| Phase | Inputs and behavior |
|---|---|
| `Build` | Mandatory `Architecture`, `OutputRoot`; `VisualStudioVersion` defaults2022. Optional explicit `CMakePath`/`PythonPath`. Resolve CMake/adjacent CTest from the selected VS bundle unless explicitly supplied; check versions/generator before compiling. Run source checks, build all own targets, verify PEs, run unfiltered tests and record `own-build.json`. |
| `Package` | Same target/VS/output inputs plus existing `BuildDirectory`. Requires exact-source successful native receipt and unchanged tested payload hashes. Install into new staging without rebuilding/retesting; generate four assets, PRI and unsigned MSIX. Validate actual archive members/PE hashes, manifest/AppIDs/extensions/capability, and extracted PRI hash/queue strings. |
| `SignCopy` | Explicit `UnsignedPackage`, NEW external `SignedOutput`, existing `CertificateThumbprint`, native target and new evidence output. Require matching successful package receipt, valid matching existing code-signing certificate/private key; copy without overwrite, sign/verify the copy, preserve unsigned original. Never used in CI; not installation authorization. |

`Build` explicitly checks BOTH nonancestor comparison commits
`8b34e5669bec822bb82768fd61b0483bbdd74567` and
`12cc27915d08fcf29c5b1f00c422e0a16cc23e1b` before invoking Source.py.
A branch checkout alone may lack either object; missing objects fail with the
exact pin before native compilation. Obtain them only in a separately approved
source-checkout step, preserving blob line endings; this helper does not fetch.

Package payload is exactly three production EXEs plus Task DLL, generated
architecture manifest, Integration PDC, four assets and PRI (plus MSIX metadata).
Tests, fixture, diagnostic identities and external renderer/DLLs are excluded.
The existing five CI evidence paths gain only `package-evidence.json`; unsigned
MSIX/PRI/assets remain ephemeral in CI. Local outputs/signatures stay local.
The external verified PR7 runtime is configured separately after future approved
installation, with no print side effect. First consented real generic-app PDF,
then silent pipeline proof and the early corrected corpus remain subsequent gates.
