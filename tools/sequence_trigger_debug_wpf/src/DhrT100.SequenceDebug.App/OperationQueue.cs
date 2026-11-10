using System.Collections.Concurrent;
using System.Diagnostics;
using System.Windows.Media;

namespace DhrT100.SequenceDebug.App;

/// <summary>A single coloured log line.</summary>
public sealed class LogEntry
{
    private static readonly Dictionary<string, Color> Colors = new(StringComparer.Ordinal)
    {
        ["TIME"] = Color.FromRgb(0x6B, 0x72, 0x80),
        ["CMD"] = Color.FromRgb(0x03, 0x69, 0xA1),
        ["OK"] = Color.FromRgb(0x15, 0x80, 0x3D),
        ["INFO"] = Color.FromRgb(0x37, 0x41, 0x51),
        ["WARN"] = Color.FromRgb(0xA1, 0x62, 0x07),
        ["ERROR"] = Color.FromRgb(0xB9, 0x1C, 0x1C),
        ["OTA"] = Color.FromRgb(0x7C, 0x3A, 0xED),
    };

    private static readonly Dictionary<string, SolidColorBrush> Brushes =
        Colors.ToDictionary(pair => pair.Key, pair => new SolidColorBrush(pair.Value));

    public LogEntry(string text, string level)
    {
        Time = DateTime.Now.ToString("HH:mm:ss");
        Text = text;
        Level = level;
    }

    public string Time { get; }
    public string Text { get; }
    public string Level { get; }

    public Brush TextBrush => Brushes.TryGetValue(Level, out var brush) ? brush : Brushes["INFO"];
}

/// <summary>
/// One background worker consuming operations in submission order. The device
/// session is single-owner (a COM port or a VISA session), so every exchange
/// stays on this thread; UI work is marshalled by the view model.
/// </summary>
public sealed class OperationQueue : IDisposable
{
    private readonly BlockingCollection<Action> _queue = new();
    private readonly Thread _worker;

    public OperationQueue()
    {
        _worker = new Thread(Loop)
        {
            IsBackground = true,
            Name = "scpi-worker",
        };
        _worker.Start();
    }

    public bool IsBusy { get; private set; }

    public void Post(Action operation)
    {
        if (_queue.IsAddingCompleted)
        {
            return;
        }
        _queue.Add(operation);
    }

    private void Loop()
    {
        foreach (var operation in _queue.GetConsumingEnumerable())
        {
            IsBusy = true;
            try
            {
                operation();
            }
            catch (Exception exception)
            {
                // A worker exception must never kill the queue; the operation
                // itself reports device errors through the view model.
                Debug.WriteLine($"operation failed: {exception}");
            }
            finally
            {
                IsBusy = false;
            }
        }
    }

    public void Complete()
    {
        _queue.CompleteAdding();
    }

    public void Dispose()
    {
        _queue.CompleteAdding();
        if (_worker.IsAlive)
        {
            _worker.Join(TimeSpan.FromSeconds(2));
        }
        _queue.Dispose();
    }
}
