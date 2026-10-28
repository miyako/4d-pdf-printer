#!/usr/bin/env python3
"""Read-only, local milestone-A proof comparator (Python stdlib + existing Poppler).

Usage: python compare_pdfs.py --manifest PRIVATE.json --output NEW-REPORT.json
       [--tools-root EXISTING-POPPLER-BIN] [--render-directory NEW-PRIVATE-DIR]

Reports contain no document text, font names, input paths, or tool diagnostics.
Every fixture requires at least one nonblank expected_text string.
Renders are retained for local critical-region review, which is NOT measured here.
Use private parent directories; Windows parent ACLs must be host-managed.
Passing these fixture gates does not establish general fidelity or a usable printer.
No tools are installed, PDFs rewritten, or files uploaded.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass


TOOLS = ("pdfinfo", "pdftotext", "pdffonts", "pdfimages", "pdftocairo", "pdftoppm")
FIXTURE_IDS = ("text", "vectors", "mixed")
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_TOOL_OUTPUT_BYTES = 4 * 1024 * 1024
MAX_RENDER_FILE_BYTES = 64 * 1024 * 1024
MAX_RENDER_TOTAL_BYTES = 512 * 1024 * 1024
MAX_SVG_BYTES = 16 * 1024 * 1024
MAX_PIXELS = 20_000_000
MAX_PAGES = 16
TOOL_TIMEOUT_SECONDS = 60
MEDIA_TOLERANCE_PT = 0.1
FULLPAGE_AREA_FRACTION = 0.90
MAX_MEAN_DIFFERENCE = 0.01
PIXEL_DIFFERENCE_THRESHOLD = 0.10
MAX_DIFFERENT_PIXEL_FRACTION = 0.005
MAX_AGGREGATE_SIZE_RATIO = 0.90
NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


class ProofError(Exception):
    """Only allowlisted codes/metadata, never underlying exception messages."""

    def __init__(self, code, stage, *, tool=None, blocked=False, report_safe=True):
        super().__init__(code)
        self.code = code
        self.stage = stage
        self.tool = tool
        self.blocked = blocked
        self.report_safe = report_safe

    def record(self, fixture=None):
        result = {"code": self.code, "stage": self.stage,
                  "status": "blocked" if self.blocked else "error"}
        if self.tool in TOOLS:
            result["tool"] = self.tool
        if fixture in FIXTURE_IDS:
            result["fixture"] = fixture
        return result


def absolute_path(value):
    return Path(os.path.abspath(os.fspath(value)))


def check_path(path, *, kind=None, new=False):
    """Check every existing component, including Windows reparse points."""
    path = absolute_path(path)
    for component in (*reversed(path.parents), path):
        try:
            info = os.lstat(component)
        except FileNotFoundError:
            if component != path:
                raise ProofError("PARENT_MISSING", "paths")
            if new:
                return path
            raise ProofError("INPUT_MISSING", "paths")
        except OSError:
            raise ProofError("PATH_ACCESS_FAILED", "paths") from None
        if (stat.S_ISLNK(info.st_mode)
                or getattr(info, "st_file_attributes", 0) & 0x400):
            raise ProofError("UNSAFE_PATH", "paths")
        if component != path and not stat.S_ISDIR(info.st_mode):
            raise ProofError("INVALID_PARENT", "paths")
    if new:
        raise ProofError("DESTINATION_EXISTS", "paths")
    if kind == "file" and not stat.S_ISREG(info.st_mode):
        raise ProofError("NOT_REGULAR_FILE", "paths")
    if kind == "directory" and not stat.S_ISDIR(info.st_mode):
        raise ProofError("NOT_DIRECTORY", "paths")
    return path


def overlaps(left, right):
    # normcase is important for Windows, including destinations not yet created.
    left = os.path.normcase(str(absolute_path(left)))
    right = os.path.normcase(str(absolute_path(right)))
    try:
        return os.path.commonpath((left, right)) in (left, right)
    except ValueError:  # Different Windows drives.
        return False


def check_collisions(inputs, output, render, tools_root=None):
    if overlaps(output, render):
        raise ProofError("PATH_COLLISION", "paths", report_safe=False)
    for path in inputs:
        if overlaps(output, path):
            raise ProofError("PATH_COLLISION", "paths", report_safe=False)
        if overlaps(render, path):
            raise ProofError("PATH_COLLISION", "paths")
    if tools_root is not None:
        if overlaps(output, tools_root):
            raise ProofError("PATH_COLLISION", "paths", report_safe=False)
        if overlaps(render, tools_root):
            raise ProofError("PATH_COLLISION", "paths")
    for index, path in enumerate(inputs):
        for other in inputs[:index]:
            if overlaps(path, other):
                raise ProofError("PATH_COLLISION", "paths")
            try:
                if os.path.samefile(path, other):
                    raise ProofError("PATH_COLLISION", "paths")
            except FileNotFoundError:
                pass
            except OSError:
                raise ProofError("PATH_ACCESS_FAILED", "paths") from None


def read_bounded(path, limit, stage):
    check_path(path, kind="file")
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            data = stream.read(limit + 1)
    except OSError:
        raise ProofError("FILE_READ_FAILED", stage) from None
    if len(data) > limit:
        raise ProofError("FILE_SIZE_LIMIT", stage)
    return data


def fingerprint(path):
    check_path(path, kind="file")
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)
        with os.fdopen(os.open(path, flags), "rb") as stream:
            before = os.fstat(stream.fileno())
            digest = hashlib.sha256()
            size = 0
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
                size += len(block)
            after = os.fstat(stream.fileno())
        if (before.st_size != size or after.st_size != size
                or before.st_mtime_ns != after.st_mtime_ns):
            raise ProofError("INPUT_CHANGED", "integrity")
        return {"sha256": digest.hexdigest(), "bytes": size}
    except OSError:
        raise ProofError("FILE_READ_FAILED", "integrity") from None


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProofError("MANIFEST_DUPLICATE_KEY", "manifest")
        result[key] = value
    return result


def validate_manifest(value):
    if (not isinstance(value, dict)
            or set(value) != {"schema", "version", "fixtures"}
            or value["schema"] != "4d-pdf-printer.proof-comparison"
            or type(value["version"]) is not int or value["version"] != 1
            or not isinstance(value["fixtures"], list) or len(value["fixtures"]) != 3):
        raise ProofError("INVALID_MANIFEST", "manifest")
    fixtures = {}
    for fixture in value["fixtures"]:
        if (not isinstance(fixture, dict)
                or set(fixture) != {"id", "baseline", "worker", "expected_text", "minimum_vector_paths"}
                or not isinstance(fixture["id"], str) or fixture["id"] not in FIXTURE_IDS
                or fixture["id"] in fixtures
                or type(fixture["minimum_vector_paths"]) is not int
                or fixture["minimum_vector_paths"] <= 0
                or not isinstance(fixture["expected_text"], list)
                or not fixture["expected_text"]
                or any(not isinstance(text, str) or not text.split()
                       for text in fixture["expected_text"])):
            raise ProofError("INVALID_MANIFEST", "manifest")
        for role in ("baseline", "worker"):
            path = fixture[role]
            if (not isinstance(path, str) or not path or "\0" in path
                    or not Path(path).is_absolute()):
                raise ProofError("INVALID_PDF_PATH", "manifest")
        fixtures[fixture["id"]] = fixture
    return [fixtures[key] for key in FIXTURE_IDS]


def load_manifest(path):
    data = read_bounded(path, MAX_MANIFEST_BYTES, "manifest")
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_unique_object,
                           parse_constant=lambda _: None)
    except (ValueError, UnicodeError, RecursionError):
        raise ProofError("INVALID_MANIFEST_JSON", "manifest") from None
    return validate_manifest(value)


def check_render_budget(directory):
    if directory is None:
        return
    check_path(directory, kind="directory")
    total = 0
    try:
        for child in directory.iterdir():
            check_path(child, kind="file")
            size = child.stat().st_size
            total += size
            if size > MAX_RENDER_FILE_BYTES or total > MAX_RENDER_TOTAL_BYTES:
                raise ProofError("RENDER_SIZE_LIMIT", "render")
    except OSError:
        raise ProofError("RENDER_ACCESS_FAILED", "render") from None


def run_tool(executable, arguments, *, tool, stage, render_directory=None):
    """Drain bounded pipes; only version probing consumes stderr in memory."""
    environment = os.environ.copy()
    environment.update(LC_ALL="C", LANG="C", LANGUAGE="C")
    try:
        process = subprocess.Popen(
            [str(executable), *map(str, arguments)], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
            cwd=render_directory, shell=False,
        )
    except OSError:
        raise ProofError("TOOL_START_FAILED", stage, tool=tool) from None
    overflow = threading.Event()
    read_failed = threading.Event()
    diagnostics = threading.Event()
    stdout, stderr = bytearray(), bytearray()

    def drain(stream, capture):
        count = 0
        try:
            with stream:
                while True:
                    block = stream.read(65536)
                    if not block:
                        break
                    if stream is process.stderr:
                        diagnostics.set()
                    count += len(block)
                    if count > MAX_TOOL_OUTPUT_BYTES:
                        overflow.set()
                        break
                    if capture is not None:
                        capture.extend(block)
        except (OSError, ValueError):
            read_failed.set()

    threads = [threading.Thread(target=drain, args=(process.stdout, stdout), daemon=True),
               threading.Thread(target=drain, args=(process.stderr, stderr if stage == "tools" else None),
                                daemon=True)]
    for thread in threads:
        thread.start()
    deadline = time.monotonic() + TOOL_TIMEOUT_SECONDS
    failure = None
    try:
        while process.poll() is None:
            if overflow.is_set():
                raise ProofError("TOOL_OUTPUT_LIMIT", stage, tool=tool)
            if time.monotonic() >= deadline:
                raise ProofError("TOOL_TIMEOUT", stage, tool=tool)
            check_render_budget(render_directory)
            time.sleep(0.02)
        check_render_budget(render_directory)
    except ProofError as error:
        failure = error
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        for thread in threads:
            thread.join(timeout=1)
    if failure:
        raise failure
    if overflow.is_set():
        raise ProofError("TOOL_OUTPUT_LIMIT", stage, tool=tool)
    if read_failed.is_set() or any(thread.is_alive() for thread in threads):
        raise ProofError("TOOL_OUTPUT_READ_FAILED", stage, tool=tool)
    if process.returncode != 0:
        raise ProofError("TOOL_FAILED", stage, tool=tool)
    if diagnostics.is_set() and stage != "tools":
        raise ProofError("TOOL_DIAGNOSTIC", stage, tool=tool)
    # Version banners normally go to stderr. They are parsed in memory, never
    # copied into the report; all other stderr is discarded.
    return bytes(stdout + stderr) if stage == "tools" else bytes(stdout)


def discover_tools(root, runner, report):
    tools = {}
    for name in TOOLS:
        report["tools"][name] = {}
        try:
            if root is None:
                candidate = shutil.which(name)
            else:
                suffix = ".exe" if os.name == "nt" else ""
                candidate = root / (name + suffix)
                if not candidate.is_file():
                    candidate = None
            if candidate is None:
                raise ProofError("TOOL_MISSING", "tools", tool=name, blocked=True)
            executable = check_path(candidate, kind="file")
            if os.name == "nt" and executable.suffix.lower() != ".exe":
                raise ProofError("UNSAFE_TOOL_TYPE", "tools", tool=name)
            identity = fingerprint(executable)
            report["tools"][name]["sha256"] = identity["sha256"]
            banner = runner(executable, ["-v"], tool=name, stage="tools")
            match = re.search(rb"\b" + name.encode("ascii") + rb" version (\d+(?:\.\d+){1,3})\b",
                              banner)
            if not match:
                raise ProofError("TOOL_VERSION_PARSE_FAILED", "tools", tool=name)
            report["tools"][name].update(
                status="passed", version=match.group(1).decode("ascii"))
            tools[name] = executable
        except ProofError as error:
            report["tools"][name]["status"] = "blocked" if error.blocked else "error"
            report["errors"].append(error.record())
    return tools


def decode_output(data, stage):
    try:
        return data.decode("utf-8")
    except UnicodeError:
        raise ProofError("TOOL_ENCODING_ERROR", stage) from None


def parse_pdfinfo(data):
    text = decode_output(data, "media")
    counts = re.findall(r"^Pages:\s*(\d+)\s*$", text, re.MULTILINE)
    if len(counts) != 1 or not 1 <= int(counts[0]) <= MAX_PAGES:
        raise ProofError("PDFINFO_PARSE_FAILED", "media")
    pages = int(counts[0])
    boxes = {}
    pattern = (r"^(?:Page\s+(\d+)\s+)?MediaBox:\s*(" + NUMBER + r")\s+("
               + NUMBER + r")\s+(" + NUMBER + r")\s+(" + NUMBER + r")\s*$")
    for line in text.splitlines():
        if "MediaBox:" not in line:
            continue
        match = re.match(pattern, line)
        if not match:
            raise ProofError("PDFINFO_PARSE_FAILED", "media")
        page = int(match.group(1)) if match.group(1) else 1
        coordinates = [float(match.group(index)) for index in range(2, 6)]
        width, height = coordinates[2] - coordinates[0], coordinates[3] - coordinates[1]
        if (page in boxes or not 1 <= page <= pages
                or not all(math.isfinite(value) for value in coordinates)
                or not math.isfinite(width) or not math.isfinite(height)
                or width <= 0 or height <= 0):
            raise ProofError("PDFINFO_PARSE_FAILED", "media")
        boxes[page] = [width, height]
    if set(boxes) != set(range(1, pages + 1)):
        raise ProofError("PDFINFO_PARSE_FAILED", "media")
    return {"page_count": pages, "media_dimensions_pt": [boxes[n] for n in range(1, pages + 1)]}


def media_equal(baseline, worker):
    return (baseline["page_count"] == worker["page_count"]
            and all(abs(left - right) <= MEDIA_TOLERANCE_PT + 1e-9
                    for first, second in zip(baseline["media_dimensions_pt"],
                                             worker["media_dimensions_pt"])
                    for left, right in zip(first, second)))


def normalize_text(text):
    return " ".join(text.split())


def text_metrics(baseline, worker, expected):
    left, right = normalize_text(baseline), normalize_text(worker)
    expected = [normalize_text(text) for text in expected]
    return {
        "equal": left == right,
        "expected_count": len(expected),
        "baseline_expected_present": sum(text in left for text in expected),
        "worker_expected_present": sum(text in right for text in expected),
    }


def _table_rows(data, header, stage):
    lines = decode_output(data, stage).splitlines()
    if (len(lines) < 2 or not re.fullmatch(header, lines[0].strip())
            or not re.fullmatch(r"[-\s]+", lines[1]) or "-" not in lines[1]):
        raise ProofError(stage.upper() + "_PARSE_FAILED", stage)
    return [line for line in lines[2:] if line.strip()]


def parse_fonts(data):
    rows = _table_rows(data, r"name\s+type\s+encoding\s+emb\s+sub\s+uni\s+object\s+ID", "fonts")
    result = {"count": 0, "embedded_count": 0, "subset_count": 0, "unicode_count": 0}
    for row in rows:
        match = re.fullmatch(r"(.+?)\s+(yes|no)\s+(yes|no)\s+(yes|no)\s+\d+\s+\d+\s*", row)
        if not match or len(match.group(1).split()) < 3:
            raise ProofError("FONTS_PARSE_FAILED", "fonts")
        result["count"] += 1
        for index, field in enumerate(("embedded_count", "subset_count", "unicode_count"), 2):
            result[field] += match.group(index) == "yes"
    result["all_embedded"] = result["embedded_count"] == result["count"]
    return result


def parse_images(data, media):
    rows = _table_rows(
        data, r"page\s+num\s+type\s+width\s+height\s+color\s+comp\s+bpc\s+enc\s+interp"
              r"\s+object\s+ID\s+x-ppi\s+y-ppi\s+size\s+ratio", "images")
    result = {"count": 0, "fullpage_count": 0, "maximum_page_area_fraction": 0.0}
    for row in rows:
        columns = row.split()
        try:
            if len(columns) not in (15, 16):
                raise ValueError
            page, number, width, height = map(int, (columns[0], columns[1], columns[3], columns[4]))
            xppi, yppi = map(float, columns[-4:-2])
            object_id = columns[10:-4]
            if not (object_id == ["[inline]"]
                    or len(object_id) == 2 and all(value.isdigit() for value in object_id)):
                raise ValueError
            if (columns[2] not in ("image", "mask", "smask")
                    or not 1 <= page <= media["page_count"] or number < 0
                    or width <= 0 or height <= 0
                    or not columns[6].isdigit() or int(columns[6]) <= 0
                    or not columns[7].isdigit() or int(columns[7]) <= 0
                    or columns[9] not in ("yes", "no")
                    or not all(math.isfinite(value) and value > 0 for value in (xppi, yppi))):
                raise ValueError
            page_width, page_height = media["media_dimensions_pt"][page - 1]
            fraction = (width * 72 / xppi) * (height * 72 / yppi) / (page_width * page_height)
            if not math.isfinite(fraction):
                raise ValueError
        except (ValueError, OverflowError, IndexError, ZeroDivisionError):
            raise ProofError("IMAGES_PARSE_FAILED", "images") from None
        result["count"] += 1
        result["fullpage_count"] += fraction >= FULLPAGE_AREA_FRACTION
        result["maximum_page_area_fraction"] = max(result["maximum_page_area_fraction"], fraction)
    return result


def count_svg_paths(data):
    if len(data) > MAX_SVG_BYTES:
        raise ProofError("SVG_SIZE_LIMIT", "vectors")
    if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
        raise ProofError("SVG_PARSE_FAILED", "vectors")
    try:
        root = ET.fromstring(data)
        if root.tag not in ("svg", "{http://www.w3.org/2000/svg}svg"):
            raise ValueError
        count = 0
        excluded = {"defs", "clipPath", "mask", "symbol", "pattern", "marker", "metadata"}
        stack = [(root, {"fill": "black", "stroke": "none"})]
        while stack:
            element, inherited = stack.pop()
            tag = element.tag.rsplit("}", 1)[-1]
            if tag in excluded:
                continue
            style = inherited.copy()
            style.update({key: value for key, value in element.attrib.items()
                          if key in ("fill", "stroke", "display", "visibility", "opacity")})
            for declaration in element.get("style", "").split(";"):
                if ":" in declaration:
                    key, value = declaration.split(":", 1)
                    style[key.strip()] = value.strip()
            if (style.get("display") == "none" or style.get("visibility") in ("hidden", "collapse")
                    or float(style.get("opacity", "1")) == 0):
                continue
            if (tag == "path" and re.search(r"[LlHhVvCcSsQqTtAaZz]", element.get("d", ""))
                    and (style.get("fill") != "none" or style.get("stroke") != "none")):
                count += 1
            stack.extend((child, style) for child in element)
        return count
    except (ET.ParseError, ValueError, RecursionError):
        raise ProofError("SVG_PARSE_FAILED", "vectors") from None


@dataclass(frozen=True)
class PPM:
    width: int
    height: int
    maxval: int
    pixels: bytes


def parse_ppm(data):
    if len(data) > MAX_RENDER_FILE_BYTES:
        raise ProofError("RASTER_SIZE_LIMIT", "raster")
    whitespace = b" \t\r\n\v\f"
    position = 0

    def token():
        nonlocal position
        while position < len(data):
            if position > 4096:
                raise ProofError("PPM_HEADER_LIMIT", "raster")
            if data[position] in whitespace:
                position += 1
            elif data[position] == ord("#"):
                while position < len(data) and data[position] not in b"\r\n":
                    position += 1
                    if position > 4096:
                        raise ProofError("PPM_HEADER_LIMIT", "raster")
            else:
                break
        start = position
        while position < len(data) and data[position] not in whitespace + b"#":
            position += 1
            if position > 4096:
                raise ProofError("PPM_HEADER_LIMIT", "raster")
        return data[start:position]

    try:
        if token() != b"P6":
            raise ValueError
        dimensions = [token(), token(), token()]
        if any(not value or not value.isdigit() for value in dimensions):
            raise ValueError
        width, height, maxval = map(int, dimensions)
        if (width <= 0 or height <= 0 or width * height > MAX_PIXELS
                or not 1 <= maxval <= 65535
                or position >= len(data) or data[position] not in whitespace):
            raise ValueError
        # Exactly one delimiter, with CRLF treated as one line ending. Never
        # lstrip(): whitespace and '#' are perfectly valid first pixel bytes.
        sample_bytes = 1 if maxval < 256 else 2
        expected_bytes = width * height * 3 * sample_bytes
        if (data[position:position + 2] == b"\r\n"
                and len(data) - position - 2 == expected_bytes):
            position += 2
        else:
            position += 1
        pixels = data[position:]
        if len(pixels) != expected_bytes:
            raise ValueError
        values = pixels if sample_bytes == 1 else (
            int.from_bytes(pixels[n:n + 2], "big") for n in range(0, len(pixels), 2))
        if any(value > maxval for value in values):
            raise ValueError
        return PPM(width, height, maxval, pixels)
    except (ValueError, OverflowError):
        raise ProofError("PPM_PARSE_FAILED", "raster") from None


def raster_metrics(baseline, worker):
    if (baseline.width, baseline.height) != (worker.width, worker.height):
        return {"dimensions_equal": False}

    def samples(image):
        if image.maxval < 256:
            return iter(image.pixels)
        return (int.from_bytes(image.pixels[n:n + 2], "big")
                for n in range(0, len(image.pixels), 2))

    total_difference, different, channel_max = 0, 0, 0
    denominator = baseline.maxval * worker.maxval
    for index, (left, right) in enumerate(zip(samples(baseline), samples(worker)), 1):
        delta = abs(left * worker.maxval - right * baseline.maxval)
        total_difference += delta
        channel_max = max(channel_max, delta)
        if index % 3 == 0:
            # Exact 10% comparison, avoiding float roundoff at the boundary.
            different += channel_max * 10 > denominator
            channel_max = 0
    pixels = baseline.width * baseline.height
    mean = total_difference / (denominator * pixels * 3)
    fraction = different / pixels
    return {
        "dimensions_equal": True,
        "normalized_mean_absolute_rgb_difference": mean,
        "different_pixel_fraction": fraction,
        "passed": mean <= MAX_MEAN_DIFFERENCE and fraction <= MAX_DIFFERENT_PIXEL_FRACTION,
    }


def size_summary(fixtures):
    baseline = sum(item["baseline"]["bytes"] for item in fixtures)
    worker = sum(item["worker"]["bytes"] for item in fixtures)
    if baseline <= 0 or any(item[role]["bytes"] <= 0 for item in fixtures
                            for role in ("baseline", "worker")):
        raise ProofError("EMPTY_PDF", "size")
    for item in fixtures:
        item["worker_baseline_size_ratio"] = item["worker"]["bytes"] / item["baseline"]["bytes"]
    ratio = worker / baseline
    return {"baseline_bytes": baseline, "worker_bytes": worker,
            "worker_baseline_size_ratio": ratio,
            "maximum_ratio": MAX_AGGREGATE_SIZE_RATIO,
            "passed": ratio <= MAX_AGGREGATE_SIZE_RATIO}


def add_stage(record, name, passed, **metrics):
    record["stages"].append({"stage": name, "status": "passed" if passed else "failed", **metrics})


def inspect_fixture(fixture, record, tools, render, runner):
    texts = {}
    media = {}
    for role in ("baseline", "worker"):
        pdf = absolute_path(fixture[role])

        def invoke(name, arguments, stage):
            return runner(tools[name], arguments, tool=name, stage=stage, render_directory=render)

        # Request every page explicitly; pdfinfo's default only describes page 1.
        info = invoke("pdfinfo", ["-f", "1", "-l", str(MAX_PAGES), "-box", pdf], "media")
        media[role] = parse_pdfinfo(info)
        record[role].update(media[role])
        texts[role] = decode_output(
            invoke("pdftotext", ["-enc", "UTF-8", "-nopgbrk", pdf, "-"], "text"), "text")
        fonts = parse_fonts(invoke("pdffonts", [pdf], "fonts"))
        record[role]["fonts"] = fonts
        required = bool(normalize_text(texts[role]) or fixture["expected_text"])
        add_stage(record, "fonts", fonts["all_embedded"] and (fonts["count"] > 0 or not required),
                  role=role)
        images = parse_images(invoke("pdfimages", ["-list", pdf], "images"), media[role])
        record[role]["images"] = images
        add_stage(record, "images", images["fullpage_count"] == 0, role=role)
        record[role]["vector_path_counts"] = []
        for page in range(1, media[role]["page_count"] + 1):
            svg = render / f"{fixture['id']}-{role}-{page:04d}.svg"
            invoke("pdftocairo", ["-svg", "-f", str(page), "-l", str(page), pdf, svg], "vectors")
            count = count_svg_paths(read_bounded(svg, MAX_SVG_BYTES, "vectors"))
            record[role]["vector_path_counts"].append(count)
            add_stage(record, "vectors", count >= fixture["minimum_vector_paths"], role=role, page=page)
            prefix = render / f"{fixture['id']}-{role}-{page:04d}"
            invoke("pdftoppm", ["-r", "150", "-f", str(page), "-l", str(page),
                               "-singlefile", pdf, prefix], "raster")
            # Parse now as well as when paired, so an unmatched page cannot hide
            # a corrupt/oversized raster behind a page-count failure.
            parse_ppm(read_bounded(prefix.with_suffix(".ppm"), MAX_RENDER_FILE_BYTES, "raster"))
    add_stage(record, "media", media_equal(media["baseline"], media["worker"]))
    text = text_metrics(texts["baseline"], texts["worker"], fixture["expected_text"])
    add_stage(record, "text", text["equal"]
              and text["baseline_expected_present"] == text["expected_count"]
              and text["worker_expected_present"] == text["expected_count"], **text)
    for page in range(1, min(media["baseline"]["page_count"], media["worker"]["page_count"]) + 1):
        images = [parse_ppm(read_bounded(
            render / f"{fixture['id']}-{role}-{page:04d}.ppm", MAX_RENDER_FILE_BYTES, "raster"))
                  for role in ("baseline", "worker")]
        metrics = raster_metrics(*images)
        add_stage(record, "raster", metrics.get("passed", False), page=page,
                  **{key: value for key, value in metrics.items() if key != "passed"})


def new_report():
    return {
        "schema": "4d-pdf-printer.proof-comparison-report", "version": 1,
        "result": "error", "manual_review_required": True, "productUsable": False,
        "critical_region_manual_review": "unmeasured",
        "scope": "automated_original_fixture_gates_only",
        "tools": {}, "fixtures": [], "stages": [], "errors": [],
        "thresholds": {
            "media_tolerance_pt": MEDIA_TOLERANCE_PT, "render_dpi": 150,
            "fullpage_image_area_fraction": FULLPAGE_AREA_FRACTION,
            "maximum_mean_rgb_difference": MAX_MEAN_DIFFERENCE,
            "pixel_channel_difference_threshold": PIXEL_DIFFERENCE_THRESHOLD,
            "maximum_different_pixel_fraction": MAX_DIFFERENT_PIXEL_FRACTION,
        },
    }


def create_report_file(path):
    check_path(path, new=True)
    try:
        flags = (os.O_WRONLY | os.O_CREAT | os.O_EXCL
                 | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0))
        return os.open(path, flags, 0o600)
    except FileExistsError:
        raise ProofError("DESTINATION_EXISTS", "report", report_safe=False) from None
    except OSError:
        raise ProofError("REPORT_CREATE_FAILED", "report", report_safe=False) from None


def compare(manifest, output, *, tools_root=None, render_directory=None, runner=None):
    """Return (exit_code, report); failures never include private diagnostics."""
    report = new_report()
    output_path = None
    report_fd = None
    report_safe = True
    snapshots = []
    runner = run_tool if runner is None else runner
    try:
        output_path = check_path(output, new=True)
        manifest_path = absolute_path(manifest)
        render = absolute_path(render_directory if render_directory is not None
                               else str(output_path) + ".renders")
        root = absolute_path(tools_root) if tools_root is not None else None
        check_collisions([manifest_path], output_path, render, root)
        fixtures = load_manifest(manifest_path)
        inputs = [manifest_path] + [absolute_path(item[role]) for item in fixtures
                                    for role in ("baseline", "worker")]
        check_collisions(inputs, output_path, render, root)
        for path in inputs:
            check_path(path, kind="file")
        check_path(render, new=True)
        if root is not None:
            check_path(root, kind="directory")
        report_fd = create_report_file(output_path)
        for fixture in fixtures:
            record = {"id": fixture["id"], "stages": []}
            report["fixtures"].append(record)
            for role in ("baseline", "worker"):
                path = absolute_path(fixture[role])
                identity = fingerprint(path)
                record[role] = identity.copy()
                snapshots.append((path, identity, record, role))
        report["size"] = size_summary(report["fixtures"])
        add_stage(report, "aggregate_size", report["size"]["passed"])
        tools = discover_tools(root, runner, report)
        if len(tools) == len(TOOLS):
            check_path(render, new=True)
            try:
                render.mkdir(mode=0o700)
            except OSError:
                raise ProofError("RENDER_CREATE_FAILED", "render") from None
            for fixture, record in zip(fixtures, report["fixtures"]):
                try:
                    inspect_fixture(fixture, record, tools, render, runner)
                except ProofError as error:
                    report["errors"].append(error.record(fixture["id"]))
    except ProofError as error:
        report_safe = error.report_safe
        report["errors"].append(error.record())
    except (OSError, ValueError, OverflowError, RecursionError):
        report["errors"].append(ProofError("INSPECTION_FAILED", "inspection").record())
    finally:
        for path, before, record, role in snapshots:
            try:
                unchanged = fingerprint(path) == before
                record[role]["sha256_unchanged"] = unchanged
                add_stage(record, "integrity", unchanged, role=role)
                if not unchanged:
                    report["errors"].append(ProofError("INPUT_CHANGED", "integrity").record(record["id"]))
            except ProofError as error:
                record[role]["sha256_unchanged"] = False
                report["errors"].append(error.record(record["id"]))
        if report["errors"]:
            report["result"] = ("blocked" if all(item["status"] == "blocked" for item in report["errors"])
                                else "error")
        else:
            stages = report["stages"] + [stage for item in report["fixtures"] for stage in item["stages"]]
            report["result"] = "passed" if stages and all(
                stage["status"] == "passed" for stage in stages) else "failed"
        try:
            if report_fd is None and output_path is not None and report_safe:
                report_fd = create_report_file(output_path)
            if report_fd is not None:
                with os.fdopen(report_fd, "w", encoding="utf-8", newline="\n") as stream:
                    report_fd = None
                    json.dump(report, stream, indent=2, allow_nan=False)
                    stream.write("\n")
                    stream.flush()
                    os.fsync(stream.fileno())
            else:
                report["result"] = "error"
        except (ProofError, OSError, ValueError):
            report["result"] = "error"
            report["errors"].append(ProofError("REPORT_WRITE_FAILED", "report").record())
        finally:
            if report_fd is not None:
                os.close(report_fd)
    return (0 if report["result"] == "passed" else 2), report


class PrivateArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        raise ProofError("INVALID_ARGUMENTS", "arguments")


def main(argv=None):
    parser = PrivateArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--tools-root")
    parser.add_argument("--render-directory")
    try:
        arguments = parser.parse_args(argv)
        code, _ = compare(arguments.manifest, arguments.output, tools_root=arguments.tools_root,
                          render_directory=arguments.render_directory)
    except ProofError:
        code = 2
    print("Automated comparison passed; local manual review is still required."
          if code == 0 else
          "Automated comparison did not pass; consult the private report if safely created.")
    return code


if __name__ == "__main__":
    sys.exit(main())
