# Rendering and containment boundary

The executable is a disposable synchronous native worker, not a broker. A
process initializes job-local objects, opens one package, renders every fixed
document/page, finalizes a single Cairo PDF stream, publishes without replacement,
emits one status and exits. It never accepts a second job or starts children.

1. Validate CLI and retain a private status stdout handle; suppress third-party
   diagnostics that might expose document content.
2. Read every entry through libarchive's seekable ZIP reader. Reject encryption,
   corruption, incomplete packages and provisional expansion/count limits.
3. Use `gxps_file_new`, `gxps_file_get_document`,
   `gxps_document_get_page` and `gxps_page_render`. No package content is
   extracted to disk; no custom XPS translator exists.
4. Render to `cairo_pdf_surface_create_for_stream` at original page dimensions,
   with 0.75 coordinate scaling. Use normal Cairo font subsetting/image/vector
   capabilities and check both render result and Cairo context/surface state.
5. Explicitly finish/destroy the PDF surface, sync/close the job-local output,
   atomically publish, then clean temporary state. Fixed creation/modification
   timestamps avoid wall-clock metadata variance; bit-for-bit identity across
   operating systems, fonts or library versions is not promised.

GErrors become redacted, typed failures. GLib warnings/critical diagnostics fail
the job; fatal assertions/native exceptions are **not** caught and followed by
more rendering. Backend error checks do not prove complete semantic fidelity.
The pinned overlay makes specific known silent omissions visible, not all of them.
No global persistent settings, renderer daemon, implicit retry or state-sharing
between jobs exists.

## Supervisor handoff

This PR intentionally does not implement a production supervisor. A caller must:

- Stage an immutable input copy and output in a fresh, access-controlled job
  area; authorize exact paths and job identity, never document title alone.
  Keep the input unchanged between preflight and libgxps reopening it. Parent
  paths/ACLs must not be writable by unrelated users or redirectable through
  attacker-controlled links. Windows temporary files inherit the parent's ACL;
  POSIX staging uses directory mode 0700 / file mode 0600.
- Start the worker directly, with Unicode-safe argument quoting, no shell,
  no desktop dependency, explicit working directory and restricted inherited
  handles. Drain/bound stdout and stderr. Initially serialize jobs.
- On Windows create/assign a non-breakaway Job Object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; create the process suspended, assign
  before resuming, and treat assignment/resume failure as job failure.
  Establish measured memory/process/CPU limits and a configurable bounded
  wall-clock timeout. These limits are **not implemented in the worker**.
- Own timeout and explicit cancellation, terminate the Job Object, wait for
  all processes and close handles. A test runner uses child-process deadlines;
  Python's timeout kills/reaps the one child (the worker creates no descendants).
  This is not a replacement for production Job Object supervision.
- Keep separate coordinator outcomes for timeout, cancellation, crash/abnormal
  exit, startup failure and malformed/missing status. Normal nonzero worker
  exits retain their stable worker error. Do not fabricate a worker success
  record for any coordinator failure.
- Reconcile filesystem publication independently of terminal status. On
  forced termination, recover/delete only this job's retained temporary
  directory after reaping; retain evidence according to policy without
  logging document content. Cleanup is not guaranteed by OS process exit.

There is no implemented PSA launch path, service IPC, printer registration,
4D plugin/ABI or 4D Server named-user service integration. Session-0 execution,
font discovery without profiles, DLL loading/replacement policy, ACLs and
deployment must be exercised under actual Windows identities. The disposable
boundary does **not** repair spooler, driver/kernel, broker, 4D-side or Microsoft
virtual printer faults; it only limits rendering-process state reuse.
