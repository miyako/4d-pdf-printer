import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest
import zipfile
from xml.etree import ElementTree

import jsonschema
from pypdf import PdfReader

from fixtures import OBFUSCATED_FONT, TEXT, font, package


parser = argparse.ArgumentParser()
parser.add_argument("--worker", type=Path, required=True)
parser.add_argument("--schema", type=Path, required=True)
parser.add_argument("--output-tests", type=Path, required=True)
options, unittest_args = parser.parse_known_args()
WORKER = options.worker.resolve()
SCHEMA = json.loads(options.schema.read_text(encoding="utf-8"))
OUTPUT_TESTS = options.output_tests.resolve()


class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="worker-tests-")
        self.root = Path(self.temporary.name)
        self.input = self.root / "input.xps"
        self.output = self.root / "output.pdf"
        package(self.input)

    def tearDown(self):
        self.assertEqual(list(self.root.glob(".gxps-job-*")), [], "normal exits must clean job files")
        self.temporary.cleanup()

    def command(self, source=None, destination=None):
        return [str(WORKER), "--protocol-version", "1",
                "--input", str(source or self.input), "--output", str(destination or self.output)]

    def invoke(self, command=None, expected=0, **kwargs):
        # Worker never spawns children; timeout kills and reaps this one process.
        process = subprocess.run(command or self.command(), capture_output=True, timeout=20, **kwargs)
        self.assertEqual(process.returncode, expected, process.stdout)
        self.assertEqual(len(process.stdout.splitlines()), 1, process.stdout)
        status = json.loads(process.stdout.decode("utf-8"))
        jsonschema.Draft202012Validator(SCHEMA).validate(status)
        self.assertEqual(status["exit_code"], process.returncode)
        self.assertEqual(status["result"], "error" if expected else "ok")
        self.assertNotIn(str(self.input), process.stdout.decode("utf-8"))
        self.assertNotIn("NOT_A_PATH", process.stdout.decode("utf-8"))
        self.assertEqual(process.stderr, b"")
        self.assertTrue(status["cleanup_complete"])
        if expected:
            self.assertFalse(status["output_published"])
        else:
            self.assertTrue(status["output_published"])
        return status

    def assert_pdf(self, path, pages=2):
        data = path.read_bytes()
        self.assertTrue(data.startswith(b"%PDF-"))
        self.assertTrue(data.rstrip().endswith(b"%%EOF"))
        reader = PdfReader(path, strict=True)
        self.assertEqual(len(reader.pages), pages)
        for i, page in enumerate(reader.pages):
            expected_size = (288, 216) if i % 2 == 0 else (144, 288)
            self.assertEqual((float(page.mediabox.width), float(page.mediabox.height)), expected_size)
            self.assertIn(TEXT, page.extract_text())
            operations = [operator for _, operator in page.get_contents().operations]
            self.assertTrue(b"m" in operations or b"re" in operations, "vector paths must survive")
            self.assertTrue(b"W" in operations or b"W*" in operations, "clipping must survive")
            resources = page["/Resources"]
            self.assertIn("/Font", resources)
            fonts = list(resources["/Font"].values())
            for ref in fonts:
                font = ref.get_object()
                if "/DescendantFonts" in font:
                    font = font["/DescendantFonts"][0].get_object()
                descriptor = font["/FontDescriptor"].get_object()
                self.assertTrue("/FontFile2" in descriptor or "/FontFile3" in descriptor)
            images = [ref.get_object() for ref in resources.get("/XObject", {}).values()
                      if ref.get_object().get("/Subtype") == "/Image"]
            self.assertTrue(images, "synthetic image must survive")
            self.assertTrue(all(image["/Width"] == 2 and image["/Height"] == 2 for image in images),
                            "no full-page rasterization")
            self.assertTrue(any(float(gs.get_object().get("/ca", 1)) < 1
                                for gs in resources.get("/ExtGState", {}).values()),
                            "transparency must survive")
        return len(data)

    def test_xps_and_oxps(self):
        measurements = []
        for oxps in (False, True):
            with self.subTest(oxps=oxps):
                source = self.root / ("input.oxps" if oxps else "input.xps")
                output = self.root / ("oxps.pdf" if oxps else "xps.pdf")
                package(source, oxps=oxps)
                status = self.invoke(self.command(source, output))
                size = self.assert_pdf(output)
                self.assertEqual(status["output_bytes"], size)
                self.assertEqual(status["pages_completed"], 2)
                self.assertEqual(status["documents"], 1)
                measurements.append({"format": "oxps" if oxps else "xps",
                                     "input_bytes": source.stat().st_size,
                                     "output_bytes": size, "pages": 2,
                                     "bytes_per_page": size / 2})
        print("SYNTHETIC_MEASUREMENTS=" + json.dumps(measurements), flush=True)

    def test_synthetic_fixtures_are_reproducible(self):
        for oxps in (False, True):
            first = self.root / "first.package"
            second = self.root / "second.package"
            package(first, oxps=oxps)
            package(second, oxps=oxps)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                types = ElementTree.fromstring(archive.read("[Content_Types].xml"))
                self.assertEqual(
                    {part.attrib["ContentType"] for part in types if part.tag.endswith("Override")},
                    {"application/vnd.ms-package.xps-fixedpage+xml",
                     "application/vnd.ms-package.xps-fixeddocument+xml",
                     "application/vnd.ms-package.xps-fixeddocumentsequence+xml"})
                relationship = ElementTree.fromstring(archive.read("_rels/.rels"))[0]
                namespace = ("http://schemas.openxps.org/oxps/v1.0" if oxps
                             else "http://schemas.microsoft.com/xps/2005/06")
                self.assertEqual(relationship.attrib["Type"], namespace + "/fixedrepresentation")
                page = ElementTree.fromstring(archive.read("Documents/1/Pages/1.fpage"))
                self.assertEqual(page.tag, "{" + namespace + "}FixedPage")

    def test_multiple_documents(self):
        package(self.input, documents=2)
        status = self.invoke()
        self.assertEqual(status["documents"], 2)
        self.assertEqual(status["pages_completed"], 4)
        self.assert_pdf(self.output, 4)

    def test_interleaved_xps_and_oxps(self):
        for oxps in (False, True):
            package(self.input, oxps=oxps, documents=2, obfuscated=True)
            self.invoke()
            direct_pdf = self.output.read_bytes()
            self.output.unlink()
            for empty_last in (False, True):
                with self.subTest(oxps=oxps, empty_last=empty_last):
                    package(self.input, oxps=oxps, documents=2, obfuscated=True,
                            piece_size=97, empty_last=empty_last)
                    with zipfile.ZipFile(self.input) as archive:
                        names = archive.namelist()
                        for part in ("[Content_Types].xml", "_rels/.rels",
                                     "FixedDocumentSequence.fdseq", OBFUSCATED_FONT):
                            self.assertNotIn(part, names)
                            self.assertIn(part + "/[0].piece", names)
                    status = self.invoke()
                    self.assertEqual(status["documents"], 2)
                    self.assertEqual(status["pages_completed"], 4)
                    self.assertEqual(status["output_bytes"], self.assert_pdf(self.output, 4))
                    self.assertEqual(self.output.read_bytes(), direct_pdf,
                                     "physical piece boundaries must not alter this synthetic PDF")
                    self.output.unlink()

    def test_interleaved_font_boundaries(self):
        for size in (1, 31, 32, 33, len(font()), len(font()) + 1):
            with self.subTest(piece_size=size):
                package(self.input, pages=1, obfuscated=True, piece_size=size, empty_last=True)
                self.invoke()
                self.assert_pdf(self.output, 1)
                self.output.unlink()

    def test_single_last_piece(self):
        package(self.input, obfuscated=True, piece_size=65536)
        self.invoke()
        self.assert_pdf(self.output)

    def test_interleaved_fixture_reproducibility_and_reconstruction(self):
        first, second = self.root / "first.oxps", self.root / "second.oxps"
        package(self.input, oxps=True, obfuscated=True)
        package(first, oxps=True, obfuscated=True, piece_size=31, empty_last=True)
        package(second, oxps=True, obfuscated=True, piece_size=31, empty_last=True)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(self.input) as direct, zipfile.ZipFile(first) as split:
            for name in direct.namelist():
                pieces = []
                index = 0
                while name + f"/[{index}].piece" in split.namelist():
                    pieces.append(split.read(name + f"/[{index}].piece"))
                    index += 1
                pieces.append(split.read(name + f"/[{index}].last.piece"))
                self.assertEqual(b"".join(pieces), direct.read(name))

    def test_malformed_interleaved_pieces(self):
        for target, expected in (("_rels/.rels", 30),
                                 ("FixedDocumentSequence.fdseq", 30),
                                 (OBFUSCATED_FONT, 32)):
            for kind, empty_last in (("missing-first", False), ("missing-middle", False),
                                     ("missing-last", False), ("missing-last", True),
                                     ("bad-suffix", False), ("out-of-order", False)):
                with self.subTest(target=target, kind=kind, empty_last=empty_last):
                    package(self.input, obfuscated=True, piece_size=31,
                            empty_last=empty_last, malformed_piece=(target, kind))
                    self.invoke(expected=expected)
                    self.assertFalse(self.output.exists())

    def test_unicode_paths(self):
        directory = self.root / "Unicode \u4e2d\u03a9"
        directory.mkdir()
        source = directory / "\u5370\u5237.oxps"
        output = directory / "\u7d50\u679c.pdf"
        package(source, oxps=True)
        self.invoke(self.command(source, output))
        self.assert_pdf(output)
        self.assertEqual(list(directory.glob(".gxps-job-*")), [])

    def test_relative_paths_and_spaces(self):
        self.invoke([str(WORKER), "--output", "new output.pdf", "--input", "input.xps",
                     "--protocol-version", "1"], cwd=self.root)
        self.assert_pdf(self.root / "new output.pdf")

    def test_invalid_cli(self):
        for args in ([], ["--protocol-version", "2"], ["--input", "x"],
                     self.command()[1:] + ["--unexpected", "x"],
                     ["--input", "x", "--input", "x", "--protocol-version", "1"]):
            with self.subTest(args=args):
                self.invoke([str(WORKER)] + args, expected=2)
        self.assertFalse(self.output.exists())

    def test_version(self):
        process = subprocess.run([str(WORKER), "--version"], capture_output=True, timeout=20)
        self.assertEqual(process.returncode, 0)
        status = json.loads(process.stdout)
        jsonschema.Draft202012Validator(SCHEMA).validate(status)
        self.assertEqual(status["result"], "info")
        self.assertFalse(status["output_published"])

    def test_missing_and_directory_input(self):
        self.invoke(self.command(self.root / "missing.xps"), expected=10)
        self.invoke(self.command(self.root), expected=10)

    def test_invalid_and_truncated_packages(self):
        for data in (b"", b"not XPS", self.input.read_bytes()[:500], self.input.read_bytes()[:-22]):
            with self.subTest(length=len(data)):
                source = self.root / "broken.xps"
                source.write_bytes(data)
                self.invoke(self.command(source), expected=11)
                self.assertFalse(self.output.exists())

    def test_encrypted_rejected(self):
        package(self.input, encrypted=True)
        self.invoke(expected=12)
        self.assertFalse(self.output.exists())

    def test_crc_and_unsupported_compression(self):
        original = self.input.read_bytes()
        with zipfile.ZipFile(self.input) as archive:
            position = archive.start_dir
        data = bytearray(original)
        # A central-directory CRC mismatch must not be accepted as a valid package.
        data[position + 16] ^= 0xFF
        self.input.write_bytes(data)
        self.invoke(expected=11)
        data = bytearray(original)
        for signature, offset in ((b"PK\x03\x04", 8), (b"PK\x01\x02", 10)):
            position = 0
            while (position := data.find(signature, position)) >= 0:
                data[position + offset:position + offset + 2] = b"\xff\xff"
                position += 4
        self.input.write_bytes(data)
        self.invoke(expected=11)
        self.assertFalse(self.output.exists())

    def test_entry_limit_boundary(self):
        for piece_size in (None, 97):
            with self.subTest(piece_size=piece_size):
                package(self.input, piece_size=piece_size)
                with zipfile.ZipFile(self.input, "a", compression=zipfile.ZIP_DEFLATED) as archive:
                    count = len(archive.infolist())
                    for i in range(16384 - count):
                        archive.writestr(f"Unused/{i}", b"")
                self.invoke()
                self.assert_pdf(self.output)
                self.output.unlink()
                with zipfile.ZipFile(self.input, "a") as archive:
                    archive.writestr("Unused/over-limit", b"")
                self.invoke(expected=13)
                self.assertFalse(self.output.exists())

    def test_page_limit_rejected_before_render(self):
        with zipfile.ZipFile(self.input) as archive:
            parts = {info.filename: archive.read(info) for info in archive.infolist()}
        ns = "http://schemas.microsoft.com/xps/2005/06"
        parts["Documents/1/FixedDocument.fdoc"] = (
            f'<FixedDocument xmlns="{ns}">' + '<PageContent Source="Pages/1.fpage"/>' * 10001
            + '</FixedDocument>').encode("utf-8")
        with zipfile.ZipFile(self.input, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, value in parts.items():
                archive.writestr(name, value)
        status = self.invoke(expected=13)
        self.assertEqual(status["pages_completed"], 0)
        self.assertFalse(self.output.exists())

    def test_bad_relationship(self):
        package(self.input, broken="relationship")
        self.invoke(expected=30)
        self.assertFalse(self.output.exists())

    def test_bad_page_xml(self):
        package(self.input, broken="xml")
        self.invoke(expected=31)
        self.assertFalse(self.output.exists())

    def test_bad_page_size(self):
        package(self.input, broken="size")
        self.invoke(expected=31)
        self.assertFalse(self.output.exists())

    def test_render_failures(self):
        for broken in ("render", "font", "font-data", "image"):
            with self.subTest(broken=broken):
                package(self.input, broken=broken)
                self.invoke(expected=32)
                self.assertFalse(self.output.exists())

    def test_checked_unsupported_features_rejected(self):
        for broken in ("arc", "arc-segment", "tile", "triangle", "color", "path-child", "gradient"):
            with self.subTest(broken=broken):
                package(self.input, broken=broken)
                self.invoke(expected=32 if broken == "color" else 33)
                self.assertFalse(self.output.exists())

    def test_destination_collision(self):
        self.output.write_bytes(b"existing destination")
        self.invoke(expected=20)
        self.assertEqual(self.output.read_bytes(), b"existing destination")
        self.invoke(self.command(destination=self.root), expected=20)

    @unittest.skipIf(os.name == "nt", "symlink creation may require Windows privileges")
    def test_dangling_symlink_collision(self):
        self.output.symlink_to(self.root / "missing")
        self.invoke(expected=20)
        self.assertTrue(self.output.is_symlink())

    def test_input_output_same_path(self):
        original = self.input.read_bytes()
        self.invoke(self.command(destination=self.input), expected=20)
        self.assertEqual(self.input.read_bytes(), original)

    def test_output_creation_failure(self):
        self.invoke(self.command(destination=self.root / "missing" / "output.pdf"), expected=21)
        self.invoke(self.command(destination=self.input / "output.pdf"), expected=21)

    @unittest.skipIf(os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0),
                     "POSIX non-root permissions required")
    def test_output_permission_failure(self):
        directory = self.root / "read-only"
        directory.mkdir()
        directory.chmod(0o500)
        try:
            self.invoke(self.command(destination=directory / "output.pdf"), expected=21)
        finally:
            directory.chmod(0o700)

    @unittest.skipIf(os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0),
                     "POSIX non-root permissions required")
    def test_input_permission_failure(self):
        self.input.chmod(0)
        try:
            self.invoke(expected=10)
        finally:
            self.input.chmod(0o600)

    @unittest.skipIf(os.name == "nt", "POSIX file-size limit fault injection")
    def test_page_write_failure(self):
        import resource

        def restrict_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (64, 64))

        self.invoke(expected=21, preexec_fn=restrict_output)
        self.assertFalse(self.output.exists())

    @unittest.skipIf(os.name == "nt", "POSIX file-size limit fault injection")
    def test_finalization_write_failure(self):
        import resource

        # A normal conversion measures this backend's exact final size; its last byte
        # belongs to the final trailer, so this fault is independent of page buffering.
        self.invoke()
        limit = self.output.stat().st_size - 1
        self.output.unlink()

        def restrict_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

        self.invoke(expected=22, preexec_fn=restrict_output)
        self.assertFalse(self.output.exists())

    def test_status_channel_failure_after_publication(self):
        process = subprocess.Popen(self.command(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        process.stdout.close()
        try:
            self.assertEqual(process.wait(timeout=20), 71)
            self.assertEqual(process.stderr.read(), b"")
            self.assert_pdf(self.output)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=20)
            process.stderr.close()

    def test_cleanup_failure_after_publication(self):
        process = subprocess.run([str(OUTPUT_TESTS), "--cleanup-failure", str(self.output)],
                                 capture_output=True, timeout=20)
        self.assertEqual(process.returncode, 24, process.stderr)
        status = json.loads(process.stdout)
        jsonschema.Draft202012Validator(SCHEMA).validate(status)
        self.assertEqual(status["result"], "error")
        self.assertEqual(status["error"]["code"], "cleanup")
        self.assertTrue(status["output_published"])
        self.assertFalse(status["cleanup_complete"])
        self.assertTrue(self.output.is_file())

    def test_repeated_disposable_jobs(self):
        for i in range(20):
            output = self.root / f"repeat-{i}.pdf"
            self.invoke(self.command(destination=output))
            self.assert_pdf(output)

    def test_concurrent_publication_collision(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(subprocess.run, self.command(), capture_output=True, timeout=20)
                       for _ in range(2)]
            processes = [future.result() for future in futures]
        self.assertEqual(sorted(p.returncode for p in processes), [0, 20])
        for process in processes:
            status = json.loads(process.stdout)
            jsonschema.Draft202012Validator(SCHEMA).validate(status)
            self.assertEqual(status["exit_code"], process.returncode)
            self.assertTrue(status["cleanup_complete"])
        self.assert_pdf(self.output)


if __name__ == "__main__":
    unittest.main(argv=[__file__] + unittest_args, verbosity=2)
