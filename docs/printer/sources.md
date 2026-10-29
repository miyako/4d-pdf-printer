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
10. [UWP interprocess communication](https://learn.microsoft.com/en-us/windows/uwp/communication/interprocess-communication)
    explicitly supports a packaged application connecting to a Win32 RPC endpoint
    with appropriate ACLs. The endpoint can be ACLed to the package SID derived
    using `DeriveAppContainerSidFromAppContainerName`, without custom-capability
    management. Microsoft's
    [CustomCapability sample](https://github.com/microsoft/Windows-universal-samples/tree/main/Samples/CustomCapability)
    includes an `ncalrpc` client and an actual Windows-service RPC server (the
    sample defaults to LocalSystem). This supports investigating PSA-to-service
    RPC once PSA activates; it does not demonstrate named-user PSA activation
    before login, after sign-out, or this renderer integration.
    **Separate the two authorization models:** the general IPC guide explicitly
    offers package-SID ACLs as an alternative to custom-capability management.
    The CustomCapability sample illustrates a custom-capability path, whose
    capability declarations, SCCD (signed custom capability descriptor),
    provisioning/authorization and signing instructions must be assessed
    separately. Sample existence does not grant a restricted/custom capability
    approval or make that deployment path available to our independently
    distributed package. No such approval, SCCD, provisioning or custom-capability
    deployment was performed. Our candidate uses the general package-SID ACL
    guidance, not the sample's capability identity or assumed entitlements;
    independent package identity/signing/trust and service deployment remain
    required validation gates.
11. [FullTrustProcessLauncher](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher?view=winrt-26100)
    activates a declared Win32 component in the **same package** and requires
    `runFullTrust`. It is not an arbitrary external `Process.Start` contract.
    The reviewed documentation neither guarantees PSA-task/no-login use nor
    states that background use is categorically forbidden. The WinAppSDK PSA
    sample declares `runFullTrust` and an `IBackgroundTask` DLL, but its PDF path
    requests UI and uses the inbox converter; it is not an external-worker
    handoff example. No claim is made that desktop/full-trust PSA is unsupported.
12. [BackgroundTaskCancellationReason](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.background.backgroundtaskcancellationreason?view=winrt-26100)
    defines `LoggingOff` as cancellation because the user logged off. Existing
    tasks must not be assumed to survive sign-out; this does not prove that a
    later print cannot activate another task.
    [BackgroundTaskDeferral](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.background.backgroundtaskdeferral?view=winrt-26100)
    says most tasks time out regardless of pending deferrals. The exact PSA v4
    trigger budget is not established by generic background-task quotas.
13. The same [IPC guide](https://learn.microsoft.com/en-us/windows/uwp/communication/interprocess-communication)
    documents named-pipe package/security constraints and `\\.\pipe\LOCAL\`
    naming, not an unrestricted cross-session pipe recipe.
    [App-service guidance](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/how-to-create-and-consume-an-app-service)
    describes background-caller time limits (normally 30 seconds plus a one-time
    5-second deferral extension). Those are app-service rules, not a proven PSA
    virtual-printer timeout, and neither mechanism closes the no-login gate.
14. [MSIX package behavior](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes)
    documents per-user installation. Generic package scope alone does not prove
    a queue's visibility or activation for a session-0 service.
    [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
    uses the caller process's security context, not a thread impersonation token.
    A permitted service coordinator can therefore own ordinary Win32 worker
    creation; the UWP task does not need to spawn the executable itself.
15. [Visual Studio on Arm](https://learn.microsoft.com/en-us/visualstudio/install/visual-studio-on-arm-devices?view=vs-2022)
    documents native ARM64 development starting with VS 2022 17.4 and separate
    compiler-host/target combinations. CMake's
    [VS 2022 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html)
    accepts explicit `-A ARM64` independently of host architecture.
    [Windows 11 x64 emulation](https://learn.microsoft.com/en-us/windows/arm/apps-on-arm-x86-emulation)
    permits x64 user-mode tools on ARM64; this build deliberately uses x64 SDK
    MakeAppx/SignTool, not an ARM64 executable on an x64 build host.
16. [Package Identity](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-f-identity)
    defines code architecture and quad version fields; [Add-AppxPackage](https://learn.microsoft.com/en-us/powershell/module/appx/add-appxpackage)
    documents signed same-family updates. Neither source guarantees this
    prototype's cross-architecture update or print deployment-handler result.
    Higher development versions 0.1.1.0 (architecture control) and 0.1.2.0
    (resource packaging correction) allow ordinary update attempts, not a promise
    that removal is unnecessary on every host. The separately recorded
    [runtime evidence](validation.md) must establish the actual update/queue
    outcome; schema-valid literal DisplayName is not reclassified as prohibited.
17. [GitHub-hosted runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners#standard-github-hosted-runners-for-public-repositories)
    lists native ARM64 `windows-11-arm` for public repositories. Its
    [image inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows11-Arm64-Readme.md)
    lists tools/components, but images change; record actual job OS/tool/test
    results rather than equating a label with executed native tests or PSA
    runtime support.
    The label has migrated to the
    [VS 2026 ARM64 image](https://github.com/actions/runner-images/blob/main/images/windows/Windows11-VS2026-Arm64-Readme.md);
    its versioned v143 ARM64 component is separate from the default toolset.
    The [VS 2026 CMake generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)
    defaults to v145; this build explicitly selects v143 with `-T`.
18. [Service user accounts](https://learn.microsoft.com/en-us/windows/win32/services/service-user-accounts)
    explicitly says SCM **automatically loads the user profile**, logs on the
    configured account and attaches its access token to the service process.
    A service account matching the signed-in desktop account does not make
    those logons or Terminal Services sessions identical.
    [SECURITY_LOGON_SESSION_DATA](https://learn.microsoft.com/en-us/windows/win32/api/ntsecapi/ns-ntsecapi-security_logon_session_data)
    separates the authentication LUID, logon type and Terminal Services session.
    [LsaGetLogonSessionData](https://learn.microsoft.com/en-us/windows/win32/api/ntsecapi/nf-ntsecapi-lsagetlogonsessiondata)
    permits the session owner to query it; diagnostics retain only status/type,
    not private user/domain/SID/LUID/profile fields. No `LoadUserProfile` fix is
    justified by the observed generic StartDoc error.
19. [Per-user services](https://learn.microsoft.com/en-us/windows/application-management/per-user-services-in-windows)
    are created when a user signs in and stopped/deleted at sign-out. The table
    lists **PrintWorkflowUserSvc**, Manual, supporting Print Workflow apps.
    Instance names have a user-context LUID suffix. This is a relevant platform
    boundary, not a documented complete PSA v4 internal activation path,
    exact AuthenticationId-to-suffix algorithm, or proof that this host's 3003
    is caused by that service. No startup-type/registry/policy/service changes
    are prescribed.
20. [StartDocW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-startdocw)
    documents positive job ID on success, nonpositive return on failure and
    synchronous/blocking behavior. It does **not** document a
    `CoInitialize[Ex]` prerequisite or guarantee a uniquely causal LastError.
    [System errors 1700-3999](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1700-3999-)
    define 3003 / ERROR_SPL_NO_STARTDOC as "A StartDocPrinter call was not issued."
    This does not instruct a GDI caller to insert `StartDocPrinter` before
    `StartDocW`. It does not prove a task factory failure or universal service
    prohibition. Capture raw return/error before any diagnostics or cleanup.
    Managed `System.Drawing.Printing` / `System.Printing` service warnings are
    not a diagnosis of this native GDI submitter.
21. [FindPackagesByPackageFamily](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-findpackagesbypackagefamily)
    queries packages for the **current user** and returns numeric Win32 codes.
    [PackageFamilyNameFromId](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-packagefamilynamefromid)
    derives the family from the authored name/publisher without another user's
    environment/registration. A positive HEAD count is not activation readiness.
    [RegOpenCurrentUser](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopencurrentuser)
    can return the default key if the profile is not loaded; therefore the
    diagnostic checks the own-token SID's exact mounted HKEY_USERS key instead
    and retains the numeric result, not a false "profile loaded" inference.
22. The pinned Microsoft
    [native WinAppSDK virtual-printer task](https://github.com/microsoft/print-oem-samples/blob/652343e4980cc962fb099496125cf749aee51c71/PSASamples/WinAppSdk/C%2B%2B/BackgroundTasks/VirtualPrinterBgTask.cpp)
    takes a background deferral, extracts the virtual-printer trigger/session,
    subscribes to data, then starts the session, matching our public-contract
    ordering. Its cloud branch does not request a target file; PDF code requests
    UI. This source is not a named-user SCM/session-0 submission sample or
    proof of no-login support. Our desktop positive control already exercises
    the product factory/Run/data path. The current native probe calls only GDI;
    it does not activate this WinRT task itself.

**Current scope decision:** desktop-first development is explicitly authorized;
Windows-service printing is deferred. The new
[separate desktop package](../desktop/README.md) measures same-package declared
full-trust activation, authenticated local RPC and exact-job title correlation
before real worker/publication integration. It does not alter the diagnostic
package or establish service support. In particular, documented
`LaunchFullTrustProcessForCurrentAppWithArgumentsAsync` is a genuine Windows 11 /
FullTrustAppContract v2 API accepting command-line arguments; the older
`LaunchFullTrustProcessForCurrentAppAsync(string)` accepts a ParameterGroup ID.
The desktop code uses only a fixed declared coordinator and fixed `--serve`.

**Deferred service decision:** retain the PSA architecture as a candidate, but do not integrate a
service worker/4D Server plugin until the named-user session-0 activation and permitted
coordinator transport gates are closed with authoritative support plus observed
pre-login/sign-out runs. No legacy driver or logged-in helper fallback is authorized.
The best source-supported transport candidate is **activated PSA task -> ACL-
protected local RPC -> named-user coordinator service -> fresh external worker**.
The end-to-end activation/deployment gate is unresolved, not proven impossible.
The [actual session-0 failure](validation.md#named-user-session-0-submission-gate)
now puts implementation on **HOLD**. Primary sources reviewed here do not close
the gap between current-user package registration and a supported background
activation from a type-5/session-0 print caller before login/after sign-out.
Do not label the architecture supported or categorically unsupported without
that platform evidence.
