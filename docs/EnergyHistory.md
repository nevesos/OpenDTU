# Persistent Energy History

This document defines the persistent on-flash formats for the fork-only persistent
energy history feature. Data is stored on LittleFS, not in `config.json`.

The feature is enabled only for builds that define `ENERGY_HISTORY_ENABLE`.
The manual probe build additionally defines `ENERGY_HISTORY_MANUAL_PROBE`.

Current PlatformIO environments with persistent Energy History enabled:

```text
generic_esp32s3_16mb_energy_history
generic_esp32s3_usb_16mb_energy_history
generic_esp32s3_usb_16mb_energy_history_psram
generic_esp32s3_usb_16mb_energy_history_probe
```

These builds use `partitions_custom_16mb_energy_history.csv`, so switching from
a normal build requires flashing with the 16-MB Energy-History partition layout.

## Directory Layout

```text
/energy/5m/
/energy/day/
/energy/month/
```

## Targets

```text
total
inv_<serial>
```

`serial` is the inverter serial number. Inverter names are not used in file
names, so history survives renames.

## File Names

Five-minute data is stored in monthly EH02 files:

```text
/energy/5m/total_YYYY_MM.eh2
/energy/5m/inv_<serial>_YYYY_MM.eh2
```

Legacy EH01 five-minute files may still exist after older firmware versions or
imports:

```text
/energy/5m/total_YYYY_MM.eh5
/energy/5m/inv_<serial>_YYYY_MM.eh5
```

New writes and visualization use `.eh2` only. `.eh5` is kept only for migration,
file management, scanning/recovery, download, upload, or deletion.

Daily data is stored in yearly files:

```text
/energy/day/total_YYYY.ehd
/energy/day/inv_<serial>_YYYY.ehd
```

Monthly data is stored in yearly files:

```text
/energy/month/total_YYYY.ehm
/energy/month/inv_<serial>_YYYY.ehm
```

## Encoding

All integer values are little-endian. Records are serialized explicitly; raw C++
struct layout must not be written directly because padding is compiler-specific.

## File Header

Fixed size: 32 bytes.

```text
magic        4 bytes   "EH01" for legacy/block files, "EH02" for compact 5m files
fileType     uint8     1=5m, 2=day, 3=month
version      uint8     1 for EH01, 2 for EH02
headerSize   uint8     32
recordSize   uint8     depends on fileType and version
year         uint16
month        uint8     1..12 for 5m, 0 for day/month
targetType   uint8     0=total, 1=inverter
serial       uint64    0 for total
intervalSec  uint16    300 for 5m, 0 otherwise
reserved     6 bytes
crc32Header  uint32    CRC over the header excluding this field
```

## Block Header

Fixed size: 16 bytes.

```text
magic        4 bytes   "EHB1"
blockIndex   uint16
startKey     uint16
recordCount  uint16
payloadSize  uint16
crc32Payload uint32
```

`startKey` meaning:

```text
5m:    day of month, 1..31
day:   day of year, 1..366
month: month, 1..12
```

CRC is calculated over the block payload only.

## Five-Minute Records

EH01 legacy block record size: 8 bytes.

```text
day          uint8     1..31
slot         uint16    0..287, local five-minute slot in the day
yieldDayWh   uint32    daily yield in Wh
flags        uint8
```

EH02 compact record size: 10 bytes. It stores the same first 8 bytes followed
by a CRC16-CCITT over those 8 bytes.

```text
day          uint8     1..31
slot         uint16    0..287, local five-minute slot in the day
yieldDayWh   uint32    daily yield in Wh
flags        uint8
crc16        uint16    CRC16-CCITT over day..flags
```

`slot` is calculated as:

```text
slot = hour * 12 + minute / 5
```

Historical average power is derived from two yield values:

```text
avgPowerW = deltaWh * 3600 / deltaSeconds
```

For a normal five-minute delta:

```text
avgPowerW = deltaWh * 12
```

## Day Record

Size: 16 bytes.

```text
dayOfYear    uint16    1..366
yieldWh      uint32
maxPowerW    uint16
avgPowerW    uint16
runtimeMin   uint16
sampleCount  uint16
flags        uint16
```

## Month Record

Size: 16 bytes.

