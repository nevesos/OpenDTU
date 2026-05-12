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
    ESP_LOGI(ProbeTag, "truncate start: %s", path.c_str());
    File file = LittleFS.open(path, "w");
    const bool ok = file;
    ESP_LOGI(ProbeTag, "truncate result: %s ok=%u size=%u", path.c_str(), ok, ok ? static_cast<unsigned>(file.size()) : 0);
    return ok;
}

bool writeProbeBytes(const String& path, const uint8_t* data, const size_t length)
{
    ESP_LOGI(ProbeTag, "write bytes start: %s length=%u", path.c_str(), static_cast<unsigned>(length));
    File file = LittleFS.open(path, "w");
    const bool ok = file && file.write(data, length) == length;
    ESP_LOGI(ProbeTag, "write bytes result: %s ok=%u", path.c_str(), ok);
    return ok;
}

bool appendProbeBytes(const String& path, const uint8_t* data, const size_t length)
{
    ESP_LOGI(ProbeTag, "append bytes start: %s length=%u", path.c_str(), static_cast<unsigned>(length));
    File file = LittleFS.open(path, "a");
    const bool ok = file && file.write(data, length) == length;
    ESP_LOGI(ProbeTag, "append bytes result: %s ok=%u", path.c_str(), ok);
    return ok;
}

bool overwriteProbeByte(const String& path, const size_t offset, const uint8_t value)
{
    ESP_LOGI(ProbeTag, "overwrite byte start: %s offset=%u value=%u", path.c_str(), static_cast<unsigned>(offset), value);
    File file = LittleFS.open(path, "r+");
    const bool ok = file && file.seek(offset) && file.write(value) == 1;
    ESP_LOGI(ProbeTag, "overwrite byte result: %s ok=%u", path.c_str(), ok);
    return ok;
}

