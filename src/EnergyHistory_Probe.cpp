// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "EnergyHistory.h"
#include <LittleFS.h>
#include <esp_log.h>
#include <inttypes.h>
#include <cstring>

namespace {

using namespace EnergyHistoryFormat;

static constexpr uint64_t ManualProbeSerial = 999999999001ULL;
static constexpr uint16_t ManualProbeYear = 2099;
static constexpr uint8_t ManualProbeMonth = 1;
static constexpr const char* ProbeTag = "EnergyHistoryProbe";

template <typename Scan>
bool verifyScan(const Scan& scan, const uint32_t validRecords)
{
    return scan.validBlocks == 2
            && scan.validRecords == validRecords
            && scan.skippedBlocks == 0
            && scan.skippedRecords == 0
            && !scan.invalidFinalBlock
            && !scan.canTruncateFinalBlock;
}

bool truncateProbeFile(const String& path)
{
    File file = LittleFS.open(path, "w");
    return file;
}

bool writeProbeBytes(const String& path, const uint8_t* data, const size_t length)
{
    File file = LittleFS.open(path, "w");
    return file && file.write(data, length) == length;
}

bool appendProbeBytes(const String& path, const uint8_t* data, const size_t length)
{
    File file = LittleFS.open(path, "a");
    return file && file.write(data, length) == length;
}

bool overwriteProbeByte(const String& path, const size_t offset, const uint8_t value)
{
    File file = LittleFS.open(path, "r+");
    return file && file.seek(offset) && file.write(value) == 1;
}

size_t probeFileSize(const String& path)
{
    File file = LittleFS.open(path, "r", false);
    return file ? file.size() : 0;
}

bool ensureProbeDirectory(const char* path)
{
    if (LittleFS.mkdir(path)) {
        return true;
    }

    File directory = LittleFS.open(path, "r", false);
    return directory && directory.isDirectory();
}

} // namespace

