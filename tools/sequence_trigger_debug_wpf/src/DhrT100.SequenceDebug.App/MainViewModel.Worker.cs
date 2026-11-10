using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using DhrT100.SequenceDebug.Core;
using DhrT100.SequenceDebug.Transport;

namespace DhrT100.SequenceDebug.App;

public sealed partial class MainViewModel
{
    private sealed record TranscriptRecord(string At, string Command, string Response);

    /// <summary>Queue a command list for the single-owner device worker.</summary>
    private void EnqueueFor(string backend, string resource, List<string> commands)
    {
        _queue.Post(() => RunCommands(backend, resource, commands));
    }

    public bool EnqueueCommands(List<string> commands)
    {
        if (_otaRunning || _transportSwitching)
        {
            var owner = _otaRunning ? "OTA" : "USB 模式切换";
            Log($"{owner} 正在独占通信资源，当前命令未加入队列。", "WARN");
            return false;
        }
        EnqueueFor(Fields["backend"], Fields["port"].Trim(), commands);
        return true;
    }

    /// <summary>
    /// Open the link, run one batch (or one observation), and report the outcome
    /// on the UI thread. Every device error ends up in the log, never as an
    /// unhandled exception on the worker.
    /// </summary>
    private bool RunCommands(string backend, string resource, List<string> commands,
                             string? observeMode = null, int observeDuration = 10,
                             (string Action, IReadOnlyDictionary<string, object?> Payload)? budget = null,
                             Action<Budget.OperationResult?, string?>? budgetOutcome = null,
                             CancellationToken? cancel = null)
    {
        var transcript = new List<TranscriptRecord>();
        void Emit(string command, string response)
        {
            transcript.Add(new TranscriptRecord(DateTime.Now.ToString("O"), command, response));
            _dispatcher.BeginInvoke(() => LogExchange(command, response, (backend, resource)));
        }

        try
        {
            // A USBTMC configuration write costs ~2 s on this firmware (the reply
            // only arrives with the transfer end), so TMC needs a wider window than
            // CDC before a missing reply is reported as a timeout.
            var timeoutMs = BackendNames.Parse(backend) == Backend.UsbTmc ? 5000 : 2000;
            using var link = ScpiLink.Open(BackendNames.Parse(backend), resource, timeoutMs);
            string Exchange(string command) => link.Exchange(command);

            if (budget is { } request)
            {
                try
                {
                    var result = Budget.RunOperation(request.Action, request.Payload, Exchange, Emit,
                        () => new BatchExecutor(Exchange, Emit).Execute(
                            new List<string> { "TRIG:STOP", "SYST:TDMA:RING:STOP" }));
                    budgetOutcome?.Invoke(result, null);
                }
                catch (Exception exception)
                {
                    budgetOutcome?.Invoke(null, exception.Message);
                    throw;
                }
            }
            else if (observeMode is null)
            {
                new BatchExecutor(Exchange, Emit, drainErrorsFirst: true).Execute(commands);
            }
            else
            {
                var result = LoopbackObserver.Observe(
                    observeMode, Exchange, Emit, observeDuration,
                    cancelled: () => cancel?.IsCancellationRequested ?? false);
                SetLoopbackResult(observeMode, result);
                Log(result, "INFO");
            }
            return true;
        }
        catch (OperationCanceledException)
        {
            if (observeMode is not null)
            {
                SetLoopbackResult(observeMode, "观察已取消；停止命令按队列执行。");
            }
            return false;
        }
        catch (Exception exception)
        {
            if (observeMode is not null)
            {
                SetLoopbackResult(observeMode, $"观察失败：{exception.Message}");
            }
            Log($"命令执行失败：{exception.Message}", "ERROR");
            _dispatcher.BeginInvoke(() => Status = "执行失败，请检查日志");
            return false;
        }
        finally
        {
            if (observeMode is not null)
            {
                SaveTranscript(observeMode, resource, transcript);
                _dispatcher.BeginInvoke(() =>
                {
                    _loopbackRunning = false;
                    _loopbackTarget = null;
                });
            }
        }
    }

    private void RunLoopback(string backend, string resource, string mode, int duration,
                             CancellationTokenSource cancel)
    {
        RunCommands(backend, resource, new List<string>(), mode, duration, cancel: cancel.Token);
    }

