# Windows continuation checkpoint

## Windows04 result (supersedes "Immediate continuation" below)

Windows04 completed the committed scoped Build for source
`dce863267cfd68712a6eeacbccc4539688188e62`, tree
`b9f07f042d88e0a3f94600c0404ef55825952944`: receipt status
**`passed-scoped-build-only`**, phase `closed`, commands 01-19 closed exit 0.
Root `C:\Users\miyako\Desktop\version005-native-scoped-build-04`; receipt
`ScopedBuildResults/receipt.json` SHA256
`31be545deff9a89eb366b73737670ece036f4519d6980b306fb51c757ccc3883`.
Run from an ordinary user PowerShell 7.6.9 window, started
`2026-11-04T14:19:55Z`; helper (command 17) `14:20:01Z`-`14:21:45Z`.

Before launch, from the Windows machine: source closure 22/22 byte-matched
(19 files, 2 action sources, `renderer/.gitattributes`); bundle
`0658db64...36d9` verified with exactly three refs; all 12 Windows03 receipt
tool hashes unchanged; real Git config held only the standard LFS definitions.

The runner differs from Windows03's (`d5443496...ff60d`) **only** in the
exact-root literal on its root guard line (`-03` -> `-04`); runner SHA256
`ee521f4848473bf0a402740d760c50c0e22d2c737468c2b949c1ebdd740182ce`.
`handoff.json`, `source.bundle` and `source-sha256.txt` are byte-identical.

Results: full `tests/Source.py` hook 14/14 OK; source before/after equal and
clean; Contract no-argument passed; Native `--isolated-journal` passed with
retained test root (stderr HRESULTs are the mocked fault/deadline fixtures).

| Output (ARM64) | Bytes | SHA256 |
|---|---|---|
| `IntegrationApp.exe` | 138752 | `7b351dc47c3b487fc721ed47c539ebb7146b1b14e1cae8c71c61dc8ccdb1ec29` |
| `IntegrationTask.dll` | 536064 | `03c4d0448636b2a5ff7afa9d82074c4edf51d07da78bb1841293a5717a65e725` |
| `IntegrationHost.exe` | 421376 | `39a9f7d298d14a1f3af6806a283ed4bfec879d020f4eeb87cdcfd925c7f2ce47` |
| `IntegrationClient.exe` | 461824 | `6aabcef39aca88c130d41c6e5e0b77a15757d531aee8d4e4cd00107fb69083c5` |
| `IntegrationContract.exe` | 150016 | `9e9621c8125381687e8d7f4db6710728cfafce04c564090061879ea687b32966` |
| `IntegrationNative.exe` | 759296 | `0c825d61f42f118b90a0b77582b0e48b3a93de60b1c22b248973b05787915f48` |

**Windows03 stays incomplete, not failed or passed.** Its receipt is unchanged
(`8b1546d5...85ad`), command 17 remains `started` with no exit code, no owning
process survives, and the last artifact is `Task.obj` at `13:52:37Z` with no
`IntegrationTask.dll`. Its folder is preserved untouched.

This is build and isolated-check evidence only: no full-suite/CTest run, no
package, signing, installation, update or print result. Installed 004 is
unchanged. Next gates (each separately approved): Package/SignCopy from the
Windows04 build directory, measured installed-004 facts and update to 005,
then one print.

## Source checkpoint

Use **`dce863267cfd68712a6eeacbccc4539688188e62`**, tree
`b9f07f042d88e0a3f94600c0404ef55825952944`, parent
`1e32dce435d2644a50dbaf80cdb80393c3e758c9`.
This is the latest committed, positively **offline-validated source** revision,
not a completed Windows build or usable-printer acceptance. The handoff commit
adds only this document; it does not replace that source identity.

All 19 reviewed source-file hashes matched the committed checkpoint when this
handoff was written. Its full `desktop-pdf/tests/Source.py` hook passed all 14
tests in offline validation04. Validation02 passed timestamp-reader cases,
scoped/full-suite receipt models, missing-005-facts rejection, selected
source/parser checks and synthetic explicit-v3 review wiring. These are not
WinRT, CompleteJob, packaging or printing evidence.

## Strongest earlier Windows positives

| Checkpoint | Positive evidence | Limit |
|---|---|---|
| Isolated native02 | Native ARM64 compilation and selected `IntegrationNative --isolated-journal <fresh private root>` completed successfully; receipt `db72438e12ecc320e93ac98e68de8741b7bf4b52f698c6879e841f297c677e8d` | Uncommitted 14-file overlay on base `1e32dce435d2644a50dbaf80cdb80393c3e758c9`, base tree `8bebed1c8048e4ba0e42e5462803fa0bd5f0315e`; not the complete 005 revision |
| Contract run | One Windows `IntegrationContract` no-argument invocation closed exit 0; receipt `92bd4f439626d9434b0789bea2f542133b264d27d9604e824ec75e6d322793b9` | Portable helper/models only, not Task/WinRT/backend/PSA acceptance |

