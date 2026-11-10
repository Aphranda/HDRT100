using System.Diagnostics;
using System.Windows;
using System.Windows.Threading;
using DhrT100.SequenceDebug.Core;
using DhrT100.SequenceDebug.Transport;

namespace DhrT100.SequenceDebug.App;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs eventArgs)
    {
        base.OnStartup(eventArgs);
        var arguments = AppArguments.Parse(eventArgs.Args);
        if (arguments.Budget is { } budget)
        {
            RunBudgetProbe(budget, arguments);
            return;
        }
        if (arguments.Bench is { } bench)
        {
            RunBench(arguments, bench);
            return;
        }
        if (arguments.Configure is { } configure)
        {
            RunConfigure(arguments, configure);
            return;
        }
        if (arguments.Probe is { } probe)
        {
            Environment.ExitCode = RunProbe(probe.Backend, probe.Resource, probe.Commands);
            Shutdown(Environment.ExitCode);
            return;
        }
        if (arguments.SmokeTest)
        {
            RunSmokeTest(arguments);
            return;
        }

        var window = new MainWindow(arguments);
        MainWindow = window;
        window.Show();
    }

    /// <summary>
    /// <c>--budget &lt;Serial|"USB TMC"&gt; &lt;resource&gt;</c> runs the budget dialog's
    /// read transaction (capability preflight, snapshot, settled loop, status)
    /// without opening the window.
    /// </summary>
    private void RunBudgetProbe((Backend Backend, string Resource) target, AppArguments arguments)
    {
        try
        {
            using var link = ScpiLink.Open(target.Backend, target.Resource);
            Console.WriteLine($"# budget read via {BackendNames.Format(target.Backend)} {target.Resource}");
            var result = Budget.RunOperation("read", new Dictionary<string, object?>(),
                link.Exchange, (command, response) => Console.WriteLine($"{command} => {response}"),
                () => { });
            var table = result.Snapshot.Table;
            Console.WriteLine($"clock_hz={result.Snapshot.ClockHz} cycle={table.Cycle} " +
                              $"phases={table.Phases.Count} persisted={result.Persisted} " +
                              $"quarantined=0x{result.Snapshot.QuarantinedMask:X} " +
                              $"misses={result.Snapshot.ScheduleMisses}");
            for (var index = 0; index < table.Phases.Count; index++)
            {
                var phase = table.Phases[index];
                var windowUs = Budget.Microseconds(phase.End - phase.Start, result.Snapshot.ClockHz);
                var wcetUs = Budget.Microseconds(phase.Wcet, result.Snapshot.ClockHz);
                Console.WriteLine(
                    $"  {Budget.Phases[index],-16} {phase.Start,8}..{phase.End,-8} " +
                    $"wcet={phase.Wcet,-8} window={windowUs}us wcet={wcetUs}us");
            }
            Console.WriteLine("budget: 通过");
            Environment.ExitCode = 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"budget failed: {exception.Message}");
            Environment.ExitCode = 1;
        }
        Shutdown(Environment.ExitCode);
    }

    /// <summary>
    /// <c>--bench &lt;alias&gt;</c> replays 配置 → 启动 → 读回 → 停止 in one process, so the
    /// start gate sees the same session state the operator's click would create.
    /// </summary>
    private void RunBench(AppArguments arguments, string alias)
    {
        var dispatcher = Dispatcher.CurrentDispatcher;
        var viewModel = BuildViewModel(arguments);
        if (viewModel is null)
        {
            Shutdown(2);
            return;
        }

        var mode = MainViewModel.ModeOf(alias);
        var failed = 0;
        var clock = System.Diagnostics.Stopwatch.StartNew();
        Console.WriteLine($"# bench {alias} via {viewModel.Fields["backend"]} {viewModel.Fields["port"].Trim()}");
        TraceLog(viewModel, clock);

        dispatcher.BeginInvoke(() => viewModel.ConfigureMode(mode));
        PumpUntilSettled(viewModel, TimeSpan.FromSeconds(180));
        failed += Check(viewModel, "配置", "配置完成，可以启动");

        dispatcher.BeginInvoke(() => viewModel.CommandMode(mode, "TRIG:START"));
        Pump(TimeSpan.FromSeconds(5));
        failed += Check(viewModel, null, null);

        dispatcher.BeginInvoke(() => viewModel.CommandMode(mode, "TRIG:SEQ:NEXT?"));
        Pump(TimeSpan.FromSeconds(4));

        dispatcher.BeginInvoke(() => viewModel.CommandMode(mode, "TRIG:STOP"));
        PumpUntilSettled(viewModel, TimeSpan.FromSeconds(90));
        failed += Check(viewModel, "停止", null);

        foreach (var entry in viewModel.LogEntries)
        {
            Console.WriteLine($"[{entry.Level}] {entry.Text}");
        }
        var errors = viewModel.LogEntries.Count(entry => entry.Level == "ERROR");
        Console.WriteLine($"bench: {(failed == 0 && errors == 0 ? "通过" : "失败")}" +
                          $"（ERROR {errors} 条，耗时 {clock.Elapsed.TotalSeconds:F1}s）");
        Environment.ExitCode = failed == 0 && errors == 0 ? 0 : 1;
        Shutdown(Environment.ExitCode);
    }

    private static int Check(MainViewModel viewModel, string? expectedStatus, string? wanted)
    {
        if (wanted is not null && viewModel.Status != wanted)
        {
            Console.Error.WriteLine($"{expectedStatus ?? "step"}: 未达到 “{wanted}”，当前 “{viewModel.Status}”");
            return 1;
        }
        return 0;
    }

    /// <summary>Pump the dispatcher for a fixed period so queued UI callbacks run.</summary>
    private void Pump(TimeSpan duration)
    {
        var frame = new DispatcherFrame();
        var timer = new DispatcherTimer(duration, DispatcherPriority.Background,
            (_, _) => frame.Continue = false, Dispatcher.CurrentDispatcher);
        timer.Start();
        Dispatcher.PushFrame(frame);
        timer.Stop();
    }

    /// <summary>Pump until the connection status reaches a terminal configure/execute state.</summary>
    private void PumpUntilSettled(MainViewModel viewModel, TimeSpan timeout)
    {
        var started = DateTime.UtcNow;
        var frame = new DispatcherFrame();
        DispatcherTimer? timer = null;
        timer = new DispatcherTimer(TimeSpan.FromMilliseconds(200), DispatcherPriority.Background,
            (_, _) =>
            {
                var terminal = viewModel.Status is "配置完成，可以启动" or "配置失败，请检查日志"
                    or "执行失败，请检查日志";
                if (terminal || DateTime.UtcNow - started > timeout)
                {
                    timer?.Stop();
                    frame.Continue = false;
                }
            }, Dispatcher.CurrentDispatcher);
        timer.Start();
        Dispatcher.PushFrame(frame);
        timer.Stop();
    }

    private MainViewModel? BuildViewModel(AppArguments arguments)
    {
        var viewModel = new MainViewModel(Dispatcher.CurrentDispatcher, arguments.SettingsPath);
        viewModel.LoadSettings();
        if (arguments.Resource is { } resource)
        {
            viewModel.Fields["port"] = resource;
        }
        if (viewModel.Fields["port"].Trim().Length == 0)
        {
            viewModel.RefreshPorts(false);
        }
        if (viewModel.Fields["port"].Trim().Length == 0)
        {
            Console.Error.WriteLine("configure failed: 未选择通信资源（用 --resource 指定）");
            return null;
        }
        return viewModel;
    }

    /// <summary>Stream log lines with elapsed time, so a stall is localised to one exchange.</summary>
    private static void TraceLog(MainViewModel viewModel, System.Diagnostics.Stopwatch clock)
    {
        var last = -1;
        viewModel.LogAppended += () =>
        {
            while (last + 1 < viewModel.LogEntries.Count)
            {
                last++;
                Console.WriteLine($"[+{clock.Elapsed.TotalSeconds,6:F2}s] {viewModel.LogEntries[last].Text}");
            }
        };
    }

    /// <summary>
    /// <c>--configure &lt;independent|rj45|turntable&gt; [--resource ...]</c> replays the
    /// “配置此模式” button through the real view model, so a bench failure can be
    /// reproduced and read from the command line.
    /// </summary>
    private void RunConfigure(AppArguments arguments, string alias)
    {
        var viewModel = BuildViewModel(arguments);
        if (viewModel is null)
        {
            Environment.ExitCode = 2;
            Shutdown(2);
            return;
        }

        Console.WriteLine($"# configure {alias} via {viewModel.Fields["backend"]} {viewModel.Fields["port"].Trim()}");
        var clock = System.Diagnostics.Stopwatch.StartNew();
        TraceLog(viewModel, clock);
        Dispatcher.CurrentDispatcher.BeginInvoke(() => viewModel.ConfigureMode(MainViewModel.ModeOf(alias)));
        // USBTMC configuration writes cost ~2 s each, so a 16-code plan needs
        // close to a minute before the batch can finish.
        PumpUntilSettled(viewModel, TimeSpan.FromSeconds(180));

        var passed = viewModel.Status == "配置完成，可以启动";
        Console.WriteLine($"configure: {(passed ? "通过" : "失败")}（状态 “{viewModel.Status}”，" +
                          $"耗时 {clock.Elapsed.TotalSeconds:F1}s）");
        Environment.ExitCode = passed ? 0 : 1;
        Shutdown(Environment.ExitCode);
    }

    /// <summary>
    /// <c>--probe &lt;Serial|"USB TMC"&gt; &lt;resource&gt; [command...]</c> runs read-only
    /// queries and prints the transcript, so a new bench can be verified without
    /// the UI. Extra arguments replace the default query set.
    /// </summary>
    private static int RunProbe(Backend backend, string resource, IReadOnlyList<string> extraCommands)
    {
        IReadOnlyList<string> queries = extraCommands.Count > 0
            ? extraCommands
            : new[]
            {
                "*IDN?", "SYST:FW:BUILD?", "SYST:USB:MODE?", "TRIG:SEQ:NEXT?", "READ:IO:STAT?",
                "READ:SEQ:REPEAT?", "SYST:TDMA:RING:STAT?", "SYST:ERR?",
            };
        try
        {
            using var link = ScpiLink.Open(backend, resource);
            Console.WriteLine($"# backend={BackendNames.Format(backend)} resource={resource}");
            var timeouts = 0;
            foreach (var query in queries)
            {
                string response;
                try
                {
                    response = link.Exchange(query);
                }
                catch (Exception exception)
                {
                    response = $"<exception {exception.GetType().Name}: {exception.Message}>";
                }
                if (response == Core.SequenceConstants.TimeoutResponse)
                {
                    timeouts++;
                }
                Console.WriteLine($"{query} => {response}");
            }
            return timeouts == 0 ? 0 : 1;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"probe failed: {exception.Message}");
            return 2;
        }
    }

    /// <summary>
    /// <c>--smoke</c> shows the real window, lets binding resolve, and then exits
    /// with a non-zero code when WPF reported a data-binding error. It makes XAML
    /// and binding mistakes fail on the command line instead of on the bench.
    /// </summary>
    private void RunSmokeTest(AppArguments arguments)
    {
        PresentationTraceSources.Refresh();
        var listener = new BindingErrorListener();
        PresentationTraceSources.DataBindingSource.Listeners.Add(listener);
        PresentationTraceSources.DataBindingSource.Switch.Level = SourceLevels.Error;
        var failures = 0;

        DispatcherUnhandledException += (_, args) =>
        {
            Console.Error.WriteLine($"未处理异常：{args.Exception}");
            failures++;
            args.Handled = true;
        };

        var window = new MainWindow(arguments);
        MainWindow = window;
        window.Show();

        // Every dialog is created too: the smoke run must parse all XAML and
        // resolve their bindings, not only the main window.
        var dialogs = new List<Window>
        {
            new AngleWindow(window.ViewModel),
            new RateWindow(window.ViewModel),
            new CounterHistoryWindow(window.ViewModel),
            new BudgetWindow(window.ViewModel),
            new LoopbackWindow(window.ViewModel, Core.SequenceConstants.ModeIndependent),
            new LoopbackWindow(window.ViewModel, Core.SequenceConstants.ModeRj45),
            new LoopbackWindow(window.ViewModel, Core.SequenceConstants.ModeTurntable),
        };
        foreach (var dialog in dialogs)
        {
            dialog.Owner = window;
            dialog.Show();
        }

        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1500) };
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            foreach (var dialog in dialogs)
            {
                dialog.Close();
            }
            window.Close();
            foreach (var message in listener.Messages)
            {
                Console.Error.WriteLine($"绑定错误：{message}");
            }
            var exitCode = failures == 0 && listener.Messages.Count == 0 ? 0 : 2;
            Console.WriteLine(listener.Messages.Count == 0
                ? "smoke: 界面已启动，无绑定错误"
                : $"smoke: 发现 {listener.Messages.Count} 条绑定错误");
            Shutdown(exitCode);
        };
        timer.Start();
    }

    private sealed class BindingErrorListener : TraceListener
    {
        public List<string> Messages { get; } = new();

        public override void Write(string? message)
        {
        }

        public override void WriteLine(string? message)
        {
            if (!string.IsNullOrWhiteSpace(message))
            {
                Messages.Add(message);
            }
        }
    }
}

