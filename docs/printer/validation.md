# Actual validation evidence

Recorded 2026-10-28 for commit
`380bf166ef0a4ccbab76e063e170c31b38c68c4b`.

[Successful Windows build run](https://github.com/miyako/4d-pdf-printer/actions/runs/40643581337)
on GitHub-hosted **Windows Server 2022 Datacenter, build 20348**,
image `20261022.344.1`; MSVC `14.44.35207`, Windows SDK `10.0.26100.0`.
This is a compiler/test runner, **not** a Windows 11 PSA runtime/service host,
and does not establish support for Windows Server OS.

| Check | Actual result |
|---|---|
| Portable source-invariant checks | Passed on macOS and Windows. |
| PowerShell parser, whitespace | All printer scripts parsed; `git diff --check` passed. |
| Native Release/x64 build | `PrintProbe.exe`, `PsaTask.dll`, `PsaDiagnostic.exe`, `ActivationProbe.exe` built. |
| CTest `probe-arguments` | Passed; ten native parser cases, no printer accessed. |
| CTest `task-factory` | Passed; DLL factory export, valid `IBackgroundTask` activation/runtime name and unknown-class rejection. Does not call `Run`, install a package or activate PSA. |
| MakeAppx | Normal SDK validation passed; unsigned MSIX created without `/nv`. |
| Artifact inspection | Downloaded CI artifact; x86-64 PE probe/DLL, manifest, PDC, four generated PNGs and third-party notice present. No renderer or mislabeled PDF payload. |

Artifact name: `printer-feasibility-unsigned-x64`.
The historical unsigned MSIX SHA256 from that run is:

```text
00f52ffec897fd87df3b764c6dd613b4a0bd5076735c173d10b480794bf93f70
```

Binary hashes are not promised reproducible across builds (PE timestamps/package
metadata); the build **procedure** is reproducible and pinned to SDK 26100.
CI artifacts have GitHub retention limits; use the workflow to rebuild.

Earlier iterations found and corrected missing explicit SDK projection headers,
the activation ABI/typed factory consumer, and MakeAppx's requirement that
virtual-printer workflow applications declare multi-instance support.
The portable invariant check now enforces that manifest declaration.

## User-reported Windows 11 queue-registration experiment

Reported 2026-10-28, separately from hosted CI. Environment: Windows 11
Enterprise 25H2, build 26200.9711, local SDK 26100 build/signing. No account
identifiers, machine names, install paths or raw event logs are retained here.
Native CPU Architecture **12 (ARM64)** was subsequently confirmed.
The observed 0.1.0.0 package was x64 under emulation; switching to an x64 console
still left both diagnostic queues absent. Native x64 runtime was never tested.
This identifies the host, **not the cause** of the unexplained registration failure.

| Observation | Evidence / limit |
|---|---|
| Local development package installation | User reports trusted development certificate, successful `Add-AppxPackage`, and package `Miyako.PsaFeasibility` version 0.1.0.0 x64 with Status Ok. This is not a successful printer installation. |
| Installed manifest | Both intended `PrintSupportVirtualPrinter` nodes are present in namespace `http://schemas.microsoft.com/appx/manifest/printsupport/windows10/2`, with intended queue names/URIs, OXPS input and `Config\DiagnosticPdc.xml`. Only the interactive node declares `OutputFileTypes="oxps"`. |
| Installed PDC | File exists. Reported SHA256 equals the Windows CI package's CRLF bytes exactly; no further content dump is needed to establish those bytes. This does **not** establish semantic acceptance by the print deployment handler. |
| Printer enumeration | Unfiltered `Get-Printer` lists only the two inbox document writers, neither diagnostic queue. **Queue-registration gate failed.** No wildcard-filter diagnosis is warranted. |
| PrintService/Admin | No matching events in the queried interval. This is not proof of handler success or absence. |
| Package deployment events | Full activity-correlated output includes fresh Add, signature validation, initial preferred-version selection and successful Add completion, with no error or explicit print-handler invocation/result. This does not establish whether the handler ran or accepted the PDC. |
| Printing, service, 4D and worker | Not attempted. No workflow activation or downstream success/failure can be inferred from this earlier installation-stage result. |

Expected SHA256 for the unchanged `DiagnosticPdc.xml` at commit `7318067`:

```text
LF:   7eede5839af460ed587b8f48232d1cbf6dce1364f7060dc9332986b452bb2925
CRLF: e38b25327081ec310b21c5cb077b018ccc8c71090d7ccd09e42bcf6b9a6b4e2c
```

The reported installed hash is the CRLF variant, exactly matching the downloaded
Windows package from [run 40650171835](https://github.com/miyako/4d-pdf-printer/actions/runs/40650171835).
The difference from the LF git checkout is line endings, not changed XML content.

### Source inspection and remaining discriminator

The [manifest specification](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/msix-manifest-specification-print-support-virtual-printer)
requires a package-relative, valid PDC and describes queue creation by a
deployment extension handler. The declared namespace, category, relative path,
OXPS preference and application multi-instance flag match the documented/sample
shape. The reduced PDC contains the same four standard feature categories as
the [Microsoft UWP sample](https://github.com/microsoft/print-oem-samples/blob/652343e4980cc962fb099496125cf749aee51c71/PSASamples/UWP/CSharp/PrintSupportApp/Config/PrinterPdc.xml):
media size, orientation, resolution and color. The inspected sources do not
establish an omitted mandatory feature or a required network/full-trust
capability for this local virtual-printer registration. Sample IPP networking
permissions are not justification for adding them.

This comparison is **not** a Windows PDC semantic validator or evidence that the
runtime handler implemented/accepted this registration. No behavior patch is
justified by these observations alone. The full Add operation's
activity-correlated events, obtained using [Get-AppxLog](https://learn.microsoft.com/en-us/powershell/module/appx/get-appxlog)
without a per-message keyword filter, still do not expose a handler result.
Native ARM64 target support was authorized as the next compatibility/control
test. Architecture causation remains unproved.
Handler-specific diagnostic evidence remains needed; do not infer a feature
prohibition or fix native job code that has not been reached.

Microsoft's [deployment troubleshooting reference](https://learn.microsoft.com/en-us/windows/win32/appxpkg/troubleshooting)
documents error 0x80073CFB for non-bitwise-identical rebuilt/resigned packages
with an existing identity/version. Preserve installed-vs-built evidence before
any controlled update. Merely rerunning installation is not proof that the DEH
reran. The observed fresh Add/initial preferred-version selection does not
support a pre-existing same-version registration as the explanation for this
initial failure.
No removal/reinstallation or manual printer/driver workaround is prescribed.

## ARM64 compatibility/control build

Version **0.1.1.0** keeps the original package name/publisher and provides separate
x64 and ARM64 builds, without changing PDC, capabilities, print workflow code,
renderer or service behavior. The [build guide](README.md#build-and-installation)
describes the explicit selector and host/target/tool separation.
Portable source checks and **23 synthetic build-support cases** passed on macOS:
all four host/target plans, default/invalid selectors and SDK, generated manifests,
PE-header/machine validation, staged/archived mixed-target and missing binaries,
and stale package version rejection. Synthetic PE/ZIP tests are not executable
Windows tests or MakeAppx schema validation.

Windows CI now requests native x64 (`windows-2022`), native ARM64
(`windows-11-arm`) and ARM64 cross-compilation on x64. The
[GitHub runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners#standard-github-hosted-runners-for-public-repositories)
lists `windows-11-arm` as ARM64 for public repositories; workflow execution also
asserts the native OS architecture. Actual Windows results/artifact revision must
be recorded separately; adding the matrix alone proves no successful build.
The jobs do not install the package, create queues or run service/4D experiments.
The user-run native ARM64 update/registration retest is still pending.

## Still not run / not proven

Windows 11 queue creation has not succeeded. Semantic PDC acceptance, real
XPS/OXPS capture, Save As, native output-path mapping, named-service account
registration/activation, pre-login execution, post-sign-out execution, 4D printing,
external coordinator IPC and PDF rendering remain unobserved in this printer PR.
No PDF size or repeated-job reliability metric has been measured.

Use the [runtime runbook](runbook.md) to close those gaps. Build or direct COM
factory success is not a substitute for the required session-0 activation gate.
