using System.Collections.ObjectModel;
using System.Text.Json.Nodes;
using System.Windows.Threading;
using DhrT100.SequenceDebug.Core;
using DhrT100.SequenceDebug.Transport;

namespace DhrT100.SequenceDebug.App;

public sealed partial class MainViewModel : ObservableObject
{
    /// <summary>Local drafts, in the same key set and order the Python tool persisted.</summary>
    public static readonly string[] TextFieldNames =
    {
        "backend", "port", "plan", "codes", "settle", "repeat_count", "source", "edge", "pulse",
        "status_mode", "gateway_plan", "gateway_codes", "gateway_settle", "gateway_repeat_count",
        "gateway_ready_input", "gateway_edge", "gateway_pulse", "gateway_timeout",
        "turntable_plan", "turntable_codes", "turntable_settle", "turntable_repeat_count",
        "turntable_edge", "turntable_pulse", "turntable_ready_input", "turntable_timeout",
        "turntable_counter_slot", "turntable_dut_slot", "turntable_vna_slot", "turntable_input",
        "turntable_threshold", "turntable_rate_kind", "turntable_rate_value", "turntable_angle_start",
        "turntable_angle_stop", "turntable_angle_step", "turntable_angle_speed", "turntable_angle_ppd",
        "independent_switch", "operating_level",
    };

    public static readonly string[] BoolFieldNames = { "turntable_angle_enabled", "auto_scroll" };

    public static readonly string[] TabNames =
    {
        "independent_page", "loopback_page", "turntable_page", "manual_switch_page", "maintenance_page",
    };

    private static readonly Dictionary<string, string[]> Enumerations = new(StringComparer.Ordinal)
    {
        ["backend"] = new[] { "Serial", "USB TMC" },
        ["source"] = new[] { "MANUAL", "IN1", "IN2", "IN3", "IN4" },
        ["gateway_ready_input"] = new[] { "MANUAL", "IN1", "IN2", "IN3", "IN4" },
        ["turntable_ready_input"] = new[] { "MANUAL", "IN1", "IN2", "IN3", "IN4" },
        ["turntable_input"] = new[] { "IN1", "IN2", "IN3", "IN4" },
        ["edge"] = new[] { "RIS", "FALL" },
        ["gateway_edge"] = new[] { "RIS", "FALL" },
        ["turntable_edge"] = new[] { "RIS", "FALL" },
        ["status_mode"] = new[] { "无", "电平", "脉冲" },
        ["turntable_rate_kind"] = new[] { "频率 Hz", "周期 ms" },
        ["independent_switch"] = new[] { "1", "2", "3", "4", "5", "6", "7", "8" },
    };

    private readonly Dispatcher _dispatcher;
    private readonly OperationQueue _queue = new();
    private readonly string? _settingsPath;
    private readonly Dictionary<string, string> _fieldOwner = new(StringComparer.Ordinal);
    private readonly Dictionary<string, (string? Mode, int Count)> _deviceConfigurations = new(StringComparer.Ordinal);
    private readonly Dictionary<string, int> _deviceFactGenerationFloor = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _loopbackDurations = new(StringComparer.Ordinal)
    {
        [SequenceConstants.ModeIndependent] = "10",
        [SequenceConstants.ModeRj45] = "10",
        [SequenceConstants.ModeTurntable] = "60",
    };
    private readonly Dictionary<string, string> _loopbackResults = new(StringComparer.Ordinal);

    private string? _configuredMode;
    private int _configuredPlanCount;
    private int _configurationGeneration;
    private (string Backend, string Resource)? _configuredResource;
    private string? _deviceMode;
    private int _devicePlanCount;
    private bool _loopbackRunning;
    private (string Backend, string Resource, string Mode)? _loopbackTarget;
    private readonly Dictionary<string, (string Backend, string Resource, string Mode)> _loopbackTargets = new(StringComparer.Ordinal);
    private CancellationTokenSource? _loopbackCancel;
    private bool _otaRunning;
    private bool _transportSwitching;
    private bool _loading;
    private int _budgetGeneration;
    private bool _counterHistoryPending;
    private bool _operatingCatalogPending;
    private Dictionary<string, int> _operatingLabels = new(StringComparer.Ordinal);
    private bool _closing;

