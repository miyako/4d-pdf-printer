# 4d-pdf-printer

Windows PDF virtual-printer feasibility and isolated libgxps/Cairo conversion
for 4D. **Not a finished PDF printer, installer or plugin.** No PDF size or
reliability improvement has been measured.

The [printer prototype](docs/printer/README.md) targets Windows 11 24H2+
(build 26100), using PSA v4 virtual-printer APIs, not a legacy v4 printer driver.
It includes a diagnostic OXPS-capture MSIX, separate Save As / no-file-UI endpoints,
a native GDI submission probe with an actual Windows-service entry point, and
Windows build automation. It does not render PDFs or launch an external helper.

**The headless service gate is open:** PSA activation and an allowed external
coordinator handoff must be demonstrated for a named-user Windows service before
desktop login and after sign-out. A hosted Windows build is not that evidence.
The first user-run Windows 11 installation reported package success but created
neither diagnostic queue; **queue registration is an earlier unresolved gate**.
No printing/service result is implied.
See the [runtime runbook](docs/printer/runbook.md),
[source-backed findings](docs/printer/sources.md), and
[conditional worker interface](docs/printer/handoff.md).

The independent renderer is developed separately under `renderer/` and
`docs/renderer/`. No AGPL component or product license is selected by the printer
prototype. Use synthetic data only in this public repository.
