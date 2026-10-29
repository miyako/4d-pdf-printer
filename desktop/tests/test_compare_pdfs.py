"""Stdlib tests and optional local macOS Poppler interoperability smoke test.

Neither mock output nor the identical-PDF smoke test is Microsoft PDF size,
fidelity, product, or Windows evidence. No installed/copied font is used.
"""

import contextlib
import copy
import io
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
import unittest
from unittest import mock

import compare_pdfs as comparator


FONT_HEADER = b"name                                 type              encoding         emb sub uni object ID\n"
FONT_SEPARATOR = b"------------------------------------ ----------------- ---------------- --- --- --- ---------\n"
FONTS = (FONT_HEADER + FONT_SEPARATOR
         + b"PRIVATE-FONT-NAME                     CID TrueType      Identity-H       yes yes yes 12 0\n")
IMAGE_HEADER = b"page num type width height color comp bpc enc interp object ID x-ppi y-ppi size ratio\n"
IMAGE_SEPARATOR = b"--------------------------------------------------------------------------------\n"
NO_IMAGES = IMAGE_HEADER + IMAGE_SEPARATOR
SVG = b'<svg xmlns="http://www.w3.org/2000/svg"><path d="M 0 0 L 10 10"/></svg>'
PPM_BYTES = b"P6\n2 1\n255\n" + bytes((255, 255, 255, 0, 0, 0))
SECRET = "PRIVATE-DOCUMENT-CONTENT"


def info(pages=1, width=595, height=842):
    return (f"Title: {SECRET}\nPages: {pages}\n"
            + "".join(f"Page {n} MediaBox: 0 0 {width} {height}\n"
                      for n in range(1, pages + 1))).encode()


