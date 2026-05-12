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

} // namespace

bool EnergyHistoryClass::runManualFiveMinuteProbe(ManualProbeResult& result)
{
    result = ManualProbeResult();

    FileHeader header;
    if (!makeFileHeader(FileType::FiveMinute, TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, header)) {
        return false;
    }

    const String path = makeFiveMinutePath(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth);
    if (path.isEmpty()) {
        return false;
    }

    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
    }

    const FiveMinuteRecord firstBlock[] = {
        { 1, 0, 10, RecordFlagValid },
        { 1, 1, 20, RecordFlagValid },
    };
    const FiveMinuteRecord secondBlock[] = {
        { 1, 1, 30, RecordFlagValid },
    };

    result.writeOk = writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, firstBlock, 2, 0)
            && writeFiveMinute(TargetType::Inverter, ManualProbeSerial, ManualProbeYear, ManualProbeMonth, secondBlock, 1, 1);
    if (!result.writeOk) {
        LittleFS.remove(path);
        return false;
    }

    FiveMinuteRecord records[2];
    result.readOk = readFiveMinuteFile(path.c_str(), header, records, 2, result.recordsRead, result.scan);
    LittleFS.remove(path);

    return result.readOk
            && result.recordsRead == 2
            && result.scan.validBlocks == 2
            && result.scan.validRecords == 3
            && result.scan.skippedBlocks == 0
            && result.scan.skippedRecords == 0
            && records[0].day == 1
            && records[0].slot == 0
            && records[0].yieldDayWh == 10
            && records[1].day == 1
            && records[1].slot == 1
            && records[1].yieldDayWh == 30;
}
