# 4d-pdf-printer

Windows PDF virtual-printer feasibility and isolated libgxps/Cairo conversion
for 4D. **Not a finished PDF printer, installer or plugin.** No PDF size or
reliability improvement has been measured.

The [printer prototype](docs/printer/README.md) targets Windows 11 24H2+
(build 26100), using PSA v4 virtual-printer APIs, not a legacy v4 printer driver.
It includes a diagnostic OXPS-capture MSIX, separate Save As / no-file-UI endpoints,
a native GDI submission probe with an actual Windows-service entry point, and
Windows build automation. It does not render PDFs or launch an external helper.
The build explicitly selects x64 (default) or ARM64 with isolated, PE-checked
packages/probes; ARM64 is a compatibility/control test, not a proven queue fix.

**Desktop-first; Windows-service printing is deferred.** The
[separate desktop development package](docs/desktop/README.md) is implementing
the early actual PSA/full-trust/RPC/exact-job handoff gate. It currently produces
**no PDFs** and deliberately fails workflow completion; it is not a working
pipeline or 4D integration.

Diagnostic queue registration is now fixed through validated PRI resources:
both queues and actual native ARM64 desktop OXPS capture were observed. The
Save As diagnostic writes OXPS; the no-file-UI endpoint receives no Windows-bound
destination. Native named-user session-0 PSA submission still fails at StartDocW
(3003), despite visible package/profile and successful inbox XPS submission.
This is retained evidence, not solved by a logged-in desktop coordinator.
See the [runtime runbook](docs/printer/runbook.md),
[source-backed findings](docs/printer/sources.md), and
[conditional worker interface](docs/printer/handoff.md).

The independent renderer is developed separately under `renderer/` and
`docs/renderer/`. No AGPL component or product license is selected by the printer
prototype. Use synthetic data only in this public repository.
