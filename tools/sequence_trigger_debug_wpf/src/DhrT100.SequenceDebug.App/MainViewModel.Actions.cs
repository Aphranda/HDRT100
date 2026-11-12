using System.IO;
using System.Numerics;
using System.Text.Json.Nodes;
using DhrT100.SequenceDebug.Core;
using DhrT100.SequenceDebug.Transport;

namespace DhrT100.SequenceDebug.App;

public sealed partial class MainViewModel
{
    // ------------------------------------------------------------- gating

    public void ConfigureMode(string mode)
    {
        if (mode != RunMode)
        {
            RunMode = mode;
            UpdateRunMode();
        }
        Configure();
    }

    public void CommandMode(string mode, string command)
    {
        if (mode != RunMode)
        {
            RunMode = mode;
            UpdateRunMode();
        }
        Command(command);
    }

    private void UpdateRunMode()
    {
        _configuredMode = null;
        _configurationGeneration++;
        Status = "页面已切换；启动前请配置此模式";
        UpdateModeHint();
    }

    public void UpdateModeHint()
    {
        OnPropertyChanged(nameof(IndependentNextEnabled));
        OnPropertyChanged(nameof(GatewayNextEnabled));
        OnPropertyChanged(nameof(TurntableNextEnabled));

        if (RunMode == SequenceConstants.ModeTurntable)
        {
            ModeHint = "启动等待计数阈值；每个位置经 RJ45 执行完整序列。";
            return;
        }

        if (RunMode == SequenceConstants.ModeRj45)
        {
            var ready = Fields["gateway_ready_input"] == "MANUAL"
                ? "SCPI NEXT"
                : $"{Fields["gateway_ready_input"]} READY";
            string gatewayOutput;
            try
            {
                gatewayOutput = GatewayAssignment().Output;
            }
            catch (ValueError)
            {
                gatewayOutput = "所选 OUT";
            }
            ModeHint = $"RJ45 物理回环：首编码 → 响应延时 → TDMA → {gatewayOutput} 触发 → " +
                       $"{ready} → TDMA → 下一编码；有限次数完成后停止。";
            return;
        }

        ModeHint = Fields["source"] == "MANUAL"
            ? "启动首编码 → 响应延时 → 配置的状态输出；使用“下一步”推进。"
            : $"首编码 → 响应延时 → 配置的状态输出；随后 {Fields["source"]} 每个 {Fields["edge"]} 沿推进一步。";
    }

    public void UpdateStatusMode()
    {
        var mode = Fields["status_mode"];
        foreach (var slot in OutSlots)
        {
            if (mode == "无" && slot.Role == SequenceConstants.RoleStatus)
            {
                slot.Enabled = false;
            }
        }
        OutputHint = mode switch
        {
            "无" => "DUT 仅输出编码电平；不占用状态 OUT，不输出完成脉冲。",
            "电平" => "启动及切步：编码稳定后等待响应时间，再置高状态 OUT。",
            _ => "启动及切步：编码稳定后等待响应时间，再输出状态脉冲。",
        };
        OnPropertyChanged(nameof(PulseEditable));
        OnPropertyChanged(nameof(AngleScanEnabled));
    }

    public bool PulseEditable => Fields["status_mode"] == "脉冲";

    private (int Sequence, int Secondary) RoleMasks(IReadOnlyList<OutSlot> slots, string secondaryRole)
    {
        var sequenceMask = 0;
        var secondaryMask = 0;
        foreach (var slot in slots)
        {
            if (!slot.Enabled)
            {
                continue;
            }
            if (slot.Role == SequenceConstants.RoleSequence)
            {
                sequenceMask |= slot.Mask;
            }
            else if (slot.Role == secondaryRole)
            {
                secondaryMask |= slot.Mask;
            }
            else
            {
                throw new ValueError($"OUT{slot.Index + 1} 角色无效");
            }
        }
        return (sequenceMask, secondaryMask);
    }

    private (int SequenceMask, string Output) GatewayAssignment()
    {
        var (sequenceMask, gatewayMask) = RoleMasks(GatewayOutSlots, SequenceConstants.RoleGateway);
        if (gatewayMask == 0 || (gatewayMask & (gatewayMask - 1)) != 0)
        {
            throw new ValueError("RJ45 模式必须启用且仅启用一路 VNA 触发 OUT");
        }
        return (sequenceMask, $"OUT{BitLength(gatewayMask)}");
    }

    private static int BitLength(int mask)
    {
        return 32 - BitOperations.LeadingZeroCount((uint)mask);
    }

    // ---------------------------------------------------------- configure

