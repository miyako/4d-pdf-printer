# Releasing Generic Print to PDF (Microsoft Store and sideload)

Maintainer procedure. End users read [INSTALL.md](INSTALL.md).

The engineering pipeline (`build.ps1` Build / Package / SignCopy) is unchanged and remains the
only producer of tested binaries. `release.ps1` turns its unsigned package into a release package:

| Release change | Why |
|---|---|
| Renderer runtime under `Runtime\` | No `configure-runtime` step for users. `ConfiguredRuntime` uses `<InstalledPath>\Runtime` when no developer `runtime.json` override exists, with the same inventory, hash, receipt and architecture checks. |
| `Microsoft.VCLibs.140.00.UWPDesktop` dependency | The renderer DLLs are /MD and import `vcruntime140.dll` / `msvcp140.dll`. The Store installs this framework automatically. |
| `generic-pdf.exe` execution alias on **Client** | Stable command name on `PATH` instead of the versioned `WindowsApps` path. |
| Client `AppListEntry="none"` | The command-line client has no Start menu tile. |
| Identity, version, display names | From Partner Center (below). `resources.pri` is rebuilt so `ms-resource:` labels resolve under the new identity name. |
| Assets | Generated "PDF" placeholders, or `-AssetsDirectory` with `StoreLogo`, `Square44x44Logo`, `Square150x150Logo`, `SplashScreen` (`.png`, scale variants allowed). |

## 1. Release clearance (blocking for the Store)

- [ ] Renderer runtime release-cleared: LGPL and other notice obligations met, and
      `INTERNAL-TEST-DELIVERY.txt` no longer present in the runtime folder. `release.ps1` refuses an
      internal runtime unless `-AllowInternalTestRuntime` is given, and `Bundle` then refuses to
      write the `.msixupload`.
- [ ] Repository licence chosen (the repository has no `LICENSE` yet).
- [ ] Native runtime for each shipped architecture (`build-runtime-x64`, `build-runtime-arm64`),
      each with a passed `native-test-receipt.json`.
- [ ] Privacy policy URL (the app collects nothing; Partner Center still asks for one).

## 2. Partner Center

1. Register as an individual developer at <https://storedeveloper.microsoft.com> (free for
   individuals).
2. *Apps and games > New product > MSIX or PWA app*; reserve **Generic Print to PDF**.
3. *Product management > Product identity*: copy
   `Package/Identity/Name` → `-IdentityName`, `Package/Identity/Publisher` → `-Publisher`
   (`CN=<GUID>`), `Package/Properties/PublisherDisplayName` → `-PublisherDisplayName`.

## 3. Build

Per architecture, on a native machine of that architecture, from a clean checkout of the release
commit (`release.ps1` requires the checkout's `Resources.resw` to match the engineering package's
`source_sha`). These are the `build.ps1` invocations used for the 0.1.0.8 device gates:

```powershell
$src = 'C:\src\4d-pdf-printer\desktop-pdf'
$out = 'C:\rel\0.2.0.0'
$common = @{ Architecture = 'ARM64'; VisualStudioVersion = '2022'; ValidationProfile = 'isolated-journal-v3'; CMakePath = $cmake }
& "$src\build.ps1" -Stage Build   @common -OutputRoot "$out\arm64-build" -PythonPath $python
& "$src\build.ps1" -Stage Package @common -OutputRoot "$out\arm64-package" -BuildDirectory "$out\arm64-build\build"
& "$src\release.ps1" -Stage Package -OutputRoot "$out\arm64-release" `
    -EngineeringPackage "$out\arm64-package\DesktopPdfIntegration.msix" `
    -RuntimeRoot 'C:\src\4d-pdf-renderer\renderer\build-runtime-arm64' `
    -IdentityName '<Name>' -Publisher 'CN=<GUID>' -PublisherDisplayName '<display name>' -Version 0.2.0.0
```

Repeat for x64, then bundle both:

```powershell
& "$src\release.ps1" -Stage Bundle -OutputRoot "$out\bundle" `
    -ReleasePackages "$out\arm64-release\<Name>_0.2.0.0_arm64.msix", "$out\x64-release\<Name>_0.2.0.0_x64.msix"
```

`Bundle` writes `<Name>_<Version>.msixbundle` and, when every runtime is release-cleared,
`<Name>_<Version>.msixupload`. Every stage writes `release-evidence.json` (inputs, hashes,
identity, runtime digests).

The version must be `major.minor.build.0`; the Store reserves the revision field. Each submission
needs a higher version than the last.

## 4. Test the exact Store identity before submitting

Sign a copy with a local code-signing certificate whose subject equals the Partner Center
publisher (`CN=<GUID>`), then install it with the sideload kit:

```powershell
& "$src\release.ps1" -Stage SignCopy -OutputRoot "$out\sideload" `
    -Package "$out\bundle\<Name>_0.2.0.0.msixbundle" -CertificateThumbprint <thumbprint>
& "$out\sideload\Install.cmd"
```

`SignCopy` also writes the public `.cer`, `Install.cmd`, `Install-Sideload.ps1`, `Uninstall.ps1`,
`VerifyInstall.ps1` and `INSTALL.md`, so the folder can be zipped as a sideload release.

Check: both queues with the new names, `VerifyInstall.ps1` exit 0 (bundled runtime, PDC applied),
a Save As print from Notepad, and a silent print through `generic-pdf`. Uninstall before installing
the Store build: a sideloaded and a Store package with the same identity conflict.

The development identity (`Miyako.DesktopPdfIntegration`, `CN=DesktopPdfPrototypeDev`) is the
default, so a release built without identity parameters updates an installed engineering package
for device testing.

## 5. Submission

- *Packages*: upload the `.msixupload`. Device family: Windows 10/11 Desktop. The manifest's
  minimum is build 26100 (Windows 11 24H2); earlier builds cannot install it.
- *Restricted capability* `runFullTrust`, justification:
  > The app is a Print Support App virtual printer. Its full-trust host process renders the print
  > job (OXPS) to PDF with a bundled native renderer in a separate, job-object-limited worker
  > process and writes the PDF to the location the user chose in the Windows Save As dialog, or
  > to a file the user registered in advance with the included command-line tool. Rendering and
  > file publication cannot run inside the app container.
- *Pricing*: free. *Age rating*: questionnaire (no user content, no network).
- *Store listing*:
  - Short description: "Print to PDF from any app, with a Save As printer and a silent printer
    for automation."
  - Description: list both printers, the `generic-pdf` command, "rendered locally, nothing
    uploaded", and third-party open-source notices (also installed under `Runtime\notices`).
  - Screenshots (at least one, 1366×768 or larger): the print dialog listing both printers; the
    Save As dialog; a resulting PDF.
- *Notes for certification*: "Open Notepad, type text, File > Print, choose *Generic Print to PDF*,
  Print, choose a file name. The PDF is written there. The *(Silent)* printer is for automation
  and only writes files registered with `generic-pdf register`; without a registration it
  completes the job without output by design."

## Known review risks

- Launching the **SaveAs** app from Start opens an empty window (the PSA entry point has no UI).
  Reviewers may treat that as broken. Options: give it a short "how to use" page, or hide it with
  `AppListEntry="none"` (Store may then require another discoverable entry point).
- Print Support App virtual printers published by individuals are uncommon in the Store; if
  certification rejects the extension, the fallback is a signed sideload release (OV certificate
  or SignPath Foundation) using the same kit.
- The silent queue renders nothing for unregistered jobs, which a tester may report as a defect;
  the certification note above explains it.
