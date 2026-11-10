using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>Bounded management-plane acknowledgement of a queued VDC owner change.</summary>
public static class OwnerState
{
    public const string OwnerQuery = "SYSTem:SYNC:VDC:DPLL:OWNer?";

    public static long ReadQueuedSequence(string response)
    {
        var fields = Csv.ParseLine(response);
        if (fields.Count < 2 || !string.Equals(fields[0].Trim(), "QUEUED", StringComparison.OrdinalIgnoreCase))
        {
            throw new RuntimeError($"expected queued owner acknowledgement: '{response}'");
        }
        if (!long.TryParse(fields[^1].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var sequence) ||
            sequence <= 0 || sequence > 0xffffffffL)
        {
            throw new RuntimeError($"invalid owner sequence: '{response}'");
        }
        return sequence;
    }

    /// <summary>
    /// Wait before ARM, never during acquisition. APPLIED alone is insufficient:
    /// Core0 must also have consumed the owner ACK and published its active
    /// calibration/topology (pending == 0).
    /// </summary>
    public static void WaitApplied(Func<string, string> query, long sequence, double timeoutSeconds,
                                   List<string> evidence, Func<double> monotonic, Action<double> sleep,
                                   double pollSeconds = 0.02)
    {
        if (sequence <= 0 || sequence > 0xffffffffL || !double.IsFinite(timeoutSeconds) || timeoutSeconds <= 0 ||
            !double.IsFinite(pollSeconds) || pollSeconds <= 0)
        {
            throw new ValueError("sequence and finite positive timeouts are required");
        }

        var deadline = monotonic() + timeoutSeconds;
        while (true)
        {
            evidence.Add(OwnerQuery);
            var raw = query(OwnerQuery);
            int[] fields;
            try
            {
                fields = Csv.ParseLine(raw).Select(Csv.ParseInt).ToArray();
            }
            catch (Exception exception) when (exception is FormatException or OverflowException)
            {
                throw new RuntimeError($"invalid owner status: '{raw}'", exception);
            }
            if (fields.Length != 5 || fields[3] is < 0 or > 3 || fields[4] is < 0 or > 1)
            {
                throw new RuntimeError($"invalid owner status: '{raw}'");
            }

            var requested = fields[0];
            var applied = fields[1];
            var result = fields[3];
            var pending = fields[4];
            if (requested != sequence)
            {
                throw new RuntimeError($"owner request changed: expected {sequence}, observed '{raw}'");
            }
            if (applied == sequence && result == 3)
            {
                throw new RuntimeError($"owner rejected sequence {sequence}: '{raw}'");
            }
            if (applied == sequence && result == 2 && pending == 0)
            {
                return;
            }

            var remaining = deadline - monotonic();
            if (remaining <= 0)
            {
                throw new TimeoutError($"owner application timed out for {sequence}: '{raw}'");
            }
            sleep(Math.Min(pollSeconds, remaining));
        }
    }
}
