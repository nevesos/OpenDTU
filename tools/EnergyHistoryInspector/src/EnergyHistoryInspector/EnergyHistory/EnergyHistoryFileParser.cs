using System.Buffers.Binary;
using System.Globalization;
using System.IO;
using System.Text;

namespace EnergyHistoryInspector.EnergyHistory;

public sealed class EnergyHistoryFileParser
{
    private static readonly byte[] FileMagic = Encoding.ASCII.GetBytes("EH01");
    private static readonly byte[] BlockMagic = Encoding.ASCII.GetBytes("EHB1");
    private const byte Version = 1;
    private const byte FileHeaderSize = 32;
    private const byte BlockHeaderSize = 16;
    private const byte FiveMinuteRecordSize = 8;
    private const byte DayRecordSize = 16;
    private const byte MonthRecordSize = 16;
    private const ushort FiveMinuteIntervalSec = 5 * 60;
    private const ushort FiveMinuteSlotsPerDay = 24 * 60 / 5;
    private const ushort KnownRecordFlagsMask = 0x001f;

    public EnergyHistoryFile Parse(string filePath)
    {
        using var stream = File.Open(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        if (stream.Length < FileHeaderSize)
        {
            throw new InvalidDataException("Die Datei ist kleiner als der Energy-History-Header.");
        }

        Span<byte> headerBytes = stackalloc byte[FileHeaderSize];
        ReadExactly(stream, headerBytes);

        var header = DecodeFileHeader(headerBytes);
        var scan = new EnergyHistoryScanResult
        {
            FilePath = filePath,
            FileSize = stream.Length,
            LastValidOffset = FileHeaderSize,
            FilesScanned = 1,
        };

        var file = new EnergyHistoryFile
        {
            Header = header,
            Scan = scan,
        };

        while (stream.Position < stream.Length)
        {
            var blockOffset = stream.Position;
            var remaining = stream.Length - stream.Position;
            if (remaining < BlockHeaderSize)
            {
                AddInvalidFinalBlock(file, blockOffset, "Unvollstaendiger Blockheader.");
                break;
            }

            var blockHeaderBytes = new byte[BlockHeaderSize];
            ReadExactly(stream, blockHeaderBytes);

            if (!TryDecodeBlockHeader(blockHeaderBytes, blockOffset, out var blockHeader))
            {
                AddInvalidFinalBlock(file, blockOffset, "Ungueltige Block-Magic.");
                break;
            }

            if (blockHeader.RecordCount == 0
                || blockHeader.PayloadSize == 0
                || blockHeader.PayloadSize != blockHeader.RecordCount * header.RecordSize)
            {
                AddInvalidFinalBlock(file, blockHeader, "Ungueltige Blockgroesse oder Record-Anzahl.");
                break;
            }

            if (stream.Length - stream.Position < blockHeader.PayloadSize)
            {
                AddInvalidFinalBlock(file, blockHeader, "Unvollstaendige Payload.");
                break;
            }

            var payload = new byte[blockHeader.PayloadSize];
            ReadExactly(stream, payload);
            var expectedCrc = EnergyHistoryCrc32.Calculate(payload);
            if (blockHeader.PayloadCrc != expectedCrc)
            {
                var isFinal = stream.Position == stream.Length;
                file.Blocks.Add(new EnergyHistoryBlock
                {
                    Header = blockHeader,
                    IsValid = false,
                    IsFinalInvalidBlock = isFinal,
                    Message = $"Payload-CRC ungueltig. Erwartet 0x{expectedCrc:x8}.",
                });
                scan.SkippedBlocks++;
                if (isFinal)
                {
                    scan.InvalidFinalBlock = true;
                    scan.CanTruncateFinalBlock = scan.LastValidOffset < scan.FileSize;
                }

                continue;
            }

            var block = new EnergyHistoryBlock
            {
                Header = blockHeader,
                IsValid = true,
                Message = "OK",
            };

            DecodeRecords(header, blockHeader, payload, block, file);
            file.Blocks.Add(block);
            scan.ValidBlocks++;
            scan.LastValidOffset = stream.Position;
        }

        return file;
    }

    private static EnergyHistoryFileHeader DecodeFileHeader(ReadOnlySpan<byte> input)
    {
        if (!input[..4].SequenceEqual(FileMagic))
        {
            throw new InvalidDataException("Ungueltige Datei-Magic. Erwartet EH01.");
        }

        var fileType = (EnergyHistoryFileType)input[4];
        var version = input[5];
        var headerSize = input[6];
        var recordSize = input[7];
        var year = BinaryPrimitives.ReadUInt16LittleEndian(input[8..10]);
        var month = input[10];
        var targetType = (EnergyHistoryTargetType)input[11];
        var serial = BinaryPrimitives.ReadUInt64LittleEndian(input[12..20]);
        var intervalSec = BinaryPrimitives.ReadUInt16LittleEndian(input[20..22]);
        var headerCrc = BinaryPrimitives.ReadUInt32LittleEndian(input[28..32]);

        if (version != Version || headerSize != FileHeaderSize)
        {
            throw new InvalidDataException("Ungueltige Header-Version oder Header-Groesse.");
        }

        if (!Enum.IsDefined(fileType) || !Enum.IsDefined(targetType))
        {
            throw new InvalidDataException("Ungueltiger Datei- oder Target-Typ.");
        }

        if (recordSize != ExpectedRecordSize(fileType))
        {
            throw new InvalidDataException("Die Record-Groesse passt nicht zum Datei-Typ.");
        }

        if (!input[22..28].SequenceEqual(stackalloc byte[6]))
        {
            throw new InvalidDataException("Reservierte Header-Bytes sind nicht leer.");
        }

        if (fileType == EnergyHistoryFileType.FiveMinute)
        {
            if (month < 1 || month > 12 || intervalSec != FiveMinuteIntervalSec)
            {
                throw new InvalidDataException("Ungueltige 5m-Headerwerte.");
            }
        }
        else if (month != 0 || intervalSec != 0)
        {
            throw new InvalidDataException("Ungueltige Tages-/Monats-Headerwerte.");
        }

        if (targetType == EnergyHistoryTargetType.Total && serial != 0)
        {
            throw new InvalidDataException("Total-Dateien duerfen keine Seriennummer tragen.");
        }

        if (targetType == EnergyHistoryTargetType.Inverter && serial == 0)
        {
            throw new InvalidDataException("Inverter-Dateien benoetigen eine Seriennummer.");
        }

        var expectedCrc = EnergyHistoryCrc32.Calculate(input[..28]);
        if (headerCrc != expectedCrc)
        {
            throw new InvalidDataException($"Header-CRC ungueltig. Erwartet 0x{expectedCrc:x8}.");
        }

        return new EnergyHistoryFileHeader(fileType, version, headerSize, recordSize, year, month, targetType, serial, intervalSec, headerCrc);
    }

    private static bool TryDecodeBlockHeader(ReadOnlySpan<byte> input, long offset, out EnergyHistoryBlockHeader header)
    {
        header = new EnergyHistoryBlockHeader(0, 0, 0, 0, 0, offset);
        if (!input[..4].SequenceEqual(BlockMagic))
        {
            return false;
        }

        header = new EnergyHistoryBlockHeader(
            BinaryPrimitives.ReadUInt16LittleEndian(input[4..6]),
            BinaryPrimitives.ReadUInt16LittleEndian(input[6..8]),
            BinaryPrimitives.ReadUInt16LittleEndian(input[8..10]),
            BinaryPrimitives.ReadUInt16LittleEndian(input[10..12]),
            BinaryPrimitives.ReadUInt32LittleEndian(input[12..16]),
            offset);
        return true;
    }

    private static void DecodeRecords(EnergyHistoryFileHeader fileHeader, EnergyHistoryBlockHeader blockHeader, byte[] payload, EnergyHistoryBlock block, EnergyHistoryFile file)
    {
        for (var index = 0; index < blockHeader.RecordCount; index++)
        {
            var raw = payload.AsSpan(index * fileHeader.RecordSize, fileHeader.RecordSize);
            var record = fileHeader.FileType switch
            {
                EnergyHistoryFileType.FiveMinute => DecodeFiveMinuteRecord(index + 1, raw),
                EnergyHistoryFileType.Day => DecodeDayRecord(index + 1, raw),
                EnergyHistoryFileType.Month => DecodeMonthRecord(index + 1, raw),
                _ => throw new InvalidDataException("Unbekannter Datei-Typ."),
            };

            block.Records.Add(record);
            if (record.IsValid)
            {
                file.Scan.ValidRecords++;
                file.Records.Add(record);
            }
            else
            {
                file.Scan.SkippedRecords++;
            }
        }
    }

    private static EnergyHistoryRecord DecodeFiveMinuteRecord(int number, ReadOnlySpan<byte> input)
    {
        var day = input[0];
        var slot = BinaryPrimitives.ReadUInt16LittleEndian(input[1..3]);
        var yieldDayWh = BinaryPrimitives.ReadUInt32LittleEndian(input[3..7]);
        var flags = input[7];
        var isValid = day is >= 1 and <= 31
            && slot < FiveMinuteSlotsPerDay
            && (flags & ~KnownRecordFlagsMask) == 0;

        return new EnergyHistoryRecord
        {
            Number = number,
            Key = $"Tag {day}, Slot {slot}",
            Day = day,
            Slot = slot,
            YieldWh = yieldDayWh,
            Flags = (EnergyHistoryRecordFlags)flags,
            IsValid = isValid,
            Raw = ToHex(input),
        };
    }

    private static EnergyHistoryRecord DecodeDayRecord(int number, ReadOnlySpan<byte> input)
    {
        var dayOfYear = BinaryPrimitives.ReadUInt16LittleEndian(input[0..2]);
        var yieldWh = BinaryPrimitives.ReadUInt32LittleEndian(input[2..6]);
        var maxPowerW = BinaryPrimitives.ReadUInt16LittleEndian(input[6..8]);
        var avgPowerW = BinaryPrimitives.ReadUInt16LittleEndian(input[8..10]);
        var runtimeMin = BinaryPrimitives.ReadUInt16LittleEndian(input[10..12]);
        var sampleCount = BinaryPrimitives.ReadUInt16LittleEndian(input[12..14]);
        var flags = BinaryPrimitives.ReadUInt16LittleEndian(input[14..16]);
        var isValid = dayOfYear is >= 1 and <= 366
            && (flags & 0xff00) == 0
            && (flags & ~KnownRecordFlagsMask) == 0;

        return new EnergyHistoryRecord
        {
            Number = number,
            Key = $"Tag {dayOfYear}",
            DayOfYear = dayOfYear,
            YieldWh = yieldWh,
            MaxPowerW = maxPowerW,
            AvgPowerW = avgPowerW,
            RuntimeMin = runtimeMin,
            Count = sampleCount,
            Flags = (EnergyHistoryRecordFlags)flags,
            IsValid = isValid,
            Raw = ToHex(input),
        };
    }

    private static EnergyHistoryRecord DecodeMonthRecord(int number, ReadOnlySpan<byte> input)
    {
        var month = input[0];
        var reserved0 = input[1];
        var yieldWh = BinaryPrimitives.ReadUInt32LittleEndian(input[2..6]);
        var maxPowerW = BinaryPrimitives.ReadUInt16LittleEndian(input[6..8]);
        var avgPowerW = BinaryPrimitives.ReadUInt16LittleEndian(input[8..10]);
        var runtimeMin = BinaryPrimitives.ReadUInt16LittleEndian(input[10..12]);
        var dayCount = BinaryPrimitives.ReadUInt16LittleEndian(input[12..14]);
        var flags = BinaryPrimitives.ReadUInt16LittleEndian(input[14..16]);
        var isValid = month is >= 1 and <= 12
            && reserved0 == 0
            && (flags & 0xff00) == 0
            && (flags & ~KnownRecordFlagsMask) == 0;

        return new EnergyHistoryRecord
        {
            Number = number,
            Key = $"Monat {month}",
            Month = month,
            YieldWh = yieldWh,
            MaxPowerW = maxPowerW,
            AvgPowerW = avgPowerW,
            RuntimeMin = runtimeMin,
            Count = dayCount,
            Flags = (EnergyHistoryRecordFlags)flags,
            IsValid = isValid,
            Raw = ToHex(input),
        };
    }

    private static void AddInvalidFinalBlock(EnergyHistoryFile file, long blockOffset, string message)
    {
        file.Blocks.Add(new EnergyHistoryBlock
        {
            Header = new EnergyHistoryBlockHeader(0, 0, 0, 0, 0, blockOffset),
            IsValid = false,
            IsFinalInvalidBlock = true,
            Message = message,
        });
        file.Scan.SkippedBlocks++;
        file.Scan.InvalidFinalBlock = true;
        file.Scan.CanTruncateFinalBlock = file.Scan.LastValidOffset < file.Scan.FileSize;
    }

    private static void AddInvalidFinalBlock(EnergyHistoryFile file, EnergyHistoryBlockHeader header, string message)
    {
        file.Blocks.Add(new EnergyHistoryBlock
        {
            Header = header,
            IsValid = false,
            IsFinalInvalidBlock = true,
            Message = message,
        });
        file.Scan.SkippedBlocks++;
        file.Scan.InvalidFinalBlock = true;
        file.Scan.CanTruncateFinalBlock = file.Scan.LastValidOffset < file.Scan.FileSize;
    }

    private static byte ExpectedRecordSize(EnergyHistoryFileType fileType)
    {
        return fileType switch
        {
            EnergyHistoryFileType.FiveMinute => FiveMinuteRecordSize,
            EnergyHistoryFileType.Day => DayRecordSize,
            EnergyHistoryFileType.Month => MonthRecordSize,
            _ => 0,
        };
    }

    private static void ReadExactly(Stream stream, Span<byte> buffer)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = stream.Read(buffer[offset..]);
            if (read == 0)
            {
                throw new EndOfStreamException("Unerwartetes Dateiende.");
            }

            offset += read;
        }
    }

    private static string ToHex(ReadOnlySpan<byte> bytes)
    {
        return string.Join(" ", bytes.ToArray().Select(value => value.ToString("x2", CultureInfo.InvariantCulture)));
    }
}
