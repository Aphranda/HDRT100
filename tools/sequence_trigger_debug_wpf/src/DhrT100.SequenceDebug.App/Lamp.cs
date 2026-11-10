using System.Windows.Media;

namespace DhrT100.SequenceDebug.App;

/// <summary>One IN/OUT level indicator in the common IO panel.</summary>
public sealed class Lamp : ObservableObject
{
    private static readonly Brush HighBrush = Freeze(Color.FromRgb(0x86, 0xEF, 0xAC));
    private static readonly Brush LowBrush = Freeze(Color.FromRgb(0xE5, 0xE7, 0xEB));
    private static readonly Brush BorderBrush = Freeze(Color.FromRgb(0xD1, 0xD5, 0xDB));

    private string _state;
    private bool _isHigh;

    public Lamp(string caption)
    {
        Caption = caption;
        _state = "低";
    }

    public string Caption { get; }

    public string State
    {
        get => _state;
        private set
        {
            if (SetProperty(ref _state, value))
            {
                OnPropertyChanged(nameof(Text));
            }
        }
    }

    public bool IsHigh
    {
        get => _isHigh;
        private set
        {
            if (SetProperty(ref _isHigh, value))
            {
                OnPropertyChanged(nameof(Background));
            }
        }
    }

    public string Text => $"{Caption}\n{State}";

    public Brush Background => IsHigh ? HighBrush : LowBrush;

    public Brush Border => BorderBrush;

    public void Set(string state, bool high)
    {
        State = state;
        IsHigh = high;
    }

    private static Brush Freeze(Color color)
    {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        return brush;
    }
}
