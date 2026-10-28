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
Portable source checks and **29 synthetic build-support cases** passed on macOS:
all four host/target plans, default/invalid selectors and SDK, generated manifests,
PE-header/machine validation, staged/archived mixed-target and missing binaries,
and stale package version rejection. Synthetic PE/ZIP tests are not executable
Windows tests or MakeAppx schema validation.
The first native ARM64 CI attempt found that `windows-11-arm` now resolves to
the VS 2026 image, not the older VS 2022 image inventory. Prerequisite checking
failed before compilation; explicit VS 2026 selection with its installed
v143 component addresses that build-host distinction, not queue registration.

Windows CI now requests native x64 (`windows-2022`), native ARM64
(`windows-11-arm`) and ARM64 cross-compilation on x64. The
[GitHub runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners#standard-github-hosted-runners-for-public-repositories)
lists `windows-11-arm` as ARM64 for public repositories; workflow execution also
asserts the native OS architecture.

### Actual Windows results for 50be556

Both push and PR checks passed for build-source commit
`50be556d9beec962900a1f65e93e3113acb415bf`:
[PR run 40657060798](https://github.com/miyako/4d-pdf-printer/actions/runs/40657060798)
and [push run 40657054960](https://github.com/miyako/4d-pdf-printer/actions/runs/40657054960).

| Target / native host | Actual result | Download from PR run |
|---|---|---|
| x64 / x64 | Compile, 29 portable checks, 2/2 CTests, PE/manifest checks and normal MakeAppx passed. | [unsigned x64](https://github.com/miyako/4d-pdf-printer/actions/runs/40657060798/artifacts/12801399578) |
| ARM64 / ARM64 | Same checks passed, **including native execution of both CTests**. | [unsigned ARM64](https://github.com/miyako/4d-pdf-printer/actions/runs/40657060798/artifacts/12801289732) |
| ARM64 / x64 (cross-compile) | Compile, portable checks, PE/manifest checks and MakeAppx passed; native CTests explicitly **not run**. | [unsigned ARM64 cross-build](https://github.com/miyako/4d-pdf-printer/actions/runs/40657060798/artifacts/12801284914) |

The genuine ARM64 runner reported Windows 11 Enterprise/build 26200,
image `windows-11-vs2026-arm64` / `20261022.194.1`, PowerShell process Arm64,
MSVC `19.44.35229.0` with explicit v143 and SDK `10.0.26100.0`.
MakeAppx's x64 host tool ran under emulation; the generated shell, task DLL,
submission probe and factory test were verified ARM64. The x64 native/cross
jobs use Windows Server only as a build host, not as a claimed runtime target.
The native CTests are the 20-case argument/timing self-test and direct DLL
factory test; neither invokes the PSA task's `Run`.

All three downloaded artifacts were rechecked on macOS: staged and actual MSIX
architectures/versions matched the target; the submission probe PE matched;
independent `file` inspection identified the native ARM64 shell/DLL/probe as
Aarch64. The ARM64 artifact's `build-evidence.json` reports native/passed; the
cross-build reports not-run-cross-compile/not-run. Both manifests use 0.1.1.0.
Artifact content is relative to the selected output base (`PsaDiagnostic.msix`,
`package/`, `build/Release/PrintProbe.exe`, `build/Release/ActivationProbe.exe`,
`build-evidence.json`).

Downloaded **native ARM64 job** SHA256 (unsigned; local rebuilding/signing can
change hashes):

```text
PsaDiagnostic.msix: 8eb42a89542bc4d51819f6625148bd26064f94fb6950bb8c3f982b64bc74bd60
PrintProbe.exe:     8a83df3a70a2070e98f7082702093ffcf1a94d764eab3448eba649afa930007c
```

The subsequent documentation-only revision records this historical build source,
not a newer binary fix. CI artifacts have retention limits.
The jobs did not sign/install the package, create queues, submit jobs or run
service/4D experiments.

### Completed user-host ARM64 update and reference comparison

The user subsequently performed a normal same-family update from x64 0.1.0.0
to **ARM64 0.1.1.0**, with Status Ok. Neither diagnostic queue appeared in
unfiltered enumeration. This update succeeded but did not solve registration;
do not repeat it or infer a generic native-architecture restriction.

On the **same user host**, the separately identified Microsoft reference control
**1.1.14.0 ARM64, Status Ok** has both **VirtualPdfPrinter** and
**VirtualPostScriptPrinter**, while the product remains installed without either
diagnostic queue. The reference queues were already present at the beginning of
the supplied transcript: first-install timing is unobserved. The reference UI
and its dynamic management page were not invoked. Static registration capability
on this host is established; printing, task `Run`, and service support are not.

### Generated-artifact comparison and bounded discriminator

The comparison uses actual packaged manifests, not just authored XML:
product [run 40657060798](https://github.com/miyako/4d-pdf-printer/actions/runs/40657060798),
artifact `printer-feasibility-unsigned-arm64`, and reference
[run 40668170711](https://github.com/miyako/4d-pdf-printer/actions/runs/40668170711),
artifact `reference-control-build-evidence` / `resources/packaged-AppxManifest.xml`.

| Registration surface | Actual product / control difference and interpretation |
|---|---|
| Static extension | Same `/printsupport/windows10/2` namespace, workflow category, application multi-instance support and ignorable print namespace. Both declare two queues. |
| Application and activation | Product `Diagnostic` / `PsaDiagnostic.exe` / `PsaDiagnostic.App`; control `App` / `PrintSupportApp.exe` / `PrintSupportApp.App`. Neither specifies explicit trust/runtime overrides. Both task entry points have package-level `windows.activatableClass.inProcessServer` registrations with threading `both`; native `PsaTask.dll` versus generated .NET Native `PrintSupportApp.dll`. Structural equivalence is not a runtime activation test. No omitted mandatory background contract was established. |
| Device family / frameworks | Product Desktop, minimum 26100, no runtime frameworks; control Universal, minimum 19041 / target 26100, four managed/native frameworks. These are differences, not demonstrated registration requirements. |
| Names / URIs / formats | Literal diagnostic names versus PRI-backed `ms-resource` names; distinct custom-scheme opaque URIs in both. Product two OXPS endpoints (file and no-file) versus control OXPS/PDF passthrough and PostScript file endpoints. Literal DisplayName, absent SupportedFormats and absent OutputFileTypes on a non-file endpoint are permitted by the public contract. |
| PDC / PDR | Product reduced PDC versus control full original PDC. Installed product bytes match the artifact; semantic acceptance remains unproved. Control packages PDR but neither queue references it. PDR is optional, not an inferred fix. |
| Other contracts / capabilities | Control retains other print extensions and network/account capabilities. No source establishes these as prerequisites for installing our local queues. They are not copied into the product. |

The hosted-only
[`Test-RegistrationAB.ps1`](../../printer/diagnostics/Test-RegistrationAB.ps1)
first installs the **unchanged product** on one disposable native ARM64 Windows
11 runner, using local Windows PowerShell 5.1. If any product queue appears, it
stops: a positive hosted baseline does not reproduce the user-host failure and
cannot justify attributing a fix to a variant.

Only an absent product baseline permits an identity-isolated baseline, followed
by a **PDC-only** variant. The latter uses exactly the reference PDC bytes at
the existing `Config\DiagnosticPdc.xml` path, keeping native binaries, task
registration, declarations, names, URIs, resources, capabilities and formats
unchanged. Both isolated variants use the same diagnostic identity/publisher;
the second has a higher version solely to avoid stale same-version byte
conflicts. Every install is a fresh Add after exact-owned package removal and
verification that the identity and both queue names are absent. An isolated
baseline that registers also stops the comparison, avoiding identity causation
confusion. Cleanup failure stops the comparison and fails the job.

The report records actual installed identity/version/architecture/status,
package/PDC hashes, full **disposable-host** queue-name inventories, bounded
10-second observations (maximum 60 seconds), cleanup, and a sanitized failure
type/HRESULT. Installed manifests are retained separately. No app UI, dynamic
registration, `Run`, job, service, policy, registry or driver changes occur.
This is a diagnostic experiment, not a product PDC replacement or a user-host
reinstall procedure.

**Actual first discriminator:** [run 40673452288](https://github.com/miyako/4d-pdf-printer/actions/runs/40673452288)
passed, including exact-owned package/queue cleanup and ephemeral trust/key
removal. [Persistent summary](evidence/registration-ab-40673452288.json);
[full report and installed manifests](https://github.com/miyako/4d-pdf-printer/actions/runs/40673452288/artifacts/12809034068).
Native ARM64 build 26200, image `20261022.194.1`, local Windows PowerShell
`5.1.26100.9711` reproduced the product failure:

| Fresh Add on the same hosted job | Installed identity/version/status | Queues after six 10-second observations |
|---|---|---|
| Unchanged product | `Miyako.PsaFeasibility` / 0.1.1.0 / Arm64 / Ok | Both absent |
| Identity-isolated baseline | `Miyako.PsaRegistrationDiagnostic` / 0.1.1.0 / Arm64 / Ok | Both absent |
| Full reference PDC only | Same isolated family / 0.1.2.0 / Arm64 / Ok | Both absent |

Every before/after inventory was exactly `Microsoft Print to PDF`; each identity
and its queues were verified absent after removal before the next Add.
**The full reference PDC alone is not sufficient.** Do not ship it as a product
fix or claim that this proves the original PDC was semantically accepted.

The second bounded discriminator restores the original PDC and changes only the
queue-name representation to equivalent `ms-resource` values in a tiny generated
PRI, under isolated version 0.1.3.0. It does not import the sample app/resources
or change queue-name text, URIs, formats, task code, device family or capabilities.
MakePri's dump must contain both exact names before installation. Payload hashes
exclude only the intentional manifest/PDC/PRI variables and must otherwise match
across variants. This is a second candidate test, not a claim that literal
DisplayName is prohibited by the public schema.

**Actual positive discriminator:** [run 40674321931](https://github.com/miyako/4d-pdf-printer/actions/runs/40674321931)
passed. [Persistent summary](evidence/registration-ab-40674321931.json);
[full report, installed manifests and PRI dump](https://github.com/miyako/4d-pdf-printer/actions/runs/40674321931/artifacts/12810210959).
On the same native ARM64 job/image/build/PowerShell as above, the product,
isolated baseline and full-PDC variant again had no queues after 60 seconds.
The **resource-only** isolated package, version 0.1.3.0, installed Arm64 / Ok
and exposed **both exact diagnostic names at the first 10-second observation**.
Its PDC hash was the original product's
`e38b25327081ec310b21c5cb077b018ccc8c71090d7ccd09e42bcf6b9a6b4e2c`,
not the reference PDC. Native binary, asset and notice hashes were identical
across all four variants; actual installed manifests preserve the original
URIs, formats, task registration, device family and capabilities. The dumped
PRI maps the two keys to exactly the original display strings. Every owned
package/queue and ephemeral certificate/key was removed successfully.

This isolates the **queue-name resource packaging** as an observed compatibility
correction. It does not show which internal DEH call fails for literals, establish
a universal schema restriction, or implicate native task activation. Product
0.1.2.0 adopts only those equivalent resource references and a generated,
verified PRI; it does not replace PDC, add contracts/capabilities/frameworks, or
change native task/job code.

The current diagnostic workflow separately downloads the exact historical
0.1.1.0 native artifact (verifying its published unsigned hash), installs it,
retains it, and performs a **normal same-family 0.1.2.0 update without removal
between versions**. It checks actual installed version/architecture/status and
requires both queues, then removes only its owned test packages and certificate.
This is the user's intended update path, not another isolated fresh Add.
Historical artifact expiry fails explicitly; there is no silent replacement.

**Actual product update:** [run 40675303751](https://github.com/miyako/4d-pdf-printer/actions/runs/40675303751)
passed at source `eec490de0aad5cef784610929181ea91c15dbea6`.
[Persistent summary](evidence/product-update-40675303751.json);
[full report and both installed manifests](https://github.com/miyako/4d-pdf-printer/actions/runs/40675303751/artifacts/12810691197).
The historical 0.1.1.0 product installed Arm64 / Ok and remained without queues
for 60 seconds. With that package **still installed**, normal `Add-AppxPackage`
updated the same `Miyako.PsaFeasibility` family to **0.1.2.0 / Arm64 / Ok**.
Both original diagnostic queue names appeared at **10 seconds**. The report
confirms `updateWithoutRemoval=true`, unchanged original PDC hash, successful
exact-owned package/queue cleanup, and removal of ephemeral trust/key.
This validates the intended non-destructive update path on the hosted runner;
the user's own host still needs only the
[short normal update retest](runbook.md#resource-packaging-correction-update).

[Build run 40675303817](https://github.com/miyako/4d-pdf-printer/actions/runs/40675303817)
passed the same revision's native x64, native ARM64 and ARM64-on-x64 builds.
All ran the 36 portable build/resource cases and normal MakeAppx validation;
native targets ran both CTests, while cross-compiled native execution was
explicitly not run. These remain factory/parser tests, not background-task `Run`.

## Still not run / not proven

Product queue creation on the user host has not succeeded; reference queue
creation has. Isolated native product-payload queue registration has succeeded
with resource-backed names on hosted ARM64. Real
XPS/OXPS capture, Save As, native output-path mapping, named-service account
registration/activation, pre-login execution, post-sign-out execution, 4D printing,
external coordinator IPC and PDF rendering remain unobserved in this printer PR.
No PDF size or repeated-job reliability metric has been measured.

Use the [runtime runbook](runbook.md) to close those gaps. Build or direct COM
factory success is not a substitute for the required session-0 activation gate.