The native02 overlay's 12 non-documentation/source-hook files match 005 exactly;
its `README.md` and `tests/Source.py` differ. This establishes continuity of the
tested Task/observation/native-isolation code, **not** native validation of 005's
new version/build/profile/receipt-reader changes. Native02 output facts:
`IntegrationTask.dll` 536064 bytes, SHA256
`e9440e18dd316756471e38f0b6adf6c8c2a39697d4a7eae95fe830d03520a023`;
`IntegrationNative.exe` 759296 bytes, SHA256
`7b32bf3f1f40b7546c15dd49567c8a409100f8a42998a741ac5170dcd1766f92`.
Do not reuse those binaries as 005 payloads.

## Windows03 is incomplete, not failed or passed

The returned `ScopedBuildResults03/receipt.json` snapshot has SHA256
`8b1546d5fdfd76dba32a31b0282181fd00f330aa174c09745ded866bb68585ad`.
Commands 01-16 closed exit 0. Command 17, the committed scoped Build helper,
started at `2026-11-04T13:51:54.9504738Z` but has **no closed status, exit code or
final channel facts**. The snapshot reports phase
`actual-committed-scoped-Build-once`; source-after is null/unmeasured.
Partial output shows configure/generate and App compilation, then Task.cpp.
This proves neither completion nor failure. A later fresh-path sentinel message
does not establish the original process outcome.

The original Windows process may still own:
`C:\Users\miyako\Desktop\version005-native-scoped-build-03`.
Do not blindly rerun, delete/reset its source/output/testroot, kill processes,
or copy potentially live results as if final. Preserve this attempt and resolve
its original window/process/receipt state on Windows before deciding further work.

## Immediate continuation on Windows

Continue with a native Windows agent and the existing setup, not another chain
of Mac-generated runner transfers. First determine whether the original attempt
is active or closed, without launching another build/test. If closed, preserve
the final receipt/logs and distinguish helper outcome, compilation, selected
tests, wrapper postchecks and any additional diagnostic failure.

For any separately authorized continuation, retain genuine clean Git source,
exact commit/tree/byte pins and required historical objects. Keep outputs
outside the checkout. Preserve LF checkout bytes (`core.autocrlf=false`,
`core.eol=lf` before first checkout), the reviewed process-local Git isolation,
environment/ancestor/props/response-file guards and fresh retained private roots.
Do not reset/stash changes to manufacture cleanliness.

The existing scoped helper is `desktop-pdf/build.ps1`, Stage **Build**,
architecture ARM64, VS2022, validation profile **isolated-journal-v3**.
It builds the four payloads plus Contract/Native and required compile dependencies;
only Contract no-argument and Native explicit isolated-journal checks are selected.
Do not substitute full-suite/default Native/CTest/process fixtures or launch
App/Host/Client payloads. The wrapper cannot interpose a pre-build generated-graph
audit: collected generated project text is post-return evidence only. Installed
standard MSBuild/SDK imports remain a trusted boundary, not total attestation.
Blocking operations have no physical hard deadline; no automatic kill/cleanup.

Measured existing Python:
`C:\Users\miyako\Desktop\test\4d-pdf-renderer\renderer\build-python-arm64\Scripts\python.exe`,
ARM64 3.13.17, SHA256
`2883a34aa9acabf20d610b2667d713cdc252a320ba179215df4a9d3a3e2e61ef`.
Use the recorded real interpreter, not a Store alias or arbitrary PATH fallback.
Existing VS2022 Professional `17.14.36804.6`, v143 `14.44.35207`,
CMake `3.31.6-msvc6`, SDK `10.0.26100.0`; recheck actual facts rather than
assuming historical availability. No installs/downloads/configuration repairs.

Required Git objects: dependency `8b34e5669bec822bb82768fd61b0483bbdd74567`,
proof `12cc27915d08fcf29c5b1f00c422e0a16cc23e1b`, and baselines
`245373012067c928781e5c85b7aafddd191b800d`,
`66740903d715cbd9a6572486eefb83c4fe7b9375`,
`5f2382888fe8b892a83b07c832654818f7ec2fdd`.
The existing source bundle contains exactly source/dependency/proof tips;
bundle SHA256 `0658db64a8e502a88531095b9c3519b2e36d0ab1f0bee5aa1792f6898d9036d9`.
It does not contain this later documentation-only commit.

## Evidence locations and product limits

Owner evidence prefix on the Mac:
`/Users/miyako/.copilot/session-state/54903834-18e3-470e-88ee-a23667ef525e/files/`.
Relevant artifacts are `version-005-offline-validation-02/`,
`version-005-offline-validation-04/`, `version-005-local-commit-01/`,
`native-journal-isolated-preparation-02/source-sha256.txt` and
`version005-native-scoped-build-preparation-05/REVIEW.txt`.
Historical returned Windows evidence is archived under
`/Users/miyako/Documents/4d-pdf-printer-archive/2026-11-04-desktop-cleanup/`,
including `CompileOnlyResults`, `ContractRunResults`, `IsolatedNativeResults02`;
`archive-manifest.json` maps former Desktop paths. Do not repair/rerun old scripts.

Installed 004 is unchanged. Its cancellation/SystemPolicy cause remains unknown.
V3 observations are race-honest/non-atomic boundary samples, not causal proof;
isolated native tests do not exercise real PSA/CompleteJob/SystemPolicy.
There is **no accepted 005 package, signing, installation or print result**.
No silent-destination/Save-As lifecycle acceptance is upgraded by this handoff.
Package/sign/install/update/one-print actions require separate scope/approval.
