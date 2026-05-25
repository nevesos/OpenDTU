#!/usr/bin/env python3
"""Generate deterministic EnergyHistory upload fixtures for year 2099.

The output files use the on-flash EH01/EHB1 binary format and can be uploaded
through the Energy History data-management UI.
"""

from __future__ import annotations

import calendar
import math
import random
import shutil
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


YEAR = 2099
OUT_DIR = Path(__file__).resolve().parent / "energy_history_2099_upload"

FILE_MAGIC = b"EH01"
BLOCK_MAGIC = b"EHB1"
VERSION = 1
FILE_HEADER_SIZE = 32
BLOCK_HEADER_SIZE = 16
FIVE_MINUTE_RECORD_SIZE = 8
DAY_RECORD_SIZE = 16
MONTH_RECORD_SIZE = 16
FIVE_MINUTE_INTERVAL_SEC = 300
SLOTS_PER_DAY = 288

FILE_TYPE_5M = 1
FILE_TYPE_DAY = 2
FILE_TYPE_MONTH = 3
TARGET_TOTAL = 0
TARGET_INVERTER = 1

FLAG_VALID = 1 << 0
FLAG_REACHABLE = 1 << 1
FLAG_PRODUCING = 1 << 2
FLAG_DAY_RESET = 1 << 3
FLAG_ESTIMATED = 1 << 4


@dataclass(frozen=True)
class Target:
    name: str
    target_type: int
    serial: int
    scale: float


@dataclass
class FiveMinuteRecord:
    day: int
    slot: int
    yield_wh: int
    flags: int


@dataclass
class DayRecord:
    day_of_year: int
    yield_wh: int
    max_power_w: int
    avg_power_w: int
    runtime_min: int
    sample_count: int
    flags: int


@dataclass
class MonthRecord:
    month: int
    yield_wh: int
    max_power_w: int
    avg_power_w: int
    runtime_min: int
    day_count: int
    flags: int


TARGETS = [
    Target("total", TARGET_TOTAL, 0, 1.0),
    Target("inv_999999990101", TARGET_INVERTER, 999999990101, 0.56),
    Target("inv_999999990102", TARGET_INVERTER, 999999990102, 0.44),
]


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def file_header(file_type: int, target: Target, month: int = 0) -> bytes:
    record_size = {
        FILE_TYPE_5M: FIVE_MINUTE_RECORD_SIZE,
        FILE_TYPE_DAY: DAY_RECORD_SIZE,
        FILE_TYPE_MONTH: MONTH_RECORD_SIZE,
    }[file_type]
    interval = FIVE_MINUTE_INTERVAL_SEC if file_type == FILE_TYPE_5M else 0
    header_month = month if file_type == FILE_TYPE_5M else 0
    header = bytearray(FILE_HEADER_SIZE)
    header[0:4] = FILE_MAGIC
    header[4] = file_type
    header[5] = VERSION
    header[6] = FILE_HEADER_SIZE
    header[7] = record_size
    struct.pack_into("<H", header, 8, YEAR)
    header[10] = header_month
    header[11] = target.target_type
    struct.pack_into("<Q", header, 12, target.serial)
    struct.pack_into("<H", header, 20, interval)
    struct.pack_into("<I", header, 28, crc32(header[:28]))
    return bytes(header)


def block(block_index: int, start_key: int, payload: bytes) -> bytes:
    header = bytearray(BLOCK_HEADER_SIZE)
    header[0:4] = BLOCK_MAGIC
    record_count = len(payload) // record_size_from_payload(payload)
    struct.pack_into("<H", header, 4, block_index)
    struct.pack_into("<H", header, 6, start_key)
    struct.pack_into("<H", header, 8, record_count)
    struct.pack_into("<H", header, 10, len(payload))
    struct.pack_into("<I", header, 12, crc32(payload))
    return bytes(header) + payload


