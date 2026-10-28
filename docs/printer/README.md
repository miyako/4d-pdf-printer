# PSA v4 feasibility prototype

**Gate: not established for a named-user Windows service before login or after sign-out.**
This is a diagnostic Windows 11 24H2+ (build 26100) PSA application, not a legacy
v4 driver, PDF implementation, finished installer, or 4D plugin. It deliberately
does not launch a renderer. No Windows/4D runtime host was available during
development. A successful hosted build does not close the gate.

| Claim category | Evidence / limit |
|---|---|
| Confirmed by source | PSA v4 virtual printer API is introduced in 26100 / UniversalApiContract 19. Manifest supports OXPS preference, multiple endpoints and OS Save As with `OutputFileTypes`. See [sources](sources.md). |
| Compile checked | See the **Printer Windows build** Actions run for this PR: native MSVC compile, probe argument tests, direct DLL factory smoke test and MakeAppx manifest/package validation. This is not printer installation, PDC semantic validation or service activation. |
| Runtime observed on Windows 11 | **None.** No installed queue, actual captured job, Save As result, 4D print, worker handoff, or pre-login/sign-out execution is claimed. |
| Still unverified | Queue/package visibility to a service identity; session-0 activation; source MIME type and ticket semantics; `DOCINFO.lpszOutput` to PSA target mapping; no-file endpoint target behavior; allowed authenticated external coordinator transport; cancellation/termination/repeated activation. |

## Implemented boundaries

`printer/psa` is a native C++/WinRT AppContainer background-task DLL with an
explicit activation factory and in-process-server manifest registration.
There is a minimal, blank CoreApplication shell (close it after opening; it is
not a helper). Launching the shell is not a prerequisite implemented by this
prototype. PSA activation is entirely the platform's print contract.
There are no NuGet/runtime framework packages, full-trust capability, network
capability, broad filesystem capability, tray app, or process-spawning code.

The package registers two clearly diagnostic queues:

| Queue | Manifest | Behavior |
|---|---|---|
| `PSA Diagnostic OXPS (Save As)` | `PreferredInputFormat="application/oxps"`, `OutputFileTypes="oxps"` | Capture OXPS and job ticket; obtain OS target `StorageFile`; copy OXPS only when target has `.oxps` extension. Only this diagnostic copy can report `Succeeded`. |
| `PSA Diagnostic Capture (no file UI)` | Same input; **no** `OutputFileTypes` | Capture OXPS/ticket; attempt `GetTargetFileAsync()` and record availability or HRESULT. Never write supplied destination and always report `Failed` (or `Canceled`): no renderer/routing implementation exists. Omission does not establish unattended output support. |

No PDF passthrough is advertised, no bytes are relabeled PDF, and Microsoft's
`XpsToPdf` baseline is not used. Only `application/oxps` is accepted; unexpected
MIME types fail explicitly. A `.partial` file signals incomplete capture;
`source.oxps` is renamed only after a complete copy. Interactive target writes
are diagnostic, not atomic; failure can leave a partial user-selected file.

Each activation gets a random folder and process ID. `job.json` stores hashed
workflow session ID / job title, endpoint, content type, UI eligibility, source
byte count, target availability when returned, last stage, HRESULT on failure,
and requested completion status. `before-target.json` preserves correlation if
target retrieval never returns. It never logs account names, target paths,
source app display names or free-text exception messages. `completion.txt`
records the final status passed to a returning `CompleteJob`, **not** downstream
success. Pre-event activation errors
use a stage/HRESULT debug output and can leave only `activation.json`.
No event-args `GetDeferral()` exists on this API: the task's
`BackgroundTaskDeferral` spans capture and completion.

Start with **one submitted job at a time across both queues**. A package-local
atomic `CaptureLock` folder rejects concurrent capture rather than queuing it.
SDK 26100 MakeAppx requires `uap10:SupportsMultipleInstances="true"` on the
application for these extensions; the manifest declares it. This permits platform
multi-instance activation; it is not an implemented serialized dispatcher or a
promise of a fresh conversion process per job.
This is not a production broker. A crash can leave the lock: stop all test
submissions, verify no task is active, preserve evidence and remove only that
specific folder before resuming. Never automatically delete another job's lock.
The host may reuse its process; this code does not promise one process/job.
The first event per task is accepted, extra events fail explicitly.
Cancellation requests cancel active copying; task completion occurs after job
completion is attempted. Reads/writes have cooperative 90-second / 256-MiB
diagnostic bounds; an individual blocked WinRT I/O or OS kill can exceed/interrupt
these bounds. This is not a hard renderer timeout or crash-containment guarantee.

## Build and installation

Use x64 Windows with Visual Studio 2022 C++ desktop tools (MSVC v143), CMake 3.24+,
PowerShell 7, and **Windows SDK 10.0.26100.0**, including C++/WinRT headers,
WindowsApp import libraries, MakeAppx and SignTool. No WDK, third-party driver,
renderer, or product license is selected here.

```powershell
pwsh -File printer/tests/Check-Source.ps1
pwsh -File printer/build.ps1
# Explicit development signing; creates a CurrentUser\My signing certificate.
pwsh -File printer/build.ps1 -Sign
```

Outputs are under ignored `printer/out`: unsigned/signed `PsaDiagnostic.msix`,
unpacked package, and `build/Release/PrintProbe.exe`. Generated solid-color PNGs
are original diagnostic assets. `MakeAppx pack` uses normal validation (no `/nv`).
The workflow builds but **does not sign, trust, install, or submit jobs**.
The hosted OS may be Windows Server; that is a build environment, not requested
or established Windows Server OS runtime support.

On an isolated Windows 11 26100+ lab machine, an administrator must review and
trust **only the generated development certificate** in the appropriate Trusted
People store, then install the MSIX for the test identity with `Add-AppxPackage`.
For example, after checking the certificate's subject/thumbprint:

```powershell
# Lab administrator only; do not run on production machines.
Import-Certificate -FilePath .\printer\out\PsaDiagnostic.cer `
  -CertStoreLocation Cert:\LocalMachine\TrustedPeople
Add-AppxPackage .\printer\out\PsaDiagnostic.msix
Get-AppxPackage -Name Miyako.PsaFeasibility
Get-Printer | Where-Object Name -Like 'PSA Diagnostic*'
```

This makes no production signing, Store approval, WPP compatibility or all-user
deployment claim. Package registration is user-scoped; provisioning/staging an
MSIX is not proof of registration or activation for a service user. Do not copy
private keys or credentials into the repository. Development trust is a deliberate
machine change: remove this test package/certificate after the experiment.

Follow [the runtime evidence runbook](runbook.md) and
[conditional worker handoff](handoff.md). The capability XML is a reduced,
attributed adaptation of the Microsoft sample; see
[`printer/THIRD-PARTY-NOTICES.txt`](../../printer/THIRD-PARTY-NOTICES.txt).