    public MainViewModel(Dispatcher dispatcher, string? settingsPath = null)
    {
        _dispatcher = dispatcher;
        _settingsPath = settingsPath;

        Fields = new FieldBag(TextFieldNames, Defaults());
        Bools = new BoolBag(BoolFieldNames, new Dictionary<string, bool>(StringComparer.Ordinal)
        {
            ["turntable_angle_enabled"] = false,
            ["auto_scroll"] = true,
        });

        OutSlots = BuildSlots(SequenceConstants.RoleStatus);
        GatewayOutSlots = BuildSlots(SequenceConstants.RoleGateway);
        TurntableOutSlots = BuildSlots(SequenceConstants.RoleGateway);

        InputLamps = new ObservableCollection<Lamp>
        {
            new("IN1"), new("IN2"), new("IN3"), new("IN4"),
        };
        OutputLamps = new ObservableCollection<Lamp>
        {
            new("OUT1"), new("OUT2"), new("OUT3"), new("OUT4"),
        };

        foreach (var mode in new[]
                 {
                     SequenceConstants.ModeIndependent, SequenceConstants.ModeRj45,
                     SequenceConstants.ModeTurntable,
                 })
        {
            _loopbackResults[mode] = "尚未观察";
        }

        RegisterFieldOwners();

        ConfigureCommand = new RelayCommand(Configure);
        ScanCommand = new RelayCommand(() => RefreshPorts(), () => !_closing);
        ClearLogCommand = new RelayCommand(ClearLog);
        ExportLogCommand = new RelayCommand(() => ExportLogRequested?.Invoke());
        RefreshStatusCommand = new RelayCommand(() => Command("TRIG:SEQ:NEXT?"));
        OpenBudgetCommand = new RelayCommand(() => OpenBudgetRequested?.Invoke());
        OpenAngleCommand = new RelayCommand(() => OpenAngleRequested?.Invoke());
        OpenRateCommand = new RelayCommand(() => OpenRateRequested?.Invoke());
        ReadCounterHistoryCommand = new RelayCommand(ReadCounterHistory);
        SetSwitchCommand = new RelayCommand(SetIndependentSwitch);
        ReadSwitchCommand = new RelayCommand(ReadIndependentSwitch);
        SetSp2tCommand = new RelayCommand(SetManualSp2t);
        ReadSp2tCommand = new RelayCommand(ReadManualSp2t);
        ReleaseOutputsCommand = new RelayCommand(ReleaseOutputs);
        SendManualCommand = new RelayCommand(SendManual);
        ReadIdentityCommand = new RelayCommand(() => EnqueueCommands(new List<string>
        {
            "*IDN?", "SYST:FW:BUILD?", "SYST:OTA:STAT?",
        }));
        QueryUsbModeCommand = new RelayCommand(() => EnqueueCommands(new List<string> { "SYST:USB:MODE?" }));
        SwitchToTmcCommand = new RelayCommand(() => SwitchUsbModeRequested?.Invoke("USBTMC"));
        SwitchToCdcCommand = new RelayCommand(() => SwitchUsbModeRequested?.Invoke("CDC"));
        SelectOtaCommand = new RelayCommand(() => SelectOtaRequested?.Invoke(), () => !_otaRunning);
        StartOtaCommand = new RelayCommand(StartOta, () => !_otaRunning);
        ReadAngleConfigCommand = new RelayCommand(() => EnqueueCommands(new List<string>
        {
            "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?", "READ:ANGLE:SPEED?", "READ:ANGLE:POSITION?",
        }));
        ApplyRateCommand = new RelayCommand(ApplyTurntableRate);

        ConfigureForMode = new RelayCommand<string>(alias => ConfigureMode(ModeOf(alias)));
        StartForMode = new RelayCommand<string>(alias => CommandMode(ModeOf(alias), "TRIG:START"));
        NextForMode = new RelayCommand<string>(alias => CommandMode(ModeOf(alias), "TRIG:SEQ:NEXT"));
        PauseForMode = new RelayCommand<string>(alias => CommandMode(ModeOf(alias), "TRIG:PAUS"));
        ContinueForMode = new RelayCommand<string>(alias => CommandMode(ModeOf(alias), "TRIG:CONT"));
        StopForMode = new RelayCommand<string>(alias => CommandMode(ModeOf(alias), "TRIG:STOP"));
        OpenLoopbackForMode = new RelayCommand<string>(alias => OpenLoopbackRequested?.Invoke(ModeOf(alias)));

        Fields.FieldChanged += OnFieldChanged;
        Bools.FieldChanged += OnBoolChanged;
        foreach (var slot in AllSlots())
        {
            slot.AssignmentChanged += () => OnAssignmentChanged(slot);
        }
    }