def record_size_from_payload(payload: bytes) -> int:
    if len(payload) % MONTH_RECORD_SIZE == 0 and len(payload) > FIVE_MINUTE_RECORD_SIZE:
        return MONTH_RECORD_SIZE
    return FIVE_MINUTE_RECORD_SIZE


def block_with_count(block_index: int, start_key: int, payload: bytes, record_count: int) -> bytes:
    header = bytearray(BLOCK_HEADER_SIZE)
    header[0:4] = BLOCK_MAGIC
    struct.pack_into("<H", header, 4, block_index)
    struct.pack_into("<H", header, 6, start_key)
    struct.pack_into("<H", header, 8, record_count)
    struct.pack_into("<H", header, 10, len(payload))
    struct.pack_into("<I", header, 12, crc32(payload))
    return bytes(header) + payload


def encode_5m(record: FiveMinuteRecord) -> bytes:
    return struct.pack("<BHIB", record.day, record.slot, record.yield_wh, record.flags)


def encode_day(record: DayRecord) -> bytes:
    return struct.pack(
        "<HIHHHHH",
        record.day_of_year,
        record.yield_wh,
        record.max_power_w,
        record.avg_power_w,
        record.runtime_min,
        record.sample_count,
        record.flags,
    )


def encode_month(record: MonthRecord) -> bytes:
    return struct.pack(
        "<BBIHHHHH",
        record.month,
        0,
        record.yield_wh,
        record.max_power_w,
        record.avg_power_w,
        record.runtime_min,
        record.day_count,
        record.flags,
    )


def day_of_year(month: int, day: int) -> int:
    return sum(calendar.monthrange(YEAR, m)[1] for m in range(1, month)) + day


def month_day_from_doy(doy: int) -> tuple[int, int]:
    remaining = doy
    for month in range(1, 13):
        days = calendar.monthrange(YEAR, month)[1]
        if remaining <= days:
            return month, remaining
        remaining -= days
    raise ValueError(doy)


def daylight_slots(doy: int) -> tuple[int, int]:
    seasonal = math.sin(2 * math.pi * (doy - 80) / 365.0)
    length_hours = 12.0 + 4.4 * seasonal
    sunrise_hour = 12.0 - length_hours / 2.0
    sunset_hour = 12.0 + length_hours / 2.0
    return max(0, round(sunrise_hour * 12)), min(287, round(sunset_hour * 12))


def expected_total_yield(doy: int, rng: random.Random) -> int:
    season = max(0.16, math.sin(math.pi * (doy - 15) / 365.0))
    cloud = rng.uniform(0.72, 1.05)
    if doy in {22, 63, 94, 136, 188, 254, 312}:
        cloud *= 0.33
    if doy in {46, 121, 199, 288, 333}:
        cloud *= 0.55
    return round((2900 + 23800 * season) * cloud)


def target_yield(total_yield: int, target: Target, doy: int) -> int:
    if target.target_type == TARGET_TOTAL:
        return total_yield
    imbalance = 1.0 + 0.025 * math.sin((doy + target.serial % 37) / 17.0)
    return round(total_yield * target.scale * imbalance)