    public void Configure()
    {
        if (_loopbackRunning)
        {
            Log("请先结束回环观察，再配置序列。", "WARN");
            return;
        }

        var mode = RunMode;
        List<string> commands;
        var codes = new List<int>();
        AngleScan? angleScan = null;
        var counterThreshold = 0;
        var counterInput = string.Empty;
        var sequenceMask = 0;
        var statusMask = 0;
        var gatewayOutput = "OUT4";
        var statusModeText = Fields["status_mode"];
        try
        {
            if (Fields["port"].Trim().Length == 0)
            {
                throw new ValueError("请先扫描或输入通信资源");
            }

            var combined = SequenceCommandBuilder.IsCombinedMode(mode);
            // Only the combined modes carry a TDMA ring, so the ring-level
            // operating profile is resolved for those alone, and only through
            // labels the device catalog advertised.
            var draft = Fields["operating_level"].Trim();
            int? operatingLevel = combined && draft.Length > 0
                ? OperatingProfile.ResolveLevel(draft, _operatingLabels)
                : null;

            if (mode == SequenceConstants.ModeTurntable)
            {
                angleScan = Bools["turntable_angle_enabled"] ? AngleDraft() : null;
                codes = ParseCodes(Fields["turntable_codes"]);
                var settle = ParseInt(Fields["turntable_settle"], "响应延时");
                var pulse = ParseInt(Fields["turntable_pulse"], "触发脉宽");
                var repeat = angleScan?.Positions ?? ParseInt(Fields["turntable_repeat_count"], "位置数");
                var timeout = ParseInt(Fields["turntable_timeout"], "READY 超时");
                var ready = Fields["turntable_ready_input"];
                var edge = Fields["turntable_edge"];
                var masks = RoleMasks(TurntableOutSlots, SequenceConstants.RoleGateway);
                sequenceMask = masks.Sequence;
                if (masks.Secondary == 0 || (masks.Secondary & (masks.Secondary - 1)) != 0)
                {
                    throw new ValueError("转台模式必须分配且仅分配一路 VNA 触发 OUT");
                }
                gatewayOutput = $"OUT{BitLength(masks.Secondary)}";
                counterInput = Fields["turntable_input"];
                counterThreshold = angleScan?.Threshold ??
                                   ParseInt(Fields["turntable_threshold"], "每位置脉冲数");
                commands = SequenceCommandBuilder.BuildModeConfiguration(
                    mode, Fields["turntable_plan"], codes, "MANUAL", edge, settle, pulse,
                    sequenceMask, 0, "NONE", ready, timeout, repeat, gatewayOutput,
                    ParseInt(Fields["turntable_counter_slot"], "计数槽位"),
                    ParseInt(Fields["turntable_dut_slot"], "DUT 槽位"),
                    ParseInt(Fields["turntable_vna_slot"], "VNA 槽位"),
                    counterInput, counterThreshold, angleScan, operatingLevel);
            }
            else if (combined)
            {
                codes = ParseCodes(Fields["gateway_codes"]);
                var settle = ParseInt(Fields["gateway_settle"], "响应延时");
                var pulse = ParseInt(Fields["gateway_pulse"], "触发脉宽");
                var repeat = ParseInt(Fields["gateway_repeat_count"], "循环次数");
                var timeout = ParseInt(Fields["gateway_timeout"], "READY 超时");
                var assignment = GatewayAssignment();
                sequenceMask = assignment.SequenceMask;
                gatewayOutput = assignment.Output;
                commands = SequenceCommandBuilder.BuildModeConfiguration(
                    mode, Fields["gateway_plan"], codes, "MANUAL", Fields["gateway_edge"],
                    settle, pulse, sequenceMask, 0, "NONE", Fields["gateway_ready_input"],
                    timeout, repeat, gatewayOutput, operatingLevel: operatingLevel);
            }
            else
            {
                codes = ParseCodes(Fields["codes"]);
                statusModeText = Fields["status_mode"];
                var settle = ParseInt(Fields["settle"], "响应延时");
                var pulse = statusModeText == "脉冲" ? ParseInt(Fields["pulse"], "状态脉宽") : 0;
                var repeat = ParseInt(Fields["repeat_count"], "循环次数");
                var masks = RoleMasks(OutSlots, SequenceConstants.RoleStatus);
                sequenceMask = masks.Sequence;
                statusMask = masks.Secondary;
                commands = SequenceCommandBuilder.BuildModeConfiguration(
                    mode, Fields["plan"], codes, Fields["source"], Fields["edge"], settle, pulse,
                    sequenceMask, statusMask, statusModeText, "IN1", 5000, repeat, "OUT4",
                    operatingLevel: operatingLevel);
            }

            if (combined)
            {
                commands.Add(SequenceConstants.OperatingCatalogQuery);
                commands.Add(SequenceConstants.OperatingProfileQuery);
            }
        }
        catch (ValueError exception)
        {
            Log($"配置错误: {exception.Message}");
            return;
        }
        catch (Exception exception) when (exception is FormatException or OverflowException or ArgumentException)
        {
            Log($"配置错误: {exception.Message}");
            return;
        }

        if (mode == SequenceConstants.ModeTurntable)
        {
            Log($"转台模式：每 {counterThreshold} 个 {counterInput} 脉冲" +
                $"执行 {codes.Count} 项序列，共 {Fields["turntable_repeat_count"]} 个位置（0=持续）；" +
                $"启动等待首个位置，响应延时 {Fields["turntable_settle"]} µs。");
            if (angleScan is not null)
            {
                Log($"角度扫描：{angleScan.Start}° → {angleScan.Stop}°，步长 {angleScan.Step}°，" +
                    $"速度 {angleScan.Speed}°/s；输入频率应为 {angleScan.Frequency} Hz，" +
                    $"位置周期 {SequenceCommandBuilder.FormatNumber(angleScan.Period)} s。" +
                    "实际转台/信号源需另行设置；即将下发并读回 ANGLE 配置。");
            }
        }
        else
        {
            var repeat = mode == SequenceConstants.ModeRj45
                ? Fields["gateway_repeat_count"]
                : Fields["repeat_count"];
            var settleText = mode == SequenceConstants.ModeRj45
                ? Fields["gateway_settle"]
                : Fields["settle"];
            Log($"{mode}：配置 {codes.Count} 个位置；循环次数 {repeat}（0=持续）；" +
                $"启动直接输出首项编码 {codes.FirstOrDefault()}，" +
                $"编码稳定后等待响应时间 {settleText} µs 再输出状态/通知网关；" +
                $"序列掩码 0x{sequenceMask:X}，状态掩码 0x{statusMask:X}（{statusModeText}）。");
        }

        if (SequenceCommandBuilder.IsCombinedMode(mode))
        {
            var ready = mode == SequenceConstants.ModeTurntable
                ? Fields["turntable_ready_input"]
                : Fields["gateway_ready_input"];
            var edge = mode == SequenceConstants.ModeTurntable
                ? Fields["turntable_edge"]
                : Fields["gateway_edge"];
            var pulse = mode == SequenceConstants.ModeTurntable
                ? Fields["turntable_pulse"]
                : Fields["gateway_pulse"];
            var timeout = mode == SequenceConstants.ModeTurntable
                ? Fields["turntable_timeout"]
                : Fields["gateway_timeout"];
            var level = Fields["operating_level"].Trim();
            Log($"VNA 网关：{ready} {edge} READY，{gatewayOutput} 触发 {pulse} µs，" +
                $"等待超时 {timeout} ms；TDMA 挡位 {(level.Length > 0 ? level : "保持")}。");
        }
        else
        {
            Log(OutputHint);
        }

        if (_otaRunning || _transportSwitching)
        {
            Log("设备维护正在占用通信资源，请稍后配置。", "WARN");
            return;
        }

        _configuredMode = null;
        _configurationGeneration++;
        var generation = _configurationGeneration;
        var backend = Fields["backend"];
        var resource = Fields["port"].Trim();
        var planCount = codes.Count;
        var batch = commands;
        _queue.Post(() =>
        {
            var passed = RunCommands(backend, resource, batch);
            _dispatcher.BeginInvoke(() => ConfigurationFinished(
                passed ? mode : null, passed ? planCount : 0, generation, (backend, resource)));
        });
    }

