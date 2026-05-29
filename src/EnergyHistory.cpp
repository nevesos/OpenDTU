// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "EnergyHistory.h"
#include <LittleFS.h>
#if defined(ENERGY_HISTORY_MANUAL_PROBE)
#include <esp_log.h>
#endif
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <new>

namespace {

using namespace EnergyHistoryFormat;

static constexpr size_t FileHeaderCrcOffset = FileHeaderSize - sizeof(uint32_t);
static constexpr uint8_t KnownRecordFlagsMask = RecordFlagValid
        | RecordFlagReachable
        | RecordFlagProducing
        | RecordFlagDayResetDetected
        | RecordFlagEstimated;
static constexpr const char* EnergyDirectory = "/energy";
static constexpr const char* FiveMinuteDirectory = "/energy/5m";
static constexpr const char* DayDirectory = "/energy/day";
static constexpr const char* MonthDirectory = "/energy/month";
static constexpr size_t FileReadBufferSize = 32;
static constexpr uint16_t MaxRecordsPerAppendBlock = 4;
static constexpr size_t FiveMinuteDayFileBytes = FileHeaderSize + FiveMinuteSlotsPerDay * (BlockHeaderSize + FiveMinuteRecordSize);
static constexpr size_t RetentionReserveBytes = 32 * 1024;
static constexpr size_t RetentionMinimumFreeBytes = FiveMinuteDayFileBytes + RetentionReserveBytes;
#if defined(ENERGY_HISTORY_MANUAL_PROBE)
static constexpr const char* EnergyHistoryTag = "EnergyHistory";
#endif

[[maybe_unused]] void writeUint8(uint8_t* output, const uint8_t value)
{
    output[0] = value;
}

[[maybe_unused]] void writeUint16Le(uint8_t* output, const uint16_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

[[maybe_unused]] void writeUint32Le(uint8_t* output, const uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

[[maybe_unused]] void writeUint64Le(uint8_t* output, const uint64_t value)
{
    writeUint32Le(output, static_cast<uint32_t>(value));
    writeUint32Le(output + sizeof(uint32_t), static_cast<uint32_t>(value >> 32));
}

[[maybe_unused]] uint8_t readUint8(const uint8_t* input)
{
    return input[0];
}

[[maybe_unused]] uint16_t readUint16Le(const uint8_t* input)
{
    return static_cast<uint16_t>(input[0])
            | (static_cast<uint16_t>(input[1]) << 8);
}

[[maybe_unused]] uint32_t readUint32Le(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0])
            | (static_cast<uint32_t>(input[1]) << 8)
            | (static_cast<uint32_t>(input[2]) << 16)
            | (static_cast<uint32_t>(input[3]) << 24);
}

[[maybe_unused]] uint64_t readUint64Le(const uint8_t* input)
{
    return static_cast<uint64_t>(readUint32Le(input))
            | (static_cast<uint64_t>(readUint32Le(input + sizeof(uint32_t))) << 32);
}

[[maybe_unused]] uint32_t calculateCrc32(const uint8_t* data, const size_t length)
{
    uint32_t crc = 0xffffffff;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 1) != 0) {
                crc = (crc >> 1) ^ 0xedb88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

uint32_t updateCrc32(uint32_t crc, const uint8_t* data, const size_t length)
{
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 1) != 0) {
                crc = (crc >> 1) ^ 0xedb88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

uint32_t finalizeCrc32(const uint32_t crc)
{
    return ~crc;
}

uint8_t expectedRecordSize(const FileType fileType)
{
    switch (fileType) {
    case FileType::FiveMinute:
        return FiveMinuteRecordSize;
    case FileType::Day:
        return DayRecordSize;
    case FileType::Month:
        return MonthRecordSize;
    default:
        return 0;
    }
}

[[maybe_unused]] bool isKnownFileType(const uint8_t fileType)
{
    return fileType == static_cast<uint8_t>(FileType::FiveMinute)
            || fileType == static_cast<uint8_t>(FileType::Day)
            || fileType == static_cast<uint8_t>(FileType::Month);
}

[[maybe_unused]] bool isKnownTargetType(const uint8_t targetType)
{
    return targetType == static_cast<uint8_t>(TargetType::Total)
            || targetType == static_cast<uint8_t>(TargetType::Inverter);
}

[[maybe_unused]] bool validateHeaderFields(const FileHeader& header)
{
    if (std::memcmp(header.magic, FileMagic, sizeof(header.magic)) != 0) {
        return false;
    }

    if (header.version != Version || header.headerSize != FileHeaderSize) {
        return false;
    }

    for (const uint8_t value : header.reserved) {
        if (value != 0) {
            return false;
        }
    }

    if (!isKnownFileType(header.fileType) || !isKnownTargetType(header.targetType)) {
        return false;
    }

    const FileType fileType = static_cast<FileType>(header.fileType);
    if (header.recordSize != expectedRecordSize(fileType)) {
        return false;
    }

    if (fileType == FileType::FiveMinute) {
        if (header.month < 1 || header.month > 12 || header.intervalSec != FiveMinuteIntervalSec) {
            return false;
        }
    } else if (header.month != 0 || header.intervalSec != 0) {
        return false;
    }

    if (header.targetType == static_cast<uint8_t>(TargetType::Total)) {
        return header.serial == 0;
    }

    return header.serial != 0;
}

[[maybe_unused]] bool encodeFileHeader(const FileHeader& header, uint8_t* output, const size_t outputSize)
{
    if (outputSize < FileHeaderSize || !validateHeaderFields(header)) {
        return false;
    }

    std::memset(output, 0, FileHeaderSize);
    std::memcpy(output, header.magic, sizeof(header.magic));
    writeUint8(output + 4, header.fileType);
    writeUint8(output + 5, header.version);
    writeUint8(output + 6, header.headerSize);
    writeUint8(output + 7, header.recordSize);
    writeUint16Le(output + 8, header.year);
    writeUint8(output + 10, header.month);
    writeUint8(output + 11, header.targetType);
    writeUint64Le(output + 12, header.serial);
    writeUint16Le(output + 20, header.intervalSec);
    writeUint32Le(output + FileHeaderCrcOffset, calculateCrc32(output, FileHeaderCrcOffset));
    return true;
}

[[maybe_unused]] bool decodeFileHeader(const uint8_t* input, const size_t inputSize, FileHeader& header)
{
    if (inputSize < FileHeaderSize) {
        return false;
    }

    std::memcpy(header.magic, input, sizeof(header.magic));
    header.fileType = readUint8(input + 4);
    header.version = readUint8(input + 5);
    header.headerSize = readUint8(input + 6);
    header.recordSize = readUint8(input + 7);
    header.year = readUint16Le(input + 8);
    header.month = readUint8(input + 10);
    header.targetType = readUint8(input + 11);
    header.serial = readUint64Le(input + 12);
    header.intervalSec = readUint16Le(input + 20);
    std::memcpy(header.reserved, input + 22, sizeof(header.reserved));
    header.crc32Header = readUint32Le(input + FileHeaderCrcOffset);

    if (!validateHeaderFields(header)) {
        return false;
    }

    return header.crc32Header == calculateCrc32(input, FileHeaderCrcOffset);
}

[[maybe_unused]] bool encodeBlockHeader(const BlockHeader& header, uint8_t* output, const size_t outputSize)
{
    if (outputSize < BlockHeaderSize || std::memcmp(header.magic, BlockMagic, sizeof(header.magic)) != 0) {
        return false;
    }

    std::memcpy(output, header.magic, sizeof(header.magic));
    writeUint16Le(output + 4, header.blockIndex);
    writeUint16Le(output + 6, header.startKey);
    writeUint16Le(output + 8, header.recordCount);
    writeUint16Le(output + 10, header.payloadSize);
    writeUint32Le(output + 12, header.crc32Payload);
    return true;
}

[[maybe_unused]] bool decodeBlockHeader(const uint8_t* input, const size_t inputSize, BlockHeader& header)
{
    if (inputSize < BlockHeaderSize) {
        return false;
    }

    std::memcpy(header.magic, input, sizeof(header.magic));
    header.blockIndex = readUint16Le(input + 4);
    header.startKey = readUint16Le(input + 6);
    header.recordCount = readUint16Le(input + 8);
    header.payloadSize = readUint16Le(input + 10);
    header.crc32Payload = readUint32Le(input + 12);

    return std::memcmp(header.magic, BlockMagic, sizeof(header.magic)) == 0;
}

[[maybe_unused]] bool isPayloadCrcValid(const BlockHeader& header, const uint8_t* payload, const size_t payloadSize)
{
    return header.payloadSize == payloadSize
            && header.crc32Payload == calculateCrc32(payload, payloadSize);
}

[[maybe_unused]] bool encodeFiveMinuteRecord(const FiveMinuteRecord& record, uint8_t* output, const size_t outputSize)
{
    if (outputSize < FiveMinuteRecordSize
            || record.day < 1
            || record.day > 31
            || record.slot >= FiveMinuteSlotsPerDay
            || (record.flags & ~KnownRecordFlagsMask) != 0) {
        return false;
    }

    writeUint8(output, record.day);
    writeUint16Le(output + 1, record.slot);
    writeUint32Le(output + 3, record.yieldDayWh);
    writeUint8(output + 7, record.flags);
    return true;
}

[[maybe_unused]] bool decodeFiveMinuteRecord(const uint8_t* input, const size_t inputSize, FiveMinuteRecord& record)
{
    if (inputSize < FiveMinuteRecordSize) {
        return false;
    }

    record.day = readUint8(input);
    record.slot = readUint16Le(input + 1);
    record.yieldDayWh = readUint32Le(input + 3);
    record.flags = readUint8(input + 7);

    return record.day >= 1
            && record.day <= 31
            && record.slot < FiveMinuteSlotsPerDay
            && (record.flags & ~KnownRecordFlagsMask) == 0;
}

[[maybe_unused]] bool encodeDayRecord(const DayRecord& record, uint8_t* output, const size_t outputSize)
{
    if (outputSize < DayRecordSize
            || record.dayOfYear < 1
            || record.dayOfYear > 366
            || (record.flags & 0xff00) != 0
            || (record.flags & ~KnownRecordFlagsMask) != 0) {
        return false;
    }

    writeUint16Le(output, record.dayOfYear);
    writeUint32Le(output + 2, record.yieldWh);
    writeUint16Le(output + 6, record.maxPowerW);
    writeUint16Le(output + 8, record.avgPowerW);
    writeUint16Le(output + 10, record.runtimeMin);
    writeUint16Le(output + 12, record.sampleCount);
    writeUint16Le(output + 14, record.flags);
    return true;
}

[[maybe_unused]] bool decodeDayRecord(const uint8_t* input, const size_t inputSize, DayRecord& record)
{
    if (inputSize < DayRecordSize) {
        return false;
    }

    record.dayOfYear = readUint16Le(input);
    record.yieldWh = readUint32Le(input + 2);
    record.maxPowerW = readUint16Le(input + 6);
    record.avgPowerW = readUint16Le(input + 8);
    record.runtimeMin = readUint16Le(input + 10);
    record.sampleCount = readUint16Le(input + 12);
    record.flags = readUint16Le(input + 14);

    return record.dayOfYear >= 1
            && record.dayOfYear <= 366
            && (record.flags & 0xff00) == 0
            && (record.flags & ~KnownRecordFlagsMask) == 0;
}

[[maybe_unused]] bool encodeMonthRecord(const MonthRecord& record, uint8_t* output, const size_t outputSize)
{
    if (outputSize < MonthRecordSize
            || record.month < 1
            || record.month > 12
            || record.reserved0 != 0
            || (record.flags & 0xff00) != 0
            || (record.flags & ~KnownRecordFlagsMask) != 0) {
        return false;
    }

    writeUint8(output, record.month);
    writeUint8(output + 1, record.reserved0);
    writeUint32Le(output + 2, record.yieldWh);
    writeUint16Le(output + 6, record.maxPowerW);
    writeUint16Le(output + 8, record.avgPowerW);
    writeUint16Le(output + 10, record.runtimeMin);
    writeUint16Le(output + 12, record.dayCount);
    writeUint16Le(output + 14, record.flags);
    return true;
}

[[maybe_unused]] bool decodeMonthRecord(const uint8_t* input, const size_t inputSize, MonthRecord& record)
{
    if (inputSize < MonthRecordSize) {
        return false;
    }

    record.month = readUint8(input);
    record.reserved0 = readUint8(input + 1);
    record.yieldWh = readUint32Le(input + 2);
    record.maxPowerW = readUint16Le(input + 6);
    record.avgPowerW = readUint16Le(input + 8);
    record.runtimeMin = readUint16Le(input + 10);
    record.dayCount = readUint16Le(input + 12);
    record.flags = readUint16Le(input + 14);

    return record.month >= 1
            && record.month <= 12
            && record.reserved0 == 0
            && (record.flags & 0xff00) == 0
            && (record.flags & ~KnownRecordFlagsMask) == 0;
}

const char* directoryForFileType(const uint8_t fileType)
{
    switch (static_cast<FileType>(fileType)) {
    case FileType::FiveMinute:
        return FiveMinuteDirectory;
    case FileType::Day:
        return DayDirectory;
    case FileType::Month:
        return MonthDirectory;
    default:
        return nullptr;
    }
}

bool ensureDirectory(const char* path)
{
    if (LittleFS.mkdir(path)) {
        return true;
    }

    File directory = LittleFS.open(path, "r", false);
    return directory && directory.isDirectory();
}

bool ensureEnergyDirectories(const uint8_t fileType)
{
    const char* typeDirectory = directoryForFileType(fileType);
    if (typeDirectory == nullptr) {
        return false;
    }

    return ensureDirectory(EnergyDirectory) && ensureDirectory(typeDirectory);
}

size_t littleFsFreeBytes()
{
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    return total > used ? total - used : 0;
}

String makePath(const char* directoryPath, const String& fileName)
{
    if (fileName.startsWith("/")) {
        return fileName;
    }

    return String(directoryPath) + "/" + fileName;
}

String baseName(const String& path)
{
    const int lastSlash = path.lastIndexOf('/');
    if (lastSlash < 0) {
        return path;
    }

    return path.substring(lastSlash + 1);
}

bool parseRetentionCandidateName(const String& path, uint16_t& year, uint8_t& month)
{
    const String name = baseName(path);
    if (name.length() < 16 || !name.startsWith("inv_") || !name.endsWith(".eh5")) {
        return false;
    }

    const int yearSeparator = name.lastIndexOf('_', static_cast<int>(name.length()) - 9);
    const int monthSeparator = name.lastIndexOf('_');
    if (yearSeparator <= 4 || monthSeparator <= yearSeparator || monthSeparator + 3 >= name.length()) {
        return false;
    }

    const String serial = name.substring(4, yearSeparator);
    const String yearString = name.substring(yearSeparator + 1, monthSeparator);
    const String monthString = name.substring(monthSeparator + 1, monthSeparator + 3);
    if (serial.isEmpty() || yearString.length() != 4 || monthString.length() != 2 || name.substring(monthSeparator + 3) != ".eh5") {
        return false;
    }

    for (uint16_t i = 0; i < serial.length(); i++) {
        if (serial[i] < '0' || serial[i] > '9') {
            return false;
        }
    }
    for (uint16_t i = 0; i < yearString.length(); i++) {
        if (yearString[i] < '0' || yearString[i] > '9') {
            return false;
        }
    }
    for (uint16_t i = 0; i < monthString.length(); i++) {
        if (monthString[i] < '0' || monthString[i] > '9') {
            return false;
        }
    }

    year = static_cast<uint16_t>(yearString.toInt());
    month = static_cast<uint8_t>(monthString.toInt());
    return year >= 2000 && month >= 1 && month <= 12;
}

bool findOldestRetentionCandidate(String& path)
{
    File directory = LittleFS.open(FiveMinuteDirectory, "r", false);
    if (!directory || !directory.isDirectory()) {
        return false;
    }

    bool found = false;
    uint16_t oldestYear = UINT16_MAX;
    uint8_t oldestMonth = UINT8_MAX;

    File file = directory.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const String candidatePath = makePath(FiveMinuteDirectory, file.name());
            uint16_t year = 0;
            uint8_t month = 0;
            if (parseRetentionCandidateName(candidatePath, year, month)
                    && (!found || year < oldestYear || (year == oldestYear && month < oldestMonth))) {
                found = true;
                oldestYear = year;
                oldestMonth = month;
                path = candidatePath;
            }
        }

        file = directory.openNextFile();
    }

    return found;
}

