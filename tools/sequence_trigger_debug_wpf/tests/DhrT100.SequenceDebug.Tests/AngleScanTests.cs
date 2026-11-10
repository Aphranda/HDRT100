using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class AngleScanTests
{
    [Theory]
    [InlineData("0", "9", "1", "1", "1000", 10, 1000, 1000d, 1d)]
    [InlineData("10", "0", "-2", "5", "1000", 6, 2000, 5000d, 0.4d)]
    [InlineData("-0.3", "0.3", "0.1", "0.1", "1000", 7, 100, 100d, 1d)]
    [InlineData("0", "0", "1", "0.05", "1000", 1, 1000, 50d, 20d)]
    [InlineData("0", "0.05", "0.05", "0.05", "1000", 2, 50, 50d, 1d)]
    public void Derives_the_exact_position_budget(string start, string stop, string step, string speed,
                                                 string ppd, int count, int threshold, double hz, double period)
    {
        var scan = AngleScanCalculator.Calculate(start, stop, step, speed, ppd);
        Assert.Equal(count, scan.Positions);
        Assert.Equal(threshold, scan.Threshold);
        Assert.Equal(hz, (double)scan.Frequency, 6);
        Assert.Equal(period, (double)scan.Period, 6);
    }

    [Theory]
    [InlineData("0", "1", "-1", "1", "1000")]
    [InlineData("0", "1", "0", "1", "1000")]
    [InlineData("0", "1", "0.3", "1", "1000")]
    [InlineData("0", "1", "0.1", "1", "3")]
    [InlineData("0", "1", "1", "0", "1000")]
    [InlineData("0", "1", "1", "1", "0")]
    [InlineData("NaN", "1", "1", "1", "1000")]
    [InlineData("0", "1", "1", "Infinity", "1000")]
    [InlineData("0", "1", "1", "1e999999999", "1000")]
    [InlineData("0", "1", "1", "1e-999999999", "1000")]
    [InlineData("0", "1.000000000000000000001", "1", "1", "1000")]
    [InlineData("0", "1", "1", "1", "2147483632")]
    [InlineData("0", "1", "1", "1e308", "1000")]
    [InlineData("100000000000000000000", "100000000000000000001", "1", "1", "1000")]
    public void Rejects_invalid_or_fractional_grids(string start, string stop, string step,
                                                    string speed, string ppd)
    {
        Assert.Throws<ValueError>(() => AngleScanCalculator.Calculate(start, stop, step, speed, ppd));
    }

    [Fact]
    public void Rejects_an_over_long_draft()
    {
        var longText = new string('1', 129);
        Assert.Throws<ValueError>(() => AngleScanCalculator.Calculate(longText, "0", "1", "1", "1000"));
    }
}