```text
month        uint8     1..12
reserved0    uint8
yieldWh      uint32
maxPowerW    uint16
avgPowerW    uint16
runtimeMin   uint16
dayCount     uint16
flags        uint16
```

## Record Flags

```text
bit 0 = valid
bit 1 = inverter reachable
bit 2 = inverter producing
bit 3 = day reset detected
bit 4 = estimated/interpolated
bit 5..7 = reserved
```

For day and month records, the same semantic bits are stored in the lower byte
of the `uint16` flags field. The upper byte is reserved.

## Write Strategy

Sampling interval:

```text
5 minutes
```

Persistence interval:

```text
5 minutes, and additionally at day/month boundaries
```

New five-minute data is appended as EH02 compact records without per-sample
block headers. Runtime and visualization paths use EH02 `.eh2` files only. The
legacy EH01 `.eh5` block format remains available for migration and file
management, but it is not used for normal 5-minute queries. The API deduplicates
by `day + slot` when repeated writes occur; the last valid record wins. This
avoids in-place rewrites and keeps flash wear low.

Runtime five-minute samples are written only during the configured day period
from `SunPosition`. If sunrise/sunset calculation is unavailable, sampling
continues so a broken location or twilight configuration does not disable
history recording. Day and month boundaries are finalized before the night
check.

Before writing a daytime five-minute sample, all configured poll-enabled
inverters must have delivered a statistics packet whose `lastUpdate` maps to the
current local day. This prevents stale `YieldDay` values from the previous day
from being persisted before the first fresh inverter data arrives.

Daily and monthly aggregates should be append/update-log style where practical.
If duplicate aggregate records exist, the last valid record for the same key
wins.

Append blocks are intentionally small for aggregate files. EH02 five-minute
runtime writes append one compact record directly. Day and month aggregate
writers support up to four records per block.

Daily aggregates are derived from valid five-minute samples for the completed
local day. `yieldWh` is the highest daily yield seen for the day. `maxPowerW` is
derived from the largest positive yield delta between consecutive samples.
`runtimeMin` counts samples marked as producing in five-minute increments.

Monthly aggregates are derived from daily aggregate records for the completed
local month. `yieldWh` is the sum of daily yields, `maxPowerW` is the highest
daily max power, and `runtimeMin` is the sum of daily runtime minutes.

The manual probe code path can write deterministic demo history for API/UI tests
under June 2099. These files are intentionally persistent and are overwritten
when the probe is executed.

## Runtime Behaviour

`EnergyHistory.init(scheduler)` registers the five-minute loop task and the
manual recovery task. The loop task runs every five minutes. The recovery task
is enabled only when recovery is requested, and runtime startup recovery remains
disabled.

Each runtime sample writes:

```text
target=total
target=inv_<serial> for every configured poll-enabled inverter
```

The total sample uses `Datastore.getTotalAcYieldDayEnabled()`. Per-inverter
samples use the inverter `YieldDay` value from `TYPE_INV / CH0 / FLD_YD`.

The first loop execution for a new local date finalizes the previously sampled
local day for total and for every poll-enabled inverter. When the local month
changed, it also finalizes the previous month. Failed finalizations are queued
and retried in small batches on later loop executions so transient read/write
failures do not permanently lose the aggregate. Up to twelve pending
finalizations are tracked, and at most two pending entries are retried per
five-minute loop run.

Runtime startup recovery is not executed automatically from the scheduler,
because scanning all files can block other scheduler-driven services. Recovery
can be requested through the Web API. Before appending, a failed append attempts
to recover a truncatable final block and then retries the append once.

Changes to history data and managed files increment in-memory revision counters.
Appending to an existing history file increments only the data revision. Creating
a new history file, deleting retention candidates, file imports/deletes, manual
per-file recovery, and append-time recovery increment both data and file
revisions, because they can change query results and the visible file list.

## Backend Query Surface

The firmware exposes narrow backend methods for future Web API use:

```text
lightweight status scan across energy files
revision counters for UI polling
5m query for one target and one local day from EH02 `.eh2` files
day query for one target and a day-of-year range
month query for one target and a month range
```

The five-minute query is day-scoped by design so callers cannot accidentally
request an unbounded monthly dump.

