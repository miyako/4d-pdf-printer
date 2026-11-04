# Desktop PDF integration: local source-review checkpoint

## 0.1.0.6 (draft: pre-CompleteJob flush and post-complete teardown label)

**0.1.0.6 is an unbuilt-for-release, unsigned, uninstalled source draft.** It stays
a draft until the official build/package/sign/update and print gates run on its exact
source. Dev builds and isolated native/Contract/Python checks are not install evidence.

Evidence basis: in the 005 Windows04 silent print series, all six installed 0.1.0.5
prints succeeded: the PDF was published and `completion.json` reported `succeeded`.
Every activation journal terminal (v3) nevertheless ended with `finalize_reason: canceled`
and `drain_state: canceled`, with framework raw reason 7 (SystemPolicy). In each case
`CompleteJob(Succeeded)` had been entered and returned without an exception. The cancellation
arrived 0–78 ms after CompleteJob entry, never before it. The journal tail was truncated:
the last published sequence was 9–16, against 11–18 accepted events. The SystemPolicy
cancellation is therefore platform teardown in response to CompleteJob: the print is fine,
but the diagnostics were cut short and mislabeled.

- **A: pre-CompleteJob flush.** After `Point::CompleteBefore`, the data path awaits
  `Trace::FlushBeforeComplete()` before `CompleteEnter`/`args.CompleteJob(status)`:
  - The flush waits until every event accepted at that point is published, or until
    failure, stop, or cancellation, or until `PreCompleteFlushMs = 500` elapses.
  - It polls in slices of at most 25 ms on a dedicated event, so it never consumes
    Finish's wake-up.
  - It never throws, and CompleteJob is still called exactly once. Status,
    `completion.json`, the duplicate-data path and data-path cancellation are unchanged.
  - Normally the flush is immediate. It adds latency before CompleteJob only when
    journal I/O lags, and then at most 500 ms.
- **B: post-complete teardown label.** A framework (origin 0) `Canceled` callback
  whose `ReadComplete()` sample shows CompleteJob entry published (state 2) records
  the reason `post-complete-teardown` instead of `canceled`:
  - It applies to both the reason CAS/setter and `Finish(..., false, 3)`.
  - A framework cancel before CompleteJob entry is still `canceled`, and so is any
    host/data-path cancel (origin 1).
  - The journal drain still stops immediately with `drain_state: canceled`; only the
    label changes.
- **Terminal v4** (the production default):
  - It adds `pre_complete_flush` as the last key: `state` (`not-attempted`,
    `in-progress`, `flushed`, `deadline`, `canceled`, `failed` or `stopped`),
    `target_count`, `published_count`, `begin_tick_ms` and `end_tick_ms`.
  - It allows the reason `post-complete-teardown`, and its observations object has `"v":3`.
  - The v2 and v3 encoders and decoders are unchanged. The bounded v4 maximum
    receipt is 2697 bytes, within the existing 3300-byte bound and the 4096-byte
    backend limit.
  - `tools/journal.py` decodes v4 with `general_v4`.

**0.1.0.5 is an uncommitted offline diagnostic revision, not a built or installed
package.** The installed 004 and every historical result remain unchanged.
`build.ps1` defaults to the original `full-suite` build/CTest behavior.
An explicitly selected `isolated-journal-v3` profile instead builds the four
package payloads plus Contract/Native test targets and runs only Contract NOARG
and Native's selected isolated journal scope. It retains its fresh private tree,
started/closed check records and byte-bound logs. It never runs no-argument Native,
process fixtures or CTest. This profile requires separate Windows build/run
authorization; importing receipt helpers does not execute it.

Scoped build receipts contain no `ctests='passed'` claim. Package and SignCopy must
select the same profile: scoped Package checks the exact source SHA/tree,
compiler/SDK/tool facts, four payloads, selected argv, successful check records,
test images and logs. Legacy full-suite receipts retain their original meaning
only in the full-suite path. The clean Git checkout guard remains mandatory;
the current dirty/untracked source cannot truthfully pass it until a separately
authorized complete source commit exists. Build/package/sign, same-family
004-to-005 update, and one future print are separate gates, none authorized here.
Name/Publisher/endpoints/queues/renderer/client semantics remain unchanged.

