using System.Buffers.Binary;
using System.IO;
using System.Text;

namespace EnergyHistoryInspector.EnergyHistory;

public sealed class EnergyHistoryFileWriter
{
    private static readonly byte[] FileMagic = Encoding.ASCII.GetBytes("EH01");
    private static readonly byte[] BlockMagic = Encoding.ASCII.GetBytes("EHB1");
    private const byte FileHeaderSize = 32;
    private const byte BlockHeaderSize = 16;
    private const ushort FiveMinuteSlotsPerDay = 24 * 60 / 5;
    private const ushort KnownRecordFlagsMask = 0x001f;

    public string Save(EnergyHistoryFile file)
    {
        if (file.Scan.SkippedBlocks != 0 || file.Scan.InvalidFinalBlock || file.Scan.CanTruncateFinalBlock)
        {
            throw new InvalidOperationException("Die Datei enthaelt uebersprungene oder defekte Bloecke. Bitte zuerst reparieren oder erneut laden.");
        }

        ValidateRecords(file);

        var backupPath = CreateBackupPath(file.Scan.FilePath);
        File.Copy(file.Scan.FilePath, backupPath, overwrite: false);

        var tempPath = $"{file.Scan.FilePath}.tmp";
        try
        {
            using (var stream = File.Open(tempPath, FileMode.Create, FileAccess.Write, FileShare.None))
            {
                WriteFileHeader(stream, file.Header);
                foreach (var block in file.Blocks.Where(block => block.IsValid))
                {
                    WriteBlock(stream, file.Header, block);
                }
            }

            File.Copy(tempPath, file.Scan.FilePath, overwrite: true);
            File.Delete(tempPath);
            return backupPath;
        }
        catch
        {
            if (File.Exists(tempPath))
            {
                File.Delete(tempPath);
            }

            throw;
        }
    }

    private static void WriteFileHeader(Stream stream, EnergyHistoryFileHeader header)
    {
        Span<byte> buffer = stackalloc byte[FileHeaderSize];
        FileMagic.CopyTo(buffer);
        buffer[4] = (byte)header.FileType;
        buffer[5] = header.Version;
        buffer[6] = header.HeaderSize;
        buffer[7] = header.RecordSize;
        BinaryPrimitives.WriteUInt16LittleEndian(buffer[8..10], header.Year);
        buffer[10] = header.Month;
        buffer[11] = (byte)header.TargetType;
        BinaryPrimitives.WriteUInt64LittleEndian(buffer[12..20], header.Serial);
        BinaryPrimitives.WriteUInt16LittleEndian(buffer[20..22], header.IntervalSec);
        var crc = EnergyHistoryCrc32.Calculate(buffer[..28]);
        BinaryPrimitives.WriteUInt32LittleEndian(buffer[28..32], crc);
        stream.Write(buffer);
    }

    private static void WriteBlock(Stream stream, EnergyHistoryFileHeader header, EnergyHistoryBlock block)
    {
        var payload = EncodePayload(header, block);
        Span<byte> blockHeader = stackalloc byte[BlockHeaderSize];
        BlockMagic.CopyTo(blockHeader);
        BinaryPrimitives.WriteUInt16LittleEndian(blockHeader[4..6], block.Header.BlockIndex);
        BinaryPrimitives.WriteUInt16LittleEndian(blockHeader[6..8], StartKey(header.FileType, block));
        BinaryPrimitives.WriteUInt16LittleEndian(blockHeader[8..10], checked((ushort)block.Records.Count));
        BinaryPrimitives.WriteUInt16LittleEndian(blockHeader[10..12], checked((ushort)payload.Length));
        BinaryPrimitives.WriteUInt32LittleEndian(blockHeader[12..16], EnergyHistoryCrc32.Calculate(payload));
        stream.Write(blockHeader);
        stream.Write(payload);
    }

    private static byte[] EncodePayload(EnergyHistoryFileHeader header, EnergyHistoryBlock block)
    {
        var payload = new byte[checked(block.Records.Count * header.RecordSize)];
        for (var index = 0; index < block.Records.Count; index++)
        {
            var target = payload.AsSpan(index * header.RecordSize, header.RecordSize);
            var record = block.Records[index];
            switch (header.FileType)
            {
                case EnergyHistoryFileType.FiveMinute:
                    EncodeFiveMinuteRecord(record, target);
                    break;
                case EnergyHistoryFileType.Day:
                    EncodeDayRecord(record, target);
                    break;
                case EnergyHistoryFileType.Month:
                    EncodeMonthRecord(record, target);
                    break;
                default:
                    throw new InvalidDataException("Unbekannter Datei-Typ.");
            }
        }

        return payload;
    }

