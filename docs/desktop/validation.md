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
The diagnostic metadata snapshot was unchanged. No service/configuration, profile, COM
apartment or foreground-launch workaround was attempted.

## Final bounded follow-up: stop condition

[Run 40703572402](https://github.com/miyako/4d-pdf-printer/actions/runs/40703572402)
at `d58bb2f4e0c8c08d842935865adec0dfa902566b` used **separate disposable ARM64
hosts** for desktop launch and independent direct GDI submission. A coordinator
from one experiment could not satisfy readiness in the other.

All three native build/package jobs passed: x64 and both ARM64 jobs. Both ARM64
runtime attempts installed the new identity and registered both queues, then
returned `blocked`, with `pdfProduced=false` and no captured PSA records.

| Independent observation | Exact result |
|---|---|
| Packaged desktop launcher | Operation creation returned; the combined wait/GetResults step threw `0x80010117` at `coordinator-launch-wait-get-results`. `launchResultAvailable=false`: no launch-result/error-details object was obtained. This does not establish whether the coordinator executable was started. |
| Direct synthetic GDI | CreateDC succeeded; `gdi-start-doc` recorded signed StartDoc return `-1`, immediate LastError `3003` and HRESULT `0x80070BBB`. The native helper clears LastError before the call and captures it before other API calls. |
| Own caller context, both hosts | Not AppContainer; expected package identity verified; token/process session 2; apartment type 1 (MTA), qualifier 0; LSA query NTSTATUS 0, actual logon type 2; profile-hive open error 0; current-user package query error 122/count 1. |

The preceding raw-context run
[40703033308](https://github.com/miyako/4d-pdf-printer/actions/runs/40703033308)
at `c00455f` independently recorded the same signed StartDoc `-1`/3003 and own
context. Actual LSA logon type 2 is stronger evidence than merely observing
session 2, but is not proof of a ready shell, complete profile initialization or
an activatable PSA environment. Neither generic error establishes a root cause.
No captured record is not proof that Run was never entered.

Exact sanitized acceptance records, including checked-out source, package/EXE
hashes and the submitted synthetic title hash where submission was attempted:

- [Desktop launcher](evidence/40703572402-arm64-desktop.json)
- [Independent GDI/PSA attempt](evidence/40703572402-arm64-psa.json)

No OXPS source was captured, so no OXPS-content hash or conversion assertion is
available. Different package/EXE hashes identify separate native builds on the
two hosts, not interchangeable artifacts. The wrapper's .NET exception is not
the native failure; the typed client stage and HRESULT above are authoritative.

`diagnosticPreserved=true` compares package-name/status snapshots before and
after. An empty snapshot also compares equal: this does **not** establish that
the old package was installed, nor test its LocalState contents or update
lifecycle. The prototype uses a separate identity and never requests removal or
an update of the diagnostic package.

## Smallest proposed ordinary-desktop gate

**For creator review only; no user-host run or trust change is authorized.**
The next genuine observation would use the existing `-PsaBootstrapOnly` mode on
an explicitly approved ordinary Windows 11 24H2+ desktop, matching native
architecture, with the coordinator initially absent. Development trust and the
new current-user prototype installation require separate explicit approval;
there is no service/profile/COM/caller substitution.

One original synthetic page to the no-file-UI endpoint must demonstrate positive
StartDoc, actual PSA metadata/launch result, authenticated AppContainer RPC and
the expected missing-config rejection with failed/canceled completion. It must
not show a picker, claim PDF success or turn a dummy print into production
startup. This can only close the **partial** actual-background gate. The full
desktop startup, in-flight RPC, auth/title/replay/cancel gate still needs its own
measured pass before destination/worker/publication implementation resumes.

No new startup bridge is implemented or selected. An app-service startup bridge
would require a separate primary-source-backed design review and would not
itself prove PSA activation.

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