The explicit native **test-only isolation revision** adds a selected journal/
deferral scope, separate from the legacy no-argument native record/status suite.
It requires a caller-specified fresh absolute fixed-local-drive directory with
existing non-reparse ancestors. The selected scope creates only fresh case
children there and **retains every tree on success or failure**; it never
recursively cleans up, retries, or kills pending owners. Ancestor handles exclude
delete-sharing while the root scope lives. This is path confinement for a private,
non-concurrently-modified test tree, not protection against a hostile same-user
writer replacing descendants. No installed printer/package cache is used by this
scope. The legacy no-argument suite remains broader and is not authorized by
isolated-scope approval; its historical temporary-directory cleanup remains.

An internal per-instance `Task` diagnostic override is non-null and immutable.
Normal WinRT/default construction still creates a fresh default `Trace` at the
original point on **each** `Run`, never at construction. Injected real-Run
reentry fixtures use only `TrialIo`; the override intentionally keeps that Trace
alive through Task destruction, without a backend-to-Task cycle. No production
cancellation, observation, finalizer, budget, schema or admission policy changes.
This is a new source revision; earlier twelve-file snapshots and accepted
compile/Contract receipts do not validate it.

Selected native cases exercise real WinRT storage/FileIO and native report
publication/collision, real Task finalization and COM MTA/STA return with mocked
instances/deferrals and manually injected cancellation. They do not execute PSA
activation, actual printing or real `CompleteJob`, or explain live SystemPolicy
cancellation. Existing waits have local deadlines, but synchronous OS I/O,
WinRT `.get()`, scheduling and thread joins provide **no hard process time bound**.
Failure may leave active async owners; retained files are not proof those owners
have stopped. Windows compilation and selected execution require separate review
and authority; offline source checks are not Windows behavioral evidence.

**Deployment and further print trials are on hold.** Package Name/Publisher,
AppIDs, capabilities, endpoints and job/worker protocols are unchanged.
Commit/build/sign/update procedures require separate approval.
Actual Silent attempt03 produced one independently validated PDF; the user
reported no UI and correct text/rectangle display. Its original technical result
remains **false**: scrambled collector ordering and an independently canceled,
non-drained journal are distinct failures. Neither is retrospectively repaired.

The bounded ARM64 native-fixture and PS5.1 synthetic lane was accepted only for
`896426a` (tree `e8b2c67`). This 0.1.0.5 draft has not been built, signed,
installed or printed. A successor needs its own exact-source evidence; accepted
896 receipts, binaries and source ledgers must not be relabelled as its acceptance.

The bounded instrumentation adds no Status getter, callback worker, wait, handler,
Cancel call or budget. Fixed immutable lock-free cells distinguish first entry,
actual reason CAS winner, elected finalizer and existing Cancel/await observations.
Journal async status remains explicitly unknown; Cancel return and natural await
outcomes are not acknowledgement or physical-stop proof. Existing job outcomes,
CompleteJob, cancellation and deferral/report ordering remain unchanged.

`tools/journal.py` dispatches by explicit expected terminal version. Historical
v1 uses the unchanged strict decoder; v2 has exact23 root/exact11 observation
fields. General activation-level v2 parsing permits an empty job when no binding
was known at freeze, including Normal/drained receipts after pre-job failure.
It never admits a job. Known-job admission still needs independently established
fresh producer context, exact32hex binding and the unchanged normal journal/
lifecycle predicate. The separate exact17-key assessment is always review-only:
shape, sampled readiness and first/minimum cancellation provenance are distinct.
No historical false result or primary PDF/job outcome is promoted.