bool ensureRetentionFreeSpaceForNewFile(bool& removedFiles)
{
    while (littleFsFreeBytes() < RetentionMinimumFreeBytes) {
        String oldestPath;
        if (!findOldestRetentionCandidate(oldestPath)) {
            return false;
        }

        if (!LittleFS.remove(oldestPath)) {
            return false;
        }
        removedFiles = true;
    }

    return true;
}

bool isNewHistoryFile(const char* path)
{
    File file = LittleFS.open(path, "r", false);
    return !file || file.size() == 0;
}

bool writeFull(File& file, const uint8_t* data, const size_t length)
{
    return file.write(data, length) == length;
}

bool headersMatch(const FileHeader& actual, const FileHeader& expected)
{
    return actual.fileType == expected.fileType
            && actual.version == expected.version
            && actual.headerSize == expected.headerSize
            && actual.recordSize == expected.recordSize
            && actual.year == expected.year
            && actual.month == expected.month
            && actual.targetType == expected.targetType
            && actual.serial == expected.serial
            && actual.intervalSec == expected.intervalSec;
}

bool ensureFileHeader(const char* path, const FileHeader& expectedHeader)
{
    uint8_t encodedHeader[FileHeaderSize];
    if (!encodeFileHeader(expectedHeader, encodedHeader, sizeof(encodedHeader))) {
        return false;
    }

    File appendFile = LittleFS.open(path, "a");
    if (!appendFile) {
        return false;
    }

    const size_t fileSize = appendFile.size();
    if (fileSize == 0) {
        return writeFull(appendFile, encodedHeader, sizeof(encodedHeader));
    }

    appendFile.close();
    File file = LittleFS.open(path, "r", false);
    if (!file || fileSize < FileHeaderSize) {
        return false;
    }

    uint8_t existingHeader[FileHeaderSize];
    if (file.read(existingHeader, sizeof(existingHeader)) != FileHeaderSize) {
        return false;
    }

    FileHeader decodedHeader;
    return decodeFileHeader(existingHeader, sizeof(existingHeader), decodedHeader)
            && headersMatch(decodedHeader, expectedHeader);
}

bool readFull(File& file, uint8_t* data, const size_t length)
{
    return file.read(data, length) == length;
}

bool readValidatedFileHeader(File& file, const FileHeader& expectedHeader)
{
    uint8_t encodedHeader[FileHeaderSize];
    if (!readFull(file, encodedHeader, sizeof(encodedHeader))) {
        return false;
    }

    FileHeader decodedHeader;
    return decodeFileHeader(encodedHeader, sizeof(encodedHeader), decodedHeader)
            && headersMatch(decodedHeader, expectedHeader);
}

bool upsertFiveMinuteRecord(FiveMinuteRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, const FiveMinuteRecord& record)
{
    if (records == nullptr || recordCapacity == 0) {
        return true;
    }

    for (uint16_t i = 0; i < recordCount; i++) {
        if (records[i].day == record.day && records[i].slot == record.slot) {
            records[i] = record;
            return true;
        }
    }

    if (recordCount >= recordCapacity) {
        return false;
    }

    records[recordCount] = record;
    recordCount++;
    return true;
}

bool upsertDayRecord(DayRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, const DayRecord& record)
{
    if (records == nullptr || recordCapacity == 0) {
        return true;
    }

    for (uint16_t i = 0; i < recordCount; i++) {
        if (records[i].dayOfYear == record.dayOfYear) {
            records[i] = record;
            return true;
        }
    }

    if (recordCount >= recordCapacity) {
        return false;
    }

    records[recordCount] = record;
    recordCount++;
    return true;
}

bool upsertMonthRecord(MonthRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, const MonthRecord& record)
{
    if (records == nullptr || recordCapacity == 0) {
        return true;
    }

    for (uint16_t i = 0; i < recordCount; i++) {
        if (records[i].month == record.month) {
            records[i] = record;
            return true;
        }
    }

    if (recordCount >= recordCapacity) {
        return false;
    }

    records[recordCount] = record;
    recordCount++;
    return true;
}

bool formatTargetName(char* output, const size_t outputSize, const TargetType targetType, const uint64_t serial)
{
    if (output == nullptr || outputSize == 0) {
        return false;
    }

    if (targetType == TargetType::Total) {
        if (serial != 0) {
            return false;
        }

        return std::snprintf(output, outputSize, "total") > 0;
    }

    if (targetType != TargetType::Inverter || serial == 0) {
        return false;
    }

    return std::snprintf(output, outputSize, "inv_%" PRIu64, serial) > 0;
}

