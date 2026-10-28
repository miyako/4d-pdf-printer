# Desktop PDF development

**Current code is the early handoff gate, not a PDF printer.** It deliberately
completes every workflow as failed/canceled and produces no output file. Do not
use either prototype queue for real documents.

The approved desktop-first direction is a thin AppContainer PSA, same-package
on-demand full-trust coordinator, authenticated local RPC and a fresh external
worker-v1 process for each conversion. Interactive Save As and explicit silent
destinations follow only after the actual background activation/authentication/
exact-title gate passes. No tray, startup service or foreground fallback is used.
The installed `Miyako.PsaFeasibility` OXPS diagnostic package is not updated,
re-signed, uninstalled or repurposed by this separate development identity.

## Early gate

Build on a matching native Windows machine with SDK 26100 and MSVC v143:

```powershell
./desktop/build.ps1 -Architecture x64 -VisualStudioVersion 2022
./desktop/build.ps1 -Architecture ARM64 -VisualStudioVersion 2026
```

Default packages are unsigned. `-CertificateThumbprint` signs only with an
explicitly supplied `CN=DesktopPdfPrototypeDev` certificate in CurrentUser/My;
the build does not create certificates, change trust, install packages or printers.
After explicitly arranging development certificate trust, a Windows 11 24H2+
desktop can run:

```powershell
./desktop/tests/Invoke-DesktopGate.ps1 `
  -PackagePath ./desktop/out/arm64/DesktopPdfPrototype.msix `
  -OutputPath ./desktop/out/arm64/acceptance.json -AllowDevelopmentInstall
```

This installs only the new current-user development package, preserves its
state, submits original synthetic GDI pages and writes sanitized evidence.
It refuses to remove an existing prototype to get installation to succeed.
The native submitter is bounded by an external watchdog. The public workflow
explicitly signs/trusts a temporary certificate **only on a disposable hosted
runner**, then removes that exact certificate. No user-host trust is automated.

The gate checks desktop method authorization, prepares distinct in-memory jobs,
submits in reverse order, and requires the actual PSA to launch the declared
coordinator, authenticate, preserve the complete title and claim exactly once.
It then submits replay/canceled titles and observes their actual rejection.
The PSA drains bounded OXPS bytes without saving them, calls CompleteJob(Failed)
and acknowledges that failure completion returned. A passed gate is **not**
conversion, successful printing, provider publication or Windows-service proof.

`desktop-windows.yml` builds/tests x64 and native ARM64. Only the Windows 11 ARM64
job attempts actual PSA registration/submission; the Server-based x64 build is
not Windows 11 runtime acceptance. Runtime results must be recorded separately
from package/PRI/factory or argument-test results.

## Authentication and correlation

Native `ncalrpc` is local-only, authenticated and packet-private. The server
checks the actual RPC caller token's user SID and Windows session on every
method; workflow methods additionally require the expected AppContainer SID.
Desktop callers cannot claim workflows, and PSA cannot prepare destinations.
Scoped impersonation is reverted before handling state; failures deny access.
Clients supply their expected server user SID in RPC security QoS rather than
trusting an endpoint name. The server is a per-user/session singleton.

The SDK generates asynchronous native RPC stubs using the documented ACF
`[async]` attribute. Calls retain their own heap input/output, binding, event
and async state. A two-second client deadline requests **abortive**
RpcAsyncCancelCall and returns an uncertain timeout, not proof of server
rollback. Completion notification and RpcAsyncCompleteCall must release the
in-flight state before it is freed; a bounded pool retains pending operations
and rejects more work at its cap. No blocked/detached RPC threads or early
marshaling-buffer release. **RPC_C_OPT_CALL_TIMEOUT is ignored for ncalrpc**;
it is not used as a local RPC deadline. A lost Claim reply is never replayed.

The complete `4DPDF1:<256-bit random token>` document title selects a prepared
record. It is spooler-visible, **not an authentication secret**. Status/cancel
use a different private credential. No title, credential, user SID or document
path is emitted in evidence. Workflow SessionId is never equated to a spooler
job ID. Hostile full-trust software already running as this same desktop user
is outside this prototype's trust boundary.

The early gate intentionally has no durable job/destination protocol, worker
supervisor, renderer runtime registration or publication implementation yet.
Those are not stubbed into successful responses. Any failed actual launch,
identity or title gate blocks expansion and must be reported with evidence.

## Primary sources and deferred work

- [Microsoft PSA v4 lifecycle and file/non-file endpoints](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/print-support-app-v4-design-guide).
- [FullTrustProcessLauncher](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher?view=winrt-26100) and the genuine Windows 11 [WithArguments API](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher.launchfulltrustprocessforcurrentappwithargumentsasync?view=winrt-26100). Only the declared coordinator and fixed `--serve` are used.
- [UWP IPC guide](https://learn.microsoft.com/en-us/windows/uwp/communication/interprocess-communication): same-package full-trust proxy and package-SID ACLed Win32 RPC. No custom capability/SCCD or sample entitlement is assumed.
- [JobTitle](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.printing.workflow.printworkflowconfiguration.jobtitle?view=winrt-26100): documented title extraction; exact preservation is measured, not inferred.
- [RPC security QoS v3 server SID](https://learn.microsoft.com/en-us/windows/win32/api/rpcdce/ns-rpcdce-rpc_security_qos_v3_w), [caller impersonation](https://learn.microsoft.com/en-us/windows/win32/api/rpcdce/nf-rpcdce-rpcimpersonateclient), and [token-query caveats](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation).
- [Native RPC ACF async attribute](https://learn.microsoft.com/en-us/windows/win32/midl/async), [abortive cancellation](https://learn.microsoft.com/en-us/windows/win32/api/rpcasync/nf-rpcasync-rpcasynccancelcall) and [completion/resource lifetime](https://learn.microsoft.com/en-us/windows/win32/api/rpcasync/nf-rpcasync-rpcasynccompletecall). [RpcBindingSetOption](https://learn.microsoft.com/en-us/windows/win32/api/rpcdce/nf-rpcdce-rpcbindingsetoption) explicitly excludes local RPC from call-timeout behavior.
- [Restricted capabilities and sideloading](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations): development trust/policy still applies; no Store approval is promised.

Windows-service printing before login/after sign-out remains explicitly deferred.
The separate [printer investigation](../printer/README.md) retains its evidence.
4D integration follows the interface in another PR; no plugin is implemented.
The unchanged [worker v1](../renderer/cli-v1.md) and renderer-owned runtime
inventory will govern later handoff; no dependency binaries enter this gate.
LGPL/source/notices/replacement/relinking, production signing/distribution/WPP,
fidelity, performance and PDF size remain release gates. No own-code license
has been selected.