    private static List<int> ParseCodes(string text)
    {
        var codes = new List<int>();
        foreach (var part in text.Split(','))
        {
            var trimmed = part.Trim();
            if (trimmed.Length == 0)
            {
                continue;
            }
            if (!int.TryParse(trimmed, out var value))
            {
                throw new ValueError($"输出编码必须是整数：'{trimmed}'");
            }
            codes.Add(value);
        }
        return codes;
    }

    private static int ParseInt(string text, string label)
    {
        if (!int.TryParse((text ?? string.Empty).Trim(), out var value))
        {
            throw new ValueError($"{label}必须是整数：'{text}'");
        }
        return value;
    }

    // ------------------------------------------------------------ commands

    public void Command(string command)
    {
        if (command == "TRIG:STOP")
        {
            _loopbackCancel?.Cancel();
            if (_loopbackRunning && _loopbackTarget is not null)
            {
                var target = _loopbackTarget.Value;
                var stopCommands = new List<string> { "TRIG:STOP" };
                if (target.Mode != SequenceConstants.ModeIndependent)
                {
                    stopCommands.Add("SYST:TDMA:RING:STOP");
                }
                stopCommands.Add("TRIG:SEQ:NEXT?");
                stopCommands.Add("READ:IO:STAT?");
                EnqueueFor(target.Backend, target.Resource, stopCommands);
                _loopbackTarget = null;
                return;
            }
        }
        else if (_loopbackRunning && !command.EndsWith('?'))
        {
            Log("回环观察期间只接受读取和停止。", "WARN");
            return;
        }

        var combined = SequenceCommandBuilder.IsCombinedMode(RunMode);
        var deviceCombined = SequenceCommandBuilder.IsCombinedMode(_deviceMode);
        var position = RunMode == SequenceConstants.ModeTurntable ||
                       _deviceMode == SequenceConstants.ModeTurntable;
        var ready = position ? Fields["turntable_ready_input"] : Fields["gateway_ready_input"];
        if (command == "TRIG:SEQ:NEXT" &&
            (((combined || deviceCombined) && ready != "MANUAL") ||
             (!combined && !deviceCombined && Fields["source"] != "MANUAL")))
        {
            Log("当前模式由外部输入推进，SCPI NEXT 已禁用。");
            return;
        }

        if (command == "TRIG:START" &&
            (_configuredMode != RunMode || _configuredResource != ResourceKey()))
        {
            Log("请先点击“配置此模式”，确认当前参数和通信资源配置成功后再启动。", "WARN");
            return;
        }

        var commands = command == "TRIG:START"
            ? SequenceCommandBuilder.BuildStartCommands(RunMode)
            : new List<string> { command };
        if (command == "TRIG:STOP" && (deviceCombined || _deviceMode is null))
        {
            commands.Add("SYST:TDMA:RING:STOP");
        }
        commands.Add("READ:SEQ:REPEAT?");
        commands.Add("READ:IO:STAT?");
        if (command != "TRIG:SEQ:NEXT?")
        {
            commands.Add("TRIG:SEQ:NEXT?");
        }
        if (deviceCombined || _deviceMode is null || (command == "TRIG:START" && combined))
        {
            commands.Add("READ:SEQ:LINK?");
        }
        if (position)
        {
            commands.Add("READ:SEQ:COUNTER?");
            if (Bools["turntable_angle_enabled"])
            {
                commands.Add("READ:ANGLE:POSITION?");
            }
        }
        EnqueueCommands(commands);
    }

