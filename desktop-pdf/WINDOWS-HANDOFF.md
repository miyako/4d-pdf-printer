# Windows continuation checkpoint

## 008 (0.1.0.8): native completion.json, 6/6 prints drained

After two failed build gates (attempts 1 and 2 below), attempt 3 at
`dfc6d9e` passed build, package+sign, update 0.1.0.7 -> 0.1.0.8 and the
6-print silent series. All six journals are `drained` with `finish` published.

### Attempt 1: receipt reserve, `c687aab`

Source `c687aabe6c7b06a418758cf73ade438fa6362865`: post-complete teardown
drains until cancel + 190 ms (`TeardownDrainMs = CancelMs - ReceiptReserveMs`,
reserve 60 ms) so the 007 runs 02/03 late seal (cancel + 62..125 ms) still
publishes `finish`. Root `C:\Users\miyako\Desktop\version008-build-01`
(retained, not reused).

- **Build** (`RunBuild008.ps1` `495cc0a7...`): `build008.json` (`c6242bc2...`)
  **`failed`**, `build.ps1` exit 1. Contract passed; the isolated-journal-v3
  Native check failed `late-sealed teardown still writes the terminal receipt`
  (stderr ends `journal terminal unavailable/deadline`, `journal drain
  incomplete`). Stopped: nothing packaged, signed or installed; 0.1.0.7 stays
  installed.
- **Cause:** the new fixture used a real `Sleep(150)` after the cancel. On the
  machine right after the build the sleep overshot past the whole cancel
  budget, so neither `finish` nor the receipt could be issued. A test timing
  defect, not device evidence. (A first backdated rewrite also failed: it
  backdated only the applied tick, violating `entry.tick <= applied tick`.)
- **Fix:** the fixture now backdates both the cancel entry stamp and the applied
  tick by 130 ms, then seals immediately: deterministic seal past the old
  125 ms window with the 120 ms receipt budget intact. Engine unchanged. Dev
  validation: Native 5/5 consecutive passes, contract all modes, Python Source,
  Journal, CompleteJob OK.

### Attempt 2: deterministic fixture, `283f84d`

Source `283f84dc53087090cb7e3780843337546daf103f` (tree `c6b88446...`), root
`C:\Users\miyako\Desktop\version008-build-02` (retained, not reused); the
cycle was approved as a whole, stop-at-first-failure.

- **Build** (`RunBuild008.ps1`, only the commit substituted): `build008.json`
  **`failed`**. The late-seal fixture passed. The 007 held-teardown fixture
  failed: `held teardown drain still leaves budget for the terminal receipt`
  (stderr `journal terminal unavailable/deadline`). Records took
  10.3 ms each, twice the dev rate. With the writer held, the drain ran to
  cancel + 190 ms and the receipt did not finish in the remaining 60 ms; no
  terminal file exists in the retained case. Stopped: nothing packaged,
  signed or installed; 0.1.0.7 stays installed.
- **Conclusion:** the 250 ms cancel budget cannot cover a 62–125 ms seal
  delay plus the `finish` write plus the receipt under load. Moving the split
  point only moves the loss. The user chose the root-cause fix: remove the
  seal delay.

### Attempt 3: native completion.json, `dfc6d9e`

- `completion.json` is written synchronously by `PublishNative` (`Task.cpp`)
  through the journal's Win32 `JournalIo` Create/Write/Rename, with the same
  bytes (BOM + compact JSON, checked equal to the StorageFile output in dev),
  pending-then-rename and fail-if-exists. `Finish` now seals about 1 ms after
  CompleteJob returns, instead of after the 62–125 ms StorageFile publish.
- `ActivationDiagnostics.h` and `tests/ActivationJournal.h` are restored
  byte-identical to 007 (125/125 split); the attempt 1/2 tests are removed.
  A new native fixture covers the `completion.json` bytes, read-back and
  collision.
