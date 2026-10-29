# Actual PSA callback activation probe

**Step2 only; not a PDF pipeline.** The new `ActivationProbe` build mode bypasses
the retained RPC sources and includes no renderer/runtime dependencies. Actual
callback-to-host activation remains **unproven** until the separately approved
normal-desktop observation. Native compilation and controlled-child units cannot
pass that gate. Full B, comparison continuation, destinations/UI and services
remain paused. No PDF is produced; the PSA deliberately completes Failed/Canceled.

**Version0.2.1 scoped validation passed:** manual
[40852574080](https://github.com/miyako/4d-pdf-printer/actions/runs/40852574080)
tested clean source `6ed19cbd71758e4d26a5ef6dc3dd4529475664e9`.
Both x64/native ARM64 passed complete controlled native units, unsigned package
checks and actual PowerShell19+7+9 cases. This verifies the narrowly repaired
TokenElevation helper and diagnostic/test surfaces, **not** normal packaged
preparation or the actual PSA callback. The next host boundary is parent-reviewed
**preparation only**, never printing automatically.
[Exact receipt](evidence/40852574080.json). All failures below remain historical.

**New version0.2.2 diagnostics pending validation:** a subsequent actual ARM64
preparation-only attempt with the user's locally built/signed0.2.1 passed
`Current(false)` but stopped at `prepare.security` / `package-sid` /
`IsValidSid` / `sid-validity`. No observer, print or callback occurred. This
does not distinguish null output from a nonnull invalid SID, nor prove a
derivation/lifetime defect. Version0.2.2 is diagnostic only; no host update
is authorized by the source change.

**Historical first-attempt STOP checkpoint:** the initially authorized native batch
[40832146213](https://github.com/miyako/4d-pdf-printer/actions/runs/40832146213)
tested source `765d6a75064e63beb961822771d8793a4f67fdbf` and **failed
compilation on x64 and ARM64**. `handle` is ambiguous between the existing
`desktop::handle` and imported `winrt::handle`, first at Contract.h:68, with
further occurrences and cascading reset/nullptr errors. The App bootstrap and
PSA DLL linked on both architectures, but host/control/unit compilation did not
complete. Native units, MSIX packaging and activation were **not executed**.
[Exact failure receipt](evidence/40832146213.json). Portable checks passed;
they did not cover Windows-only name lookup or the consented Windows wrapper.
At docs checkpoint `918a4de`, no repair/rerun was performed and that source was
not ready for a host run.

**Separately authorized surgical follow-up passed:** fresh manual run
[40832585234](https://github.com/miyako/4d-pdf-printer/actions/runs/40832585234)
tested source `b8f4b7b1156ad61ac922f01265d35c19371b39ca` with clean source
receipts. The only source change is13 explicit `desktop::handle` qualifications
in four new probe files; no shared types/namespaces, protocol, ACLs, timings or
expected assertions changed. Both x64 job123309968974 and native ARM64
job123309968621 passed full own-code compilation, one native controlled-file/
process CTest each (3.27s/3.24s), unsigned MSIX/PE/manifest/PRI checks and
PowerShell writer/consent/manifest guards. See the
[fresh run receipt](evidence/40832585234.json), original
[x64 build receipt](evidence/40832585234-x64.json) and
[ARM64 build receipt](evidence/40832585234-arm64.json).
Public artifacts contain JSON/text only, not binaries.

This resolves the compile blocker, **not the actual PSA activation gate**.
No AppContainer file sharing, system FullTrust launch, package installation,
printing or consented Windows wrapper body was exercised. The normal-desktop
procedure below is ready for **parent review only**, not authorized execution.
At that compile checkpoint, development-family and signing-certificate state
were still unknown. The later supervised desktop state is described below.

### Subsequent actual desktop preparation stop

The separately supervised native ARM64 desktop attempt with the user's clean
`b8f4b7b` build subsequently installed the new development package and passed
identity/hash preflight, but stopped at **prepare**, before any observer,
synthetic submission or actual PSA callback. The first result/old directories
must remain preserved. A subsequent existing-output-directory rejection did not
replace that first result. This is not an activation failure or evidence against
the healthy diagnostic package. Certificate/trust/installation are not repeated
by the preparation-diagnostic fix.

Source inspection finds the native `--prepare` dispatch and expected
`prepared-private-correlation`/64-lowercase-hex contract agree. A native failure
returns exit1 and a JSON HRESULT, often still at `control-arguments` even when
`Current()`/`Prepare()` failed; that stage alone does not identify the failing API.
The old wrapper consumed stdout into an unsaved private variable and discarded
native exit/error details when its generic contract check or JSON parsing failed.
Its saved PowerShell RuntimeException `0x80131501` is **not** a Win32 or launcher
root cause.

The masking was reproduced locally by executing the actual original
prepare/catch/finally/report body with a simulated native exit1/JSON
`0x80070005`: only generic `0x80131501` survived. That access-denied value is
**synthetic test data, not the user's observed native error**. The actual native
cause remains unknown.

The narrow PowerShell-only correction uses the existing subprocess stream-pump
pattern, with fixed alias/`--prepare`, separate create-new
`prepare.stdout.private`/`prepare.stderr.private` files, at most16KiB retained
per stream, a45s capture deadline and bounded exact-client exit observation.
It records process start/exit/exit code, byte counts, capture API/limit/deadline
failures and allowlisted native stage/HRESULT without publishing job IDs, raw
stdout/stderr, paths or exception messages. It no longer relies on ambient
`$LASTEXITCODE`. `failure.private.json` preserves the bounded original exception
message; a result-write collision retains the primary exception and typed
secondary `ReportWriteError`. Existing files are never overwritten.
Native code, package/alias registration, protocol, ACLs and request expiry are
unchanged. This capture behavior has **not** been exercised against the Windows
AppExecutionAlias; no native rebuild, CI dispatch or host retry is included.

`Check-ActivationProbe.ps1` now also runs16 actual production prepare/parser/
catch/finally/report cases: valid preparation, native JSON error/exit1,
empty/malformed/array output, wrong stage/ID, stdout/stderr floods, invalid UTF8,
timeout, process-start error and stdout/stderr/private-error/result collisions.
Only the native executable is replaced by a controlled local PowerShell child;
capture, real file I/O and terminal error handling are production code.
Preflight/ACL checks are outside that extracted test segment, not bypassed in
production. Neither observer/print execution nor Windows alias behavior is
proven by these tests. The retained7+9 harness cases still pass locally.

No large ACL/metadata collection is requested to infer a discarded HRESULT;
existing metadata cannot reconstruct it. Preserve all first-attempt records.
The parent-authorized **source change**, not host execution, adds the small
`-PreparationOnly` branch with separate `-AllowPackagePreparation` consent.
Native preparation writes package-local state, so this is not a read-only
diagnostic. It uses the same production capture/contract/error path and all
existing desktop/package/build/hash/path/ACL checks, but cannot reach either
`--observe` or `--print`, even if preparation succeeds or print consent was also
supplied. It always reports `activationGatePassed:false`, `pdfProduced:false`
and mode `preparation-only-not-activation`; valid preparation gets that result
label, while errors remain blocked with explicit diagnostics. No expired job
records are removed, request replaced, or result overwritten. A fresh protected
output root is still required.

The report separately records `wrapperVersion:2` and the SHA256 of the executing
wrapper. Existing `sourceCommit`/own-code hashes continue to identify the
unchanged native build/installed package, not the updated script. The16 actual
body tests now execute the preparation-only branch with the real alternative
print branch present and trap any observer/submission process call; success,
native error, malformed output, timeout and collision paths all keep the gate
false. Separate real-script tests enforce the new explicit consent and platform
guards. No C++ change, native rebuild, CI dispatch or user execution is included.
Only the parent may review and seek consent for one subsequent preparation-only
call; the full synthetic-print procedure below remains paused. Do not retry
preparation, reinstall, alter certificates/COM/profile/DPI or print automatically.

Future parent command blocks must use an enclosing `& { ... }` so a terminating
guard error aborts the whole pasted block; independently pasted top-level
statements in an interactive shell can otherwise continue after a prior error.
This does not blame the caller or authorize reusing the preserved output root.

### Version0.2.1 diagnostic update (not an access-denied fix)

The subsequent preparation-only Windows observation established actual control
exit1 with native `0x80070005`, complete captured JSON and observed process exit.
It exercised neither observer, print, callback nor host launch. The outer
PowerShell exception is separate. The reported Windows wrapper SHA is exactly
the repository's version2 bytes converted to CRLF, verified locally.
Installing as the same user from an elevated installer shell does not establish
why the later non-elevated preparation failed; no reinstall/elevation/ACL change
is prescribed.

The narrowly approved next development version is **0.2.1.0**, same family and
publisher. It adds per-invocation diagnostic context, not a guessed permission
fix. The original HRESULT and compatibility stage remain; optional
`error_context` supplies fixed stage/API/object-role/origin/reason and an
immediate raw code only when one was returned. It distinguishes Win32 failures,
WinRT failures, C++ filesystem operations and our own validation failures.
Token size/value queries and SID copying inside the shared identity helpers are
covered, not collapsed into `Current()`. Successful APIs do not sample
GetLastError. The PowerShell version3 summary copies only enumerated safe fields;
raw private captures and preparation-only isolation remain unchanged. No token,
path, SID, descriptor or document content is added.

**Preserved-state compatibility:** the existing admission scan does not call
`Validate` or compare old package hashes. It still checks directory tokens,
owner/DACL, request parsing and `created_ms`/`expires_ms` freshness. Expired
well-formed0.2.0 requests remain untouched and count toward cap8; fresh requests
remain busy. Actual callback/host validation continues to reject old
full-package-name hashes after update. Missing/partial/bad-ACL/invalid-time old
records can still block admission; diagnostics identify `existing-job` or
`existing-request`, but do not delete, normalize or rewrite them.

Deterministic native tests target preserved API/validation HRESULTs, all queried
token-information roles, SID validity, invalid file handles, JSON numeric guards
and serialization. Synthetic old-version records test actual admission with
unchanged bytes, fresh-request rejection and old-hash replay rejection.
These still use fake UnitContext/temp files, **not** real medium/nonadmin
packaged `Current()` or AppContainer access. No restricted-token or real-caller
coverage is claimed. Existing API libraries remain WindowsApp/Advapi32/Userenv/
Ole32/Shell32; the new context is standard C++ data, not a runtime dependency.

**Resumed validation STOP:** saved source `582d45b006749e1268d2e6f893fe1cb38fe4811f`
was tested in [40841276658](https://github.com/miyako/4d-pdf-printer/actions/runs/40841276658),
then canceled at the user's pause request. It remains **cancelled**, not verified
or successful. Its preserved partial logs nevertheless show C3861 at Tests.cpp:130
(`Base`) and139/142 (`Prepare`): the new compatibility test included `Files.h`
instead of the declaring `Controller.h`. The four payload targets linked on
both architectures; the unit target did not. No native CTest or packaging
was established. [Canceled-attempt receipt](evidence/40841276658.json).

On explicit resume, the only code change was that one include. Local portable
C++20 protocol/diagnostic cases, actual PowerShell preparation/report17 cases,
writer/consent/manifest guards and retained7+9 harness cases passed. These do not
exercise Windows-only code. Fresh manual
[40847288110](https://github.com/miyako/4d-pdf-printer/actions/runs/40847288110)
tested source `2b16916e4d4040c0f6462fad0d453d02b38ffb99`: all five own-code
targets compiled and linked on x64 and native ARM64, but the native CTest
**failed on both**, after0.07s/0.03s, with exactly
`Activation probe unit failed HRESULT=0x80070057; no PSA/printing performed.`
CTest exited8. The unit catch does not identify a subcase/API; do not infer one
or equate its E_INVALIDARG with the user's preparation E_ACCESSDENIED.
Unsigned packaging and subsequent hosted script checks were not reached.
See the [fresh failure receipt](evidence/40847288110.json) and unmodified
[x64](evidence/40847288110-x64-LastTest.log) /
[ARM64](evidence/40847288110-arm64-LastTest.log) CTest logs.

Work stopped at this first fresh native failure: no repair/retry, host build,
signing, installation, preparation or printing followed. Version0.2.1 diagnostics
are **not ready for user update**. The actual desktop E_ACCESSDENIED cause is
still unknown; the actual PSA callback gate is still unexercised. Historical
receipts, old roots and installed0.2.0/certificate/diagnostic queues are unchanged.

**Separately authorized observability-only follow-up:** source
`07ebd9b36f8e05eb29391dba83939a384a839589` changes only Tests.cpp: named subcases,
`source_location` assertion lines, separate assertion/production-exception
reporting and stack-local safe diagnostic context passed through existing
optional helper arguments. Existing predicates and production behavior remain
unchanged. Portable tests execute assertion capture/exact line, separate kinds,
preserved HRESULT, bounded formatting without source paths and context reset.
No static logic fix was proven before the batch.

Fresh manual [40848598469](https://github.com/miyako/4d-pdf-printer/actions/runs/40848598469)
compiled/linked all five targets on x64 and native ARM64, then **failed** the
same CTest after0.07s/0.03s. The now-specific first failure on both is
`kind=assertion case=diagnostic.token-size-error case_line=171 assertion_line=175`,
with `context_stage=current.token api=GetTokenInformation object=token-elevation
origin=validation reason=token-size-probe has_code=1 code=24 code_kind=win32`.
The invalid-handle/zero-buffer test expects `ERROR_INVALID_HANDLE`6 for every
information class; the observed `TokenElevation` call instead returned
`ERROR_BAD_LENGTH`24, making that code predicate false. This is a **test
assertion**, not a new native E_INVALIDARG, the user's actual preparation result,
or proof of what a valid-token query would return. The production guard's
original E_ACCESSDENIED mapping and captured raw DWORD are unchanged.

The state-compatibility/controlled-child cases and unsigned packaging were not
reached. [Exact receipt](evidence/40848598469.json) and unmodified
[x64](evidence/40848598469-x64-LastTest.log) /
[ARM64](evidence/40848598469-arm64-LastTest.log) logs preserve the result.
Again stopped without changing the assertion, production code or security,
or dispatching a retry. Version0.2.1 remains **not ready for user update**;
actual user cause and PSA gate remain unknown/unexercised.

The next narrowly authorized unit revision replaces that invalid-handle error
assumption with a **direct same-arguments Win32 reference**, capturing BOOL,
failed-API DWORD and required length immediately. It also opens a real current
process `TOKEN_QUERY` handle and compares all seven helper information classes
against direct queries, including an explicit fixed-size `TOKEN_ELEVATION`.
The [GetTokenInformation contract](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation)
allows null/zero probes but does not promise one universal failure code.
The [information-class contract](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class)
documents fixed value types and that `TokenImpersonationLevel` fails on primary
tokens; that direct rejection is checked separately, not treated as required
helper success. Only fixed class names/query sizes/BOOL/error/length are logged,
never token contents or SIDs. Production size-probe/deny conditions are unchanged.
These are real process-token queries on CI, **not** normal packaged caller,
AppContainer, preparation or activation proof.

**Valid-token discrepancy reproduced; STOP:** fresh manual
[40849698888](https://github.com/miyako/4d-pdf-printer/actions/runs/40849698888)
tested source `c5612822f99589eaf07d21e791fdf0daccf8051f` without changing
production code. All five targets compiled/linked on x64 and native ARM64.
The corrected invalid-handle references passed for all seven classes.
On the real valid process token, the first five direct/helper comparisons
matched; primary-token impersonation queries failed87 as documented and their
helper rejection matched. Then, on **both architectures**:

| TokenElevation query | Input bytes | BOOL | Error | Required bytes |
| --- | ---: | ---: | ---: | ---: |
| Direct null/zero reference | 0 | 0 | 24 (`ERROR_BAD_LENGTH`) | 4 |
| Direct `TOKEN_ELEVATION` reference | 4 | 1 | 0 | 4 |
| Production `TokenData` size probe | 0 | rejected | captured24; HRESULT `0x80070005` | direct reference4 |

The precise failure is `kind=production-exception case=valid-token.helper
case_line=246`, stage `current.token`, API `GetTokenInformation`, object
`token-elevation`, origin `validation`, reason `token-size-probe`, code24.
This demonstrates an **own helper contract bug**: Common.h:75 requires
`ERROR_INSUFFICIENT_BUFFER`122 for every size probe and throws at79, before
querying this valid fixed-size class. `Current(false)` uses it at Contract.h:86.
No production fix or guard relaxation has been made. This is stronger than the
earlier invalid-handle observation, but still does **not** prove the user's
discarded first failing API, normal nonadmin packaged context or actual callback.

The CTest failed after0.34s/0.03s; remaining native diagnostic/security/state/
child cases and packaging were not reached. See the
[exact receipt](evidence/40849698888.json) and unmodified
[x64](evidence/40849698888-x64-LastTest.log) /
[ARM64](evidence/40849698888-arm64-LastTest.log) logs. Local portable C++ and
PowerShell17+7+9 cases passed, but do not cover these Win32 calls.
Stopped for parent review before a targeted fixed-size-query repair, any retry
or user update. Version0.2.1 remains not ready for host action.

**Reviewed TokenElevation-only repair:** that information class now uses a real
`TOKEN_ELEVATION` buffer of its documented size, rather than a generic zero
probe. Failed BOOL queries immediately retain the actual Win32 DWORD and
HRESULT with `value-query`/`win32` context. Invalid handles therefore no longer
manufacture E_ACCESSDENIED from the zero-probe error; their mapping follows the
same fixed-buffer direct API reference. Successful results must return exactly
`sizeof(TOKEN_ELEVATION)`; wrong sizes remain fail-closed E_ACCESSDENIED with
`token-fixed-size`/`validation` context and no fabricated API error.

The [documented DWORD member](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_elevation)
defines zero versus any nonzero value, not just0/1. Its representation is copied
unchanged. The sole production caller, `Current(false)`, still requires
`TokenIsElevated == 0`; no caller privilege, impersonation, architecture, identity
or ACL check changes. Every other `TokenData` class retains its old path.
Regression coverage now includes direct fixed-buffer invalid-handle comparison,
valid-token typed reference, exact-size rejection/preservation, and19 actual
PowerShell preparation/report cases distinguishing fixed-query API failure from
size validation. Portable checks passed; native completion is still required
before any user handoff. No new library or package capability is introduced.

**Fixed-query validation advanced, then STOP:** fresh manual
[40850726709](https://github.com/miyako/4d-pdf-printer/actions/runs/40850726709)
tested source `b464d37119c899887b8d9f944353ee2298268f65`. All five native
targets compiled/linked on both architectures. The fixed-buffer invalid-token
reference/error mapping, exact-size/representation cases and seven valid-token
direct/helper comparisons passed, including TokenElevation. Synthetic diagnostic
and retained-state compatibility cases also passed, followed by the first
controlled fake-callback child acknowledgment/observed exit0/replay stages.
These remain controlled-unit evidence, not actual package/callback proof.

The next first failure is **an assertion** at Tests.cpp:571, case
`files.unprotected-directory-rejected` (case line566), on both architectures:
stage `prepare.existing`, API `validation`, object `existing-job`, origin
`validation`, observed reason `owner`, no API code. The fixture is created by
`CreateDirectoryW(..., nullptr)`; the assertion expects `dacl-protected`.
Production inspection checks owner before protected-DACL, so the actual fixture
was rejected earlier. No raw owner was collected, no specific owner identity is
inferred, and this is not evidence about the user's package owner or a failed
production deny check. The assertion/fixture/production guards were not changed.

The suite failed after0.70s/0.34s; the missing-observer child branch and unsigned
packaging were not reached. [Exact receipt](evidence/40850726709.json), unmodified
[x64](evidence/40850726709-x64-LastTest.log) /
[ARM64](evidence/40850726709-arm64-LastTest.log) logs retain the outcome.
Local PowerShell19+7+9 and portable C++ checks passed. Stopped at this new failure
without repair/retry or any host action. Version0.2.1 is still **not ready for
user update**; the actual user's first failing operation and PSA gate remain
unproven.

**Deterministic fixture repair; native pass but workflow STOP:** source
`7718cb726c2a2f2c5816d72528a0a9422abd521d` changes only the synthetic unit fixture.
It clones the existing explicitly owned descriptor, clears only the clone's
`SE_DACL_PROTECTED` bit with the
[documented setter](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-setsecuritydescriptorcontrol),
and pins/readbacks the created directory before unchanged production inspection.
No owner-assignment privilege, production guard change or live path is used.
The exact `dacl-protected` assertion is retained.

Fresh manual [40851551623](https://github.com/miyako/4d-pdf-printer/actions/runs/40851551623)
verified `owner_matches_current_user=1 dacl_protected=0` on **both architectures**.
All native compilation, complete controlled CTests (3.35s each, including
missing-observer bounded exit1/no-ack) and unsigned MSIX/PE/manifest/PRI packaging
passed. Original [x64](evidence/40851551623-x64.json) /
[ARM64](evidence/40851551623-arm64.json) clean-source build receipts and raw
[x64](evidence/40851551623-x64-LastTest.log) /
[ARM64](evidence/40851551623-arm64-LastTest.log) CTest logs are preserved.

The **overall workflow failed** in the next hosted PowerShell guard step:
Check-ActivationPrepare.ps1:159 throws `Raw private Unicode stderr was lost.`
on both runners. The actual production-body test strictly decodes controlled
child stderr as UTF8 and compares the fixed Japanese test string; actual bytes
are not in the logs. The child uses `Console.Error.Write` without explicit
encoding, but encoding-versus-capture cause is not yet proven. Do not equate
this with a user preparation failure or discard the assertion. Local macOS
PowerShell19+7+9 passed; the complete Windows body suite and later hosted steps
did not. [Overall failure receipt](evidence/40851551623.json).
Stopped without script repair/retry or user action. Native pass alone is not
full validation or a user-update/actual-callback gate pass.

**Separately reviewed test-only UTF8 repair and overall pass:** the controlled
producer now emits explicit UTF8-no-BOM bytes through `OpenStandardError.Write`.
The assertion compares captured bytes to an independent fixed nine-byte
reference, without transcoding, trimming or normalizing production capture.
Only this known synthetic fixture may produce bounded32-byte hex diagnostics
on mismatch. Invalid-UTF8/flood/timeout/collision cases and native sources remain
unchanged. The existing guard entry point now reuses the retained7+9 harness
checks, so the existing ActivationProbe matrix verifies all of them without
workflow changes or renderer/comparator work.

Fresh [40852574080](https://github.com/miyako/4d-pdf-printer/actions/runs/40852574080)
at `6ed19cbd71758e4d26a5ef6dc3dd4529475664e9` passed **overall** on x64
job123375458729 and native ARM64 job123375459252: complete native CTests
3.30s/3.35s, unsigned MSIX/PE/manifest/PRI, script/consent guards,19 actual
preparation/report cases and retained7+9 cases on each runner. The fixture
readback remains owner-matches1/protected0, and exact DACL rejection plus
missing-observer/no-ack cases passed. Original
[x64](evidence/40852574080-x64.json) /
[ARM64](evidence/40852574080-arm64.json) build receipts and raw
[x64](evidence/40852574080-x64-LastTest.log) /
[ARM64](evidence/40852574080-arm64-LastTest.log) CTest logs are preserved with the
[overall receipt](evidence/40852574080.json). These are not signed host binaries.

After production repair `b464d37`, only test sources changed; the latest delta
from the native-pass checkpoint is three PowerShell test scripts. The original
Unicode failure's bytes were not retained, so its encoding cause is still not
asserted. CI success does not prove the user's first failing operation, real
medium packaged `Current()`, alias preparation or actual activation. No user
build/update/preparation/printing followed; all old state and failures remain.

**Conditional user handoff, not authorization:** only after native validation and
parent review, preserve the complete old native output directory (including
signed0.2.0 MSIX, build receipt and CTest LastTest.log) and every existing proof/
preparation root. Prefer a fresh detached source worktree at the approved SHA
and build there, leaving the old worktree/output untouched; alternatively use
an explicitly new archive before any build can overwrite owned output files.
Use existing VS2022/v143/SDK26100 and existing signing certificate/trust. No
renderer build. Only an ordinary current-user0.2.0->0.2.1 update is contemplated;
no same-version overwrite, uninstall, new identity or diagnostic-family change.
Then, only after separate consent, one new-root preparation-only call using the
new local receipt/wrapper may expose the first failing API/guard. No printing or
activation pass follows from compilation or preparation.

## Version0.2.2: bounded family-SID postcondition diagnostics

`Context` owns identity bytes and family strings; it has no derived package-SID
pointer. `Security` derives a separate pointer, copies it before `FreeSid`, then
uses the owned bytes for SID-to-string conversion and ACL construction. The
[derivation contract](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deriveappcontainersidfromappcontainername)
requires `FreeSid`, matching this path. An ordinary full-trust token's empty
AppContainer SID is not the package-family SID input. No missing-token, dangling
pointer, incorrect deallocator or stale role has been established.

The ctor role audit finds one `CopySid`/`IsValidSid` site immediately after
derivation under `package-sid`. User and package SID string conversions each
set their own role and `ConvertSidToStringSidW` API; SYSTEM SID construction is
later, outside this ctor. No role changes or reordered security checks are
needed. Old tests already call the real derivation API with a dummy family;
they did not cover the actual family input or a normal packaged caller.

The new per-invocation `error_context.derivation` snapshot contains only exact
unsigned HRESULT, `outputPresent`, `validityChecked` and `valid`. Nonnegative
results other than S_OK are retained, not normalized. Null input still fails
E_ACCESSDENIED but is labeled `validation` / `sid-missing`, without claiming
`IsValidSid` was called. Nonnull invalid input still fails E_ACCESSDENIED at
`IsValidSid` / `sid-validity`. API failures retain their original HRESULT;
no failure is bypassed, principal substituted, profile created or ACL relaxed.
The wrapperVersion4 summary strictly allowlists these numeric/Boolean fields,
separately from the current per-call role/API. Raw pointers, SID contents and
family/path strings are not reported.

Regression additions use the actual development family and real process
identity in the production `Security` ctor with returned/copied/moved/temporary
Contexts. An independent direct API reference records exact HRESULT and guarded
postconditions, frees its allocation, and returns owned SID bytes. Tests compare
validity/EqualSid and SID-to-string conversion after ctor/helper return and
Context/Security destruction, without publishing contents. Fixed synthetic
null, nonnull-invalid and S_FALSE observations retain failure predicates.
The25 actual PowerShell body cases cover those distinctions, API HRESULTs,
later user-SID conversion role preservation and unsafe metadata rejection.
These are not actual packaged `Current()` or PSA proof.

Manifest, builder and wrapper require same-family/publisher **0.2.2.0**.
Package-full-name binding, expiry, admission and record rules are unchanged:
old state is never rewritten or pruned, expired valid records remain/count8,
fresh records remain busy, and old package-hash callbacks remain rejected.
Any future update must preserve the user's signed0.2.1 MSIX, build receipt,
CTest log, all earlier versions/three private attempt roots, certificate/trust
and healthy diagnostic package/queues. No same-version overwrite or uninstall.

## Supported primitives, not an assumed callback result

The [PSA v4 guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/print-support-app-v4-design-guide)
defines `windows.printSupportVirtualPrinterWorkflow` and its
`VirtualPrinterDataAvailable` background event. Omitting `OutputFileTypes`
retains the no-file-UI endpoint. This does **not** make DOCINFO an application
destination carrier; the existing actual diagnostic observations still apply.

[FullTrustProcessLauncher](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher?view=winrt-26100)
supports a same-package full-trust component with `runFullTrust` and a
[`windows.fullTrustProcess` extension](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop-extension).
[`LaunchFullTrustProcessForCurrentAppWithArgumentsAsync`](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslauncher.launchfulltrustprocessforcurrentappwithargumentsasync?view=winrt-26100)
accepts runtime arguments on Desktop22000+, FullTrustAppContract v2. These are
not a manifest ParameterGroup ID. Its
[result](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.fulltrustprocesslaunchresult?view=winrt-26100)
provides launch status/extended HRESULT, **not** a PID or process handle.
The documented threading model is Both. These contracts justify testing this
bridge, not claiming this particular PSA host/context will accept it.

The fixed target is `DesktopPdfProbeHost.exe`; only the actual callback calls
the launcher, with exactly `--job <64 lowercase hex characters>`. The desktop
control never calls FullTrustProcessLauncher. No arbitrary executable, path,
command, foreground fallback, AppContainer CreateProcess, environment carrier,
custom entitlement, COM/profile change or resident coordinator is introduced.
The old desktop `.get()`0x80010117 and StartDoc3003 observations at `d58bb2f`
remain historical, not results from this callback.

## Minimal ownership and lifetime contract

The current-user package's
[`ApplicationData.LocalCacheFolder`](https://learn.microsoft.com/en-us/uwp/api/windows.storage.applicationdata.localcachefolder?view=winrt-26100)
contains `ActivationProbe-v1/<random job ID>`. This is package-local data, not a
global next-job setting. The full-trust preparation creates a protected DACL
for exactly the current user, SYSTEM and derived package SID, with an inherited
low-integrity label for AppContainer access. Native opens reject reparse points,
wrong owners/ACLs and unexpected types; pinned directory handles deny deletion.
Same-package sharing follows the documented
[IPC/package boundary](https://learn.microsoft.com/en-us/windows/uwp/communication/interprocess-communication).
It is **not** protection against hostile full-trust software running as this same
user. The random title is correlation, not sole authentication.

The immutable request binds user hash, Windows session, package-full-name hash,
endpoint and30s expiry. Preparation is serialized; another unexpired request is
busy, and eight retained job directories exhaust this deliberately small probe.
No automatic pruning or retry. Files are at most16KiB; exclusive creation of a
pending file and rename without replacement make claims/records one-time.
The actual callback binds the complete `PSAPROBE1:<ID>` title, real workflow
SessionId hash, AppContainer context and request hash before launch. The workflow
SessionId is not equated to a spooler job number.

The one-shot host verifies those bindings, freshness and no closed marker, then
claims `host-entry.json` once, with PID/creation time. It waits at most3s for the
observer to hold and validate that exact process: expected installed EXE,
package full name, actual user/session and PID creation time. The observer
publishes `observing.json`; only then may the host atomically acknowledge and
exit0. The callback validates acknowledgment, records raw launch results and
CompleteJob(Failed) return separately. The observer requires that terminal
callback record, acknowledgment and actual process exit0, plus no matching
same-package/session host remaining **at that observation**, before gate pass.

Callback polling is bounded by12s and request expiry; known-host exit waits5s;
desktop wrapper watchdog45s. Closed/expired/duplicate requests cannot produce
a valid late acknowledgment. Cancellation requests the system async operation's
Cancel, which **does not guarantee no later process launch**. Failure evidence
retains this uncertainty. The observer may terminate/reap only an identity-
validated process handle; an unseen process cannot be declared reaped. A late
host should reject expired/closed state and exit; actual Windows scheduling or
an indefinitely blocked filesystem/API call is not a hard termination guarantee.
No OS/spooler crash-immunity claim.

## Bounded local evidence

Private records contain the correlation ID but no document bytes/destinations;
never upload them. Sixteen fixed background-entry log slots make a missing
prepared-job record distinguishable from a logged entry failure. Per-job records
capture stage/API HRESULT, package/user/session hashes, process architecture/PID,
apartment, launch result/extended HRESULT, workflow hash, acknowledgment,
creation time, exit code and cleanup/late-launch uncertainty. The source/build
receipt carries exact source SHA, dirty state, own-code hashes and package/PRI
hashes. Native observer output copies only explicitly allowlisted failure fields.
No automatic upload or font/content collection. No worker stdout is changed.

Normal errors stay visible; private stderr/debug messages are separate from
single JSON stdout. `result.json` is create-new UTF8 without BOM; collisions
never overwrite and a report failure does not replace the primary exception.
No source OXPS is drained or saved in this activation-only probe.

## Single native build/unit batch

The manual-only `desktop-windows.yml` mode `ActivationProbe` compiles x64 and
native ARM64, runs native controlled-process/file units, validates actual
PE/manifest/PRI/MSIX wiring, and uploads text/JSON receipts only. No signing,
trust, installation, GDI submission, PSA callback, dependencies or renderer build.
SDK26100 headers and `windowsapp`/Ole32 cover WinRT; Advapi32 covers ACL/token
APIs, Userenv covers `DeriveAppContainerSidFromAppContainerName`, Shell32 covers
`CommandLineToArgvW`; only the synthetic submitter links Gdi32/Winspool.
RPC targets/MIDL are bypassed. Native units use a separately controlled copy of
the unit EXE in a kill-on-close Job Object, **not** the system full-trust launcher.

Portable tests execute strict token/expiry cases, the actual PowerShell JSON
writer's Unicode/collision/bound paths, consent/platform guards and manifest
checks. They do not execute the consented Windows wrapper body. Native units
exercise protected file/Unicode/replay/expiry/session/request mismatches,
unprotected ACL rejection, child acknowledgment+exit0, and missing-observer
bounded exit1/no acknowledgment. They do not validate AppContainer broker ACL
inheritance or real package activation; those are precisely the desktop gate.

## Parent-review handoff: one preparation-only ARM64 desktop observation

**Not authorization to execute. Parent supplies commands only after review.**
Use a normal signed-in, non-elevated native PowerShell7 desktop on the existing
Windows11 ARM64 machine, VS2022/v143/SDK26100. No renderer/Python/Poppler work.

1. Use a **new source worktree** at tested
   `6ed19cbd71758e4d26a5ef6dc3dd4529475664e9`, not the original worktree/output.
   Preserve the old signed0.2.0 MSIX, build receipt, CTest LastTest.log, all
   private attempt roots and package state. A separately named verified archive
   before any overwrite is an alternative, not a cleanup/resume mechanism.
   Record clean source state and
   `Get-AppxPackage` identity/version/status for BOTH `Miyako.PsaFeasibility` and
   `Miyako.DesktopPdfPrototype`, plus their existing queues.
   Parent command blocks must enclose all guards/actions in `& { ... }`.
   Stop if the probe package is already version0.2.1.0 or newer,
   has another publisher, or existing development state cannot be preserved;
   never solve an update error by uninstalling.
2. Build only `./desktop/build.ps1 -Mode ActivationProbe -Architecture ARM64
   -VisualStudioVersion 2022 -CertificateThumbprint <EXISTING approved cert>`.
   Use the existing same-user `CN=DesktopPdfPrototypeDev` private key/trust;
   create or change neither. Stop if missing. Preserve this new local native
   test log/receipt/signed-MSIX hash separately; CI binary hashes are not the
   user's local-build identity.
3. Explicitly approve and current-user install/update ONLY
   `desktop/out/arm64/DesktopPdfActivationProbe.msix`. Same development identity
   version0.2.1.0 preserves its LocalState/LocalCache on an ordinary update; it
   retains the0.2.0 automation endpoint and alias `4dpdf-probe.exe`. Do not
   uninstall, overwrite the same version, change identity or touch the healthy
   diagnostic family/queues. Verify version/status/family/publisher and
   registration and the development no-file-UI queue; stop if missing. Do not
   launch the ordinary desktop app as a substitute activation test.
4. Choose ONE new absolute local private report directory under the current
   user's protected profile, not any old root and not precreated. After separate
   preparation-write consent, run `desktop/tests/Invoke-ActivationProbe.ps1` with
   `-PrivateOutputRoot <NEW directory> -BuildEvidencePath
   desktop/out/arm64/activation-build-evidence.json -PreparationOnly
   -AllowPackagePreparation`. Do **not** supply `-AllowOneSyntheticPrint`.
   It first verifies installed own-code hashes against that clean local build
   receipt and native architecture, then performs one bounded native preparation.
   No observer, submission, callback/host launch, Save As or PDF is authorized.
5. Preserve results on success **or failure**; no automatic retry. Review safe
   `result.json` native HRESULT/stage/API/object/origin/reason/code and bounded
   private captures. Preparation success still reports gate/PDF false and
   `preparation-only-not-activation`; a blocked result is evidence, not permission
   to relax guards, clear records or elevate the caller. Old expired records
   remain/count toward cap8; fresh or malformed old state may still block.
   Review that result before separately considering any actual PSA print gate.

**Rollback boundary:** no automatic uninstall, downgrade, certificate/trust
change or deletion of package-local/attempt records. This is an update of an
existing development family, not a disposable new identity. Preserve evidence
and known-process exit observations; any later rollback requires its own
state-preserving review. The diagnostic family/queues are never cleanup targets.

Historical producer success `b33b741`/40825887654, failed40825481671 and the
NoClobber fix remain unchanged. Actual product size/fidelity/native worker
lifecycle proof and every user-host action remain outside this build gate.
