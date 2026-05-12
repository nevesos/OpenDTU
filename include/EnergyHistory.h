// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <Arduino.h>
#include <TaskSchedulerDeclarations.h>
#include <cstdint>

namespace EnergyHistoryFormat {

static constexpr char FileMagic[4] = { 'E', 'H', '0', '1' };
static constexpr char BlockMagic[4] = { 'E', 'H', 'B', '1' };

static constexpr uint8_t Version = 1;
static constexpr uint8_t FileHeaderSize = 32;
static constexpr uint8_t BlockHeaderSize = 16;

enum class FileType : uint8_t {
    FiveMinute = 1,
    Day = 2,
    Month = 3,
};

enum class TargetType : uint8_t {
    Total = 0,
    Inverter = 1,
};

enum RecordFlags : uint8_t {
    RecordFlagValid = 1 << 0,
    RecordFlagReachable = 1 << 1,
    RecordFlagProducing = 1 << 2,
    RecordFlagDayResetDetected = 1 << 3,
    RecordFlagEstimated = 1 << 4,
};

// Logical value containers only. On-flash headers and records must be
// serialized field-by-field; do not write sizeof(struct).
struct FileHeader {
    char magic[4];
    uint8_t fileType;
    uint8_t version;
    uint8_t headerSize;
    uint8_t recordSize;
    uint16_t year;
    uint8_t month;
    uint8_t targetType;
    uint64_t serial;
    uint16_t intervalSec;
    uint8_t reserved[6];
    uint32_t crc32Header;
};

struct BlockHeader {
    char magic[4];
    uint16_t blockIndex;
    uint16_t startKey;
    uint16_t recordCount;
    uint16_t payloadSize;
    uint32_t crc32Payload;
};

struct FiveMinuteRecord {
    uint8_t day;
    uint16_t slot;
    uint32_t yieldDayWh;
    uint8_t flags;
};

struct DayRecord {
    uint16_t dayOfYear;
    uint32_t yieldWh;
    uint16_t maxPowerW;
    uint16_t avgPowerW;
    uint16_t runtimeMin;
    uint16_t sampleCount;
    uint16_t flags;
};

struct MonthRecord {
    uint8_t month;
    uint8_t reserved0;
    uint32_t yieldWh;
    uint16_t maxPowerW;
    uint16_t avgPowerW;
    uint16_t runtimeMin;
    uint16_t dayCount;
    uint16_t flags;
};

static constexpr uint8_t FiveMinuteRecordSize = 8;
static constexpr uint8_t DayRecordSize = 16;
static constexpr uint8_t MonthRecordSize = 16;
static constexpr uint16_t FiveMinuteIntervalSec = 5 * 60;
static constexpr uint16_t FiveMinuteSlotsPerDay = 24 * 60 / 5;

} // namespace EnergyHistoryFormat

class EnergyHistoryClass {
public:
    EnergyHistoryClass();
    void init(Scheduler& scheduler);

private:
    void loop();
    bool appendBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, uint16_t blockIndex, uint16_t startKey, const uint8_t* payload, uint16_t payloadSize, uint16_t recordCount);
    bool appendFiveMinuteBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, const EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCount, uint16_t blockIndex);

    Task _loopTask;
};

extern EnergyHistoryClass EnergyHistory;
