// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "WebApi_energy_history_file.h"
#include "EnergyHistory.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include <AsyncJson.h>
#include <LittleFS.h>

namespace {

bool getManagedPath(AsyncWebServerRequest* request, String& path)
{
    if (!request->hasParam("file")) {
        return false;
    }

    return EnergyHistory.isManagedFilePath(request->getParam("file")->value(), path);
}

String uploadTempPath(const String& path)
{
    return path + ".upload";
}

void sendJsonMessage(AsyncWebServerRequest* request, const int code, const char* type, const char* message, const WebApiError error)
{
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["type"] = type;
    root["message"] = message;
    root["code"] = error;
    response->setCode(code);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

} // namespace

void WebApiEnergyHistoryFileClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    using std::placeholders::_4;
    using std::placeholders::_5;
    using std::placeholders::_6;

    server.on("/api/energy/history/file/list", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiEnergyHistoryFileClass::onFileList, this, _1)));
    server.on("/api/energy/history/file/download", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiEnergyHistoryFileClass::onFileDownload, this, _1)));
    server.on("/api/energy/history/file/upload", HTTP_POST,
        std::bind(&WebApiEnergyHistoryFileClass::onFileUploadFinish, this, _1),
        std::bind(&WebApiEnergyHistoryFileClass::onFileUpload, this, _1, _2, _3, _4, _5, _6));
}

void WebApiEnergyHistoryFileClass::onFileList(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    JsonArray files = root["files"].to<JsonArray>();

    EnergyHistory.listFiles([&files](const EnergyHistoryClass::FileInfo& file) {
        if (file.path.endsWith(".upload")) {
            return;
        }

        JsonObject item = files.add<JsonObject>();
        item["path"] = file.path;
        item["size"] = file.size;
    });

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiEnergyHistoryFileClass::onFileDownload(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    String path;
    if (!getManagedPath(request, path)) {
        request->send(400);
        return;
    }

    File file = LittleFS.open(path, "r", false);
    if (!file || file.isDirectory()) {
        request->send(404);
        return;
    }
    file.close();

    request->send(LittleFS, path, asyncsrv::T_application_octet_stream, true);
}

void WebApiEnergyHistoryFileClass::onFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    if (!index) {
        String path;
        if (!getManagedPath(request, path)) {
            return;
        }

        request->_tempFile = LittleFS.open(uploadTempPath(path), "w", false);
        if (!request->_tempFile) {
            return;
        }
    }

    if (len && request->_tempFile) {
        request->_tempFile.write(data, len);
    }

    if (final && request->_tempFile) {
        request->_tempFile.close();
    }
}

void WebApiEnergyHistoryFileClass::onFileUploadFinish(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    String path;
    if (!getManagedPath(request, path)) {
        sendJsonMessage(request, 400, "warning", "Invalid energy history file path", WebApiError::GenericValueMissing);
        return;
    }

    const String tempPath = uploadTempPath(path);
    if (!LittleFS.exists(tempPath)) {
        sendJsonMessage(request, 500, "danger", "Energy history file upload failed", WebApiError::GenericWriteFailed);
        return;
    }

    LittleFS.remove(path);
    if (!LittleFS.rename(tempPath, path)) {
        LittleFS.remove(tempPath);
        sendJsonMessage(request, 500, "danger", "Energy history file upload failed", WebApiError::GenericWriteFailed);
        return;
    }

    sendJsonMessage(request, 200, "success", "Energy history file uploaded", WebApiError::GenericSuccess);
}
