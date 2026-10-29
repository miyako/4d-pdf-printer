# Conditional exact-job worker handoff

**Historical service-coordinator proposal; Windows-service printing is deferred.**
The separately authorized [desktop-first handoff](../desktop/README.md) uses a
new development identity and on-demand same-package full-trust coordinator.
Its actual activation/authentication/title gate must pass before worker wiring;
it is not a service-session workaround or evidence of PDF output.

**The service handoff below is not implemented or proven permissible.**
The [actual named-user session-0 submission failure](validation.md#named-user-session-0-submission-gate)
keeps this architecture on **HOLD**, even with the desktop signed in. Do not
implement a service broker/4D plugin to bypass the unresolved service activation gate.
This document proposes a boundary, not a functioning broker. The independent
renderer owns the authoritative CLI/status documentation under `docs/renderer/`
and `renderer/status-v1.schema.json` (those files may land in a separate PR).

The agreed worker interface is:

```text
pdf-render-worker.exe --protocol-version 1 --input <Unicode XPS/OXPS path> --output <new PDF path>
```

One job/process; no stdin/retries/overwrite. The coordinator must require a normal
exit 0 and exactly one complete UTF-8 JSON status line matching schema
`4d-pdf-printer.worker-status`, `version:1`, `result:"ok"`, matching `exit_code`,
`output_published:true`, and the documented cleanup/result invariants. `info`,
mere file existence, missing/truncated JSON, mismatching version, crash/kill or
timeout is **not conversion success**. Post-publication failures may leave a
finished PDF: do not retry implicitly or overwrite it. Reference the renderer
schema rather than recreating a competing status definition here.

## Proposed coordinator boundary

1. An authenticated caller registers one immutable job record with a durable
   service coordinator **before submission**, receiving an unguessable job token.
   Record binds authenticated principal, unique queue identity, expected
   submission token, destination authorization, immutable renderer options,
   timeout, no-overwrite publication policy, and creation/expiry. No process
   environment variables, user defaults or shared "next job" state.
2. A supported AppContainer-to-coordinator transport must carry only that job's
   input/ticket and token, with identity/ACL checks and size/deadline limits.
   The workflow's `SessionId` can be a correlation field but is not assumed to be
   the GDI job ID or security identity. A title token alone is not authentication.
   Mapping the token into this PSA job needs a supported exact-job carrier and
   runtime verification; custom PrintTicket/private settings are a candidate, not
   an implemented contract. The diagnostic title hash only helps lab correlation.
3. Start serialized. Coordinator privately stages input plus ticket and creates
   a fresh disposable worker in a Windows Job Object with memory/process limits,
   hard deadline, process-tree termination and reaping. Worker code must never
   load into 4D, spoolsv, or the long-lived coordinator. CWD and executable path
   are coordinator-controlled; use correct Unicode argument quoting, not a shell.
   The independent renderer investigation has observed upstream libgxps process
   aborts on malformed paths and incomplete-content error propagation. Missing
   JSON on process death and abandoned job-local files are concrete supervisor
   requirements, not reasons to retry implicitly or move conversion in-process.
4. The worker takes only staged source/output paths. The current CLI does **not**
   apply arbitrary PrintTicket options: decide and test whether upstream OXPS
   already materializes each option (copies, N-up, page selection, orientation).
   Unsupported/unapplied options must fail, not disappear silently.
5. Interactive output remains the OS-authorized `StorageFile`, not its path
   string. If a legal transport is established, return PDF bytes to PSA for
   writing that exact file and finish within the workflow lifetime. A service
   cannot simply reopen a UWP `StorageFile` path and inherit broker access.
   Unattended output is the coordinator-authorized destination for the immutable
   job, not a presumed mapping from `lpszOutput` or a title.
6. Persist coordinator outcomes separately: accepted, captured, running,
   converted, published, failed/canceled/timed-out, cleanup/recovery required.
   Do not report `CompleteJob(Succeeded)` before the requested publication
   actually succeeds. Avoid success-shaped fallbacks and uncertain retries.

The diagnostic package provides only `source.oxps`, `ticket.xml`, redacted
`job.json`, and activation evidence under its private LocalState. Those files
are **not a shared queue**, a supported service handoff, or a request to start
watching the user's package storage.

## Gate before choosing transport

Prove PSA activation for the registered service identity before interactive login
and after sign-out. Microsoft documents a supported packaged-app-to-Win32 RPC
pattern, including package-SID endpoint ACLs, and supplies a UWP client / actual
Windows-service `ncalrpc` example. The concrete candidate is:

```text
activated PSA task
  -> authenticated, package-SID-ACL-protected local RPC
  -> dedicated named-user coordinator Windows service
  -> fresh disposable pdf-render-worker.exe
```

See [source findings 10-14](sources.md). This is a source-supported **transport
pattern**, not an implemented or observed PSA/service integration. The Microsoft
sample's service defaults to LocalSystem; it does not prove our named-user setup.
The coordinator is a separately installed service, not an assumed RPC facility in
4D Server, a tray app, or a proposed named-user service embedded in this MSIX.
No custom capability or broad all-AppContainer endpoint permission is proposed.
The general package-SID ACL guidance is distinct from the CustomCapability
sample's custom-capability authorization model. The latter can involve capability
approval, an SCCD, provisioning and specific signing/deployment prerequisites;
none have been established, performed or inherited by this independent package.
The sample is transport implementation evidence, **not** our deployment recipe
or permission grant. Validate our own package family/SID, signing/trust,
installation scope and service ACLs on the target system.
Package SID access alone is insufficient job authorization: also authenticate
the expected user/service principal and bind the unguessable exact-job token to
that identity. Fail closed on missing identity or stale/mismatched job records.
Cross-session binding, endpoint ACLs, payload limits, cancellation/disconnect,
deployment and recovery all need target-host validation.

`FullTrustProcessLauncher` is a same-package declared-component mechanism
requiring `runFullTrust`, not arbitrary process creation. Its existence neither
proves this PSA/background/no-login use nor establishes a blanket prohibition.
Likewise app services, generic manifest scope flags, localhost permission or
ordinary background activation do not prove a session-0 arrangement.
Logoff is a documented task cancellation reason; a deferral is not an unlimited
lease. Do not retry a possibly published conversion just because PSA disconnected.
Test sign-out **during** an active conversion independently of submitting a new
job after sign-out: observe PSA cancellation/completion, spooler final state,
coordinator and worker lifetime, and exact destination publication as separate
outcomes. A service or worker surviving sign-out does not prove that the PSA job
finished, that publication was authorized, or that the caller received success.
The current PR cannot execute that conversion test because no broker/worker
integration exists; it is an explicit gate for the later integration.
If the platform cannot support this, stop and present the incompatibility. Do not
replace it silently with a tray app, legacy driver or in-spooler renderer.