    public void SendManual()
    {
        if (_loopbackRunning)
        {
            Log("请先结束回环观察，再发送手动指令。", "WARN");
            return;
        }
        var command = ManualCommand.Trim();
        if (command.Length == 0 || !EnqueueCommands(new List<string> { command }))
        {
            return;
        }
        // Inspect every header rather than a '?' anywhere in parameters.
        var headers = command.Split(';', '\r', '\n')
            .Select(part => part.Trim())
            .Where(part => part.Length > 0)
            .Select(part => part.Split(' ')[0])
            .ToList();
        if (headers.Any(header => !header.EndsWith('?')))
        {
            InvalidateDeviceConfiguration();
        }
    }

    public void SetIndependentSwitch()
    {
        if (SequenceState is not ("UNKNOWN" or "IDLE"))
        {
            Log($"手动开关只能在序列未运行且资源空闲时切换（当前状态：{SequenceState}）。", "WARN");
            return;
        }
        EnqueueCommands(new List<string>
        {
            $"CONF:SWITCH1 {Fields["independent_switch"]}", "READ:SWITCH1?", "READ:IO:STAT?",
        });
    }

    public void ReadIndependentSwitch()
    {
        EnqueueCommands(new List<string> { "READ:SWITCH1?", "READ:IO:STAT?" });
    }

    /// <summary>SP2T on the IN3 pad (OUT0), addressed as switch 2.</summary>
    public void SetManualSp2t()
    {
        if (SequenceState is not ("UNKNOWN" or "IDLE"))
        {
            Log($"手动开关只能在序列未运行且资源空闲时切换（当前状态：{SequenceState}）。", "WARN");
            return;
        }
        EnqueueCommands(new List<string>
        {
            $"CONF:SWITCH2 {ManualSp2t}", "READ:SWITCH2?", "READ:IO:STAT?",
        });
    }

    public void ReadManualSp2t()
    {
        EnqueueCommands(new List<string> { "READ:SWITCH2?", "READ:IO:STAT?" });
    }

    /// <summary>
    /// Release the SMA outputs. The firmware refuses a sequence start while any
    /// output pin reads high (a leftover manual SP8T position) and reports that
    /// rejection without a reply, so this is the documented recovery action.
    /// </summary>
    public void ReleaseOutputs()
    {
        if (_loopbackRunning)
        {
            Log("请先结束回环观察，再释放输出。", "WARN");
            return;
        }
        Log("释放输出：TRIG:STOP → CONF:SWITCH1 1；随后读回开关位置与 IO 状态。", "WARN");
        EnqueueCommands(new List<string>
        {
            "TRIG:STOP", "CONF:SWITCH1 1", "READ:SWITCH1?", "READ:IO:STAT?", "TRIG:SEQ:NEXT?",
        });
    }

    public void ReadCounterHistory()
    {
        if (_counterHistoryPending)
        {
            return;
        }
        _counterHistoryPending = true;
        if (!EnqueueCommands(new List<string> { "READ:SEQ:COUNTER?" }))
        {
            _counterHistoryPending = false;
            return;
        }
        CounterHistory = "正在读取最近一条记录…";
        CounterHistoryRequested?.Invoke();
    }

    private void ReadingOperatingCatalogOnce()
    {
        if (_operatingLabels.Count > 0 || _operatingCatalogPending)
        {
            return;
        }
        if (Fields["port"].Trim().Length == 0)
        {
            return;
        }
        _operatingCatalogPending = true;
        if (!EnqueueCommands(new List<string> { SequenceConstants.OperatingCatalogQuery }))
        {
            _operatingCatalogPending = false;
        }
    }

    /// <summary>First user touch of the profile control asks the device for its ladder.</summary>
    public void RequestOperatingCatalog() => ReadingOperatingCatalogOnce();

