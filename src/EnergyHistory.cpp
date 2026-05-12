// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 urdev
 */
#include "EnergyHistory.h"
#include <LittleFS.h>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

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
    if (LittleFS.exists(path)) {
        return true;
    }

    return LittleFS.mkdir(path);
}

bool ensureEnergyDirectories(const uint8_t fileType)
{
    const char* typeDirectory = directoryForFileType(fileType);
    if (typeDirectory == nullptr) {
        return false;
    }

    return ensureDirectory(EnergyDirectory) && ensureDirectory(typeDirectory);
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

    if (!LittleFS.exists(path)) {
        File file = LittleFS.open(path, "w");
        if (!file) {
            return false;
        }

        return writeFull(file, encodedHeader, sizeof(encodedHeader));
    }

    File file = LittleFS.open(path, "r", false);
    if (!file || file.size() < FileHeaderSize) {
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

bool skipBytes(File& file, const size_t length)
{
    uint8_t buffer[FileReadBufferSize];
    size_t remaining = length;

    while (remaining > 0) {
        const size_t chunkSize = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!readFull(file, buffer, chunkSize)) {
            return false;
        }

        remaining -= chunkSize;
    }

    return true;
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

} // namespace

EnergyHistoryClass EnergyHistory;

EnergyHistoryClass::EnergyHistoryClass()
    : _loopTask(5 * 60 * TASK_SECOND, TASK_FOREVER, std::bind(&EnergyHistoryClass::loop, this))
{
}

void EnergyHistoryClass::init(Scheduler& scheduler)
{
    scheduler.addTask(_loopTask);
    _loopTask.enable();
}

void EnergyHistoryClass::loop()
{
    // Persistence is intentionally not wired yet. See docs/EnergyHistory.md
    // for the on-flash format and retention rules.
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

bool EnergyHistoryClass::appendBlock(const char* path, const FileHeader& expectedHeader, const uint16_t blockIndex, const uint16_t startKey, const uint8_t* payload, const uint16_t payloadSize, const uint16_t recordCount)
{
    if (path == nullptr
            || payload == nullptr
            || payloadSize == 0
            || recordCount == 0
            || expectedHeader.recordSize == 0
            || payloadSize != recordCount * expectedHeader.recordSize
            || !ensureEnergyDirectories(expectedHeader.fileType)
            || !ensureFileHeader(path, expectedHeader)) {
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

bool EnergyHistoryClass::scanFile(const char* path, const FileHeader& expectedHeader, ScanResult& result)
{
    if (path == nullptr || !LittleFS.exists(path)) {
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
            if (!skipBytes(file, remaining - BlockHeaderSize)) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
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
            continue;
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    return true;
}

bool EnergyHistoryClass::scanFiveMinuteFile(const char* path, const FileHeader& expectedHeader, ScanResult& result)
{
    uint16_t recordCount = 0;
    return readFiveMinuteFile(path, expectedHeader, nullptr, 0, recordCount, result);
}

bool EnergyHistoryClass::readFiveMinuteFile(const char* path, const FileHeader& expectedHeader, FiveMinuteRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    if (path == nullptr
            || expectedHeader.fileType != static_cast<uint8_t>(FileType::FiveMinute)
            || expectedHeader.recordSize != FiveMinuteRecordSize
            || !LittleFS.exists(path)) {
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
                || blockHeader.payloadSize != blockHeader.recordCount * FiveMinuteRecordSize) {
            result.skippedBlocks++;
            if (!skipBytes(file, remaining - BlockHeaderSize)) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
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
            if (!upsertFiveMinuteRecord(records, recordCapacity, recordCount, record)) {
                result.skippedRecords++;
            }
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    return true;
}

bool EnergyHistoryClass::readDayFile(const char* path, const FileHeader& expectedHeader, DayRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    if (path == nullptr
            || expectedHeader.fileType != static_cast<uint8_t>(FileType::Day)
            || expectedHeader.recordSize != DayRecordSize
            || !LittleFS.exists(path)) {
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
                || blockHeader.payloadSize != blockHeader.recordCount * DayRecordSize) {
            result.skippedBlocks++;
            if (!skipBytes(file, remaining - BlockHeaderSize)) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
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
            continue;
        }

        if (!file.seek(payloadStart)) {
            result.skippedBlocks++;
            result.invalidFinalBlock = true;
            result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            return true;
        }

        for (uint16_t i = 0; i < blockHeader.recordCount; i++) {
            uint8_t encodedRecord[DayRecordSize];
            if (!readFull(file, encodedRecord, sizeof(encodedRecord))) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            DayRecord record;
            if (!decodeDayRecord(encodedRecord, sizeof(encodedRecord), record)) {
                result.skippedRecords++;
                continue;
            }

            result.validRecords++;
            if (!upsertDayRecord(records, recordCapacity, recordCount, record)) {
                result.skippedRecords++;
            }
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    return true;
}

bool EnergyHistoryClass::readMonthFile(const char* path, const FileHeader& expectedHeader, MonthRecord* records, const uint16_t recordCapacity, uint16_t& recordCount, ScanResult& result)
{
    if (path == nullptr
            || expectedHeader.fileType != static_cast<uint8_t>(FileType::Month)
            || expectedHeader.recordSize != MonthRecordSize
            || !LittleFS.exists(path)) {
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
                || blockHeader.payloadSize != blockHeader.recordCount * MonthRecordSize) {
            result.skippedBlocks++;
            if (!skipBytes(file, remaining - BlockHeaderSize)) {
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
            }
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
            if (!readFull(file, encodedRecord, sizeof(encodedRecord))) {
                result.skippedBlocks++;
                result.invalidFinalBlock = true;
                result.canTruncateFinalBlock = result.lastValidOffset < result.fileSize;
                return true;
            }

            MonthRecord record;
            if (!decodeMonthRecord(encodedRecord, sizeof(encodedRecord), record)) {
                result.skippedRecords++;
                continue;
            }

            result.validRecords++;
            if (!upsertMonthRecord(records, recordCapacity, recordCount, record)) {
                result.skippedRecords++;
            }
        }

        result.validBlocks++;
        result.lastValidOffset = file.position();
    }

    return true;
}