The current source emits terminal **v3 / observations v2** for independent,
in-memory `CompleteJob` boundary observations. Explicit expected-v1 and expected-v2
decoders retain their original contracts; v2 never accepts the additional `j`
field. No historical binary, receipt or native failure is upgraded by this source
change. A future capture must explicitly select v3; existing frozen operators
and their expected-v2 evidence are not silently adapted.

`j = [call, frameworkSample]`, where `call = [firstEntry, return]`.
The entry payload is `[tick, thread, attemptedSubmittedStatus]` (raw SDK enum0..2).
The return payload uses the existing `[tick, thread, failed, nullableHRESULT]`
outcome convention. The first framework-cancellation sample payload is
`[[entryTick, thread], [sampleEndTick, thread], call]`, bound to the first framework
cancellation context. These fixed cells require no file I/O, waits, new locks,
Status getter or new phase synchronization. Both primary and duplicate-data
completion paths record attempts; repeated attempts mark the first result
ambiguous rather than borrowing a different call's outcome.

Sampling reads return before entry, and the terminal reads the immutable
framework sample before the current call cells. It does **not** make those reads
atomic. Unseen/publishing/ready, missing framework samples and repeated attempts
remain explicit. `entry-recorded-return-unobserved` includes a call not yet
entered, in progress, or already returned before its AFTER observation published.
`not-attempted-at-entry-read` applies only at that read. A ready return proves the
projected call returned before that observation, not before cancellation entry.
There is a gap between the BEFORE stamp and the API call, and between its return
or caught failure and the AFTER stamp; equal millisecond ticks cannot order them.
The framework sampling interval starts at callback entry and may overlap a return.
The parser never manufactures an atomic OS-call/cancellation ordering.

The v3 assessment adds `complete_job` to the historical17-key assessment,
including race-honest snapshot/framework phases, attempted status, original
outcome and `os_call_order="not-established"`. Additional sample flags256/512
denote incomplete boundary evidence/repeated attempts. This is diagnostic
instrumentation, not a cancellation-cause fix, admission exception or execution
lifetime guarantee. Receipt I/O can still be absent or fail; retaining observations
in memory does not prove process lifetime or persistence.

`tools/JournalCapture.ps1` supplies reusable functions, **not a runnable print
operator**. Explicit integer dictionary indexing orders bounded0..31 mappings
without changing copied slot/hash/internal-sequence associations. The original
executed operator and raw evidence remain untouched. `tools/journal_capture.py`
validates the copied journal stage with an explicit expected version; it is not
a replacement PDF/producer/runtime validator. Local portable/PS7 fixtures do not
establish native Windows/PS5.1 behavior or authorize another trial.

The earlier checkpoint below remains relevant history.
The preceding `6674090` source produced one genuine Silent PDF: matching worker/
publication evidence, a receipt after `CompleteJob(Succeeded)` returned, and user
reports of no UI, readable text and a visible rectangle. Its strict collection
result stayed **false** because the activation journal stopped at `job-created`.
The later same-job snapshot retained the same incomplete prefix and staging files.
Those immutable results are not retroactively upgraded by this source correction.

The change below distinguishes published events from dequeued events, waits
boundedly through `finish`, and reports diagnostic failure independently. It does
not establish the old stall's exact cause, certify PSA lifecycle behavior, or prove
process exit/unload. Native CI results must be tied to their exact source commit;
unpackaged test fixtures are not AppContainer/printing evidence.

## Selected product boundary

Windows 11 desktop, signed-in user, native x64 or ARM64; later 4D minimum 19.
System Windows Save As uses normal **explicit OS-confirmed replacement**.
Silent printing uses a registered **exact new destination and never overwrites**.
No recompression/postprocessing, resident broker, native file dialog, elevation,
services/session 0 or pre-login. No promise of smaller PDFs or fixes to upstream
Windows/driver/app bugs.

The separate candidate manifest defines `Miyako.DesktopPdfIntegration` 0.1.0.4, publisher
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

