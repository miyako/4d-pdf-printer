# Windows 11 runtime evidence runbook

**No runtime rows below have been executed.** Use synthetic pages only. Do not
upload account names, paths, private print tickets/documents, credentials or full
Windows event logs to this public repository.

## Evidence and identities

Use an isolated Windows 11 24H2+ machine with SDK/build outputs from this PR.
Record OS edition/build/patch, package version/hash, commit, test case, UTC
timestamps and a synthetic job token in a private evidence bundle. Record local
identity/session and package/queue visibility **privately**; sanitize them before
sharing. Label every observation as observed, failed, or not run.

Install the diagnostic MSIX for the desktop test identity. Separately determine
whether supported deployment can register it for the **named service identity**
without requiring an interactive login at runtime. Check that identity's package,
queue and profile/data availability, not merely an administrator's `Get-Printer`.
Do not assume `Add-AppxProvisionedPackage`, staging or another user's install
registers it for an existing service account. If deployment requires a one-time
setup login, record that fact; the actual pre-login test is a cold reboot with no
desktop session for any user. A scheduled task is not a Windows-service test.

Find private package captures (for the identity actually activated):

```text
%LOCALAPPDATA%\Packages\<Miyako.PsaFeasibility package family>\LocalState\
  Captures\<random activation id>\activation.json
  Captures\<random activation id>\job.json
  Captures\<random activation id>\ticket.xml
  Captures\<random activation id>\source.oxps
  Captures\<random activation id>\completion.txt
```

`source.partial`, missing `completion.txt`, only `activation.json`, or a stale
`CaptureLock` are evidence of an incomplete run, not successful conversion.
Inspect captured ZIP entries for OXPS fixed-document/page namespaces and document
sequence; MIME/extension alone does not prove payload conformance. Open only
synthetic captures in a suitable XPS/OXPS viewer. Compare the SHA256 of the known
synthetic job name to `jobTitleSha256`; `workflowSessionSha256` is not a GDI job
ID. The probe logs numeric GDI job ID, UTC FILETIME, process/session and stage.
Titles, accounts, queue names and destinations are not written to its JSON log.

## Desktop cases

```powershell
$probe = (Resolve-Path .\printer\out\build\Release\PrintProbe.exe).Path
# OS Save As baseline; choose an .oxps destination.
& $probe --queue 'PSA Diagnostic OXPS (Save As)' --interactive `
  --job-name 'synthetic-desktop-save' --timeout-seconds 120

# Test lpszOutput on file queue: does it bypass OS Save As and yield exact target?
& $probe --queue 'PSA Diagnostic OXPS (Save As)' --noninteractive `
  --output 'C:\PsaLab\synthetic-file-route.oxps' --job-name 'synthetic-file-route'

# No OutputFileTypes, both with and without native output path.
& $probe --queue 'PSA Diagnostic Capture (no file UI)' --noninteractive `
  --output 'C:\PsaLab\synthetic-no-ui-route.oxps' --job-name 'synthetic-no-ui-route'
& $probe --queue 'PSA Diagnostic Capture (no file UI)' --noninteractive `
  --job-name 'synthetic-no-target'
```

Use new destinations per run. `--noninteractive` means **the probe itself never
opens a dialog**, never calls a UI launcher, and bounds submission using a
disposable-process watchdog. It cannot forbid the print system from showing
Save As. Any OS dialog in this mode is a **failed unattended experiment**.
There is intentionally no undocumented "suppress all PSA UI" flag.
The watchdog defaults to 60 seconds (1..300), starts before synchronous queue/GDI
calls, and kills only this disposable submitter if blocked. A killed submitter
may leave a spooled job or OS dialog; inspect/cancel that exact job before retrying.
Zero exit means GDI accepted `EndDoc`, **not** PSA success. Automated capture
intentionally completes Failed even if the submission process exits zero.

For each case collect: did OS UI appear, did `StartDoc` return, numeric job ID,
activation/process evidence, input MIME/payload validity, target API return/error,
requested completion and `CompleteJob` return evidence, spooler outcome, exact
destination existence/size/content, and whether an unintended destination changed.
Repeat interactive cancellation, read-only/nonexistent parent, occupied file,
missing package for identity, missing queue and denied queue permission.
File queue failures can occur **before PSA activation**; no capture folder does
not by itself identify why activation failed. Correlate sanitized
PrintService/AppModel diagnostics privately with the timestamps.

## Real named-user service probe