Queries return deduplicated logical records. If multiple valid records exist for
the same key, the last valid record wins. Five-minute visualization uses compact
`.eh2` files only; legacy `.eh5` files are kept only for migration, download, or
manual deletion.

Day queries first read persisted day records. Missing requested days are rebuilt
from the corresponding EH02 five-minute month file when possible, returned to
the caller, and persisted back into the day file in append blocks. For one
query range, each required five-minute month file is read once and then reused
to derive all missing days in that month.

Month queries first read persisted month records. Missing requested months are
rebuilt from day records when possible. A month query rebuilds at most one
missing month per request to avoid long blocking work in the async web-server
task. The rebuilt month is returned immediately and then persisted when the
write succeeds. Repeated UI/API polling therefore fills missing month aggregates
progressively.

## Web API

Revision:

```text
GET /api/energy/history/revision
```

Response:

```text
data_revision
file_revision
last_change_ms
recovery_pending
recovery_running
```

Status:

```text
GET /api/energy/history/status
```

The status response is intentionally lightweight. It counts managed history
files and bytes and reports LittleFS usage plus recovery state. It does not deep
scan every block; use the file scan endpoint for block/record validation.

```text
files_scanned
invalid_final_block_files
truncatable_final_block_files
bytes_scanned
littlefs_total
littlefs_used
recovery.pending
recovery.running
recovery.run_count
recovery.last_started_ms
recovery.last_finished_ms
```

Manual recovery:

```text
POST /api/energy/history/recovery
```

This schedules a background recovery pass over `/energy/5m`, `/energy/day`,
and `/energy/month`. A second request while recovery is pending or running
returns HTTP 409.

History:

```text
GET /api/energy/history?resolution=5m&target=total&date=YYYY-MM-DD
GET /api/energy/history?resolution=day&target=total&year=YYYY&from=1&to=366
GET /api/energy/history?resolution=month&target=total&year=YYYY&from=1&to=12
```

`target` defaults to `total` and may also be `inv_<serial>`. The five-minute
endpoint is intentionally limited to one day per request.

Response metadata:

```text
target
resolution
date / year / from / to
interval_sec        only for 5m
count
data[]
scan
```

The `scan` object contains:

```text
files_scanned
valid_blocks
skipped_blocks
valid_records
skipped_records
file_size
last_valid_offset
invalid_final_block
can_truncate_final_block
```

Five-minute rows:

```text
day
slot
yield_wh
avg_power_w
flags
```

Day rows:

```text
day_of_year
yield_wh
max_power_w
avg_power_w
runtime_min
sample_count
flags
```

Month rows:

```text
month
yield_wh
max_power_w
avg_power_w
runtime_min
day_count
flags
```

## File Management API

The Web API exposes a file-management surface for import/export, diagnostics,
and manual recovery. All paths are normalized to absolute paths below
`/energy/`; empty paths, paths ending in `/`, paths containing `..`, paths
containing `//`, and paths outside `/energy/` are rejected.

List managed files:

```text
GET /api/energy/history/file/list
```

Response:

```text
files[].path
files[].size
```

Temporary upload/migration/backup files ending in `.upload`, `.migrate`, or
`.backup` are filtered out of the list.

The file list recursively enumerates `/energy/`, while status/recovery operate
on the known `/energy/5m`, `/energy/day`, and `/energy/month` directories.
Because this endpoint walks LittleFS directories, the UI should avoid calling it
on hot paths unless file-management data or period target discovery is needed.

Deep-scan one managed file:

```text
GET /api/energy/history/file/scan?file=/energy/5m/total_YYYY_MM.eh2
```

Recover one managed file by truncating a corrupt or incomplete final block when
possible:

```text
POST /api/energy/history/file/recover?file=/energy/5m/total_YYYY_MM.eh2
```

Download one managed file:

```text
GET /api/energy/history/file/download?file=/energy/5m/total_YYYY_MM.eh2
```

Delete one managed file:

```text
POST /api/energy/history/file/delete?file=/energy/5m/total_YYYY_MM.eh2
```

Upload and overwrite one managed file:

```text
POST /api/energy/history/file/upload?file=/energy/5m/total_YYYY_MM.eh2
```

Uploads are first written to `<target>.upload` and then renamed over the target
path when the upload finishes.

Migrate one legacy EH01 five-minute file to EH02:

