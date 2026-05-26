// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "WebApi_energy_history.h"
#include "EnergyHistory.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include <AsyncJson.h>
#include <cstdlib>
#include <memory>
#include <new>

namespace {

using namespace EnergyHistoryFormat;

bool parseUint16Param(AsyncWebServerRequest* request, const char* name, uint16_t& value)
{
    if (!request->hasParam(name)) {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(request->getParam(name)->value().c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed > UINT16_MAX) {
        return false;
    }

    value = static_cast<uint16_t>(parsed);
    return true;
}

bool parseUint8Param(AsyncWebServerRequest* request, const char* name, uint8_t& value)
{
    uint16_t parsed = 0;
    if (!parseUint16Param(request, name, parsed) || parsed > UINT8_MAX) {
        return false;
    }

    value = static_cast<uint8_t>(parsed);
    return true;
}

bool parseTarget(AsyncWebServerRequest* request, TargetType& targetType, uint64_t& serial)
{
    targetType = TargetType::Total;
    serial = 0;
    if (!request->hasParam("target")) {
        return true;
    }

    const String target = request->getParam("target")->value();
    if (target == "total") {
        return true;
    }

    if (!target.startsWith("inv_")) {
        return false;
    }

    const String serialString = target.substring(4);
    if (serialString.isEmpty()) {
        return false;
    }

    char* end = nullptr;
    serial = std::strtoull(serialString.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || serial == 0) {
        return false;
    }

    targetType = TargetType::Inverter;
    return true;
}

bool parseDate(const String& date, uint16_t& year, uint8_t& month, uint8_t& day)
{
    if (date.length() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    year = static_cast<uint16_t>(date.substring(0, 4).toInt());
    month = static_cast<uint8_t>(date.substring(5, 7).toInt());
    day = static_cast<uint8_t>(date.substring(8, 10).toInt());
    return year >= 2000 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

String formatTargetId(const TargetType targetType, const uint64_t serial)
{
    return targetType == TargetType::Total ? String("total") : String("inv_") + String(serial);
}

uint16_t averagePowerFromDelta(const FiveMinuteRecord* records, const uint16_t recordCount, const uint16_t index)
{
    if (records == nullptr || index == 0 || index >= recordCount) {
        return 0;
    }

    const FiveMinuteRecord& current = records[index];
    const FiveMinuteRecord& previous = records[index - 1];
    if (current.slot <= previous.slot || current.yieldDayWh < previous.yieldDayWh) {
        return 0;
    }

    const uint32_t deltaWh = current.yieldDayWh - previous.yieldDayWh;
    const uint32_t deltaSec = static_cast<uint32_t>(current.slot - previous.slot) * FiveMinuteIntervalSec;
    if (deltaSec == 0) {
        return 0;
    }

    const uint32_t powerW = (deltaWh * 3600U + deltaSec / 2U) / deltaSec;
    return powerW > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(powerW);
}

void writeScan(JsonObject root, const EnergyHistoryClass::ScanResult& scan)
{
    root["files_scanned"] = scan.filesScanned;
    root["valid_blocks"] = scan.validBlocks;
    root["skipped_blocks"] = scan.skippedBlocks;
    root["valid_records"] = scan.validRecords;
    root["skipped_records"] = scan.skippedRecords;
    root["file_size"] = scan.fileSize;
    root["last_valid_offset"] = scan.lastValidOffset;
    root["invalid_final_block"] = scan.invalidFinalBlock;
    root["can_truncate_final_block"] = scan.canTruncateFinalBlock;
}

void writeRecoveryStatus(JsonObject root, const EnergyHistoryClass::RecoveryStatus& status)
{
    root["pending"] = status.pending;
    root["running"] = status.running;
    root["run_count"] = status.runCount;
    root["last_started_ms"] = status.lastStartedMillis;
    root["last_finished_ms"] = status.lastFinishedMillis;
}

void sendBadRequest(AsyncWebServerRequest* request, const char* message)
{
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["type"] = "warning";
    root["message"] = message;
    root["code"] = WebApiError::GenericValueMissing;
    response->setCode(400);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

} // namespace

void WebApiEnergyHistoryClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;

    server.on("/api/energy/history/revision", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiEnergyHistoryClass::onRevision, this, _1)));
    server.on("/api/energy/history/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiEnergyHistoryClass::onStatus, this, _1)));
    server.on("/api/energy/history/recovery", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiEnergyHistoryClass::onRecoveryPost, this, _1)));
    server.on("/api/energy/history", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiEnergyHistoryClass::onHistory, this, _1)));
}