bool ensureProbeDirectory(const char* path)
{
    ESP_LOGI(ProbeTag, "mkdir start: %s", path);
    if (LittleFS.mkdir(path)) {
        ESP_LOGI(ProbeTag, "mkdir result: %s created=1", path);
        return true;
    }

    ESP_LOGI(ProbeTag, "mkdir fallback-open start: %s", path);
    File directory = LittleFS.open(path, "r", false);
    const bool ok = directory && directory.isDirectory();
    ESP_LOGI(ProbeTag, "mkdir fallback-open result: %s ok=%u isDirectory=%u", path, ok, directory ? directory.isDirectory() : 0);
    return ok;
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
    ESP_LOGI(ProbeTag, "paths: fiveMinute=%s day=%s month=%s", fiveMinutePath.c_str(), dayPath.c_str(), monthPath.c_str());
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
    ESP_LOGI(
            ProbeTag,
            "setup result: dirs=%u/%u/%u/%u truncate=%u/%u/%u",
            energyDirOk,
            fiveMinuteDirOk,
            dayDirOk,
            monthDirOk,
            truncateFiveMinuteOk,
            truncateDayOk,
            truncateMonthOk);
    if (!cleanupBeforeWriteOk) {
        ESP_LOGE(ProbeTag, "setup failed, cleanup remove start");
        LittleFS.remove(fiveMinutePath);
        LittleFS.remove(dayPath);
        LittleFS.remove(monthPath);
        return false;
    }

    ESP_LOGI(ProbeTag, "write 5m block 0 start");
    const bool fiveMinuteWrite0Ok = writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 2, 0);
    ESP_LOGI(ProbeTag, "write 5m block 0 result=%u", fiveMinuteWrite0Ok);
    ESP_LOGI(ProbeTag, "write 5m block 1 start");
    const bool fiveMinuteWrite1Ok = writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, secondFiveMinuteBlock, 1, 1);
    ESP_LOGI(ProbeTag, "write 5m block 1 result=%u", fiveMinuteWrite1Ok);

    ESP_LOGI(ProbeTag, "write day block 0 start");
    const bool dayWrite0Ok = writeDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, firstDayBlock, 2, 0);
    ESP_LOGI(ProbeTag, "write day block 0 result=%u", dayWrite0Ok);
    ESP_LOGI(ProbeTag, "write day block 1 start");
    const bool dayWrite1Ok = writeDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, secondDayBlock, 1, 1);
    ESP_LOGI(ProbeTag, "write day block 1 result=%u", dayWrite1Ok);

    ESP_LOGI(ProbeTag, "write month block 0 start");
    const bool monthWrite0Ok = writeMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, firstMonthBlock, 2, 0);
    ESP_LOGI(ProbeTag, "write month block 0 result=%u", monthWrite0Ok);
    ESP_LOGI(ProbeTag, "write month block 1 start");
    const bool monthWrite1Ok = writeMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, secondMonthBlock, 1, 1);
    ESP_LOGI(ProbeTag, "write month block 1 result=%u", monthWrite1Ok);

    const bool fiveMinuteWriteOk = fiveMinuteWrite0Ok && fiveMinuteWrite1Ok;
    const bool dayWriteOk = dayWrite0Ok && dayWrite1Ok;
    const bool monthWriteOk = monthWrite0Ok && monthWrite1Ok;
    ESP_LOGI(ProbeTag, "write summary: 5m=%u day=%u month=%u", fiveMinuteWriteOk, dayWriteOk, monthWriteOk);

    FiveMinuteRecord fiveMinuteRecords[2];
    DayRecord dayRecords[2];
    MonthRecord monthRecords[2];

    bool fiveMinuteReadOk = false;
    bool dayReadOk = false;
    bool monthReadOk = false;
    if (fiveMinuteWriteOk) {
        ESP_LOGI(ProbeTag, "read 5m start");
        fiveMinuteReadOk = readFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, fiveMinuteRecords, 2, result.fiveMinuteRecordsRead, result.fiveMinuteScan);
        ESP_LOGI(ProbeTag, "read 5m result=%u records=%u validBlocks=%" PRIu32 " validRecords=%" PRIu32 " skippedBlocks=%" PRIu32 " skippedRecords=%" PRIu32,
                fiveMinuteReadOk, result.fiveMinuteRecordsRead, result.fiveMinuteScan.validBlocks, result.fiveMinuteScan.validRecords, result.fiveMinuteScan.skippedBlocks, result.fiveMinuteScan.skippedRecords);
    }
    if (dayWriteOk) {
        ESP_LOGI(ProbeTag, "read day start");
        dayReadOk = readDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, dayRecords, 2, result.dayRecordsRead, result.dayScan);
        ESP_LOGI(ProbeTag, "read day result=%u records=%u validBlocks=%" PRIu32 " validRecords=%" PRIu32 " skippedBlocks=%" PRIu32 " skippedRecords=%" PRIu32,
                dayReadOk, result.dayRecordsRead, result.dayScan.validBlocks, result.dayScan.validRecords, result.dayScan.skippedBlocks, result.dayScan.skippedRecords);
    }
    if (monthWriteOk) {
        ESP_LOGI(ProbeTag, "read month start");
        monthReadOk = readMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, monthRecords, 2, result.monthRecordsRead, result.monthScan);
        ESP_LOGI(ProbeTag, "read month result=%u records=%u validBlocks=%" PRIu32 " validRecords=%" PRIu32 " skippedBlocks=%" PRIu32 " skippedRecords=%" PRIu32,
                monthReadOk, result.monthRecordsRead, result.monthScan.validBlocks, result.monthScan.validRecords, result.monthScan.skippedBlocks, result.monthScan.skippedRecords);
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

    FileHeader correctFiveMinuteHeader;
    FileHeader mismatchingFiveMinuteHeader;
    const bool headerSetupOk = makeFileHeader(FileType::FiveMinute, TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, correctFiveMinuteHeader)
            && makeFileHeader(FileType::FiveMinute, TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth + 1, mismatchingFiveMinuteHeader);
    ESP_LOGI(ProbeTag, "negative header setup result=%u", headerSetupOk);

    if (headerSetupOk) {
        ESP_LOGI(ProbeTag, "negative header-mismatch start");
        uint16_t mismatchRecordCount = 0;
        FiveMinuteRecord mismatchRecords[1];
        const bool mismatchWriteOk = truncateProbeFile(fiveMinutePath)
                && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 1, 0);
        ScanResult mismatchScan;
        const bool mismatchReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), mismatchingFiveMinuteHeader, mismatchRecords, 1, mismatchRecordCount, mismatchScan);
        result.headerMismatchOk = mismatchWriteOk && !mismatchReadOk && mismatchRecordCount == 0;
        ESP_LOGI(ProbeTag, "negative header-mismatch result=%u write=%u read=%u records=%u", result.headerMismatchOk, mismatchWriteOk, mismatchReadOk, mismatchRecordCount);

        ESP_LOGI(ProbeTag, "negative corrupt-header start");
        uint8_t corruptHeader[FileHeaderSize];
        std::memset(corruptHeader, 0, sizeof(corruptHeader));
        uint16_t corruptHeaderRecordCount = 0;
        FiveMinuteRecord corruptHeaderRecords[1];
        ScanResult corruptHeaderScan;
        const bool corruptHeaderWriteOk = writeProbeBytes(fiveMinutePath, corruptHeader, sizeof(corruptHeader));
        const bool corruptHeaderReadOk = readFiveMinuteFile(fiveMinutePath.c_str(), correctFiveMinuteHeader, corruptHeaderRecords, 1, corruptHeaderRecordCount, corruptHeaderScan);
        result.corruptHeaderOk = corruptHeaderWriteOk && !corruptHeaderReadOk && corruptHeaderRecordCount == 0;
        ESP_LOGI(ProbeTag, "negative corrupt-header result=%u write=%u read=%u records=%u", result.corruptHeaderOk, corruptHeaderWriteOk, corruptHeaderReadOk, corruptHeaderRecordCount);

        ESP_LOGI(ProbeTag, "negative corrupt-crc start");
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
                && !result.corruptCrcScan.invalidFinalBlock
                && !result.corruptCrcScan.canTruncateFinalBlock;
        ESP_LOGI(ProbeTag, "negative corrupt-crc result=%u write=%u read=%u records=%u validBlocks=%" PRIu32 " skippedBlocks=%" PRIu32,
                result.corruptCrcOk, corruptCrcWriteOk, corruptCrcReadOk, corruptCrcRecordCount, result.corruptCrcScan.validBlocks, result.corruptCrcScan.skippedBlocks);

        ESP_LOGI(ProbeTag, "negative incomplete-final-block start");
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
        ESP_LOGI(ProbeTag, "negative incomplete-final-block result=%u write=%u read=%u records=%u validBlocks=%" PRIu32 " skippedBlocks=%" PRIu32 " lastValid=%u fileSize=%u",
                result.incompleteFinalBlockOk, incompleteWriteOk, incompleteReadOk, incompleteRecordCount, result.incompleteFinalBlockScan.validBlocks, result.incompleteFinalBlockScan.skippedBlocks, static_cast<unsigned>(result.incompleteFinalBlockScan.lastValidOffset), static_cast<unsigned>(result.incompleteFinalBlockScan.fileSize));

        ESP_LOGI(ProbeTag, "negative small-buffer start");
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
        ESP_LOGI(ProbeTag, "negative small-buffer result=%u write=%u read=%u records=%u validRecords=%" PRIu32 " skippedRecords=%" PRIu32,
                result.smallBufferOk, smallBufferWriteOk, smallBufferReadOk, smallBufferRecordCount, result.smallBufferScan.validRecords, result.smallBufferScan.skippedRecords);
    }

    ESP_LOGI(ProbeTag, "cleanup remove start");
    const bool removeFiveMinuteOk = LittleFS.remove(fiveMinutePath);
    const bool removeDayOk = LittleFS.remove(dayPath);
    const bool removeMonthOk = LittleFS.remove(monthPath);
    result.cleanupOk = removeFiveMinuteOk && removeDayOk && removeMonthOk;
    ESP_LOGI(ProbeTag, "cleanup result: 5m=%u day=%u month=%u", removeFiveMinuteOk, removeDayOk, removeMonthOk);
    ESP_LOGI(
            ProbeTag,
            "probe result: 5m=%u day=%u month=%u headerMismatch=%u corruptHeader=%u corruptCrc=%u incompleteFinal=%u smallBuffer=%u cleanup=%u",
            result.fiveMinuteOk,
            result.dayOk,
            result.monthOk,
            result.headerMismatchOk,
            result.corruptHeaderOk,
            result.corruptCrcOk,
            result.incompleteFinalBlockOk,
            result.smallBufferOk,
            result.cleanupOk);

    return result.fiveMinuteOk
            && result.dayOk
            && result.monthOk
            && result.headerMismatchOk
            && result.corruptHeaderOk
            && result.corruptCrcOk
            && result.incompleteFinalBlockOk
            && result.smallBufferOk
            && result.cleanupOk;
}
