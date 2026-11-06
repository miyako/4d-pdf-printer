# Generic Print to PDF: install and use

Generic Print to PDF adds two virtual printers to Windows:

| Printer | What it does |
|---|---|
| **Generic Print to PDF** | Asks where to save, like *Microsoft Print to PDF*. |
| **Generic Print to PDF (Silent)** | Saves without any dialog to a file name that a program registered beforehand with `generic-pdf`. Built for automation (for example 4D). |

PDFs are rendered on your PC. Nothing is uploaded, and no service or background process stays
running between print jobs.

## Requirements

- Windows 11 version 24H2 (build 26100) or later.
- An x64 or ARM64 PC (each gets a native build).

## Install

### From the Microsoft Store (recommended)

Open the *Generic Print to PDF* page in the Microsoft Store and choose **Get**. Windows installs the
app with its dependencies and adds both printers within a few seconds. Updates arrive automatically.

### Sideload build (testing, or before the Store listing is live)

1. Unzip the release folder. It contains the `.msixbundle` (or `.msix`), a `.cer` certificate,
   `Install.cmd` and helper scripts.
2. Double-click **`Install.cmd`**. It:
   - trusts the included certificate if Windows does not trust it yet (one UAC prompt);
   - downloads the Microsoft Visual C++ runtime framework only if it is missing;
   - installs or updates the package for your user account;
   - waits for both printers to appear and, if PowerShell 7 is installed, runs `VerifyInstall.ps1`.

Advanced: `powershell -ExecutionPolicy Bypass -File Install-Sideload.ps1 -Package <file> -Certificate <file.cer>`.

## Print with Save As

Print from any app and choose **Generic Print to PDF**. Windows asks for a file name and location.

## Print silently (automation)

The Silent printer never shows a dialog. It saves only jobs whose **document name** equals a
selector issued by `generic-pdf.exe`, which the package adds to your `PATH` (a Windows app execution
alias). Every command prints one JSON line.

1. `generic-pdf register <destination.pdf> [completion-ms] [registration-ms]`
   reserves one exact new file. The destination must be a `.pdf` under your user profile, its folder
   must exist, and the file must not exist yet. The reply holds `id` and `selector` (`4dpdf:<id>`).
   Defaults: the print must complete within 300000 ms of `submitted`, and must be submitted within
   600000 ms of `register`.
2. `generic-pdf submitted <id>` immediately before you start printing. This starts the completion
   budget; a registration is used once and cannot be resubmitted.
3. Print to **Generic Print to PDF (Silent)** with the document name set to the `selector`.
4. `generic-pdf wait <id>` blocks until the PDF has been published. Exit code 0 = PDF saved,
   1 = failed, 2 = timed out or unknown (a cancel is recorded). `generic-pdf status <id>` returns
   at once (exit 3 = still pending). `generic-pdf cancel <id>` withdraws a registration.

Silent jobs never overwrite an existing file. A job with any other document name fails without output.

Example (Windows PowerShell):

```powershell
$reg = generic-pdf register "$env:USERPROFILE\Documents\hello.pdf" | ConvertFrom-Json
Add-Type -AssemblyName System.Drawing
$doc = New-Object System.Drawing.Printing.PrintDocument
$doc.PrinterSettings.PrinterName = 'Generic Print to PDF (Silent)'
$doc.DocumentName = $reg.selector
$doc.add_PrintPage({ param($s, $e)
    $e.Graphics.DrawString('Hello', (New-Object System.Drawing.Font 'Segoe UI', 24), [System.Drawing.Brushes]::Black, 100, 100) })
generic-pdf submitted $reg.id | Out-Null
$doc.Print()
generic-pdf wait $reg.id
```

## Check an installation

With PowerShell 7: `pwsh -File VerifyInstall.ps1` (in the release folder, or `desktop-pdf\tools` in the
repository). It changes nothing and prints JSON; exit 0 means every check passed. It confirms both
queues exist, that Windows applied the app's printer capabilities (A4, portrait, 300 dpi, colour),
and that the bundled PDF renderer is present.

If a queue offers 600 dpi or dozens of paper sizes, Windows created it without the app's
capabilities. Uninstall and install again.

## Uninstall

*Settings > Apps > Installed apps > Generic Print to PDF > Uninstall*, or run `Uninstall.ps1`
(add `-RemoveCertificate` to also remove a sideload certificate; one UAC prompt). Windows removes
both printers and the app's job records. PDFs you saved are not touched.

## Third-party software

The PDF renderer bundled under `Runtime\` in the install folder uses open-source libraries
(cairo, libgxps, libarchive, GLib, FreeType, fontconfig and others). Their licences are in
`Runtime\notices\`.