    // ---------------------------------------------------------------- state

    public FieldBag Fields { get; }
    public BoolBag Bools { get; }
    public IReadOnlyList<OutSlot> OutSlots { get; }
    public IReadOnlyList<OutSlot> GatewayOutSlots { get; }
    public IReadOnlyList<OutSlot> TurntableOutSlots { get; }
    public ObservableCollection<Lamp> InputLamps { get; }
    public ObservableCollection<Lamp> OutputLamps { get; }
    public ObservableCollection<LogEntry> LogEntries { get; } = new();
    public ObservableCollection<string> Resources { get; } = new();
    public ObservableCollection<string> OperatingChoices { get; } = new();

    public RelayCommand ConfigureCommand { get; }
    public RelayCommand ScanCommand { get; }
    public RelayCommand ClearLogCommand { get; }
    public RelayCommand ExportLogCommand { get; }
    public RelayCommand RefreshStatusCommand { get; }
    public RelayCommand OpenBudgetCommand { get; }
    public RelayCommand OpenAngleCommand { get; }
    public RelayCommand OpenRateCommand { get; }
    public RelayCommand ReadCounterHistoryCommand { get; }
    public RelayCommand SetSwitchCommand { get; }
    public RelayCommand ReadSwitchCommand { get; }
    public RelayCommand SetSp2tCommand { get; }
    public RelayCommand ReadSp2tCommand { get; }
    public RelayCommand ReleaseOutputsCommand { get; }
    public RelayCommand SendManualCommand { get; }
    public RelayCommand ReadIdentityCommand { get; }
    public RelayCommand QueryUsbModeCommand { get; }
    public RelayCommand SwitchToTmcCommand { get; }
    public RelayCommand SwitchToCdcCommand { get; }
    public RelayCommand SelectOtaCommand { get; }
    public RelayCommand StartOtaCommand { get; }
    public RelayCommand ReadAngleConfigCommand { get; }
    public RelayCommand ApplyRateCommand { get; }

    public RelayCommand<string> ConfigureForMode { get; }
    public RelayCommand<string> StartForMode { get; }
    public RelayCommand<string> NextForMode { get; }
    public RelayCommand<string> PauseForMode { get; }
    public RelayCommand<string> ContinueForMode { get; }
    public RelayCommand<string> StopForMode { get; }
    public RelayCommand<string> OpenLoopbackForMode { get; }

    /// <summary>Short aliases used by the XAML command parameters.</summary>
    public static string ModeOf(string alias)
    {
        return alias switch
        {
            "independent" => SequenceConstants.ModeIndependent,
            "rj45" => SequenceConstants.ModeRj45,
            "turntable" => SequenceConstants.ModeTurntable,
            _ => throw new ArgumentException($"未知模式别名：{alias}", nameof(alias)),
        };
    }

    // Window opening is a view concern; the view model only raises intent.
    public event Action? ExportLogRequested;
    public event Action? SelectOtaRequested;
    public event Action? OpenBudgetRequested;
    public event Action? OpenAngleRequested;
    public event Action? OpenRateRequested;
    public event Action<string>? OpenLoopbackRequested;
    public event Action<string>? SwitchUsbModeRequested;
    public event Action? CounterHistoryRequested;
    public event Action? LogAppended;
    public event Action? BudgetInvalidated;
    public event Action<string, string>? LoopbackResultChanged;
    public event Action<string, bool, string>? UsbModeFinished;
    public event Action<bool, string>? OtaFinished;

