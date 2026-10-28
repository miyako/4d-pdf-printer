# Desktop gate evidence

The desktop PDF implementation is **blocked at its early runtime gate**. No
renderer is attached, no PDF is produced, and no workflow success is reported.
These results do not prove that PSA/full-trust/RPC integration is supported or
unsupported on an ordinary Windows desktop.

## Native build and registration

[Run 40701285156](https://github.com/miyako/4d-pdf-printer/actions/runs/40701285156)
at `9a0a560`:

| Surface | Observed result |
|---|---|
| x64 native | MSVC v143/SDK 26100 build, native argument test, PE checks, PRI generation and MakeAppx succeeded. The Server-based runner is not Windows 11 runtime evidence. |
| ARM64 native | Native ARM64 build/test/PE/PRI/MSIX succeeded on Windows 11 build 26200. No x64-worker emulation is involved. |
| Current-user deployment | Separate development identity installed; both new PDF-named prototype queues registered. Diagnostic package metadata remained unchanged. |
| Desktop CLI activation | `LaunchFullTrustProcessForAppWithArgumentsAsync("Printer", "--serve")` did not yield a launch-result object. The client caught `0x80010117` / RPC_E_CALL_COMPLETE. That source revision did not distinguish operation creation from wait/GetResults. |
| PSA activation/auth/title | Not established; no PSA records were captured. |

The recorded package SHA256 was
`e70f7a6b472afe249a9821512b3529d510effcc219df2e9ed1c5831413fa6d70`.
The runner's native OS/process architecture was ARM64, and the submitter session
was 2. That alone does not establish logon type, an interactive shell or PSA
readiness. RPC_E_CALL_COMPLETE means "Call context cannot be accessed after call
completed"; its generic text is **not a root-cause diagnosis**.

## Independent actual PSA attempt

[Run 40702323846](https://github.com/miyako/4d-pdf-printer/actions/runs/40702323846)
at `1a5337d` removed the desktop-launch dependency
from the diagnostic experiment. It attempted one direct synthetic GDI job to
the new no-file-UI endpoint with an intentionally unprepared random title.
The intended partial observation was PSA-background launch/authentication,
followed by missing-config rejection and failed completion, never PDF success.

Both native builds/packages again passed, and Windows 11 ARM64 registration
succeeded. The attempt failed inside the GDI submission function with
`0x80070BBB` / Win32 3003, before the client entered its PSA-readiness wait.
There were **no captured PSA activation records**; their absence does not prove
the task's Run method was never entered. This revision did not record
the raw StartDoc result/immediate LastError, so this observation is not used to
infer which GDI call failed. The following diagnostic revision adds the existing
native probe's exact StartDoc/context helpers to distinguish that.

The recorded package SHA256 was
`c0d0db30e0e5effb3e27953e723aed41522d8f87151b74db0487172da7124ea0`.
The runner again reported native ARM64, OS build 26200 and submitter session 2.
Diagnostic metadata was preserved. No service/configuration, profile, COM
apartment or foreground-launch workaround was attempted.

## What remains unverified

- Actual PSA-background FullTrustProcessLauncher creation/await/result and
  authenticated RPC on an ordinary supported Windows desktop.
- In-flight async-RPC abort/completion, pending-cap/recovery, deadline races and
  callback/DLL lifetime. The code contains explicit assertions and ownership
  safeguards, but compiling them or reaching an absent server is not proof.
- Exact GDI title round trip, method-role enforcement through the actual PSA,
  one-time/replay/cancel binding, and distinct-user/session runtime negatives.
- Any real worker conversion through PSA, interactive StorageFile publication,
  silent destination routing, final workflow success or 4D integration.

The large pipeline remains stopped. A complete gate needs actual execution, not
a manufactured PSA caller or a green build label. Hosted-context failure is
separate from the user's already-observed ordinary-desktop diagnostic OXPS
printing. Windows-service support remains deferred, not tested again here.
