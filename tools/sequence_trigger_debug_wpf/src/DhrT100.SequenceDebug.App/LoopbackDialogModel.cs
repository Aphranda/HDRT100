using DhrT100.SequenceDebug.Core;

namespace DhrT100.SequenceDebug.App;

/// <summary>View model for one loopback tool window; drafts stay on the main model.</summary>
public sealed class LoopbackDialogModel : ObservableObject
{
    private readonly MainViewModel _main;
    private string _duration;
    private string _result;

    public LoopbackDialogModel(MainViewModel main, string mode)
    {
        _main = main;
        Mode = mode;
        _duration = main.SelectedLoopbackDuration(mode);
        _result = main.LoopbackResult(mode);

        Wiring = mode switch
        {
            SequenceConstants.ModeIndependent => "预设接线：OUT4 → IN1；OUT1–OUT3 为 SP8T 编码",
            SequenceConstants.ModeRj45 =>
                "预设接线：RJ45 物理回环；OUT4 → IN1（READY）；OUT1–OUT3 为 SP8T 编码",
            _ => "预设接线：RJ45 物理回环；外部计数脉冲 → IN1；OUT4 → IN2（READY）；OUT1–OUT3 为 SP8T 编码",
        };
        ReceiverNote = mode == SequenceConstants.ModeIndependent
            ? string.Empty
            : "实际网分：OUT4 → 网分触发输入；网分反馈 OUT → READY 输入。";

        ApplyPresetCommand = new RelayCommand(() => main.ApplyLoopbackPreset(Mode));
        ConfigureCommand = new RelayCommand(() => main.ConfigureMode(Mode));
        StartCommand = new RelayCommand(Start);
        StopCommand = new RelayCommand(() => main.StopLoopback(Mode));
        main.LoopbackResultChanged += OnResultChanged;
    }

    public string Mode { get; }
    public string Title => $"回环工具 · {Mode}";
    public string Wiring { get; }
    public string ReceiverNote { get; }

    public RelayCommand ApplyPresetCommand { get; }
    public RelayCommand ConfigureCommand { get; }
    public RelayCommand StartCommand { get; }
    public RelayCommand StopCommand { get; }

    public string Duration
    {
        get => _duration;
        set
        {
            if (SetProperty(ref _duration, value))
            {
                _main.SetLoopbackDuration(Mode, value);
            }
        }
    }

    public string Result
    {
        get => _result;
        private set => SetProperty(ref _result, value);
    }

    private void Start()
    {
        _main.SetLoopbackDuration(Mode, Duration);
        _main.StartLoopback(Mode);
    }

    private void OnResultChanged(string mode, string result)
    {
        if (mode == Mode)
        {
            Result = result;
        }
    }

    public void Detach()
    {
        _main.LoopbackResultChanged -= OnResultChanged;
    }
}
