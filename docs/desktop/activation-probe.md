# Actual PSA callback activation probe

**Step2 only; not a PDF pipeline.** The new `ActivationProbe` build mode bypasses
the retained RPC sources and includes no renderer/runtime dependencies. Actual
callback-to-host activation remains **unproven** until the separately approved
normal-desktop observation. Native compilation and controlled-child units cannot
pass that gate. Full B, comparison continuation, destinations/UI and services
remain paused. No PDF is produced; the PSA deliberately completes Failed/Canceled.

**Historical first-attempt STOP checkpoint:** the initially authorized native batch
[40832146213](https://github.com/miyako/4d-pdf-printer/actions/runs/40832146213)
tested source `765d6a75064e63beb961822771d8793a4f67fdbf` and **failed
compilation on x64 and ARM64**. `handle` is ambiguous between the existing
`desktop::handle` and imported `winrt::handle`, first at Contract.h:68, with
further occurrences and cascading reset/nullptr errors. The App bootstrap and
PSA DLL linked on both architectures, but host/control/unit compilation did not
complete. Native units, MSIX packaging and activation were **not executed**.
[Exact failure receipt](evidence/40832146213.json). Portable checks passed;
they did not cover Windows-only name lookup or the consented Windows wrapper.
At docs checkpoint `918a4de`, no repair/rerun was performed and that source was
not ready for a host run.

**Separately authorized surgical follow-up passed:** fresh manual run
[40832585234](https://github.com/miyako/4d-pdf-printer/actions/runs/40832585234)
tested source `b8f4b7b1156ad61ac922f01265d35c19371b39ca` with clean source
receipts. The only source change is13 explicit `desktop::handle` qualifications
in four new probe files; no shared types/namespaces, protocol, ACLs, timings or
expected assertions changed. Both x64 job123309968974 and native ARM64
job123309968621 passed full own-code compilation, one native controlled-file/
process CTest each (3.27s/3.24s), unsigned MSIX/PE/manifest/PRI checks and
PowerShell writer/consent/manifest guards. See the
[fresh run receipt](evidence/40832585234.json), original
[x64 build receipt](evidence/40832585234-x64.json) and
[ARM64 build receipt](evidence/40832585234-arm64.json).
Public artifacts contain JSON/text only, not binaries.

This resolves the compile blocker, **not the actual PSA activation gate**.
No AppContainer file sharing, system FullTrust launch, package installation,
printing or consented Windows wrapper body was exercised. The normal-desktop
procedure below is ready for **parent review only**, not authorized execution.
At that compile checkpoint, development-family and signing-certificate state
were still unknown. The later supervised desktop state is described below.

### Subsequent actual desktop preparation stop

The separately supervised native ARM64 desktop attempt with the user's clean
`b8f4b7b` build subsequently installed the new development package and passed
identity/hash preflight, but stopped at **prepare**, before any observer,
synthetic submission or actual PSA callback. The first result/old directories
must remain preserved. A subsequent existing-output-directory rejection did not
replace that first result. This is not an activation failure or evidence against
the healthy diagnostic package. Certificate/trust/installation are not repeated
by the preparation-diagnostic fix.

Source inspection finds the native `--prepare` dispatch and expected
`prepared-private-correlation`/64-lowercase-hex contract agree. A native failure
returns exit1 and a JSON HRESULT, often still at `control-arguments` even when
`Current()`/`Prepare()` failed; that stage alone does not identify the failing API.
The old wrapper consumed stdout into an unsaved private variable and discarded
native exit/error details when its generic contract check or JSON parsing failed.
Its saved PowerShell RuntimeException `0x80131501` is **not** a Win32 or launcher
root cause.

The masking was reproduced locally by executing the actual original
prepare/catch/finally/report body with a simulated native exit1/JSON
`0x80070005`: only generic `0x80131501` survived. That access-denied value is
**synthetic test data, not the user's observed native error**. The actual native
cause remains unknown.

The narrow PowerShell-only correction uses the existing subprocess stream-pump
pattern, with fixed alias/`--prepare`, separate create-new
`prepare.stdout.private`/`prepare.stderr.private` files, at most16KiB retained
per stream, a45s capture deadline and bounded exact-client exit observation.
It records process start/exit/exit code, byte counts, capture API/limit/deadline
failures and allowlisted native stage/HRESULT without publishing job IDs, raw
stdout/stderr, paths or exception messages. It no longer relies on ambient
`$LASTEXITCODE`. `failure.private.json` preserves the bounded original exception
message; a result-write collision retains the primary exception and typed
secondary `ReportWriteError`. Existing files are never overwritten.
Native code, package/alias registration, protocol, ACLs and request expiry are
unchanged. This capture behavior has **not** been exercised against the Windows
AppExecutionAlias; no native rebuild, CI dispatch or host retry is included.

`Check-ActivationProbe.ps1` now also runs16 actual production prepare/parser/
catch/finally/report cases: valid preparation, native JSON error/exit1,
empty/malformed/array output, wrong stage/ID, stdout/stderr floods, invalid UTF8,
timeout, process-start error and stdout/stderr/private-error/result collisions.
Only the native executable is replaced by a controlled local PowerShell child;
capture, real file I/O and terminal error handling are production code.
Preflight/ACL checks are outside that extracted test segment, not bypassed in
production. Neither observer/print execution nor Windows alias behavior is
proven by these tests. The retained7+9 harness cases still pass locally.

No large ACL/metadata collection is requested to infer a discarded HRESULT;
existing metadata cannot reconstruct it. Preserve all first-attempt records.
The parent-authorized **source change**, not host execution, adds the small
`-PreparationOnly` branch with separate `-AllowPackagePreparation` consent.
Native preparation writes package-local state, so this is not a read-only
diagnostic. It uses the same production capture/contract/error path and all
existing desktop/package/build/hash/path/ACL checks, but cannot reach either
`--observe` or `--print`, even if preparation succeeds or print consent was also
supplied. It always reports `activationGatePassed:false`, `pdfProduced:false`
and mode `preparation-only-not-activation`; valid preparation gets that result
label, while errors remain blocked with explicit diagnostics. No expired job
records are removed, request replaced, or result overwritten. A fresh protected
output root is still required.

The report separately records `wrapperVersion:2` and the SHA256 of the executing
wrapper. Existing `sourceCommit`/own-code hashes continue to identify the
unchanged native build/installed package, not the updated script. The16 actual
body tests now execute the preparation-only branch with the real alternative
print branch present and trap any observer/submission process call; success,
native error, malformed output, timeout and collision paths all keep the gate
false. Separate real-script tests enforce the new explicit consent and platform
guards. No C++ change, native rebuild, CI dispatch or user execution is included.
Only the parent may review and seek consent for one subsequent preparation-only
call; the full synthetic-print procedure below remains paused. Do not retry
preparation, reinstall, alter certificates/COM/profile/DPI or print automatically.

Future parent command blocks must use an enclosing `& { ... }` so a terminating
guard error aborts the whole pasted block; independently pasted top-level
statements in an interactive shell can otherwise continue after a prior error.
This does not blame the caller or authorize reusing the preserved output root.

## Supported primitives, not an assumed callback result

The [PSA v4 guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/print-support-app-v4-design-guide)
defines `windows.printSupportVirtualPrinterWorkflow` and its
`VirtualPrinterDataAvailable` background event. Omitting `OutputFileTypes`
retains the no-file-UI endpoint. This does **not** make DOCINFO an application
destination carrier; the existing actual diagnostic observations still apply.

[FullTrustProcessLauncher](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher?view=winrt-26100)
supports a same-package full-trust component with `runFullTrust` and a
[`windows.fullTrustProcess` extension](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop-extension).
[`LaunchFullTrustProcessForCurrentAppWithArgumentsAsync`](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher.launchfulltrustprocessforcurrentappwithargumentsasync?view=winrt-26100)
accepts runtime arguments on Desktop22000+, FullTrustAppContract v2. These are
not a manifest ParameterGroup ID. Its
[result](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslaunchresult?view=winrt-26100)
provides launch status/extended HRESULT, **not** a PID or process handle.
The documented threading model is Both. These contracts justify testing this
bridge, not claiming this particular PSA host/context will accept it.

The fixed target is `DesktopPdfProbeHost.exe`; only the actual callback calls
the launcher, with exactly `--job <64 lowercase hex characters>`. The desktop
control never calls FullTrustProcessLauncher. No arbitrary executable, path,
command, foreground fallback, AppContainer CreateProcess, environment carrier,
custom entitlement, COM/profile change or resident coordinator is introduced.
The old desktop `.get()`0x80010117 and StartDoc3003 observations at `d58bb2f`
remain historical, not results from this callback.

## Minimal ownership and lifetime contract

The current-user package's
[`ApplicationData.LocalCacheFolder`](https://learn.microsoft.com/en-us/uwp/api/windows.storage.applicationdata.localcachefolder?view=winrt-26100)
contains `ActivationProbe-v1/<random job ID>`. This is package-local data, not a
global next-job setting. The full-trust preparation creates a protected DACL
for exactly the current user, SYSTEM and derived package SID, with an inherited
low-integrity label for AppContainer access. Native opens reject reparse points,
wrong owners/ACLs and unexpected types; pinned directory handles deny deletion.
Same-package sharing follows the documented
[IPC/package boundary](https://learn.microsoft.com/en-us/windows/uwp/communication/interprocess-communication).
It is **not** protection against hostile full-trust software running as this same
user. The random title is correlation, not sole authentication.

The immutable request binds user hash, Windows session, package-full-name hash,
endpoint and30s expiry. Preparation is serialized; another unexpired request is
busy, and eight retained job directories exhaust this deliberately small probe.
No automatic pruning or retry. Files are at most16KiB; exclusive creation of a
pending file and rename without replacement make claims/records one-time.
The actual callback binds the complete `PSAPROBE1:<ID>` title, real workflow
SessionId hash, AppContainer context and request hash before launch. The workflow
SessionId is not equated to a spooler job number.

The one-shot host verifies those bindings, freshness and no closed marker, then
claims `host-entry.json` once, with PID/creation time. It waits at most3s for the
observer to hold and validate that exact process: expected installed EXE,
package full name, actual user/session and PID creation time. The observer
publishes `observing.json`; only then may the host atomically acknowledge and
exit0. The callback validates acknowledgment, records raw launch results and
CompleteJob(Failed) return separately. The observer requires that terminal
callback record, acknowledgment and actual process exit0, plus no matching
same-package/session host remaining **at that observation**, before gate pass.

Callback polling is bounded by12s and request expiry; known-host exit waits5s;
desktop wrapper watchdog45s. Closed/expired/duplicate requests cannot produce
a valid late acknowledgment. Cancellation requests the system async operation's
Cancel, which **does not guarantee no later process launch**. Failure evidence
retains this uncertainty. The observer may terminate/reap only an identity-
validated process handle; an unseen process cannot be declared reaped. A late
host should reject expired/closed state and exit; actual Windows scheduling or
an indefinitely blocked filesystem/API call is not a hard termination guarantee.
No OS/spooler crash-immunity claim.

## Bounded local evidence

Private records contain the correlation ID but no document bytes/destinations;
never upload them. Sixteen fixed background-entry log slots make a missing
prepared-job record distinguishable from a logged entry failure. Per-job records
capture stage/API HRESULT, package/user/session hashes, process architecture/PID,
apartment, launch result/extended HRESULT, workflow hash, acknowledgment,
creation time, exit code and cleanup/late-launch uncertainty. The source/build
receipt carries exact source SHA, dirty state, own-code hashes and package/PRI
hashes. Native observer output copies only explicitly allowlisted failure fields.
No automatic upload or font/content collection. No worker stdout is changed.

Normal errors stay visible; private stderr/debug messages are separate from
single JSON stdout. `result.json` is create-new UTF8 without BOM; collisions
never overwrite and a report failure does not replace the primary exception.
No source OXPS is drained or saved in this activation-only probe.

## Single native build/unit batch

The manual-only `desktop-windows.yml` mode `ActivationProbe` compiles x64 and
native ARM64, runs native controlled-process/file units, validates actual
PE/manifest/PRI/MSIX wiring, and uploads text/JSON receipts only. No signing,
trust, installation, GDI submission, PSA callback, dependencies or renderer build.
SDK26100 headers and `windowsapp`/Ole32 cover WinRT; Advapi32 covers ACL/token
APIs, Userenv covers `DeriveAppContainerSidFromAppContainerName`, Shell32 covers
`CommandLineToArgvW`; only the synthetic submitter links Gdi32/Winspool.
RPC targets/MIDL are bypassed. Native units use a separately controlled copy of
the unit EXE in a kill-on-close Job Object, **not** the system full-trust launcher.

Portable tests execute strict token/expiry cases, the actual PowerShell JSON
writer's Unicode/collision/bound paths, consent/platform guards and manifest
checks. They do not execute the consented Windows wrapper body. Native units
exercise protected file/Unicode/replay/expiry/session/request mismatches,
unprotected ACL rejection, child acknowledgment+exit0, and missing-observer
bounded exit1/no acknowledgment. They do not validate AppContainer broker ACL
inheritance or real package activation; those are precisely the desktop gate.

## Parent-reviewed procedure: one native ARM64 desktop observation

**Not authorization to execute. Parent supplies commands only after review.**
Use a normal signed-in, non-elevated native PowerShell7 desktop on the existing
Windows11 ARM64 machine, VS2022/v143/SDK26100. No renderer/Python/Poppler work.

1. Record the approved source SHA and clean source state. Record
   `Get-AppxPackage` identity/version/status for BOTH `Miyako.PsaFeasibility` and
   `Miyako.DesktopPdfPrototype`, plus their existing queues. Preserve all old
   roots/receipts. Stop if the probe package is already version0.2.0.0 or newer,
   has another publisher, or existing development state cannot be preserved;
   never solve an update error by uninstalling.
2. Build only `./desktop/build.ps1 -Mode ActivationProbe -Architecture ARM64
   -VisualStudioVersion 2022 -CertificateThumbprint <explicit development cert>`.
   The build does not create certificates/trust/install. A matching
   `CN=DesktopPdfPrototypeDev` signing certificate with private key and explicitly
   arranged development trust are necessary. If absent, separately review the
   smallest development certificate and MSIX trust setup first, including the
   required trust store/elevation as a separate explicit decision; do not
   reuse/re-sign the healthy diagnostic package or purchase signing.
3. Explicitly approve and current-user install/update ONLY
   `desktop/out/arm64/DesktopPdfActivationProbe.msix`. Same development identity
   version0.2.0.0 preserves its LocalState/LocalCache on an ordinary update; it
   replaces the old development endpoints with one automation endpoint and a
   new alias `4dpdf-probe.exe`. This is not a side-by-side update of the old
   development gate. The diagnostic family/queues remain separate. Verify
   registration and the development no-file-UI queue; stop if missing. Do not
   launch the ordinary desktop app as a substitute activation test.
4. Choose ONE new absolute local private report directory under the current
   user's protected profile, not any old proof root. After explicit print
   consent, run `desktop/tests/Invoke-ActivationProbe.ps1` with
   `-PrivateOutputRoot <NEW directory> -BuildEvidencePath
   desktop/out/arm64/activation-build-evidence.json -AllowOneSyntheticPrint`.
   It first verifies installed own-code hashes against that clean local build
   receipt and native architecture. This prepares
   once, starts the observer and submits exactly one original page to the fixed
   development queue. No Save As, PDF, destination or document fixture is involved.
5. Preserve results on success **or failure**; no automatic reprint/retry.
   Review sanitized `result.json`, observer/submission results, raw immediate
   StartDoc/EndDoc returns and private package-local callback/host records as
   needed. Verify exact callback hash/ID bindings, launch result, host entry,
   ack, exit and CompleteJob(Failed) separately. A spooler failure is expected;
   only `activationGatePassed:true` with exact process exit evidence passes
   this narrow gate. Missing callback evidence is **inconclusive about desktop
   architecture**, not failure of the user's healthy diagnostic package.

**Rollback:** first preserve evidence and observe no matching probe host/client
remaining. Never remove by process name or touch `Miyako.PsaFeasibility`, its
certificates/queues or original roots. If the development family did not exist
before this test, removing only its exact recorded package full name is an
optional separately approved cleanup; removal deletes its state. Remove only
a newly created certificate's exact thumbprint if separately approved and not
used elsewhere. If updating a preexisting development family, do not uninstall
or downgrade automatically: retain state and request an explicit rollback plan.
Do not delete package-local records while a system launch might still arrive.

Historical producer success `b33b741`/40825887654, failed40825481671 and the
NoClobber fix remain unchanged. Actual product size/fidelity/native worker
lifecycle proof and every user-host action remain outside this build gate.
