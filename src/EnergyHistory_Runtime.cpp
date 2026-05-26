// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "EnergyHistory.h"
#include "Configuration.h"
#include "Datastore.h"
#include "SunPosition.h"
#include <Hoymiles.h>
#include <ctime>

namespace {

using namespace EnergyHistoryFormat;

uint32_t yieldWhFromFloat(const float value)
{
    if (value <= 0.0f) {
        return 0;
    }

    if (value >= static_cast<float>(UINT32_MAX)) {
        return UINT32_MAX;
    }

    return static_cast<uint32_t>(value + 0.5f);
}

uint8_t flagsFromState(const bool reachable, const bool producing)
{
    uint8_t flags = RecordFlagValid;
    if (reachable) {
        flags |= RecordFlagReachable;
    }
    if (producing) {
        flags |= RecordFlagProducing;
    }

    return flags;
}

bool lastUpdateIsCurrentLocalDay(const uint32_t lastUpdateMillis, const tm& currentLocalTime)
{
    if (lastUpdateMillis == 0) {
        return false;
    }

    const time_t now = time(nullptr);
    const uint32_t ageSeconds = (millis() - lastUpdateMillis) / 1000U;
    const time_t lastUpdateTime = now - static_cast<time_t>(ageSeconds);

    tm lastUpdateLocalTime = {};
    localtime_r(&lastUpdateTime, &lastUpdateLocalTime);

    return lastUpdateLocalTime.tm_year == currentLocalTime.tm_year
            && lastUpdateLocalTime.tm_mon == currentLocalTime.tm_mon
            && lastUpdateLocalTime.tm_mday == currentLocalTime.tm_mday;
}

bool allEnabledInvertersHaveCurrentDayStats(const tm& currentLocalTime)
{
    bool foundEnabledInverter = false;
    for (uint8_t i = 0; i < Hoymiles.getNumInverters(); i++) {
        auto inv = Hoymiles.getInverterByPos(i);
        if (inv == nullptr) {
            continue;
        }

        auto cfg = Configuration.getInverterConfig(inv->serial());
        if (cfg == nullptr || !cfg->Poll_Enable) {
            continue;
        }

        foundEnabledInverter = true;
        if (!lastUpdateIsCurrentLocalDay(inv->Statistics()->getLastUpdate(), currentLocalTime)) {
            return false;
        }
    }

    return foundEnabledInverter;
}

} // namespace

void EnergyHistoryClass::loop()
{
#if defined(ENERGY_HISTORY_ENABLE)
    persistCurrentFiveMinuteSlot();
#endif
}