    private string _status = "未连接";
    private string _sequenceState = "UNKNOWN";
    private string _switchPosition = "当前开关：未运行";
    private string _ioInputs = "输入：—";
    private string _ioOutputs = "输出：—";
    private string _ioOwned = "占用：—";
    private string _ioState = "IO状态：—";
    private string _repeatStatus = "循环次数：1；0 表示持续运行";
    private string _linkStatus = "独立模式：输入脉冲 → 编码切换 → 状态反馈";
    private string _counterStatus = "转台计数：未读取";
    private string _counterHistory = "历史记录：未读取";
    private string _modeHint = "外部脉冲模式：启动后等待输入脉冲";
    private string _outputHint = string.Empty;
    private string _independentSwitchStatus = "SP8T：未读取";
    private string _manualSp2t = "0";
    private string _manualSp2tStatus = "OUT0（SP2T）：未读取";
    private string _outputReleaseHint = string.Empty;
    private string _operatingStatus = "TDMA 挡位：未读取";
    private string _operatingLabel = "TDMA 挡位（档位表未读取）";
    private string _otaStatus = "未选择固件";
    private string _otaFile = string.Empty;
    private string _manualCommand = string.Empty;
    private string _turntableAnglePreview = string.Empty;
    private string _turntableAngleCount = "—";
    private string _turntableAngleThreshold = "—";
    private string _turntableRatePreview = string.Empty;
    private int _selectedTabIndex;
    private string _runMode = SequenceConstants.ModeIndependent;

    public string Status { get => _status; set => SetProperty(ref _status, value); }
    public string SequenceState { get => _sequenceState; set => SetProperty(ref _sequenceState, value); }
    public string SwitchPosition { get => _switchPosition; set => SetProperty(ref _switchPosition, value); }
    public string IoInputs { get => _ioInputs; set => SetProperty(ref _ioInputs, value); }
    public string IoOutputs { get => _ioOutputs; set => SetProperty(ref _ioOutputs, value); }
    public string IoOwned { get => _ioOwned; set => SetProperty(ref _ioOwned, value); }
    public string IoState { get => _ioState; set => SetProperty(ref _ioState, value); }
    public string RepeatStatus { get => _repeatStatus; set => SetProperty(ref _repeatStatus, value); }
    public string LinkStatus { get => _linkStatus; set => SetProperty(ref _linkStatus, value); }
    public string CounterStatus { get => _counterStatus; set => SetProperty(ref _counterStatus, value); }
    public string CounterHistory { get => _counterHistory; set => SetProperty(ref _counterHistory, value); }
    public string ModeHint { get => _modeHint; set => SetProperty(ref _modeHint, value); }
    public string OutputHint { get => _outputHint; set => SetProperty(ref _outputHint, value); }
    public string IndependentSwitchStatus { get => _independentSwitchStatus; set => SetProperty(ref _independentSwitchStatus, value); }
    public string ManualSp2t { get => _manualSp2t; set => SetProperty(ref _manualSp2t, value); }
    public string ManualSp2tStatus { get => _manualSp2tStatus; set => SetProperty(ref _manualSp2tStatus, value); }
    public string OutputReleaseHint { get => _outputReleaseHint; set => SetProperty(ref _outputReleaseHint, value); }
    public string OperatingStatus { get => _operatingStatus; set => SetProperty(ref _operatingStatus, value); }
    public string OperatingLabel { get => _operatingLabel; set => SetProperty(ref _operatingLabel, value); }
    public string OtaStatus { get => _otaStatus; set => SetProperty(ref _otaStatus, value); }
    public string OtaFile { get => _otaFile; set => SetProperty(ref _otaFile, value); }
    public string ManualCommand { get => _manualCommand; set => SetProperty(ref _manualCommand, value); }
    public string TurntableAnglePreview { get => _turntableAnglePreview; set => SetProperty(ref _turntableAnglePreview, value); }
    public string TurntableAngleCount { get => _turntableAngleCount; set => SetProperty(ref _turntableAngleCount, value); }
    public string TurntableAngleThreshold { get => _turntableAngleThreshold; set => SetProperty(ref _turntableAngleThreshold, value); }
    public string TurntableRatePreview { get => _turntableRatePreview; set => SetProperty(ref _turntableRatePreview, value); }
    public string RunMode { get => _runMode; private set => SetProperty(ref _runMode, value); }

    public int SelectedTabIndex
    {
        get => _selectedTabIndex;
        set
        {
            if (!SetProperty(ref _selectedTabIndex, value))
            {
                return;
            }
            var mode = value switch
            {
                0 => SequenceConstants.ModeIndependent,
                1 => SequenceConstants.ModeRj45,
                2 => SequenceConstants.ModeTurntable,
                _ => null,
            };
            if (mode is not null && mode != RunMode)
            {
                RunMode = mode;
                UpdateRunMode();
            }
        }
    }

