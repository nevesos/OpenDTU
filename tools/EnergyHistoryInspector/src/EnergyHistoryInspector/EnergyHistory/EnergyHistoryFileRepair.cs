using System.IO;

namespace EnergyHistoryInspector.EnergyHistory;

public static class EnergyHistoryFileRepair
{
    public static string TruncateFinalInvalidBlock(string filePath, long lastValidOffset)
    {
        if (lastValidOffset < 32)
        {
            throw new InvalidOperationException("Der letzte gueltige Offset liegt vor dem Dateiheader.");
        }

        var backupPath = CreateBackupPath(filePath);
        File.Copy(filePath, backupPath, overwrite: false);

        using var stream = File.Open(filePath, FileMode.Open, FileAccess.Write, FileShare.None);
        if (lastValidOffset > stream.Length)
        {
            throw new InvalidOperationException("Der letzte gueltige Offset liegt hinter dem Dateiende.");
        }

        stream.SetLength(lastValidOffset);
        return backupPath;
    }

    private static string CreateBackupPath(string filePath)
    {
        var directory = Path.GetDirectoryName(filePath) ?? string.Empty;
        var fileName = Path.GetFileName(filePath);
        var timestamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
        return Path.Combine(directory, $"{fileName}.{timestamp}.bak");
    }
}
