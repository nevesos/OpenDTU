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
    struct ScanResult {
        uint32_t filesScanned = 0;
        uint32_t validBlocks = 0;
        uint32_t skippedBlocks = 0;
        uint32_t validRecords = 0;
        uint32_t skippedRecords = 0;
        size_t fileSize = 0;
        size_t lastValidOffset = 0;
        bool invalidFinalBlock = false;
        bool canTruncateFinalBlock = false;
    };

    struct ManualProbeResult {
        bool fiveMinuteOk = false;
        bool dayOk = false;
        bool monthOk = false;
        bool headerMismatchOk = false;
        bool corruptHeaderOk = false;
        bool corruptCrcOk = false;
        bool incompleteFinalBlockOk = false;
        bool truncateFinalBlockOk = false;
        bool smallBufferOk = false;
        bool cleanupOk = false;
        uint16_t fiveMinuteRecordsRead = 0;
        uint16_t dayRecordsRead = 0;
        uint16_t monthRecordsRead = 0;
        ScanResult fiveMinuteScan;
        ScanResult dayScan;
        ScanResult monthScan;
        ScanResult corruptCrcScan;
        ScanResult incompleteFinalBlockScan;
        ScanResult smallBufferScan;
    };

    void loop();
    bool makeFileHeader(EnergyHistoryFormat::FileType fileType, EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, EnergyHistoryFormat::FileHeader& header);
    String makeFiveMinutePath(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month);
    String makeDayPath(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year);
    String makeMonthPath(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year);
    bool writeFiveMinute(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, const EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCount, uint16_t blockIndex);
    bool writeDay(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, const EnergyHistoryFormat::DayRecord* records, uint16_t recordCount, uint16_t blockIndex);
    bool writeMonth(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, const EnergyHistoryFormat::MonthRecord* records, uint16_t recordCount, uint16_t blockIndex);
    bool readFiveMinute(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool readDay(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, EnergyHistoryFormat::DayRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool readMonth(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, EnergyHistoryFormat::MonthRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool appendBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, uint16_t blockIndex, uint16_t startKey, const uint8_t* payload, uint16_t payloadSize, uint16_t recordCount);
    bool appendFiveMinuteBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, const EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCount, uint16_t blockIndex);
    bool appendDayBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, const EnergyHistoryFormat::DayRecord* records, uint16_t recordCount, uint16_t blockIndex);
    bool appendMonthBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, const EnergyHistoryFormat::MonthRecord* records, uint16_t recordCount, uint16_t blockIndex);
    bool recoverFinalBlock(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, ScanResult& result);
    bool truncateFile(const char* path, size_t size);
    void recoverExistingEnergyFiles();
    void recoverEnergyDirectory(const char* directoryPath);
    bool scanFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, ScanResult& result);
    bool scanFiveMinuteFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, ScanResult& result);
    template <typename Record>
    bool readRecordFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::FileType fileType, uint8_t recordSize, Record* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result, bool (*decodeRecord)(const uint8_t*, size_t, Record&), bool (*upsertRecord)(Record*, uint16_t, uint16_t&, const Record&));
    bool readFiveMinuteFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool readDayFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::DayRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool readMonthFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::MonthRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool runManualPersistenceProbe(ManualProbeResult& result);
    bool persistCurrentFiveMinuteSlot();
    bool finalizeCompletedPeriod(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, uint8_t day, bool finalizeMonth);
    bool buildDayRecordFromFiveMinute(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, uint8_t day, EnergyHistoryFormat::DayRecord& record);
    bool buildMonthRecordFromDay(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, EnergyHistoryFormat::MonthRecord& record);

    Task _loopTask;
    bool _lastFiveMinuteSlotValid = false;
    uint16_t _lastFiveMinuteYear = 0;
    uint8_t _lastFiveMinuteMonth = 0;
    uint8_t _lastFiveMinuteDay = 0;
    uint16_t _lastFiveMinuteSlot = 0;
};

extern EnergyHistoryClass EnergyHistory;