void WebApiEnergyHistoryClass::onRevision(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    EnergyHistoryClass::Revision revision;
    EnergyHistory.getRevision(revision);

    EnergyHistoryClass::RecoveryStatus recoveryStatus;
    EnergyHistory.getRecoveryStatus(recoveryStatus);

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["data_revision"] = revision.dataRevision;
    root["file_revision"] = revision.fileRevision;
    root["last_change_ms"] = revision.lastChangeMillis;
    root["recovery_pending"] = recoveryStatus.pending;
    root["recovery_running"] = recoveryStatus.running;

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiEnergyHistoryClass::onStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    EnergyHistoryClass::Status status;
    EnergyHistory.getStatus(status);

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["files_scanned"] = status.filesScanned;
    root["valid_blocks"] = status.validBlocks;
    root["skipped_blocks"] = status.skippedBlocks;
    root["valid_records"] = status.validRecords;
    root["skipped_records"] = status.skippedRecords;
    root["invalid_final_block_files"] = status.invalidFinalBlockFiles;
    root["truncatable_final_block_files"] = status.truncatableFinalBlockFiles;
    root["bytes_scanned"] = status.bytesScanned;
    root["littlefs_total"] = status.littlefsTotalBytes;
    root["littlefs_used"] = status.littlefsUsedBytes;

    EnergyHistoryClass::RecoveryStatus recoveryStatus;
    EnergyHistory.getRecoveryStatus(recoveryStatus);
    writeRecoveryStatus(root["recovery"].to<JsonObject>(), recoveryStatus);

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiEnergyHistoryClass::onRecoveryPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    const bool started = EnergyHistory.requestRecovery();

    EnergyHistoryClass::RecoveryStatus status;
    EnergyHistory.getRecoveryStatus(status);

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["type"] = started ? "success" : "warning";
    root["message"] = started ? "Energy history recovery scheduled" : "Energy history recovery is already pending or running";
    writeRecoveryStatus(root["recovery"].to<JsonObject>(), status);
    if (!started) {
        response->setCode(409);
    }

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiEnergyHistoryClass::onHistory(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    if (!request->hasParam("resolution")) {
        sendBadRequest(request, "Missing resolution");
        return;
    }

    TargetType targetType = TargetType::Total;
    uint64_t serial = 0;
    if (!parseTarget(request, targetType, serial)) {
        sendBadRequest(request, "Invalid target");
        return;
    }

    const String resolution = request->getParam("resolution")->value();
    EnergyHistoryClass::ScanResult scan;
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["target"] = formatTargetId(targetType, serial);
    root["resolution"] = resolution;
    JsonArray data = root["data"].to<JsonArray>();

    bool queryOk = false;
    if (resolution == "5m") {
        if (!request->hasParam("date")) {
            delete response;
            sendBadRequest(request, "Missing date");
            return;
        }

        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        if (!parseDate(request->getParam("date")->value(), year, month, day)) {
            delete response;
            sendBadRequest(request, "Invalid date");
            return;
        }

        std::unique_ptr<FiveMinuteRecord[]> records(new (std::nothrow) FiveMinuteRecord[FiveMinuteSlotsPerDay]);
        if (!records) {
            delete response;
            sendBadRequest(request, "Out of memory");
            return;
        }

        uint16_t recordCount = 0;
        queryOk = EnergyHistory.queryFiveMinuteDay(targetType, serial, year, month, day, records.get(), FiveMinuteSlotsPerDay, recordCount, scan);
        root["date"] = request->getParam("date")->value();
        root["interval_sec"] = FiveMinuteIntervalSec;

        for (uint16_t i = 0; queryOk && i < recordCount; i++) {
            JsonObject item = data.add<JsonObject>();
            item["day"] = records[i].day;
            item["slot"] = records[i].slot;
            item["yield_wh"] = records[i].yieldDayWh;
            item["avg_power_w"] = averagePowerFromDelta(records.get(), recordCount, i);
            item["flags"] = records[i].flags;
        }
    } else if (resolution == "day") {
        uint16_t year = 0;
        uint16_t from = 1;
        uint16_t to = 366;
        if (!parseUint16Param(request, "year", year)) {
            delete response;
            sendBadRequest(request, "Missing or invalid year");
            return;
        }
        if (request->hasParam("from") && !parseUint16Param(request, "from", from)) {
            delete response;
            sendBadRequest(request, "Invalid from");
            return;
        }
        if (request->hasParam("to") && !parseUint16Param(request, "to", to)) {
            delete response;
            sendBadRequest(request, "Invalid to");
            return;
        }

        std::unique_ptr<DayRecord[]> records(new (std::nothrow) DayRecord[366]);
        if (!records) {
            delete response;
            sendBadRequest(request, "Out of memory");
            return;
        }

        uint16_t recordCount = 0;
        queryOk = EnergyHistory.queryDay(targetType, serial, year, from, to, records.get(), 366, recordCount, scan);
        root["year"] = year;
        root["from"] = from;
        root["to"] = to;

        for (uint16_t i = 0; queryOk && i < recordCount; i++) {
            JsonObject item = data.add<JsonObject>();
            item["day_of_year"] = records[i].dayOfYear;
            item["yield_wh"] = records[i].yieldWh;
            item["max_power_w"] = records[i].maxPowerW;
            item["avg_power_w"] = records[i].avgPowerW;
            item["runtime_min"] = records[i].runtimeMin;
            item["sample_count"] = records[i].sampleCount;
            item["flags"] = records[i].flags;
        }
    } else if (resolution == "month") {
        uint16_t year = 0;
        uint8_t from = 1;
        uint8_t to = 12;
        if (!parseUint16Param(request, "year", year)) {
            delete response;
            sendBadRequest(request, "Missing or invalid year");
            return;
        }
        if (request->hasParam("from") && !parseUint8Param(request, "from", from)) {
            delete response;
            sendBadRequest(request, "Invalid from");
            return;
        }
        if (request->hasParam("to") && !parseUint8Param(request, "to", to)) {
            delete response;
            sendBadRequest(request, "Invalid to");
            return;
        }

        std::unique_ptr<MonthRecord[]> records(new (std::nothrow) MonthRecord[12]);
        if (!records) {
            delete response;
            sendBadRequest(request, "Out of memory");
            return;
        }

        uint16_t recordCount = 0;
        queryOk = EnergyHistory.queryMonth(targetType, serial, year, from, to, records.get(), 12, recordCount, scan);
        root["year"] = year;
        root["from"] = from;
        root["to"] = to;

        for (uint16_t i = 0; queryOk && i < recordCount; i++) {
            JsonObject item = data.add<JsonObject>();
            item["month"] = records[i].month;
            item["yield_wh"] = records[i].yieldWh;
            item["max_power_w"] = records[i].maxPowerW;
            item["avg_power_w"] = records[i].avgPowerW;
            item["runtime_min"] = records[i].runtimeMin;
            item["day_count"] = records[i].dayCount;
            item["flags"] = records[i].flags;
        }
    } else {
        delete response;
        sendBadRequest(request, "Invalid resolution");
        return;
    }

    root["count"] = data.size();
    writeScan(root["scan"].to<JsonObject>(), scan);
    if (!queryOk) {
        root["type"] = "warning";
        root["message"] = "History query failed";
        root["code"] = WebApiError::GenericNoValueFound;
        response->setCode(404);
    }

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