- Dev measurement (isolated test root): native publish 0.6–1.8 ms; StorageFile
  publish 105–502 ms. Dev validation: Native 8/8 consecutive passes, contract
  all modes, Python Source 14, Journal 15, CompleteJob 8 OK.

Source `dfc6d9eed7071b5bc24acb0d2ac47eab547dd776` (tree
`566da32b3cfa801f31c36e9ee233cc3bad04567c`), root
`C:\Users\miyako\Desktop\version008-build-03`. The whole cycle was approved at
once; each gate ran once, stop-at-first-failure, no retry or kill. Scripts are
the build-02 / 007 scripts with commit, root, versions and hashes substituted.

- **Build** (`RunBuild008.ps1` `0ca263ce...e6ae`): `build008.json`
  (`cfa5b0a1...f6b7`) **`passed-scoped-build-only`**, including the
  isolated-journal-v3 Native check. Payload SHA256:
  IntegrationApp.exe `2a6e3408...c47f`, IntegrationTask.dll `c72fdbdc...388c`,
  IntegrationHost.exe `b6eabe0c...1df4`, IntegrationClient.exe
  `969a6ac6...8c2f`.
- **Package + SignCopy** (`RunPackage008.ps1` `5511d40f...d469`):
  `package008.json` (`6a585e75...cb3a`) **`packaged-and-signed-only`**.
  Unsigned 621455 bytes
  `d5bbfacc02a40aeb3249afd7104a1c0e467761423b5d2586ebf06ea6586aece0`; signed
  624553 bytes
  `4b75a845ea21e8b7e8ce9df95cf393a8fc7cfdcdfc8fbbf74976e242a2d601bb`,
  signature Valid, thumbprint `0C2B96E9...37BB` (expires 2026-11-28).
- **Update 007 -> 008** (`RunUpdate008.ps1` `737087ec...fb13`, one
  `Add-AppxPackage` 01:10:04Z-01:10:07Z): `Update/result.json`
  (`bbd5cedf...856b`) **`verified-update-only`**. 633/633 package data files
  unchanged, queues unchanged, installed payload matches the build.
- **Silent print series** (`RunSilentPrintRun008.ps1` `69c161a5...5b99`,
  `RunSilentPrintSeries008.ps1` `d51a2c84...2eca`):
  `SilentPrintSeries/series.json` (`1e5c5dfb...67a5`). All 6
  `CAPTURED-FOR-REVIEW-NOT-PRODUCT-SUCCESS`, completion `succeeded`, PDFs
  17670 bytes SHA256 `45572825...16c1` (identical to 005/006/007). Queues
  empty.

  | run | finalize_reason | drain | cancellation_drain | accepted | last pub = finish | rejected after seal | flush ms | return-to-seal ms | seal minus cancel ms |
  |---|---|---|---|---|---|---|---|---|---|
  | 01 | post-complete-teardown | drained | continued | 21 | 20 | 0 | 16 | 47 | 0 |
  | 02 | post-complete-teardown | drained | continued | 21 | 20 | 0 | 0 | 32 | 32 |
  | 03 | normal | drained | continued | 20 | 19 | 1 | 16 | 15 | 0 |
  | 04 | normal | drained | continued | 20 | 19 | 1 | 16 | 16 | 0 |
  | 05 | normal | drained | none | 20 | 19 | 0 | 16 | 0 | (no cancel) |
  | 06 | normal | drained | continued | 20 | 19 | 1 | 16 | 16 | -15 |

  All: flush 18/18 `flushed`, `CompleteJob(Succeeded)` returned without
  error, framework cancel reason 7 where present, terminal receipt v5
  present with no in-flight I/O and no failure. Every
  `completion.json` is BOM + compact JSON with `outcome` `succeeded`, and the
  client read it (the series completion column). Tick resolution is about
  15.6 ms.

  **Interpretation.** Publishing `completion.json` natively removes the
  62–125 ms StorageFile delay between CompleteJob and `Finish`. The seal now
  lands 0–47 ms after CompleteJob returns, at or before the teardown cancel
  (007: 62–125 ms after it). In 4/6 runs the seal won the race against the
  cancel (`normal`). The late cancel is rejected after seal (03/04/06) or
  never arrives (05), so `finish` is seq 19 there. In 2/6 the cancel came
  first and the teardown drain published every record, including `finish`
  (seq 20). There are no `deadline` outcomes and no lost records, against 2/6
  in 007. 0.1.0.8 is installed.

