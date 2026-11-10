using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// Builds the SCPI batches for the three sequence modes. Every method is pure:
/// it validates the operator draft and returns commands, without touching a
/// transport. Nothing is sent until the caller submits the batch.
/// </summary>
public static class SequenceCommandBuilder
{
    public static List<string> BuildConfigurationCommands(
        string plan, IReadOnlyList<int> codes, string source, string edge, int settleUs,
        int pulseUs, int sequenceOutputMask = 7, int statusOutputMask = 8,
        string statusMode = "PULSE")
    {
        var mode = statusMode switch
        {
            "无" => "NONE",
            "电平" => "LEVEL",
            "脉冲" => "PULSE",
            _ => (statusMode ?? string.Empty).ToUpperInvariant(),
        };
        if (mode is not ("NONE" or "LEVEL" or "PULSE"))
        {
            throw new ValueError("状态输出形式必须是无、电平或脉冲");
        }
        if (mode == "NONE" && statusOutputMask != 0)
        {
            throw new ValueError("无状态输出模式的状态掩码必须为 0");
        }
        if (mode != "NONE" && statusOutputMask == 0)
        {
            throw new ValueError("电平或脉冲模式必须分配至少一个状态输出 OUT");
        }
        if (sequenceOutputMask == 0 ||
            ((sequenceOutputMask | statusOutputMask) & ~15) != 0 ||
            (sequenceOutputMask & statusOutputMask) != 0)
        {
            throw new ValueError("序列编码必须占至少一个 OUT，且与状态输出互不重叠、均在 OUT1–OUT4 范围内");
        }
        if (settleUs < 0 || settleUs > SequenceConstants.TimeMaxUs)
        {
            throw new ValueError("建立时间超出固件范围");
        }
        if (mode == "PULSE" && (pulseUs <= 0 || pulseUs > SequenceConstants.TimeMaxUs))
        {
            throw new ValueError("脉冲宽度必须大于 0 且不超出固件范围");
        }
        var configuredPulse = mode == "PULSE" ? pulseUs : 0;
        if (codes.Count == 0)
        {
            throw new ValueError("至少需要一个输出编码");
        }
        // The firmware validates (value >> SP8T_SHIFT) & ~sequence_output_mask,
        // so bit 0 (OUT0 / SP2T) is outside the SP8T mask and codes span 0..15.
        if (codes.Any(code => code < 0 || code > SequenceConstants.CodeMax ||
                              ((code >> SequenceConstants.CodeSp8tShift) & ~sequenceOutputMask) != 0))
        {
            throw new ValueError(
                $"输出编码只能使用序列编码 OUT（掩码 0x{sequenceOutputMask:X}）");
        }

        var states = Enumerable.Range(0, codes.Count).ToList();
        var (channels, polarity) = TriggerParameters(codes.Count);
        var commands = new List<string>
        {
            "TRIG:STOP",
            $"CONF:TRIG {channels},{polarity},1,1",
            $"CONF:SEQ {plan}," + string.Join(",", states),
            $"CONF:SEQ:ACT {plan}",
            $"CONF:SEQ:OUTPUT {sequenceOutputMask},{statusOutputMask},{mode},{settleUs},{configuredPulse}",
        };
        for (var index = 0; index < codes.Count; index++)
        {
            commands.Add($"CONF:SEQ:CODE {states[index]},{codes[index]}");
        }
        commands.Add($"CONF:SEQ:SOUR {source},{edge}");
        commands.Add("TRIG:SEQ:NEXT?");
        commands.Add("READ:IO:STAT?");
        return commands;
    }

