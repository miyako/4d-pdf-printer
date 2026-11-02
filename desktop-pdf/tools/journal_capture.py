"""Copied-evidence journal boundary; caller supplies independently verified producer facts."""
import hashlib
import re

import journal
import journal_v1 as v1


def check(capture, final_raw, journal_raw, grace, facts, activation, job, slots, *, expected_version):
    need = v1.need
    need(v1.identifier(activation) and v1.identifier(job), "genuine fresh activation/job")
    need(job not in facts["old_jobs"] and activation not in facts["old_activations"], "fresh producer job/activation context")
    need(type(capture) is dict and set(capture) == {"collection_attempted", "activation", "records", "terminal",
         "unresolved", "deferral_return_proved", "process_exit_proved", "physical_unload_proved"}, "exact capture envelope")
    need(final_raw["metadata_errors"] == [], "complete raw namespace before content")
    need(journal_raw["cache"] == final_raw["rows"] and
         journal_raw["terminal_siblings"] == final_raw["terminal_siblings"], "same frozen raw inventory")
    old_prefixes = (["Jobs\\"+value for value in facts["old_jobs"]] +
                    ["Registrations\\"+value for value in facts["old_registrations"]] +
                    ["ActivationDiagnostics\\"+value for value in facts["old_activations"]])
    for row in final_raw["rows"]:
        need(v1.integer(row["attributes"], 0xffffffff) and type(row["directory"]) is bool, "raw native attributes/kind")
        old = any(row["path"] == p or row["path"].startswith(p+"\\") for p in old_prefixes)
        need(old or not row["attributes"] & 0x400, "new namespace reparse")
    marker = "Jobs\\"+job+"\\accepted"
    markers = [row for row in final_raw["rows"] if row["path"] == marker]
    need(len(markers) == 1 and markers[0]["directory"] is True and not markers[0]["attributes"] & 0x400 and
         final_raw["enumerated_directories"].count(marker) == 1 and
         not any(row["path"].startswith(marker+"\\") for row in final_raw["rows"]), "ordinary empty enumerated marker")
    need(grace["fixed_grace_ms"] == 3000 and v1.integer(grace["first_completion_ms"]) and
         v1.integer(grace["observed_ms"]) and grace["observed_ms"]-grace["first_completion_ms"] >= 3000 and
         grace["physical_finish_guaranteed"] is False and journal_raw["grace_complete"] is True,
         "one observed grace is not physical finish")
    need(capture["collection_attempted"] is True and capture["unresolved"] == [] and
         capture["deferral_return_proved"] is capture["process_exit_proved"] is capture["physical_unload_proved"] is False,
         "capture proof limits")
    need(capture["activation"] == activation, "exact activation")
    directories = [r["path"].split("\\")[1] for r in journal_raw["cache"]
                   if r["directory"] and re.fullmatch(r"ActivationDiagnostics\\[0-9a-f]{32}", r["path"]) and
                   r["path"].split("\\")[1] not in facts["old_activations"]]
    need(directories == [activation], "one new activation prefix")
    prefix = "ActivationDiagnostics\\"+activation+"\\"
    source_rows = [r for r in journal_raw["cache"] if r["path"].startswith(prefix)]
    records = capture["records"]
    need(type(records) is list and 1 <= len(records) <= 32 and len(source_rows) == len(records),
         "no missing/unknown/staging entries")

    def item_bytes(item, name, maximum):
        need(type(item) is dict and set(item) == {"slot", "bytes", "sha256"} and item["slot"] == name,
             "exact copied item binding")
        need(v1.integer(item["bytes"], maximum) and item["bytes"] > 0 and
             type(item["sha256"]) is str and re.fullmatch("[0-9a-f]{64}", item["sha256"]), "copied item bounds")
        need(name in slots and type(slots[name]) is bytes, "copied slot present")
        raw = slots[name]
        need(len(raw) == item["bytes"] and hashlib.sha256(raw).hexdigest() == item["sha256"], "copied exact bytes/SHA")
        return raw

    raw_records = []
    for index, record in enumerate(records):
        need(type(record) is dict and set(record) == {"sequence", "slot", "bytes", "sha256"} and
             type(record["sequence"]) is int and record["sequence"] == index, "one contiguous numeric sequence")
        name = record["slot"]
        need(type(name) is str and re.fullmatch(r"journal-slot-(?:0[0-9]|[12][0-9]|3[01])\.bin", name), "evidence slot name")
        source = [r for r in source_rows if r["path"] == prefix+str(index)+".json"]
        need(len(source) == 1 and source[0]["directory"] is False and source[0]["bytes"] == record["bytes"],
             "raw metadata/final-file mapping")
        raw_records.append(item_bytes({k: record[k] for k in ("slot", "bytes", "sha256")}, name, 8192))
    need(len({r["slot"] for r in records}) == len(records), "unique copied slots")
    terminal = capture["terminal"]
    siblings = journal_raw["terminal_siblings"]
    need(type(terminal) is dict and set(terminal) == {"slot", "bytes", "sha256"}, "terminal item shape")
    need(type(siblings) is list and len(siblings) == 1 and
         siblings[0]["name"] == "DesktopPdfJournal-"+activation+".json" and
         v1.integer(siblings[0]["metadata"]["attributes"], 0xffffffff) and
         v1.integer(siblings[0]["metadata"]["bytes"], 4096) and
         siblings[0]["metadata"]["directory"] is False and siblings[0]["metadata"]["bytes"] == terminal["bytes"] and
         not siblings[0]["metadata"]["attributes"] & 0x400, "exact ordinary independent terminal sibling")
    raw_terminal = item_bytes(terminal, "journal-terminal.bin", 4096)
    return journal.journal_admission(raw_records, raw_terminal, activation, job, facts["old_activations"],
                                     expected_version=expected_version)