## 007 (0.1.0.7) native journal records: build, package, update, print series

Source `4b51b4a9496af4bd5fb82a9d5d8ff7e47e167f51` (tree
`28a7d5e87cc571fbe86be1fc449678901c544095`): native Win32 per-record journal
writes, post-complete teardown keeps draining (bounded to teardown + 125 ms),
terminal v5 `cancellation_drain`; see README 0.1.0.7. Root
`C:\Users\miyako\Desktop\version007-build-01`, the whole cycle approved at
once, each gate run once, stop-at-first-failure, no retry/kill. Same route and
scripts as 006 (text-substituted commit/branch/root/versions/hashes).

- **Build** (`RunBuild007.ps1` `3cec0d64...ecbe2`): `build007.json`
  (`8a8f2608...7e143fc`) **`passed-scoped-build-only`**. Payload SHA256:
  IntegrationApp.exe `6b620bbb...2a61`, IntegrationTask.dll `bf4460b2...c244`,
  IntegrationHost.exe `c8a2c6b8...30af`, IntegrationClient.exe
  `53b3bffa...cb05`.
- **Package + SignCopy** (`RunPackage007.ps1` `f78e5636...7e71`):
  `package007.json` (`dd02d357...c5`) **`packaged-and-signed-only`**.
  Unsigned 620192 bytes `c4df50a161328be9ffb057cf74fbcb82b630b9e9fb34405706df52e8520f3e53`;
  signed 623294 bytes
  `3551759aaa3a31c3a97e98a556ce2b7f1817efa58b36c82504915e37a7c23a3d`,
  signature Valid, thumbprint `0C2B96E9...37BB` (expires 2026-11-28).
- **Update 006 -> 007** (`RunUpdate007.ps1` `d6db77fa...a10`, one
  `Add-AppxPackage` 22:26:41Z-22:26:44Z): `Update/result.json`
  (`3a3a6721...2ad6`) **`verified-update-only`**; 449/449 package data files
  unchanged, queues unchanged, installed payload matches the build.
- **Silent print series** (`RunSilentPrintRun007.ps1` `0559ee88...bcd4e`,
  `RunSilentPrintSeries007.ps1` `3a6a826d...2de`; v5 `cancellation_drain` and
  cancel-minus-entry columns added): `SilentPrintSeries/series.json`
  (`30df37dd...8077`). All 6 `CAPTURED-FOR-REVIEW-NOT-PRODUCT-SUCCESS`,
  completion `succeeded`, PDFs 17670 bytes SHA256 `45572825...16c1` (identical
  to 005/006), queue empty after each run.

  | run | drain | cancellation_drain | accepted | last pub | flush target/pub | flush ms | cancel-entry ms | cancel-to-seal ms |
  |---|---|---|---|---|---|---|---|---|
  | 01 | drained | continued | 21 | 20 | 18/18 | 16 | 15 | 79 |
  | 02 | deadline | continued | 21 | 19 | 18/18 | 0 | 281 | 125 |
  | 03 | deadline | continued | 21 | 19 | 18/18 | 15 | 47 | 125 |
  | 04 | drained | continued | 21 | 20 | 18/18 | 16 | 16 | 62 |
  | 05 | drained | continued | 21 | 20 | 18/18 | 16 | 0 | 78 |
  | 06 | drained | continued | 21 | 20 | 18/18 | 15 | 16 | 62 |

  All: `finalize_reason` `post-complete-teardown`, framework cancel reason 7,
  flush state `flushed`, `CompleteJob(Succeeded)` returned without error.
  Tick resolution is about 15.6 ms.

  **Interpretation.** Native record writes work in the real background task,
  including after the teardown cancel. The pre-CompleteJob flush now always
  completes (18/18) in 0-16 ms instead of 312-625 ms, and the post-CompleteJob
  `canceled` and `complete-job-returned` records are now published. 4/6 runs
  are fully `drained`. In 2/6 runs only the final `finish` record (seq 20)
  is missing: the data path writes `completion.json` (StorageFile) between
  CompleteJob and `Finish`, which sealed the journal 62-125 ms after the
  cancel; when the seal lands at cancel + 125 ms the teardown drain window
  (anchored at the cancel tick) has already closed, so the decision freezes
  at the seal tick as `deadline` with no I/O in flight. A follow-up would
  anchor the teardown cut-off to the finalize deadline minus a receipt
  reserve (for example cancel + 250 - 60 ms) instead of cancel + 125 ms.