    private void SaveTranscript(string mode, string resource, List<TranscriptRecord> transcript)
    {
        try
        {
            var root = RepositoryRoot.Find() ?? Environment.CurrentDirectory;
            var folder = Path.Combine(root, "out", "sequence-loopback");
            Directory.CreateDirectory(folder);
            var path = Path.Combine(folder, $"observe-{DateTime.Now:yyyyMMdd-HHmmss-fff}.json");
            var payload = new
            {
                mode,
                resource,
                transcript = transcript.Select(record => new
                {
                    at = record.At,
                    command = record.Command,
                    response = record.Response,
                }),
            };
            File.WriteAllText(path, JsonSerializer.Serialize(payload,
                new JsonSerializerOptions { WriteIndented = true }));
            Log($"回环记录：{path}", "INFO");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Log($"回环记录保存失败：{exception.Message}", "ERROR");
        }
    }

    // ------------------------------------------------------------ USB switch

    private void RunUsbSwitch(string backend, string resource, string mode)
    {
        var target = mode == "USBTMC" ? BackendNames.UsbTmc : BackendNames.Serial;

        void Emit(string command, string response)
        {
            _dispatcher.BeginInvoke(() => LogExchange(command, response, (backend, resource)));
        }

        try
        {
            using var link = ScpiLink.Open(BackendNames.Parse(backend), resource, 2500);
            var currentResponse = link.Exchange("SYST:USB:MODE?");
            Emit("SYST:USB:MODE?", currentResponse);
            if (SequenceConstants.IsTimeout(currentResponse))
            {
                // The command table only carries SYSTem:USB:MODE* when
                // PROJECT_ENABLE_USB_RUNTIME_SWITCH is compiled in; otherwise the
                // header is undefined and the device never answers.
                throw new RuntimeError(
                    "当前固件未实现 SYST:USB:MODE?（PROJECT_ENABLE_USB_RUNTIME_SWITCH 未编译）。" +
                    "请先烧录 pico2-usb-runtime-switch 构建，再切换 USB 模式。");
            }
            var current = StatusFormat.ParseUsbMode(currentResponse);
            if (current != mode)
            {
                var setResponse = link.Exchange($"SYST:USB:MODE {mode}");
                Emit($"SYST:USB:MODE {mode}", setResponse);
                var verifyResponse = link.Exchange("SYST:USB:MODE?");
                Emit("SYST:USB:MODE?", verifyResponse);
                if (StatusFormat.ParseUsbMode(verifyResponse) != mode)
                {
                    throw new RuntimeError("USB 模式写入后读回不一致");
                }

                try
                {
                    var bootResponse = link.Exchange("SYST:USB:BOOT");
                    Emit("SYST:USB:BOOT", bootResponse);
                }
                catch (Exception exception)
                {
                    // The device reboots USB before acknowledging. CDC closes the
                    // handle (IOException/OperationCanceledException) and USBTMC
                    // returns VI_ERROR_SYSTEM_ERROR; both are expected here.
                    Emit("SYST:USB:BOOT", $"设备已重启 USB（{exception.Message}）");
                }
            }

            var message = current == mode
                ? $"USB 已处于 {mode} 模式。"
                : $"已写入 {mode} 并请求重启，等待设备重新枚举。";
            _dispatcher.BeginInvoke(() => FinishUsbModeSwitch(true, target, message));
        }
        catch (Exception exception)
        {
            _dispatcher.BeginInvoke(() => FinishUsbModeSwitch(false, backend, exception.Message));
        }
    }

    // ------------------------------------------------------------------- OTA