    // -------------------------------------------------------------- ports

    public void RefreshPorts(bool logResult = true)
    {
        var backend = BackendNames.Parse(Fields["backend"]);
        IReadOnlyList<string> ports;
        try
        {
            ports = ResourceDiscovery.Discover(backend);
        }
        catch (Exception exception)
        {
            ports = Array.Empty<string>();
            if (logResult)
            {
                Log($"{Fields["backend"]} 资源扫描失败：{exception.Message}", "ERROR");
            }
        }

        Resources.Clear();
        foreach (var port in ports)
        {
            Resources.Add(port);
        }

        var current = Fields["port"].Trim();
        if (ports.Count > 0 && !ports.Contains(current))
        {
            Fields["port"] = ports[0];
        }

        var kind = backend == Backend.UsbTmc ? "USBTMC 资源" : "串口";
        Status = ports.Count > 0 ? $"发现 {ports.Count} 个{kind}" : $"未发现{kind}";
        if (logResult)
        {
            Log($"{kind}扫描：" + (ports.Count > 0 ? string.Join(", ", ports) : "未发现设备"));
        }
    }

    // ------------------------------------------------------------ USB mode

    public Func<string, string, bool>? ConfirmRequest { get; set; }
    public Action<string, string>? WarnRequest { get; set; }

    public void SwitchUsbMode(string mode)
    {
        if (_loopbackRunning)
        {
            Log("请先结束回环观察，再切换 USB 模式。", "WARN");
            return;
        }
        if (_otaRunning || _transportSwitching)
        {
            Log("设备维护正在占用通信资源，请稍后切换 USB 模式。", "WARN");
            return;
        }
        if (ConfirmRequest is not null &&
            !ConfirmRequest("确认切换 USB 模式",
                $"设备将保存 {mode} 模式并重启 USB。当前连接会断开，是否继续？"))
        {
            return;
        }

        Log($"准备切换 USB 模式：{mode}", "WARN");
        var resource = Fields["port"].Trim();
        if (resource.Length == 0)
        {
            Log("未选择当前通信资源，无法切换 USB 模式。", "ERROR");
            return;
        }

        InvalidateDeviceConfiguration();
        _transportSwitching = true;
        Status = $"正在切换到 {mode}";
        var backend = Fields["backend"];
        _queue.Post(() => RunUsbSwitch(backend, resource, mode));
    }

    public void FinishUsbModeSwitch(bool passed, string backend, string message)
    {
        _transportSwitching = false;
        Log(message, passed ? "OK" : "ERROR");
        if (passed)
        {
            Fields["backend"] = backend;
            Status = "等待 USB 重新枚举";
            _dispatcher.BeginInvoke(async () =>
            {
                await Task.Delay(3500);
                RefreshPorts();
            });
        }
        else
        {
            Status = "USB 模式切换失败";
        }
        UsbModeFinished?.Invoke(message, passed, backend);
    }

    // ----------------------------------------------------------------- OTA

    public void StartOta()
    {
        if (_loopbackRunning)
        {
            Log("请先结束回环观察，再进行 OTA。", "WARN");
            return;
        }
        if (_otaRunning || _transportSwitching)
        {
            Log("设备维护正在占用通信资源，请稍后升级。", "WARN");
            return;
        }

        var image = OtaFile;
        var port = Fields["port"].Trim();
        if (image.Length == 0 || !File.Exists(image))
        {
            WarnRequest?.Invoke("固件不可用", "请选择有效的 .pkg 或 .bin 固件文件。");
            return;
        }
        if (port.Length == 0)
        {
            WarnRequest?.Invoke("串口不可用", "请先扫描并选择目标串口。");
            return;
        }
        if (ConfirmRequest is not null &&
            !ConfirmRequest("确认单板 OTA", $"将通过 {port} 更新当前识别到的一块 DHRT100，是否继续？"))
        {
            return;
        }

        InvalidateDeviceConfiguration();
        _otaRunning = true;
        OtaStatus = "升级中";
        OnPropertyChanged(nameof(CanSelectOta));
        OnPropertyChanged(nameof(OtaProgressVisibility));
        Log($"OTA 开始：{Path.GetFileName(image)} -> {port}", "OTA");
        var backend = Fields["backend"];
        _queue.Post(() => RunOta(image, port, backend));
    }

    public void FinishOta(bool passed, string message)
    {
        _otaRunning = false;
        OnPropertyChanged(nameof(CanSelectOta));
        OnPropertyChanged(nameof(OtaProgressVisibility));
        OtaStatus = passed ? "升级完成" : "升级失败";
        Log(message, passed ? "OK" : "ERROR");
        _dispatcher.BeginInvoke(async () =>
        {
            await Task.Delay(1200);
            RefreshPorts();
        });
        OtaFinished?.Invoke(passed, message);
    }

    // --------------------------------------------------------- angle / rate

    public AngleScan AngleDraft()
    {
        return AngleScanCalculator.Calculate(
            Fields["turntable_angle_start"], Fields["turntable_angle_stop"],
            Fields["turntable_angle_step"], Fields["turntable_angle_speed"],
            Fields["turntable_angle_ppd"]);
    }