Both unchanged printer extensions now belong to existing **SaveAs**, with one
fixed full-trust Host extension; separate packagedClassicApp **Client** remains.
There are two total Applications, one printer-owning Application and two endpoints.
Silent's expected printer AUMID changes from the package family plus `!Silent`
to `!SaveAs`, and its full-trust invoker PRAID becomes `SaveAs`. URI-based protocol
mode remains `Silent`; Save As and Client identities remain unchanged.
`HostArguments.h`, used only by Host and tests, adds the exact
`/InvokerPRAID: SaveAs --mode Silent --job <32lowerhex>` form by delegating its
bare mode/job suffix to unchanged `Contract.h` validation. All original strict
bare/corresponding-wrapper forms remain accepted for compatibility. Arbitrary
owners, nested wrappers, other mismatches, malformed IDs and extras still fail.
This is argument compatibility, not authentication. Host still validates the
request's mode and job ID. The preceding one-job dispatch evidence is separate
from portable argument tests and does not establish repeated reliability.
Generic interactive document titles are never parsed as registration selectors.

This correction changes Task diagnostic finalization and journal helpers/tests.
App/Client, shared job contract, worker/runtime and generic publication sources
remain unchanged. The existing
full native Build/Package pipeline must rebuild and test all own targets, then
record fresh actual PE hashes. Native PE timestamps are not deterministic: frozen
source does not imply byte-identical245 binaries. No mixed-binary reuse,
timestamp normalization or relaxed receipt/inventory checks are proposed.

Any later normal in-place update needs separate approval. Package family/cache
stay the same, but withdrawing `!Silent` can refresh/recreate queue enrollment or
leave stale AUMID binding; unchanged URI/port is not AUMID proof. Unexpected
enrollment must stop, not trigger force/uninstall/re-registration or cleanup.
Restoration would require a separately reviewed forward higher-version package,
not an assumed safe normal downgrade to0.1.0.1. Preserve existing jobs,
registrations, diagnostic captures/traces, runtime, default and evidence.
Topology plus Host compatibility and deployment are coupled variables, not a
strict causal experiment. Actual fresh Silent exact PDF/no-UI/publication/
CompleteJob evidence and a separately authorized Save As regression are required
before any usability claim. No install/print operator or native gate is authorized
by this source change.

### Diagnostic-only Task activation checkpoints

`ActivationDiagnostics.h` adds a separate
`LocalCache\DesktopPdf\ActivationDiagnostics\<activation-32lowerhex>\<sequence>.json`
namespace, never a Jobs/Registrations or success-contract record. `Run` is the
earliest observable boundary: factory/constructor tracing is deliberately omitted
to avoid new WinRT initialization or activation in the class factory.
Checkpoints cover Run/deferral, trigger acquisition, session acquisition plus
subscription, immediately before/after `session.Start()`, Data/duplicate entry,
endpoint/content/configuration checks, silent selector/registration/submission
checks, metadata validation/job creation, pre-job/data failure, CompleteJob and
cancellation/Finish. Session-subscribed is emitted after BOTH acquisition and
subscription; no intervening success marker is invented if subscription fails.
Original HRESULTs are copied before diagnostics and existing failure/completion
handling continues unchanged. Pre-job-failed means no job StorageFolder was
acquired, not proof that a failed async folder operation left no directory.

The diagnostic captures the actual **current-process**
`GetCurrentApplicationUserModelId` result at writer initialization, not an AUMID
inferred from URI/manifest/title or a claimed thread/trigger identity. It records
available/unavailable/error/invalid-response and original API/validation codes.
The one fixed 256-wide-character buffer is not resized/retried. Only that bounded
identity, checkpoint, process/thread IDs, monotonic event tick, HRESULT and an
optional own job ID are recorded: no title, destination, session/source/document
text, OXPS or PDF payload.

Each activation admits **31 ordinary events plus one reserved `finish`**, each at
most8192 JSON bytes. Admission loss is explicit and never certified as clean
coverage. Sealing rejects later additions; sequence numbers remain unpadded0..31.
One writer uses tracked create/write/rename operations with no replacement. The
last-published cursor advances only after rename returns, never after dequeue.
First stage/HRESULT and in-flight sequence/staging leaf are retained. The generic
Job `Publish` helper and all job/worker records remain unchanged.

