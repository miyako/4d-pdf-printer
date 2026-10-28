# Microsoft virtual-printer reference control

This is an isolated **static queue-registration control**, not a product fix,
renderer selection, or proof of service/4D support. Do not remove or reinstall
`Miyako.PsaFeasibility`, delete its LocalState, change policies/services, or add
a printer manually to make this comparison pass.

## Preserved user baseline

The test host is Windows 11 Enterprise 25H2, build **26200.9711**, on Apple
silicon, `Win32_Processor.Architecture = 12` (native ARM64). An x64 shell on it
is emulated; it is not an x64 host.

The original x64 prototype 0.1.0.0 installed/trusted successfully with Status
Ok. Unfiltered `Get-Printer` listed only Microsoft Print to PDF and Microsoft
XPS Document Writer, neither diagnostic queue. Both intended static manifest
nodes were present. The packaged CRLF PDC SHA256 was
`E38B25327081EC310B21C5CB077B018CCC8C71090D7CCD09E42BCF6B9A6B4E2C`.
The LF source hash
`7eede5839af460ed587b8f48232d1cbf6dce1364f7060dc9332986b452bb2925`
differs only in line endings; this is not evidence of a wrong PDC.

A normal signed update to native ARM64 0.1.1.0 also reported Status Ok, but
**neither diagnostic queue appeared**. Native ARM64 did not fix registration.
Both full deployment ActivityId chains showed routine successful deployment
without an explicit print-handler/PDC HRESULT. Native-update events 574/855
confirmed x64 to ARM64, 400 success, and 472 moved the old package to Deleted
with result 0. Later 471/493 concerned unrelated deleted StorePurchaseApp /
WindowsAppRuntime leftovers, not a demonstrated printer cause. Do not delete
WindowsApps files. No print, Save As, service, renderer, or 4D job was run.
This control does not request another dump of those unchanged facts.

## Exact source and isolation

