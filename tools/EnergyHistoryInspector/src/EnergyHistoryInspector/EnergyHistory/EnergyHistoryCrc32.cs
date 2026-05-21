namespace EnergyHistoryInspector.EnergyHistory;

public static class EnergyHistoryCrc32
{
    private const uint Polynomial = 0xedb88320;

    public static uint Calculate(ReadOnlySpan<byte> data)
    {
        return Finalize(Update(0xffffffff, data));
    }

    public static uint Update(uint crc, ReadOnlySpan<byte> data)
    {
        foreach (var value in data)
        {
            crc ^= value;

            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 1) != 0
                    ? (crc >> 1) ^ Polynomial
                    : crc >> 1;
            }
        }

        return crc;
    }

    public static uint Finalize(uint crc)
    {
        return ~crc;
    }
}
