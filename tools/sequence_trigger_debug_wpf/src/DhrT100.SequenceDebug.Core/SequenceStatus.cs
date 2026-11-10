using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>Parsed <c>TRIG:SEQ:NEXT?</c> runtime row.</summary>
public sealed class SequenceStatus
{
    public static readonly string[] Fields =
    {
        "state", "run_id", "generation", "count", "current_index", "current_state",
        "next_index", "executed_index", "executed_state", "completed_index",
        "completed_state", "cycles", "accepted", "completed", "busy_rejected",
        "notready_rejected", "cancelled", "faults", "error", "written_us",
        "rise_us", "completed_us", "late_us", "backend_fault",
    };

    public static readonly HashSet<string> States = new(StringComparer.Ordinal)
    {
        "IDLE", "STARTING", "READY", "BUSY", "PAUSING", "PAUSED", "STOPPING", "FAULT",
    };

    public required string State { get; init; }
    public required string Error { get; init; }
    public required long RunId { get; init; }
    public required long Generation { get; init; }
    public required long Count { get; init; }
    public required long CurrentIndex { get; init; }
    public required long CurrentState { get; init; }
    public required long NextIndex { get; init; }
    public required long Cycles { get; init; }
    public required long Accepted { get; init; }
    public required long Completed { get; init; }
    public required long BusyRejected { get; init; }
    public required long Faults { get; init; }
    public required long BackendFault { get; init; }
    public required long Cancelled { get; init; }

    public static SequenceStatus Parse(string response)
    {
        List<string> values;
        try
        {
            values = Csv.ParseLine(response);
        }
        catch (FormatException exception)
        {
            throw new RuntimeError("malformed sequence status", exception);
        }
        if (values.Count != Fields.Length)
        {
            throw new RuntimeError("incomplete sequence status");
        }

        var row = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 0; index < Fields.Length; index++)
        {
            row[Fields[index]] = values[index];
        }
        if (!States.Contains(row["state"]))
        {
            throw new RuntimeError("unknown sequence state");
        }
        foreach (var field in Fields)
        {
            if (field is "state" or "error")
            {
                continue;
            }
            var text = row[field];
            if (text.Length == 0 || text.Length > 20 || !text.All(char.IsAsciiDigit))
            {
                throw new RuntimeError($"invalid status field {field}");
            }
        }

        long Number(string name) => long.Parse(row[name], NumberStyles.None, CultureInfo.InvariantCulture);

        return new SequenceStatus
        {
            State = row["state"],
            Error = row["error"],
            RunId = Number("run_id"),
            Generation = Number("generation"),
            Count = Number("count"),
            CurrentIndex = Number("current_index"),
            CurrentState = Number("current_state"),
            NextIndex = Number("next_index"),
            Cycles = Number("cycles"),
            Accepted = Number("accepted"),
            Completed = Number("completed"),
            BusyRejected = Number("busy_rejected"),
            Faults = Number("faults"),
            BackendFault = Number("backend_fault"),
            Cancelled = Number("cancelled"),
        };
    }
}
