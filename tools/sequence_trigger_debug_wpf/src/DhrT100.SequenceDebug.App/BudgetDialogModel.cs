using System.Collections.ObjectModel;
using DhrT100.SequenceDebug.Core;

namespace DhrT100.SequenceDebug.App;

/// <summary>One editable phase row of the Core1 budget table.</summary>
public sealed class BudgetPhaseRow : ObservableObject
{
    private readonly Action _changed;
    private string _window = string.Empty;
    private string _wcet = string.Empty;

    public BudgetPhaseRow(string name, Action changed)
    {
        Name = name;
        _changed = changed;
    }

    public string Name { get; }

    public string Window
    {
        get => _window;
        set
        {
            if (SetProperty(ref _window, value))
            {
                _changed();
            }
        }
    }

    public string Wcet
    {
        get => _wcet;
        set
        {
            if (SetProperty(ref _wcet, value))
            {
                _changed();
            }
        }
    }

    public void SetSilently(string window, string wcet)
    {
        _window = window;
        _wcet = wcet;
        OnPropertyChanged(nameof(Window));
        OnPropertyChanged(nameof(Wcet));
    }
}

/// <summary>
/// Budget dialog logic. Editing and APPLY stay volatile; only SAVE writes Flash,
/// and only after the applied table was read back unchanged.
/// </summary>
public sealed class BudgetDialogModel : ObservableObject
{
    private readonly MainViewModel _main;
    private Budget.Snapshot? _snapshot;
    private bool _busy;
    private string _cycleUs = string.Empty;
    private string _guardUs = "—";
    private string _message = "先读取当前设备预算。编辑和临时应用均不写 Flash。";
    private string _draftMessage = string.Empty;
    private bool _canRead = true;
    private bool _canValidate;
    private bool _canApply;
    private bool _canSave;

    public BudgetDialogModel(MainViewModel main)
    {
        _main = main;
        foreach (var name in Budget.Phases[..^1])
        {
            Rows.Add(new BudgetPhaseRow(name, OnEdited));
        }
        ReadCommand = new RelayCommand(() => Request("read"));
        ValidateCommand = new RelayCommand(() => Request("validate"));
        ApplyCommand = new RelayCommand(() => Request("apply"));
        SaveCommand = new RelayCommand(() => Request("save"));
        RefreshControls();
    }

    public ObservableCollection<BudgetPhaseRow> Rows { get; } = new();
    public RelayCommand ReadCommand { get; }
    public RelayCommand ValidateCommand { get; }
    public RelayCommand ApplyCommand { get; }
    public RelayCommand SaveCommand { get; }

    public string CycleUs
    {
        get => _cycleUs;
        set
        {
            if (SetProperty(ref _cycleUs, value))
            {
                OnEdited();
            }
        }
    }

    public string GuardUs { get => _guardUs; private set => SetProperty(ref _guardUs, value); }
    public string Message { get => _message; private set => SetProperty(ref _message, value); }
    public string DraftMessage { get => _draftMessage; private set => SetProperty(ref _draftMessage, value); }
    public bool CanRead { get => _canRead; private set => SetProperty(ref _canRead, value); }
    public bool CanValidate { get => _canValidate; private set => SetProperty(ref _canValidate, value); }
    public bool CanApply { get => _canApply; private set => SetProperty(ref _canApply, value); }
    public bool CanSave { get => _canSave; private set => SetProperty(ref _canSave, value); }

    public void Read()
    {
        Request("read");
    }

    public void Invalidate()
    {
        _snapshot = null;
        _busy = false;
        DraftMessage = string.Empty;
        Message = "设备连接或状态已变化，请重新读取预算。";
        RefreshControls();
    }

    private Budget.Table Table()
    {
        if (_snapshot is null)
        {
            throw new ValueError("请先读取当前设备预算");
        }
        return Budget.TableFromMicroseconds(_snapshot.ClockHz, CycleUs,
            Rows.Select(row => row.Window).ToList(),
            Rows.Select(row => row.Wcet).ToList());
    }

    private void Request(string action)
    {
        if (_busy)
        {
            return;
        }
        try
        {
            IReadOnlyDictionary<string, object?> payload = action == "read"
                ? new Dictionary<string, object?>()
                : new Dictionary<string, object?>
                {
                    ["baseline"] = _snapshot,
                    ["table"] = Table(),
                };
            if (action == "save" && payload["table"] is Budget.Table table &&
                _snapshot is not null && !table.Equals(_snapshot.Table))
            {
                throw new ValueError("请先临时应用编辑内容，再保存到 Flash");
            }

            _busy = true;
            RefreshControls();
            Message = "正在处理；请等待设备读回…";
            _main.SubmitBudget(action, payload, ReceiveResult);
        }
        catch (Exception exception) when (exception is ValueError or FormatException)
        {
            ReceiveError(exception.Message);
        }
    }

    private void ReceiveResult(Budget.OperationResult? result, string? error)
    {
        _busy = false;
        if (error is not null)
        {
            ReceiveError(error);
            return;
        }
        if (result is null)
        {
            ReceiveError("未收到设备结果");
            return;
        }

        _snapshot = result.Snapshot;
        if (result.Action != "validate")
        {
            CycleUs = Budget.Microseconds(_snapshot.Table.Cycle, _snapshot.ClockHz);
            for (var index = 0; index < Rows.Count; index++)
            {
                var phase = _snapshot.Table.Phases[index];
                Rows[index].SetSilently(
                    Budget.Microseconds(phase.End - phase.Start, _snapshot.ClockHz),
                    Budget.Microseconds(phase.Wcet, _snapshot.ClockHz));
            }
        }

        var outcome = result.Action switch
        {
            "read" => "已读取实际预算",
            "validate" => "草稿校验通过，尚未应用",
            "apply" => "临时预算已生效",
            _ => "当前预算已保存到 Flash",
        };
        var saved = result.Persisted ? "当前实际预算已固化" : "当前实际预算未固化";
        Message = $"{outcome}；{saved}。\n" +
                  $"隔离位图=0x{_snapshot.QuarantinedMask:X}，" +
                  $"调度错失={_snapshot.ScheduleMisses}（非实时稳定性验收）。";
        OnEdited();
    }

    private void ReceiveError(string message)
    {
        Invalidate();
        Message = $"操作未确认：{message}。请重新读取设备预算。";
    }

    private void OnEdited()
    {
        try
        {
            var table = Table();
            GuardUs = Budget.Microseconds(table.Cycle - table.Phases[^1].Start, _snapshot!.ClockHz);
            DraftMessage = table.Equals(_snapshot.Table)
                ? string.Empty
                : "草稿尚未应用；保存到 Flash 前请先临时应用。";
        }
        catch (ValueError)
        {
            GuardUs = "预算超限或输入无效";
            DraftMessage = _snapshot is null ? string.Empty : "草稿无效，请检查总周期、各阶段时窗与 WCET。";
        }
        RefreshControls();
    }

    private void RefreshControls()
    {
        var valid = false;
        var unchanged = false;
        try
        {
            var table = Table();
            valid = true;
            unchanged = _snapshot is not null && table.Equals(_snapshot.Table);
        }
        catch (ValueError)
        {
        }
        CanRead = !_busy;
        CanValidate = !_busy && valid;
        CanApply = !_busy && valid;
        CanSave = !_busy && valid && unchanged;
    }
}
