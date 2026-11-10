using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>Validated angular grid; mirrors the Python <c>AngleScan</c> dataclass.</summary>
public sealed record AngleScan(
    decimal Start,
    decimal Stop,
    decimal Step,
    decimal Speed,
    decimal PulsesPerDegree,
    int Positions,
    int Threshold,
    decimal Frequency,
    decimal Period);

public static class AngleScanCalculator
{
    /// <summary>Validate an exact angular grid before any SCPI mutations are queued.</summary>
    public static AngleScan Calculate(string start, string stop, string step, string speed,
                                      string pulsesPerDegree)
    {
        var raw = new[] { start, stop, step, speed, pulsesPerDegree };
        if (raw.Any(value => (value ?? string.Empty).Trim().Length > 128))
        {
            throw new ValueError("数值过长");
        }

        var values = new decimal[raw.Length];
        for (var index = 0; index < raw.Length; index++)
        {
            if (!decimal.TryParse((raw[index] ?? string.Empty).Trim(), NumberStyles.Float,
                                  CultureInfo.InvariantCulture, out values[index]))
            {
                throw new ValueError("角度、速度及输入标定须为有效有限数值");
            }
        }

        foreach (var value in values)
        {
            if (!double.IsFinite((double)value) || (value != 0m && (double)value == 0d))
            {
                throw new ValueError("参数必须是固件可表示的有限数值");
            }
        }

        var startD = values[0];
        var stopD = values[1];
        var stepD = values[2];
        var speedD = values[3];
        var ppdD = values[4];
        if (stepD == 0m || speedD <= 0m || ppdD <= 0m)
        {
            throw new ValueError("步长不能为 0；速度和每度脉冲数必须大于 0");
        }

        // Rational keeps a fractional endpoint or pulse count from being rounded.
        var intervals = (Rational.FromDecimal(stopD) - Rational.FromDecimal(startD)) /
                        Rational.FromDecimal(stepD);
        var pulses = Rational.FromDecimal(stepD).Abs() * Rational.FromDecimal(ppdD);
        if (intervals.Sign < 0 || !intervals.IsInteger)
        {
            throw new ValueError("步长方向必须指向终止角度，且终止角度须准确落在步长网格上");
        }
        if (!pulses.IsInteger || pulses < Rational.One)
        {
            throw new ValueError("每步脉冲数必须为正整数；请调整步长或每度脉冲数");
        }

        // A reduced fraction is not required for the integer check, but the value
        // itself must be divided: 0.6/0.1 is 60/10, not 60.
        var count = (int)(intervals.Numerator / intervals.Denominator) + 1;
        var threshold = (int)(pulses.Numerator / pulses.Denominator);
        if ((long)count * threshold > SequenceConstants.CounterThresholdMax)
        {
            throw new ValueError("本次扫描累计脉冲数超出固件计数范围，请缩小扫描范围");
        }

        double startF = (double)startD, stopF = (double)stopD, stepF = (double)stepD, ppdF = (double)ppdD;
        var firmwareIntervals = (stopF - startF) / stepF;
        var firmwarePulses = Math.Abs(stepF) * ppdF;
        if (!double.IsFinite(firmwareIntervals) ||
            Math.Abs(firmwareIntervals - (count - 1)) > 1e-7 ||
            !double.IsFinite(firmwarePulses) || Math.Abs(firmwarePulses - threshold) > 1e-7 ||
            (count > 1 && (startF + stepF == startF || stopF - stepF == stopF)))
        {
            throw new ValueError("角度或脉冲数超出固件浮点精度，请调整范围或步长");
        }

        var frequency = ppdD * speedD;
        var period = Math.Abs(stepD) / speedD;
        foreach (var value in new[] { frequency, period })
        {
            var number = (double)value;
            if (!double.IsFinite(number) || number <= 0d || !double.IsFinite(1d / number))
            {
                throw new ValueError("换算后的频率或周期超出固件数值范围");
            }
        }
        if (!double.IsFinite((double)(1m / period)) || 1m / period <= 0m)
        {
            throw new ValueError("换算后的频率或周期超出固件数值范围");
        }

        return new AngleScan(startD, stopD, stepD, speedD, ppdD, count, threshold, frequency, period);
    }
}