## 006 (0.1.0.6) Windows-only build, package and sign

Source `78b31901045ba5f8e6b5ec83f61e62beb4b4e359` (tree
`e50b81c6dcf58e7cc798deaad7f98b017fa314e3`): pre-CompleteJob journal flush
and `post-complete-teardown` label, see README 0.1.0.6. Root
`C:\Users\miyako\Desktop\version006-build-01`, each gate approved and run
once, no retry. Route: Windows-only (no Mac-prepared handoff/bundle); fresh
`git clone` of `miyako-desktop-pdf-006` from GitHub, detached at the commit,
`GIT_*`/`PYTHON*`/`CMAKE_*`/MSBuild variables removed in the runner.

- **Build** (`RunBuild006.ps1` `b6923978...8e3e`; `build.ps1 -Stage Build`
  profile `isolated-journal-v3`): `build006.json` (`c60abc44...e680`) outcome
  **`passed-scoped-build-only`**; contract and isolated-journal checks exit 0
  with their success markers. Payload SHA256: IntegrationApp.exe
  `32ba776a...9d68`, IntegrationTask.dll `99b69a01...4b9a`,
  IntegrationHost.exe `1ffb7a47...f93d`, IntegrationClient.exe
  `6cd79b56...cb9f`.
- **Package + SignCopy** (`RunPackage006.ps1` `4aae3d14...fa9a`):
  `package006.json` (`beecf2b4...7bc3`) outcome
  **`packaged-and-signed-only`**. Unsigned `Package/DesktopPdfIntegration.msix`
  625020 bytes `642ce865a4902aef44e621260ad40693cc5878c60d3b060b94fbbbc24378422a`;
  signed `SignCopy/DesktopPdfIntegration-signed.msix` 628115 bytes
  `659a5d87d90f5e26468ae8685f6460c9c0698243a92004b8c721154c25511a46`,
  signature Valid, thumbprint `0C2B96E9...37BB` (expires 2026-11-28), no
  timestamp.
- **Update 005 -> 006** (`RunUpdate006.ps1`, 005 `RunUpdate.ps1` with
  versions/root/hashes substituted; one `Add-AppxPackage`,
  21:15:53Z-21:15:58Z): `Update/result.json` outcome
  **`verified-update-only`**; 284/284 package data files unchanged (no
  removed/added/changed), queues unchanged, 0 jobs, 0 package processes,
  installed payload matches the 006 build hashes above.