def records_for_day(target: Target, month: int, day: int) -> list[FiveMinuteRecord]:
    doy = day_of_year(month, day)
    rng = random.Random(YEAR * 100000 + target.serial + doy)
    total = expected_total_yield(doy, rng)
    daily_total = target_yield(total, target, doy)
    start_slot, end_slot = daylight_slots(doy)
    records: list[FiveMinuteRecord] = []

    if doy == 14:
        return records

    for slot in range(start_slot, end_slot + 1):
        if doy == 63 and 120 <= slot <= 146:
            continue
        if doy == 288 and slot % 9 == 0:
            continue

        progress = (slot - start_slot) / max(1, end_slot - start_slot)
        curve = 0.5 - 0.5 * math.cos(math.pi * progress)
        ripple = 1.0 + 0.018 * math.sin(slot / 5.0 + target.serial % 11)
        yield_wh = max(0, round(daily_total * curve * ripple))
        flags = FLAG_VALID | FLAG_REACHABLE

        producing = yield_wh > 0 and slot < end_slot
        if producing:
            flags |= FLAG_PRODUCING

        if doy == 107 and target.name == "inv_999999990102" and 120 <= slot <= 144:
            flags = FLAG_VALID
            yield_wh = records[-1].yield_wh if records else 0

        if doy == 172 and 116 <= slot <= 164:
            flags |= FLAG_ESTIMATED

        if doy == 190 and slot >= 150:
            if slot == 150:
                yield_wh = max(0, yield_wh - 4200)
            elif records:
                yield_wh = max(records[-1].yield_wh, yield_wh - 4200)
            flags |= FLAG_DAY_RESET

        records.append(FiveMinuteRecord(day, slot, yield_wh, flags))

    if records:
        final = records[-1]
        final.yield_wh = max(final.yield_wh, daily_total)
        final.flags = FLAG_VALID | FLAG_REACHABLE
    return records


def day_record(doy: int, records: list[FiveMinuteRecord]) -> DayRecord | None:
    if not records:
        return None

    max_yield = max(record.yield_wh for record in records if record.flags & FLAG_VALID)
    runtime_min = sum(5 for record in records if record.flags & FLAG_PRODUCING)
    flags = FLAG_VALID
    max_power = 0
    previous: FiveMinuteRecord | None = None
    for record in records:
        flags |= record.flags & (FLAG_REACHABLE | FLAG_PRODUCING | FLAG_ESTIMATED | FLAG_DAY_RESET)
        if previous and record.yield_wh >= previous.yield_wh and record.slot > previous.slot:
            delta_wh = record.yield_wh - previous.yield_wh
            delta_sec = (record.slot - previous.slot) * FIVE_MINUTE_INTERVAL_SEC
            max_power = max(max_power, round(delta_wh * 3600 / delta_sec))
        elif previous and record.yield_wh < previous.yield_wh:
            flags |= FLAG_DAY_RESET
        previous = record

    avg_power = round(max_yield * 60 / runtime_min) if runtime_min else 0
    return DayRecord(
        day_of_year=doy,
        yield_wh=max_yield,
        max_power_w=min(max_power, 65535),
        avg_power_w=min(avg_power, 65535),
        runtime_min=min(runtime_min, 65535),
        sample_count=len(records),
        flags=flags,
    )


def month_record(month: int, days: list[DayRecord]) -> MonthRecord | None:
    valid_days = [day for day in days if day.flags & FLAG_VALID]
    if not valid_days:
        return None
    yield_wh = sum(day.yield_wh for day in valid_days)
    runtime = sum(day.runtime_min for day in valid_days)
    flags = FLAG_VALID
    for day in valid_days:
        flags |= day.flags & (FLAG_REACHABLE | FLAG_PRODUCING | FLAG_ESTIMATED | FLAG_DAY_RESET)
    avg_power = round(yield_wh * 60 / runtime) if runtime else 0
    return MonthRecord(
        month=month,
        yield_wh=yield_wh,
        max_power_w=max(day.max_power_w for day in valid_days),
        avg_power_w=min(avg_power, 65535),
        runtime_min=min(runtime, 65535),
        day_count=len(valid_days),
        flags=flags,
    )


def write_5m_file(target: Target, month: int, records_by_day: dict[int, list[FiveMinuteRecord]]) -> None:
    path = OUT_DIR / "energy" / "5m" / f"{target.name}_{YEAR}_{month:02d}.eh5"
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(file_header(FILE_TYPE_5M, target, month))
    for day in range(1, calendar.monthrange(YEAR, month)[1] + 1):
        for record in records_by_day.get(day, []):
            payload = encode_5m(record)
            block_index = (day - 1) * SLOTS_PER_DAY + record.slot
            data += block_with_count(block_index, day, payload, 1)

            if target.name == "total" and month == 6 and day == 21 and record.slot == 144:
                corrected = FiveMinuteRecord(day, record.slot, record.yield_wh + 110, record.flags | FLAG_ESTIMATED)
                data += block_with_count(block_index, day, encode_5m(corrected), 1)

    if target.name == "total" and month == 9:
        data += BLOCK_MAGIC + b"\x01\x02\x03\x04"

    path.write_bytes(bytes(data))


