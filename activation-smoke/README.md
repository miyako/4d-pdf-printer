# PSA activation smoke (no PDF)

**Review-only experiment, not yet an observed activation proof.** This separately
named package answers just one question: can a real PSA
`VirtualPrinterDataAvailable` callback use the documented FullTrust launcher to
start its fixed packaged desktop host, whose normal exit is observed through an
exact process handle? Compilation and unpackaged tests cannot answer that question.

Identity: `Miyako.PsaActivationSmoke`, version `0.1.0.0`, publisher
`CN=DesktopPdfPrototypeDev`. Its only queue is
**4D PDF Activation Smoke (No PDF)**, URI `psa-activation-smoke:synthetic`.
The manifest requests OXPS, has no `OutputFileTypes`, and has no Save As
or target-file call. The `windows.fullTrustProcess` extension is attached to
`Printer`, with `runFullTrust`, and names only `SmokeHost.exe`.

No PDF, renderer, publication, destination, authentication, 4D integration,
service, resident broker, login/startup/tray component, RPC, or network is present.
The stopped desktop/renderer experiments and their packages, queues, worktrees,
diagnostics, and unused signed checkpoint must remain untouched. This code does
not use their SID/token/DACL/request machinery. A test ID is correlation, not
authority; hostile same-user software is outside this proof.

## Mechanism and pass gate

`Invoke-Smoke.ps1` checks the separately installed package and exact queue, creates
a fresh 32-character lowercase hexadecimal ID, and starts its installed
`SmokeObserver.exe --observe ID`. The observer uses
`ApplicationData.Current.LocalCacheFolder` (no guessed package-data path) and a
new `ActivationSmoke/ID` subfolder. It starts only a second copy of **itself**
with `--submit ID`; that child uses normal GDI and one original synthetic page,
with the ID in the document title. There is no input fixture/document.

Only the actual data callback in `Task.cpp` calls
`LaunchFullTrustProcessForCurrentAppWithArgumentsAsync("--test-id ID")`.
The desktop programs never invoke that API or start the host. The callback
records entry, workflow session, content type and printer URI before launch;
metadata is diagnostic, not an AppContainer/SID admission predicate. Record-write
errors are explicit and do not silently substitute a successful launch or prevent
an otherwise valid callback from trying the launcher. Missing records cannot pass.

The windowed-subsystem host strictly parses that argument, records ID/PID/process
creation time, then waits at most 10 seconds for `ready.json`. The observer opens
a handle with `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION`, checks the exact
installed host image and process creation time, **then** writes that simple
observation-ready record. The host returns normally with code 0; without the
ready record it returns nonzero. The observer retains the handle and uses
`WaitForSingleObject` and `GetExitCodeProcess`, not a process-list absence or a
self-reported exit file.

The final `observation.json` keeps these fields separate:
`actualCallbackObserved`, `launchResult`, `hostEntryObserved`,
`hostExitObserved`, `activationFeasibilityPassed`, and `pdfProduced=false`.
Pass requires the real matching-ID callback record, launcher `Success`, verified
host entry/handle, observed exit code 0, and no callback logging failure.
`launch.json` also retains raw launch enum, extended HRESULT, failure API/stage,
and cancellation/late-launch uncertainty. A partial result is incomplete or
failed evidence, **not** a blanket platform prohibition.

The print job is deliberately completed **Failed**, or **Canceled** on
cancellation. Failed spooler status and an unsuccessful GDI `EndDoc` are expected
possibilities: raw `StartDocW`, `EndDoc`, page/draw returns and immediately captured
last-error-on-failure are in `gdi.json`. They are not the activation pass gate.
An activation pass is emphatically not a successful printer/PDF result.

## Bounds, records, and errors

The background-task deferral, session event handlers and coroutine strong
references retain callback lifetime. Waiting for the actual callback is limited
to 45 seconds. Launcher status is polled asynchronously for at most 15 seconds,
without blocking an STA with `.get()`; cancellation explicitly calls `Cancel`.
Neither timeout nor cancellation excludes a late host launch.

