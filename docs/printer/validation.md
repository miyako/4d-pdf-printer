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

## Not run / not proven

No signing/trust/installation, Windows 11 queue creation, semantic PDC acceptance,
real XPS/OXPS capture, Save As, native output-path mapping, named-service account
registration/activation, pre-login execution, post-sign-out execution, 4D printing,
external coordinator IPC or PDF rendering has been observed in this printer PR.
No PDF size or repeated-job reliability metric has been measured.

Use the [runtime runbook](runbook.md) to close those gaps. Build or direct COM
factory success is not a substitute for the required session-0 activation gate.