- **Silent print series** (`RunSilentPrintRun006.ps1` `00401807...773c`,
  `RunSilentPrintSeries006.ps1` `af9a4d33...08a1`; 005 scripts with
  root/version/payload substituted and v4 flush columns added; runs 01-06
  sequential, passive observation, no retry/kill): `SilentPrintSeries/series.json`
  (`17cc06cd...3c11`). All 6 `CAPTURED-FOR-REVIEW-NOT-PRODUCT-SUCCESS`,
  completion `succeeded`, PDFs 17670 bytes, all SHA256 `45572825...16c1`
  (identical to 005), queue empty after each run.

  | run | finalize_reason | drain | accepted | last pub | flush | target/pub | flush ms | cancel-CompleteJob entry ms |
  |---|---|---|---|---|---|---|---|---|
  | 01 | post-complete-teardown | canceled | 21 | 14 | deadline | 18/14 | 515 | 32 |
  | 02 | post-complete-teardown | canceled | 21 | 13 | deadline | 18/13 | 625 | 47 |
  | 03 | post-complete-teardown | canceled | 21 | 16 | deadline | 18/16 | 515 | 31 |
  | 04 | post-complete-teardown | canceled | 21 | 17 | flushed | 18/18 | 312 | 32 |
  | 05 | post-complete-teardown | canceled | 21 | 15 | deadline | 18/16 | 500 | 0 |
  | 06 | post-complete-teardown | canceled | 21 | 18 | flushed | 18/18 | 407 | 0 |

  All: framework cancel reason 7 (SystemPolicy), `CompleteJob(Succeeded)`
  returned without error, ordering `cancel-after-CompleteJob-returned`.

  **Interpretation.** Fix B is confirmed on device: every post-CompleteJob
  SystemPolicy cancel is now labelled `post-complete-teardown`, never
  `canceled`. Fix A is only partial: the journal writer publishes too slowly
  (about 30 ms per record via StorageFile create/write/rename), so the
  pre-CompleteJob backlog of 18 records reached the 500 ms budget in 4/6 runs
  and the flush delayed CompleteJob by 312-625 ms (625 > budget: wait-slice and
  scheduling overshoot). Records after CompleteJob are still always lost
  because the teardown cancel follows within 0-47 ms. Print output is
  unaffected. A future change would need cheaper journal writes (fewer files /
  batching) rather than a longer pre-CompleteJob wait.

## Windows04 package, update and first 005 silent print

All run from an ordinary user PowerShell 7.6.9 window under root
`C:\Users\miyako\Desktop\version005-native-scoped-build-04`, each gate
separately approved, no retry/kill/uninstall/queue change.

- **Package** (`build.ps1 -Stage Package`, profile `isolated-journal-v3`,
  from `ScopedBuildOutput/build`): `Package/DesktopPdfIntegration.msix`
  621699 bytes, SHA256
  `475da76c590a1e2c4fd6f5476df3d7f0596bc296ffe1dcf4a5154e706256ae26`.
- **SignCopy** (cert `CN=DesktopPdfPrototypeDev`, thumbprint
  `0C2B96E9...37BB`, no timestamp): `SignCopy/DesktopPdfIntegration-signed.msix`
  624796 bytes, SHA256
  `e0be3fa39c53862fa98a7a94824792a15fd528bc56cdc403be080d667dba62b2`,
  signature Valid, identity `Miyako.DesktopPdfIntegration` 0.1.0.5 arm64.
- **Update 004 -> 005** (`RunUpdate.ps1` SHA256 `0d47d155...e479`, one
  `Add-AppxPackage`, 14:31:41Z-14:31:43Z): `Update/result.json`
  (`b644185a...ec50`) outcome **`verified-update-only`**; 144/144 package data
  files unchanged, both queues unchanged, 0 jobs, 0 package processes.
  Installed payload matches the Windows04 build hashes below.
