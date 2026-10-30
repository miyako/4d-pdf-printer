# Worker CLI and status, version 1

```text
pdf-render-worker[.exe] --protocol-version 1 --input <path> --output <path>
pdf-render-worker[.exe] --version
pdf-render-worker[.exe] --help
```

Conversion requires all three options, exactly once; their order is arbitrary.
There are no short options, `--name=value` forms, positional operands or
overwrite flag. Empty paths, unknown/duplicate options and other protocol
versions fail with exit 2. Paths may contain spaces and Unicode. Relative paths
resolve against the caller's working directory; this is not a URI interface.
The caller supplies an immutable, accessible input copy and a trusted,
access-controlled destination parent on a local filesystem. The destination
must not exist (including directories, symlinks or input=output).

## Success rule and transport

**Conversion success requires a normal exit 0 AND exactly one valid v1 status
with `result:"ok"`, `output_published:true`, `cleanup_complete:true`.**
`result:"info"` from help/version is **never conversion success**. Validate
against [`renderer/status-v1.schema.json`](../../renderer/status-v1.schema.json);
also require the recorded exit code to equal the process exit code. The schema
is versioned independently of `worker_version`.

The normal response is one UTF-8 JSON object plus LF on stdout, no BOM. It
contains no input/output path, title, document text or raw upstream error message.
`--help` additionally emits fixed usage on stderr. During conversion, both
third-party output streams are redirected to the null device; the worker retains
a separate stdout handle only for its structured status. Generic human messages
are diagnostic, not stable branch keys. There is no progress stream.

Status is not promised after a native crash, allocation abort, timeout,
termination, startup/loader failure or broken stdout. Missing, truncated,
malformed, extra or mismatched status is a **failed/indeterminate outcome
requiring reconciliation**, never success inferred from file presence.
Exit 71 means the worker could not deliver its status and might already have
published a PDF. No valid status record with `exit_code:71` is promised.

## Record fields

All fields are required; unknown fields are rejected by the v1 schema.

| Field | Type / meaning |
|---|---|
| `schema` | string, exactly `4d-pdf-printer.worker-status` |
| `version` | integer, exactly `1` |
| `worker_version` | string; initial executable is `0.1.0` |
| `result` | string enum `ok`, `error`, `info` |
| `exit_code` | integer from the table below, matching OS normal exit |
| `stage` | string enum `arguments`, `help`, `version`, `input`, `document`, `page`, `output`, `render`, `finalize`, `publish`, `cleanup`, `complete` |
| `error` | null on ok/info; otherwise object with required string `code`, nonempty string `message`, nullable string `domain`, integer `native_code`; no extra members |
| `document` | integer >= 1 or null; current fixed-document position, not a persistent job ID |
| `page` | integer >= 1 or null; position **within that fixed document** |
| `documents` | integer >= 0; sequence document count when known |
| `pages_completed` | integer >= 0; successfully rendered pages, **not committed pages** on failure |
| `output_bytes` | integer >= 0; bytes written into temporary PDF; may be nonzero on any later failure |
| `output_published` | boolean; publication committed before status construction |
| `cleanup_complete` | boolean; normal cleanup result, not a crash-cleanup promise |

`document` and `page` are null after successful publication. Error positions
are null until known; a later finalization/publication error may retain the
last page position. Counts and byte sizes are JSON integers (clients should
preserve 64-bit byte counts). `domain` and `native_code` identify library/OS
diagnostics and are **not stable across platforms/dependency versions**.

Example:

```json
{"schema":"4d-pdf-printer.worker-status","version":1,"worker_version":"0.1.0","result":"ok","exit_code":0,"stage":"complete","error":null,"document":null,"page":null,"documents":1,"pages_completed":2,"output_bytes":6115,"output_published":true,"cleanup_complete":true}
```

## Stable exits and `error.code`

The first normal failure wins except that failed cleanup supersedes it with 24.
The stage records where that failure was detected, not a complete event history.

| Exit | `error.code` | Meaning |
|---|---|---|
| 0 | null error | Converted (`ok`), or help/version (`info`) |
| 2 | `invalid_arguments` | Invalid CLI/protocol/Unicode arguments |
| 10 | `input_io` | Input is missing, inaccessible or not a regular file |
| 11 | `invalid_package` | Not a complete seekable ZIP, corrupt structure/data, or unsupported ZIP compression |
| 12 | `unsupported_input` | ZIP encryption detected; no password interface |
| 13 | `resource_limit` | Entry, expanded-byte or page limit; also empty fixed document |
| 20 | `output_exists` | Destination exists, including a publication race |
| 21 | `output_io` | Destination/transaction creation or page-stream write failure |
| 22 | `finalize` | Cairo trailer finalization, file flush/sync or close failure |
| 23 | `publish` | Atomic no-replacement publication failed, other than collision |
| 24 | `cleanup` | Job-local cleanup incomplete; **output may be published** |
| 30 | `document` | Cannot open/parse fixed-document sequence or fixed document; empty/oversized sequence |
| 31 | `page` | Cannot load page; invalid/oversized dimensions |
| 32 | `render` | libgxps page error or Cairo rendering failure |
| 33 | `backend_warning` | Backend warning/critical diagnostic rejected output, including checked unsupported constructs |
| 70 | `internal` | Detected internal/resource setup failure; native corruption is not caught |
| 71 | no reliable record | Status-channel failure; **output may be published** |

Known unsupported rendering constructs can return 32 when libgxps also supplies
a render error; 33 is the warning-only rejection path. Not every unsupported XPS
semantic is detected; [the patch notes](dependencies.md#maintained-libgxps-patch)
define the checked subset.

Prototype guards: 16,384 ZIP entries; 1,073,741,824 total expanded bytes; 10,000
documents; 10,000 total pages; page dimensions finite, positive and at most
200,000 XPS units each. These are provisional safety bounds, not product
acceptance criteria or sufficient memory/CPU containment.
ZIP-entry and expanded-byte limits count physical OPC pieces as well as direct
entries. Valid interleaved XPS/OpenXPS need not have direct logical part names
in the ZIP. libgxps's patched piece reader, not a normalization step, reads them.

## Publication, failures and recovery

A `.gxps-job-<random>` directory in the destination parent holds `output.pdf`.
The PDF is finished, checked, flushed/synced and closed before publication.
Windows uses same-volume `MoveFileExW` without `REPLACE_EXISTING`; POSIX development
uses an atomic hard link then removes the temporary name. Normal exits attempt
to remove both temporary file and directory. No partial PDF is exposed at the
destination. Atomic visibility is **not a power-loss durability guarantee** or
a claim about every network filesystem.

The outcome and filesystem cannot be committed atomically with stdout. In
particular, **exit 24, exit 71, or a kill between publication and status may
leave a completed destination**. Do not retry automatically, delete it blindly,
or assume nonzero means no file exists. Tests exercise cleanup-after-publication
through the same native completion/status code and a closed stdout pipe after
real conversion.

The coordinator must own exact per-job paths/tokens, serialize initially, track
whether it launched/terminated/reaped the process, validate output/status, and
reconcile indeterminate publication before offering a retry. After forced
termination it must remove **only that job's recorded staging files** once
the process/Job Object has been reaped. Never globally sweep arbitrary matching
directories. See [supervisor requirements](architecture.md#supervisor-handoff).
