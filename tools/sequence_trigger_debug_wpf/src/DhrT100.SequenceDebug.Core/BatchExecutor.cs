namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// Issues a command batch and verifies every asynchronous boundary before the
/// next dependent configuration is sent. A bare "OK" is not evidence: setters
/// are confirmed through <c>SYST:ERR?</c>, their numeric acknowledgement, and
/// the ring/owner state readbacks that gate the following command.
/// </summary>
public sealed class BatchExecutor
{
    private static readonly string[] AngleSetterPrefixes =
    {
        "CONF:ANGLE:SWEEP ", "CONF:ANGLE:INPUT ", "CONF:ANGLE:SPEED ",
    };

    private readonly Func<string, string> _exchange;
    private readonly Action<string, string> _emit;
    private readonly Func<double> _monotonic;
    private readonly Action<double> _sleep;
    private readonly bool _drainErrorsFirst;
    private readonly List<string> _ownerEvidence = new();

    public BatchExecutor(Func<string, string> exchange, Action<string, string> emit,
                         Func<double>? monotonic = null, Action<double>? sleep = null,
                         bool drainErrorsFirst = false)
    {
        _exchange = exchange;
        _emit = emit;
        _monotonic = monotonic ?? (() => Environment.TickCount64 / 1000.0);
        _sleep = sleep ?? (seconds => Thread.Sleep((int)Math.Round(Math.Max(0d, seconds) * 1000d)));
        _drainErrorsFirst = drainErrorsFirst;
    }

    public IReadOnlyList<string> OwnerEvidence => _ownerEvidence;