    public static List<string> BuildModeConfiguration(
        string mode, string plan, IReadOnlyList<int> codes, string source, string edge,
        int settleUs, int pulseUs, int sequenceMask, int statusMask, string statusMode,
        string readyInput = "MANUAL", int timeoutMs = 5000, long repeatCount = 1,
        string gatewayOutput = "OUT4", int counterSlot = 1, int dutSlot = 2, int vnaSlot = 3,
        string counterInput = "IN1", int counterThreshold = 1000, AngleScan? angleScan = null,
        int? operatingLevel = 7)
    {
        if (mode is not (SequenceConstants.ModeIndependent or SequenceConstants.ModeRj45
            or SequenceConstants.ModeTurntable))
        {
            throw new ValueError("请选择运行模式");
        }
        // null keeps whatever profile the ring already runs: a combined batch
        // without an explicit choice must not stage a level the operator never picked.
        if (operatingLevel is int level && level < 0)
        {
            throw new ValueError("TDMA operating level must be uint32 or None");
        }
        if (angleScan is not null)
        {
            if (mode != SequenceConstants.ModeTurntable)
            {
                throw new ValueError("角度扫描仅适用于转台计数模式");
            }
            // Revalidate callers' data too: a manually constructed object is not authority.
            angleScan = AngleScanCalculator.Calculate(
                FormatNumber(angleScan.Start), FormatNumber(angleScan.Stop),
                FormatNumber(angleScan.Step), FormatNumber(angleScan.Speed),
                FormatNumber(angleScan.PulsesPerDegree));
            repeatCount = angleScan.Positions;
            counterThreshold = angleScan.Threshold;
        }
        if (repeatCount < 0 || repeatCount > 0xffffffffL)
        {
            throw new ValueError("循环次数必须为非负整数；0 表示持续运行");
        }
        if (repeatCount > 0 && codes.Count * repeatCount > 0xffffffffL)
        {
            throw new ValueError("循环次数与序列长度的乘积超出固件计数范围");
        }

        var combined = mode is SequenceConstants.ModeRj45 or SequenceConstants.ModeTurntable;
        var position = mode == SequenceConstants.ModeTurntable;
        if (position &&
            (!IsInput(counterInput) || counterInput == readyInput ||
             counterThreshold < 1 || (long)counterThreshold > SequenceConstants.CounterThresholdMax ||
             new[] { counterSlot, dutSlot, vnaSlot }.Any(slot => slot < 0 || slot >= 8) ||
             new[] { counterSlot, dutSlot, vnaSlot }.Distinct().Count() != 3))
        {
            throw new ValueError(
                $"计数与 READY 输入须分开；阈值须在 1–{SequenceConstants.CounterThresholdMax} 内；三个槽位须不同且在 0–7 内");
        }

        var normalizedStatusMode = statusMode switch
        {
            "无" => "NONE",
            "电平" => "LEVEL",
            "脉冲" => "PULSE",
            _ => (statusMode ?? string.Empty).ToUpperInvariant(),
        };

        var gatewayMasks = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            ["OUT1"] = 1,
            ["OUT2"] = 2,
            ["OUT3"] = 4,
            ["OUT4"] = 8,
        };
        if (combined &&
            (!IsReadySource(readyInput) || edge is not ("RIS" or "FALL") ||
             timeoutMs < 1 || timeoutMs > 0x7fffffff ||
             pulseUs <= 0 || pulseUs > SequenceConstants.TimeMaxUs ||
             !gatewayMasks.ContainsKey(gatewayOutput) ||
             statusMask != 0 || normalizedStatusMode != "NONE" ||
             (sequenceMask & gatewayMasks.GetValueOrDefault(gatewayOutput, 0)) != 0))
        {
            throw new ValueError("请检查 READY、OUT 属性、触发脉宽和等待超时");
        }

        var baseCommands = BuildConfigurationCommands(
            plan, codes, combined ? "MANUAL" : source, edge, settleUs,
            combined ? 0 : pulseUs, sequenceMask, combined ? 0 : statusMask,
            combined ? "NONE" : normalizedStatusMode);

        var commands = new List<string> { "TRIG:STOP", "SYST:TDMA:RING:STOP", "CONF:SEQ:LINK OFF" };
        // baseCommands[1..^2] drops TRIG:STOP and the trailing readbacks.
        commands.AddRange(baseCommands.Skip(1).Take(baseCommands.Count - 3));
        commands.Add($"CONF:SEQ:REPEAT {repeatCount}");

