# Energy History Implementation Agent

Use this briefing for future Codex/agent sessions that assist with the
persistent energy history feature.

The agent is an implementation assistant, not an autonomous owner. It must help
the user plan, reason, inspect code, and implement explicitly approved steps. It
must not independently continue the feature beyond the user's current request.

## Mission

Assist the user with implementing a fork-only persistent energy history feature
for OpenDTU. The feature stores inverter and total energy history on
LittleFS/flash and exposes it later through the Web API and webapp. It must not
store history data in `config.json`.

The implementation should preserve `feature/module-overview` as a clean upstream
PR branch. Work belongs on:

```text
feature/persistent-energy-history
```

The feature is intended for the user's long-lived fork, not necessarily for
upstream OpenDTU.

## Collaboration Rules

Do not develop autonomously. Work only on the concrete step the user asks for.

Before making code changes, explain what files will be touched and why. Keep the
scope narrow.

If a requirement, tradeoff, format detail, retention rule, API behavior, UI
behavior, or migration behavior is unclear, ask a concise question before
proceeding. Do not fill gaps with silent assumptions.

If there are multiple reasonable approaches, present the options with practical
tradeoffs and ask the user to choose unless one option has already been agreed.

If the user asks for planning or discussion, do not edit files.

If the user asks for implementation, implement only the agreed step and stop
after verification with a short summary.

Treat `docs/EnergyHistory.md` as the current agreement. Any deviation from it
requires explicit user confirmation and a matching documentation update.

## Current Baseline

Relevant branch state when this briefing was created:

```text
feature/persistent-energy-history
469f5473 feat: add custom partition scheme for 16MB energy history support
```

The branch is based on `fork/main` / `feature/module-overview`.

Existing new files:

```text
docs/EnergyHistory.md
include/EnergyHistory.h
src/EnergyHistory.cpp
```

`EnergyHistory.cpp` is currently only a skeleton. It is intentionally not wired
into `main.cpp` yet.

The selected build environment is:

```text
generic_esp32s3_usb_16mb_energy_history
```

A verification build succeeded after adding the skeleton:

```text
C:\Users\uwero\.platformio\penv\Scripts\pio.exe run -e generic_esp32s3_usb_16mb_energy_history
```

Generated firmware size was about 1.73 MB.

## Non-Negotiable Design Decisions

Use LittleFS flash persistence. Do not store history samples in `config.json`.

Store separate data series for:

```text
total
inv_<serial>
```

Use inverter serial numbers in filenames, not inverter names.

Five-minute records do not store instantaneous `powerW`. Historical average
power is derived from energy deltas.

The five-minute record is 8 bytes:

```text
day          uint8     1..31
slot         uint16    0..287
yieldDayWh   uint32
flags        uint8
```

Use explicit little-endian serialization. Do not write raw C++ structs to flash.

Use CRC32 per block. Corrupt blocks must be ignored; a corrupt final block may be
truncated to the last valid block boundary.

Prefer append/log-style writes. Avoid frequent in-place rewrites.

Sampling target:

```text
every 5 minutes
```

Persistence target:

```text
every 15 minutes, plus day/month boundary finalization
```

Default retention target:

```text
5m total:         24 months
5m per inverter:  12 months
day:              30 years
month:            30 years
```

## Format Reference

The canonical format specification is:

```text
docs/EnergyHistory.md
```

Keep implementation aligned with that file. If the implementation requires a
format change, update the document in the same commit.

## Existing Code Patterns To Follow

Backend modules are currently flat pairs:

```text
include/<Module>.h
src/<Module>.cpp
```

Scheduler-backed classes usually expose:

```cpp
void init(Scheduler& scheduler);
```

and keep a private `Task`.

Examples to inspect before editing:

```text
include/Datastore.h
src/Datastore.cpp
include/SunPosition.h
src/SunPosition.cpp
src/WebApi.cpp
include/WebApi.h
src/WebApi_* .cpp
include/WebApi_* .h
```

Use `Datastore` as the primary source for total values:

```cpp
Datastore.getTotalAcYieldDayEnabled()
Datastore.getTotalAcYieldTotalEnabled()
Datastore.getTotalAcPowerEnabled()
```

For per-inverter values, inspect the existing `Hoymiles` access patterns in
`Datastore.cpp` and `WebApi_ws_live.cpp`.

## Implementation Roadmap

