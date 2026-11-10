namespace DhrT100.SequenceDebug.App;

/// <summary>
/// Named string drafts, the equivalent of the Tk <c>StringVar</c> set. WPF binds
/// <c>Fields[plan]</c> to the indexer, so one notification updates every page
/// that shows the same draft.
/// </summary>
public sealed class FieldBag : ObservableObject
{
    private readonly Dictionary<string, string> _values = new(StringComparer.Ordinal);
    private readonly HashSet<string> _allowed;

    public FieldBag(IEnumerable<string> names, IEnumerable<KeyValuePair<string, string>> defaults)
    {
        _allowed = new HashSet<string>(names, StringComparer.Ordinal);
        foreach (var pair in defaults)
        {
            _values[pair.Key] = pair.Value;
        }
    }

    /// <summary>Raised after a draft value actually changed.</summary>
    public event Action<string>? FieldChanged;

    public string this[string name]
    {
        get => _values.TryGetValue(name, out var value) ? value : string.Empty;
        set
        {
            if (!_allowed.Contains(name))
            {
                throw new ArgumentException($"未知参数：{name}", nameof(name));
            }
            var normalised = value ?? string.Empty;
            if (_values.TryGetValue(name, out var existing) && existing == normalised)
            {
                return;
            }
            _values[name] = normalised;
            // "Item[]" refreshes indexer bindings; the name also lets a single
            // field binding observe its own value.
            OnPropertyChanged("Item[]");
            OnPropertyChanged(name);
            FieldChanged?.Invoke(name);
        }
    }

    public bool Has(string name) => _allowed.Contains(name);

    public string Get(string name) => this[name];

    public void Set(string name, string value) => this[name] = value;

    public Dictionary<string, string> Snapshot(IEnumerable<string> names)
    {
        return names.ToDictionary(name => name, Get, StringComparer.Ordinal);
    }
}

/// <summary>Named boolean drafts (the Tk <c>BooleanVar</c> set).</summary>
public sealed class BoolBag : ObservableObject
{
    private readonly Dictionary<string, bool> _values = new(StringComparer.Ordinal);
    private readonly HashSet<string> _allowed;

    public BoolBag(IEnumerable<string> names, IEnumerable<KeyValuePair<string, bool>> defaults)
    {
        _allowed = new HashSet<string>(names, StringComparer.Ordinal);
        foreach (var pair in defaults)
        {
            _values[pair.Key] = pair.Value;
        }
    }

    public event Action<string>? FieldChanged;

    public bool this[string name]
    {
        get => _values.TryGetValue(name, out var value) && value;
        set
        {
            if (!_allowed.Contains(name))
            {
                throw new ArgumentException($"未知开关：{name}", nameof(name));
            }
            if (_values.TryGetValue(name, out var existing) && existing == value)
            {
                return;
            }
            _values[name] = value;
            OnPropertyChanged("Item[]");
            OnPropertyChanged(name);
            FieldChanged?.Invoke(name);
        }
    }
}

/// <summary>One OUT1–OUT4 assignment cell (enabled + role).</summary>
public sealed class OutSlot : ObservableObject
{
    private bool _enabled;
    private string _role;

    public OutSlot(int index, IReadOnlyList<string> roles, bool enabled, string role)
    {
        Index = index;
        Name = $"OUT{index + 1}";
        Roles = roles;
        _enabled = enabled;
        _role = role;
        Mask = 1 << index;
    }

    public int Index { get; }
    public string Name { get; }
    public int Mask { get; }
    public IReadOnlyList<string> Roles { get; }

    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (SetProperty(ref _enabled, value))
            {
                AssignmentChanged?.Invoke();
            }
        }
    }

    public string Role
    {
        get => _role;
        set
        {
            if (SetProperty(ref _role, value))
            {
                AssignmentChanged?.Invoke();
            }
        }
    }

    public event Action? AssignmentChanged;
}