`PrintProbe.exe --service` implements the SCM service entry point itself; do not
register an ordinary console program and call that a working service.
It performs one submission then stops. Setup must be done manually by an
authorized lab administrator; the repository never creates accounts/services,
changes printer settings, or embeds credentials.

Copy the executable to a protected fixed path, e.g.
`C:\PsaLab\bin\PrintProbe.exe`. Create a separate `C:\PsaLab\evidence` directory
and grant only the named service user/admins the needed access. Give the service
user write permission to the synthetic output directory, read/execute on the
binary, permission to print to the test queue and "Log on as a service".
Keep the executable and service configuration non-writable by untrusted users.

Use the Services management UI to create/configure an ordinary own-process
service named **PsaPrintProbe** (or `sc.exe create` for the binary path, then set
the named-user credentials through an approved private administrator workflow).
Never put a password into a committed script, command transcript or CI secret
for this experiment. Suggested binary path including arguments:

```text
"C:\PsaLab\bin\PrintProbe.exe" --service --noninteractive --queue "PSA Diagnostic Capture (no file UI)" --output "C:\PsaLab\evidence\synthetic-service.oxps" --job-name synthetic-service --log "C:\PsaLab\evidence\service.jsonl" --timeout-seconds 90 --delay-seconds 120
```

Select **Automatic**, set **Log On** to the named valid user (not Local System),
and record configuration privately. The delay supports a sign-out test; it is
included in the watchdog's total budget. Do not enable service recovery retries.

| Case | Required procedure and acceptance evidence |
|---|---|
| Service with desktop present | Start through SCM. Log must show service submission in Windows session 0, not a console impersonation. Compare package/queue visibility, PSA identity/session privately and target behavior. |
| Before first desktop login | Configure Automatic, cold reboot, **do not log in**. Observe remotely through an independently authorized admin channel, or retrieve evidence later while verifying timestamps precede first login. Delayed startup/activation requiring login is a failure. |
| After sign-out | Start delayed one-shot service; fully sign out (not lock or disconnect), leave no user helper/session, let delay elapse. Retrieve timestamped evidence later. Also test a service start initiated remotely while signed out. |
| File queue `lpszOutput` | Repeat service cases with Save As queue and fresh `.oxps` destinations. Any blocked UI, timeout, absent activation or wrong target fails the unattended case. |
| Registration/profile/permission negative cases | Missing package for service user, unavailable profile, queue denied, destination denied, missing target and UI-ineligible states must fail explicitly, not hang indefinitely or report PDF success. |

The probe JSON proves the **submitter's** numeric session, not the PSA host's.
Use OS tracing/debugger/Process Explorer in the private lab to establish the
activated host's token/session/package identity, including whether it survives
sign-out. Capture those private facts in sanitized yes/no evidence rather than
publishing machine/account data. Review SCM exit code and `queryex` state as well
as the log. Service exit success still does not establish workflow success.

Do not proceed to worker integration merely because a job reaches `StartDoc`.
Require actual background activation/source evidence in each state and a
supported authenticated coordinator handoff. No tray/helper session may be
introduced to make a failing test pass.

## Repeated jobs and later 4D checks

Initially submit serially and wait for workflow completion/evidence, not only
`EndDoc`, before the next job. Use unique names/destinations and stop after
timeouts/stale locks. Observe distinct activation IDs, process reuse, failures,
memory/handle growth and recovery; do not promise numerical reliability or PDF
size improvement. Printer-side containment cannot repair caller GDI misuse or
upstream Windows faults.

Only after native gates close, test 4D 19 (limited support acceptable), current
desktop/remote clients and named-user 4D Server service independently.
`SET CURRENT PRINTER` precedes `SET PRINT OPTION`. Explicitly choose our queue,
not **Generic PDF driver** (that chooses Microsoft's). Destination option 9:
printer=1, file=2, PDF=3; custom queue routing must be tested, not assumed.
Document-name option is 12. Serialize the entire native printing lifecycle on
4D 19; plugin `threadSafe` flags cannot repair printing/settings concurrency.
4D 20 R4+ non-blocking printing differs. The plugin tutorial is
`miyako/4d-plugin-tutorial`, branch `example`; no plugin is implemented here.
Use the [4D service-account guide](https://developer.4d.com/docs/server/service)
for account setup only, not as evidence of PSA support.

Remove the lab service, installed diagnostic package and specific development
certificate when finished, using their recorded names/thumbprints. Preserve only
sanitized synthetic evidence; delete captures locally after review. Never remove
whole profile/package directories indiscriminately.