bool isLeapYear(const uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t daysInMonth(const uint16_t year, const uint8_t month)
{
    static constexpr uint8_t DaysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) {
        return 0;
    }

    if (month == 2 && isLeapYear(year)) {
        return 29;
    }

    return DaysPerMonth[month - 1];
}

uint16_t dayOfYear(const uint16_t year, const uint8_t month, const uint8_t day)
{
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)) {
        return 0;
    }

    uint16_t value = day;
    for (uint8_t m = 1; m < month; m++) {
        value += daysInMonth(year, m);
    }

    return value;
}

uint16_t firstDayOfYearForMonth(const uint16_t year, const uint8_t month)
{
    return dayOfYear(year, month, 1);
}

bool monthDayFromDayOfYear(const uint16_t year, const uint16_t dayOfYearValue, uint8_t& month, uint8_t& day)
{
    if (dayOfYearValue < 1 || dayOfYearValue > (isLeapYear(year) ? 366 : 365)) {
        return false;
    }

    uint16_t remaining = dayOfYearValue;
    for (uint8_t m = 1; m <= 12; m++) {
        const uint8_t monthDays = daysInMonth(year, m);
        if (remaining <= monthDays) {
            month = m;
            day = static_cast<uint8_t>(remaining);
            return true;
        }

        remaining -= monthDays;
    }

    return false;
}

uint16_t saturateUint16(const uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

uint16_t averagePowerW(const uint32_t yieldWh, const uint32_t runtimeMin)
{
    if (runtimeMin == 0) {
        return 0;
    }

    const uint64_t value = (static_cast<uint64_t>(yieldWh) * 60U + runtimeMin / 2U) / runtimeMin;
    return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

} // namespace

EnergyHistoryClass EnergyHistory;

EnergyHistoryClass::EnergyHistoryClass()
    : _loopTask(5 * 60 * TASK_SECOND, TASK_FOREVER, std::bind(&EnergyHistoryClass::loop, this))
    , _startupTask(TASK_IMMEDIATE, TASK_ONCE, std::bind(&EnergyHistoryClass::startupLoop, this))
    , _recoveryTask(TASK_IMMEDIATE, TASK_ONCE, std::bind(&EnergyHistoryClass::recoveryLoop, this))
{
}

void EnergyHistoryClass::init(Scheduler& scheduler)
{
    scheduler.addTask(_loopTask);
    scheduler.addTask(_recoveryTask);
    _loopTask.enable();
}

void EnergyHistoryClass::startupLoop()
{
#if defined(ENERGY_HISTORY_MANUAL_PROBE)
    esp_log_level_set(EnergyHistoryTag, ESP_LOG_VERBOSE);
    ManualProbeResult probe;
    const bool probeOk = runManualPersistenceProbe(probe);
    if (probeOk) {
        ESP_LOGI(
                EnergyHistoryTag,
                "Manual persistence probe ok: 5m=%u day=%u month=%u headerMismatch=%u corruptHeader=%u corruptCrc=%u incompleteFinal=%u truncateFinal=%u smallBuffer=%u demoData=%u cleanup=%u records=%u/%u/%u blocks=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
                probe.fiveMinuteOk,
                probe.dayOk,
                probe.monthOk,
                probe.headerMismatchOk,
                probe.corruptHeaderOk,
                probe.corruptCrcOk,
                probe.incompleteFinalBlockOk,
                probe.truncateFinalBlockOk,
                probe.smallBufferOk,
                probe.demoDataOk,
                probe.cleanupOk,
                probe.fiveMinuteRecordsRead,
                probe.dayRecordsRead,
                probe.monthRecordsRead,
                probe.fiveMinuteScan.validBlocks,
                probe.dayScan.validBlocks,
                probe.monthScan.validBlocks);
    } else {
        ESP_LOGE(
                EnergyHistoryTag,
                "Manual persistence probe failed: 5m=%u day=%u month=%u headerMismatch=%u corruptHeader=%u corruptCrc=%u incompleteFinal=%u truncateFinal=%u smallBuffer=%u demoData=%u cleanup=%u records=%u/%u/%u blocks=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
                probe.fiveMinuteOk,
                probe.dayOk,
                probe.monthOk,
                probe.headerMismatchOk,
                probe.corruptHeaderOk,
                probe.corruptCrcOk,
                probe.incompleteFinalBlockOk,
                probe.truncateFinalBlockOk,
                probe.smallBufferOk,
                probe.demoDataOk,
                probe.cleanupOk,
                probe.fiveMinuteRecordsRead,
                probe.dayRecordsRead,
                probe.monthRecordsRead,
                probe.fiveMinuteScan.validBlocks,
                probe.dayScan.validBlocks,
                probe.monthScan.validBlocks);
    }
#endif

    // Runtime startup recovery is intentionally not run from the scheduler.
    // Full-file scans can block other scheduler-driven services on device.
}

bool EnergyHistoryClass::requestRecovery()
{
    if (_recoveryPending || _recoveryRunning) {
        return false;
    }

    _recoveryPending = true;
    _recoveryTask.enable();
    _recoveryTask.restart();
    return true;
}

void EnergyHistoryClass::getRecoveryStatus(RecoveryStatus& status)
{
    status.pending = _recoveryPending;
    status.running = _recoveryRunning;
    status.runCount = _recoveryRunCount;
    status.lastStartedMillis = _recoveryLastStartedMillis;
    status.lastFinishedMillis = _recoveryLastFinishedMillis;
}

void EnergyHistoryClass::getRevision(Revision& revision)
{
    revision.dataRevision = _dataRevision;
    revision.fileRevision = _fileRevision;
    revision.lastChangeMillis = _lastChangeMillis;
}

void EnergyHistoryClass::markHistoryChanged(const bool dataChanged, const bool filesChanged)
{
    if (dataChanged) {
        _dataRevision++;
    }
    if (filesChanged) {
        _fileRevision++;
    }
    if (dataChanged || filesChanged) {
        _lastChangeMillis = millis();
    }
}

void EnergyHistoryClass::markDataChanged()
{
    markHistoryChanged(true, false);
}

void EnergyHistoryClass::markFilesChanged()
{
    markHistoryChanged(true, true);
}

void EnergyHistoryClass::recoveryLoop()
{
    _recoveryPending = false;
    _recoveryRunning = true;
    _recoveryRunCount++;
    _recoveryLastStartedMillis = millis();

    recoverExistingEnergyFiles();

    _recoveryLastFinishedMillis = millis();
    _recoveryRunning = false;
}

bool EnergyHistoryClass::isManagedFilePath(const String& path, String& normalizedPath)
{
    normalizedPath = path;
    normalizedPath.trim();

    if (normalizedPath.isEmpty()) {
        return false;
    }

    if (!normalizedPath.startsWith("/")) {
        normalizedPath = "/" + normalizedPath;
    }

    if (normalizedPath.indexOf("..") >= 0 || normalizedPath.indexOf("//") >= 0) {
        return false;
    }

    if (!normalizedPath.startsWith(String(EnergyDirectory) + "/")) {
        return false;
    }

    if (normalizedPath.endsWith("/")) {
        return false;
    }

    return true;
}

bool EnergyHistoryClass::listFiles(const std::function<void(const FileInfo&)>& visitor)
{
    if (!visitor) {
        return false;
    }

    return listFilesInDirectory(EnergyDirectory, visitor);
}

bool EnergyHistoryClass::makeFileHeader(const FileType fileType, const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, FileHeader& header)
{
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.magic, FileMagic, sizeof(header.magic));
    header.fileType = static_cast<uint8_t>(fileType);
    header.version = Version;
    header.headerSize = FileHeaderSize;
    header.recordSize = expectedRecordSize(fileType);
    header.year = year;
    header.month = fileType == FileType::FiveMinute ? month : 0;
    header.targetType = static_cast<uint8_t>(targetType);
    header.serial = serial;
    header.intervalSec = fileType == FileType::FiveMinute ? FiveMinuteIntervalSec : 0;

    return validateHeaderFields(header);
}

String EnergyHistoryClass::makeFiveMinutePath(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month)
{
    if (month < 1 || month > 12) {
        return String();
    }

    char targetName[32];
    if (!formatTargetName(targetName, sizeof(targetName), targetType, serial)) {
        return String();
    }

    char path[80];
    if (std::snprintf(path, sizeof(path), "%s/%s_%04u_%02u.eh5", FiveMinuteDirectory, targetName, year, month) <= 0) {
        return String();
    }

    return String(path);
}

String EnergyHistoryClass::makeDayPath(const TargetType targetType, const uint64_t serial, const uint16_t year)
{
    char targetName[32];
    if (!formatTargetName(targetName, sizeof(targetName), targetType, serial)) {
        return String();
    }

    char path[80];
    if (std::snprintf(path, sizeof(path), "%s/%s_%04u.ehd", DayDirectory, targetName, year) <= 0) {
        return String();
    }

    return String(path);
}

String EnergyHistoryClass::makeMonthPath(const TargetType targetType, const uint64_t serial, const uint16_t year)
{
    char targetName[32];
    if (!formatTargetName(targetName, sizeof(targetName), targetType, serial)) {
        return String();
    }

    char path[80];
    if (std::snprintf(path, sizeof(path), "%s/%s_%04u.ehm", MonthDirectory, targetName, year) <= 0) {
        return String();
    }

    return String(path);
}

bool EnergyHistoryClass::writeFiveMinute(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, const FiveMinuteRecord* records, const uint16_t recordCount, const uint16_t blockIndex)
{
    FileHeader header;
    if (!makeFileHeader(FileType::FiveMinute, targetType, serial, year, month, header)) {
        return false;
    }

    const String path = makeFiveMinutePath(targetType, serial, year, month);
    if (path.isEmpty()) {
        return false;
    }

    return appendFiveMinuteBlock(path.c_str(), header, records, recordCount, blockIndex);
}

bool EnergyHistoryClass::writeDay(const TargetType targetType, const uint64_t serial, const uint16_t year, const DayRecord* records, const uint16_t recordCount, const uint16_t blockIndex)
{
    FileHeader header;
    if (!makeFileHeader(FileType::Day, targetType, serial, year, 0, header)) {
        return false;
    }

    const String path = makeDayPath(targetType, serial, year);
    if (path.isEmpty()) {
        return false;
    }

    return appendDayBlock(path.c_str(), header, records, recordCount, blockIndex);
}

