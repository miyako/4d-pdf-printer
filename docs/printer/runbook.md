# Windows 11 runtime evidence runbook

**Current gate: product 0.1.2.0 has both queues and desktop capture works, but
native named-user session-0 `StartDocW` fails (3003) while inbox XPS submission
works.** See [observed evidence](validation.md#named-user-session-0-submission-gate).
Do not repeat registration, certificate or old service-control experiments.
Pre-login, sign-out and 4D tests remain on HOLD.
Use synthetic pages only. Do not
upload account names, paths, private print tickets/documents, credentials or full
Windows event logs to this public repository.

## Evidence and identities

Use an isolated Windows 11 24H2+ machine with SDK/build outputs from this PR.
Record OS edition/build/patch, package version/hash, commit, test case, UTC
timestamps and a synthetic job token in a private evidence bundle. Record local
identity/session and package/queue visibility **privately**; sanitize them before
sharing. Label every observation as observed, failed, or not run.
Establish native CPU architecture separately from the package/process architecture
(for example, `Get-CimInstance Win32_Processor | Select-Object Architecture`).
The original artifact targeted x64; successful x64 package installation alone does not
establish a native x64 host. The original observation is now confirmed to be
CPU Architecture **12 (ARM64)**, with the x64 0.1.0.0 package installed under
emulation. Switching from an ARM console to an x64 console still showed neither
diagnostic queue. This does **not** demonstrate architecture caused the failure.
Native ARM64 builds are now available as a compatibility/control test, not a
proven registration fix; renderer/plugin ARM64 support is outside this change.

Install the diagnostic MSIX for the desktop test identity. Separately determine
whether supported deployment can register it for the **named service identity**
without requiring an interactive login at runtime. Check that identity's package,
queue and profile/data availability, not merely an administrator's `Get-Printer`.
Do not assume `Add-AppxProvisionedPackage`, staging or another user's install
registers it for an existing service account. If deployment requires a one-time
setup login, record that fact; the actual pre-login test is a cold reboot with no
desktop session for any user. A scheduled task is not a Windows-service test.

Find private package captures (for the identity actually activated):

```text
%LOCALAPPDATA%\Packages\<Miyako.PsaFeasibility package family>\LocalState\
  Captures\<random activation id>\activation.json
  Captures\<random activation id>\job.json
  Captures\<random activation id>\ticket.xml
  Captures\<random activation id>\source.oxps
  Captures\<random activation id>\completion.txt
```

`source.partial`, missing `completion.txt`, only `activation.json`, or a stale
`CaptureLock` are evidence of an incomplete run, not successful conversion.
Inspect captured ZIP entries for OXPS fixed-document/page namespaces and document
sequence; MIME/extension alone does not prove payload conformance. Open only
synthetic captures in a suitable XPS/OXPS viewer. Compare the SHA256 of the known
synthetic job name to `jobTitleSha256`; `workflowSessionSha256` is not a GDI job
ID. The probe logs numeric GDI job ID, UTC FILETIME, process/session and stage.
Titles, accounts, queue names and destinations are not written to its JSON log.

## Native ARM64 update and retest

**Historical procedure, already completed on the reported host:** ARM64 0.1.1.0
updated normally and is healthy, but both product queues remain absent.
Do not repeat this historical update, reinstall the successful reference control,
or collect the same deployment logs again. The
[bounded hosted discriminator](validation.md#generated-artifact-comparison-and-bounded-discriminator)
isolates a queue-resource packaging correction, implemented separately in
0.1.2.0. Use the normal higher-version update below, not another 0.1.1.0 rebuild.

For the existing x64 0.1.0.0 installation, first preserve the original installed
manifest/PDC/hash, package identity/version/architecture, complete Add activity
and printer-enumeration evidence **privately outside the checkout**. Preserve
any existing package LocalState/captures before deployment; never assume a
deployment operation is an evidence backup. No additional content dump is
required to re-establish the already-confirmed PDC-byte match.

Use the reviewed ARM64-support revision and the prerequisites in the
[build guide](README.md#build-and-installation). Build/sign explicitly:

```powershell
pwsh -File printer/build.ps1 -Architecture ARM64 -Sign
if ($LASTEXITCODE -ne 0) { throw 'ARM64 build failed; do not install existing/stale outputs.' }
# Expected: printer/out/arm64/{PsaDiagnostic.msix,PsaDiagnostic.cer,build-evidence.json}
# Probe: printer/out/arm64/build/Release/PrintProbe.exe
# Generated manifest: printer/out/arm64/package/AppxManifest.xml (arm64, 0.1.1.0)
Get-Content .\printer\out\arm64\build-evidence.json
Get-PfxCertificate -FilePath .\printer\out\arm64\PsaDiagnostic.cer |
    Select-Object Subject, Thumbprint
```

For a VS 2026-only host, use
`pwsh -File printer/build.ps1 -Architecture ARM64 -VisualStudioVersion 2026 -Sign`
**instead**, with the additional v143/CMake prerequisites in the build guide.
Do not silently switch generators in an existing CMake cache.

On the confirmed native ARM64 host, the evidence must say
`targetArchitecture=arm64`, `hostOSArchitecture=Arm64`,
`testExecution=native`, `ctests=passed`, and `peAndManifestChecks=passed`.
`processArchitecture` may differ if PowerShell itself is emulated.
Cross-compilation on x64 instead records `not-run-cross-compile` / `not-run`;
that is not a native self-test. A local ARM64 build automatically runs both
CTest tests (synthetic arguments and DLL factory only).

The name/publisher remained unchanged; **both targets then used version 0.1.1.0**,
higher than the observed 0.1.0.0. After checking the new certificate's exact
subject/thumbprint, have an authorized lab administrator trust the **matching**
ARM64-build certificate, then attempt the normal same-family higher-version
update as the intended test identity:

```powershell
# Lab administrator only: deliberate certificate trust change.
Import-Certificate -FilePath .\printer\out\arm64\PsaDiagnostic.cer `
    -CertStoreLocation Cert:\LocalMachine\TrustedPeople
# Same test identity as the old package; no Remove-AppxPackage or force flags.
Add-AppxPackage -Path .\printer\out\arm64\PsaDiagnostic.msix -ErrorAction Stop
```

Each `-Sign` run creates a new certificate; the old trusted certificate alone
does not establish trust in this new signature. Do not rerun signing and then
use a stale certificate/package from another output base. CI artifacts are
unsigned; the command above is for the locally built/signed package, not the
unsigned download.

Microsoft documents [same-family updates with Add-AppxPackage](https://learn.microsoft.com/en-us/powershell/module/appx/add-appxpackage).
A higher version avoids reusing the original identity/version, but does **not**
guarantee cross-architecture update or queue creation on this host. If deployment
rejects the update, stop and retain the HRESULT/ActivityId privately; do not
force shutdown/downgrade/reinstall or automatically remove the old package.
No manual removal is currently prescribed. If Windows ultimately requires one,
it would de-register the old package/queues and can remove package app data;
first review the exact error, preserve LocalState/evidence and get explicit
approval for that separate destructive step.

The **first retest is package architecture plus unfiltered enumeration**, not
printing or service setup:

```powershell
$packages = @(Get-AppxPackage -Name Miyako.PsaFeasibility -ErrorAction Stop)
$packages | Select-Object Name, Version, Architecture, Status
Get-Printer -ErrorAction Stop
if ($packages.Count -ne 1 -or $packages[0].Version.ToString() -ne '0.1.1.0' -or
    $packages[0].Architecture.ToString() -ne 'Arm64' -or $packages[0].Status.ToString() -ne 'Ok') {
    throw 'Expected exactly one installed ARM64 0.1.1.0 package with Status Ok; stop.'
}
```

Then apply the exact-name gate below. If either queue remains absent, preserve
the new Add activity separately from the original x64 experiment and stop.
If both appear, that is an observed native ARM64 package-registration result,
**not proof that architecture alone caused/fixed the old failure**, nor evidence
of capture, unattended output or named-user service behavior.

## Resource packaging correction update

For the reported host already running **ARM64 0.1.1.0 / Status Ok**, use the
reviewed follow-up PR revision implementing **0.1.2.0**. Leave the working
Microsoft reference installed and do not launch either application or submit a
job. Preserve existing LocalState and private evidence; no removal, forced
shutdown, downgrade, driver/service/policy change or same-version reinstall is
part of this retest.

On the existing VS2022/v143/SDK26100 ARM64 build host, from that reviewed checkout:

```powershell
pwsh -NoProfile -File printer/build.ps1 -Architecture ARM64 -Sign
if ($LASTEXITCODE -ne 0) { throw 'Build/sign failed; stop without installing stale outputs.' }
Get-Content .\printer\out\arm64\build-evidence.json
Get-PfxCertificate -FilePath .\printer\out\arm64\PsaDiagnostic.cer |
    Select-Object Subject, Thumbprint
```

Require version **0.1.2.0**, `targetArchitecture=arm64`, native/passed CTests,
PE/manifest checks passed, and verified queue-name resources. Each `-Sign` run
creates a new certificate: an authorized lab administrator must review and trust
**only this run's matching public CER**, subject `CN=PsaFeasibilityDev`, with the
displayed thumbprint:

```powershell
# Administrator, only after reviewing this exact new certificate.
Import-Certificate -FilePath .\printer\out\arm64\PsaDiagnostic.cer `
    -CertStoreLocation Cert:\LocalMachine\TrustedPeople
```

Return to the same normal user's **Windows PowerShell 5.1** for local Appx and
PrintManagement, not a PowerShell 7 compatibility proxy:

```powershell
$ErrorActionPreference = 'Stop'
Add-AppxPackage -Path .\printer\out\arm64\PsaDiagnostic.msix
$package = @(Get-AppxPackage -Name Miyako.PsaFeasibility)
$package | Select-Object Name, Version, Architecture, Status
if ($package.Count -ne 1 -or $package[0].Version.ToString() -ne '0.1.2.0' -or
    $package[0].Architecture.ToString() -ine 'Arm64' -or
    $package[0].Status.ToString() -ine 'Ok') { throw 'Expected healthy ARM64 0.1.2.0; stop.' }
Start-Sleep -Seconds 10
Get-Printer
```

Expected names remain **PSA Diagnostic OXPS (Save As)** and
**PSA Diagnostic Capture (no file UI)**. If missing at 10 seconds, wait once until
60 seconds and enumerate without reinstalling. Stop there if either is missing;
do not repeat certificate/install/log loops. Queue presence closes only this
user-host registration gate, not task `Run`, printing, Save As, fidelity,
service/session-0, renderer or 4D validation.

## Queue-registration gate before desktop printing

Package installation success and `Get-AppxPackage` Status Ok are not queue
registration evidence. Check the expected names against **unfiltered**
`Get-Printer` output under the same test identity before submitting:

```powershell
$expected = @('PSA Diagnostic OXPS (Save As)', 'PSA Diagnostic Capture (no file UI)')
$installedPrinters = @(Get-Printer -ErrorAction Stop)
$missing = @($expected | Where-Object { $_ -notin $installedPrinters.Name })
if ($missing.Count) {
    throw "Queue registration not established; stop printing tests. Missing: $($missing -join ', ')"
}
```

If queues are missing, preserve the original deployment evidence before changing
or reinstalling anything. Confirm both virtual-printer declarations in the
**installed** manifest (including enclosing Extension Category/EntryPoint and
application multi-instance attribute), and the installed PDC's existence/hash.
Compare to the same build's unpacked package rather than assuming a same-version
rebuild replaced its installed contents. LF/CRLF hash variants of the unchanged
diagnostic PDC are documented in [validation evidence](validation.md).

Use the deployment activity/correlation ID to obtain its complete event chain,
without filtering individual messages for the package name or PDC:

```powershell
# Use the ActivityId of the relevant package Add operation, not an unrelated install.
Get-AppxLog -ActivityId '<deployment-activity-guid>' |
    Select-Object TimeCreated, Id, LevelDisplayName, Message
```

Retain this output privately and redact user SIDs, account names and paths before
sharing. Microsoft's [deployment troubleshooting guide](https://learn.microsoft.com/en-us/windows/win32/appxpkg/troubleshooting)
starts with AppXDeploymentServer/Operational. An empty PrintService/Admin query
does not prove a successful deployment extension handler (DEH) operation. Neither
does the absence of handler messages in keyword-filtered deployment output.
If the complete activity still does not expose a handler/PDC result, report the
gap rather than inferring that the handler ran, was absent, or that the PDC was
accepted. Do not add a queue manually or change capabilities, print drivers,
runtime trust level or PDC contents to make this gate appear to pass.

## Desktop cases (only after the queue-registration gate passes)

```powershell
$probe = (Resolve-Path .\printer\out\build\Release\PrintProbe.exe).Path
# ARM64 host after the native ARM64 update: use this INSTEAD of the x64 path.
# $probe = (Resolve-Path .\printer\out\arm64\build\Release\PrintProbe.exe).Path
# OS Save As baseline; choose an .oxps destination.
& $probe --queue 'PSA Diagnostic OXPS (Save As)' --interactive `
  --job-name 'synthetic-desktop-save' --timeout-seconds 120

# Test lpszOutput on file queue: does it bypass OS Save As and yield exact target?
& $probe --queue 'PSA Diagnostic OXPS (Save As)' --noninteractive `
  --output 'C:\PsaLab\synthetic-file-route.oxps' --job-name 'synthetic-file-route'

# No OutputFileTypes, both with and without native output path.
& $probe --queue 'PSA Diagnostic Capture (no file UI)' --noninteractive `
  --output 'C:\PsaLab\synthetic-no-ui-route.oxps' --job-name 'synthetic-no-ui-route'
& $probe --queue 'PSA Diagnostic Capture (no file UI)' --noninteractive `
  --job-name 'synthetic-no-target'
```

Use new destinations per run. `--noninteractive` means **the probe itself never
opens a dialog**, never calls a UI launcher, and bounds submission using a
disposable-process watchdog. It cannot forbid the print system from showing
Save As. Any OS dialog in this mode is a **failed unattended experiment**.
There is intentionally no undocumented "suppress all PSA UI" flag.
`--timeout-seconds` is the submission allowance (default 60, range 1..300).
`--delay-seconds` is a separate startup delay (default 0, range 0..86400).
The watchdog starts before that delay and the synchronous queue/GDI calls;
its total budget is **delay + timeout**, not timeout alone. A delay greater than
the timeout is valid: no `delay < timeout` restriction applies. Malformed or
out-of-range timing arguments fail explicitly with exit 2 before submission.
The watchdog kills only this disposable submitter if blocked. A killed submitter
may leave a spooled job or OS dialog; inspect/cancel that exact job before retrying.
Zero exit means GDI accepted `EndDoc`, **not** PSA success. Automated capture
intentionally completes Failed even if the submission process exits zero.

For each case collect: did OS UI appear, did `StartDoc` return, numeric job ID,
activation/process evidence, input MIME/payload validity, target API return/error,
requested completion and `CompleteJob` return evidence, spooler outcome, exact
destination existence/size/content, and whether an unintended destination changed.
Repeat interactive cancellation, read-only/nonexistent parent, occupied file,
missing package for identity, missing queue and denied queue permission.
File queue failures can occur **before PSA activation**; no capture folder does
not by itself identify why activation failed. Correlate sanitized
PrintService/AppModel diagnostics privately with the timestamps.

## Real named-user service probe

The setup and acceptance matrix below describe the eventual gate, **not the
next action on the reported host**. Its existing one-shot service is stopped,
Manual, and last targeted the **inbox XPS control**, not the product. Preserve
that account/configuration and use only the bounded diagnostic test below after
review. Do not configure Automatic, reboot, sign out, or repeat Save As tests now.

### One instrumented service submission

This follow-up changes the **standalone probe only**; product identity/version,
MSIX, task DLL, PDC, PRI and queues are unchanged. No package update, development
certificate/trust change, SDK upgrade, service recreation or password handling
is needed. Use the reviewed CI native ARM64 `build/Release/PrintProbe.exe`, or
build just that target from the existing configured ARM64 build directory:

```powershell
cmake --build .\printer\out\arm64\build --config Release --target PrintProbe
if ($LASTEXITCODE -ne 0) { throw 'Probe build failed; stop.' }
```

An authorized administrator can replace only the stopped service's protected
probe executable. Privately verify its SHA256 against the selected artifact or
new local build, then run `PrintProbe.exe --version` (must report
`PrintProbe 2 (arm64); evidence schema psa-probe-v2`). A new local build has its
own hash; filenames/version alone do not identify the bytes.
Do not execute the service command directly in a console and call it SCM evidence.

**One next test:** keep the same existing named-user SCM account and signed-in
desktop state. Change **PathName only** through the approved private workflow
(for example `Invoke-CimMethod Win32_Service.Change` with only `PathName`).
Use the no-file product queue, **omit `--output`**, new synthetic title/log,
delay 0 and timeout 90. The generic binary-path shape is:

```text
"C:\PsaLab\bin\PrintProbe.exe" --service --noninteractive --diagnose-context --queue "PSA Diagnostic Capture (no file UI)" --job-name synthetic-service-context-04 --log "C:\PsaLab\evidence\service-context-04.jsonl" --timeout-seconds 90
```

Paths are examples, not the user's private locations. Check that the log does
not already exist. Preserve the existing account, Manual startup and recovery
Take No Action. Start **once through SCM**, wait for the stopped state, inspect
its reported exit code, and retain the complete new JSONL privately. Do not
repeat the already successful inbox control. No logged-in helper is introduced.

Schema **`psa-probe-v2`** deliberately distinguishes:

| Evidence | Meaning / limit |
|---|---|
| `probeVersion`, `processArchitecture`, `executableSha256`, `jobTitleSha256` | Exact executable and UTF-8 title-hash correlation on every record. Title hash must match task `job.json` if one exists; it is not authentication. Startup hashing failure/early watchdog can leave empty hashes and a failed run. |
| `serviceMode`, process/session/time | Requested SCM mode and actual submitter session. `serviceMode` alone is not proof of service logon. |
| `submission-context.tokenSessionId`, `logonType`, `logonQueryNtStatus` | Own process token/LSA query, taken after delay immediately before queue access. Service logon type is 5, distinct from interactive 2 / remote-interactive 10. A nonzero NTSTATUS or null type is an unknown/failed query, not "interactive false." No SID, account name or authentication LUID is emitted. |
| `profileHiveOpenWin32Error` | Read-only open of `HKEY_USERS\<own token SID>`; 0 means accessible mounted hive, 2 means absent at this observation, 5 means access denied/unknown. Does not read profile contents or establish AppContainer LocalState access. It never loads/repairs a profile. |
| `packageQueryWin32Error`, `currentUserPackageCount` | Native `FindPackagesByPackageFamily` HEAD sizing query for the product's public name/publisher under the service process's current user. 122 is the expected sizing response when space is needed; 0 can mean no matches. Other codes produce null count. Count is registration visibility, **not** package health/activation/broker availability. No cross-user install, WinRT initialization or desktop-token impersonation occurs. |
| `startDocReturn`, `startDocLastError` | Raw signed return and immediately captured error, before logging/cleanup. LastError is cleared before the call; nonzero error on success is not failure, zero error on nonpositive return is not success. No job ID is invented from a negative return. |
| `probe-failed.win32Error`, `failureStage` | Preserves a captured API failure and its fixed source-defined stage (including a context-query failure); uses ERROR_GEN_FAILURE (31) if no nonzero code exists. Unlike v1's generic terminal error 0, this cannot look like success. No free-text exception is written to JSON. The detailed preceding stage remains authoritative. |

Within the same private time window, retain any **already-enabled** relevant
PrintService, AppModel-Runtime and TWinUI events with ActivityId/RelatedActivityId.
Do not enable channels, change policies or export full machine logs publicly.
Read-only inspection of `PrintWorkflowUserSvc*` state can accompany this one
test; do not start/reconfigure it. An instance for an interactive logon is not
evidence of an instance available to the service logon. The documented suffix
identifies a user context; do not invent an exact token-LUID/suffix mapping.
Inspect fresh `activation.json` as well as exact-title-hash `job.json` privately;
no matching job is not proof that `Run` was never entered. Earlier activation
can fail before either file exists.

**Stop/decision:** if the service token sees no product registration or cannot
access its hive, report that precise boundary without auto-provisioning,
loading a hive, impersonating a desktop user or changing capabilities. If those
queries succeed but `StartDocW` still fails without an exact capture, the
remaining gap is inside the Windows submission/workflow activation boundary.
Stop with these records and request Microsoft-supported activation semantics
and a correlated internal error/trace for a type-5/session-0 caller. Neither
3003 nor a stopped per-user service proves universal PSA prohibition.
Do not construct a file-queue reference service test: its Save As dependency is
a confounder. No faithful independently identified no-file reference variant
was built in this follow-up. Hosted CI has no authorized runner-user service
password; LocalSystem and manufactured-account provisioning are not substitutes.

### Eventual setup and acceptance matrix (on HOLD)

`PrintProbe.exe --service` implements the SCM service entry point itself; do not
register an ordinary console program and call that a working service.
It performs one submission then stops. Setup must be done manually by an
authorized lab administrator; the repository never creates accounts/services,
changes printer settings, or embeds credentials.

Copy the executable matching the installed package/host (ARM64 source:
`printer/out/arm64/build/Release/PrintProbe.exe`; x64 source:
`printer/out/build/Release/PrintProbe.exe`) to a protected fixed path, e.g.
`C:\PsaLab\bin\PrintProbe.exe`. Create a separate `C:\PsaLab\evidence` directory
and grant only the named service user/admins the needed access. Give the service
user write permission to the synthetic output directory, read/execute on the
binary, permission to print to the test queue and "Log on as a service".
Keep the executable and service configuration non-writable by untrusted users.

Use `sc.exe create` to register an ordinary own-process service named
**PsaPrintProbe**, then configure its named-user account in the Services
management UI through an approved private administrator workflow.
Never put a password into a committed script, command transcript or CI secret
for this experiment. Suggested binary path including arguments:

```text
"C:\PsaLab\bin\PrintProbe.exe" --service --noninteractive --queue "PSA Diagnostic Capture (no file UI)" --output "C:\PsaLab\evidence\synthetic-service.oxps" --job-name synthetic-service --log "C:\PsaLab\evidence\service.jsonl" --timeout-seconds 90 --delay-seconds 120
```

For example, in an elevated PowerShell session, assign the entire binary-path
line above to `$binaryPath` as a single-quoted string, then:

```powershell
sc.exe create PsaPrintProbe type= own start= demand binPath= $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Service registration failed' }
services.msc
```

**Do not start it under the default Local System account.** In Services, set
**Log On** to the named valid user, verify its credentials privately, then select
**Automatic** for the cold-boot case and record configuration privately. The
delay supports a sign-out test and is **added to** the submission allowance.
The exact command above waits 120 seconds and has a 210-second watchdog budget
(120 + 90), leaving a nominal 90 seconds for submission after the delay.
The 300-second maximum limits the timeout argument, not the combined budget.
The probe JSON records `delaySeconds`, `timeoutSeconds` and
`watchdogBudgetSeconds` so the actual selected values can be checked.
Do not enable service recovery retries. Use `sc.exe start PsaPrintProbe` and
`sc.exe queryex PsaPrintProbe` to start/inspect the one-shot test; distinguish
SCM errors, the service's reported exit code and workflow evidence.

| Case | Required procedure and acceptance evidence |
|---|---|
| Service with desktop present | Start through SCM. Log must show service submission in Windows session 0, not a console impersonation. Compare package/queue visibility, PSA identity/session privately and target behavior. |
| Before first desktop login | Configure Automatic, cold reboot, **do not log in**. Observe remotely through an independently authorized admin channel, or retrieve evidence later while verifying timestamps precede first login. Delayed startup/activation requiring login is a failure. |
| After sign-out | Start delayed one-shot service; fully sign out (not lock or disconnect), leave no user helper/session, let delay elapse. Retrieve timestamped evidence later. Also test a service start initiated remotely while signed out. |
| Sign-out during active capture (this PR) | Submit a synthetic job and sign out while PSA capture/output is active. Observe task cancellation/completion, the exact spooler job's final state, and any complete/partial OXPS destination independently. No helper may preserve the desktop session. |
| Sign-out during active conversion (later integration; not executable in this PR) | Once a permitted coordinator/worker exists, sign out after the worker starts but before publication. Separately record PSA task cancellation/`CompleteJob` result, spooler job completion/cancellation, coordinator and worker process lifetime/deadline/cleanup, and exact destination publication (absent, partial, valid complete). A surviving service or a completed PDF alone is not proof the required PSA job finished successfully. Do not retry an uncertain publication automatically. |
| File queue `lpszOutput` | Repeat service cases with Save As queue and fresh `.oxps` destinations. Any blocked UI, timeout, absent activation or wrong target fails the unattended case. |
| Registration/profile/permission negative cases | Missing package for service user, unavailable profile, queue denied, destination denied, missing target and UI-ineligible states must fail explicitly, not hang indefinitely or report PDF success. |

The probe JSON proves the **submitter's** numeric session, not the PSA host's.
Use OS tracing/debugger/Process Explorer in the private lab to establish the
activated host's token/session/package identity, including whether it survives
sign-out. Capture those private facts in sanitized yes/no evidence rather than
publishing machine/account data. Review SCM exit code and `queryex` state as well
as the log. Service exit success still does not establish workflow success.

Do not proceed to worker integration merely because a job reaches `StartDoc`.
Require actual background activation/source evidence in each state and a
supported authenticated coordinator handoff. No tray/helper session may be
introduced to make a failing test pass.

Microsoft documents logoff as a background-task cancellation reason. Test both
an active job interrupted by sign-out and a **new** job submitted after sign-out;
these are different cases. A deferral does not guarantee survival or unlimited
runtime. Preserve ambiguous publication evidence and do not retry automatically.
If activation passes, separately validate the [local-RPC service candidate](handoff.md):
correct package/user ACLs, wrong-user/package rejection, endpoint absent,
disconnect/timeout/cancel, worker crash with no JSON, and abandoned job-local files.
The documented generic RPC pattern is not already wired into this package.
Do not copy the CustomCapability sample's capability identity, signing or
provisioning assumptions. General package-SID RPC ACL guidance does not establish
restricted/custom-capability approval or an SCCD deployment for our package.
No such approvals/descriptors were obtained. Validate independent package trust
and service authorization, including correct-user/package and wrong-user/package
cases, before treating the transport as usable in this distribution.

## Repeated jobs and later 4D checks

Initially submit serially and wait for workflow completion/evidence, not only
`EndDoc`, before the next job. Use unique names/destinations and stop after
timeouts/stale locks. Observe distinct activation IDs, process reuse, failures,
memory/handle growth and recovery; do not promise numerical reliability or PDF
size improvement. Printer-side containment cannot repair caller GDI misuse or
upstream Windows faults.

Only after native gates close, test 4D 19 (limited support acceptable), current
desktop/remote clients and named-user 4D Server service independently.
`SET CURRENT PRINTER` precedes `SET PRINT OPTION`. Explicitly choose our queue,
not **Generic PDF driver** (that chooses Microsoft's). Destination option 9:
printer=1, file=2, PDF=3; custom queue routing must be tested, not assumed.
Document-name option is 12. Serialize the entire native printing lifecycle on
4D 19; plugin `threadSafe` flags cannot repair printing/settings concurrency.
4D 20 R4+ non-blocking printing differs. The plugin tutorial is
`miyako/4d-plugin-tutorial`, branch `example`; no plugin is implemented here.
Use the [4D service-account guide](https://developer.4d.com/docs/server/service)
for account setup only, not as evidence of PSA support.

Remove the lab service, installed diagnostic package and specific development
certificate when finished, using their recorded names/thumbprints. Preserve only
sanitized synthetic evidence; delete captures locally after review. Never remove
whole profile/package directories indiscriminately.
