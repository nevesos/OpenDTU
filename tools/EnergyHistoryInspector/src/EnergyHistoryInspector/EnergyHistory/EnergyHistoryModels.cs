using System.Collections.ObjectModel;

namespace EnergyHistoryInspector.EnergyHistory;

public enum EnergyHistoryFileType : byte
{
    FiveMinute = 1,
    Day = 2,
    Month = 3,
}

public enum EnergyHistoryTargetType : byte
{
    Total = 0,
    Inverter = 1,
}

[Flags]
public enum EnergyHistoryRecordFlags : ushort
{
    Valid = 1 << 0,
    Reachable = 1 << 1,
    Producing = 1 << 2,
    DayResetDetected = 1 << 3,
    Estimated = 1 << 4,
}

public sealed record EnergyHistoryFileHeader(
    EnergyHistoryFileType FileType,
    byte Version,
    byte HeaderSize,
    byte RecordSize,
    ushort Year,
    byte Month,
    EnergyHistoryTargetType TargetType,
    ulong Serial,
    ushort IntervalSec,
    uint HeaderCrc);

public sealed record EnergyHistoryBlockHeader(
    ushort BlockIndex,
    ushort StartKey,
    ushort RecordCount,
    ushort PayloadSize,
    uint PayloadCrc,
    long Offset);

public sealed class EnergyHistoryRecord
{
    public int Number { get; init; }
    public string Key { get; set; } = string.Empty;
    public byte Day { get; set; }
    public ushort Slot { get; set; }
    public ushort DayOfYear { get; set; }
    public byte Month { get; set; }
    public uint YieldWh { get; set; }
    public ushort MaxPowerW { get; set; }
    public ushort AvgPowerW { get; set; }
    public ushort RuntimeMin { get; set; }
    public ushort Count { get; set; }
    public EnergyHistoryRecordFlags Flags { get; set; }
    public bool IsValid { get; set; }
    public string Raw { get; set; } = string.Empty;
}

public sealed class EnergyHistoryBlock
{
    public required EnergyHistoryBlockHeader Header { get; init; }
    public bool IsValid { get; init; }
    public bool IsFinalInvalidBlock { get; init; }
    public string Message { get; init; } = string.Empty;
    public ObservableCollection<EnergyHistoryRecord> Records { get; } = [];
}

public sealed class EnergyHistoryScanResult
{
    public string FilePath { get; init; } = string.Empty;
    public long FileSize { get; init; }
    public long LastValidOffset { get; set; }
    public uint FilesScanned { get; set; }
    public uint ValidBlocks { get; set; }
    public uint SkippedBlocks { get; set; }
    public uint ValidRecords { get; set; }
    public uint SkippedRecords { get; set; }
    public bool InvalidFinalBlock { get; set; }
    public bool CanTruncateFinalBlock { get; set; }
}

public sealed class EnergyHistoryFile
{
    public required EnergyHistoryFileHeader Header { get; init; }
    public required EnergyHistoryScanResult Scan { get; init; }
    public ObservableCollection<EnergyHistoryBlock> Blocks { get; } = [];
    public ObservableCollection<EnergyHistoryRecord> Records { get; } = [];
}