class ParserTests(unittest.TestCase):
    def assert_code(self, code, function, *args):
        with self.assertRaises(comparator.ProofError) as raised:
            function(*args)
        self.assertEqual(raised.exception.code, code)

    def test_ppm_preserves_binary_header_like_pixels(self):
        for pixels in (b" \n#", b"\0\xff\r", b"###", b"\t\v\f"):
            with self.subTest(pixels=pixels):
                image = comparator.parse_ppm(b"P6\n1 1\n255\n" + pixels)
                self.assertEqual(image.pixels, pixels)

    def test_ppm_comments_and_whitespace(self):
        image = comparator.parse_ppm(
            b"P6\t# first\r\n 1 #width\n 1\t#height\r255\n" + b"\x01\x02\x03")
        self.assertEqual((image.width, image.height, image.maxval), (1, 1, 255))
        self.assertEqual(image.pixels, b"\x01\x02\x03")

    def test_ppm_crlf_and_cr_delimiter_followed_by_lf_pixel(self):
        self.assertEqual(comparator.parse_ppm(b"P6\r\n1 1\r\n255\r\n #\0").pixels, b" #\0")
        self.assertEqual(comparator.parse_ppm(b"P6\n1 1\n255\r\n#\0").pixels, b"\n#\0")

    def test_ppm_sixteen_bit_big_endian(self):
        image = comparator.parse_ppm(b"P6\n1 1\n65535\n\0\0\xff\xff\x12\x34")
        self.assertEqual(image.maxval, 65535)
        self.assertEqual(image.pixels, b"\0\0\xff\xff\x12\x34")
        self.assert_code("PPM_PARSE_FAILED", comparator.parse_ppm, b"P6\n1 1\n256\n\0\0\0\0\x01\x01")

    def test_ppm_rejects_bad_headers_payloads_and_extra_images(self):
        for data in (
            b"", b"P3\n1 1\n255\n\0\0\0", b"P6\n0 1\n255\n",
            b"P6\n-1 1\n255\n", b"P6\n1 1\n0\n\0\0\0",
            b"P6\n1 1\n65536\n\0\0\0", b"P6\n1 1\n255",
            b"P6\n1 1\n255#comment\n\0\0\0", b"P6\n1 1\n255\n\0\0",
            b"P6\n1 1\n255\n\0\0\0\n", b"P6\n1 1\n1\n\0\0\x02",
            PPM_BYTES + PPM_BYTES,
        ):
            with self.subTest(data=data[:30]):
                self.assert_code("PPM_PARSE_FAILED", comparator.parse_ppm, data)

    def test_ppm_limits(self):
        self.assert_code("PPM_HEADER_LIMIT", comparator.parse_ppm, b"P6\n#" + b"x" * 4097)
        with mock.patch.object(comparator, "MAX_PIXELS", 1):
            self.assert_code("PPM_PARSE_FAILED", comparator.parse_ppm, PPM_BYTES)
        with mock.patch.object(comparator, "MAX_RENDER_FILE_BYTES", 1):
            self.assert_code("RASTER_SIZE_LIMIT", comparator.parse_ppm, PPM_BYTES)

    def test_metrics_identity_dimensions_and_normalization(self):
        image = comparator.parse_ppm(PPM_BYTES)
        metrics = comparator.raster_metrics(image, image)
        self.assertTrue(metrics["within_reference_target"])
        self.assertFalse(metrics["reference_target_approved"])
        self.assertEqual(metrics["normalized_mean_absolute_rgb_difference"], 0)
        self.assertEqual(metrics["different_pixel_fraction"], 0)
        unmatched = comparator.raster_metrics(image, comparator.PPM(1, 2, 255, image.pixels))
        self.assertFalse(unmatched["dimensions_equal"])
        self.assertIsNone(unmatched["normalized_mean_absolute_rgb_difference"])
        self.assertIsNone(unmatched["within_reference_target"])
        first = comparator.PPM(1, 1, 255, b"\0\xff\0")
        second = comparator.PPM(1, 1, 65535, b"\0\0\xff\xff\0\0")
        self.assertEqual(comparator.raster_metrics(first, second)["normalized_mean_absolute_rgb_difference"], 0)

    def test_metrics_unapproved_references_and_exact_boundaries(self):
        baseline = comparator.PPM(200, 1, 255, bytes(600))
        changed = comparator.PPM(200, 1, 255, b"\xff\0\0" + bytes(597))
        self.assertTrue(comparator.raster_metrics(baseline, changed)["within_reference_target"])
        self.assertEqual(comparator.raster_metrics(baseline, changed)["different_pixel_fraction"], 0.005)
        shorter = comparator.PPM(199, 1, 255, bytes(597))
        self.assertFalse(comparator.raster_metrics(
            shorter, comparator.PPM(199, 1, 255, b"\xff\0\0" + bytes(594)))["within_reference_target"])
        mean_failure = comparator.raster_metrics(
            comparator.PPM(1, 1, 255, b"\0\0\0"), comparator.PPM(1, 1, 255, b"\x03\x03\x03"))
        self.assertEqual(mean_failure["different_pixel_fraction"], 0)
        self.assertFalse(mean_failure["within_reference_target"])
        exact_mean = comparator.raster_metrics(
            comparator.PPM(1, 1, 100, b"\0\0\0"), comparator.PPM(1, 1, 100, b"\x01\x01\x01"))
        self.assertTrue(exact_mean["within_reference_target"])
        exact_channel = comparator.raster_metrics(
            comparator.PPM(1, 1, 10, b"\x03\x03\x03"), comparator.PPM(1, 1, 10, b"\x04\x04\x04"))
        self.assertEqual(exact_channel["different_pixel_fraction"], 0)

    def test_pdfinfo_media_and_page_count(self):
        one = comparator.parse_pdfinfo(b"Pages: 1\nMediaBox: -10 -20 585 822\n")
        self.assertEqual(one, {"page_count": 1, "media_dimensions_pt": [[595.0, 842.0]]})
        self.assertEqual(comparator.parse_pdfinfo(info(2))["page_count"], 2)
        self.assertTrue(comparator.media_equal(one, comparator.parse_pdfinfo(info(width=595.1))))
        self.assertFalse(comparator.media_equal(one, comparator.parse_pdfinfo(info(width=595.101))))
        self.assertFalse(comparator.media_equal(one, comparator.parse_pdfinfo(info(2))))

    def test_pdfinfo_errors_are_not_guessed(self):
        for data in (b"Pages: 0\n", b"Pages: 17\n", b"Pages: 1\n",
                     b"Pages: 2\nMediaBox: 0 0 595 842\n",
                     b"Pages: 1\nMediaBox: 0 0 0 842\n",
                     b"Pages: 1\nMediaBox: 0 0 NaN 842\n",
                     b"Pages: 1\nMediaBox: 0 0 1e999 842\n",
                     b"Pages: 1\nMediaBox: 0 0 595 842\nMediaBox: 0 0 595 842\n"):
            with self.subTest(data=data):
                self.assert_code("PDFINFO_PARSE_FAILED", comparator.parse_pdfinfo, data)

    def test_text_normalizes_all_whitespace_and_requires_expected_strings(self):
        metrics = comparator.text_metrics("a\tb\nc\f", " a b  c ", ["a\nb", "missing"])
        self.assertTrue(metrics["equal"])
        self.assertEqual(metrics["baseline_expected_present"], 1)
        self.assertEqual(metrics["worker_expected_present"], 1)
        self.assertNotIn("missing", json.dumps(metrics))
        self.assertFalse(comparator.text_metrics("abc", "ABC", [])["equal"])

    def test_fonts_counts_only_including_unembedded(self):
        self.assertEqual(comparator.parse_fonts(FONTS),
                         {"count": 1, "embedded_count": 1, "subset_count": 1,
                          "unicode_count": 1, "all_embedded": True})
        metrics = comparator.parse_fonts(FONTS.replace(b"yes yes yes", b"no no no"))
        self.assertFalse(metrics["all_embedded"])
        self.assertNotIn("PRIVATE-FONT", json.dumps(metrics))
        self.assertEqual(comparator.parse_fonts(FONT_HEADER + FONT_SEPARATOR)["count"], 0)
        self.assert_code("FONTS_PARSE_FAILED", comparator.parse_fonts, b"")
        self.assert_code("FONTS_PARSE_FAILED", comparator.parse_fonts, FONTS.replace(b"yes yes yes", b"??? yes yes"))

    def test_image_physical_area_not_pixel_count(self):
        media = comparator.parse_pdfinfo(info(width=600, height=800))
        row = b"1 0 image 600 800 rgb 3 8 jpeg no 12 0 72 72 2K 1%\n"
        full = comparator.parse_images(NO_IMAGES + row, media)
        self.assertEqual(full["fullpage_count"], 1)
        self.assertEqual(full["maximum_page_area_fraction"], 1)
        small = comparator.parse_images(NO_IMAGES + row.replace(b"72 72", b"300 300"), media)
        self.assertEqual(small["count"], 1)
        self.assertEqual(small["fullpage_count"], 0)
        inline = row.replace(b"12 0", b"[inline]")
        self.assertEqual(comparator.parse_images(NO_IMAGES + inline, media)["count"], 1)
        self.assertEqual(comparator.parse_images(NO_IMAGES, media)["count"], 0)

    def test_image_parse_failures(self):
        media = comparator.parse_pdfinfo(info())
        row = b"1 0 image 600 800 rgb 3 8 jpeg no 12 0 72 72 2K 1%\n"
        for data in (b"", NO_IMAGES + b"garbage\n",
                     NO_IMAGES + row.replace(b"72 72", b"0 72"),
                     NO_IMAGES + row.replace(b"72 72", b"nan 72"),
                     NO_IMAGES + row.replace(b"1 0 image", b"2 0 image"),
                     NO_IMAGES + row.replace(b"600 800", b"-1 800")):
            with self.subTest(data=data):
                self.assert_code("IMAGES_PARSE_FAILED", comparator.parse_images, data, media)

    def test_svg_excludes_font_definitions_clips_and_hidden_paths(self):
        svg = b"""<svg xmlns="http://www.w3.org/2000/svg">
          <defs><symbol id="glyph"><path d="M0 0L1 1"/></symbol>
            <path d="M0 0L1 1"/></defs>
          <clipPath><path d="M0 0L1 1"/></clipPath>
          <use href="#glyph"/>
          <g style="display:none"><path d="M0 0L1 1"/></g>
          <path visibility="hidden" d="M0 0L1 1"/>
          <path opacity="0" d="M0 0L1 1"/>
          <path fill="none" stroke="none" d="M0 0L1 1"/>
          <path d="M0 0"/>
          <path d="M0 0L1 1"/><path fill="none" stroke="black" d="M0 0C1 1 2 2 3 3"/>
        </svg>"""
        self.assertEqual(comparator.count_svg_paths(svg), 2)
        self.assertEqual(comparator.count_svg_paths(SVG), 1)
        self.assert_code("SVG_PARSE_FAILED", comparator.count_svg_paths, b"<svg>")
        self.assert_code("SVG_PARSE_FAILED", comparator.count_svg_paths, b"<html/>")
        self.assert_code("SVG_PARSE_FAILED", comparator.count_svg_paths,
                         b'<!DOCTYPE svg [<!ENTITY x "secret">]><svg>&x;</svg>')
        with mock.patch.object(comparator, "MAX_SVG_BYTES", 2):
            self.assert_code("SVG_SIZE_LIMIT", comparator.count_svg_paths, SVG)

    def test_aggregate_is_weighted_and_reports_every_fixture_including_growth(self):
        fixtures = [{"id": name, "baseline": {"bytes": baseline}, "worker": {"bytes": worker}}
                    for name, baseline, worker in (("text", 100, 120), ("vectors", 100, 100),
                                                  ("mixed", 800, 680))]
        result = comparator.size_summary(fixtures)
        self.assertTrue(result["within_reference_target"])
        self.assertFalse(result["reference_target_approved"])
        self.assertEqual(result["worker_baseline_size_ratio"], 0.9)
        self.assertEqual([item["worker_baseline_size_ratio"] for item in fixtures], [1.2, 1, 0.85])
        fixtures[2]["worker"]["bytes"] += 1
        self.assertFalse(comparator.size_summary(fixtures)["within_reference_target"])
        fixtures[2]["baseline"]["bytes"] = 0
        self.assert_code("EMPTY_PDF", comparator.size_summary, fixtures)


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        # Always beneath this project, never the system temporary directory.
        workspace = tempfile.TemporaryDirectory(
            prefix=".compare-pdfs-test-", dir=Path(__file__).absolute().parent)
        self.addCleanup(workspace.cleanup)
        self.root = Path(workspace.name)
        self.manifest_path = self.root / "private-manifest.json"
        self.output = self.root / "report.json"
        self.render = self.root / "private-renders"
        self.tools = self.root / "tools"
        self.tools.mkdir()
        for name in comparator.TOOLS:
            (self.tools / (name + (".exe" if os.name == "nt" else ""))).write_bytes(b"mock executable")
        self.manifest = {
            "schema": "4d-pdf-printer.proof-comparison", "version": 1, "fixtures": [],
        }
        for name in comparator.FIXTURE_IDS:
            fixture = {"id": name, "expected_text": [SECRET], "minimum_vector_paths": 1}
            for role, size in (("baseline", 100), ("worker", 80)):
                path = self.root / f"{name}-{role}.pdf"
                path.write_bytes(b"%PDF-" + b" " * (size - 5))
                fixture[role] = str(path)
            self.manifest["fixtures"].append(fixture)
        self.save_manifest()
        self.calls = []
        self.behavior = {}

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def runner(self, executable, arguments, *, tool, stage, render_directory=None):
        self.calls.append((tool, stage, list(arguments)))
        if stage == "tools":
            return f"{tool} version 25.06.0\n{SECRET}: {self.root}\n".encode()
        pdf = next((str(argument) for argument in arguments if str(argument).endswith(".pdf")), "")
        role = "worker" if "-worker.pdf" in pdf else "baseline"
        override = self.behavior.get((tool, role))
        if isinstance(override, Exception):
            raise override
        if callable(override):
            return override(arguments)
        if override is not None:
            result = override
        else:
            result = {"pdfinfo": info(), "pdftotext": (SECRET + "\n").encode(),
                      "pdffonts": FONTS, "pdfimages": NO_IMAGES,
                      "pdftocairo": SVG, "pdftoppm": PPM_BYTES}[tool]
        if tool == "pdftocairo":
            Path(arguments[-1]).write_bytes(result)
            return b""
        if tool == "pdftoppm":
            Path(str(arguments[-1]) + ".ppm").write_bytes(result)
            return b""
        return result

    def compare(self, **kwargs):
        options = {"tools_root": self.tools, "render_directory": self.render, "runner": self.runner}
        options.update(kwargs)
        return comparator.compare(self.manifest_path, self.output, **options)

    def assert_private(self, report):
        serialized = json.dumps(report)
        for forbidden in (str(self.root), SECRET, "PRIVATE-FONT-NAME", "text-baseline.pdf",
                          "text-worker.pdf", "Traceback"):
            self.assertNotIn(forbidden, serialized)

    def test_manifest_validation(self):
        valid = copy.deepcopy(self.manifest)
        valid["fixtures"].reverse()
        self.assertEqual([item["id"] for item in comparator.validate_manifest(valid)],
                         list(comparator.FIXTURE_IDS))
        mutations = (
            lambda value: value.update(extra=True),
            lambda value: value.update(schema="wrong"),
            lambda value: value.update(version=True),
            lambda value: value.update(version=2),
            lambda value: value["fixtures"].pop(),
            lambda value: value["fixtures"][0].update(id="unknown"),
            lambda value: value["fixtures"][0].update(id="vectors"),
            lambda value: value["fixtures"][0].update(extra=True),
            lambda value: value["fixtures"][0].update(minimum_vector_paths=True),
            lambda value: value["fixtures"][0].update(minimum_vector_paths=0),
            lambda value: value["fixtures"][0].update(expected_text="not an array"),
            lambda value: value["fixtures"][0].update(expected_text=[]),
            lambda value: value["fixtures"][0].update(expected_text=[1]),
            lambda value: value["fixtures"][0].update(expected_text=["  "]),
            lambda value: value["fixtures"][0].update(baseline="relative.pdf"),
            lambda value: value["fixtures"][0].update(worker=str(self.root) + "\0.pdf"),
        )
        for mutate in mutations:
            value = copy.deepcopy(self.manifest)
            mutate(value)
            with self.subTest(mutation=mutate):
                with self.assertRaises(comparator.ProofError):
                    comparator.validate_manifest(value)

    def test_empty_expected_text_cannot_disable_lost_text_checks(self):
        self.behavior = {("pdftotext", role): b"" for role in ("baseline", "worker")}
        for index, name in enumerate(comparator.FIXTURE_IDS):
            with self.subTest(fixture=name):
                original = self.manifest["fixtures"][index]["expected_text"]
                self.manifest["fixtures"][index]["expected_text"] = []
                self.save_manifest()
                code, report = self.compare()
                self.assertEqual(code, 2)
                self.assertEqual(report["result"], "error")
                self.assertEqual(report["errors"][0]["code"], "INVALID_MANIFEST")
                self.assertFalse(self.calls)
                self.assertFalse(self.render.exists())
                self.assert_private(report)
                self.output.unlink()
                self.manifest["fixtures"][index]["expected_text"] = original

    def test_manifest_bad_json_duplicate_keys_and_limits_produce_private_reports(self):
        self.manifest_path.write_text('{"schema":"secret","schema":"duplicate"}', encoding="utf-8")
        code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(report["errors"][0]["code"], "MANIFEST_DUPLICATE_KEY")
        self.assertTrue(self.output.is_file())
        self.assert_private(report)
        self.output.unlink()
        self.manifest_path.write_bytes(b"\xff")
        self.assertEqual(self.compare()[1]["errors"][0]["code"], "INVALID_MANIFEST_JSON")
        self.output.unlink()
        self.save_manifest()
        with mock.patch.object(comparator, "MAX_MANIFEST_BYTES", 1):
            self.assertEqual(self.compare()[1]["errors"][0]["code"], "FILE_SIZE_LIMIT")

    def test_mock_completed_measurement_private_reports_and_pdf_integrity(self):
        before = {path: path.read_bytes() for path in self.root.glob("*.pdf")}
        code, report = self.compare()
        self.assertEqual(code, 0)
        self.assertEqual(report["result"], "completed")
        self.assertFalse(report["reference_targets"]["approved"])
        self.assertTrue(report["manual_review_required"])
        self.assertFalse(report["productUsable"])
        self.assertEqual(report["critical_region_manual_review"], "unmeasured")
        self.assertEqual(len(report["fixtures"]), 3)
        self.assertEqual(len(report["tools"]), 6)
        self.assertEqual(report, json.loads(self.output.read_text(encoding="utf-8")))
        self.assertEqual({path: path.read_bytes() for path in before}, before)
        for fixture in report["fixtures"]:
            for role in ("baseline", "worker"):
                self.assertTrue(fixture[role]["sha256_unchanged"])
                self.assertEqual(fixture[role]["vector_path_counts"], [1])
        self.assertEqual(len(list(self.render.glob("*.ppm"))), 6)
        self.assertEqual(len(list(self.render.glob("*.svg"))), 6)
        if os.name != "nt":
            self.assertEqual(stat.S_IMODE(self.render.stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE(self.output.stat().st_mode), 0o600)
        for tool, stage, arguments in self.calls:
            if tool == "pdftoppm" and stage == "raster":
                self.assertEqual(arguments[:2], ["-r", "150"])
            if tool == "pdftocairo" and stage == "vectors":
                self.assertEqual(arguments[:5], ["-svg", "-f", "1", "-l", "1"])
        self.assert_private(report)

    def test_each_gate_failure_is_not_green(self):
        variants = (
            ("pdfinfo", info(width=596), "media"),
            ("pdfinfo", info(2), "media"),
            ("pdftotext", b"different private text", "text"),
            ("pdffonts", FONTS.replace(b"yes yes yes", b"no yes yes"), "fonts"),
            ("pdffonts", FONT_HEADER + FONT_SEPARATOR, "fonts"),
            ("pdfimages", NO_IMAGES + b"1 0 image 595 842 rgb 3 8 jpeg no 12 0 72 72 2K 1%\n", "images"),
            ("pdftocairo", b"<svg/>", "vectors"),
        )
        for tool, data, gate in variants:
            with self.subTest(tool=tool, gate=gate):
                self.behavior = {(tool, "worker"): data}
                code, report = self.compare()
                self.assertEqual(code, 2)
                self.assertEqual(report["result"], "failed")
                self.assertTrue(any(stage["stage"] == gate and stage["status"] == "failed"
                                    for fixture in report["fixtures"] for stage in fixture["stages"]))
                self.assert_private(report)
                self.output.unlink()
                shutil.rmtree(self.render)

    def test_expected_text_required_even_when_extracted_text_matches(self):
        self.behavior = {("pdftotext", role): b"same but missing expected content"
                         for role in ("baseline", "worker")}
        self.assertEqual(self.compare()[1]["result"], "failed")

    def test_size_reference_miss_does_not_abort_or_select_winners(self):
        for index, size in enumerate((120, 80, 80)):
            Path(self.manifest["fixtures"][index]["worker"]).write_bytes(b"x" * size)
        code, report = self.compare()
        self.assertEqual(code, 0)
        self.assertEqual(report["result"], "completed")
        self.assertFalse(report["size"]["within_reference_target"])
        self.assertFalse(report["size"]["reference_target_approved"])
        self.assertTrue(report["manual_review_required"])
        self.assertFalse(report["productUsable"])
        self.assertAlmostEqual(report["size"]["worker_baseline_size_ratio"], 280 / 300)
        self.assertEqual([item["worker_baseline_size_ratio"] for item in report["fixtures"]],
                         [1.2, 0.8, 0.8])
        self.assertEqual(report["fixtures"][0]["size_change"], "larger")
        self.assertEqual(len(list(self.render.glob("*.ppm"))), 6)

    def test_larger_output_and_pixel_reference_misses_complete_for_review(self):
        for fixture in self.manifest["fixtures"]:
            Path(fixture["worker"]).write_bytes(b"x" * 150)
        self.behavior = {("pdftoppm", "worker"): b"P6\n2 1\n255\n" + bytes(6)}
        code, report = self.compare()
        self.assertEqual(code, 0)
        self.assertEqual(report["result"], "completed")
        self.assertEqual(report["size"]["worker_baseline_size_ratio"], 1.5)
        self.assertEqual(report["size"]["size_change"], "larger")
        self.assertFalse(report["size"]["size_improvement_observed"])
        self.assertTrue(report["manual_review_required"])
        self.assertFalse(report["productUsable"])
        for fixture in report["fixtures"]:
            raster = fixture["observations"][0]
            self.assertEqual(raster["stage"], "raster")
            self.assertEqual(raster["status"], "observed")
            self.assertFalse(raster["reference_target_approved"])
            self.assertFalse(raster["within_reference_target"])
            self.assertEqual(raster["normalized_mean_absolute_rgb_difference"], 0.5)
            self.assertEqual(raster["different_pixel_fraction"], 0.5)
        self.assertEqual(len(list(self.render.glob("*.svg"))), 6)
        self.assert_private(report)

    def test_raster_dimensions_are_explicit_uncomparable_observations(self):
        self.behavior = {("pdftoppm", "worker"): b"P6\n1 1\n255\n\0\0\0"}
        code, report = self.compare()
        self.assertEqual(code, 0)
        self.assertEqual(report["result"], "completed")
        for fixture in report["fixtures"]:
            raster = fixture["observations"][0]
            self.assertFalse(raster["dimensions_equal"])
            self.assertIsNone(raster["normalized_mean_absolute_rgb_difference"])
            self.assertIsNone(raster["within_reference_target"])
        self.assertTrue(report["manual_review_required"])
        self.assertFalse(report["productUsable"])

    def test_invalid_raster_remains_an_actual_error(self):
        self.behavior = {("pdftoppm", "worker"): b"P6\n1 1\n255\n\0"}
        code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(report["result"], "error")
        self.assertTrue(all(error["code"] == "PPM_PARSE_FAILED" for error in report["errors"]))

    def test_missing_tools_are_blocked_with_all_ratios_and_integrity(self):
        with mock.patch.object(comparator.shutil, "which", return_value=None):
            code, report = self.compare(tools_root=None)
        self.assertEqual(code, 2)
        self.assertEqual(report["result"], "blocked")
        self.assertEqual(len(report["errors"]), 6)
        self.assertTrue(all(error["code"] == "TOOL_MISSING" for error in report["errors"]))
        self.assertEqual(len([item["worker_baseline_size_ratio"] for item in report["fixtures"]]), 3)
        self.assertFalse(self.render.exists())
        self.assert_private(report)

    def test_explicit_tools_root_never_falls_back_to_path(self):
        next(self.tools.iterdir()).unlink()
        with mock.patch.object(comparator.shutil, "which") as which:
            code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(report["result"], "blocked")
        which.assert_not_called()

    def test_path_discovery_and_tool_versions_are_fingerprinted_not_logged(self):
        def find(name):
            return str(self.tools / (name + (".exe" if os.name == "nt" else "")))
        with mock.patch.object(comparator.shutil, "which", side_effect=find):
            code, report = self.compare(tools_root=None)
        self.assertEqual(code, 0)
        identity = comparator.fingerprint(next(self.tools.iterdir()))
        for tool in report["tools"].values():
            self.assertEqual(tool["sha256"], identity["sha256"])
            self.assertEqual(tool["version"], "25.06.0")
        self.assert_private(report)

    def test_tool_failure_and_parse_failure_never_guess_comparison(self):
        for output in (comparator.ProofError("TOOL_FAILED", "text", tool="pdftotext"),
                       b"\xff"):
            with self.subTest(output=type(output)):
                self.behavior = {("pdftotext", "worker"): output}
                code, report = self.compare()
                self.assertEqual(code, 2)
                self.assertEqual(report["result"], "error")
                self.assertEqual(len(report["errors"]), 3)
                self.assertFalse(any(stage["stage"] == "text"
                                     for fixture in report["fixtures"] for stage in fixture["stages"]))
                self.assert_private(report)
                self.output.unlink()
                shutil.rmtree(self.render)

    def test_version_failure_is_error_and_does_not_create_renders(self):
        with mock.patch.object(self, "runner", return_value=b"unparseable private banner"):
            code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(report["result"], "error")
        self.assertTrue(all(item["code"] == "TOOL_VERSION_PARSE_FAILED" for item in report["errors"]))
        self.assertFalse(self.render.exists())
        self.assert_private(report)

    def test_mutation_detected_even_after_tool_failure(self):
        target = Path(self.manifest["fixtures"][0]["worker"])

        def mutate(arguments):
            target.write_bytes(b"changed input")
            raise comparator.ProofError("TOOL_FAILED", "text", tool="pdftotext")

        self.behavior = {("pdftotext", "worker"): mutate}
        code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(report["result"], "error")
        self.assertFalse(report["fixtures"][0]["worker"]["sha256_unchanged"])
        self.assertIn("INPUT_CHANGED", [error["code"] for error in report["errors"]])

    def test_report_and_render_destinations_never_overwrite(self):
        self.output.write_bytes(b"existing private report")
        code, _ = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(self.output.read_bytes(), b"existing private report")
        self.assertFalse(self.calls)
        self.output.unlink()
        self.render.mkdir()
        sentinel = self.render / "keep-private"
        sentinel.write_bytes(b"keep")
        code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(sentinel.read_bytes(), b"keep")
        self.assertTrue(self.output.is_file())
        self.assertEqual(report["errors"][0]["code"], "DESTINATION_EXISTS")

    def test_collisions_rejected_before_render_or_tool_execution(self):
        cases = (
            {"render_directory": self.output},
            {"render_directory": self.root},
            {"render_directory": Path(self.manifest["fixtures"][0]["baseline"])},
            {"tools_root": self.root},
        )
        for options in cases:
            with self.subTest(options=options):
                code, report = self.compare(**options)
                self.assertEqual(code, 2)
                self.assertEqual(report["errors"][0]["code"], "PATH_COLLISION")
                self.assertFalse(self.calls)
                self.assertFalse(self.render.exists())
                if self.output.exists():
                    self.output.unlink()
        # A nonexistent input must not become an output file on error.
        self.manifest["fixtures"][0]["worker"] = str(self.output.with_suffix(".pdf"))
        self.save_manifest()
        code, report = comparator.compare(self.manifest_path, self.output.with_suffix(".pdf"),
                                           tools_root=self.tools, render_directory=self.render)
        self.assertEqual(code, 2)
        self.assertEqual(report["errors"][0]["code"], "PATH_COLLISION")
        self.assertFalse(self.output.with_suffix(".pdf").exists())

    def test_duplicate_inputs_and_hardlinks_rejected(self):
        original = self.manifest["fixtures"][0]["baseline"]
        self.manifest["fixtures"][0]["worker"] = original
        self.save_manifest()
        self.assertEqual(self.compare()[1]["errors"][0]["code"], "PATH_COLLISION")
        self.output.unlink()
        link = self.root / "hardlink.pdf"
        os.link(original, link)
        self.manifest["fixtures"][0]["worker"] = str(link)
        self.save_manifest()
        self.assertEqual(self.compare()[1]["errors"][0]["code"], "PATH_COLLISION")

    def test_symlink_inputs_parents_tools_and_outputs_rejected(self):
        alias = self.root / "alias"
        try:
            alias.symlink_to(self.tools, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("Host does not permit test symlinks")
        code, report = self.compare(tools_root=alias)
        self.assertEqual(code, 2)
        self.assertEqual(report["errors"][0]["code"], "UNSAFE_PATH")
        self.output.unlink()
        code, report = comparator.compare(self.manifest_path, alias / "new.json",
                                           tools_root=self.tools, render_directory=self.render)
        self.assertEqual(code, 2)
        self.assertEqual(report["errors"][0]["code"], "UNSAFE_PATH")
        self.assertFalse((self.tools / "new.json").exists())
        baseline = Path(self.manifest["fixtures"][0]["baseline"])
        link = self.root / "linked.pdf"
        link.symlink_to(baseline)
        self.manifest["fixtures"][0]["baseline"] = str(link)
        self.save_manifest()
        self.assertEqual(self.compare()[1]["errors"][0]["code"], "UNSAFE_PATH")

    def test_reparse_attribute_rejected(self):
        real_lstat = comparator.os.lstat

        def reparse(path, *args, **kwargs):
            if Path(path) == self.tools:
                return mock.Mock(st_mode=stat.S_IFDIR, st_file_attributes=0x400)
            return real_lstat(path, *args, **kwargs)

        with mock.patch.object(comparator.os, "lstat", side_effect=reparse):
            code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(report["errors"][0]["code"], "UNSAFE_PATH")

    def test_report_creation_race_does_not_overwrite(self):
        original = comparator.create_report_file

        def race(path):
            path.write_bytes(b"concurrent private report")
            return original(path)

        with mock.patch.object(comparator, "create_report_file", side_effect=race):
            code, report = self.compare()
        self.assertEqual(code, 2)
        self.assertEqual(self.output.read_bytes(), b"concurrent private report")
        self.assertEqual(report["result"], "error")
        self.assertFalse(self.calls)

    def test_generated_size_budget_and_symlinks(self):
        self.render.mkdir()
        path = self.render / "oversized.ppm"
        path.write_bytes(b"12345")
        with mock.patch.object(comparator, "MAX_RENDER_FILE_BYTES", 4):
            with self.assertRaises(comparator.ProofError) as raised:
                comparator.check_render_budget(self.render)
        self.assertEqual(raised.exception.code, "RENDER_SIZE_LIMIT")
        with mock.patch.object(comparator, "MAX_RENDER_TOTAL_BYTES", 4):
            with self.assertRaises(comparator.ProofError) as raised:
                comparator.check_render_budget(self.render)
        self.assertEqual(raised.exception.code, "RENDER_SIZE_LIMIT")

    def test_console_arguments_never_echo_private_paths(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            code = comparator.main(["--unknown", str(self.root), SECRET])
        self.assertEqual(code, 2)
        self.assertNotIn(str(self.root), output.getvalue())
        self.assertNotIn(SECRET, output.getvalue())
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            code = comparator.main(["--manifest", str(self.root / "missing.json"),
                                    "--output", str(self.output)])
        self.assertEqual(code, 2)
        self.assertNotIn(str(self.root), output.getvalue())
        self.assertTrue(self.output.exists())


class SubprocessTests(unittest.TestCase):
    def process(self, stdout=b"", stderr=b"", returncode=0):
        process = mock.Mock()
        process.stdout = io.BytesIO(stdout)
        process.stderr = io.BytesIO(stderr)
        process.poll.return_value = returncode
        process.returncode = returncode
        return process

    def test_array_arguments_and_locale(self):
        process = self.process(b"only stdout")
        with mock.patch.object(comparator.subprocess, "Popen", return_value=process) as popen:
            result = comparator.run_tool("pdfinfo", ["private path.pdf"], tool="pdfinfo", stage="media")
        self.assertEqual(result, b"only stdout")
        arguments, options = popen.call_args
        self.assertEqual(arguments[0], ["pdfinfo", "private path.pdf"])
        self.assertFalse(options["shell"])
        self.assertEqual(options["env"]["LC_ALL"], "C")
        self.assertEqual(options["env"]["LANG"], "C")

    def test_success_exit_with_stderr_is_not_green_or_logged(self):
        process = self.process(b"apparently valid", f"{SECRET} /private/path".encode())
        with mock.patch.object(comparator.subprocess, "Popen", return_value=process):
            with self.assertRaises(comparator.ProofError) as raised:
                comparator.run_tool("pdfinfo", [], tool="pdfinfo", stage="media")
        self.assertEqual(str(raised.exception), "TOOL_DIAGNOSTIC")
        self.assertNotIn(SECRET, json.dumps(raised.exception.record()))

    def test_version_stderr_consumed_in_memory(self):
        process = self.process(stderr=b"pdfinfo version 25.06.0\n")
        with mock.patch.object(comparator.subprocess, "Popen", return_value=process):
            self.assertIn(b"25.06.0", comparator.run_tool("pdfinfo", ["-v"],
                                                        tool="pdfinfo", stage="tools"))

    def test_tool_failure_is_typed_and_private(self):
        process = self.process(f"{SECRET} stdout".encode(), f"{SECRET} stderr".encode(), 1)
        with mock.patch.object(comparator.subprocess, "Popen", return_value=process):
            with self.assertRaises(comparator.ProofError) as raised:
                comparator.run_tool("pdfinfo", [], tool="pdfinfo", stage="media")
        self.assertEqual(str(raised.exception), "TOOL_FAILED")
        self.assertNotIn(SECRET, json.dumps(raised.exception.record()))

    def test_output_limits_cover_both_streams(self):
        for stdout, stderr in ((b"x" * 100, b""), (b"", b"x" * 100)):
            with self.subTest(stream="stdout" if stdout else "stderr"):
                process = self.process(stdout, stderr)
                with mock.patch.object(comparator.subprocess, "Popen", return_value=process), \
                        mock.patch.object(comparator, "MAX_TOOL_OUTPUT_BYTES", 10):
                    with self.assertRaises(comparator.ProofError) as raised:
                        comparator.run_tool("pdfinfo", [], tool="pdfinfo", stage="media")
                self.assertEqual(raised.exception.code, "TOOL_OUTPUT_LIMIT")

    def test_timeout_kills_only_started_process(self):
        process = self.process()
        process.poll.side_effect = lambda: 0 if process.kill.called else None
        with mock.patch.object(comparator.subprocess, "Popen", return_value=process), \
                mock.patch.object(comparator.time, "monotonic", side_effect=(0, 61)):
            with self.assertRaises(comparator.ProofError) as raised:
                comparator.run_tool("pdfinfo", [], tool="pdfinfo", stage="media")
        self.assertEqual(raised.exception.code, "TOOL_TIMEOUT")
        process.kill.assert_called_once_with()
        process.wait.assert_called_once_with()

    def test_start_failure_hides_os_error_paths(self):
        with mock.patch.object(comparator.subprocess, "Popen",
                               side_effect=OSError(f"{SECRET} /private/tool/path")):
            with self.assertRaises(comparator.ProofError) as raised:
                comparator.run_tool("pdfinfo", [], tool="pdfinfo", stage="media")
        self.assertEqual(str(raised.exception), "TOOL_START_FAILED")
        self.assertNotIn(SECRET, json.dumps(raised.exception.record()))


@unittest.skipUnless(sys.platform == "darwin", "Local macOS tool interoperability only")
class ActualPopplerInteroperabilityTests(unittest.TestCase):
    @staticmethod
    def original_tiny_pdf():
        """One original Type3 mark mapped to selectable 'A', plus a stroked box."""
        def stream(payload):
            return (f"<< /Length {len(payload)} >>\nstream\n".encode("ascii")
                    + payload + b"\nendstream")

        contents = (b"q 0.1 0.3 0.7 RG 2 w\n"
                    b"12 12 m 84 12 l 84 40 l 12 40 l h S Q\n"
                    b"BT /F1 24 Tf 20 55 Td (A) Tj ET\n")
        # Deliberately original polygon, not an outline copied from a font.
        glyph = (b"650 0 0 0 600 700 d1\n"
                 b"0 0 m 180 700 l 400 460 l 600 700 l 520 0 l h f\n")
        unicode_map = b"""/CIDInit /ProcSet findresource begin
12 dict begin
begincmap
/CIDSystemInfo << /Registry (OriginalProof) /Ordering (OriginalGlyph) /Supplement 0 >> def
/CMapName /OriginalProofMap def
/CMapType 2 def
1 begincodespacerange
<00> <FF>
endcodespacerange
1 beginbfchar
<41> <0041>
endbfchar
endcmap
CMapName currentdict /CMap defineresource pop
end
end
"""
        objects = [
            b"<< /Type /Catalog /Pages 2 0 R >>",
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 96 96] "
             b"/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>"),
            stream(contents),
            (b"<< /Type /Font /Subtype /Type3 /Name /OriginalProofGlyph "
             b"/FontBBox [0 0 600 700] /FontMatrix [0.001 0 0 0.001 0 0] "
             b"/FirstChar 65 /LastChar 65 /Widths [650] "
             b"/Encoding << /Type /Encoding /Differences [65 /proofmark] >> "
             b"/CharProcs << /proofmark 6 0 R >> /Resources << >> /ToUnicode 7 0 R >>"),
            stream(glyph),
            stream(unicode_map),
        ]
        pdf = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offsets = []
        for number, body in enumerate(objects, 1):
            offsets.append(len(pdf))
            pdf.extend(f"{number} 0 obj\n".encode("ascii") + body + b"\nendobj\n")
        xref = len(pdf)
        pdf.extend(f"xref\n0 {len(objects) + 1}\n0000000000 65535 f \n".encode("ascii"))
        for offset in offsets:
            pdf.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
        pdf.extend((f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
                    f"startxref\n{xref}\n%%EOF\n").encode("ascii"))
        return bytes(pdf)

    def existing_poppler_root(self):
        # Homebrew's public command symlinks are not document/render inputs.
        # Pass the resolved existing bin directory, retaining the comparator's
        # strict rejection of symlinks/reparse paths at its file boundary.
        for candidate in (shutil.which("pdfinfo"), "/opt/homebrew/bin/pdfinfo"):
            if candidate is None:
                continue
            try:
                root = Path(candidate).resolve(strict=True).parent
                if all((root / tool).is_file() for tool in comparator.TOOLS):
                    return root
            except (OSError, RuntimeError):
                continue
        self.skipTest("Existing local Poppler tool set absent; interoperability unmeasured")

    def test_actual_poppler_identical_pdfs_complete_without_size_improvement(self):
        tools_root = self.existing_poppler_root()
        with tempfile.TemporaryDirectory(
                prefix=".compare-pdfs-poppler-", dir=Path(__file__).absolute().parent) as directory:
            root = Path(directory)
            pdf = self.original_tiny_pdf()
            self.assertLess(len(pdf), 4096)
            manifest = {
                "schema": "4d-pdf-printer.proof-comparison", "version": 1, "fixtures": [],
            }
            for name in comparator.FIXTURE_IDS:
                fixture = {"id": name, "expected_text": ["A"], "minimum_vector_paths": 1}
                for role in ("baseline", "worker"):
                    path = root / f"{name}-{role}.pdf"
                    path.write_bytes(pdf)
                    fixture[role] = str(path)
                manifest["fixtures"].append(fixture)
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            output = root / "report.json"
            console = io.StringIO()
            # Only the timeout is shortened; every tool is real, and all
            # structural validation and observational references remain unchanged.
            with mock.patch.object(comparator, "TOOL_TIMEOUT_SECONDS", 5), \
                    contextlib.redirect_stdout(console), contextlib.redirect_stderr(console):
                code, report = comparator.compare(
                    manifest_path, output, tools_root=tools_root,
                    render_directory=root / "renders")
            self.assertEqual(console.getvalue(), "")
            self.assertEqual(report["errors"], [], "Actual Poppler inspection must not error")
            self.assertEqual(code, 0)
            self.assertEqual(report["result"], "completed")
            self.assertEqual(report["size"]["worker_baseline_size_ratio"], 1.0)
            self.assertFalse(report["size"]["within_reference_target"])
            self.assertFalse(report["size"]["reference_target_approved"])
            self.assertFalse(report["size"]["size_improvement_observed"])
            self.assertEqual(report["size"]["size_change"], "identical")
            self.assertTrue(report["manual_review_required"])
            self.assertFalse(report["productUsable"])
            self.assertEqual(report["critical_region_manual_review"], "unmeasured")
            self.assertEqual(len(report["tools"]), len(comparator.TOOLS))
            self.assertTrue(all(tool["status"] == "passed" for tool in report["tools"].values()))
            self.assertEqual(report["stages"], [])
            self.assertEqual(report["observations"][0]["stage"], "aggregate_size")
            self.assertEqual(report["observations"][0]["status"], "observed")
            self.assertEqual(len(report["fixtures"]), 3)
            for fixture in report["fixtures"]:
                self.assertEqual(fixture["worker_baseline_size_ratio"], 1.0)
                self.assertEqual({stage["stage"] for stage in fixture["stages"]},
                                 {"media", "text", "fonts", "images", "vectors", "integrity"})
                self.assertTrue(all(stage["status"] == "passed" for stage in fixture["stages"]))
                text = next(stage for stage in fixture["stages"] if stage["stage"] == "text")
                self.assertTrue(text["equal"])
                self.assertEqual(text["baseline_expected_present"], 1)
                self.assertEqual(text["worker_expected_present"], 1)
                raster = next(stage for stage in fixture["observations"] if stage["stage"] == "raster")
                self.assertTrue(raster["dimensions_equal"])
                self.assertEqual(raster["normalized_mean_absolute_rgb_difference"], 0)
                self.assertEqual(raster["different_pixel_fraction"], 0)
                for role in ("baseline", "worker"):
                    self.assertEqual(fixture[role]["page_count"], 1)
                    self.assertEqual(fixture[role]["media_dimensions_pt"], [[96.0, 96.0]])
                    self.assertEqual(fixture[role]["fonts"]["count"], 1)
                    self.assertEqual(fixture[role]["fonts"]["embedded_count"], 1)
                    self.assertTrue(fixture[role]["fonts"]["all_embedded"])
                    self.assertEqual(fixture[role]["images"]["count"], 0)
                    self.assertGreaterEqual(fixture[role]["vector_path_counts"][0], 1)
                    self.assertTrue(fixture[role]["sha256_unchanged"])
                    self.assertEqual(fixture[role]["bytes"], len(pdf))
            persisted = output.read_text(encoding="utf-8")
            self.assertEqual(json.loads(persisted), report)
            self.assertTrue(str(root) not in persisted and str(tools_root) not in persisted,
                            "Reports must not expose local paths")
            self.assertTrue("OriginalProofGlyph" not in persisted,
                            "Reports must not expose font names")


if __name__ == "__main__":
    unittest.main()