The MTA desktop observer has a 45-second observation deadline and a brief owned
submitter wait (1 second), followed if needed by exact-handle termination and a
2-second exit-observation wait. Failed termination or unobserved exit blocks the
overall pass and is recorded separately without replacing a primary activation
error. Normal nonzero GDI submitter exit is not itself cleanup failure. No
processes are selected by name. The wrapper has a 55-second deadline; if needed,
it requests termination of its exact owned observer **tree** and checks a
2-second observer-exit wait. Secondary cleanup uncertainty is preserved in its
report/stderr without replacing the primary error. That wait observes the
observer, not every descendant individually, and the system-launched host is
not covered by the tree kill. A host launched after that
deadline still has its own 10-second ready-record timeout. These are bounded
application waits, **not** hard OS-hang/termination guarantees or lifetime
reliability claims; synchronous GDI/OS/storage calls can still stall. No repeated
automatic attempts are made.

Records are small UTF-8/no-BOM JSON files, create-new pending files renamed
fail-if-exists after write. No records are overwritten. There is no prepared
request registry, authorization handshake, nonce, lock/claim/expiry framework or
custom security descriptor. `ready.json` exists only so observation can acquire
the process handle before normal host exit. Failures name the API/stage and raw
HRESULT on stderr or (for the AppContainer/windowed components) debugger output;
callback/launch records preserve available diagnostics. Record failure is itself
inconclusive evidence, not success.

The wrapper saves bounded raw stdout/stderr privately (64 KiB each), plus
create-new `wrapper.json`. Its report describes transport/execution, not an
independent activation pass. Primary process errors survive secondary report-write
errors. No font/input-document/render output or private paths are uploaded.

## Build and evidence (not installation)

`build.ps1` is an isolated **unsigned**, native-only x64/ARM64 build with MSVC
v143 and SDK `10.0.26100.0`; VS 2022 and VS 2026 select the already merged small
build-plan/PE/manifest-architecture helpers. CMake lists the WinRT `windowsapp`,
`ole32`, and GDI `gdi32` dependencies explicitly. Only the merged minimal
FrameworkView bootstrap and diagnostic PDC are reused, not old activation code.
Fresh outputs are under `activation-smoke/out/ARCH/UNIQUE/`, never old build paths.
No signing, certificate creation/import, trust/policy modification, installation,
or printing is performed by the build.

Tests cover strict IDs/titles/arguments, record parsing, PID/creation-time
parsing, wrong image/time rejection, and controlled child timeout/exit0.
The production owned-submitter cleanup helper is exercised with real handles:
normal exit1, denied termination/unobserved exit, and actual termination followed
by observed exit. Wrapper
tests execute the actual process-capture/report body, including exact known raw
UTF-8 bytes independent of console encoding, nonzero exits, deadlines,
create-new collisions and primary-error retention. A controlled process tree
exercises the actual production wrapper timeout/`Kill(true)`/checked wait; the
test holds the exact descendant handle before termination and observes its exit.
These tests run unpackaged;
they do **not** establish packaged ApplicationData or PSA behavior.

The narrow pull-request workflow checks out the **exact PR head SHA**, builds
original source on native x64 and ARM64, runs these tests, validates PE
architectures and the unsigned MSIX, verifies the packaged manifest/host
extension/name and exact compiled binary hashes, and dumps the actual packaged
PRI to verify its identity and queue string. Only text/JSON evidence is uploaded,
never executables, DLLs, MSIX, caches, or renderer dependencies.

This is one deliberately authorized native batch. **On its first native
compiler/test/package failure, stop and report the exact diagnostic; do not
repair/re-run automatically.** Existing workflows were inspected first: printer
and renderer path filters do not cover this new directory. No assumption is made
that a new `workflow_dispatch` is registered on default.

## Proposed one-test handoff (parent review; not authorization)

After source/native-evidence review, installation/signing is a **separate explicit
consent step**, using only the already trusted matching development certificate.
No user certificate thumbprint or machine-specific path is public configuration.
No existing package/queue/data is reused, upgraded, removed or reset.

Only after that consent and verified separate installation would the parent
provide one guarded PowerShell command block, enclosed in `& { ... }`, invoking
`Invoke-Smoke.ps1` with a new absolute private evidence directory. That single
invocation submits one original synthetic job and collects the pass-gate evidence.
Do not invoke the host manually as a substitute, repeat printing automatically,
or infer activation from CI. Missing entry/handle/exit remains incomplete.
This document intentionally provides **no executable Windows command block**;
the coordinating parent reviews and supplies any user commands.

API references:
[FullTrust launcher with arguments](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher.launchfulltrustprocessforcurrentappwithargumentsasync?view=winrt-26100)
(Desktop 22000+, FullTrustAppContract v2),
[launch result/extended error](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslaunchresult?view=winrt-26100),
[virtual-printer event arguments](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.workflow.printworkflowvirtualprinterdataavailableeventargs?view=winrt-26100).
