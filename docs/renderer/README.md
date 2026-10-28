# Standalone renderer prototype

This component converts a single XPS/OpenXPS file directly to PDF with the public
libgxps and Cairo APIs, then exits. It is independent of PSA, spooler, broker, 4D
and interactive login. Windows targets are **native x64 and ARM64 on Windows 11
24H2+**, with separate dynamic dependency/runtime trees. x64 Windows Server CI
and native Windows 11 ARM64 CI test the standalone executable, not printer
activation or end-to-end 4D integration. x64 emulation is not ARM64 runtime proof.
There is no installer, signing promise or production supervisor in this PR.
Delivery is desktop/4D-client first; Windows-service/session-0 submission remains
a future open requirement, not a desktop blocker.

**Important limitations:** libgxps 0.3.2 has known unsupported features and can
abort on malformed input. A small pinned patch rejects the specific observed
silent-loss paths; it is not comprehensive XPS conformance validation. Arbitrary
native crashes/hangs remain possible. An external supervisor is mandatory for
untrusted or unattended jobs; see [the process boundary](architecture.md).
Synthetic tests cannot establish better real-world size/fidelity or fixes for
Microsoft repeated-printing failures.

## CLI

```text
pdf-render-worker.exe --protocol-version 1 --input <source.xps-or-oxps> --output <new.pdf>
```

Invoke the executable directly with separately encoded arguments, not a shell
command assembled from document paths. Windows uses `wmain`, UTF-8 GLib filenames
and native wide publication APIs. Input extensions do not determine the parser;
the ZIP package relationships do. All fixed documents/pages are emitted in order
at original size (96 XPS units/inch to 72 PDF points/inch). Fonts, glyph text,
paths and images are passed to Cairo; there is no full-document rasterization,
font disabling or subsequent recompression. Unsupported Cairo operations may
use its normal 300-DPI fallback, not an assurance of all-vector output.

No overwrite is supported. This is a **provisional conservative prototype policy**,
not the agreed production overwrite UX. No implicit retries, stdin stream jobs,
page selection, settings environment variables, UI or credentials are implemented.
See [the exact v1 status and exit contract](cli-v1.md) and
[`status-v1.schema.json`](../../renderer/status-v1.schema.json).

## Windows source build

Use PowerShell 7, Git, Python 3.12 and the matching native Windows OS.
x64 uses Visual Studio 2022; ARM64 uses Visual Studio 2026 and CMake >= 4.2.
Both require installed v143/14.44 Hostx64 tools and release/debug CRT libraries
for the target architecture, plus a matching Windows SDK. vcpkg host helpers
use the native architecture's triplet too: the ARM64 runner's x64 cross-compiler
executables do not imply that x64 CRT libraries are installed.
The helper selects/records the exact
installed patch version and SDK, generates explicit dynamic overlay triplets,
and fails instead of silently switching toolsets/architectures.
From the repository worktree:

```powershell
$ErrorActionPreference = 'Stop'
$arch = 'arm64' # Use 'x64' on native x64 Windows.
& renderer/tools/build-windows.ps1 -Architecture $arch -Stage Prepare
& renderer/tools/build-windows.ps1 -Architecture $arch -Stage Build
& renderer/tools/build-windows.ps1 -Architecture $arch -Stage Test
```

Stop on any nonzero build command. `renderer/vcpkg.json` pins the registry
baseline; `renderer/ports/libgxps` is the explicit overlay. vcpkg has no separate
application lockfile here: the baseline, manifest, overlay and tool commit form
the source recipe. Compiler/SDK and resulting ABIs still belong in build evidence.
Use **dynamic** `x64-windows` or `arm64-windows`, never static or ARM64EC.
CMake deliberately requires both fail-closed and interleaving patch markers.
`Prepare` recreates only the selected architecture's build/runtime/evidence
trees; installed dependencies and local ABI-keyed binary caches are retained.
Public CI does not restore/upload dependency binary caches: public-repository
caches can be readable from fork PRs, and covered-source/distribution gates
apply to this route too. Per-job ephemeral caches are allowed; this policy is
not a conclusion that caching generally is unlawful or that deletion grants
license clearance.