    public IReadOnlyList<string> IndependentRoles { get; } = new[]
    {
        SequenceConstants.RoleSequence, SequenceConstants.RoleStatus,
    };

    public IReadOnlyList<string> GatewayRoles { get; } = new[]
    {
        SequenceConstants.RoleSequence, SequenceConstants.RoleGateway,
    };

    public IReadOnlyList<string> SwitchPositions { get; } = new[] { "1", "2", "3", "4", "5", "6", "7", "8" };
    public IReadOnlyList<string> Sp2tLevels { get; } = new[] { "0", "1" };
    public IReadOnlyList<string> ReadyInputs { get; } = new[] { "MANUAL", "IN1", "IN2", "IN3", "IN4" };
    public IReadOnlyList<string> Edges { get; } = new[] { "RIS", "FALL" };
    public IReadOnlyList<string> StatusModes { get; } = new[] { "无", "电平", "脉冲" };
    public IReadOnlyList<string> RateKinds { get; } = new[] { "频率 Hz", "周期 ms" };
    public IReadOnlyList<string> BackendChoices { get; } = new[] { "Serial", "USB TMC" };

    public bool IndependentNextEnabled =>
        Fields["source"] == "MANUAL" && RunMode == SequenceConstants.ModeIndependent;
    public bool GatewayNextEnabled =>
        Fields["gateway_ready_input"] == "MANUAL" && RunMode == SequenceConstants.ModeRj45;
    public bool TurntableNextEnabled =>
        Fields["turntable_ready_input"] == "MANUAL" && RunMode == SequenceConstants.ModeTurntable;

    public bool AngleScanEnabled => Bools["turntable_angle_enabled"];

    public bool CanSelectOta => !_otaRunning;

    public System.Windows.Visibility OtaProgressVisibility =>
        _otaRunning ? System.Windows.Visibility.Visible : System.Windows.Visibility.Collapsed;

    public string SelectedLoopbackDuration(string mode)
    {
        return _loopbackDurations.TryGetValue(mode, out var value) ? value : "10";
    }

    public void SetLoopbackDuration(string mode, string value)
    {
        _loopbackDurations[mode] = value;
    }

    public string LoopbackResult(string mode)
    {
        return _loopbackResults.TryGetValue(mode, out var value) ? value : "尚未观察";
    }

    public string AppTitle => "DHRT100 序列触发调试 (WPF)";

    // -------------------------------------------------------------- defaults