Source: [microsoft/print-oem-samples at
652343e4980cc962fb099496125cf749aee51c71](https://github.com/microsoft/print-oem-samples/tree/652343e4980cc962fb099496125cf749aee51c71).
The commit API resolves that full SHA to the March 30, 2026 documentation
update. [`upstream.json`](../../printer/reference-control/upstream.json)
pins both the full commit and the downloaded tarball SHA256:
`213093c3d6b9cf602a2bda94685b10fbc241879b208d5a8c0d10000ca0377e9d`.
A changed tarball, even for the same URL, fails closed; do not silently update
the hash. GitHub archive compression is not promised byte-stable forever.

The fetcher verifies the archive **before extraction**, then extracts only
the UWP C# `PrintSupportApp`, its `Tasks` project, native `PSASamples/XpsUtil`,
the original NuGet configuration/readme and root MIT license. It does not vendor
the repository into this project. `original-files.json` inventories the exact
extracted bytes. Microsoft copyright and the full MIT license accompany any
distributed sample artifact. This does not select a license for our own product.

| Surface | Upstream / control comparison |
|---|---|
| Identity Name | `dd5de805-d9bb-4785-88e9-399cbff2130e` becomes `Miyako.PsaReferenceControl` |
| Identity Publisher | `CN=Contoso` becomes `CN=PsaReferenceControlDev` for local development signing |
| Version / PhoneIdentity | Preserve `1.1.14.0` and original phone GUIDs; Windows package identity is distinct from the prototype |
| Static extension declarations | Preserve both `printsupport2` extensions, entry points, format lists, PDC path, names/URIs, and other print extensions |
| Names / URIs | `VirtualPdfPrinter` / `contoso-psa:print-to-pdf`; `VirtualPostScriptPrinter` / `contoso-psa:print-to-ps`, resolved using original resources |
| PDC / PDR / assets / resources / authored code | Preserve upstream bytes; use the original UWP XAML/PRI build, not placeholder logos/resources or a rewritten task |
| Packaging flags | Single ARM64 MSIX, no x86/x64 bundle; unsigned; disable auto version increment and generated test installer; redirect upstream hard-coded `D:\PSASample\` output |
| Build baseline | SDK `10.0.26100.0`, VS2022/v143, Release ARM64 with upstream .NET Native enabled |
| Generated header | Upstream MIDL regenerates its checked-in, automatically generated `XpsUtil.h` from unchanged `XpsUtil.idl`; before/after hashes are reported separately, not called an authored-source adaptation |
| Generated manifest | Normal UWP build resolves the executable token, generates ARM64 identity metadata, EN-US/DE-DE resources, framework dependencies and .NET Native activatable-class registrations. The project's existing minimum 19041 / selected target 26100 override source manifest placeholder `10.0.0.0` values; no hand-edited minimum or static-registration OS requirement is inferred. |

These are **identity-adapted Microsoft sample sources**, not an unmodified
Microsoft-signed distribution. A locally trusted development signature proves
neither Microsoft's original distribution nor production signing works.

The sample also includes a **dynamic `VirtualPrinterManager` management page**.
It remains upstream code, but this experiment must not launch the application,
open that page, or invoke its APIs. Registration under test is solely the two
static manifest declarations at normal package installation. Do not use the UI
as an alternate way to create queues. If compiling that page needs APIs outside
SDK 26100, that is a build prerequisite/blocker to report, not permission to
raise the SDK/OS requirement or replace the sample logic.

Its print workflow includes watermarking and the inbox `XpsToPdf` converter.
That is **included sample behavior, not exercised by this registration test**,
not our selected renderer, and not a production fallback. The sample's network
capabilities are preserved for fidelity, not asserted to be static-registration
requirements. `DisplayName` resources and a larger PDC are comparison variables,
not explanations for the prototype's absent queues.

## Build and evidence boundaries

The workflow is
[`printer-reference-control.yml`](../../.github/workflows/printer-reference-control.yml).
The first build method is the actual upstream UWP solution, VS2022 on a hosted
x64 Windows runner, cross-compiling ARM64. Managed `Tasks` / C# intermediates
may be IL-only AnyCPU or ARM64-targeted; native XpsUtil, .NET Native output and
framework dependencies must match the ARM64 package. An ARM64 manifest alone
is not sufficient architecture evidence.

The user's existing C++ environment is not asked to install the UWP managed /
.NET Native toolchain. When a build-verified artifact is available, local work
is sign-only using the installed SDK 26100 x64 SignTool (emulated on ARM64),
followed by explicit review, trust and normal user installation.

For a reproducible **developer/CI build**, not the end-user handoff:

```powershell
pwsh -File printer/reference-control/Build-Control.ps1 -OutputDirectory C:\psa-rc
```

Requires VS2022 UWP C#/.NET Native and C++ ARM64 components, SDK 26100,
PowerShell 7, NuGet and tar. Missing dependencies fail explicitly. Use a fresh
output directory rather than deleting another build's evidence.
The output root must be short (at most 40 characters) for legacy tools; CI uses
the runner's temporary drive rather than the deeply nested repository path.
NuGet versions in the upstream projects are retained: `Microsoft.NETCore.UniversalWindowsPlatform`
6.2.10, `Microsoft.UI.Xaml` 2.4.0, `Microsoft.Windows.CppWinRT` 2.0.250303.1,
and `Microsoft.Windows.ImplementationLibrary` 1.0.250325.1. A source pin is not a
claim that every transitive NuGet binary or compiler output is byte-reproducible.

Build/packaging success, hosted OS architecture, deployment success and actual
queue enumeration must be reported separately. CI is not the user's machine.
No reference package, installation result, queue success or native execution
is implied merely by adding this recipe.

## Local sign-only and side-by-side installation

Use the **unsigned ARM64 control artifact**, not a CI-signed test copy. Extract
it to a new lab directory. It includes the single main MSIX, four unmodified
ARM64 Microsoft framework APPXs, source/build evidence, public notices, and the
standalone `Sign-Control.ps1`; no PFX or private key is distributed.
The eventual artifact/run URL and unsigned-package hash belong in the results
section below, not a hash taken from a different build.

Run this in PowerShell 7 from that extracted directory, substituting the exact
published unsigned **MSIX file** SHA256 (not the artifact ZIP digest):

```powershell
$ErrorActionPreference = 'Stop'
pwsh -NoProfile -File .\Sign-Control.ps1 `
  -PackagePath .\PsaReferenceControl.arm64.msix `
  -ExpectedSha256 '<published unsigned MSIX SHA256>' `
  -OutputDirectory .\signed
if ($LASTEXITCODE -ne 0) { throw 'Signing failed; stop here.' }
```

The script validates the hash, distinct identity, publisher, version and ARM64
native payload before signing a copy. It creates a **local** code-signing key
in CurrentUser\My and exports only a public `.cer` and signing receipt. It never
imports trust or installs the package, and refuses existing output directories.
SDK 26100 x64 SignTool is a host tool, emulated on ARM64; this does not change
the package target.

Review the one exported public certificate and receipt. Its subject must be
`CN=PsaReferenceControlDev`; compare its thumbprint to the local receipt.
Do not trust an unrelated certificate from the prototype or another signing run.
In a separate **administrator** PowerShell window on this disposable lab
machine only, explicitly import **that exact reviewed public certificate**:

```powershell
# Use the actual lab directory containing this signing run's output.
Get-PfxCertificate -FilePath 'C:\lab\reference-control\signed\PsaReferenceControlDev.cer' |
  Format-List Subject, Thumbprint, NotAfter
Import-Certificate -FilePath 'C:\lab\reference-control\signed\PsaReferenceControlDev.cer' `
  -CertStoreLocation Cert:\LocalMachine\TrustedPeople
```

Return to the normal user's PowerShell window. Leave the existing prototype
installed. **Do not launch the sample's application/UI or submit a job.**
Install once normally with its supplied framework dependencies, then enumerate.
The selection below omits frameworks already satisfied by an installed,
healthy ARM64/neutral version; it does not try to downgrade a newer framework:

```powershell
$ErrorActionPreference = 'Stop'
Get-Printer
$info = & {
  . .\Sign-Control.ps1
  Assert-ControlPackage -Path (Resolve-Path .\signed\PsaReferenceControl.arm64.msix).Path -SignatureState Signed
}
$frameworks = @(Get-Content .\architecture-evidence.json -Raw | ConvertFrom-Json |
  Where-Object name -ne 'Miyako.PsaReferenceControl')
if ($frameworks.Count -ne 4) { throw 'Expected four supplied ARM64 frameworks.' }
$dependencies = @(foreach ($framework in $frameworks) {
  $declaration = @($info.Dependencies | Where-Object Name -ceq $framework.name)
  if ($declaration.Count -ne 1) { throw 'Unexpected framework declaration.' }
  $installed = @(Get-AppxPackage -Name $framework.name | Where-Object {
    $_.Publisher -ceq $declaration[0].Publisher -and $_.IsFramework -and
    [version]$_.Version -ge [version]$declaration[0].MinVersion -and
    $_.Architecture.ToString() -iin @('arm64', 'neutral') -and $_.Status.ToString() -ieq 'Ok'
  })
  if ($installed.Count -eq 0) { (Resolve-Path ".\Dependencies\arm64\$($framework.file)").Path }
})
$install = @{ Path = (Resolve-Path .\signed\PsaReferenceControl.arm64.msix).Path }
if ($dependencies.Count -ne 0) { $install.DependencyPath = $dependencies }
Add-AppxPackage @install
Get-AppxPackage -Name Miyako.PsaReferenceControl |
  Select-Object Name, Version, Architecture, Status
Get-AppxPackage -Name Miyako.PsaFeasibility |
  Select-Object Name, Version, Architecture, Status
Get-Printer
```

Expected new names are **VirtualPdfPrinter** and **VirtualPostScriptPrinter**.
If either was present before installation, the comparison has a collision: stop,
do not rename/remove that existing queue to engineer a result. Package Status Ok
without queues remains an absent-queue result. If installation fails, stop and
record that gate; do not loop through trust-store changes or reinstalls.
The sign-only receipt records separate input/output hashes because local signing
changes the MSIX bytes. Keep the key local; never upload it or a PFX.
The development certificate lasts 30 days and is not timestamped; this is a
short-lived lab package, not a public release or permanent signing arrangement.

## Disposable hosted registration experiment

The separate `windows-11-arm` job consumes the **same unsigned artifact**.
It requires a native ARM64 Windows 11 client OS and an elevated, GitHub-hosted
disposable job. It signs a temporary copy, validates its signer against the
generated public certificate, verifies the original Microsoft framework
signatures, then temporarily trusts only that developer leaf certificate and
performs a normal `Add-AppxPackage` for the job user. It never launches the app.

The probe refuses an existing control identity or either original queue name.
It enumerates **all** printers before installation and up to six times at
10-second intervals afterward, publishing only counts, the two expected-queue
booleans and known inbox names. It reads Spooler state without starting,
stopping or interpreting a trigger-start service as broken. No policy, printer,
registry or service changes are used to engineer registration.

`outcome=present`, `some`, or `absent` is the actual observation.
**A green experiment job may contain `absent`: it means the bounded experiment
completed, not that registration passed.** Deployment/enumeration/preflight
errors fail the step and retain a sanitized stage/HRESULT. Cleanup removes only
exact packages/certificate resources created by that attempt, including partial
deployment where discoverable; cleanup errors also fail the job. The workflow
then removes that signing key. Neither a private key/PFX nor the ephemeral
CI-signed package is uploaded.

The JSON records native OS and PowerShell process architecture separately.
An elevated hosted account, different OS patch/image, installed framework set,
and runner session state are **not** the user's Enterprise desktop environment.
Installation on an ARM64 OS is not execution of the sample print handler:
there are no print jobs, no Save As/UI, no renderer measurements or service/4D
activation assertions.

## Interpretation matrix

Compare **unfiltered** `Get-Printer` after one normal reference installation
with the already-established prototype-absent result. Do not submit a job yet.

| Reference queues | Prototype queues | What this supports / does not establish |
|---|---|---|
| Both present | Absent | Static registration works for this identity-adapted reference on that host. Investigate narrowly isolated package/PDC/PRI differences; do not bulk-copy the sample into the product. Does not prove print/Save As/service/4D behavior. |
| Both absent | Absent | No successful control. Both may share an unmet prerequisite or deployment condition; **does not alone prove a Windows bug or prohibition**, or identify a root cause. |
| Only one present | Absent | Endpoint-specific difference worth isolating. Does not establish why the other endpoint or prototype failed. |
| Cannot build/install/enumerate | Absent | Comparison blocked at that named gate; not a negative registration result. Do not use dynamic registration, manual Add-Printer, trust loops or policy/service changes to turn it into a pass. |

Static DEH registration is documented in the
[Microsoft manifest specification](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/msix-manifest-specification-print-support-virtual-printer)
and [design guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/print-support-app-v4-design-guide).
The [schema](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-printsupport2-printsupportvirtualprinter)
does not make the later SourceContent workflow proof of installation behavior.
[microsoft/print-oem-samples#12](https://github.com/microsoft/print-oem-samples/issues/12)
is corroborating non-maintainer reporting, not an authoritative OS minimum or
a demonstrated remedy for this host.
