# Desktop worker delivery contract

This is an external **development runtime**, not a signed installer, license
clearance or production packaging promise. The renderer owns the dependency
assembly/inventory; a desktop consumer must use it instead of maintaining a
second packager. Public CI uploads evidence only, never the executable/DLL tree,
private captures, fonts, PDFs, screenshots or raw document diagnostics.
Remote dependency binary-cache upload/restore is disabled as well: publicly
retrievable CI caches are inside this project's conservative distribution gate.
Local ephemeral per-job caches/build trees are fine. This boundary is not a
blanket legal judgment about caching and does not establish license compliance.
Earlier development dispatches did upload dependency binary caches. Four
exactly identified entries for this workstream's branch were deleted when this
gate was clarified, including an `always()` save that completed during job
cancellation. Other workstreams' caches were not purged. Prior retrieval cannot
be ruled out; do not describe those caches as never having been public or treat
their deletion as license clearance. No private captures or credentials were
involved.

## Architecture and runtime root

Build x64 and ARM64 independently. Use a matching explicit runtime root, never
PATH discovery or an emulation fallback:

| Target | Dynamic target triplet | Worker and every app-local DLL PE machine | Native CI |
|---|---|---|---|
| x64 | `x64-windows` | `0x8664` (AMD64) | `windows-2022` |
| ARM64 | `arm64-windows` | `0xaa64` (ARM64) | `windows-11-arm` |

An x64 shell/build tool on Windows ARM64 is a host tool, not proof of a native
worker. The ARM64 runner must execute the ARM64 worker and complete the same
conversion/contract tests. Cross-compilation alone is not runtime evidence.
Do not mix install trees or copy all of vcpkg's installed `bin/`, host tools,
debug libraries, converters or unrelated executables.

Runtime files must remain together under the trusted renderer root. The
executable is `pdf-render-worker.exe`; app-local dependency DLLs are adjacent.
The inventory records target-bin package ownership, PE machines, hashes,
recursive imports and OS dependencies. Notices and exact recipe/toolchain
provenance accompany the development tree. System/API-set dependencies and
Microsoft VC/UCRT runtime prerequisites are not third-party target DLLs or a
license to redistribute files from Windows System32.

The renderer-owned source-build helper is
[`build-windows.ps1`](../../renderer/tools/build-windows.ps1), stages `Prepare`,
`Build`, `Test`, with `-Architecture x64` or `arm64`. It creates:

| Path relative to repository | Content |
|---|---|
| `renderer/build-runtime-<arch>/pdf-render-worker.exe` | Worker selected by the coordinator |
| `renderer/build-runtime-<arch>/*.dll` | Recursively imported, matching-machine target-owned DLLs |
| `renderer/build-runtime-<arch>/runtime-imports.json` | Single authoritative component inventory, schema `4d-pdf-printer.pe-inventory`, version 2 |
| `renderer/build-runtime-<arch>/build-provenance.json` | Source/vcpkg SHA, recipe hashes, compiler/SDK/OS architecture |
| `renderer/build-runtime-<arch>/notices/<port>/` | Target-graph copyright and SPDX files, not complete covered source |
| `renderer/build-runtime-<arch>/native-test-receipt.json` | Written only after the native contract/transaction tests pass |
| `renderer/build-evidence-<arch>/` | Public synthetic test log, import dumps, package file lists, notices and provenance |

`Prepare` defaults to installed **VS 2022 for both native architectures**.
`-VisualStudioVersion 2026` is an explicit opt-in requiring CMake 4.2+; VS 2022
uses the existing CMake 3.24+ project requirement. Hosted ARM64 CI opts into
2026 explicitly, while x64 CI selects 2022. Both keep the same pinned v143/14.44
target compiler/CRT checks and native target/host triplet policy. The chosen year
is recorded as `visual_studio_selection` alongside the exact installed version,
generator, source/recipe/compiler hashes and SDK. `Build`/`Test` consume that
prepared provenance, not a fresh VS selection.

The selector and CMake gating have portable PowerShell coverage. Native
VS 2022 ARM64 preparation/runtime compatibility has **not** been executed by
this correction; prior hosted VS 2026 native engine results must not be relabeled
as testing the revised local helper. No tool installation or user-host renderer
execution is implied or authorized by these instructions.

Inventory `files[]` contains `file`, `machine`, `sha256`, `bytes`,
`package_file_list`, `installed_path` and `imports[]`. Each import has `file`,
`resolution`, `classification`, `observed_path` and `classification_basis`.
`external_requirements[]` distinguishes API contracts, observed known Windows
DLL names, external VC/UCRT prerequisites and unclassified System32 files;
`os_ownership_attested:false` is intentional. Rejecting unclassified files or
attesting an installed runtime is a coordinator/deployment policy decision,
not an implicit trust grant from this inventory. `notices[]` records notice
paths and SHA-256s. Provenance records the exact generated overlay triplets in
the evidence tree and selected recipe/compiler hashes; none is a product
signature. The inventory is written with UTF-8 PowerShell encoding.

The inventory is **observational build evidence**, not a signed trust root or
a universal Windows system-DLL allowlist. It does not prove dynamic loads,
fontconfig/config/data completeness, safe DLL search policy or legal clearance.
The coordinator must select an authorized immutable tree, verify architecture
and its accepted inventory/digest, pin that exact tree in prepared jobs, reject
missing/mismatched/drifted files, and never download or silently substitute a
different renderer. A hash file writable by the same attacker as the DLL is
not independent authenticity. Rendering real desktop documents and testing
modified-library replacement remain separate gates.

## Invocation and ownership

The unchanged authority is [CLI v1](cli-v1.md) and
[`status-v1.schema.json`](../../renderer/status-v1.schema.json):

```text
pdf-render-worker.exe --protocol-version 1 --input <Unicode XPS/OXPS path> --output <NEW PDF path>
```

Invoke directly with separately encoded arguments, one fresh process per job,
one immutable accessible input, one new output, no overwrite and no automatic
retry. Conversion success requires **normal exit 0 and exactly one schema-valid,
matching v1 `ok` status with `output_published:true` and
`cleanup_complete:true`**; `info` and file existence are not success.

The external full-trust desktop coordinator owns authenticated exact-job
handoff, serialization, destination authorization, Job Object assignment before
resume, deadlines, cancellation, termination, wait/reap, bounded output pipes
and exact-job temporary recovery. The worker does not launch children, enforce
a production memory/time budget, maintain a queue or implement this supervisor.
Never load its libraries into 4D, spooler or PSA, and do not assume arbitrary
`ProcessStart` inside the PSA AppContainer is supported.

Exit 24 (cleanup), exit 71 (status delivery), crashes/kills and missing/malformed
status can happen **after publication**. Reconcile only the recorded job's
output/staging state after reaping; do not blindly retry, delete a completed
destination or globally sweep `.gxps-job-*`.

## Release boundary

The first product target is desktop/4D client printing. Windows-service/session-0
submission is deferred, still open, and not solved by a logged-in helper.
This milestone does not claim printer/coordinator integration, service support
or fixes for Microsoft repeated-print faults.

External loose DLL placement does not by itself satisfy LGPL. Before any
dependency-bearing binary distribution, supply matching complete covered source,
patches/build scripts, notices/license mapping and an actually usable
replacement/relinking route; resolve immutable-MSIX/signing behavior and
own-code license/EULA choices. See [the full gates](dependencies.md). Keeping
the initial own-code desktop MSIX separate from the renderer is a development
direction, not a permanent installer design or compliance conclusion.
