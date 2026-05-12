// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "EnergyHistory.h"
#include <LittleFS.h>

namespace {

using namespace EnergyHistoryFormat;

static constexpr uint64_t ManualProbeSerial = 999999999001ULL;
static constexpr uint16_t ManualProbeYear = 2099;
static constexpr uint8_t ManualProbeMonth = 1;

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

} // namespace

bool EnergyHistoryClass::runManualPersistenceProbe(ManualProbeResult& result)
{
    result = ManualProbeResult();

    const String fiveMinutePath = makeFiveMinutePath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth);
    const String dayPath = makeDayPath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear);
    const String monthPath = makeMonthPath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear);
    if (fiveMinutePath.isEmpty() || dayPath.isEmpty() || monthPath.isEmpty()) {
        return false;
    }

    LittleFS.remove(fiveMinutePath);
    LittleFS.remove(dayPath);
    LittleFS.remove(monthPath);

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

    const bool fiveMinuteWriteOk = writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstFiveMinuteBlock, 2, 0)
            && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, secondFiveMinuteBlock, 1, 1);
    const bool dayWriteOk = writeDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, firstDayBlock, 2, 0)
            && writeDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, secondDayBlock, 1, 1);
    const bool monthWriteOk = writeMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, firstMonthBlock, 2, 0)
            && writeMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, secondMonthBlock, 1, 1);

    FiveMinuteRecord fiveMinuteRecords[2];
    DayRecord dayRecords[2];
    MonthRecord monthRecords[2];

    const bool fiveMinuteReadOk = readFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, fiveMinuteRecords, 2, result.fiveMinuteRecordsRead, result.fiveMinuteScan);
    const bool dayReadOk = readDay(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, dayRecords, 2, result.dayRecordsRead, result.dayScan);
    const bool monthReadOk = readMonth(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, monthRecords, 2, result.monthRecordsRead, result.monthScan);

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

    result.cleanupOk = LittleFS.remove(fiveMinutePath)
            && LittleFS.remove(dayPath)
            && LittleFS.remove(monthPath);

    return result.fiveMinuteOk && result.dayOk && result.monthOk && result.cleanupOk;
}
