# Persistent Energy History

This document defines the first on-flash format for the fork-only persistent
energy history feature. Data is stored on LittleFS, not in `config.json`.

The feature is enabled only for builds that define `ENERGY_HISTORY_ENABLE`.
The manual probe build additionally defines `ENERGY_HISTORY_MANUAL_PROBE`.

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

Five-minute data is stored in monthly files:

```text
/energy/5m/total_YYYY_MM.eh5
/energy/5m/inv_<serial>_YYYY_MM.eh5
```

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
magic        4 bytes   "EH01"
fileType     uint8     1=5m, 2=day, 3=month
version      uint8     1
headerSize   uint8     32
recordSize   uint8     depends on fileType
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

## Five-Minute Record

Size: 8 bytes.

```text
day          uint8     1..31
slot         uint16    0..287, local five-minute slot in the day
yieldDayWh   uint32    daily yield in Wh
flags        uint8
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

Five-minute data should be appended in small blocks. The API must deduplicate by
`day + slot` when repeated writes occur; the last valid record wins. This avoids
in-place rewrites and keeps flash wear low.

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

Append blocks are intentionally small. Five-minute runtime writes append one
record per block. Day and month aggregate writers support up to four records per
block.

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
manual recovery task. The loop task runs every five minutes.

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
failures do not permanently lose the aggregate.

Runtime startup recovery is not executed automatically from the scheduler,
because scanning all files can block other scheduler-driven services. Recovery
can be requested through the Web API. Before appending, a failed append attempts
to recover a truncatable final block and then retries the append once.

Changes to history data and managed files increment in-memory revision counters.
`markDataChanged()` increments only the data revision. `markFilesChanged()`
increments both data and file revisions, because file imports/deletes can change
query results and the visible file list.

## Backend Query Surface

The firmware exposes narrow backend methods for future Web API use:

```text
lightweight status scan across energy files
revision counters for UI polling
5m query for one target and one local day
day query for one target and a day-of-year range
month query for one target and a month range
```

The five-minute query is day-scoped by design so callers cannot accidentally
request an unbounded monthly dump.

Queries return deduplicated logical records. If multiple valid records exist for
the same key, the last valid record wins.

Day queries first read persisted day records. Missing requested days are rebuilt
from the corresponding five-minute month file when possible, returned to the
caller, and persisted back into the day file in append blocks.

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
valid_blocks
skipped_blocks
valid_records
skipped_records
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

Temporary upload files ending in `.upload` are filtered out of the list.

Deep-scan one managed file:

```text
GET /api/energy/history/file/scan?file=/energy/5m/total_YYYY_MM.eh5
```

Recover one managed file by truncating a corrupt or incomplete final block when
possible:

```text
POST /api/energy/history/file/recover?file=/energy/5m/total_YYYY_MM.eh5
```

Download one managed file:

```text
GET /api/energy/history/file/download?file=/energy/5m/total_YYYY_MM.eh5
```

Delete one managed file:

```text
POST /api/energy/history/file/delete?file=/energy/5m/total_YYYY_MM.eh5
```

Upload and overwrite one managed file:

```text
POST /api/energy/history/file/upload?file=/energy/5m/total_YYYY_MM.eh5
```

Uploads are first written to `<target>.upload` and then renamed over the target
path when the upload finishes.

File imports, deletes, and completed uploads call `markFilesChanged()`, which
updates both revision counters. Downloads and read-only scans do not change
revisions.

## Web UI

The energy history view shows current total live values, loads `total` plus all
configured inverters, and polls the revision endpoint to refresh when history
data, file imports, recovery, or deletes change the backend state. Inverter
target IDs are built from the configured hexadecimal serial converted to the
decimal `inv_<serial>` form used by the backend.

Available query views:

```text
Day view      -> resolution=5m, one local date
Month view    -> resolution=day, day-of-year range for one month
Year view     -> resolution=month, month range 1..12
```

The chart overlays total power, per-inverter power, and cumulative total energy.
For five-minute data the frontend can show either the data range from the first
to the last present slot or a fixed full-day axis. Missing slots remain gaps.

A secondary daily-energy chart is loaded for inverter targets for the currently
selected month. It can render inverter energy stacked or as separate bars. The
manual probe demo inverters are shown automatically for June 2099 when those
targets are not present in the normal inverter list.

The comparison area can load the available years from managed `total` files and
render a month-by-month yearly comparison, a yearly total comparison, and a
summary table.

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
created. It keeps enough free space for one per-target five-minute day file
plus 32 KiB reserve before the new file header is written.

Cleanup is allowed to delete only old per-inverter five-minute files:

```text
/energy/5m/inv_<serial>_YYYY_MM.eh5
```

Cleanup rules:

1. Delete the oldest per-inverter five-minute files first.
2. Do not delete `/energy/5m/total_YYYY_MM.eh5`.
3. Do not delete day or month aggregate files.
4. Ignore unrecognized file names and unrelated LittleFS files.

## Recovery

When reading:

1. Validate file header magic, version, type, size fields, and header CRC.
2. Validate each block header.
3. Validate payload CRC.
4. Ignore invalid blocks.
5. Report skipped blocks through API status/warnings.

In the current runtime implementation, automatic startup recovery is disabled.
Recovery runs only when requested through `POST /api/energy/history/recovery`,
through per-file recovery, or as an append retry after an append failure. If
only the final block is invalid or incomplete, recovery truncates the file to
the last valid block boundary.