- **One silent print** (`RunSilentPrint.ps1` SHA256 `079896b5...c803`,
  14:52:25Z-14:52:37Z): same flow as 004 attempt02 (`IntegrationClient
  register` -> `submitted` -> PrintProbe `fbf24a3c...c8c3` on
  "4D PDF Integration (Silent)"), passive observation only (no
  `IntegrationClient wait`, so no client `cancel.json`). Evidence
  `SilentPrint01/` (`result.json` `a04d580d...03ab`). Registration
  `6b842db3...f1e9`, spooler job 29, job `58ca81d7...7e52`, activation
  `e7245a1e...a182`. Completion `succeeded`, stage
  `host/serialization/worker`, hresult 0, first observed 4.9 s after probe
  start; `silent-005-01.pdf` 17670 bytes `%PDF-1.7`, SHA256
  `45572825...16c1` (= `result.json`). Queue empty and no package process
  afterwards. Outcome recorded as `CAPTURED-FOR-REVIEW-NOT-PRODUCT-SUCCESS`.

**Journal terminal (version 3) reproduces 004 exactly in shape**:
`finalize_reason`/`drain_state` `canceled`, accepted 21, dequeued 18, last
published 16, finish 20, `outstanding_io_at_decision` true, deferral acquired
and release planned but not observed, in-flight sequence 17
`complete-job-before`/`rename-final`, `first_failure` null. Framework
cancellation context `c[0]` raw reason **7 (SystemPolicy)** on a different
thread from the worker.

New v3 `j` field (decoded via `ObservationWire.h` `CompleteWire`):
`CompleteJob` entered with status 0 (`Succeeded`) at tick 667760453 on thread
6204 and **returned without exception** at tick 667760468 on thread 6204. The
first framework cancellation (thread 14264, tick 667760468) sampled
`completeCancellation` and already saw both the entry and the return
published (`ReadComplete` reads `returned` before `entry`). So in this run the
SystemPolicy cancellation was observed **after** `CompleteJob(Succeeded)` had
returned; what it truncates is the activation's own post-`CompleteJob`
journaling/deferral release, not the print. Ticks are equal at
`GetTickCount64` resolution, so ordering rests on the publish/read sample,
not on timestamps; this is still a single observation, not causal proof.

**Repeat series, runs 02-06** (`RunSilentPrintSeries.ps1` SHA256
`510ce96e...cdf9`, per-run `RunSilentPrintRun.ps1` `da7f03c3...b764` = run 01
script with only run index/evidence dir/destination changed; 15:09:26Z-15:10:48Z;
sequential fresh processes, idle check + 5 s gap, stop on first anomaly).
Evidence `SilentPrint02`..`06/`, table `SilentPrintSeries2/series.json`
(`efdbbee7...a87a`). `SilentPrintSeries/` is an aborted first attempt of the
series script (variable-name bug, failed before any print) and is kept.
All five: completion `succeeded`, PDF 17670 bytes with the same SHA256 as run
01, queue empty, no package process, no errors.

| Run | Completion ms | CompleteJob entry -> return | Cancel (reason 7) at | Cancel sample saw | Published/dequeued |
|---|---|---|---|---|---|
| 01 | 4901 | 667760453 -> 667760468 | 667760468 | returned | 16/18 |
| 02 | 3514 | 668788187 -> 668788203 | 668788218 | returned | 10/12 |
| 03 | 2768 | 668804125 -> 668804140 | 668804203 | returned | 9/11 |
| 04 | 2460 | 668819843 -> 668819843 | 668819843 | returned | 10/12 |
| 05 | 2894 | 668835703 -> 668835718 | 668835718 | returned | 10/12 |
| 06 | 2556 | 668851046 -> 668851062 | 668851062 | entered, not returned | 13/15 |

In 6/6 runs: `CompleteJob(Succeeded)` returned without exception, the
terminal is `canceled` with framework reason 7 (SystemPolicy), deferral
release not observed, `first_failure` null. The framework cancellation never
preceded `CompleteJob` entry (0-78 ms after it); in run 06 it raced the call
(sampled between entry and return, same tick) and `CompleteJob` still
returned normally. Working reading: the SystemPolicy cancellation is the
platform tearing down the task in response to `CompleteJob`, and what it
loses is the journal tail (in-flight sequence 10-17 across runs), not the
print. Still observational, not causal proof.

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