        if (combined)
        {
            if (position)
            {
                commands.Add($"CONF:SEQ:NODE:ROLE {counterSlot},2,COUNTER");
            }
            else
            {
                dutSlot = 2;
                vnaSlot = 3;
            }
            commands.Add($"CONF:SEQ:NODE:ROLE {dutSlot},5,DUT");
            commands.Add($"CONF:SEQ:NODE:ROLE {vnaSlot},7,VNA");
            commands.Add("CONF:SEQ:NODE:ACT");
            if (operatingLevel is not null)
            {
                commands.Add($"SYST:TDMA:OPMODE:STAGE {operatingLevel}");
                commands.Add("SYST:TDMA:OPMODE:APPLY");
            }
            commands.Add("SYST:TDMA:RING:TOPOLOGY 2,0,0");
            commands.Add("CAL:TOPOLOGY:PROBE 1,10");
            commands.Add("SYST:TDMA:FLIGHT:MODE 1");
            var prefix = position
                ? $"POSITION,{counterSlot},{dutSlot},{vnaSlot},{counterInput},{counterThreshold}"
                : "LOOPBACK,2,3";
            commands.Add($"CONF:SEQ:LINK {prefix},{readyInput},{gatewayOutput}," +
                         $"{pulseUs},{timeoutMs},{edge}");
            commands.Add("READ:SEQ:LINK?");
            if (position)
            {
                commands.Add("READ:SEQ:COUNTER?");
                if (angleScan is not null)
                {
                    commands.Add($"CONF:ANGLE:SWEEP {FormatNumber(angleScan.Start)}," +
                                 $"{FormatNumber(angleScan.Stop)}," +
                                 $"{FormatNumber(angleScan.Step)}," +
                                 $"{FormatNumber(angleScan.Speed)}");
                    commands.Add($"CONF:ANGLE:INPUT {counterInput}," +
                                 $"{FormatNumber(angleScan.PulsesPerDegree)}");
                    commands.Add("READ:ANGLE:SWEEP?");
                    commands.Add("READ:ANGLE:INPUT?");
                    commands.Add("READ:ANGLE:SPEED?");
                    commands.Add("READ:ANGLE:POSITION?");
                }
            }
        }

        commands.Add("READ:SEQ:REPEAT?");
        commands.Add("TRIG:SEQ:NEXT?");
        commands.Add("READ:IO:STAT?");
        return commands;
    }

    /// <summary>
    /// <c>CONF:TRIG</c> takes (chan_count, pol, freq_count, wave_count), and the
    /// firmware caps <c>chan_count</c> at TRIGGER_SEQUENCE_CHANNEL_MAX. A plan
    /// that also drives the SP2T bit uses pol=2, which doubles the state count:
    /// 16 codes are 8 channels × 2 polarizations, not 16 channels.
    /// </summary>
    public static (int Channels, int Polarity) TriggerParameters(int codeCount)
    {
        if (codeCount <= 0)
        {
            throw new ValueError("至少需要一个输出编码");
        }
        if (codeCount <= SequenceConstants.ChannelMax)
        {
            return (codeCount, 0);
        }
        if (codeCount % 2 == 0 && codeCount / 2 <= SequenceConstants.ChannelMax)
        {
            return (codeCount / 2, 2);
        }
        throw new ValueError(
            $"输出编码数量超出固件通道上限：{codeCount} 个" +
            $"（单极化最多 {SequenceConstants.ChannelMax} 个，双极化最多 {SequenceConstants.ChannelMax * 2} 个）");
    }

    public static List<string> BuildStartCommands(string mode)
    {
        if (mode is SequenceConstants.ModeRj45 or SequenceConstants.ModeTurntable)
        {
            return new List<string>
            {
                "SYST:TDMA:RING:STOP", "SYST:TDMA:RING:ARM", "SYST:TDMA:RING:TRAIN 4096",
                "SYST:TDMA:RING:START", "TRIG:START",
            };
        }
        return new List<string> { "TRIG:START" };
    }

    public static bool IsCombinedMode(string? mode)
    {
        return mode is SequenceConstants.ModeRj45 or SequenceConstants.ModeTurntable;
    }

    public static bool IsInput(string value)
    {
        return value is "IN1" or "IN2" or "IN3" or "IN4";
    }

    public static bool IsReadySource(string value)
    {
        return value is "MANUAL" or "IN1" or "IN2" or "IN3" or "IN4";
    }

    /// <summary>Render a decimal the way Python rendered <c>Decimal</c> in an f-string.</summary>
    public static string FormatNumber(decimal value)
    {
        return value.ToString(CultureInfo.InvariantCulture);
    }
}