    public void Execute(IReadOnlyList<string> commands)
    {
        if (_drainErrorsFirst)
        {
            DrainDeviceErrors();
        }
        var setters = new Dictionary<string, (int Index, string Args)>(StringComparer.Ordinal);
        for (var index = 0; index < commands.Count; index++)
        {
            var command = commands[index];
            var upper = command.ToUpperInvariant();
            var prefix = AngleSetterPrefixes.FirstOrDefault(item => upper.StartsWith(item, StringComparison.Ordinal));
            if (prefix is null)
            {
                continue;
            }
            var parts = SplitHeader(command);
            if (parts.Length == 2)
            {
                setters[parts[0].ToUpperInvariant()] = (index, parts[1]);
            }
        }

        AngleScan? expectedAngle = null;
        string? expectedSource = null;
        var lastAngleWrite = -1;
        if (setters.ContainsKey("CONF:ANGLE:SWEEP") && setters.ContainsKey("CONF:ANGLE:INPUT"))
        {
            var sweepArgs = Csv.ParseLine(setters["CONF:ANGLE:SWEEP"].Args);
            var inputArgs = Csv.ParseLine(setters["CONF:ANGLE:INPUT"].Args);
            if (sweepArgs.Count != 4 || inputArgs.Count != 2)
            {
                throw new ValueError("ANGLE 配置参数数量错误");
            }
            if (setters.TryGetValue("CONF:ANGLE:SPEED", out var speed) &&
                speed.Index > setters["CONF:ANGLE:SWEEP"].Index)
            {
                sweepArgs[3] = speed.Args;
            }
            expectedAngle = AngleScanCalculator.Calculate(
                sweepArgs[0], sweepArgs[1], sweepArgs[2], sweepArgs[3], inputArgs[1]);
            expectedSource = inputArgs[0].Trim().ToUpperInvariant();
            lastAngleWrite = setters.Values.Max(value => value.Index);
        }

        var angleReadbacks = new Dictionary<string, string>(StringComparer.Ordinal);

        string Query(string command)
        {
            var response = _exchange(command);
            _emit(command, response);
            if (SequenceConstants.IsTimeout(response))
            {
                throw new RuntimeError($"{command} 超时");
            }
            return response;
        }

        void WaitUntil(string command, Func<string, bool> ready)
        {
            var deadline = _monotonic() + 5.0;
            while (true)
            {
                if (ready(Query(command)))
                {
                    return;
                }
                if (_monotonic() >= deadline)
                {
                    throw new RuntimeError($"等待设备状态超时：{command}");
                }
                _sleep(0.05);
            }
        }

        for (var commandIndex = 0; commandIndex < commands.Count; commandIndex++)
        {
            var command = commands[commandIndex];
            var header = FirstToken(command).ToUpperInvariant();

            if (header == "TRIG:SEQ:NEXT")
            {
                var link = Csv.ParseUnsigned(Csv.ParseLine(Query("READ:SEQ:LINK?")));
                if (link.Length != 27)
                {
                    throw new RuntimeError("RJ45 状态字段不匹配，未发送 NEXT");
                }
                var enabled = link[0];
                var phase = link[1];
                var error = link[2];
                var readyInput = link[16];
                if (enabled != 0 && readyInput != 0)
                {
                    throw new RuntimeError("RJ45 READY 来源不是 MANUAL，未发送 NEXT");
                }
                if (enabled != 0 && (phase != 3 || error != 0))
                {
                    throw new RuntimeError($"RJ45 尚不能接受 NEXT：phase={phase}, error={error}");
                }
            }

            if (header == "TRIG:START")
            {
                EnsureOutputsReleased();
            }

            var response = _exchange(command);
            _emit(command, response);
            if (SequenceConstants.IsTimeout(response) &&
                !SequenceConstants.RingAckOnly.Contains(header, StringComparer.Ordinal))
            {
                // The firmware rejects a start without emitting a reply, so the
                // operator only sees a timeout: name the likely causes.
                var hint = header == "TRIG:START"
                    ? "（设备拒绝了启动且未回包；请核对 READ:SEQ:CHEC? 与 READ:TRIG:STAT?）"
                    : string.Empty;
                throw new RuntimeError($"{command} 超时，已停止后续命令{hint}");
            }
            if (header.Contains('?'))
            {
                if (expectedAngle is not null && commandIndex > lastAngleWrite)
                {
                    angleReadbacks[header] = response;
                }
                continue;
            }

            var deviceError = Csv.ParseLine(Query("SYST:ERR?"));
            if (deviceError.Count == 0 || deviceError[0] != "0")
            {
                throw new RuntimeError($"{command} 被设备拒绝：{string.Join(",", deviceError)}");
            }

            if (header.StartsWith("CONF:SEQ", StringComparison.Ordinal) ||
                header.StartsWith("CONF:TRIG", StringComparison.Ordinal) ||
                header.StartsWith("CONF:ANGLE", StringComparison.Ordinal) ||
                header.StartsWith("TRIG:", StringComparison.Ordinal))
            {
                if (header == "CONF:SEQ:NODE:ROLE")
                {
                    var fields = Csv.ParseLine(response);
                    if (fields.Count != 1 || fields[0] != "STAGED")
                    {
                        throw new RuntimeError($"角色配置未暂存：{response}");
                    }
                }
                else if (header == "CONF:SEQ:NODE:ACT")
                {
                    var fields = Csv.ParseLine(response);
                    if (fields.Count == 0 || fields[0] != "ACTIVE")
                    {
                        throw new RuntimeError($"角色配置未激活：{response}");
                    }
                }
                else if (response != "1")
                {
                    throw new RuntimeError($"{command} 未确认执行：{response}");
                }
            }

            if (header == "TRIG:STOP")
            {
                WaitUntil("TRIG:SEQ:NEXT?", value =>
                {
                    var fields = Csv.ParseLine(value);
                    return fields.Count > 0 && fields[0] == "IDLE";
                });
            }
            else if (header == "SYST:TDMA:RING:TOPOLOGY")
            {
                var sequence = OwnerState.ReadQueuedSequence(response);
                OwnerState.WaitApplied(Query, sequence, 5.0, _ownerEvidence, _monotonic, _sleep);
            }
            else if (SequenceConstants.RingAckOnly.Contains(header, StringComparer.Ordinal))
            {
                WaitUntil(SequenceConstants.RingStatusQuery, value => IsRingReady(header, value));
            }
            else if (header == "SYST:TDMA:RING:TRAIN")
            {
                _sleep(0.2);
            }
        }

        if (expectedAngle is not null)
        {
            if (!angleReadbacks.ContainsKey("READ:ANGLE:SWEEP?") ||
                !angleReadbacks.ContainsKey("READ:ANGLE:INPUT?"))
            {
                throw new RuntimeError("ANGLE 配置缺少下发完成后的 SWEEP/INPUT 读回，未确认配置成功");
            }
            AngleReadbackValidator.Validate(expectedAngle, expectedSource!,
                angleReadbacks["READ:ANGLE:SWEEP?"], angleReadbacks["READ:ANGLE:INPUT?"]);
        }
    }