bool EnergyHistoryClass::runManualPersistenceProbe(ManualProbeResult& result)
{
    esp_log_level_set(ProbeTag, ESP_LOG_VERBOSE);
    result = ManualProbeResult();
    ESP_LOGI(ProbeTag, "probe start");

    const String fiveMinutePath = makeFiveMinutePath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth);
    const String dayPath = makeDayPath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear);
    const String monthPath = makeMonthPath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear);
    if (fiveMinutePath.isEmpty() || dayPath.isEmpty() || monthPath.isEmpty()) {
        ESP_LOGE(ProbeTag, "path creation failed");
        return false;
    }

    const FiveMinuteRecord firstFiveMinuteBlock[] = {
        { 1, 0, 10, RecordFlagValid },
        { 1, 1, 20, RecordFlagValid },
    };
    const FiveMinuteRecord secondFiveMinuteBlock[] = {
        { 1, 1, 30, RecordFlagValid },
    };
    const DayRecord firstDayBlock[] = {
        { 1, 100, 80, 20, 30, 6, RecordFlagValid },
        { 2, 200, 90, 30, 40, 8, RecordFlagValid },
    };
    const DayRecord secondDayBlock[] = {
        { 2, 250, 95, 35, 45, 9, RecordFlagValid | RecordFlagEstimated },
    };
    const MonthRecord firstMonthBlock[] = {
        { 1, 0, 1000, 100, 40, 300, 2, RecordFlagValid },
        { 2, 0, 2000, 110, 50, 400, 3, RecordFlagValid },
    };
    const MonthRecord secondMonthBlock[] = {
        { 2, 0, 2500, 120, 60, 450, 4, RecordFlagValid | RecordFlagEstimated },
    };

    const bool energyDirOk = ensureProbeDirectory("/energy");
    const bool fiveMinuteDirOk = ensureProbeDirectory("/energy/5m");
    const bool dayDirOk = ensureProbeDirectory("/energy/day");
    const bool monthDirOk = ensureProbeDirectory("/energy/month");
    const bool truncateFiveMinuteOk = truncateProbeFile(fiveMinutePath);
    const bool truncateDayOk = truncateProbeFile(dayPath);
    const bool truncateMonthOk = truncateProbeFile(monthPath);
    const bool cleanupBeforeWriteOk = energyDirOk
            && fiveMinuteDirOk
            && dayDirOk
            && monthDirOk
            && truncateFiveMinuteOk
            && truncateDayOk
            && truncateMonthOk;
    ESP_LOGI(ProbeTag, "setup: dirs=%u/%u/%u/%u truncate=%u/%u/%u", energyDirOk, fiveMinuteDirOk, dayDirOk, monthDirOk, truncateFiveMinuteOk, truncateDayOk, truncateMonthOk);
    if (!cleanupBeforeWriteOk) {
        ESP_LOGE(ProbeTag, "setup failed");
        LittleFS.remove(fiveMinutePath);
        LittleFS.remove(dayPath);
        LittleFS.remove(monthPath);
        return false;
    }

    const bool fiveMinuteWrite0Ok = writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 2, 0);
    const bool fiveMinuteWrite1Ok = writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, secondFiveMinuteBlock, 1, 1);

    const bool dayWrite0Ok = writeDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, firstDayBlock, 2, 0);
    const bool dayWrite1Ok = writeDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, secondDayBlock, 1, 1);

    const bool monthWrite0Ok = writeMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, firstMonthBlock, 2, 0);
    const bool monthWrite1Ok = writeMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, secondMonthBlock, 1, 1);

    const bool fiveMinuteWriteOk = fiveMinuteWrite0Ok && fiveMinuteWrite1Ok;
    const bool dayWriteOk = dayWrite0Ok && dayWrite1Ok;
    const bool monthWriteOk = monthWrite0Ok && monthWrite1Ok;

    FiveMinuteRecord fiveMinuteRecords[2];
    DayRecord dayRecords[2];
    MonthRecord monthRecords[2];

    bool fiveMinuteReadOk = false;
    bool dayReadOk = false;
    bool monthReadOk = false;
    if (fiveMinuteWriteOk) {
        fiveMinuteReadOk = readFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, fiveMinuteRecords, 2, result.fiveMinuteRecordsRead, result.fiveMinuteScan);
    }
    if (dayWriteOk) {
        dayReadOk = readDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, dayRecords, 2, result.dayRecordsRead, result.dayScan);
    }
    if (monthWriteOk) {
        monthReadOk = readMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, monthRecords, 2, result.monthRecordsRead, result.monthScan);
    }

    result.fiveMinuteOk = fiveMinuteWriteOk
            && fiveMinuteReadOk
            && result.fiveMinuteRecordsRead == 2
            && verifyScan(result.fiveMinuteScan, 3)
            && fiveMinuteRecords[0].day == 1
            && fiveMinuteRecords[0].slot == 0
            && fiveMinuteRecords[0].yieldDayWh == 10
            && fiveMinuteRecords[1].day == 1
            && fiveMinuteRecords[1].slot == 1
            && fiveMinuteRecords[1].yieldDayWh == 30;
    result.dayOk = dayWriteOk
            && dayReadOk
            && result.dayRecordsRead == 2
            && verifyScan(result.dayScan, 3)
            && dayRecords[0].dayOfYear == 1
            && dayRecords[0].yieldWh == 100
            && dayRecords[1].dayOfYear == 2
            && dayRecords[1].yieldWh == 250
            && dayRecords[1].flags == (RecordFlagValid | RecordFlagEstimated);
    result.monthOk = monthWriteOk
            && monthReadOk
            && result.monthRecordsRead == 2
            && verifyScan(result.monthScan, 3)
            && monthRecords[0].month == 1
            && monthRecords[0].yieldWh == 1000
            && monthRecords[1].month == 2
            && monthRecords[1].yieldWh == 2500
            && monthRecords[1].flags == (RecordFlagValid | RecordFlagEstimated);
    ESP_LOGI(
            ProbeTag,
            "roundtrip: 5m=%u day=%u month=%u records=%u/%u/%u blocks=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
            result.fiveMinuteOk,
            result.dayOk,
            result.monthOk,
            result.fiveMinuteRecordsRead,
            result.dayRecordsRead,
            result.monthRecordsRead,
            result.fiveMinuteScan.validBlocks,
            result.dayScan.validBlocks,
            result.monthScan.validBlocks);

    FileHeader correctFiveMinuteHeader;
    FileHeader mismatchingFiveMinuteHeader;
    const bool headerSetupOk = makeFileHeader(FileType::FiveMinute, TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, correctFiveMinuteHeader)
            && makeFileHeader(FileType::FiveMinute, TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth + 1, mismatchingFiveMinuteHeader);
    ESP_LOGI(ProbeTag, "negative setup=%u", headerSetupOk);

    if (headerSetupOk) {
        uint16_t mismatchRecordCount = 0;
        FiveMinuteRecord mismatchRecords[1];
        const bool mismatchWriteOk = truncateProbeFile(fiveMinutePath)
                && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 1, 0);
        ScanResult mismatchScan;
        const bool mismatchReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), mismatchingFiveMinuteHeader, mismatchRecords, 1, mismatchRecordCount, mismatchScan);
        result.headerMismatchOk = mismatchWriteOk && !mismatchReadOk && mismatchRecordCount == 0;

        uint8_t corruptHeader[FileHeaderSize];
        std::memset(corruptHeader, 0, sizeof(corruptHeader));
        uint16_t corruptHeaderRecordCount = 0;
        FiveMinuteRecord corruptHeaderRecords[1];
        ScanResult corruptHeaderScan;
        const bool corruptHeaderWriteOk = writeProbeBytes(fiveMinutePath, corruptHeader, sizeof(corruptHeader));
        const bool corruptHeaderReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), correctFiveMinuteHeader, corruptHeaderRecords, 1, corruptHeaderRecordCount, corruptHeaderScan);
        result.corruptHeaderOk = corruptHeaderWriteOk && !corruptHeaderReadOk && corruptHeaderRecordCount == 0;

        uint16_t corruptCrcRecordCount = 0;
        FiveMinuteRecord corruptCrcRecords[1];
        const bool corruptCrcWriteOk = truncateProbeFile(fiveMinutePath)
                && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 1, 0)
                && overwriteProbeByte(fiveMinutePath, FileHeaderSize + BlockHeaderSize, 0xff);
        const bool corruptCrcReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), correctFiveMinuteHeader, corruptCrcRecords, 1, corruptCrcRecordCount, result.corruptCrcScan);
        result.corruptCrcOk = corruptCrcWriteOk
                && corruptCrcReadOk
                && corruptCrcRecordCount == 0
                && result.corruptCrcScan.validBlocks == 0
                && result.corruptCrcScan.skippedBlocks == 1
                && result.corruptCrcScan.validRecords == 0
                && result.corruptCrcScan.skippedRecords == 0
                && result.corruptCrcScan.invalidFinalBlock
                && result.corruptCrcScan.canTruncateFinalBlock;
        ScanResult corruptCrcTruncateScan;
        const bool corruptCrcTruncateOk = result.corruptCrcOk
                && recoverFinalBlock(fiveMinutePath.c_str(), correctFiveMinuteHeader, corruptCrcTruncateScan)
                && corruptCrcTruncateScan.canTruncateFinalBlock
                && probeFileSize(fiveMinutePath) == FileHeaderSize;

        const uint8_t partialBlock[] = { 'E', 'H', 'B', '1' };
        uint16_t incompleteRecordCount = 0;
        FiveMinuteRecord incompleteRecords[1];
        const bool incompleteWriteOk = truncateProbeFile(fiveMinutePath)
                && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 1, 0)
                && appendProbeBytes(fiveMinutePath, partialBlock, sizeof(partialBlock));
        const bool incompleteReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), correctFiveMinuteHeader, incompleteRecords, 1, incompleteRecordCount, result.incompleteFinalBlockScan);
        result.incompleteFinalBlockOk = incompleteWriteOk
                && incompleteReadOk
                && incompleteRecordCount == 1
                && result.incompleteFinalBlockScan.validBlocks == 1
                && result.incompleteFinalBlockScan.skippedBlocks == 1
                && result.incompleteFinalBlockScan.validRecords == 1
                && result.incompleteFinalBlockScan.skippedRecords == 0
                && result.incompleteFinalBlockScan.invalidFinalBlock
                && result.incompleteFinalBlockScan.canTruncateFinalBlock
                && result.incompleteFinalBlockScan.lastValidOffset == result.incompleteFinalBlockScan.fileSize - sizeof(partialBlock);

        ScanResult truncateScan;
        ScanResult truncateVerifyScan;
        uint16_t truncateVerifyRecordCount = 0;
        FiveMinuteRecord truncateVerifyRecords[1];
        const bool truncateOk = result.incompleteFinalBlockOk
                && recoverFinalBlock(fiveMinutePath.c_str(), correctFiveMinuteHeader, truncateScan);
        const bool truncateVerifyReadOk = truncateOk
                && readFiveMinuteFile(fiveMinutePath.c_str(), correctFiveMinuteHeader, truncateVerifyRecords, 1, truncateVerifyRecordCount, truncateVerifyScan);
        const bool incompleteTruncateOk = truncateOk
                && truncateScan.canTruncateFinalBlock
                && probeFileSize(fiveMinutePath) == result.incompleteFinalBlockScan.lastValidOffset
                && truncateVerifyReadOk
                && truncateVerifyRecordCount == 1
                && truncateVerifyScan.validBlocks == 1
                && truncateVerifyScan.skippedBlocks == 0
                && !truncateVerifyScan.invalidFinalBlock
                && !truncateVerifyScan.canTruncateFinalBlock;
        result.truncateFinalBlockOk = corruptCrcTruncateOk && incompleteTruncateOk;

        uint16_t smallBufferRecordCount = 0;
        FiveMinuteRecord smallBufferRecords[1];
        const bool smallBufferWriteOk = truncateProbeFile(fiveMinutePath)
                && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 2, 0);
        const bool smallBufferReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), correctFiveMinuteHeader, smallBufferRecords, 1, smallBufferRecordCount, result.smallBufferScan);
        result.smallBufferOk = smallBufferWriteOk
                && smallBufferReadOk
                && smallBufferRecordCount == 1
                && result.smallBufferScan.validBlocks == 1
                && result.smallBufferScan.validRecords == 2
                && result.smallBufferScan.skippedBlocks == 0
                && result.smallBufferScan.skippedRecords == 1
                && !result.smallBufferScan.invalidFinalBlock
                && !result.smallBufferScan.canTruncateFinalBlock;
        ESP_LOGI(
                ProbeTag,
                "negative: headerMismatch=%u corruptHeader=%u corruptCrc=%u incompleteFinal=%u truncateFinal=%u smallBuffer=%u",
                result.headerMismatchOk,
                result.corruptHeaderOk,
                result.corruptCrcOk,
                result.incompleteFinalBlockOk,
                result.truncateFinalBlockOk,
                result.smallBufferOk);
    }

    const bool removeFiveMinuteOk = LittleFS.remove(fiveMinutePath);
    const bool removeDayOk = LittleFS.remove(dayPath);
    const bool removeMonthOk = LittleFS.remove(monthPath);
    result.cleanupOk = removeFiveMinuteOk && removeDayOk && removeMonthOk;
    ESP_LOGI(ProbeTag, "cleanup: 5m=%u day=%u month=%u", removeFiveMinuteOk, removeDayOk, removeMonthOk);
    ESP_LOGI(
            ProbeTag,
            "probe result: 5m=%u day=%u month=%u headerMismatch=%u corruptHeader=%u corruptCrc=%u incompleteFinal=%u truncateFinal=%u smallBuffer=%u cleanup=%u",
            result.fiveMinuteOk,
            result.dayOk,
            result.monthOk,
            result.headerMismatchOk,
            result.corruptHeaderOk,
            result.corruptCrcOk,
            result.incompleteFinalBlockOk,
            result.truncateFinalBlockOk,
            result.smallBufferOk,
            result.cleanupOk);

    return result.fiveMinuteOk
            && result.dayOk
            && result.monthOk
            && result.headerMismatchOk
            && result.corruptHeaderOk
            && result.corruptCrcOk
            && result.incompleteFinalBlockOk
            && result.truncateFinalBlockOk
            && result.smallBufferOk
            && result.cleanupOk;
}
