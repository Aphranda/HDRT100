namespace DhrT100.SequenceDebug.Core;

/// <summary>Readback formatters for the sequence, link, counter and USB status rows.</summary>
public static class StatusFormat
{
    public static string FormatLinkStatus(string response, int planCount)
    {
        var values = Csv.ParseUnsigned(Csv.ParseLine(response));
        if (values.Length != 27)
        {
            throw new ValueError("RJ45 网关状态字段不匹配");
        }

        var enabled = values[0];
        var phase = values[1];
        var error = values[2];
        var target = values[21] == 0 ? "持续" : values[21].ToString();
        var rounds = planCount > 0 ? values[12] / planCount : 0;
        var phaseName = SequenceConstants.LinkPhases.TryGetValue((int)phase, out var name)
            ? name
            : $"阶段 {phase}";
        return $"RJ45 物理回环：{phaseName} · 启用={enabled} · 错误={error} · " +
               $"测量触发 {values[11]} / READY {values[12]} / 切换完成 {values[13]} · " +
               $"轮次 {rounds}/{target} · TDMA 发送 {values[8]} / 接收 {values[9]} / 拒绝 {values[10]} · " +
               $"交换 {values[22]} · 延迟 {values[23]}/{values[24]}/{values[25]}/{values[26]} ms";
    }

    public static string FormatCounterStatus(string response)
    {
        var values = Csv.ParseUnsigned(Csv.ParseLine(response));
        if (values.Length != 12)
        {
            throw new ValueError("转台计数状态字段不匹配");
        }

        var enabled = values[0];
        var slot = values[1];
        var channel = values[2];
        var threshold = values[3];
        var total = values[4];
        var positions = values[5];
        var partial = values[6];
        var faults = values[7];
        var history = values[8];
        var retained = values[9];
        var phase = values[10];
        var error = values[11];
        var phaseName = SequenceConstants.LinkPhases.TryGetValue((int)phase, out var name)
            ? name
            : $"阶段 {phase}";
        return $"计数槽位 {slot} · IN{channel} · 启用={enabled} · 累计脉冲 {total} · " +
               $"位置 {positions} · 当前 {partial}/{threshold} · 异常 {faults} · " +
               $"记录 {retained}/{history} · {phaseName} · 错误 {error}";
    }

    public static string FormatCounterHistory(string response)
    {
        var values = Csv.ParseUnsigned(Csv.ParseLine(response));
        if (values.Length != 8)
        {
            throw new ValueError("转台历史记录字段不匹配");
        }

        var ordinal = values[0];
        var run = values[1];
        var generation = values[2];
        var position = values[3];
        var index = values[4];
        var threshold = values[5];
        var observed = values[6];
        var flags = values[7];
        var outcomes = new List<string>();
        if ((flags & 1) != 0) outcomes.Add("已请求切换");
        if ((flags & 2) != 0) outcomes.Add("切换完成");
        if ((flags & 4) != 0) outcomes.Add("采样完成");
        var outcomeText = outcomes.Count > 0 ? string.Join(" / ", outcomes) : "无完成标记";
        return $"记录 {ordinal} · 运行 {run} · 配置代次 {generation}\n" +
               $"位置 {position} · 序列索引 {index}（从 0 开始）\n" +
               $"阈值脉冲数 {threshold} · 请求时计数快照 {observed}\n" +
               $"结果：{outcomeText}（flags={flags}）\n" +
               "计数快照是发送切换请求时的软件读数，不是实际开关输出的物理边沿测量。";
    }

    public static string FormatSwitchPosition(string response)
    {
        var fields = Csv.ParseLine(response);
        if (fields.Count < 7)
        {
            throw new ValueError("序列状态字段不足");
        }

        var runtimeState = fields[0];
        // These are uint32 on the wire: an idle device reports 0xFFFFFFFF for
        // current_index, which does not fit a signed 32-bit integer.
        var count = Csv.ParseUnsigned(fields[3]);
        var currentIndex = Csv.ParseUnsigned(fields[4]);
        var currentState = Csv.ParseUnsigned(fields[5]);
        if (runtimeState == "IDLE" || count == 0 || currentIndex == 0xffffffffL)
        {
            return "当前开关：未运行（输出已释放）";
        }
        if (currentIndex >= count)
        {
            throw new ValueError("当前开关位置越界");
        }
        return $"当前开关：第 {currentIndex + 1}/{count} 位 " +
               $"（状态 {currentState}，运行态 {runtimeState}）";
    }

    public static string ParseUsbMode(string response)
    {
        var mode = (response ?? string.Empty).Trim().Trim('"').ToUpperInvariant();
        if (mode is not ("CDC" or "USBTMC"))
        {
            throw new ValueError("当前固件未启用 PROJECT_ENABLE_USB_RUNTIME_SWITCH");
        }
        return mode;
    }

    /// <summary>Operator position shown for the independent SP8T switch.</summary>
    public static int ParseSwitchPosition(string response)
    {
        var values = Csv.SplitInts(response);
        if (values.Length < 2)
        {
            throw new ValueError("独立 SP8T 读回字段不足");
        }
        return values[1];
    }
}