def write_day_file(target: Target, day_records: list[DayRecord]) -> None:
    path = OUT_DIR / "energy" / "day" / f"{target.name}_{YEAR}.ehd"
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(file_header(FILE_TYPE_DAY, target))
    for record in day_records:
        data += block_with_count(record.day_of_year, record.day_of_year, encode_day(record), 1)
        if target.name == "total" and record.day_of_year == 190:
            corrected = DayRecord(
                record.day_of_year,
                record.yield_wh + 350,
                record.max_power_w,
                record.avg_power_w,
                record.runtime_min,
                record.sample_count,
                record.flags | FLAG_DAY_RESET,
            )
            data += block_with_count(record.day_of_year + 400, record.day_of_year, encode_day(corrected), 1)
    path.write_bytes(bytes(data))


def write_month_file(target: Target, month_records: list[MonthRecord]) -> None:
    path = OUT_DIR / "energy" / "month" / f"{target.name}_{YEAR}.ehm"
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(file_header(FILE_TYPE_MONTH, target))
    for record in month_records:
        data += block_with_count(record.month, record.month, encode_month(record), 1)
    path.write_bytes(bytes(data))


def write_readme() -> None:
    readme = OUT_DIR / "README.md"
    readme.write_text(
        "\n".join(
            [
                "# Energy History Testdaten 2099",
                "",
                "Diese Dateien sind Upload-Fixtures fuer die Energy-History-Oberflaeche.",
                "Beim Upload muss der Zielpfad dem relativen Pfad unter diesem Ordner entsprechen,",
                "zum Beispiel `/energy/5m/total_2099_01.eh5`.",
                "",
                "Ziele:",
                "",
                "- `total`",
                "- `inv_999999990101`",
                "- `inv_999999990102`",
                "",
                "Enthaltene Besonderheiten:",
                "",
                "- ganzes Kalenderjahr 2099 mit 5-Minuten-, Tages- und Monatsdateien",
                "- saisonale und taegliche Ertragsvariation",
                "- 2099-01-14: kompletter Datenausfall",
                "- 2099-03-04: fehlende Mittags-Samples",
                "- 2099-04-17: WR 2 zeitweise nicht erreichbar",
                "- 2099-06-21: geschaetzte Samples und ein korrigierter Duplicate-Slot",
                "- 2099-07-09: Day-Reset-Flag und korrigierter Tagesdatensatz",
                "- 2099-09 total 5m: absichtlich unvollstaendiger finaler Block fuer Recovery-Tests",
                "- 2099-10-15: einzelne fehlende Samples",
                "",
            ]
        ),
        encoding="ascii",
    )


def main() -> None:
    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)

    for target in TARGETS:
        all_day_records: list[DayRecord] = []
        month_records: list[MonthRecord] = []

        for month in range(1, 13):
            records_by_day: dict[int, list[FiveMinuteRecord]] = {}
            day_records_for_month: list[DayRecord] = []
            for day in range(1, calendar.monthrange(YEAR, month)[1] + 1):
                records = records_for_day(target, month, day)
                records_by_day[day] = records
                record = day_record(day_of_year(month, day), records)
                if record is not None:
                    all_day_records.append(record)
                    day_records_for_month.append(record)

            write_5m_file(target, month, records_by_day)
            monthly = month_record(month, day_records_for_month)
            if monthly is not None:
                month_records.append(monthly)

        write_day_file(target, all_day_records)
        write_month_file(target, month_records)

    write_readme()
    print(f"Wrote {len(list(OUT_DIR.rglob('*.*')))} files to {OUT_DIR}")


if __name__ == "__main__":
    main()