    private void RunOta(string image, string port, string backend)
    {
        var root = RepositoryRoot.Find();
        var outDir = Path.Combine(root ?? Environment.CurrentDirectory, "out", "sequence-debug-ui",
            $"ota-{DateTime.Now:yyyyMMdd-HHmmss}");
        var lines = new List<string>();
        try
        {
            Directory.CreateDirectory(outDir);
            var python = Environment.GetEnvironmentVariable("DHRT100_PYTHON") ?? "python";
            var script = backend == BackendNames.UsbTmc
                ? Path.Combine(root ?? ".", "tools", "visa_ota_send", "visa_ota_send.py")
                : Path.Combine(root ?? ".", "tools", "ota_multi_update", "ota_multi_update.py");

            var startInfo = new ProcessStartInfo(python)
            {
                WorkingDirectory = root ?? Environment.CurrentDirectory,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };
            if (backend == BackendNames.UsbTmc)
            {
                startInfo.ArgumentList.Add(script);
                startInfo.ArgumentList.Add(port);
                startInfo.ArgumentList.Add(image);
                startInfo.ArgumentList.Add("--boot");
            }
            else
            {
                startInfo.ArgumentList.Add(script);
                startInfo.ArgumentList.Add(image);
                startInfo.ArgumentList.Add("--ports");
                startInfo.ArgumentList.Add(port);
                startInfo.ArgumentList.Add("--expected-board-count");
                startInfo.ArgumentList.Add("1");
                startInfo.ArgumentList.Add("--max-workers");
                startInfo.ArgumentList.Add("1");
                startInfo.ArgumentList.Add("--out-dir");
                startInfo.ArgumentList.Add(outDir);
            }

            using var process = Process.Start(startInfo) ?? throw new RuntimeError("无法启动 OTA 工具");
            void OnLine(object sender, DataReceivedEventArgs args)
            {
                if (string.IsNullOrWhiteSpace(args.Data))
                {
                    return;
                }
                lock (lines)
                {
                    lines.Add(args.Data);
                }
                Log(args.Data, "OTA");
            }

            process.OutputDataReceived += OnLine;
            process.ErrorDataReceived += OnLine;
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();

            lock (lines)
            {
                File.WriteAllText(Path.Combine(outDir, "ota.log"), string.Join("\n", lines) + "\n");
            }
            if (process.ExitCode != 0)
            {
                throw new RuntimeError($"OTA 工具退出码 {process.ExitCode}");
            }
            _dispatcher.BeginInvoke(() => FinishOta(true, $"升级完成，证据：{outDir}"));
        }
        catch (Exception exception)
        {
            _dispatcher.BeginInvoke(() => FinishOta(false, $"升级失败：{exception.Message}"));
        }
    }

    // ----------------------------------------------------------------- budget

    /// <summary>Submit one budget action; the dialog receives the outcome asynchronously.</summary>
    public void SubmitBudget(string action, IReadOnlyDictionary<string, object?> payload,
                            Action<Budget.OperationResult?, string?> sink)
    {
        if (_otaRunning || _transportSwitching || _loopbackRunning)
        {
            sink(null, "请先结束设备维护或回环观察，再调节预算。");
            return;
        }
        var (backend, resource) = ResourceKey();
        if (resource.Length == 0)
        {
            sink(null, "请先选择设备连接。");
            return;
        }
        if (action is "apply" or "save")
        {
            _configuredMode = null;
            _configuredResource = null;
            _configurationGeneration++;
        }

        var generation = _budgetGeneration;
        var key = (backend, resource);
        _queue.Post(() => RunCommands(backend, resource, new List<string>(),
            budget: (action, payload),
            budgetOutcome: (result, error) => _dispatcher.BeginInvoke(() =>
            {
                if (key != ResourceKey() || generation != _budgetGeneration)
                {
                    return;
                }
                sink(result, error);
            })));
    }

    // ------------------------------------------------------------- IO update