/// <summary>Command line: <c>--settings &lt;path&gt;</c>, <c>--no-settings</c> or <c>--smoke</c>.</summary>
public sealed class AppArguments
{
    public string? SettingsPath { get; private init; } = SettingsStore.DefaultPath;

    public bool SmokeTest { get; private init; }

    public (Backend Backend, string Resource, IReadOnlyList<string> Commands)? Probe { get; private init; }

    public (Backend Backend, string Resource)? Budget { get; private init; }

    public string? Configure { get; private init; }

    public string? Bench { get; private init; }

    public string? Resource { get; private init; }

    public static AppArguments Parse(string[] args)
    {
        string? settingsPath = SettingsStore.DefaultPath;
        var smoke = false;
        (Backend, string, IReadOnlyList<string>)? probe = null;
        (Backend, string)? budget = null;
        string? configure = null;
        string? bench = null;
        string? resource = null;
        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--smoke":
                    smoke = true;
                    break;
                case "--no-settings":
                    settingsPath = null;
                    break;
                case "--settings" when index + 1 < args.Length:
                    settingsPath = args[++index];
                    break;
                case "--resource" when index + 1 < args.Length:
                    resource = args[++index];
                    break;
                case "--configure" when index + 1 < args.Length:
                    configure = args[++index];
                    break;
                case "--budget" when index + 2 < args.Length:
                    budget = (Transport.BackendNames.Parse(args[index + 1]), args[index + 2]);
                    index += 2;
                    break;
                case "--bench" when index + 1 < args.Length:
                    bench = args[++index];
                    break;
                case "--probe" when index + 2 < args.Length:
                {
                    var backend = Transport.BackendNames.Parse(args[index + 1]);
                    var target = args[index + 2];
                    var commands = args.Skip(index + 3).ToList();
                    probe = (backend, target, commands);
                    index = args.Length;
                    break;
                }
            }
        }
        return new AppArguments
        {
            SettingsPath = settingsPath,
            SmokeTest = smoke,
            Probe = probe,
            Budget = budget,
            Configure = configure,
            Bench = bench,
            Resource = resource,
        };
    }

    /// <summary>Smoke runs must not touch the operator's saved drafts.</summary>
    public static AppArguments WithoutSettings() => new() { SettingsPath = null };
}