    public void PreviewTurntableAngle()
    {
        try
        {
            var scan = AngleDraft();
            TurntableAngleCount = scan.Positions.ToString();
            TurntableAngleThreshold = scan.Threshold.ToString();
            TurntableAnglePreview =
                $"位置数 {scan.Positions}（含起止） · 每位置 N={scan.Threshold} 个脉冲\n" +
                $"输入频率 {scan.Frequency:G8} Hz · 脉冲周期 {1000m / scan.Frequency:G8} ms\n" +
                $"位置周期 {scan.Period:G8} s · 位置频率 {1m / scan.Period:G8} Hz\n" +
                $"启动后累计首个 N 脉冲对应开始角度 {scan.Start}°；每位置执行整轮 SP8T。";
        }
        catch (ValueError exception)
        {
            TurntableAngleCount = "—";
            TurntableAngleThreshold = "—";
            TurntableAnglePreview = exception.Message;
        }
    }

    public void PreviewTurntableRate()
    {
        try
        {
            var (threshold, frequency, seconds) =
                PositionThreshold.Calculate(Fields["turntable_rate_kind"], Fields["turntable_rate_value"]);
            TurntableRatePreview =
                $"频率 {frequency:G6} Hz · 脉冲周期 {1000m / frequency:G6} ms\n" +
                $"目标：每 1 秒一个位置 → N={threshold}\n" +
                $"按整数脉冲取最近阈值，预计位置间隔 {seconds:G6} 秒。";
        }
        catch (ValueError exception)
        {
            TurntableRatePreview = exception.Message;
        }
    }

    public void ApplyTurntableRate()
    {
        if (Bools["turntable_angle_enabled"])
        {
            TurntableRatePreview = "当前启用了角度扫描；请先关闭角度扫描，再使用原始阈值换算。";
            return;
        }
        try
        {
            var (threshold, _, _) =
                PositionThreshold.Calculate(Fields["turntable_rate_kind"], Fields["turntable_rate_value"]);
            Fields["turntable_threshold"] = threshold.ToString();
            PreviewTurntableRate();
            Log($"每秒位置阈值已填入 N={threshold}；请点击“配置此模式”下发。");
        }
        catch (ValueError exception)
        {
            TurntableRatePreview = exception.Message;
        }
    }

    // -------------------------------------------------------------- loopback

    public void ApplyLoopbackPreset(string mode)
    {
        if (_loopbackRunning)
        {
            Log("回环观察正在运行，请先停止。", "WARN");
            return;
        }

        (string Plan, string Codes, string Settle, string Pulse, string Repeat, string Edge) keys;
        IReadOnlyList<OutSlot> slots;
        if (mode == SequenceConstants.ModeIndependent)
        {
            keys = ("plan", "codes", "settle", "pulse", "repeat_count", "edge");
            Fields["source"] = "IN1";
            Fields["status_mode"] = "脉冲";
            slots = OutSlots;
        }
        else if (mode == SequenceConstants.ModeRj45)
        {
            keys = ("gateway_plan", "gateway_codes", "gateway_settle", "gateway_pulse",
                    "gateway_repeat_count", "gateway_edge");
            Fields["gateway_ready_input"] = "IN1";
            Fields["gateway_timeout"] = "5000";
            slots = GatewayOutSlots;
        }
        else
        {
            keys = ("turntable_plan", "turntable_codes", "turntable_settle", "turntable_pulse",
                    "turntable_repeat_count", "turntable_edge");
            Fields["turntable_ready_input"] = "IN2";
            Fields["turntable_input"] = "IN1";
            Fields["turntable_timeout"] = "5000";
            slots = TurntableOutSlots;
        }

        Fields[keys.Plan] = "SP8T";
        // Full 0..15 plan: bit0 is the SP2T polarization, bits1..3 the SP8T
        // position, so SP8T is the outer loop and H/V the inner one.
        Fields[keys.Codes] = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15";
        Fields[keys.Settle] = "10000";
        Fields[keys.Pulse] = "100";
        Fields[keys.Repeat] = "10";
        Fields[keys.Edge] = "RIS";
        var secondary = mode == SequenceConstants.ModeIndependent
            ? SequenceConstants.RoleStatus
            : SequenceConstants.RoleGateway;
        for (var index = 0; index < slots.Count; index++)
        {
            slots[index].Enabled = true;
            slots[index].Role = index < 3 ? SequenceConstants.RoleSequence : secondary;
        }

        SetLoopbackResult(mode,
            mode == SequenceConstants.ModeTurntable && Bools["turntable_angle_enabled"]
                ? "回环预设已填入；保留角度扫描的位置数/阈值，延时 10000 µs，脉宽 100 µs。尚未下发。"
                : "预设已填入，尚未下发；10 轮/位置，延时 10000 µs，脉宽 100 µs。");
        UpdateModeHint();
        UpdateStatusMode();
    }