    public void UpdateIo(string command, string response)
    {
        var upper = command.ToUpperInvariant();

        // This build of the firmware may not implement the runtime USB switch at
        // all (SYST:USB:MODE? is absent and the queue reports -113 later).
        if (upper.StartsWith("SYST:USB:MODE?", StringComparison.Ordinal) &&
            SequenceConstants.IsTimeout(response))
        {
            Log("当前固件未实现 SYST:USB:MODE?（未启用 PROJECT_ENABLE_USB_RUNTIME_SWITCH）。" +
                "要使用 USB TMC，请先烧录 pico2-usb-runtime-switch 构建（PROJECT_USB_DEFAULT_MODE=CDC），" +
                "再用本页的“切到 USB TMC”。", "WARN");
            return;
        }

        if (upper.StartsWith("SYST:TDMA:OPMODE:CATALOG?", StringComparison.Ordinal))
        {
            _operatingCatalogPending = false;
            try
            {
                var entries = OperatingProfile.ParseCatalog(response);
                var labels = OperatingProfile.CatalogLabels(entries);
                _operatingLabels = labels;
                ReplaceOperatingChoices();
                OperatingLabel = $"TDMA 挡位（10–30 MHz，{labels.Count} 档）";
                var draft = Fields["operating_level"].Trim();
                if (draft.Length > 0 && draft.All(char.IsAsciiDigit))
                {
                    var numeric = int.Parse(draft);
                    var match = labels.FirstOrDefault(pair => pair.Value == numeric);
                    Fields["operating_level"] = match.Key ?? string.Empty;
                }
                else if (draft.Length > 0 && !labels.ContainsKey(draft))
                {
                    Fields["operating_level"] = string.Empty;
                }
                Log(OperatingProfile.FormatCatalog(entries
                    .Where(entry => entry.BaudHz >= SequenceConstants.OperatingGuiMinBaudHz &&
                                    entry.BaudHz <= SequenceConstants.OperatingGuiMaxBaudHz)
                    .ToList()));
            }
            catch (Exception exception) when (exception is ValueError or FormatException)
            {
                Log("TDMA 档位表读回失败，档位草稿保持原值。", "WARN");
            }
            return;
        }

        if (upper.StartsWith("SYST:TDMA:OPMODE?", StringComparison.Ordinal))
        {
            try
            {
                var text = OperatingProfile.FormatStatus(response);
                OperatingStatus = text;
                Log(text, text.Contains("失败", StringComparison.Ordinal) ? "WARN" : "INFO");
                var draft = Fields["operating_level"].Trim();
                if (_operatingLabels.Count > 0 && (draft.Length == 0 || draft.All(char.IsAsciiDigit)))
                {
                    var active = response.Split(',').Take(3).Select(Csv.ParseUnsigned).ToArray();
                    if (active.Length == 3)
                    {
                        var label = OperatingProfile.LevelLabel(active[1], active[2]);
                        if (_operatingLabels.ContainsKey(label))
                        {
                            Fields["operating_level"] = label;
                        }
                    }
                }
            }
            catch (Exception exception) when (exception is ValueError or FormatException)
            {
                OperatingStatus = "TDMA 挡位读回失败，请检查固件版本和命令日志";
            }
            return;
        }

        if (upper.StartsWith("READ:SEQ:COUNTER:HIST?", StringComparison.Ordinal))
        {
            try
            {
                CounterHistory = StatusFormat.FormatCounterHistory(response);
            }
            catch (Exception exception) when (exception is ValueError or FormatException)
            {
                CounterHistory = "历史记录不可用或已被覆盖，请重新读取最新记录。";
            }
            return;
        }

        if (upper.StartsWith("READ:SEQ:COUNTER?", StringComparison.Ordinal))
        {
            try
            {
                CounterStatus = StatusFormat.FormatCounterStatus(response);
                if (_counterHistoryPending)
                {
                    _counterHistoryPending = false;
                    var history = Csv.ParseUnsigned(Csv.ParseLine(response))[8];
                    if (history > 0)
                    {
                        if (!EnqueueCommands(new List<string> { $"READ:SEQ:COUNTER:HIST? {history}" }))
                        {
                            CounterHistory = "历史记录读取未排入队列，请重试。";
                        }
                    }
                    else
                    {
                        CounterHistory = "本次运行尚无切换记录。";
                    }
                }
            }
            catch (Exception exception) when (exception is ValueError or FormatException)
            {
                CounterStatus = "转台计数状态解析失败，请检查固件版本和命令日志";
                _counterHistoryPending = false;
                CounterHistory = "读取失败，请检查固件版本和命令日志。";
            }
            return;
        }

        if (upper.StartsWith("READ:SEQ:LINK?", StringComparison.Ordinal))
        {
            try
            {
                LinkStatus = StatusFormat.FormatLinkStatus(response, _devicePlanCount);
            }
            catch (Exception exception) when (exception is ValueError or FormatException)
            {
                LinkStatus = "RJ45 状态解析失败，请检查固件版本和命令日志";
            }
            return;
        }

        if (upper.StartsWith("READ:SEQ:REPEAT?", StringComparison.Ordinal))
        {
            try
            {
                var fields = Csv.SplitInts(response);
                if (fields.Length != 3)
                {
                    throw new ValueError("循环次数读回格式异常");
                }
                var configured = fields[0] == 0 ? "持续" : fields[0].ToString();
                var run = fields[1] == 0 ? "持续" : fields[1].ToString();
                RepeatStatus = $"循环次数：{configured} · 本次：{run} · " +
                               (fields[2] == 1 ? "已完成并停止" : "未完成");
            }
            catch (Exception exception) when (exception is ValueError or FormatException)
            {
                RepeatStatus = "循环次数读回失败";
            }
            return;
        }

        if (upper.StartsWith("READ:SWITCH1?", StringComparison.Ordinal))
        {
            try
            {
                var fields = Csv.SplitInts(response);
                Fields["independent_switch"] = fields[1].ToString();
                IndependentSwitchStatus = $"SP8T：第 {fields[1]} 位";
            }
            catch (Exception exception) when (exception is ValueError or FormatException
                                              or IndexOutOfRangeException)
            {
                IndependentSwitchStatus = "SP8T：读回失败";
            }
            return;
        }

        if (upper.StartsWith("READ:SWITCH2?", StringComparison.Ordinal))
        {
            try
            {
                var fields = Csv.SplitInts(response);
                var level = fields[1];
                var busy = fields.Length > 2 ? fields[2] : 0;
                ManualSp2t = level.ToString();
                ManualSp2tStatus = $"OUT0（SP2T）：{level}" + (busy != 0 ? " · 忙" : string.Empty);
            }
            catch (Exception exception) when (exception is ValueError or FormatException
                                              or IndexOutOfRangeException)
            {
                ManualSp2tStatus = "OUT0（SP2T）：读回失败";
            }
            return;
        }

        if (upper.StartsWith("TRIG:SEQ:NEXT?", StringComparison.Ordinal))
        {
            try
            {
                var fields = Csv.ParseLine(response);
                SequenceState = fields[0].Trim('"');
                SwitchPosition = StatusFormat.FormatSwitchPosition(response);
            }
            catch (Exception exception) when (exception is ValueError or FormatException
                                              or IndexOutOfRangeException or OverflowException)
            {
                SwitchPosition = "当前开关：状态解析失败";
            }
            return;
        }

        // The IO masks are the only small integer row read here; every other
        // reply carries uint32 fields that must not be parsed as Int32.
        if (!upper.StartsWith("READ:IO:STAT?", StringComparison.Ordinal))
        {
            return;
        }
        int[] values;
        try
        {
            values = response.Trim().Split(',').Select(part => int.Parse(part.Trim())).ToArray();
        }
        catch (Exception exception) when (exception is FormatException or OverflowException)
        {
            return;
        }

        if (values.Length == 5)
        {
            IoInputs = $"输入：0x{values[0]:X}";
            IoOutputs = $"输出：0x{values[1]:X}";
            IoOwned = $"占用：0x{values[2]:X}";
            IoState = $"已武装={values[3]} 忙={values[4]}";
            OutputReleaseHint = values[1] != 0 && SequenceState is "IDLE" or "UNKNOWN"
                ? $"输出未释放（掩码 0x{values[1]:X}）：独立 SP8T / 组合模式启动前请先“释放输出”"
                : string.Empty;
            for (var index = 0; index < InputLamps.Count; index++)
            {
                var high = (values[0] & (1 << index)) != 0;
                InputLamps[index].Set(high ? "高" : "低", high);
            }
            for (var index = 0; index < OutputLamps.Count; index++)
            {
                var high = (values[1] & (1 << index)) != 0;
                OutputLamps[index].Set(high ? "高" : "低", high);
            }
        }
    }
}

/// <summary>Finds the repository root so OTA subprocesses and evidence land in place.</summary>
public static class RepositoryRoot
{
    public static string? Find()
    {
        foreach (var start in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            var directory = new DirectoryInfo(start);
            while (directory is not null)
            {
                if (Directory.Exists(Path.Combine(directory.FullName, "tools")) &&
                    File.Exists(Path.Combine(directory.FullName, "CMakeLists.txt")))
                {
                    return directory.FullName;
                }
                directory = directory.Parent;
            }
        }
        return null;
    }
}