bool EnergyHistoryClass::writeMonth(const TargetType targetType, const uint64_t serial, const uint16_t year, const MonthRecord* records, const uint16_t recordCount, const uint16_t blockIndex)
{
    FileHeader header;
    if (!makeFileHeader(FileType::Month, targetType, serial, year, 0, header)) {
        return false;
    }

    const String path = makeMonthPath(targetType, serial, year);
    if (path.isEmpty()) {
        return false;
    }

    return appendMonthBlock(path.c_str(), header, records, recordCount, blockIndex);
}

bool EnergyHistoryClass::readFiveMinute(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, FiveMinuteRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    FileHeader header;
    if (!makeFileHeader(FileType::FiveMinute, targetType, serial, year, month, header)) {
        return false;
    }

    const String path = makeFiveMinutePath(targetType, serial, year, month);
    if (path.isEmpty()) {
        return false;
    }

    return readFiveMinuteFile(path.c_str(), header, records, recordCapacity, recordCount, result);
}

bool EnergyHistoryClass::readDay(const TargetType targetType, const uint64_t serial, const uint16_t year, DayRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    FileHeader header;
    if (!makeFileHeader(FileType::Day, targetType, serial, year, 0, header)) {
        return false;
    }

    const String path = makeDayPath(targetType, serial, year);
    if (path.isEmpty()) {
        return false;
    }

    return readDayFile(path.c_str(), header, records, recordCapacity, recordCount, result);
}

bool EnergyHistoryClass::readMonth(const TargetType targetType, const uint64_t serial, const uint16_t year, MonthRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    FileHeader header;
    if (!makeFileHeader(FileType::Month, targetType, serial, year, 0, header)) {
        return false;
    }

    const String path = makeMonthPath(targetType, serial, year);
    if (path.isEmpty()) {
        return false;
    }

    return readMonthFile(path.c_str(), header, records, recordCapacity, recordCount, result);
}

bool EnergyHistoryClass::appendBlock(const char* path, const FileHeader& expectedHeader, const uint16_t blockIndex, const uint16_t startKey, const uint8_t* payload, const uint16_t payloadSize, const uint16_t recordCount)
{
    if (path == nullptr
            || payload == nullptr
            || payloadSize == 0
            || recordCount == 0
            || expectedHeader.recordSize == 0
            || payloadSize != recordCount * expectedHeader.recordSize) {
        return false;
    }

    if (!ensureEnergyDirectories(expectedHeader.fileType)) {
        return false;
    }

    const bool newHistoryFile = isNewHistoryFile(path);
    bool retentionRemovedFiles = false;
    if (newHistoryFile) {
        if (!ensureRetentionFreeSpaceForNewFile(retentionRemovedFiles)) {
            return false;
        }
    }

    if (!ensureFileHeader(path, expectedHeader)) {
        return false;
    }

    BlockHeader blockHeader;
    std::memcpy(blockHeader.magic, BlockMagic, sizeof(blockHeader.magic));
    blockHeader.blockIndex = blockIndex;
    blockHeader.startKey = startKey;
    blockHeader.recordCount = recordCount;
    blockHeader.payloadSize = payloadSize;
    blockHeader.crc32Payload = calculateCrc32(payload, payloadSize);

    uint8_t encodedBlockHeader[BlockHeaderSize];
    if (!encodeBlockHeader(blockHeader, encodedBlockHeader, sizeof(encodedBlockHeader))) {
        return false;
    }

    auto appendEncodedBlock = [&]() -> bool {
        File file = LittleFS.open(path, "a");
        if (!file) {
            return false;
        }

        if (!writeFull(file, encodedBlockHeader, sizeof(encodedBlockHeader))
                || !writeFull(file, payload, payloadSize)) {
            return false;
        }

        file.flush();
        return true;
    };

    if (appendEncodedBlock()) {
        markHistoryChanged(true, newHistoryFile || retentionRemovedFiles);
        return true;
    }

    ScanResult recoveryScan;
    if (!recoverFinalBlock(path, expectedHeader, recoveryScan)) {
        return false;
    }

    if (!appendEncodedBlock()) {
        return false;
    }

    markHistoryChanged(true, true);
    return true;
}

bool EnergyHistoryClass::appendFiveMinuteBlock(const char* path, const FileHeader& expectedHeader, const FiveMinuteRecord* records, const uint16_t recordCount, const uint16_t blockIndex)
{
    if (records == nullptr
            || recordCount == 0
            || expectedHeader.fileType != static_cast<uint8_t>(FileType::FiveMinute)
            || expectedHeader.recordSize != FiveMinuteRecordSize) {
        return false;
    }

    uint8_t payload[FiveMinuteRecordSize * 3];
    if (recordCount > sizeof(payload) / FiveMinuteRecordSize) {
        return false;
    }

    for (uint16_t i = 0; i < recordCount; i++) {
        if (!encodeFiveMinuteRecord(records[i], payload + i * FiveMinuteRecordSize, FiveMinuteRecordSize)) {
            return false;
        }
    }

    return appendBlock(path, expectedHeader, blockIndex, records[0].day, payload, recordCount * FiveMinuteRecordSize, recordCount);
}

bool EnergyHistoryClass::appendDayBlock(const char* path, const FileHeader& expectedHeader, const DayRecord* records, const uint16_t recordCount, const uint16_t blockIndex)
{
    if (records == nullptr
            || recordCount == 0
            || recordCount > MaxRecordsPerAppendBlock
            || expectedHeader.fileType != static_cast<uint8_t>(FileType::Day)
            || expectedHeader.recordSize != DayRecordSize) {
        return false;
    }

    uint8_t payload[DayRecordSize * MaxRecordsPerAppendBlock];
    for (uint16_t i = 0; i < recordCount; i++) {
        if (!encodeDayRecord(records[i], payload + i * DayRecordSize, DayRecordSize)) {
            return false;
        }
    }

    return appendBlock(path, expectedHeader, blockIndex, records[0].dayOfYear, payload, recordCount * DayRecordSize, recordCount);
}

bool EnergyHistoryClass::appendMonthBlock(const char* path, const FileHeader& expectedHeader, const MonthRecord* records, const uint16_t recordCount, const uint16_t blockIndex)
{
    if (records == nullptr
            || recordCount == 0
            || recordCount > MaxRecordsPerAppendBlock
            || expectedHeader.fileType != static_cast<uint8_t>(FileType::Month)
            || expectedHeader.recordSize != MonthRecordSize) {
        return false;
    }

    uint8_t payload[MonthRecordSize * MaxRecordsPerAppendBlock];
    for (uint16_t i = 0; i < recordCount; i++) {
        if (!encodeMonthRecord(records[i], payload + i * MonthRecordSize, MonthRecordSize)) {
            return false;
        }
    }

    return appendBlock(path, expectedHeader, blockIndex, records[0].month, payload, recordCount * MonthRecordSize, recordCount);
}

bool EnergyHistoryClass::truncateFile(const char* path, const size_t size)
{
    if (path == nullptr || size < FileHeaderSize) {
        return false;
    }

    File source = LittleFS.open(path, "r", false);
    if (!source || source.size() < size) {
        return false;
    }

    if (source.size() == size) {
        return true;
    }

    const String tempPath = String(path) + ".tmp";
    LittleFS.remove(tempPath);

    File target = LittleFS.open(tempPath, "w");
    if (!target) {
        return false;
    }

    uint8_t buffer[FileReadBufferSize];
    size_t remaining = size;
    while (remaining > 0) {
        const size_t chunkSize = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!readFull(source, buffer, chunkSize) || !writeFull(target, buffer, chunkSize)) {
            source.close();
            target.close();
            LittleFS.remove(tempPath);
            return false;
        }

        remaining -= chunkSize;
    }

    target.flush();
    source.close();
    target.close();

    if (!LittleFS.remove(path)) {
        LittleFS.remove(tempPath);
        return false;
    }

    if (!LittleFS.rename(tempPath, path)) {
        LittleFS.remove(tempPath);
        return false;
    }

    markHistoryChanged(true, true);
    return true;
}

bool EnergyHistoryClass::recoverFinalBlock(const char* path, const FileHeader& expectedHeader, ScanResult& result)
{
    result = ScanResult();
    if (!scanFile(path, expectedHeader, result)) {
        return false;
    }

    if (!result.canTruncateFinalBlock) {
        return true;
    }

    return truncateFile(path, result.lastValidOffset);
}

void EnergyHistoryClass::recoverExistingEnergyFiles()
{
    recoverEnergyDirectory(FiveMinuteDirectory);
    recoverEnergyDirectory(DayDirectory);
    recoverEnergyDirectory(MonthDirectory);
}

void EnergyHistoryClass::recoverEnergyDirectory(const char* directoryPath)
{
    File directory = LittleFS.open(directoryPath, "r", false);
    if (!directory || !directory.isDirectory()) {
        return;
    }

    File file = directory.openNextFile();
    while (file) {
        if (!file.isDirectory() && file.size() >= FileHeaderSize) {
            const String fileName = file.name();
            String path = fileName;
            if (!fileName.startsWith("/")) {
                path = String(directoryPath) + "/" + fileName;
            }

            uint8_t encodedHeader[FileHeaderSize];
            if (file.read(encodedHeader, sizeof(encodedHeader)) == FileHeaderSize) {
                FileHeader header;
                ScanResult result;
                if (decodeFileHeader(encodedHeader, sizeof(encodedHeader), header)) {
                    recoverFinalBlock(path.c_str(), header, result);
                }
            }
        }

        file = directory.openNextFile();
    }

    file.close();
    directory.close();
}

bool EnergyHistoryClass::listFilesInDirectory(const char* directoryPath, const std::function<void(const FileInfo&)>& visitor)
{
    File directory = LittleFS.open(directoryPath, "r", false);
    if (!directory || !directory.isDirectory()) {
        return false;
    }

    File file = directory.openNextFile();
    while (file) {
        String path = file.path();
        if (path.isEmpty()) {
            const String fileName = file.name();
            path = fileName.startsWith("/") ? fileName : String(directoryPath) + "/" + fileName;
        }

        if (file.isDirectory()) {
            listFilesInDirectory(path.c_str(), visitor);
        } else {
            FileInfo info;
            info.path = path;
            info.size = file.size();
            visitor(info);
        }

        file = directory.openNextFile();
    }

    file.close();
    directory.close();
    return true;
}

