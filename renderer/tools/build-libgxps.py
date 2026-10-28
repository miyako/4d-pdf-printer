"""Local development build of the exact patched library; not a dependency installer."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile
import urllib.request


# Same archive hash as the pinned vcpkg port.
SHA512 = "d3d56e4906477b68d088bf83bde666f9ea8bf383add592772aad53dd571e727f1bc0410dd020e12212ede5ff8e26cb46150a9860a6f7af29c4d195f03e946fe9"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True, help="new, disposable build directory")
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    prefix = args.prefix.resolve()
    work.mkdir(parents=True, exist_ok=False)
    archive = work / "libgxps-0.3.2.tar.xz"
    urllib.request.urlretrieve("https://download.gnome.org/sources/libgxps/0.3/libgxps-0.3.2.tar.xz", archive)
    if hashlib.sha512(archive.read_bytes()).hexdigest() != SHA512:
        raise RuntimeError("libgxps archive checksum mismatch")
    with tarfile.open(archive) as source:
        source.extractall(work, filter="data")
    source = work / "libgxps-0.3.2"
    patches = Path(__file__).resolve().parents[1] / "ports" / "libgxps"
    for patch in ("meson-compat.patch", "fail-closed.patch"):
        subprocess.run(["patch", "-p1", "--input", str(patches / patch)], cwd=source, check=True)
    subprocess.run(["meson", "setup", str(work / "build"), str(source),
                    "--prefix", str(prefix), "--libdir", "lib", "-Ddisable-introspection=true",
                    "-Denable-test=false", "-Denable-gtk-doc=false"], check=True)
    subprocess.run(["meson", "compile", "-C", str(work / "build")], check=True)
    subprocess.run(["meson", "install", "-C", str(work / "build")], check=True)


if __name__ == "__main__":
    main()
