"""Review-only reading of an activation journal that has no terminal receipt.

The writer stages each record as pending-<id>, writes it, then renames it to <sequence>.json.
When the process ends between Write and Rename, the record bytes stay in the pending file.
This reports such a file as "written-not-renamed". It never promotes it to a published record,
never admits a job and never stands in for the terminal receipt; use journal.assess when a
terminal exists. Pure reading: no Windows calls, collection, retry or normalization.
"""
import json
import re
import sys
from pathlib import Path

import journal_v1 as v1

need = v1.need
PENDING = re.compile(r"pending-[0-9a-f]{32}")
PUBLISHED = re.compile(r"(0|[1-9]|[12][0-9]|3[01])\.json")
MAXIMUM = 8192


def record(raw, activation):
    value = v1.wire(raw, MAXIMUM, allow_bom=True)
    need(set(value) == v1.DIAGNOSTIC_KEYS and value["schema"] == "4d-pdf-printer.activation-diagnostic" and
         type(value["version"]) is int and value["version"] == 1, "diagnostic shape/schema")
    need(v1.integer(value["sequence"], 31) and value["activation"] == activation, "diagnostic source binding")
    need(value["checkpoint"] in v1.POINTS and v1.integer(value["tick_ms"]), "diagnostic event/tick")
    for key in ("hresult", "process", "thread", "identity_error", "identity_validation_error"):
        need(v1.integer(value[key], 0xffffffff), "diagnostic native integer")
    return value


def summary(value):
    return dict(sequence=value["sequence"], checkpoint=value["checkpoint"], tick_ms=value["tick_ms"],
                process=value["process"], thread=value["thread"])


def unterminated(entries, activation, *, terminal_present):
    """entries maps each leaf name in one activation folder to its bytes."""
    need(v1.identifier(activation), "activation identifier")
    need(terminal_present is False, "terminal present; use journal.assess")
    need(type(entries) is dict and len(entries) <= 64, "bounded activation folder")
    published, pending = {}, []
    for leaf, raw in entries.items():
        need(type(leaf) is str and type(raw) is bytes and len(raw) <= MAXIMUM, "leaf/byte bound")
        if PUBLISHED.fullmatch(leaf):
            value = record(raw, activation)
            need(leaf == f"{value['sequence']}.json", "published name matches sequence")
            published[value["sequence"]] = summary(value)
        elif PENDING.fullmatch(leaf):
            pending.append((leaf, raw))
        else:
            raise ValueError("unknown activation folder entry")
    count = len(published)
    need(sorted(published) == list(range(count)), "published sequence gap")
    need(len(pending) <= 1, "one writer leaves at most one pending record")
    staged = []
    for leaf, raw in pending:
        item = dict(leaf=leaf, bytes=len(raw), state="created-not-written", record=None)
        if raw:
            try:
                value = record(raw, activation)
            except ValueError:
                item["state"] = "unreadable"
            else:
                item["record"] = summary(value)
                item["state"] = "written-not-renamed" if value["sequence"] == count else "unexpected-sequence"
        staged.append(item)
    processes = {item["process"] for item in published.values()} | {
        item["record"]["process"] for item in staged if item["record"] is not None}
    need(len(processes) <= 1, "one actual Task process")
    ordered = [published[sequence] for sequence in range(count)]
    return dict(schema="4d-pdf-printer.unterminated-journal", version=1, activation=activation,
                terminal="missing", published_count=count,
                last_published=ordered[-1] if ordered else None, published=ordered, pending=staged,
                review_disposition="review-only", decision_admission="not-evaluable",
                published_through_finish=False, technical_success_promoted=False)


def read_folder(folder):
    """Reads <root>\\DesktopPdf\\ActivationDiagnostics\\<activation>; the terminal is <root>\\DesktopPdfJournal-<activation>.json."""
    folder = Path(folder)
    activation = folder.name
    need(v1.identifier(activation) and folder.is_dir() and not folder.is_symlink(), "activation folder")
    need(folder.parent.name == "ActivationDiagnostics" and folder.parent.parent.name == "DesktopPdf",
         "activation folder layout")
    terminal = folder.parents[2] / f"DesktopPdfJournal-{activation}.json"
    entries = {}
    for path in folder.iterdir():
        need(path.is_file() and not path.is_symlink(), "regular activation folder entry")
        need(path.stat().st_size <= MAXIMUM, "leaf/byte bound")
        entries[path.name] = path.read_bytes()
    return unterminated(entries, activation, terminal_present=terminal.exists())


if __name__ == "__main__":
    need(len(sys.argv) == 2, "usage: journal_pending.py <activation folder>")
    print(json.dumps(read_folder(sys.argv[1]), indent=2))