    public void StartLoopback(string mode)
    {
        if (_loopbackRunning || _otaRunning || _transportSwitching)
        {
            Log("通信任务正在运行，请稍后重试。", "WARN");
            return;
        }
        if (_configuredMode != mode || _configuredResource != ResourceKey())
        {
            SetLoopbackResult(mode, "请先配置此模式，再启动观察。");
            return;
        }
        if (!int.TryParse(SelectedLoopbackDuration(mode), out var duration) || duration is < 1 or > 600)
        {
            SetLoopbackResult(mode, "观察时长须为 1–600 秒整数。");
            return;
        }

        var (backend, resource) = ResourceKey();
        _loopbackRunning = true;
        _loopbackTarget = (backend, resource, mode);
        _loopbackTargets[mode] = _loopbackTarget.Value;
        _loopbackCancel = new CancellationTokenSource();
        SetLoopbackResult(mode, "正在启动并观察…");
        var cancel = _loopbackCancel;
        _queue.Post(() => RunLoopback(backend, resource, mode, duration, cancel));
    }

    public void StopLoopback(string mode)
    {
        if (!_loopbackTargets.TryGetValue(mode, out var target))
        {
            CommandMode(mode, "TRIG:STOP");
            return;
        }
        if (_loopbackTarget is not null &&
            (_loopbackTarget.Value.Backend, _loopbackTarget.Value.Resource) ==
            (target.Backend, target.Resource))
        {
            _loopbackCancel?.Cancel();
        }
        var commands = new List<string>
        {
            "TRIG:STOP", "SYST:TDMA:RING:STOP", "TRIG:SEQ:NEXT?", "READ:IO:STAT?",
        };
        EnqueueFor(target.Backend, target.Resource, commands);
    }

    public void SetLoopbackResult(string mode, string result)
    {
        if (!_dispatcher.CheckAccess())
        {
            _dispatcher.BeginInvoke(() => SetLoopbackResult(mode, result));
            return;
        }
        _loopbackResults[mode] = result;
        LoopbackResultChanged?.Invoke(mode, result);
    }

    // -------------------------------------------------------------- settings

    public void LoadSettings()
    {
        if (_settingsPath is null)
        {
            return;
        }

        Dictionary<string, JsonNode?> values;
        try
        {
            values = SettingsStore.Load(_settingsPath);
        }
        catch (Exception exception) when (exception is IOException or ValueError
                                          or UnauthorizedAccessException)
        {
            Log($"本地配置读取失败，使用默认参数：{exception.Message}", "WARN");
            return;
        }

        var invalid = new List<string>();
        _loading = true;
        try
        {
            foreach (var name in TextFieldNames)
            {
                if (!values.TryGetValue(name, out var node) || node is null)
                {
                    continue;
                }
                if (node is not JsonValue value || !value.TryGetValue<string>(out var text) ||
                    text.Length > 4096 || text.Contains('\r') || text.Contains('\n') ||
                    text.Contains('\0') ||
                    (Enumerations.TryGetValue(name, out var allowed) && !allowed.Contains(text)))
                {
                    invalid.Add(name);
                    continue;
                }
                Fields[name] = text;
            }

            foreach (var name in BoolFieldNames)
            {
                if (!values.TryGetValue(name, out var node) || node is null)
                {
                    continue;
                }
                if (node is JsonValue value && value.TryGetValue<bool>(out var flag))
                {
                    Bools[name] = flag;
                }
                else
                {
                    invalid.Add(name);
                }
            }

            foreach (var (name, slots) in ArrayFields())
            {
                if (!values.TryGetValue(name, out var node) || node is not JsonArray array ||
                    array.Count != 4)
                {
                    if (values.ContainsKey(name))
                    {
                        invalid.Add(name);
                    }
                    continue;
                }

                var enabled = name.EndsWith("enabled", StringComparison.Ordinal);
                var roles = name == "out_roles"
                    ? new[] { SequenceConstants.RoleSequence, SequenceConstants.RoleStatus }
                    : new[] { SequenceConstants.RoleSequence, SequenceConstants.RoleGateway };
                var items = new List<object>();
                var valid = true;
                foreach (var item in array)
                {
                    if (enabled)
                    {
                        if (item is JsonValue flagValue && flagValue.TryGetValue<bool>(out var flag))
                        {
                            items.Add(flag);
                            continue;
                        }
                        valid = false;
                        break;
                    }
                    if (item is JsonValue textValue && textValue.TryGetValue<string>(out var text) &&
                        roles.Contains(text))
                    {
                        items.Add(text);
                        continue;
                    }
                    valid = false;
                    break;
                }
                if (!valid)
                {
                    invalid.Add(name);
                    continue;
                }
                for (var index = 0; index < 4; index++)
                {
                    if (enabled)
                    {
                        slots[index].Enabled = (bool)items[index];
                    }
                    else
                    {
                        slots[index].Role = (string)items[index];
                    }
                }
            }

            if (values.TryGetValue("loopback_durations", out var durations) && durations is JsonObject map)
            {
                foreach (var mode in _loopbackDurations.Keys.ToList())
                {
                    if (!map.TryGetPropertyValue(mode, out var node) || node is not JsonValue value ||
                        !value.TryGetValue<string>(out var text) || text.Length > 3 ||
                        !text.All(char.IsAsciiDigit) || !int.TryParse(text, out var seconds) ||
                        seconds is < 1 or > 600)
                    {
                        if (map.ContainsKey(mode))
                        {
                            invalid.Add("loopback_durations");
                        }
                        continue;
                    }
                    _loopbackDurations[mode] = text;
                }
            }
            else if (values.ContainsKey("loopback_durations"))
            {
                invalid.Add("loopback_durations");
            }

            if (values.TryGetValue("selected_tab", out var tab) && tab is JsonValue tabValue &&
                tabValue.TryGetValue<string>(out var tabName))
            {
                var index = Array.IndexOf(TabNames, tabName);
                if (index >= 0)
                {
                    SelectedTabIndex = index;
                }
                else
                {
                    invalid.Add("selected_tab");
                }
            }
            else if (values.ContainsKey("selected_tab"))
            {
                invalid.Add("selected_tab");
            }
        }
        finally
        {
            _loading = false;
        }

        // A stored level number predates the catalog labels; the live profile
        // readback names the field instead of leaving a bare index on screen.
        var draft = Fields["operating_level"].Trim();
        if (draft.Length > 0 && draft.All(char.IsAsciiDigit))
        {
            Fields["operating_level"] = string.Empty;
            Log("旧档位草稿（纯档号）已忽略；挡位将按设备档位表名称显示。");
        }
        else if (draft.Length > 0)
        {
            var match = System.Text.RegularExpressions.Regex.Match(
                draft, @"^(\d+(?:\.\d+)?)MHz / ");
            if (!match.Success ||
                !decimal.TryParse(match.Groups[1].Value, out var frequency) ||
                frequency * 1_000_000m < SequenceConstants.OperatingGuiMinBaudHz ||
                frequency * 1_000_000m > SequenceConstants.OperatingGuiMaxBaudHz)
            {
                Fields["operating_level"] = string.Empty;
                Log("范围外档位草稿已忽略；GUI 仅提供 10–30 MHz 挡位。");
            }
        }

        UpdateStatusMode();
        UpdateModeHint();
        PreviewTurntableAngle();
        PreviewTurntableRate();

        if (invalid.Count > 0)
        {
            Log("本地配置中无效字段已忽略：" +
                string.Join(", ", invalid.Distinct().OrderBy(name => name, StringComparer.Ordinal)), "WARN");
        }
        if (values.Count > 0)
        {
            Log("已载入本地参数草稿；设备配置尚未下发。");
        }
    }