The [Windows workflow](../../.github/workflows/renderer-windows.yml) builds,
executes synthetic tests against the assembled native runtime with a restricted
PATH, inventories actual release PE machines/imports/owners, and saves installed
notices/SBOMs plus recipe/compiler/SDK provenance. Each architecture has separate
`build-windows-<arch>`, `build-installed-<arch>`, `build-runtime-<arch>` and
`build-evidence-<arch>` trees. The external development root is
`renderer/build-runtime-<arch>/`, not an installed vcpkg `bin/` directory.
CI uploads **evidence only** as `renderer-windows-<arch>-evidence`; no binaries.
Manual dispatch can select `architecture=arm64` or `x64` to avoid a redundant
cold build when only one architecture needs new evidence; PRs run both.
`cmake --install` still installs the executable only, not this assembled tree.
See [the exact layout and consumer contract](delivery.md).

## macOS development checks

This is a portable development path, not the pinned Windows dependency graph.
Install missing native build prerequisites through your package manager:
Meson, Ninja, pkg-config, Cairo, GLib, FreeType, libarchive, PNG, JPEG, TIFF,
LCMS and libxslt. With Homebrew, add keg-only `.pc` directories to
`PKG_CONFIG_PATH`, including libarchive, libtiff, jpeg-turbo and little-cms2.
Do not install Ghostscript or assume its similarly named library is libgxps.

```sh
python3.12 -m venv renderer/build-testenv
renderer/build-testenv/bin/python -m pip install -r renderer/tests/requirements.txt
python3.12 renderer/tools/build-libgxps.py \
  --work renderer/build-gxps --prefix renderer/build-gxps-prefix
export PKG_CONFIG_PATH="$PWD/renderer/build-gxps-prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
cmake -S renderer -B renderer/build-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE="$PWD/renderer/build-testenv/bin/python"
cmake --build renderer/build-local
ctest --test-dir renderer/build-local --output-on-failure --verbose
```

The helper requires a **new** work directory, verifies the upstream archive's
SHA-512 before extracting, applies the same three overlay patches and builds a
local dynamic library. It does not install dependencies or modify system paths.

## Evidence and remaining gates

The synthetic generator contains original page XML, a generated six-glyph font
and a generated 2x2 PNG, not third-party documents/fonts. It covers legacy XPS,
OXPS, two fixed documents, mixed-size multipage output, Unicode/CJK extraction,
embedded fonts, clipping/transparency/vector/image resources, invalid input,
encryption markers, CRC/compression errors, exact ZIP-entry count and page-count
guards, collision/race/permission paths, output write/finalization,
post-publication failures and 20 sequential fresh processes. The fixture font
timestamps and ZIP metadata are fixed; repeated generation is byte-checked.
Interleaved regressions use only those generated assets: split content types,
relationships, document sequences, pages and obfuscated fonts/images; multiple
documents/pages; one-byte and 31/32/33-byte boundaries; single/empty/nonempty
terminal pieces; and checked missing/gapped/misnamed/out-of-order pieces.
The same physical entry-count boundary applies to interleaved ZIPs.

On the initial macOS arm64 development run (patched libgxps 0.3.2, Cairo 1.18.4,
GLib 2.88.3, libarchive 3.8.10), each generated two-page XPS and OXPS produced
**6,115 bytes / 3,057.5 bytes per page**. Input ZIP sizes were 3,611 and 3,574
bytes. These are measurements, not size-reduction claims or acceptance targets.
Tests inspect PDF structure, text and resources; **no independent XPS/Microsoft
pixel reference, real 4D corpus, accessibility/PDF-A/print-ticket validation,
WDP validation or Microsoft failure reproduction has been established**.
Separately authorized capture conversion and source/PDF raster checks were
performed locally and retained privately, not added to this repository or CI.
Those source checks share libgxps's parser with conversion; PDF rasterizers
also differ in thin-stroke/text antialiasing. They do not establish pixel-perfect
fidelity, independent XPS conformance or a size improvement over Microsoft.

