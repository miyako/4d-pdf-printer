# Standalone renderer prototype

This component converts a single XPS/OpenXPS file directly to PDF with the public
libgxps and Cairo APIs, then exits. It is independent of PSA, spooler, broker, 4D
and interactive login. The initial Windows target is **x64 on Windows 11 24H2+**;
other Windows architectures are not supported. Windows Server CI tests the
standalone executable, not Windows 11 printer activation or 4D service integration.
There is no installer, signing promise or production supervisor in this PR.

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

Use Visual Studio 2022 C++ tools/Windows SDK, CMake >= 3.24, Git and Python 3.12.
From the repository worktree in PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg.git renderer/build-vcpkg
git -C renderer/build-vcpkg checkout --detach 866e9e258a76437074a5cf02d4c2d71b53bff2f8
& renderer/build-vcpkg/bootstrap-vcpkg.bat -disableMetrics
& renderer/build-vcpkg/vcpkg.exe install --triplet x64-windows --host-triplet x64-windows --x-manifest-root=renderer --x-install-root=renderer/build-installed
python -m venv renderer/build-testenv
& renderer/build-testenv/Scripts/python.exe -m pip install -r renderer/tests/requirements.txt
$root = (Get-Location).Path
$env:PKG_CONFIG_PATH = "$root/renderer/build-installed/x64-windows/lib/pkgconfig;$root/renderer/build-installed/x64-windows/share/pkgconfig"
cmake -S renderer -B renderer/build-windows -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$root/renderer/build-vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_INSTALLED_DIR=$root/renderer/build-installed" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_HOST_TRIPLET=x64-windows `
  "-DPKG_CONFIG_EXECUTABLE=$root/renderer/build-installed/x64-windows/tools/pkgconf/pkgconf.exe" `
  "-DPython3_EXECUTABLE=$root/renderer/build-testenv/Scripts/python.exe"
cmake --build renderer/build-windows --config Release --parallel
ctest --test-dir renderer/build-windows -C Release --output-on-failure --verbose
```

Stop on any nonzero build command. `renderer/vcpkg.json` pins the registry
baseline; `renderer/ports/libgxps` is the explicit overlay. vcpkg has no separate
application lockfile here: the baseline, manifest, overlay and tool commit form
the source recipe. Compiler/SDK and resulting ABIs still belong in build evidence.
Use **dynamic** `x64-windows`, not `x64-windows-static`.
CMake deliberately rejects a stock libgxps without the patch marker.

The [Windows workflow](../../.github/workflows/renderer-windows.yml) builds,
executes synthetic tests, inventories actual release PE imports and saves
package file lists, installed notices and SBOMs. It uploads **evidence only**;
it does not publish a cleared redistributable runtime. `cmake --install` installs
the executable only, not a deployment package.

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
SHA-512 before extracting, applies the same two overlay patches and builds a
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

On the initial macOS arm64 development run (patched libgxps 0.3.2, Cairo 1.18.4,
GLib 2.88.3, libarchive 3.8.10), each generated two-page XPS and OXPS produced
**6,115 bytes / 3,057.5 bytes per page**. Input ZIP sizes were 3,611 and 3,574
bytes. These are measurements, not size-reduction claims or acceptance targets.
Tests inspect PDF structure, text and resources; **no pixel-reference comparison,
real 4D corpus, accessibility/PDF-A/print-ticket validation, WDP validation or
Microsoft failure reproduction has been performed**.

Runtime DLL/notice inventory, LGPL replacement/relinking, immutable MSIX,
current font/rendering fidelity and unattended service execution must be
reviewed before distribution. See [dependencies and licensing](dependencies.md).
The project's own-code license remains **undecided**; this change adds no
product-license grant.