    private IEnumerable<(string Name, IReadOnlyList<OutSlot> Slots)> ArrayFields()
    {
        yield return ("out_enabled", OutSlots);
        yield return ("out_roles", OutSlots);
        yield return ("gateway_out_enabled", GatewayOutSlots);
        yield return ("gateway_out_roles", GatewayOutSlots);
        yield return ("turntable_out_enabled", TurntableOutSlots);
        yield return ("turntable_out_roles", TurntableOutSlots);
    }

    public void SaveSettings()
    {
        if (_settingsPath is null)
        {
            return;
        }

        var values = new Dictionary<string, JsonNode?>(StringComparer.Ordinal);
        foreach (var name in TextFieldNames)
        {
            values[name] = JsonValue.Create(Fields[name]);
        }
        foreach (var name in BoolFieldNames)
        {
            values[name] = JsonValue.Create(Bools[name]);
        }
        foreach (var (name, slots) in ArrayFields())
        {
            var array = new JsonArray();
            var enabled = name.EndsWith("enabled", StringComparison.Ordinal);
            foreach (var slot in slots)
            {
                array.Add(enabled ? JsonValue.Create(slot.Enabled) : JsonValue.Create(slot.Role));
            }
            values[name] = array;
        }
        var durations = new JsonObject();
        foreach (var pair in _loopbackDurations)
        {
            durations[pair.Key] = JsonValue.Create(pair.Value);
        }
        values["loopback_durations"] = durations;
        values["selected_tab"] =
            JsonValue.Create(TabNames[Math.Clamp(SelectedTabIndex, 0, TabNames.Length - 1)]);

        try
        {
            SettingsStore.Save(_settingsPath, values);
        }
        catch (Exception exception) when (exception is IOException or ValueError
                                          or UnauthorizedAccessException)
        {
            Log($"本地配置保存失败：{exception.Message}", "WARN");
            WarnRequest?.Invoke("配置未保存",
                $"无法保存本次参数：{exception.Message}\n配置文件：{_settingsPath}");
        }
    }

    /// <summary>Save drafts and stop the worker; called once from the window's Closing handler.</summary>
    public void PrepareShutdown()
    {
        SaveSettings();
        _loopbackCancel?.Cancel();
        _closing = true;
        _queue.Complete();
        _queue.Dispose();
    }

    public bool CanClose => !_otaRunning;
}