bool EnergyHistoryClass::scanFile(const char* path, const FileHeader& expectedHeader, ScanResult& result)
{
    if (path == nullptr) {
        return false;
    }

    File file = LittleFS.open(path, "r", false);
    if (!file || file.size() < FileHeaderSize || !readValidatedFileHeader(file, expectedHeader)) {
        return false;
    }

    result.filesScanned++;
    result.fileSize = file.size();
    result.lastValidOffset = FileHeaderSize;

    while (file.available() > 0) {
        const size_t remaining = result.fileSize - file.position();
        if (remaining < BlockHeaderSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        uint8_t encodedBlockHeader[BlockHeaderSize];
        if (!readFull(file, encodedBlockHeader, sizeof(encodedBlockHeader))) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        BlockHeader blockHeader;
        if (!decodeBlockHeader(encodedBlockHeader, sizeof(encodedBlockHeader), blockHeader)
                || blockHeader.recordCount == 0
                || blockHeader.payloadSize == 0
                || blockHeader.payloadSize != blockHeader.recordCount * expectedHeader.recordSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        if (result.fileSize - file.position() < blockHeader.payloadSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        uint32_t crc = 0xffffffff;
        uint8_t buffer[FileReadBufferSize];
        uint16_t bytesRemaining = blockHeader.payloadSize;

        while (bytesRemaining > 0) {
            const size_t chunkSize = bytesRemaining < sizeof(buffer) ? bytesRemaining : sizeof(buffer);
            if (!readFull(file, buffer, chunkSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            crc = updateCrc32(crc, buffer, chunkSize);
            bytesRemaining -= chunkSize;
        }

        if (blockHeader.crc32Payload != finalizeCrc32(crc)) {
            result.skippedBlocks++;
            if (file.position() == result.fileSize) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
            continue;
        }

        result.validBlocks++;
        result.validRecords += blockHeader.recordCount;
        result.lastValidOffset = file.position();
    }

    return true;
}

bool EnergyHistoryClass::scanFiveMinuteFile(const char* path, const FileHeader& expectedHeader, ScanResult& result)
{
    uint16_t recordCount = 0;
    return readFiveMinuteFile(path, expectedHeader, nullptr, 0, recordCount, result);
}

template <typename Record>
bool EnergyHistoryClass::readRecordFile(const char* path, const FileHeader& expectedHeader, const FileType fileType, const uint8_t recordSize, Record* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result, bool (*decodeRecord)(const uint8_t*, size_t, Record&), bool (*upsertRecord)(Record*, uint16_t, uint16_t&, const Record&))
{
    if (path == nullptr
            || expectedHeader.fileType != static_cast<uint8_t>(fileType)
            || expectedHeader.recordSize != recordSize
            || decodeRecord == nullptr
            || upsertRecord == nullptr) {
        return false;
    }

    recordCount = 0;

    File file = LittleFS.open(path, "r", false);
    if (!file || file.size() < FileHeaderSize || !readValidatedFileHeader(file, expectedHeader)) {
        return false;
    }

    result.filesScanned++;
    result.fileSize = file.size();
    result.lastValidOffset = FileHeaderSize;

    while (file.available() > 0) {
        const size_t remaining = result.fileSize - file.position();
        if (remaining < BlockHeaderSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        uint8_t encodedBlockHeader[BlockHeaderSize];
        if (!readFull(file, encodedBlockHeader, sizeof(encodedBlockHeader))) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        BlockHeader blockHeader;
        if (!decodeBlockHeader(encodedBlockHeader, sizeof(encodedBlockHeader), blockHeader)
                || blockHeader.recordCount == 0
                || blockHeader.payloadSize == 0
                || blockHeader.payloadSize != blockHeader.recordCount * recordSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        if (result.fileSize - file.position() < blockHeader.payloadSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        const size_t payloadStart = file.position();
        uint32_t crc = 0xffffffff;
        uint16_t bytesRemaining = blockHeader.payloadSize;
        uint8_t buffer[FileReadBufferSize];

        while (bytesRemaining > 0) {
            const size_t chunkSize = bytesRemaining < sizeof(buffer) ? bytesRemaining : sizeof(buffer);
            if (!readFull(file, buffer, chunkSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            crc = updateCrc32(crc, buffer, chunkSize);
            bytesRemaining -= chunkSize;
        }

        if (blockHeader.crc32Payload != finalizeCrc32(crc)) {
            result.skippedBlocks++;
            if (file.position() == result.fileSize) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
            continue;
        }

        if (!file.seek(payloadStart)) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            return true;
        }

        for (uint16_t i = 0; i < blockHeader.recordCount; i++) {
            uint8_t encodedRecord[MonthRecordSize];
            if (!readFull(file, encodedRecord, recordSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            Record record;
            if (!decodeRecord(encodedRecord, recordSize, record)) {
                result.skippedRecords++;
                continue;
            }

            result.validRecords++;
            if (!upsertRecord(records, recordCapacity, recordCount, record)) {
                result.skippedRecords++;
            }
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    return true;
}

bool EnergyHistoryClass::readFiveMinuteFile(const char* path, const FileHeader& expectedHeader, FiveMinuteRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    return readRecordFile(path, expectedHeader, FileType::FiveMinute, FiveMinuteRecordSize, records, recordCapacity, recordCount, result, decodeFiveMinuteRecord, upsertFiveMinuteRecord);
}

bool EnergyHistoryClass::readDayFile(const char* path, const FileHeader& expectedHeader, DayRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    return readRecordFile(path, expectedHeader, FileType::Day, DayRecordSize, records, recordCapacity, recordCount, result, decodeDayRecord, upsertDayRecord);
}

bool EnergyHistoryClass::readMonthFile(const char* path, const FileHeader& expectedHeader, MonthRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    return readRecordFile(path, expectedHeader, FileType::Month, MonthRecordSize, records, recordCapacity, recordCount, result, decodeMonthRecord, upsertMonthRecord);
}

bool EnergyHistoryClass::getStatus(Status& status)
{
    status = Status();
    status.littlefsTotalBytes = LittleFS.totalBytes();
    status.littlefsUsedBytes = LittleFS.usedBytes();

    const char* directories[] = {
        FiveMinuteDirectory,
        DayDirectory,
        MonthDirectory,
    };

    for (const char* directoryPath : directories) {
        File directory = LittleFS.open(directoryPath, "r", false);
        if (!directory || !directory.isDirectory()) {
            continue;
        }

        File file = directory.openNextFile();
        while (file) {
            if (!file.isDirectory() && file.size() >= FileHeaderSize) {
                status.filesScanned++;
                status.bytesScanned += file.size();
            }

            file = directory.openNextFile();
        }
    }

    return true;
}

bool EnergyHistoryClass::scanManagedFile(const String& path, ScanResult& result)
{
    result = ScanResult();

    String normalizedPath;
    if (!isManagedFilePath(path, normalizedPath)) {
        return false;
    }

    File file = LittleFS.open(normalizedPath.c_str(), "r", false);
    if (!file || file.isDirectory() || file.size() < FileHeaderSize) {
        return false;
    }

    uint8_t encodedHeader[FileHeaderSize];
    if (file.read(encodedHeader, sizeof(encodedHeader)) != FileHeaderSize) {
        return false;
    }

    FileHeader header;
    if (!decodeFileHeader(encodedHeader, sizeof(encodedHeader), header)) {
        return false;
    }

    file.close();
    return scanFile(normalizedPath.c_str(), header, result);
}

bool EnergyHistoryClass::recoverManagedFile(const String& path, ScanResult& result)
{
    result = ScanResult();

    String normalizedPath;
    if (!isManagedFilePath(path, normalizedPath)) {
        return false;
    }

    File file = LittleFS.open(normalizedPath.c_str(), "r", false);
    if (!file || file.isDirectory() || file.size() < FileHeaderSize) {
        return false;
    }

    uint8_t encodedHeader[FileHeaderSize];
    if (file.read(encodedHeader, sizeof(encodedHeader)) != FileHeaderSize) {
        return false;
    }

    FileHeader header;
    if (!decodeFileHeader(encodedHeader, sizeof(encodedHeader), header)) {
        return false;
    }

    file.close();
    return recoverFinalBlock(normalizedPath.c_str(), header, result);
}

bool EnergyHistoryClass::queryFiveMinuteDay(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, const uint8_t day, FiveMinuteRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    result = ScanResult();
    recordCount = 0;
    if (records == nullptr
            || recordCapacity == 0
            || day < 1
            || day > 31) {
        return false;
    }

    FileHeader header;
    if (!makeFileHeader(FileType::FiveMinute, targetType, serial, year, month, header)) {
        return false;
    }

    const String path = makeFiveMinutePath(targetType, serial, year, month);
    if (path.isEmpty()) {
        return false;
    }

    File file = LittleFS.open(path.c_str(), "r", false);
    if (!file || file.size() < FileHeaderSize || !readValidatedFileHeader(file, header)) {
        return false;
    }

    result.filesScanned++;
    result.fileSize = file.size();
    result.lastValidOffset = FileHeaderSize;

    while (file.available() > 0) {
        const size_t remaining = result.fileSize - file.position();
        if (remaining < BlockHeaderSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        uint8_t encodedBlockHeader[BlockHeaderSize];
        if (!readFull(file, encodedBlockHeader, sizeof(encodedBlockHeader))) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        BlockHeader blockHeader;
        if (!decodeBlockHeader(encodedBlockHeader, sizeof(encodedBlockHeader), blockHeader)
                || blockHeader.recordCount == 0
                || blockHeader.payloadSize == 0
                || blockHeader.payloadSize != blockHeader.recordCount * FiveMinuteRecordSize
                || result.fileSize - file.position() < blockHeader.payloadSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        const size_t payloadStart = file.position();
        if (blockHeader.startKey != day) {
            if (!file.seek(payloadStart + blockHeader.payloadSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            result.lastValidOffset = file.position();
            continue;
        }

        uint32_t crc = 0xffffffff;
        uint16_t bytesRemaining = blockHeader.payloadSize;
        uint8_t buffer[FileReadBufferSize];
        while (bytesRemaining > 0) {
            const size_t chunkSize = bytesRemaining < sizeof(buffer) ? bytesRemaining : sizeof(buffer);
            if (!readFull(file, buffer, chunkSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            crc = updateCrc32(crc, buffer, chunkSize);
            bytesRemaining -= chunkSize;
        }

        if (blockHeader.crc32Payload != finalizeCrc32(crc)) {
            result.skippedBlocks++;
            if (file.position() == result.fileSize) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
            continue;
        }

        if (!file.seek(payloadStart)) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            return true;
        }

        for (uint16_t i = 0; i < blockHeader.recordCount; i++) {
            uint8_t encodedRecord[FiveMinuteRecordSize];
            if (!readFull(file, encodedRecord, sizeof(encodedRecord))) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            FiveMinuteRecord record;
            if (!decodeFiveMinuteRecord(encodedRecord, sizeof(encodedRecord), record)) {
                result.skippedRecords++;
                continue;
            }

            result.validRecords++;
            if (record.day == day && !upsertFiveMinuteRecord(records, recordCapacity, recordCount, record)) {
                result.skippedRecords++;
            }
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    return true;
}

bool EnergyHistoryClass::queryDay(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint16_t fromDayOfYear, const uint16_t toDayOfYear, DayRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    result = ScanResult();
    recordCount = 0;
    if (records == nullptr
            || recordCapacity == 0
            || fromDayOfYear < 1
            || toDayOfYear > 366
            || fromDayOfYear > toDayOfYear) {
        return false;
    }

    FileHeader header;
    if (!makeFileHeader(FileType::Day, targetType, serial, year, 0, header)) {
        return false;
    }

    const String path = makeDayPath(targetType, serial, year);
    if (path.isEmpty()) {
        return false;
    }

    std::unique_ptr<DayRecord[]> allRecords(new (std::nothrow) DayRecord[366]);
    if (!allRecords) {
        return false;
    }

    uint16_t allRecordCount = 0;
    if (!readDayFile(path.c_str(), header, allRecords.get(), 366, allRecordCount, result)
            && LittleFS.exists(path)) {
        return false;
    }

    tm currentLocalTime = {};
    const bool hasCurrentLocalTime = getLocalTime(&currentLocalTime, 5);
    const uint16_t currentYear = hasCurrentLocalTime ? static_cast<uint16_t>(currentLocalTime.tm_year + 1900) : 0;
    const uint8_t currentMonth = hasCurrentLocalTime ? static_cast<uint8_t>(currentLocalTime.tm_mon + 1) : 0;
    const uint8_t currentDay = hasCurrentLocalTime ? static_cast<uint8_t>(currentLocalTime.tm_mday) : 0;
    const uint16_t currentDayOfYear = hasCurrentLocalTime ? dayOfYear(currentYear, currentMonth, currentDay) : 0;

    auto isCurrentOpenDay = [&](const uint16_t dayOfYearValue) {
        return hasCurrentLocalTime
                && year == currentYear
                && dayOfYearValue == currentDayOfYear;
    };

    uint16_t missingDaysByMonth[12][31] = {};
    uint16_t missingDayCountByMonth[12] = {};

    for (uint16_t dayOfYearValue = fromDayOfYear; dayOfYearValue <= toDayOfYear; dayOfYearValue++) {
        const DayRecord* existingRecord = nullptr;
        for (uint16_t i = 0; i < allRecordCount; i++) {
            if (allRecords[i].dayOfYear == dayOfYearValue) {
                existingRecord = &allRecords[i];
                break;
            }
        }

        if (recordCount >= recordCapacity) {
            result.skippedRecords++;
            continue;
        }

        if (existingRecord != nullptr && !isCurrentOpenDay(dayOfYearValue)) {
            records[recordCount] = *existingRecord;
            recordCount++;
            continue;
        }

        uint8_t month = 0;
        uint8_t day = 0;
        if (monthDayFromDayOfYear(year, dayOfYearValue, month, day) && month >= 1 && month <= 12) {
            const uint8_t monthIndex = static_cast<uint8_t>(month - 1);
            if (missingDayCountByMonth[monthIndex] < 31) {
                missingDaysByMonth[monthIndex][missingDayCountByMonth[monthIndex]] = dayOfYearValue;
                missingDayCountByMonth[monthIndex]++;
            }
        }
    }

    for (uint8_t monthIndex = 0; monthIndex < 12; monthIndex++) {
        const uint16_t missingDayCount = missingDayCountByMonth[monthIndex];
        if (missingDayCount == 0) {
            continue;
        }

        DayRecord derivedRecords[31];
        uint16_t derivedRecordCount = 0;
        ScanResult monthScan;
        if (!buildDayRecordsFromFiveMinuteMonth(
                    targetType,
                    serial,
                    year,
                    static_cast<uint8_t>(monthIndex + 1),
                    missingDaysByMonth[monthIndex],
                    missingDayCount,
                    derivedRecords,
                    sizeof(derivedRecords) / sizeof(derivedRecords[0]),
                    derivedRecordCount,
                    monthScan)) {
            continue;
        }

        result.filesScanned += monthScan.filesScanned;
        result.validBlocks += monthScan.validBlocks;
        result.skippedBlocks += monthScan.skippedBlocks;
        result.validRecords += monthScan.validRecords;
        result.skippedRecords += monthScan.skippedRecords;

        if (derivedRecordCount > 0) {
            DayRecord persistRecords[31];
            uint16_t persistRecordCount = 0;
            for (uint16_t i = 0; i < derivedRecordCount; i++) {
                if (!isCurrentOpenDay(derivedRecords[i].dayOfYear)) {
                    persistRecords[persistRecordCount] = derivedRecords[i];
                    persistRecordCount++;
                }
            }

            if (persistRecordCount > 0) {
                writeDayRecordsBatched(targetType, serial, year, persistRecords, persistRecordCount);
            }
        }

        for (uint16_t i = 0; i < derivedRecordCount; i++) {
            if (derivedRecords[i].dayOfYear < fromDayOfYear || derivedRecords[i].dayOfYear > toDayOfYear) {
                continue;
            }

            if (recordCount >= recordCapacity) {
                result.skippedRecords++;
                continue;
            }

            records[recordCount] = derivedRecords[i];
            recordCount++;
        }
    }

    for (uint16_t i = 1; i < recordCount; i++) {
        DayRecord value = records[i];
        uint16_t j = i;
        while (j > 0 && records[j - 1].dayOfYear > value.dayOfYear) {
            records[j] = records[j - 1];
            j--;
        }
        records[j] = value;
    }

    return true;
}

// EnergyHistory.cpp
// Watchdog-freundliche Lösung für fehlende Monatswerte.
//
// Ersetze die bestehende Funktion EnergyHistoryClass::queryMonth(...) vollständig durch diese Version.
// EnergyHistory.h muss dafür nicht geändert werden.
//
// Verhalten:
// - Vorhandene Monatsrecords werden sofort zurückgegeben.
// - Fehlende Monatsrecords werden aus Tagesdaten rekonstruiert.
// - queryDay() darf dabei fehlende Tagesdaten aus 5-Minuten-Daten rekonstruieren und persistieren.
// - Pro Aufruf wird höchstens EIN fehlender Monat rekonstruiert, damit async_tcp/loopTask nicht
//   zu lange blockiert und der ESP32-Task-Watchdog nicht auslöst.
// - Die UI bekommt dadurch sofort alle schon vorhandenen Monatswerte. Fehlende Monate füllen sich
//   über wiederholte Abfragen nach und nach, werden dann aber dauerhaft in LittleFS persistiert.

bool EnergyHistoryClass::queryMonth(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t fromMonth, const uint8_t toMonth, MonthRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    result = ScanResult();
    recordCount = 0;

    if (records == nullptr
            || recordCapacity == 0
            || fromMonth < 1
            || toMonth > 12
            || fromMonth > toMonth) {
        return false;
    }

    // Wichtig für ESP32 + AsyncWebServer/loopTask:
    // Ein kompletter Jahres-Rebuild kann mehrere LittleFS-Dateien lesen und schreiben.
    // Deshalb nur eine kleine Arbeitseinheit pro Abfrage nachholen.
    static constexpr uint8_t MaxMonthRebuildsPerQuery = 1;

    auto cooperativeYield = []() {
        // Gibt anderen Tasks, insbesondere async_tcp und WiFi, Zeit.
        // Keine harte Garantie gegen jede Blockade, aber verhindert lange CPU-Monopole
        // zwischen den einzelnen Rekonstruktionsschritten.
        delay(1);
    };

    FileHeader monthHeader;
    if (!makeFileHeader(FileType::Month, targetType, serial, year, 0, monthHeader)) {
        return false;
    }

    const String monthPath = makeMonthPath(targetType, serial, year);
    if (monthPath.isEmpty()) {
        return false;
    }

    std::unique_ptr<MonthRecord[]> allRecords(new (std::nothrow) MonthRecord[12]);
    if (!allRecords) {
        return false;
    }

    uint16_t allRecordCount = 0;
    if (!readMonthFile(monthPath.c_str(), monthHeader, allRecords.get(), 12, allRecordCount, result)
            && LittleFS.exists(monthPath)) {
        return false;
    }

    cooperativeYield();

    bool monthPresent[12] = {};
    for (uint16_t i = 0; i < allRecordCount; i++) {
        if (allRecords[i].month >= 1 && allRecords[i].month <= 12) {
            monthPresent[allRecords[i].month - 1] = true;
        }
    }

    tm currentLocalTime = {};
    const bool hasCurrentLocalTime = getLocalTime(&currentLocalTime, 5);
    const uint16_t currentYear = hasCurrentLocalTime ? static_cast<uint16_t>(currentLocalTime.tm_year + 1900) : 0;
    const uint8_t currentMonth = hasCurrentLocalTime ? static_cast<uint8_t>(currentLocalTime.tm_mon + 1) : 0;

    auto sameMonthRecord = [](const MonthRecord& lhs, const MonthRecord& rhs) {
        return lhs.month == rhs.month
                && lhs.yieldWh == rhs.yieldWh
                && lhs.maxPowerW == rhs.maxPowerW
                && lhs.avgPowerW == rhs.avgPowerW
                && lhs.runtimeMin == rhs.runtimeMin
                && lhs.dayCount == rhs.dayCount
                && lhs.flags == rhs.flags;
    };

    uint8_t rebuildCount = 0;
    MonthRecord derivedRecords[MaxMonthRebuildsPerQuery];
    uint16_t derivedRecordCount = 0;

    for (uint8_t monthValue = fromMonth;
            monthValue <= toMonth && rebuildCount < MaxMonthRebuildsPerQuery;
            monthValue++) {
        const bool currentOpenMonth = hasCurrentLocalTime
                && year == currentYear
                && monthValue == currentMonth;
        if (monthPresent[monthValue - 1] && !currentOpenMonth) {
            continue;
        }

        const uint16_t fromDayOfYear = firstDayOfYearForMonth(year, monthValue);
        const uint8_t monthDays = daysInMonth(year, monthValue);
        if (fromDayOfYear == 0 || monthDays == 0) {
            continue;
        }

        const uint16_t toDayOfYear = static_cast<uint16_t>(fromDayOfYear + monthDays - 1);

        std::unique_ptr<DayRecord[]> dayRecords(new (std::nothrow) DayRecord[31]);
        if (!dayRecords) {
            break;
        }

        uint16_t dayRecordCount = 0;
        ScanResult dayScan;

        // queryDay() ist hier absichtlich nur auf EINEN Monat begrenzt.
        // Falls Tagesrecords fehlen, baut queryDay() diese aus der 5-Minuten-Datei dieses Monats auf
        // und persistiert sie über writeDayRecordsBatched().
        if (!queryDay(
                    targetType,
                    serial,
                    year,
                    fromDayOfYear,
                    toDayOfYear,
                    dayRecords.get(),
                    31,
                    dayRecordCount,
                    dayScan)) {
            cooperativeYield();
            continue;
        }

        result.filesScanned += dayScan.filesScanned;
        result.validBlocks += dayScan.validBlocks;
        result.skippedBlocks += dayScan.skippedBlocks;
        result.validRecords += dayScan.validRecords;
        result.skippedRecords += dayScan.skippedRecords;
        result.invalidFinalBlock = result.invalidFinalBlock || dayScan.invalidFinalBlock;
        result.canTruncateFinalBlock = result.canTruncateFinalBlock || dayScan.canTruncateFinalBlock;

        cooperativeYield();

        MonthRecord monthRecord;
        if (!buildMonthRecordFromDayRecords(year, monthValue, dayRecords.get(), dayRecordCount, monthRecord)) {
            continue;
        }

        const MonthRecord* existingRecord = nullptr;
        for (uint16_t i = 0; i < allRecordCount; i++) {
            if (allRecords[i].month == monthValue) {
                existingRecord = &allRecords[i];
                break;
            }
        }

        if (currentOpenMonth && existingRecord != nullptr && sameMonthRecord(*existingRecord, monthRecord)) {
            monthPresent[monthValue - 1] = true;
            rebuildCount++;
            continue;
        }

        if (!upsertMonthRecord(allRecords.get(), 12, allRecordCount, monthRecord)) {
            result.skippedRecords++;
            continue;
        }

        monthPresent[monthValue - 1] = true;
        if (!currentOpenMonth) {
            derivedRecords[derivedRecordCount] = monthRecord;
            derivedRecordCount++;
        }
        rebuildCount++;
    }

    if (derivedRecordCount > 0) {
        // Absichtlich nicht fatal: Auch wenn LittleFS im Moment nicht schreiben kann,
        // soll die Abfrage den rekonstruierten Wert liefern. Beim nächsten Aufruf wird erneut versucht.
        if (!writeMonthRecordsBatched(targetType, serial, year, derivedRecords, derivedRecordCount)) {
            result.skippedRecords += derivedRecordCount;
        }
        cooperativeYield();
    }

    for (uint8_t monthValue = fromMonth; monthValue <= toMonth; monthValue++) {
        if (recordCount >= recordCapacity) {
            result.skippedRecords++;
            continue;
        }

        for (uint16_t i = 0; i < allRecordCount; i++) {
            if (allRecords[i].month == monthValue) {
                records[recordCount] = allRecords[i];
                recordCount++;
                break;
            }
        }
    }

    return true;
}

bool EnergyHistoryClass::writeDayRecordsBatched(const TargetType targetType, const uint64_t serial, const uint16_t year, const DayRecord* records, const uint16_t recordCount)
{
    if (records == nullptr || recordCount == 0) {
        return false;
    }

    bool ok = true;
    for (uint16_t offset = 0; offset < recordCount; offset += MaxRecordsPerAppendBlock) {
        const uint16_t remaining = recordCount - offset;
        const uint16_t chunkCount = remaining > MaxRecordsPerAppendBlock ? MaxRecordsPerAppendBlock : remaining;
        ok = writeDay(targetType, serial, year, records + offset, chunkCount, records[offset].dayOfYear) && ok;
    }

    return ok;
}

bool EnergyHistoryClass::writeMonthRecordsBatched(const TargetType targetType, const uint64_t serial, const uint16_t year, const MonthRecord* records, const uint16_t recordCount)
{
    if (records == nullptr || recordCount == 0) {
        return false;
    }

    bool ok = true;
    for (uint16_t offset = 0; offset < recordCount; offset += MaxRecordsPerAppendBlock) {
        const uint16_t remaining = recordCount - offset;
        const uint16_t chunkCount = remaining > MaxRecordsPerAppendBlock ? MaxRecordsPerAppendBlock : remaining;
        ok = writeMonth(targetType, serial, year, records + offset, chunkCount, records[offset].month) && ok;
    }

    return ok;
}

bool EnergyHistoryClass::buildDayRecordsFromFiveMinuteMonth(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, const uint16_t* requestedDaysOfYear, const uint16_t requestedDayCount, DayRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    result = ScanResult();
    recordCount = 0;
    if (requestedDaysOfYear == nullptr
            || requestedDayCount == 0
            || records == nullptr
            || recordCapacity == 0
            || month < 1
            || month > 12) {
        return false;
    }

    const uint16_t firstDay = firstDayOfYearForMonth(year, month);
    const uint8_t monthDays = daysInMonth(year, month);
    if (firstDay == 0 || monthDays == 0) {
        return false;
    }

    int8_t compactIndexByDay[31];
    std::memset(compactIndexByDay, -1, sizeof(compactIndexByDay));
    uint8_t dayIndexByCompact[31] = {};
    uint8_t compactDayCount = 0;
    for (uint16_t i = 0; i < requestedDayCount; i++) {
        const uint16_t dayOfYearValue = requestedDaysOfYear[i];
        if (dayOfYearValue < firstDay || dayOfYearValue >= firstDay + monthDays) {
            continue;
        }

        const uint8_t dayIndex = static_cast<uint8_t>(dayOfYearValue - firstDay);
        if (compactIndexByDay[dayIndex] >= 0) {
            continue;
        }

        compactIndexByDay[dayIndex] = static_cast<int8_t>(compactDayCount);
        dayIndexByCompact[compactDayCount] = dayIndex;
        compactDayCount++;
    }

    if (compactDayCount == 0) {
        return true;
    }

    FileHeader header;
    if (!makeFileHeader(FileType::FiveMinute, targetType, serial, year, month, header)) {
        return false;
    }

    const String path = makeFiveMinutePath(targetType, serial, year, month);
    if (path.isEmpty()) {
        return false;
    }

    File file = LittleFS.open(path.c_str(), "r", false);
    if (!file || file.size() < FileHeaderSize || !readValidatedFileHeader(file, header)) {
        return false;
    }

    const size_t slotCount = static_cast<size_t>(compactDayCount) * FiveMinuteSlotsPerDay;
    std::unique_ptr<uint32_t[]> yieldBySlot(new (std::nothrow) uint32_t[slotCount]());
    std::unique_ptr<uint8_t[]> flagsBySlot(new (std::nothrow) uint8_t[slotCount]());
    if (!yieldBySlot || !flagsBySlot) {
        return false;
    }

    result.filesScanned++;
    result.fileSize = file.size();
    result.lastValidOffset = FileHeaderSize;

    while (file.available() > 0) {
        const size_t remaining = result.fileSize - file.position();
        if (remaining < BlockHeaderSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        uint8_t encodedBlockHeader[BlockHeaderSize];
        if (!readFull(file, encodedBlockHeader, sizeof(encodedBlockHeader))) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        BlockHeader blockHeader;
        if (!decodeBlockHeader(encodedBlockHeader, sizeof(encodedBlockHeader), blockHeader)
                || blockHeader.recordCount == 0
                || blockHeader.payloadSize == 0
                || blockHeader.payloadSize != blockHeader.recordCount * FiveMinuteRecordSize
                || result.fileSize - file.position() < blockHeader.payloadSize) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            break;
        }

        const size_t payloadStart = file.position();
        if (blockHeader.startKey < 1
                || blockHeader.startKey > monthDays
                || compactIndexByDay[blockHeader.startKey - 1] < 0) {
            if (!file.seek(payloadStart + blockHeader.payloadSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            result.lastValidOffset = file.position();
            continue;
        }

        uint32_t crc = 0xffffffff;
        uint16_t bytesRemaining = blockHeader.payloadSize;
        uint8_t buffer[FileReadBufferSize];
        while (bytesRemaining > 0) {
            const size_t chunkSize = bytesRemaining < sizeof(buffer) ? bytesRemaining : sizeof(buffer);
            if (!readFull(file, buffer, chunkSize)) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            crc = updateCrc32(crc, buffer, chunkSize);
            bytesRemaining -= chunkSize;
        }

        if (blockHeader.crc32Payload != finalizeCrc32(crc)) {
            result.skippedBlocks++;
            if (file.position() == result.fileSize) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
            continue;
        }

        if (!file.seek(payloadStart)) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            return true;
        }

        for (uint16_t i = 0; i < blockHeader.recordCount; i++) {
            uint8_t encodedRecord[FiveMinuteRecordSize];
            if (!readFull(file, encodedRecord, sizeof(encodedRecord))) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            FiveMinuteRecord sample;
            if (!decodeFiveMinuteRecord(encodedRecord, sizeof(encodedRecord), sample)) {
                result.skippedRecords++;
                continue;
            }

            result.validRecords++;
            if ((sample.flags & RecordFlagValid) == 0 || sample.day < 1 || sample.day > monthDays) {
                continue;
            }

            const int8_t compactIndex = compactIndexByDay[sample.day - 1];
            if (compactIndex < 0) {
                continue;
            }

            const size_t slotIndex = static_cast<size_t>(compactIndex) * FiveMinuteSlotsPerDay + sample.slot;
            yieldBySlot[slotIndex] = sample.yieldDayWh;
            flagsBySlot[slotIndex] = sample.flags;
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    for (uint8_t compactIndex = 0; compactIndex < compactDayCount; compactIndex++) {
        if (recordCount >= recordCapacity) {
            result.skippedRecords++;
            break;
        }

        const uint8_t dayIndex = dayIndexByCompact[compactIndex];
        const uint16_t dayOfYearValue = static_cast<uint16_t>(firstDay + dayIndex);
        uint32_t maxYieldWh = 0;
        uint32_t maxPowerW = 0;
        uint32_t runtimeMin = 0;
        uint16_t sampleCount = 0;
        uint16_t previousSlot = 0;
        uint32_t previousYieldWh = 0;
        bool previousValid = false;
        DayRecord record;
        std::memset(&record, 0, sizeof(record));
        record.dayOfYear = dayOfYearValue;

        for (uint16_t slot = 0; slot < FiveMinuteSlotsPerDay; slot++) {
            const size_t slotIndex = static_cast<size_t>(compactIndex) * FiveMinuteSlotsPerDay + slot;
            const uint8_t flags = flagsBySlot[slotIndex];
            if ((flags & RecordFlagValid) == 0) {
                continue;
            }

            const uint32_t yieldWh = yieldBySlot[slotIndex];
            sampleCount++;
            if ((flags & RecordFlagReachable) != 0) {
                record.flags |= RecordFlagReachable;
            }
            if ((flags & RecordFlagProducing) != 0) {
                record.flags |= RecordFlagProducing;
                runtimeMin += FiveMinuteIntervalSec / 60;
            }
            if ((flags & RecordFlagEstimated) != 0) {
                record.flags |= RecordFlagEstimated;
            }

            if (yieldWh > maxYieldWh) {
                maxYieldWh = yieldWh;
            }

            if (previousValid) {
                if (yieldWh < previousYieldWh) {
                    record.flags |= RecordFlagDayResetDetected;
                } else if (slot > previousSlot && yieldWh > previousYieldWh) {
                    const uint32_t deltaWh = yieldWh - previousYieldWh;
                    const uint32_t deltaSec = static_cast<uint32_t>(slot - previousSlot) * FiveMinuteIntervalSec;
                    const uint32_t powerW = (deltaWh * 3600U + deltaSec / 2U) / deltaSec;
                    if (powerW > maxPowerW) {
                        maxPowerW = powerW;
                    }
                }
            }

            previousSlot = slot;
            previousYieldWh = yieldWh;
            previousValid = true;
        }

        if (sampleCount == 0) {
            continue;
        }

        record.yieldWh = maxYieldWh;
        record.maxPowerW = saturateUint16(maxPowerW);
        record.runtimeMin = saturateUint16(runtimeMin);
        record.sampleCount = sampleCount;
        record.avgPowerW = averagePowerW(record.yieldWh, record.runtimeMin);
        record.flags |= RecordFlagValid;
        records[recordCount] = record;
        recordCount++;
    }

    return true;
}

bool EnergyHistoryClass::buildDayRecordFromFiveMinute(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, const uint8_t day, DayRecord& record)
{
    const uint16_t doy = dayOfYear(year, month, day);
    if (doy == 0) {
        return false;
    }

    FileHeader header;
    if (!makeFileHeader(FileType::FiveMinute, targetType, serial, year, month, header)) {
        return false;
    }

    const String path = makeFiveMinutePath(targetType, serial, year, month);
    if (path.isEmpty()) {
        return false;
    }

    File file = LittleFS.open(path.c_str(), "r", false);
    if (!file || file.size() < FileHeaderSize || !readValidatedFileHeader(file, header)) {
        return false;
    }

    const size_t fileSize = file.size();
    std::unique_ptr<bool[]> slotsPresent(new (std::nothrow) bool[FiveMinuteSlotsPerDay]());
    std::unique_ptr<FiveMinuteRecord[]> slots(new (std::nothrow) FiveMinuteRecord[FiveMinuteSlotsPerDay]());
    if (!slotsPresent || !slots) {
        return false;
    }

    while (file.available() > 0) {
        const size_t remaining = fileSize - file.position();
        if (remaining < BlockHeaderSize) {
            break;
        }

        uint8_t encodedBlockHeader[BlockHeaderSize];
        if (!readFull(file, encodedBlockHeader, sizeof(encodedBlockHeader))) {
            break;
        }

        BlockHeader blockHeader;
        if (!decodeBlockHeader(encodedBlockHeader, sizeof(encodedBlockHeader), blockHeader)
                || blockHeader.recordCount == 0
                || blockHeader.payloadSize == 0
                || blockHeader.payloadSize != blockHeader.recordCount * FiveMinuteRecordSize
                || fileSize - file.position() < blockHeader.payloadSize) {
            break;
        }

        const size_t payloadStart = file.position();
        uint32_t crc = 0xffffffff;
        uint16_t bytesRemaining = blockHeader.payloadSize;
        uint8_t buffer[FileReadBufferSize];
        while (bytesRemaining > 0) {
            const size_t chunkSize = bytesRemaining < sizeof(buffer) ? bytesRemaining : sizeof(buffer);
            if (!readFull(file, buffer, chunkSize)) {
                return false;
            }

            crc = updateCrc32(crc, buffer, chunkSize);
            bytesRemaining -= chunkSize;
        }

        if (blockHeader.crc32Payload != finalizeCrc32(crc)) {
            continue;
        }

        if (!file.seek(payloadStart)) {
            return false;
        }

        for (uint16_t i = 0; i < blockHeader.recordCount; i++) {
            uint8_t encodedRecord[FiveMinuteRecordSize];
            if (!readFull(file, encodedRecord, sizeof(encodedRecord))) {
                return false;
            }

            FiveMinuteRecord sample;
            if (!decodeFiveMinuteRecord(encodedRecord, sizeof(encodedRecord), sample)
                    || sample.day != day
                    || (sample.flags & RecordFlagValid) == 0) {
                continue;
            }

            slots[sample.slot] = sample;
            slotsPresent[sample.slot] = true;
        }
    }

    std::memset(&record, 0, sizeof(record));
    record.dayOfYear = doy;
    uint32_t maxYieldWh = 0;
    uint32_t maxPowerW = 0;
    uint32_t runtimeMin = 0;
    uint16_t sampleCount = 0;
    uint16_t previousSlot = 0;
    uint32_t previousYieldWh = 0;
    bool previousValid = false;

    for (uint16_t slot = 0; slot < FiveMinuteSlotsPerDay; slot++) {
        if (!slotsPresent[slot]) {
            continue;
        }

        const FiveMinuteRecord& sample = slots[slot];
        sampleCount++;
        if ((sample.flags & RecordFlagReachable) != 0) {
            record.flags |= RecordFlagReachable;
        }
        if ((sample.flags & RecordFlagProducing) != 0) {
            record.flags |= RecordFlagProducing;
            runtimeMin += FiveMinuteIntervalSec / 60;
        }
        if ((sample.flags & RecordFlagEstimated) != 0) {
            record.flags |= RecordFlagEstimated;
        }

        if (sample.yieldDayWh > maxYieldWh) {
            maxYieldWh = sample.yieldDayWh;
        }

        if (previousValid) {
            if (sample.yieldDayWh < previousYieldWh) {
                record.flags |= RecordFlagDayResetDetected;
            } else if (slot > previousSlot && sample.yieldDayWh > previousYieldWh) {
                const uint32_t deltaWh = sample.yieldDayWh - previousYieldWh;
                const uint32_t deltaSec = static_cast<uint32_t>(slot - previousSlot) * FiveMinuteIntervalSec;
                const uint32_t powerW = (deltaWh * 3600U + deltaSec / 2U) / deltaSec;
                if (powerW > maxPowerW) {
                    maxPowerW = powerW;
                }
            }
        }

        previousSlot = slot;
        previousYieldWh = sample.yieldDayWh;
        previousValid = true;
    }

    if (sampleCount == 0) {
        return false;
    }

    record.yieldWh = maxYieldWh;
    record.maxPowerW = saturateUint16(maxPowerW);
    record.runtimeMin = saturateUint16(runtimeMin);
    record.sampleCount = sampleCount;
    record.avgPowerW = averagePowerW(record.yieldWh, record.runtimeMin);
    record.flags |= RecordFlagValid;
    return true;
}

bool EnergyHistoryClass::buildMonthRecordFromDay(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, MonthRecord& record)
{
    FileHeader header;
    if (!makeFileHeader(FileType::Day, targetType, serial, year, 0, header)) {
        return false;
    }

    const String path = makeDayPath(targetType, serial, year);
    if (path.isEmpty()) {
        return false;
    }

    std::unique_ptr<DayRecord[]> days(new (std::nothrow) DayRecord[366]());
    if (!days) {
        return false;
    }

    uint16_t recordCount = 0;
    ScanResult scan;
    if (!readDayFile(path.c_str(), header, days.get(), 366, recordCount, scan)) {
        return false;
    }

    return buildMonthRecordFromDayRecords(year, month, days.get(), recordCount, record);
}

bool EnergyHistoryClass::buildMonthRecordFromDayRecords(const uint16_t year, const uint8_t month, const DayRecord* days, const uint16_t dayRecordCount, MonthRecord& record)
{
    const uint16_t firstDay = firstDayOfYearForMonth(year, month);
    const uint8_t monthDays = daysInMonth(year, month);
    if (days == nullptr || firstDay == 0 || monthDays == 0) {
        return false;
    }

    std::memset(&record, 0, sizeof(record));
    record.month = month;
    uint32_t yieldWh = 0;
    uint32_t runtimeMin = 0;
    uint16_t maxPowerW = 0;
    uint16_t dayCount = 0;

    for (uint16_t i = 0; i < dayRecordCount; i++) {
        if (days[i].dayOfYear < firstDay
                || days[i].dayOfYear >= firstDay + monthDays
                || (days[i].flags & RecordFlagValid) == 0) {
            continue;
        }

        dayCount++;
        yieldWh += days[i].yieldWh;
        runtimeMin += days[i].runtimeMin;
        if (days[i].maxPowerW > maxPowerW) {
            maxPowerW = days[i].maxPowerW;
        }

        record.flags |= days[i].flags & KnownRecordFlagsMask;
    }

    if (dayCount == 0) {
        return false;
    }

    record.yieldWh = yieldWh;
    record.maxPowerW = maxPowerW;
    record.runtimeMin = saturateUint16(runtimeMin);
    record.dayCount = dayCount;
    record.avgPowerW = averagePowerW(record.yieldWh, record.runtimeMin);
    record.flags |= RecordFlagValid;
    return true;
}

bool EnergyHistoryClass::finalizeCompletedPeriod(const TargetType targetType, const uint64_t serial, const uint16_t year, const uint8_t month, const uint8_t day, const bool finalizeMonth)
{
    DayRecord dayRecord;
    if (!buildDayRecordFromFiveMinute(targetType, serial, year, month, day, dayRecord)) {
        return false;
    }

    bool ok = writeDay(targetType, serial, year, &dayRecord, 1, dayRecord.dayOfYear);

    if (finalizeMonth) {
        MonthRecord monthRecord;
        ok = buildMonthRecordFromDay(targetType, serial, year, month, monthRecord)
                && writeMonth(targetType, serial, year, &monthRecord, 1, month)
                && ok;
    }

    return ok;
}
