using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// Validates the binding and values returned by the final ANGLE readbacks.
/// A setter acknowledgement is not evidence that the device adopted the grid.
/// </summary>
public static class AngleReadbackValidator
{
    public static void Validate(AngleScan scan, string source, string sweepResponse, string inputResponse)
    {
        try
        {
            var sweep = Csv.ParseLine(sweepResponse);
            var input = Csv.ParseLine(inputResponse);
            if (sweep.Count != 6 || input.Count != 7)
            {
                throw new ValueError("字段数不匹配");
            }
            if (sweep[^1] != "1" || input[^1] != "1")
            {
                throw new ValueError("配置未绑定到当前 POSITION 序列");
            }
            if (Csv.ParseUnsigned(sweep[4]) != scan.Positions ||
                Csv.ParseUnsigned(input[3]) != scan.Threshold)
            {
                throw new ValueError("位置数或脉冲阈值与提交配置不一致");
            }
            if (input[0] != source)
            {
                throw new ValueError("输入端口与提交配置不一致");
            }

            var expected = new[]
            {
                scan.Start, scan.Stop, scan.Step, scan.Speed, scan.PulsesPerDegree,
                scan.Frequency, scan.Period, 1m / scan.Period,
            };
            var actual = new[]
            {
                sweep[0], sweep[1], sweep[2], sweep[3],
                input[1], input[2], input[4], input[5],
            };
            for (var index = 0; index < actual.Length; index++)
            {
                if (!double.TryParse(actual[index], NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var number) ||
                    !double.IsFinite(number) ||
                    !IsClose(number, (double)expected[index]))
                {
                    throw new ValueError($"参数与提交配置不一致：{actual[index]} != {expected[index]}");
                }
            }
        }
        catch (Exception exception) when (exception is ValueError or FormatException
                                          or OverflowException or ArgumentException)
        {
            throw new RuntimeError($"ANGLE 配置读回校验失败：{exception.Message}", exception);
        }
    }

    /// <summary>SCPI_ResultDouble emits 15 significant digits; 1e-13 relative is safe.</summary>
    private static bool IsClose(double observed, double submitted)
    {
        if (double.IsNaN(observed) || double.IsNaN(submitted))
        {
            return false;
        }
        var difference = Math.Abs(observed - submitted);
        if (submitted == 0d)
        {
            return difference == 0d;
        }
        return difference <= 1e-13 * Math.Abs(submitted);
    }
}
