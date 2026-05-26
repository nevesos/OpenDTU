// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <Arduino.h>
#include <TaskSchedulerDeclarations.h>
#include <cstdint>
#include <functional>

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

    struct Status {
        uint32_t filesScanned = 0;
        uint32_t validBlocks = 0;
        uint32_t skippedBlocks = 0;
        uint32_t validRecords = 0;
        uint32_t skippedRecords = 0;
        uint32_t invalidFinalBlockFiles = 0;
        uint32_t truncatableFinalBlockFiles = 0;
        size_t bytesScanned = 0;
        size_t littlefsTotalBytes = 0;
        size_t littlefsUsedBytes = 0;
    };

    struct RecoveryStatus {
        bool pending = false;
        bool running = false;
        uint32_t runCount = 0;
        uint32_t lastStartedMillis = 0;
        uint32_t lastFinishedMillis = 0;
    };

    struct Revision {
        uint32_t dataRevision = 0;
        uint32_t fileRevision = 0;
        uint32_t lastChangeMillis = 0;
    };

    struct FileInfo {
        String path;
        size_t size = 0;
    };

    bool getStatus(Status& status);
    void getRevision(Revision& revision);
    void markDataChanged();
    void markFilesChanged();
    bool requestRecovery();
    void getRecoveryStatus(RecoveryStatus& status);
    bool isManagedFilePath(const String& path, String& normalizedPath);
    bool listFiles(const std::function<void(const FileInfo&)>& visitor);
    bool scanManagedFile(const String& path, ScanResult& result);
    bool recoverManagedFile(const String& path, ScanResult& result);
    bool queryFiveMinuteDay(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, uint8_t day, EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool queryDay(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint16_t fromDayOfYear, uint16_t toDayOfYear, EnergyHistoryFormat::DayRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool queryMonth(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t fromMonth, uint8_t toMonth, EnergyHistoryFormat::MonthRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);

private:
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
        bool demoDataOk = false;
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

    void startupLoop();
    void recoveryLoop();
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
    void markHistoryChanged(bool dataChanged, bool filesChanged);
    void recoverExistingEnergyFiles();
    void recoverEnergyDirectory(const char* directoryPath);
    bool listFilesInDirectory(const char* directoryPath, const std::function<void(const FileInfo&)>& visitor);
    bool scanFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, ScanResult& result);
    bool scanFiveMinuteFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, ScanResult& result);
    template <typename Record>
    bool readRecordFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::FileType fileType, uint8_t recordSize, Record* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result, bool (*decodeRecord)(const uint8_t*, size_t, Record&), bool (*upsertRecord)(Record*, uint16_t, uint16_t&, const Record&));
    bool readFiveMinuteFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::FiveMinuteRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool readDayFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::DayRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool readMonthFile(const char* path, const EnergyHistoryFormat::FileHeader& expectedHeader, EnergyHistoryFormat::MonthRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool runManualPersistenceProbe(ManualProbeResult& result);
    bool writeDemoData();
    bool persistCurrentFiveMinuteSlot();
    bool writeDayRecordsBatched(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, const EnergyHistoryFormat::DayRecord* records, uint16_t recordCount);
    bool writeMonthRecordsBatched(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, const EnergyHistoryFormat::MonthRecord* records, uint16_t recordCount);
    bool buildDayRecordsFromFiveMinuteMonth(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, const uint16_t* requestedDaysOfYear, uint16_t requestedDayCount, EnergyHistoryFormat::DayRecord* records, uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result);
    bool buildMonthRecordFromDayRecords(uint16_t year, uint8_t month, const EnergyHistoryFormat::DayRecord* days, uint16_t dayRecordCount, EnergyHistoryFormat::MonthRecord& record);
    bool finalizeCompletedPeriod(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, uint8_t day, bool finalizeMonth);
    bool buildDayRecordFromFiveMinute(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, uint8_t day, EnergyHistoryFormat::DayRecord& record);
    bool buildMonthRecordFromDay(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, EnergyHistoryFormat::MonthRecord& record);
    bool queuePendingFinalization(EnergyHistoryFormat::TargetType targetType, uint64_t serial, uint16_t year, uint8_t month, uint8_t day, bool finalizeMonth);
    void processPendingFinalizations(uint8_t maxAttempts);

    Task _loopTask;
    Task _startupTask;
    Task _recoveryTask;
    bool _recoveryPending = false;
    bool _recoveryRunning = false;
    uint32_t _recoveryRunCount = 0;
    uint32_t _recoveryLastStartedMillis = 0;
    uint32_t _recoveryLastFinishedMillis = 0;
    bool _lastFiveMinuteSlotValid = false;
    uint16_t _lastFiveMinuteYear = 0;
    uint8_t _lastFiveMinuteMonth = 0;
    uint8_t _lastFiveMinuteDay = 0;
    uint16_t _lastFiveMinuteSlot = 0;
    uint32_t _dataRevision = 1;
    uint32_t _fileRevision = 1;
    uint32_t _lastChangeMillis = 0;

    struct PendingFinalization {
        bool active = false;
        EnergyHistoryFormat::TargetType targetType = EnergyHistoryFormat::TargetType::Total;
        uint64_t serial = 0;
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        bool finalizeMonth = false;
        uint8_t attempts = 0;
    };

    static constexpr uint8_t MaxPendingFinalizations = 12;
    PendingFinalization _pendingFinalizations[MaxPendingFinalizations];
};

extern EnergyHistoryClass EnergyHistory;