    private static IEnumerable<KeyValuePair<string, string>> Defaults()
    {
        return new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["backend"] = "Serial",
            ["port"] = string.Empty,
            ["plan"] = "SP8T",
            ["codes"] = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15",
            ["settle"] = "10",
            ["repeat_count"] = "1",
            ["source"] = "IN1",
            ["edge"] = "RIS",
            ["pulse"] = "10",
            ["status_mode"] = "脉冲",
            ["gateway_plan"] = "SP8T",
            ["gateway_codes"] = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15",
            ["gateway_settle"] = "10",
            ["gateway_repeat_count"] = "1",
            ["gateway_ready_input"] = "MANUAL",
            ["gateway_edge"] = "RIS",
            ["gateway_pulse"] = "10",
            ["gateway_timeout"] = "5000",
            ["turntable_plan"] = "SP8T",
            ["turntable_codes"] = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15",
            ["turntable_settle"] = "10",
            ["turntable_repeat_count"] = "1",
            ["turntable_edge"] = "RIS",
            ["turntable_pulse"] = "10",
            ["turntable_ready_input"] = "IN2",
            ["turntable_timeout"] = "5000",
            ["turntable_counter_slot"] = "1",
            ["turntable_dut_slot"] = "2",
            ["turntable_vna_slot"] = "3",
            ["turntable_input"] = "IN1",
            ["turntable_threshold"] = "1000",
            ["turntable_rate_kind"] = "频率 Hz",
            ["turntable_rate_value"] = "50",
            ["turntable_angle_start"] = "0",
            ["turntable_angle_stop"] = "9",
            ["turntable_angle_step"] = "1",
            ["turntable_angle_speed"] = "1",
            ["turntable_angle_ppd"] = "1000",
            ["independent_switch"] = "1",
            ["operating_level"] = string.Empty,
        };
    }

    private List<OutSlot> BuildSlots(string secondaryRole)
    {
        var roles = secondaryRole == SequenceConstants.RoleGateway ? GatewayRoles : IndependentRoles;
        var slots = new List<OutSlot>(4);
        for (var index = 0; index < 4; index++)
        {
            var role = index < 3 ? SequenceConstants.RoleSequence : secondaryRole;
            slots.Add(new OutSlot(index, roles, true, role));
        }
        return slots;
    }

    private IEnumerable<OutSlot> AllSlots()
    {
        return OutSlots.Concat(GatewayOutSlots).Concat(TurntableOutSlots);
    }

    private void RegisterFieldOwners()
    {
        void Add(string mode, params string[] names)
        {
            foreach (var name in names)
            {
                _fieldOwner[name] = mode;
            }
        }

        Add(SequenceConstants.ModeIndependent, "plan", "codes", "settle", "repeat_count", "source",
            "edge", "pulse", "status_mode");
        Add(SequenceConstants.ModeRj45, "gateway_plan", "gateway_codes", "gateway_settle",
            "gateway_repeat_count", "gateway_ready_input", "gateway_edge", "gateway_pulse",
            "gateway_timeout");
        Add(SequenceConstants.ModeTurntable, "turntable_plan", "turntable_codes", "turntable_settle",
            "turntable_repeat_count", "turntable_edge", "turntable_pulse", "turntable_ready_input",
            "turntable_timeout", "turntable_counter_slot", "turntable_dut_slot", "turntable_vna_slot",
            "turntable_input", "turntable_threshold", "turntable_angle_start", "turntable_angle_stop",
            "turntable_angle_step", "turntable_angle_speed", "turntable_angle_ppd");
    }

    private void OnAssignmentChanged(OutSlot slot)
    {
        var mode = OutSlots.Contains(slot)
            ? SequenceConstants.ModeIndependent
            : GatewayOutSlots.Contains(slot)
                ? SequenceConstants.ModeRj45
                : SequenceConstants.ModeTurntable;
        DraftChanged(mode);
        if (mode == SequenceConstants.ModeIndependent)
        {
            UpdateStatusMode();
        }
        UpdateModeHint();
    }

    private void OnFieldChanged(string name)
    {
        if (name is "port" or "backend")
        {
            ConnectionChanged();
            return;
        }
        if (_fieldOwner.TryGetValue(name, out var owner))
        {
            DraftChanged(owner);
        }
        if (name.StartsWith("turntable_angle_", StringComparison.Ordinal))
        {
            PreviewTurntableAngle();
        }
        if (name is "source" or "gateway_ready_input" or "turntable_ready_input")
        {
            UpdateModeHint();
        }
        if (name == "status_mode")
        {
            UpdateStatusMode();
        }
    }

    private void OnBoolChanged(string name)
    {
        if (name == "turntable_angle_enabled")
        {
            DraftChanged(SequenceConstants.ModeTurntable);
            PreviewTurntableAngle();
            OnPropertyChanged(nameof(AngleScanEnabled));
        }
    }

    private void DraftChanged(string mode)
    {
        if (_loading)
        {
            return;
        }
        if (mode == RunMode || mode == _configuredMode)
        {
            _configuredMode = null;
            _configurationGeneration++;
            Status = "参数已修改，请配置此模式";
        }
    }

    private void ConnectionChanged()
    {
        _configuredMode = null;
        _configuredResource = null;
        _configurationGeneration++;
        var key = KeyOf(ResourceKey());
        _deviceConfigurations.TryGetValue(key, out var facts);
        _deviceMode = facts.Mode;
        _devicePlanCount = facts.Count;
        ClearDeviceReadback();
        ClearOperatingCatalog();
        InvalidateBudgetEditor();
        Status = "资源已改变，请配置此模式";
    }

    private (string Backend, string Resource) ResourceKey()
    {
        return (Fields["backend"], Fields["port"].Trim());
    }

    private void ClearOperatingCatalog()
    {
        _operatingLabels = new Dictionary<string, int>(StringComparer.Ordinal);
        _operatingCatalogPending = false;
        OperatingStatus = "TDMA 挡位：未读取";
        OperatingLabel = "TDMA 挡位（档位表未读取）";
        ReplaceOperatingChoices();
    }

    /// <summary>Refresh the profile choices the ComboBox exposes.</summary>
    private void ReplaceOperatingChoices()
    {
        if (!_dispatcher.CheckAccess())
        {
            _dispatcher.BeginInvoke(ReplaceOperatingChoices);
            return;
        }
        OperatingChoices.Clear();
        foreach (var key in _operatingLabels.Keys)
        {
            OperatingChoices.Add(key);
        }
    }

    private void InvalidateBudgetEditor()
    {
        _budgetGeneration++;
        BudgetInvalidated?.Invoke();
    }

    private void ClearDeviceReadback()
    {
        SequenceState = "UNKNOWN";
        SwitchPosition = "当前开关：未读取";
        IndependentSwitchStatus = "SP8T：未读取";
        ManualSp2tStatus = "OUT0（SP2T）：未读取";
        OutputReleaseHint = string.Empty;
        IoInputs = "输入：未读取";
        IoOutputs = "输出：未读取";
        IoOwned = "占用：未读取";
        IoState = "IO 状态：未读取";
        RepeatStatus = "循环次数：未读取";
        LinkStatus = "RJ45 状态：未读取";
        CounterStatus = "转台计数：未读取";
        CounterHistory = "历史记录：未读取";
        _counterHistoryPending = false;
        foreach (var lamp in InputLamps.Concat(OutputLamps))
        {
            lamp.Set("未知", false);
        }
    }

    private void InvalidateDeviceConfiguration()
    {
        _configuredMode = null;
        _configuredResource = null;
        _configuredPlanCount = 0;
        _configurationGeneration++;
        var key = KeyOf(ResourceKey());
        // A queued configuration predating a reset must not restore device facts.
        _deviceFactGenerationFloor[key] = _configurationGeneration;
        _deviceConfigurations[key] = (null, 0);
        _deviceMode = null;
        _devicePlanCount = 0;
        ClearDeviceReadback();
        ClearOperatingCatalog();
        InvalidateBudgetEditor();
        Status = "设备配置未知，请重新配置此模式";
    }

    private void ConfigurationFinished(string? mode, int count, int generation,
                                      (string Backend, string Resource) resourceKey)
    {
        var key = KeyOf(resourceKey);
        if (generation < _deviceFactGenerationFloor.GetValueOrDefault(key, 0))
        {
            return;
        }
        _deviceConfigurations[key] = (mode, count);
        if (key != KeyOf(ResourceKey()))
        {
            return;
        }
        _deviceMode = mode;
        _devicePlanCount = count;
        if (generation == _configurationGeneration)
        {
            _configuredMode = mode;
            _configuredPlanCount = count;
            _configuredResource = mode is null ? null : resourceKey;
            Status = mode is not null ? "配置完成，可以启动" : "配置失败，请检查日志";
        }
        else
        {
            Status = "设备配置已结束；当前草稿需重新配置";
        }
    }

    private static string KeyOf((string Backend, string Resource) key)
    {
        return $"{key.Backend}|{key.Resource}";
    }

    // -------------------------------------------------------------------- log

    public void Log(string text, string level = "INFO")
    {
        if (!_dispatcher.CheckAccess())
        {
            _dispatcher.BeginInvoke(() => Log(text, level));
            return;
        }
        LogEntries.Add(new LogEntry(text, level));
        while (LogEntries.Count > SequenceConstants.MaxLogLines)
        {
            LogEntries.RemoveAt(0);
        }
        LogAppended?.Invoke();
    }

    public string LogText => string.Join(Environment.NewLine,
        LogEntries.Select(entry => $"[{entry.Time}] {entry.Text}"));

    public void ClearLog()
    {
        LogEntries.Clear();
    }

    private void LogExchange(string command, string response, (string Backend, string Resource) resourceKey)
    {
        Log($"> {command}", "CMD");
        var level = SequenceConstants.IsTimeout(response) ? "ERROR" : "OK";
        Log($"< {response}", level);
        if (KeyOf(resourceKey) != KeyOf(ResourceKey()))
        {
            return;
        }
        var summary = response.Replace("\r", " ").Replace("\n", " ");
        Status = summary.Length <= 24 ? summary : summary[..21] + "…";
        try
        {
            UpdateIo(command, response);
        }
        catch (Exception exception)
        {
            // A display parser must never take down the session: the device
            // reply is already in the log, so report and keep the link usable.
            Log($"状态显示失败（{command}）：{exception.Message}", "WARN");
        }
    }
}