```text
POST /api/energy/history/file/migrate-v2?file=/energy/5m/inv_<serial>_YYYY_MM.eh5
POST /api/energy/history/file/migrate-v2?file=/energy/5m/inv_<serial>_YYYY_MM.eh5&overwrite=1
```

Migration writes `<target>.eh2.migrate`, validates the result, and then renames
it to `.eh2`. Existing `.eh2` files are kept unless `overwrite=1` is supplied.
The source `.eh5` is not deleted automatically.

File imports, deletes, completed uploads, successful migrations, and recovery
operations call `markFilesChanged()`, which updates both revision counters.
Downloads and read-only scans do not change revisions.

## Web UI

The energy history view shows current total live values, loads `total` plus all
configured inverters, and polls the revision endpoint to refresh when history
data, file imports, recovery, or deletes change the backend state. Inverter
target IDs are built from the configured hexadecimal serial converted to the
decimal `inv_<serial>` form used by the backend.

The view can read the managed file list and add up to 32 additional inverter
targets found in matching history files for the selected period. This allows
imported or old inverter history to remain visible even when the inverter is no
longer configured. For five-minute periods, only `.eh2` files participate in
this target discovery.

Available query views:

```text
Day view      -> resolution=5m, one local date
Month view    -> resolution=day, day-of-year range for one month
Year view     -> resolution=month, month range 1..12
```

The chart overlays total power, per-inverter power, and cumulative total energy.
For five-minute data the frontend can show the data range from the first to the
last present slot, a fixed full-day axis, or a seven-day axis. The seven-day
axis loads seven day-scoped backend requests per target and shifts chart
navigation in seven-day steps. Missing slots remain gaps. Automatic revision
refresh is skipped while the five-minute chart is in seven-day mode to avoid
unexpected reloads of the wider view.

A secondary daily-energy chart is loaded for inverter targets for the currently
selected month. It can render inverter energy stacked or as separate bars. The
chart appears as soon as the first inverter series has data and is updated
progressively as the remaining inverter series finish loading. The chart has
its own month navigation and can be opened to drill down into a day.

The comparison area can load the available years from managed `total` 5m, day,
or month files and render a month-by-month yearly comparison, a yearly total
comparison, and a summary table. Clicking a populated comparison month drills
down into that month.

The data-management panel can list files, select a file, preview the file
through the normal history API, download, deep-scan, recover, delete, and upload
history files. It supports multi-file upload, selected-file bulk download,
upload progress with retry/cancel state, and per-file raw scan output. For
five-minute files the preview starts at the first day of the file month and
searches forward for the first day containing data.

## Retention

There is no fixed age-based retention for history files. Data is kept
indefinitely unless LittleFS free space becomes low.

Low-free-space cleanup runs only when a new history file is about to be
created. It keeps enough free space for one compact EH02 five-minute day plus
64 KiB reserve before the new file header is written. The threshold is based on
one target's daily five-minute write volume:

```text
32-byte file header + 288 * 10-byte compact records + 64 KiB
```

Cleanup is allowed to delete only old per-inverter five-minute files:

```text
/energy/5m/inv_<serial>_YYYY_MM.eh2
/energy/5m/inv_<serial>_YYYY_MM.eh5   legacy, if still present
```

Cleanup rules:

1. Delete the oldest per-inverter five-minute files first.
2. Do not delete `/energy/5m/total_YYYY_MM.eh2` or legacy
   `/energy/5m/total_YYYY_MM.eh5` files.
3. Do not delete day or month aggregate files.
4. Ignore unrecognized file names and unrelated LittleFS files.

## Recovery

When reading:

1. Validate file header magic, version, type, size fields, and header CRC.
2. Validate each block header for EH01 files.
3. Validate payload CRC for EH01 blocks or record CRC16 for EH02 records.
4. Ignore invalid blocks.
5. Report skipped blocks through API status/warnings.

In the current runtime implementation, automatic startup recovery is disabled.
Recovery runs only when requested through `POST /api/energy/history/recovery`,
through per-file recovery, or as an append retry after an append failure. The
background recovery request scans the known history directories and tries to
repair files whose headers decode successfully. If only the final EH01 block
or final EH02 record is invalid or incomplete, recovery truncates the file to
the last valid boundary.
