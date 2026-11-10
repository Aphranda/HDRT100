using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Input;
using Microsoft.Win32;

namespace DhrT100.SequenceDebug.App;

public partial class MainWindow : Window
{
    private readonly MainViewModel _viewModel;
    private readonly Dictionary<string, LoopbackWindow> _loopbackWindows = new(StringComparer.Ordinal);
    private BudgetWindow? _budgetWindow;
    private AngleWindow? _angleWindow;
    private RateWindow? _rateWindow;
    private CounterHistoryWindow? _counterHistoryWindow;
    private bool _shutdown;

    /// <summary>Exposed for the <c>--smoke</c> self-check only.</summary>
    internal MainViewModel ViewModel => _viewModel;

    public MainWindow(AppArguments arguments)
    {
        InitializeComponent();
        _viewModel = new MainViewModel(Dispatcher, arguments.SettingsPath);
        DataContext = _viewModel;

        _viewModel.ConfirmRequest = (title, text) =>
            MessageBox.Show(this, text, title, MessageBoxButton.YesNo, MessageBoxImage.Question) ==
            MessageBoxResult.Yes;
        _viewModel.WarnRequest = (title, text) =>
            MessageBox.Show(this, text, title, MessageBoxButton.OK, MessageBoxImage.Warning);

        _viewModel.ExportLogRequested += ExportLog;
        _viewModel.SelectOtaRequested += SelectOtaFile;
        _viewModel.OpenBudgetRequested += OpenBudget;
        _viewModel.OpenAngleRequested += OpenAngle;
        _viewModel.OpenRateRequested += OpenRate;
        _viewModel.OpenLoopbackRequested += OpenLoopback;
        _viewModel.SwitchUsbModeRequested += mode => _viewModel.SwitchUsbMode(mode);
        _viewModel.CounterHistoryRequested += OpenCounterHistory;
        _viewModel.LogAppended += ScrollLogToEnd;
        _viewModel.BudgetInvalidated += () => _budgetWindow?.Invalidate();

        // Selecting a backend rescans, matching the Tk combobox binding.
        _viewModel.Fields.FieldChanged += name =>
        {
            if (name == "backend")
            {
                _viewModel.RefreshPorts();
            }
        };

        Loaded += (_, _) =>
        {
            _viewModel.LoadSettings();
            _viewModel.RefreshPorts(false);
            _viewModel.PreviewTurntableAngle();
            _viewModel.PreviewTurntableRate();
        };
        Closing += OnClosing;
    }

    private void OnClosing(object? sender, CancelEventArgs eventArgs)
    {
        if (_shutdown)
        {
            return;
        }
        if (!_viewModel.CanClose)
        {
            eventArgs.Cancel = true;
            MessageBox.Show(this, "请等待升级完成后再关闭调试工具。", "OTA 正在运行",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        _shutdown = true;
        _viewModel.PrepareShutdown();
    }

    private void ScrollLogToEnd()
    {
        if (!_viewModel.Bools["auto_scroll"] || _viewModel.LogEntries.Count == 0)
        {
            return;
        }
        LogList.ScrollIntoView(_viewModel.LogEntries[^1]);
    }

    private void OnOperatingProfileFocus(object sender, RoutedEventArgs eventArgs)
    {
        _viewModel.RequestOperatingCatalog();
    }

    private void OnManualCommandKeyDown(object sender, KeyEventArgs eventArgs)
    {
        if (eventArgs.Key == Key.Return)
        {
            _viewModel.SendManual();
            eventArgs.Handled = true;
        }
    }

    private void ExportLog()
    {
        var dialog = new SaveFileDialog
        {
            DefaultExt = ".log",
            FileName = $"dhrt100_sequence_{DateTime.Now:yyyyMMdd_HHmmss}.log",
            Filter = "日志文件 (*.log)|*.log|文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*",
        };
        if (dialog.ShowDialog(this) != true)
        {
            return;
        }
        try
        {
            File.WriteAllText(dialog.FileName, _viewModel.LogText + Environment.NewLine);
            _viewModel.Log($"日志已导出：{dialog.FileName}");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            MessageBox.Show(this, $"日志导出失败：{exception.Message}", "导出失败",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void SelectOtaFile()
    {
        var dialog = new OpenFileDialog
        {
            Title = "选择 DHRT100 固件包",
            Filter = "DHRT100 OTA 包 (*.pkg)|*.pkg|固件镜像 (*.bin)|*.bin|所有文件 (*.*)|*.*",
        };
        if (dialog.ShowDialog(this) != true)
        {
            return;
        }
        _viewModel.OtaFile = dialog.FileName;
        _viewModel.OtaStatus = "固件已选择";
        _viewModel.Log($"已选择 OTA 固件：{dialog.FileName}", "OTA");
    }

    private void OpenBudget()
    {
        if (_budgetWindow is not null)
        {
            _budgetWindow.Activate();
            return;
        }
        _budgetWindow = new BudgetWindow(_viewModel) { Owner = this };
        _budgetWindow.Closed += (_, _) => _budgetWindow = null;
        _budgetWindow.Show();
        _budgetWindow.Read();
    }

    private void OpenAngle()
    {
        if (_angleWindow is not null)
        {
            _angleWindow.Activate();
            return;
        }
        _angleWindow = new AngleWindow(_viewModel) { Owner = this };
        _angleWindow.Closed += (_, _) => _angleWindow = null;
        _angleWindow.Show();
    }

    private void OpenRate()
    {
        if (_rateWindow is not null)
        {
            _rateWindow.Activate();
            return;
        }
        _rateWindow = new RateWindow(_viewModel) { Owner = this };
        _rateWindow.Closed += (_, _) => _rateWindow = null;
        _rateWindow.Show();
    }

    private void OpenLoopback(string mode)
    {
        if (_loopbackWindows.TryGetValue(mode, out var existing))
        {
            existing.Activate();
            return;
        }
        var window = new LoopbackWindow(_viewModel, mode) { Owner = this };
        _loopbackWindows[mode] = window;
        window.Closed += (_, _) => _loopbackWindows.Remove(mode);
        window.Show();
    }

    private void OpenCounterHistory()
    {
        if (_counterHistoryWindow is not null)
        {
            _counterHistoryWindow.Activate();
            return;
        }
        _counterHistoryWindow = new CounterHistoryWindow(_viewModel) { Owner = this };
        _counterHistoryWindow.Closed += (_, _) => _counterHistoryWindow = null;
        _counterHistoryWindow.Show();
    }
}