    /// <summary>
    /// The firmware only starts a sequence when every SMA output currently reads
    /// low (<c>outputs_available()</c> is otherwise false) and it reports that
    /// rejection as a silent failure. A leftover manual SP8T position therefore
    /// blocks START with no reply at all; catch it before the command is sent.
    /// </summary>
    private void EnsureOutputsReleased()
    {
        string probe;
        try
        {
            probe = _exchange("READ:IO:STAT?");
        }
        catch (Exception)
        {
            return;
        }
        _emit("READ:IO:STAT?", probe);
        if (SequenceConstants.IsTimeout(probe))
        {
            return;
        }
        var values = Csv.ParseLine(probe);
        if (values.Count != 5)
        {
            return;
        }
        var outputMask = Csv.ParseUnsigned(values[1]);
        if (outputMask == 0)
        {
            return;
        }
        throw new RuntimeError(
            $"序列启动前 SMA 输出必须全部为低（固件 outputs_available 条件）；" +
            $"当前输出掩码 0x{outputMask:X}。" +
            "请先在“手动 SP8T”页点击“释放输出”（TRIG:STOP → CONF:SWITCH1 1），再点击启动。");
    }

    /// <summary>
    /// Pop the device error queue before a user batch. A firmware rejection from
    /// an earlier transaction (for example a failed START) otherwise becomes the
    /// first <c>SYST:ERR?</c> answer and is blamed on the next command.
    /// </summary>
    private void DrainDeviceErrors()
    {
        for (var attempt = 0; attempt < 8; attempt++)
        {
            string response;
            try
            {
                response = _exchange("SYST:ERR?");
            }
            catch (Exception)
            {
                return;
            }
            _emit("SYST:ERR?", response);
            if (SequenceConstants.IsTimeout(response))
            {
                return;
            }
            var fields = Csv.ParseLine(response);
            if (fields.Count == 0 || fields[0] == "0")
            {
                return;
            }
        }
    }

    private static bool IsRingReady(string header, string value)
    {
        var fields = Csv.ParseUnsigned(Csv.ParseLine(value));
        if (fields.Length != SequenceConstants.RuntimeFieldCount)
        {
            throw new ValueError("TDMA 运行状态字段不匹配");
        }
        if (header == "SYST:TDMA:RING:STOP")
        {
            return fields.Field("ring_enabled") == 0 &&
                   fields.Field("ring_adapter_started") == 0 &&
                   fields.Field("ring_config_seq") == fields.Field("ring_applied_config_seq");
        }
        var armed = fields.Field("ring_enabled") == 1 && fields.Field("ring_adapter_started") == 1;
        return armed &&
               (header.EndsWith(":ARM", StringComparison.Ordinal) || fields.Field("ring_up_running") == 1);
    }

    /// <summary>Python <c>str.split(maxsplit=1)</c>.</summary>
    internal static string[] SplitHeader(string command)
    {
        var trimmed = command.Trim();
        if (trimmed.Length == 0)
        {
            return Array.Empty<string>();
        }
        var index = 0;
        while (index < trimmed.Length && !char.IsWhiteSpace(trimmed[index]))
        {
            index++;
        }
        if (index == trimmed.Length)
        {
            return new[] { trimmed };
        }
        var rest = trimmed[index..].TrimStart();
        return rest.Length == 0 ? new[] { trimmed } : new[] { trimmed[..index], rest };
    }

    internal static string FirstToken(string command)
    {
        var parts = SplitHeader(command);
        return parts.Length > 0 ? parts[0] : string.Empty;
    }
}
