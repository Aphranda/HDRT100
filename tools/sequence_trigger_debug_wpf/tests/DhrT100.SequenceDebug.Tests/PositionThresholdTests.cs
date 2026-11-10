using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class PositionThresholdTests
{
    [Theory]
    [InlineData("频率 Hz", "50", 50)]
    [InlineData("周期 ms", "20", 50)]
    [InlineData("频率 Hz", "1000", 1000)]
    [InlineData("周期 ms", "0.2", 5000)]
    [InlineData("频率 Hz", "100000", 100000)]
    [InlineData("频率 Hz", "49.5", 50)]
    public void Converts_a_rate_to_an_integral_threshold(string kind, string value, int expected)
    {
        var (threshold, frequency, seconds) = PositionThreshold.Calculate(kind, value);
        Assert.Equal(expected, threshold);
        Assert.InRange(Math.Abs((double)seconds - 1d), 0d, 0.011d);
        Assert.True(frequency > 0m);
    }

    [Theory]
    [InlineData("")]
    [InlineData("no")]
    [InlineData("0")]
    [InlineData("-1")]
    [InlineData("NaN")]
    [InlineData("Infinity")]
    [InlineData("1e999999999")]
    [InlineData("0.1")]
    public void Rejects_invalid_rates(string value)
    {
        Assert.Throws<ValueError>(() => PositionThreshold.Calculate("频率 Hz", value));
    }
}
