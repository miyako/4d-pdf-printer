# PSA v4 feasibility prototype

**Windows-service printing is deferred: native named-user SCM/session-0 submission fails at
`StartDocW` on the reported host, with or without `lpszOutput` (error 3003, no job
ID). The same service submits to inbox XPS.** Both product queues now exist after
the normal ARM64 0.1.2.0 update, and desktop PSA activation/capture is observed.
Neither desktop success nor inbox submission establishes PSA service support.
This is a diagnostic Windows 11 24H2+ (build 26100) PSA application, not a legacy
v4 driver, PDF implementation, finished installer, or 4D plugin. It deliberately
does not launch a renderer. See the [current service evidence and source
trace](validation.md#named-user-session-0-submission-gate) and
[one bounded next test](runbook.md#one-instrumented-service-submission).

Desktop-first development is now authorized independently under
[`desktop/` and its separate package](../desktop/README.md). Its early
activation/authentication/exact-job gate does not produce PDFs. The service
investigation below is historical/deferred, not a prerequisite for that approved
desktop scope or a request for more service-host tests. These OXPS diagnostic
queues and their LocalState remain unchanged.

| Claim category | Evidence / limit |
|---|---|
| Confirmed by source | PSA v4 API is introduced in 26100 / UniversalApiContract 19. Manifest supports OXPS, multiple endpoints and OS Save As. Packaged-app-to-Win32-service RPC is a documented transport pattern; logoff is a task cancellation reason. See [sources](sources.md). |
| Compile checked | [Recorded Windows run and artifact evidence](validation.md): MSVC compile, probe argument tests, direct DLL factory smoke test and MakeAppx validation. Architecture/PE/build-host evidence is separate from queue registration, PDC semantic acceptance and service activation. |
| Runtime observed on Windows 11 | Native ARM64 / 26200.9711, product 0.1.2.0 / Ok, both queues. Desktop Save As writes diagnostic OXPS; desktop no-file endpoint captures OXPS but `GetTargetFileAsync` returns 0x80070490. Named-user session-0 product submission fails; inbox XPS service submission returns job 6. |
| Still unverified | Service-token package/profile visibility and actual PSA task activation; platform cause/supportability of the service failure; pre-login/post-sign-out operation; ticket semantics, authenticated destination/coordinator transport, cancellation/repeated-job resilience, PDF generation and 4D. |

## Implemented boundaries

`printer/psa` is a native C++/WinRT AppContainer background-task DLL with an
explicit activation factory and in-process-server manifest registration.
There is a minimal, blank CoreApplication shell (close it after opening; it is
not a helper). Launching the shell is not a prerequisite implemented by this
prototype. PSA activation is entirely the platform's print contract.
There are no NuGet/runtime framework packages, full-trust capability, network
capability, broad filesystem capability, tray app, or process-spawning code.

The manifest declares two clearly diagnostic queues. Both are now observed on
the user's host. The earlier controlled ARM64 comparison isolated equivalent
PRI-backed names as the registration correction; do not reopen PDC, trust or
console-architecture experiments.

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

Use Windows with Visual Studio 2022 C++ desktop tools (MSVC v143), CMake 3.24+,
PowerShell 7, and **Windows SDK 10.0.26100.0**, including C++/WinRT headers,
WindowsApp import libraries and **x64 MakeAppx/MakePri/SignTool host tools**.
For ARM64, select **MSVC v143 - VS 2022 C++ ARM64 build tools**
(`Microsoft.VisualStudio.Component.VC.Tools.ARM64`), not ARM64EC.
On an ARM64 host use native Visual Studio 2022 17.4+; on x64 this component
cross-compiles ARM64. The x64 target requires
`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`.
Missing components fail explicitly; the build does not install them.
For a VS 2026-only host, explicitly add `-VisualStudioVersion 2026`, install
CMake 4.2+ and the **v143 14.44** component
`Microsoft.VisualStudio.Component.VC.14.44.17.14.ARM64` (or `.x86.x64` for x64).
This still requests v143, not VS 2026's default v145. The native ARM64 CI image
currently has VS 2026, so that job selects this option explicitly.
Choose the VS version before the first build for that target; changing generators
in an existing CMake cache fails explicitly, with no automatic cache deletion.
No WDK, third-party driver, renderer, or product license is selected here.

```powershell
pwsh -File printer/tests/Check-Source.ps1
pwsh -File printer/tests/Check-BuildSupport.ps1
# Default remains x64, even from an ARM64 console.
pwsh -File printer/build.ps1
# Explicit development signing; creates a CurrentUser\My signing certificate.
pwsh -File printer/build.ps1 -Sign
# Native ARM64 package/probe; independent CMake cache, staging and output.
pwsh -File printer/build.ps1 -Architecture ARM64 -Sign
# Alternative for a VS 2026-only host (choose one generator, not both in one cache):
# pwsh -File printer/build.ps1 -Architecture ARM64 -VisualStudioVersion 2026 -Sign
```

| Target | Output base | Package / certificate / probe (relative to base) |
|---|---|---|
| x64 (default) | `printer/out` | `PsaDiagnostic.msix`, `PsaDiagnostic.cer` (with `-Sign`), `build/Release/PrintProbe.exe` |
| ARM64 (`-Architecture ARM64`) | `printer/out/arm64` | Same filenames; **all under this independent base** |

Each base also contains `package/` (unpacked MSIX) and `build-evidence.json`.
The source manifest is an x64 template; the build generates the selected
`ProcessorArchitecture` (`x64` or `arm64`) and verifies it against the actual PE
machine types (`0x8664` / `0xaa64`) of shell, task DLL, submission probe and
factory test. It checks every EXE/DLL in staging and the **actual MSIX ZIP**,
rejecting mismatches rather than merely trusting a filename or CMake flag.
The development version is **0.1.2.0 for both targets**, same name/publisher.
The two queue names now use equivalent `ms-resource` references. MakePri compiles
the two original strings into `resources.pri`; the build validates its resource
map/values and requires a nonempty PRI in staging and the actual MSIX. This is
an observed static-registration compatibility correction, not a blanket claim
that literal DisplayName is invalid. The PDC, native activation and task/job
code, URIs, formats, device family and capabilities are unchanged.

The script reports native host OS architecture, PowerShell process architecture,
requested target, absolute output paths and test-execution mode separately.
CMake explicitly selects `-A x64` / `-A ARM64`, v143 and a VS instance with the
requested component. The SDK version is pinned and checked after selection.
SDK tool paths always end in `bin/10.0.26100.0/x64`, **not** the target folder;
these x64 tools use Windows 11 emulation on an ARM64 host. A shell change never
changes the requested target. See [primary sources](sources.md).

Portable architecture/negative packaging tests run for every build. The three
native CTests run when target equals native host; x64-on-ARM64 execution is
explicitly labeled emulated. ARM64-on-x64 builds explicitly report CTests
**not run** rather than attempting unsupported execution; run them on ARM64.
`build-evidence.json` records that distinction. CI includes native x64,
native ARM64 (`windows-11-arm`) and ARM64-on-x64 cross-compilation, with separate
architecture-named artifacts. The additional native CLI evidence test checks
read-only context, binary/job hashes and a deliberately nonexistent queue's
failure. These tests do not call `Run`, install a printer, submit a job or
exercise SCM/session-0 activation.

Generated solid-color PNGs are original diagnostic assets.
`MakeAppx pack` uses normal validation (no `/nv`).
The build workflow **does not sign, trust, install, or submit jobs**.
The hosted OS may be Windows Server; that is a build environment, not requested
or established Windows Server OS runtime support.
A separate [bounded registration discriminator](validation.md#generated-artifact-comparison-and-bounded-discriminator)
signs/installs only on a disposable native Windows 11 ARM64 runner and never
submits jobs. It is not a user-host installation helper or a product fix.

On an isolated Windows 11 26100+ lab machine, an administrator must review and
trust **only the generated development certificate** in the appropriate Trusted
People store, then install the MSIX for the test identity with `Add-AppxPackage`.
For example, after checking the certificate's subject/thumbprint:

```powershell
# Lab administrator only; do not run on production machines.
Import-Certificate -FilePath .\printer\out\PsaDiagnostic.cer `
  -CertStoreLocation Cert:\LocalMachine\TrustedPeople
Add-AppxPackage .\printer\out\PsaDiagnostic.msix
Get-AppxPackage -Name Miyako.PsaFeasibility | Select-Object Name, Version, Architecture, Status
Get-Printer
```

For the reported host already running ARM64 0.1.1.0, **do not use the x64
example above**: use the [normal 0.1.2.0 resource correction update](runbook.md#resource-packaging-correction-update).
Every `-Sign` run creates a **new certificate**; trust the matching certificate
from the selected output base after checking its subject/thumbprint, not the
certificate from another architecture or earlier build. Normal installation
success still does not establish that either queue was created.

This makes no production signing, Store approval, WPP compatibility or all-user
deployment claim. Package registration is user-scoped; provisioning/staging an
MSIX is not proof of registration or activation for a service user. Do not copy
private keys or credentials into the repository. Development trust is a deliberate
machine change: remove this test package/certificate after the experiment.

Follow [the runtime evidence runbook](runbook.md) and
[conditional worker handoff](handoff.md). The capability XML is a reduced,
attributed adaptation of the Microsoft sample; see
[`printer/THIRD-PARTY-NOTICES.txt`](../../printer/THIRD-PARTY-NOTICES.txt).
