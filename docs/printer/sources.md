# Source-backed platform findings

Reviewed 2026-10-28. Documentation can change; compile against the pinned SDK and
collect runtime evidence on the actual 26100+ lab build.

1. [PSA v4 design guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/print-support-app-v4-design-guide)
   describes a UWP software endpoint, `PrintWorkflowVirtualPrinterTriggerDetails`,
   session handlers registered before `Start()`, task deferral, `CompleteJob()`,
   and OS-provided target file. The inbox XpsToPdf sample only demonstrates
   integration. It proves neither independent rendering benefits nor
   no-login/service-session activation.
2. [Virtual printer manifest specification](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/msix-manifest-specification-print-support-virtual-printer)
   requires a PDC file and supports `application/oxps` (default) or
   `application/postscript`, multiple queues and distinct `PrinterUri`s.
   `OutputFileTypes` makes a file printer with OS Save As. Omitting it is documented
   for cloud/application endpoints, **not** arbitrary silent filesystem output.
3. [PrintWorkflowVirtualPrinterSession](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.workflow.printworkflowvirtualprintersession?view=winrt-26100)
   is introduced in Windows 11 24H2 / 10.0.26100 / UniversalApiContract v19.
   [Event args](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.workflow.printworkflowvirtualprinterdataavailableeventargs?view=winrt-26100)
   expose `SourceContent`, `Configuration`, `UILauncher`, `GetJobPrintTicket`,
   `GetTargetFileAsync`, `GetPdlConverter`, and `CompleteJob`. There is no
   documented event-args deferral; use the background task deferral.
4. [Workflow configuration](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.workflow.printworkflowconfiguration?view=winrt-26100)
   exposes `SessionId` (a unique **workflow** string), title and source app name.
   This is not a documented GDI spooler job-ID mapping. The prototype hashes these
   identifiers. Neither title nor session hash authenticates a job's owner.
5. [DOCINFOW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-docinfow)
   defines `lpszOutput` as an output filename. A null pointer selects the device.
   This does not document how a PSA no-file endpoint receives it; the probe tests
   that question, rather than assuming it.
6. [Out-of-process background tasks](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-and-register-a-background-task)
   require an `IBackgroundTask` implementation and a deferral for asynchronous
   lifetime. [In-process server manifest](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-f-inprocessserver)
   defines DLL/class registration. The code authors only those public interfaces,
   using the SDK C++/WinRT headers and a manually exported factory.
7. [Current printSupport2 extension schema](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-printsupport2-extension)
   lists trust/runtime/scope attributes, including `mediumIL`,
   `packagedClassicApp` and `desktop7:Scope`. These generic manifest attributes do
   **not** alone establish a supported service-session PSA handoff on 26100.
   This first package deliberately does not enable them or claim they are impossible.
8. [Microsoft print-oem-samples](https://github.com/microsoft/print-oem-samples/tree/652343e4980cc962fb099496125cf749aee51c71/PSASamples)
   contains UWP and WinAppSDK variants and example PDC XML. Example presence is
   not a service-session support contract.
9. [4D Server as a service](https://developer.4d.com/docs/server/service)
   is the user's service-account setup reference, not evidence that PSA activation
   works from that account or that 4D Server implies Windows Server OS support.

**Decision:** retain the PSA architecture as a candidate, but do not integrate a
production worker/4D plugin until the named-user session-0 activation and permitted
coordinator transport gates are closed with authoritative support plus observed
pre-login/sign-out runs. No legacy driver or logged-in helper fallback is authorized.