One Task ownership gate separates waiting, primary Data, finalizing and closed.
Only the Data owner may finalize an active job; duplicate callbacks never create
another job. Callback registration/revocation happens outside locks, including a
subscription that reenters finalization before returning its token. Exactly one
finalizer retains Task/deferral and attempts deferral completion once, including
diagnostic exceptions. The existing `CompleteJob` call and its subsequent
`completion.json` publication stay in their original order and failure semantics.
Diagnostic failures never rewrite that job outcome or a published PDF.

The finalizer asynchronously waits through the published `finish`: **1500ms**
drain allowance and **up to500ms** independent terminal-receipt allowance within
one **2000ms absolute added wait**. First OS/internal cancellation tightens the
deadline to **first request+250ms**, inclusive of existing job cleanup, not another
allowance afterward. If already expired, there is no added diagnostic wait.
An auto-reset signal and timed async waits replace neither the job deadline nor
the existing callback watchdog. No STA `.get()`, storage polling, retry or deletion.
Normal WinRT awaiters restore the finalizer's calling COM context; file work runs
on the background pool with the existing MTA-usage guard.

A separate bounded diagnostic receipt has schema
`4d-pdf-printer.activation-journal-terminal`, version3, at:
`LocalCache\DesktopPdfJournal-<activation32hex>.json`. A native backend creates its
same-directory `.partial` with CREATE_NEW, writes <=4096UTF8bytes, flushes, closes,
then uses no-replace `MoveFileFromAppW`. The existing package-name/publisher guard
applies; no new capability, custom DACL/SID, alternate path or fallback identity.
This avoids reusing FileIO safe-save for the error sink, not the underlying disk.
The atomic-visibility strategy still needs supported-filesystem validation; neither
flush nor rename promises power-loss durability or a physical I/O time limit.

The historical v2 compact ASCII encoder retains its reviewed2300-byte envelope.
The v3 encoder uses a conservative3300-byte limit: the new fixed `j` field is
bounded by1000bytes over the old envelope (including both copies, maximum-width
integers and tuple/key punctuation). The backend cap remains4096bytes.
Out-of-domain observations explicitly
fail diagnostic reporting rather than truncate, normalize or downgrade. Historical
v1/v2 receipts remain accepted only with their explicit expected version.

The immutable receipt freezes **before** its own I/O and the deferral attempt.
It carries reason, drain state (`drained/failed/deadline/canceled/unavailable`),
accepted/dequeued/published/finish cursors, rejection counts, monotonic deadline,
in-flight stage/leaf, first raw HRESULT and nullable sequence/checkpoint. Its
`deferral_release_observed` is always false: it cannot attest a later return,
process exit or DLL unload. An in-flight `cancel_requested` describes an attempt
already observed at decision time, not an acknowledgement or future cancel call.
A late successful rename never upgrades a frozen timeout/cancellation decision.
The sink's own first failure and any secondary close error are separately
debugger-reported with stage/code; missing final receipt
or staging alone is unknown, never success. No recursive durable fallback/retry.

Outstanding event/report/cancellation workers retain buffers/state and the **own
C++/WinRT module lock** until their continuations clean up. Cancel is best effort,
not proof I/O stopped. Timing bounds application waiting, not blocked COM/filesystem
calls, OS scheduling or the Windows background-task execution quota. A deferral
does not guarantee survival past that quota; a module lock does not keep a process
alive. A wedged filesystem can retain work beyond the finalizer deadline; this is
not a process-wide resource/repeated-reliability guarantee.

Future observers must read/hash **finalized immutable files only**, using compatible
sharing. Active pending/`.~tmp`/`.partial` entries get names/kinds/length metadata,
not restrictive-share content hashing. Preserve raw metadata before interpreting;
unknown staging, missing terminal or incomplete/lost events blocks full journal
certification. Existing consumed operators and their failed results are unchanged;
this is not an allowlist, new collector or retrospective evidence normalization.

