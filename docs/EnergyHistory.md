# Persistent Energy History

This document defines the first on-flash format for the fork-only persistent
energy history feature. Data is stored on LittleFS, not in `config.json`.

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

Daily and monthly aggregates should be append/update-log style where practical.
If duplicate aggregate records exist, the last valid record for the same key
wins.

Daily aggregates are derived from valid five-minute samples for the completed
local day. `yieldWh` is the highest daily yield seen for the day. `maxPowerW` is
derived from the largest positive yield delta between consecutive samples.
`runtimeMin` counts samples marked as producing in five-minute increments.

Monthly aggregates are derived from daily aggregate records for the completed
local month. `yieldWh` is the sum of daily yields, `maxPowerW` is the highest
daily max power, and `runtimeMin` is the sum of daily runtime minutes.

The manual probe build also writes deterministic demo history for API/UI tests
under June 2099. These files are intentionally persistent and are overwritten on
the next probe boot.

## Retention

There is no fixed age-based retention for history files. Data is kept
indefinitely unless LittleFS free space becomes low.

Low-free-space cleanup is allowed to delete only old per-inverter five-minute
files:

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

At startup or before appending, the newest file for a target may be scanned. If
only the final block is invalid or incomplete, the file can be truncated to the
last valid block boundary.