The earlier renderer code at `ade0bd0` passed the
[Windows Server 2022/x64/MSVC build and execution run](https://github.com/miyako/4d-pdf-printer/actions/runs/40645829062):
30 Python cases registered, 25 passed / 5 POSIX-only cases skipped, plus native
transaction tests. It produced the same 6,115-byte PDFs and validated Unicode
Windows paths, direct conversion, error/status shapes, collisions and repeated
fresh invocations. macOS passes all 30 cases and native tests; worker
AddressSanitizer/UndefinedBehaviorSanitizer checks also passed. The current
interleaving milestone passes **35 Python cases plus native transaction tests**
on macOS ARM64, including a run with both patched libgxps and the worker
instrumented for AddressSanitizer/UndefinedBehaviorSanitizer (Apple leak
detection disabled). This does not substitute for Windows native execution.
Installed DLL,
feature and license evidence is distinguished from the manifest assumptions in
[the observed runtime inventory](dependencies.md#observed-windows-pe-runtime-closure).

The current worker/overlay/build code at
`0ce31edb786d4d13268ef4e9c63b61b13017c180` passed the
[native Windows x64 job in run 40701343705](https://github.com/miyako/4d-pdf-printer/actions/runs/40701343705/job/122861694213):
35 Python cases (30 passed, 5 POSIX-only skipped), plus native transactions,
against the assembled runtime with a restricted PATH. All 27 packaged PEs
(worker and 26 target-owned DLLs) were `0x8664`; DLLs totaled 13,216,768 bytes
and the worker 61,952 bytes. The two-page synthetic PDFs remained 6,115 bytes.
This job used VS 2022 17.14.37806.7/v143 14.44.35207, SDK 10.0.26100.0,
CMake 3.31.6 and a native x64 OS. The **overall run was later cancelled** to stop
the other architecture's pending binary-cache publication; the completed x64
result remains valid. Do not relabel it as testing the later cache-disabled
workflow revision `fc8ea13984508f1a5405eb40851bf21527cc077d`: that revision changes
only workflow/cache policy and related documentation, not the renderer,
dependencies, helpers or test inputs.

The cache-disabled revision `fc8ea13984508f1a5405eb40851bf21527cc077d` also passed
[native Windows 11 ARM64 run 40711028689](https://github.com/miyako/4d-pdf-printer/actions/runs/40711028689)
(job `122896034653`, 1h34m13s cold build). It executed the assembled ARM64 worker:
35 Python cases registered, 30 passed / 5 POSIX-only skipped, plus native
transactions; the generated two-page PDFs were again 6,115 bytes. All 27 runtime
PEs were **`0xaa64`**, including the 57,856-byte worker and 26 target-owned DLLs
totaling 12,513,792 bytes, with ownership/notices and native test receipt checks.
The runner reported a native ARM64 OS/process, native `arm64-windows` host ports,
VS 2026 18.11.12315.225/v143 14.44.35207, SDK 10.0.26100.0 and CMake 4.4.4.
Host tools/interpreters may be x64-emulated; the tested worker/DLLs were native
ARM64, not x64 emulation or compile-only evidence.

The final checkpoint changes documentation/evidence only relative to that
revision. Its draft PR deliberately uses a `[skip ci]` documentation commit to
honor the bounded-build checkpoint: **final HEAD/PR checks are not claimed
green**. The completed manual results above retain their exact tested revisions.
Neither architecture result establishes printer activation, end-to-end desktop
printing, user-host validation or Windows-service submission.

Runtime DLL/notice inventory, LGPL replacement/relinking, immutable MSIX,
current font/rendering fidelity and desktop identity integration must be
reviewed before distribution. Windows-service execution is deferred, not
silently supplied by a logged-in helper. See
[dependencies and licensing](dependencies.md) and
[the desktop worker delivery contract](delivery.md).
The project's own-code license remains **undecided**; this change adds no
product-license grant.