### Phase 1: Serialization Layer

Implement helpers inside `EnergyHistory` or a local namespace:

```text
write uint8/uint16/uint32/uint64 little-endian
read uint8/uint16/uint32/uint64 little-endian
CRC32 calculation
file header encode/decode
block header encode/decode
record encode/decode
```

Add focused unit-like tests only if the repo's test setup supports them without
large framework churn. Otherwise verify with PlatformIO build and keep helper
logic small and reviewable.

### Phase 2: File Writer

Implement append block writing for:

```text
/energy/5m/
/energy/day/
/energy/month/
```

Required behavior:

```text
create directories if missing
create file header if file does not exist
validate existing header before append
append block header + payload
flush/close after write
```

Do not wire automatic sampling yet until manual/internal write paths are
reviewable.

### Phase 3: Reader And Recovery

Implement scanning:

```text
validate file header
iterate blocks
check block magic, sizes, CRC
ignore invalid blocks
deduplicate records by key; last valid record wins
optionally truncate only invalid final block
```

Expose internal status data:

```text
files scanned
blocks valid
blocks skipped
oldest/newest timestamp
estimated storage usage
```

### Phase 4: Sampler

Wire `EnergyHistory.init(scheduler)` in `main.cpp` only after the writer/reader
is in place.

Sampling rules:

```text
only write after local time is plausible
sample every 5 minutes
persist every 15 minutes
do not write night-only zero spam
store total and enabled/polling inverters
include flags for valid/reachable/producing/reset
```

Handle day changes, DST/local time, reboots, and inverter day-yield resets
conservatively. Prefer gaps over invented data.

### Phase 5: Retention

Implement retention deletion:

```text
delete oldest 5m files first
keep day/month aggregates unless explicitly configured
protect config and unrelated LittleFS files
```

### Phase 6: Web API

Add a dedicated Web API module:

```text
include/WebApi_energy_history.h
src/WebApi_energy_history.cpp
```

Register it in:

```text
include/WebApi.h
src/WebApi.cpp
```

Suggested endpoints:

```text
GET /api/energy/history/status
GET /api/energy/history?resolution=5m|day|month&target=total|inv_<serial>&from=YYYY-MM-DD&to=YYYY-MM-DD
```

Use readonly credentials like other status endpoints.

API rules:

```text
default target is total
avoid huge 5m dumps by default
derive avgPowerW from yield deltas for 5m responses
surface warnings for skipped corrupt blocks
```

### Phase 7: Webapp

Add the UI only after the API is stable.

Likely files:

```text
webapp/src/router/index.ts
webapp/src/components/NavBar.vue
webapp/src/views/EnergyHistoryView.vue
webapp/src/locales/*.json
```

Keep the UI compact and operational. Do not build a landing page. Prefer
selectable target/resolution/date range and a chart/table that can handle
missing values.

After webapp changes, rebuild `webapp_dist` according to the repo's existing
workflow and re-run the firmware build.

## Engineering Constraints

Do not rewrite unrelated code.

Do not make speculative improvements.

Do not wire the feature into runtime startup, API, or webapp navigation unless
the user explicitly asks for that phase.

Keep changes in small commits:

```text
1. format/spec/skeleton
2. serialization helpers
3. writer
4. reader/recovery
5. sampler wiring
6. API
7. webapp
```

Respect uncommitted user changes. Do not revert files unless explicitly asked.

Use `rg` for searches.

Use `apply_patch` for manual edits.

When running PlatformIO on this machine, prefer:

```text
C:\Users\uwero\.platformio\penv\Scripts\pio.exe run -e generic_esp32s3_usb_16mb_energy_history
```

If sandbox permissions block `.pio` or `C:\Users\uwero\.platformio`, request
escalation instead of working around it.

## Review Checklist

Before finalizing each implementation step:

```text
no raw struct writes to flash
header and block sizes match docs
CRC failure does not crash reader
invalid records are skipped
API cannot allocate unbounded JSON for long ranges
filesystem full behavior prefers deleting old 5m data
PlatformIO build passes for generic_esp32s3_usb_16mb_energy_history
firmware still fits within the 3 MB OTA slot
```

## Suggested First Task For Next Session

Implement the serialization and CRC helper layer for `EnergyHistory` without
wiring it into runtime sampling yet. Keep it private to `EnergyHistory.cpp`
unless other modules need it later. Verify with a PlatformIO build.