Portable tests exercise production journal/gate/budgets, races, retained failures,
late publication, the no-retry backend algorithm and injected native-stage errors.
Native fixtures execute real tracked FileIO, native no-replace publication,
injected failures/pending operations and the real Task finalizer with a test
deferral. They do not activate a PSA, install a package, print, or prove actual
AppContainer access/background-task scheduling. Native compile/run results remain
separate exact-commit CI evidence. Deployment and later print trials remain on hold.

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

Source guards also require exact Git objects `ae53b92a5e1f3fa442f41246079cde7e2f4afd47`
and frozen integration baseline `245373012067c928781e5c85b7aafddd191b800d`, alongside
the dependency and geometry refs above. CI explicitly fetches all four comparison
objects; a local native checkout must contain them before validation/build, not
rely on incidental checkout depth. No local fetch or operator is authorized here.

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

The diagnostic extension was tested at `750272a8deba819e3ee8fef60b32080672fbaf7c`
in run41054339960: x64 passed25/package; ARM64 passed19/25, package skipped.
All six ARM64 failures reported the same profile-directory allow ACE: type0,
flags0, mask1048608 (`0x00100020`). No account/capability purpose is inferred.
Publication tests report the original HRESULT and exception message before any
branch assertions. The existing-target case requires the unchanged exact
`destination exists/unsupported` semantic reason, an ordinary-file attribute
snapshot and unchanged hash/no intent; an unrelated early failure is not a pass.
On the existing ACL allow-list rejection only, production exception context adds
the rejected directory, numeric SID, ACE type/flags and access mask (decimal).
The diagnostic extension left allow/deny/INHERIT_ONLY ordering and the
user/SYSTEM/Admin rule unchanged.
Diagnostic failures retain the original exception; no permissions are changed.
This context is exception data, not a new production log/telemetry upload:
current Host/Client error paths still emit HRESULT-only failures. Controlled CI
test catches can include it in their text evidence; local user test logs stay
local and must not be automatically uploaded without privacy review.
In original run41043771157 at `a5382e0`, x64 passed25/package; ARM64 passed20/25
with five failures and package skipped. That run discarded publication messages,
so it alone did not establish their common cause. Its validation case reported
an ACL rejection before metadata-negative checks, not their coverage. The later
diagnostic run above established the identical rejection in all six failures.

### Narrow directory traversal policy correction (source only, natively unrun)

The shared production `NonPropagatingDirectoryTraversalGrant` predicate adds an
intentional rights-based alternative for an applicable allow ACE from an otherwise
untrusted principal: **only a subset of FILE_TRAVERSE | SYNCHRONIZE**, with flags
**0 or INHERITED_ACE alone**. All four subsets, including empty, are permitted.
All other rights and flag combinations are rejected by this exception, including
OI/CI propagation, NO_PROPAGATE, audit/unknown flags, generic/MAXIMUM_ALLOWED
rights, read/list/data/attributes/EA, create/write/delete and security-control
rights. Prior INHERIT_ONLY skips, deny/type handling and trusted-principal
acceptance are unchanged. No SID exemption, effective-access engine or ACL write.

Directory traversal does not grant child-file data access. Bit0x20 is also
FILE_EXECUTE on files, so the exception must not propagate. See Microsoft's
[access-right constants](https://learn.microsoft.com/en-us/windows/win32/fileio/file-access-rights-constants),
[file security](https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights)
and [ACE inheritance rules](https://learn.microsoft.com/en-us/windows/win32/secauthz/ace-inheritance-rules).
Profile/topology/provider checks, rejection diagnostics and no-replace semantics
are unchanged. Existing validation-case source calls the SAME predicate for the
observed mask, four subsets, all32 mask bits (disallowed bits alone/combined)
and all256 flag bytes for every allowed subset, before live SafeParents checks.
These new native assertions and real-profile/publication behavior are **unrun**;
no filesystem ACL edits or new runner/target/CTest are introduced.

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
