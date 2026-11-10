using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>Converts a declared source rate to integral pulses for a one-second position.</summary>
public static class PositionThreshold
{
    public static (int Threshold, decimal Frequency, decimal Seconds) Calculate(string kind, string value)
    {
        if (!decimal.TryParse((value ?? string.Empty).Trim(), NumberStyles.Float,
                              CultureInfo.InvariantCulture, out var number) ||
            number <= 0m)
        {
            throw new ValueError("请输入有效正数；每秒脉冲数须能换算为固件支持的整数阈值。");
        }

        decimal frequency;
        if (kind == "频率 Hz")
        {
            frequency = number;
        }
        else if (kind == "周期 ms")
        {
            frequency = 1000m / number;
        }
        else
        {
            throw new ValueError("请输入有效正数；每秒脉冲数须能换算为固件支持的整数阈值。");
        }

        if (frequency < 0.5m ||
            frequency > SequenceConstants.CounterThresholdMax + 0.49m)
        {
            throw new ValueError("请输入有效正数；每秒脉冲数须能换算为固件支持的整数阈值。");
        }

        var threshold = (int)decimal.Round(frequency, 0, MidpointRounding.AwayFromZero);
        if (threshold < 1 || (long)threshold > SequenceConstants.CounterThresholdMax)
        {
            throw new ValueError("请输入有效正数；每秒脉冲数须能换算为固件支持的整数阈值。");
        }

        return (threshold, frequency, threshold / frequency);
    }
}