bool EnergyHistoryClass::persistCurrentFiveMinuteSlot()
{
    tm timeinfo = {};
    if (!getLocalTime(&timeinfo, 5)) {
        return false;
    }

    const uint16_t year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
    const uint8_t month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
    const uint8_t day = static_cast<uint8_t>(timeinfo.tm_mday);
    const uint16_t slot = static_cast<uint16_t>(timeinfo.tm_hour * 12 + timeinfo.tm_min / 5);
    if (day < 1 || day > 31 || month < 1 || month > 12 || slot >= FiveMinuteSlotsPerDay) {
        return false;
    }

    processPendingFinalizations(2);

    if (_lastFiveMinuteSlotValid
            && _lastFiveMinuteYear == year
            && _lastFiveMinuteMonth == month
            && _lastFiveMinuteDay == day
            && _lastFiveMinuteSlot == slot) {
        return true;
    }

    const bool dateChanged = _lastFiveMinuteSlotValid
            && (_lastFiveMinuteYear != year
                    || _lastFiveMinuteMonth != month
                    || _lastFiveMinuteDay != day);
    const bool monthChanged = dateChanged
            && (_lastFiveMinuteYear != year || _lastFiveMinuteMonth != month);
    if (dateChanged) {
        if (!finalizeCompletedPeriod(TargetType::Total, 0, _lastFiveMinuteYear, _lastFiveMinuteMonth, _lastFiveMinuteDay, monthChanged)) {
            queuePendingFinalization(TargetType::Total, 0, _lastFiveMinuteYear, _lastFiveMinuteMonth, _lastFiveMinuteDay, monthChanged);
        }

        for (uint8_t i = 0; i < Hoymiles.getNumInverters(); i++) {
            auto inv = Hoymiles.getInverterByPos(i);
            if (inv == nullptr) {
                continue;
            }

            auto cfg = Configuration.getInverterConfig(inv->serial());
            if (cfg == nullptr || !cfg->Poll_Enable) {
                continue;
            }

            if (!finalizeCompletedPeriod(TargetType::Inverter, inv->serial(), _lastFiveMinuteYear, _lastFiveMinuteMonth, _lastFiveMinuteDay, monthChanged)) {
                queuePendingFinalization(TargetType::Inverter, inv->serial(), _lastFiveMinuteYear, _lastFiveMinuteMonth, _lastFiveMinuteDay, monthChanged);
            }
        }
    }

    if (SunPosition.isSunsetAvailable() && !SunPosition.isDayPeriod()) {
        _lastFiveMinuteSlotValid = true;
        _lastFiveMinuteYear = year;
        _lastFiveMinuteMonth = month;
        _lastFiveMinuteDay = day;
        _lastFiveMinuteSlot = slot;
        return true;
    }

    if (!allEnabledInvertersHaveCurrentDayStats(timeinfo)) {
        return false;
    }

    const uint16_t blockIndex = static_cast<uint16_t>((day - 1) * FiveMinuteSlotsPerDay + slot);
    FiveMinuteRecord record;
    record.day = day;
    record.slot = slot;
    record.yieldDayWh = yieldWhFromFloat(Datastore.getTotalAcYieldDayEnabled());
    record.flags = flagsFromState(Datastore.getIsAtLeastOneReachable(), Datastore.getIsAtLeastOneProducing());

    bool ok = writeFiveMinute(TargetType::Total, 0, year, month, &record, 1, blockIndex);

    for (uint8_t i = 0; i < Hoymiles.getNumInverters(); i++) {
        auto inv = Hoymiles.getInverterByPos(i);
        if (inv == nullptr) {
            continue;
        }

        auto cfg = Configuration.getInverterConfig(inv->serial());
        if (cfg == nullptr || !cfg->Poll_Enable) {
            continue;
        }

        FiveMinuteRecord inverterRecord;
        inverterRecord.day = day;
        inverterRecord.slot = slot;
        inverterRecord.yieldDayWh = yieldWhFromFloat(inv->Statistics()->getChannelFieldValue(TYPE_INV, CH0, FLD_YD));
        inverterRecord.flags = flagsFromState(inv->isReachable(), inv->isProducing());
        ok = writeFiveMinute(TargetType::Inverter, inv->serial(), year, month, &inverterRecord, 1, blockIndex) && ok;
    }

    if (ok) {
        _lastFiveMinuteSlotValid = true;
        _lastFiveMinuteYear = year;
        _lastFiveMinuteMonth = month;
        _lastFiveMinuteDay = day;
        _lastFiveMinuteSlot = slot;
    }

    return ok;
}

bool EnergyHistoryClass::queuePendingFinalization(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, const uint8_t day, const bool finalizeMonth)
{
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    PendingFinalization* freeSlot = nullptr;
    for (uint8_t i = 0; i < MaxPendingFinalizations; i++) {
        PendingFinalization& pending = _pendingFinalizations[i];
        if (!pending.active) {
            if (freeSlot == nullptr) {
                freeSlot = &pending;
            }
            continue;
        }

        if (pending.targetType == targetType
                && pending.serial == serial
                && pending.year == year
                && pending.month == month
                && pending.day == day) {
            pending.finalizeMonth = pending.finalizeMonth || finalizeMonth;
            return true;
        }
    }

    if (freeSlot == nullptr) {
        return false;
    }

    freeSlot->active = true;
    freeSlot->targetType = targetType;
    freeSlot->serial = serial;
    freeSlot->year = year;
    freeSlot->month = month;
    freeSlot->day = day;
    freeSlot->finalizeMonth = finalizeMonth;
    freeSlot->attempts = 0;
    return true;
}

void EnergyHistoryClass::processPendingFinalizations(const uint8_t maxAttempts)
{
    uint8_t attempts = 0;
    for (uint8_t i = 0; i < MaxPendingFinalizations && attempts < maxAttempts; i++) {
        PendingFinalization& pending = _pendingFinalizations[i];
        if (!pending.active) {
            continue;
        }

        attempts++;
        if (finalizeCompletedPeriod(pending.targetType, pending.serial, pending.year, pending.month, pending.day, pending.finalizeMonth)) {
            pending = PendingFinalization();
            continue;
        }

        if (pending.attempts < UINT8_MAX) {
            pending.attempts++;
        }
    }
}
