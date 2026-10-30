# Dependencies, patch maintenance and redistribution gates

**Own-code licensing is undecided.** This PR does not assign a product license
or certify legal compliance. The third-party source patch/port notices below
do not grant a license to unrelated new worker or test code. No binaries are
published as a supported redistributable package by this change.

## Pinned recipes versus actual runtime

Registry/tool commit:
[`866e9e258a76437074a5cf02d4c2d71b53bff2f8`](https://github.com/microsoft/vcpkg/tree/866e9e258a76437074a5cf02d4c2d71b53bff2f8).
Upstream libgxps is **0.3.2**, SHA-512 pinned in the overlay and local helper.
The registry port is revision 5; our local revision **6** retains the upstream
build approach. The current local revision **7** additionally fixes the
observed interleaved resource reader defects described below. Historical
upstream commit `693b1b74fce64fa3d340ee9fcf29db60ce2c3551` was useful research,
but the build uses the release archive, not that moving-source assumption.

The Windows triplets are `x64-windows` and `arm64-windows`: dynamic libraries
and dynamic CRT, separate target/install/runtime trees and one shared pinned
recipe. Cairo
uses its default fontconfig/freetype features. Do not infer the final feature
set from one incoming dependency edge: the observed Windows installed status
includes FreeType[brotli,bzip2,png,zlib], TIFF[jpeg,lzma,zip] and
libxml2[iconv,zlib]. libarchive defaults select bzip2/crypto/lz4/lzma/xar/zstd.
On Windows, xar uses XmlLite and the observed archive DLL uses CNG/bcrypt;
libxml2 is installed through libxslt but is not in the observed PE closure.

The following describes the **pinned installed/build graph**, not a list of
shipped DLLs. Each port's exact manifest/recipe/notices lives under
`ports/<name>/` at the registry commit. Null/absent license metadata was
checked against source notices where noted.

| Target port | Version / revision | License facts |
|---|---|---|
| libgxps | 0.3.2#7 overlay | LGPL-2.1-or-later; registry #5 omits license metadata |
| cairo | 1.18.6 | LGPL-2.1-only OR MPL-1.1 |
| glib | 2.90.1 | LGPL-2.1-or-later AND (LGPL-2.1-only OR MPL-1.1) |
| lcms | 2.19.1#1 | MIT; GPL plugin features not selected |
| libarchive | 3.8.9 | Composite permissive notices, not a single SPDX assertion; manifest null |
| libjpeg-turbo | 3.2.0#1 | BSD-3-Clause AND IJG |
| libpng | 1.6.59 | libpng-2.0 |
| libxslt | 1.1.45 | Permissive MIT/X11-style libxslt/libexslt notices, name-use conditions; manifest null |
| tiff | 4.7.2 | libtiff; observed jpeg/lzma/zip features |
| fontconfig | 2.17.1#2 | MIT |
| freetype | 2.14.3 | FTL OR GPL-2.0-or-later; use/review FTL route; observed codec features above |
| brotli | 1.2.1 | MIT; observed through FreeType |
| pixman | 0.46.4#1 | MIT AND BSD-2-Clause |
| pthread | 3.0.0#2 | Empty forwarding port, not a separate DLL |
| pthreads | 3.0.0#14 | Apache-2.0 (do not assume historical LGPL) |
| dirent | 1.26 | MIT, headers |
| expat | 2.9.0 | MIT AND CC0-1.0 |
| gettext-libintl | 0.22.5#4 | LGPL-2.1-or-later |
| libffi | 3.8.0 | MIT |
| libiconv | 1.19#1 | LGPL-2.1-or-later library; libcharset LGPL-2.0-or-later; **GPL-3.0-or-later utility** |
| pcre2 | 10.49#2 | BSD-3-Clause WITH PCRE2-exception; JIT adds BSD-2-Clause |
| libspng | 0.7.4#1 | BSD-2-Clause AND libpng-2.0 |
| libxml2 | 2.15.4 | MIT; observed iconv/zlib features |
| zlib | 1.3.2#2 | Zlib |
| bzip2 | 1.0.8#6 | bzip2-1.0.6; default tool feature selected, not needed in worker package |
| lz4 | 1.10.0 | BSD-2-Clause; GPL tools not selected |
| liblzma | 5.8.4 | 0BSD |
| openssl | 3.6.5#2 | Apache-2.0 |
| zstd | 1.5.7 | BSD-3-Clause OR GPL-2.0-only; use/review BSD route |

Host-only helpers include vcpkg CMake/config/get-vars/make/Meson ports, pkgconf,
gettext tools and gperf; recipes may additionally acquire NASM and other build
tools. **GPL build tools do not belong in the runtime package.** In particular,
never indiscriminately copy vcpkg `tools/`, host output, `iconv.exe`, converter
utilities, debug libraries or all installed `bin/` files. The reviewed selected
library manifests/notices disclosed **no AGPL requirement**; this is not a
complete file-by-file source-license or vulnerability audit.

Actual Windows evidence comes from `tools/inventory-windows.ps1` in CI:
recursive PE imports, byte sizes, SHA-256s, package-ownership file lists,
installed status, copyright files and `vcpkg.spdx.json` records. Missing app-local
non-system imports fail the inventory. This distinguishes linked runtime closure
from merely installed ports, but **delay/dynamic loads, font/config/data files,
representative rendering module traces, VC runtime redistribution and final
shipped-file allowlisting still require release review**. CI does not upload
executables/DLLs, including development runtime trees: binary distribution is
deferred until the source/compliance delivery gates below are met.

### Observed Windows PE runtime closure

The final renderer-code Windows Server 2022/x64/MSVC run,
[40645829062](https://github.com/miyako/4d-pdf-printer/actions/runs/40645829062),
built commit `ade0bd06ce97d3f5233b351cfb8ec5fa2274b67a` and observed **26
third-party DLLs, totaling 13,215,744 bytes**, in addition to the worker:

| Component | Observed app-local DLL names |
|---|---|
| libgxps | `gxps-2.dll` |
| Cairo | `cairo-2.dll` |
| GLib | `gio-2.0-0.dll`, `glib-2.0-0.dll`, `gobject-2.0-0.dll`, `gmodule-2.0-0.dll` |
| libarchive | `archive.dll` |
| LCMS / JPEG / TIFF / PNG | `lcms2-2.dll`, `jpeg62.dll`, `tiff.dll`, `libpng16.dll` |
| fontconfig / FreeType / pixman / Expat | `fontconfig-1.dll`, `freetype.dll`, `pixman-1-0.dll`, `libexpat.dll` |
| libffi / PCRE2 | `ffi-8.dll`, `pcre2-8.dll` |
| gettext-libintl / libiconv | `intl-8.dll`, `iconv-2.dll` |
| compression | `bz2.dll`, `liblzma.dll`, `lz4.dll`, `z.dll`, `zstd.dll`, `brotlidec.dll`, `brotlicommon.dll` |

This is measured PE linkage, **not an approved packaging allowlist or proof of
all runtime loading**. OpenSSL, libxslt, libxml2, libspng and pthread packages
were installed but not present in this observed PE closure; do not copy them
merely because the install plan lists them. Windows system/API-set DLLs and
the MSVC/UCRT runtime are separate requirements, not included in the 26 or
the byte total. The first inventory's candidate ownership search also matched
host-tool copies of `intl-8.dll`/`iconv-2.dll`; the maintained script now requires
the exact `x64-windows/bin/<file>` entry. Those runtime DLLs are the LGPL library
ports, **not the GPL gettext/iconv command-line programs**.
The [source-only observation snapshot](../../renderer/dependencies/observed-windows-x64.json)
retains actual names, byte sizes, hashes/imports and exact target-bin owners,
derived from that run's uploaded file lists. It is not a binary release or
an assertion that additional dynamically loaded libraries can be omitted.

## Maintained libgxps patch

The overlay contains:

- `meson-compat.patch`: upstream
  [a18e12604d2cab13146a357c7fd3b80909b65efd](https://gitlab.gnome.org/GNOME/libgxps/-/commit/a18e12604d2cab13146a357c7fd3b80909b65efd),
  Wolfgang Stoeggl, 2021-09-09, also carried by the pinned vcpkg port.
- `fail-closed.patch`: changes to LGPL-2.1-or-later libgxps, dated 2026-10-28.
  Propagates image-brush decode errors instead of discarding them, supplies an
  error for unknown image formats/non-Windows WDP, returns a render GError for
  the observed unknown-path-command assertion, and makes known unsupported
  arc/path, tile-flip, color/interpolation, triangle cap and VisualBrush
  attributes issue warnings. The wrapper rejects warnings before publication.
  A pkg-config marker prevents accidentally building against unpatched 0.3.2.
- `interleaved.patch`: changes to LGPL-2.1-or-later libgxps, dated 2026-10-28.
  Uses the existing OPC piece reader rather than rewriting input packages.
  Reads a complete multipart resource instead of allocating/reading only the
  first physical entry's size, fills logical reads across piece boundaries
  (including PNG header reads), recognizes a sole `[0].last.piece`, and returns
  a GError for a missing next/terminal piece rather than recursing at EOF.
  Buffer growth uses checked address-space arithmetic. The worker still
  drains/counts every physical ZIP entry and enforces the same 1-GiB total
  expanded-byte and 16,384-entry bounds before libgxps reads the immutable
  input. A second pkg-config marker prevents stale revision-6 builds.

Original synthetic multipart obfuscated-font tests failed with render exit 32
before this correction; sole-last-piece packages failed with document exit 30.
The checked regressions now cover XPS/OXPS, multiple documents/pages, binary
resources, XML token splits, one-byte and 31/32/33-byte font boundaries,
nonempty/empty terminal pieces, sole-last pieces, and missing/gapped/misnamed/
out-of-order pieces in referenced root relationships, sequences and fonts.
Direct and interleaved encodings produce identical PDF bytes for the generated
cases on each tested backend. These observations are not exhaustive OPC
conformance or rejection of every malformed layout: duplicate/ambiguous parts,
unused metadata and all content-type semantics are not validated.

Initial real synthetic regressions on unpatched Homebrew 0.3.2 observed:
`Data="NOT_A_PATH"` aborted with SIGABRT; corrupt PNG in an ImageBrush produced
normal exit 0 and a PDF with the image omitted. The same generated inputs now
return render error 32 with no published output. Arc commands, ArcSegment,
FlipX tiling, Triangle cap, unknown path children and gradient interpolation
are specifically exercised as warning rejection 33; invalid colors produce
render error 32. Tests execute the worker in child processes with deadlines.

**This is not a complete omission audit.** Other malformed inputs can still
crash/hang native libraries; other semantics may still be approximated or ignored.
WDP on Windows, ICC profiles, real fonts, print tickets, gradients and production
documents need much broader coverage. No original XPS translator or new arc
implementation has been added. On an upstream update, rebase/review every patch,
run regressions against both original and patched source, retain ABI/source
provenance and seek upstream fixes rather than expanding into a forked engine.

`renderer/third-party/libgxps-COPYING` and `libgxps-AUTHORS` preserve upstream
license/authorship material for these source patches; individual upstream file
copyright notices remain in the corresponding release source. The port recipe
derives from vcpkg under `renderer/third-party/vcpkg-LICENSE.txt`.

## Source, notices and LGPL deployment plan

libgxps and GLib are covered libraries, as are applicable gettext/iconv pieces.
The process boundary **does not remove LGPL obligations**. A plausible route is
loose, interface-compatible, user-replaceable DLLs with an explicitly supported
replacement mechanism, not statically linked covered code. This route has not
yet been demonstrated as a deliverable.

Before binary distribution:

1. Select the product license/EULA and applicable alternative licenses; preserve
   customer modification and reverse-engineering rights needed for debugging
   modifications. Confirm the selected LGPL version/section route with qualified
   review, not a generic "LGPL is fine" statement.
2. Publish matching complete covered-library source alongside each binary
   release: exact archives, notices, all vcpkg and local patches, changed-file
   notices, build/install scripts, tool/SDK/ABI provenance and source hashes.
   Upstream download links alone are not the delivery mechanism.
3. Preserve actual copyright/license texts and source attributions for the
   shipped runtime closure; record artifact hashes and file-to-source/license
   mappings. libarchive's composite COPYING and libxslt/libexslt/libiconv
   distinct notices must not be replaced by assumed single-license labels.
4. Test replacing each applicable DLL and indirect dependency with an
   interface-compatible modified build, without vendor-only signature/hash
   enforcement defeating the advertised path. Alternatively establish an
   appropriate relinking/rebuild kit, including needed application object code
   or suitably licensed source, libraries/data, scripts and a usable execution
   route. Own-code licensing is still a gate to such a source-based kit.

Signed immutable MSIX library files are not automatically replaceable.
Package mutation may invalidate execution. Choose and test an approved external/
unpackaged worker-library location or an actually usable relink/rebuild/repackage
route, including user signing/execution as appropriate. Do not promise that
using DLLs, publishing this source, or separating executables proves compliance.
Do not conflate LGPL 2.1 and LGPL 3 installation-information requirements.
See [LGPL 2.1 section 6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)
and [MSIX package runtime behavior](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes).

## Test assets and tools

No copied application documents, private inputs or downloaded font files are
used in the repository or public CI. A separately authorized local capture and
its PDF/raster evidence remain private and are not published as fixtures or
artifacts. Synthetic XML/PNG/font geometry is original new project material with
own-code licensing still undecided. FontTools generates font tables (MIT);
pypdf inspects PDF structure/text (BSD-3-Clause); jsonschema validates the status
(MIT). Their pinned test-only dependencies have their own installed notices.
No Ghostscript, MuPDF, qpdf, PDF-Writer or Microsoft XpsToPdf dependency is added.

The generated OpenXPS case uses ECMA-388 first edition Annex E Table E.1
part content types and Table E.2's
`http://schemas.openxps.org/oxps/v1.0/fixedrepresentation`, with the OpenXPS
page namespace. These identifiers and synthetic piece reconstruction are checked
independently of libgxps, which does not validate the content-type table. Public
CI uses only original generated cases; no Windows-generated private capture
is transferred to CI.
[Primary standard](https://ecma-international.org/wp-content/uploads/ECMA-388_1st_edition_june_2009.pdf),
PDF pages 418-419.