    private static void EncodeFiveMinuteRecord(EnergyHistoryRecord record, Span<byte> target)
    {
        target[0] = record.Day;
        BinaryPrimitives.WriteUInt16LittleEndian(target[1..3], record.Slot);
        BinaryPrimitives.WriteUInt32LittleEndian(target[3..7], record.YieldWh);
        target[7] = checked((byte)record.Flags);
    }

    private static void EncodeDayRecord(EnergyHistoryRecord record, Span<byte> target)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(target[0..2], record.DayOfYear);
        BinaryPrimitives.WriteUInt32LittleEndian(target[2..6], record.YieldWh);
        BinaryPrimitives.WriteUInt16LittleEndian(target[6..8], record.MaxPowerW);
        BinaryPrimitives.WriteUInt16LittleEndian(target[8..10], record.AvgPowerW);
        BinaryPrimitives.WriteUInt16LittleEndian(target[10..12], record.RuntimeMin);
        BinaryPrimitives.WriteUInt16LittleEndian(target[12..14], record.Count);
        BinaryPrimitives.WriteUInt16LittleEndian(target[14..16], (ushort)record.Flags);
    }

    private static void EncodeMonthRecord(EnergyHistoryRecord record, Span<byte> target)
    {
        target[0] = record.Month;
        target[1] = 0;
        BinaryPrimitives.WriteUInt32LittleEndian(target[2..6], record.YieldWh);
        BinaryPrimitives.WriteUInt16LittleEndian(target[6..8], record.MaxPowerW);
        BinaryPrimitives.WriteUInt16LittleEndian(target[8..10], record.AvgPowerW);
        BinaryPrimitives.WriteUInt16LittleEndian(target[10..12], record.RuntimeMin);
        BinaryPrimitives.WriteUInt16LittleEndian(target[12..14], record.Count);
        BinaryPrimitives.WriteUInt16LittleEndian(target[14..16], (ushort)record.Flags);
    }

    private static ushort StartKey(EnergyHistoryFileType fileType, EnergyHistoryBlock block)
    {
        var first = block.Records.FirstOrDefault()
            ?? throw new InvalidDataException("Ein Block ohne Records kann nicht geschrieben werden.");

        return fileType switch
        {
            EnergyHistoryFileType.FiveMinute => first.Day,
            EnergyHistoryFileType.Day => first.DayOfYear,
            EnergyHistoryFileType.Month => first.Month,
            _ => 0,
        };
    }

    private static void ValidateRecords(EnergyHistoryFile file)
    {
        foreach (var record in file.Blocks.Where(block => block.IsValid).SelectMany(block => block.Records))
        {
            var flags = (ushort)record.Flags;
            if ((flags & ~KnownRecordFlagsMask) != 0)
            {
                throw new InvalidDataException($"Record #{record.Number}: ungueltige Flags.");
            }

            switch (file.Header.FileType)
            {
                case EnergyHistoryFileType.FiveMinute:
                    if (record.Day is < 1 or > 31 || record.Slot >= FiveMinuteSlotsPerDay)
                    {
                        throw new InvalidDataException($"Record #{record.Number}: ungueltiger Tag oder Slot.");
                    }
                    break;
                case EnergyHistoryFileType.Day:
                    if (record.DayOfYear is < 1 or > 366)
                    {
                        throw new InvalidDataException($"Record #{record.Number}: ungueltiger Jahrestag.");
                    }
                    break;
                case EnergyHistoryFileType.Month:
                    if (record.Month is < 1 or > 12)
                    {
                        throw new InvalidDataException($"Record #{record.Number}: ungueltiger Monat.");
                    }
                    break;
            }
        }
    }

    private static string CreateBackupPath(string filePath)
    {
        var directory = Path.GetDirectoryName(filePath) ?? string.Empty;
        var fileName = Path.GetFileName(filePath);
        var timestamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
        return Path.Combine(directory, $"{fileName}.{timestamp}.write.bak");
    }
}
