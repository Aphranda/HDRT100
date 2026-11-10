using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// The TDMA operating ladder. The device owns the catalog; this type only
/// converts the wire terms (baud / cycle period) that identify a level.
/// </summary>
public static class OperatingProfile
{
    public sealed record Entry(long Level, long BaudHz, long CyclePeriodNs);

    /// <summary>Validate a numeric TDMA operating profile draft.</summary>
    public static int ParseLevel(string value, int? maximum = null)
    {
        var text = (value ?? string.Empty).Trim();
        var limit = maximum ?? 0xff;
        if (!IsAsciiDecimal(text) || text.Length > 3)
        {
            throw new ValueError($"TDMA 挡位须为 0–{limit} 的整数");
        }
        var level = int.Parse(text, CultureInfo.InvariantCulture);
        if (level < 0 || level > limit)
        {
            throw new ValueError($"TDMA 挡位须为 0–{limit} 的整数");
        }
        return level;
    }

    /// <summary>Semantic profile name: what the level means on the wire, not its index.</summary>
    public static string LevelLabel(long baudHz, long cyclePeriodNs)
    {
        var periodUs = cyclePeriodNs / 1000d;
        var period = periodUs >= 1000d && periodUs % 1000d == 0d
            ? $"{Format(periodUs / 1000d)}ms"
            : $"{Format(periodUs)}µs";
        return $"{Format(baudHz / 1e6)}MHz / {period}";
    }

    /// <summary>Resolve only a level advertised in the GUI's filtered device catalog.</summary>
    public static int ResolveLevel(string draft, IReadOnlyDictionary<string, int>? labels)
    {
        var text = (draft ?? string.Empty).Trim();
        if (labels is null || labels.Count == 0)
        {
            throw new ValueError("请先读取设备档位表，再选择 10–30 MHz 挡位");
        }
        if (labels.TryGetValue(text, out var level))
        {
            return level;
        }
        var known = labels.Values.ToHashSet();
        if (IsAsciiDecimal(text))
        {
            var number = int.Parse(text, CultureInfo.InvariantCulture);
            if (!known.Contains(number))
            {
                throw new ValueError($"第 {number} 挡不在可选的 10–30 MHz 档位表中");
            }
            return ParseLevel(text, known.Max());
        }
        throw new ValueError("请从设备档位表中选择一个已读回的档位");
    }

    /// <summary>Return (level, baud_hz, cycle_period_ns) for every advertised level.</summary>
    public static List<Entry> ParseCatalog(string response)
    {
        List<string> rawFields;
        try
        {
            rawFields = Csv.ParseLine(response);
        }
        catch (FormatException exception)
        {
            throw new ValueError("TDMA 档位表读回字段不匹配", exception);
        }

        long[] values;
        try
        {
            values = Csv.ParseUnsigned(rawFields);
        }
        catch (FormatException exception)
        {
            throw new ValueError("TDMA 档位表条目不匹配", exception);
        }

        if (values.Length == 0 || values[0] <= 0 ||
            values.Length != 1 + values[0] * SequenceConstants.OperatingCatalogFields)
        {
            throw new ValueError("TDMA 档位表读回字段不匹配");
        }

        var entries = new List<Entry>((int)values[0]);
        long previous = -1;
        for (var index = 0; index < values[0]; index++)
        {
            var offset = 1 + index * SequenceConstants.OperatingCatalogFields;
            // Ascending unique levels catch a shifted or truncated response.
            if (values[offset] <= previous)
            {
                throw new ValueError("TDMA 档位表条目不匹配");
            }
            previous = values[offset];
            entries.Add(new Entry(values[offset], values[offset + 1], values[offset + 2]));
        }
        return entries;
    }

    /// <summary>Keep device IDs while restricting GUI choices to the supported baud band.</summary>
    public static Dictionary<string, int> CatalogLabels(IEnumerable<Entry> entries)
    {
        var filtered = entries
            .Where(entry => entry.BaudHz >= SequenceConstants.OperatingGuiMinBaudHz &&
                            entry.BaudHz <= SequenceConstants.OperatingGuiMaxBaudHz)
            .ToList();
        var names = filtered.Select(entry => LevelLabel(entry.BaudHz, entry.CyclePeriodNs)).ToList();
        var labels = new Dictionary<string, int>(StringComparer.Ordinal);
        for (var index = 0; index < filtered.Count; index++)
        {
            var name = names[index];
            // Only a repeated baud/period pair keeps its index visible, so two
            // entries can never collapse into one choice.
            var label = names.Count(item => item == name) > 1
                ? $"{name} (#{filtered[index].Level})"
                : name;
            labels[label] = (int)filtered[index].Level;
        }
        return labels;
    }

    /// <summary>Describe the device ladder in wire terms instead of level indices.</summary>
    public static string FormatCatalog(IReadOnlyList<Entry> entries)
    {
        var ladder = string.Join(" · ",
            entries.Select(entry => LevelLabel(entry.BaudHz, entry.CyclePeriodNs)));
        return $"TDMA 档位表：{entries.Count} 档 —— {ladder}";
    }

    /// <summary>Format SYST:TDMA:OPMODE? (active, staged, then stage/apply/reject counts).</summary>
    public static string FormatStatus(string response)
    {
        long[] values;
        try
        {
            values = Csv.ParseUnsigned(Csv.ParseLine(response));
        }
        catch (FormatException exception)
        {
            throw new ValueError("TDMA 挡位读回字段不匹配", exception);
        }
        if (values.Length != 16)
        {
            throw new ValueError("TDMA 挡位读回字段不匹配");
        }

        var active = values[0..6];
        var staged = values[6..12];
        var stageCount = values[12];
        var applyCount = values[13];
        var rejectCount = values[14];
        var lastResult = values[15];
        var hil = (active[4] & SequenceConstants.OperatingProfileHilValidated) != 0
            ? "已 HIL 验证"
            : "未 HIL 验证";
        return $"TDMA 挡位：当前 {LevelLabel(active[1], active[2])}" +
               $"（挡号 {active[0]} · {hil}） · 暂存 {LevelLabel(staged[1], staged[2])}" +
               $"（挡号 {staged[0]}）\n" +
               $"配置事务：stage {stageCount} · apply {applyCount} · reject {rejectCount}" +
               $" · last_result {lastResult}";
    }

    private static string Format(double value)
    {
        // Python's ":g" keeps six significant digits and drops trailing zeros.
        return value.ToString("G6", CultureInfo.InvariantCulture);
    }

    private static bool IsAsciiDecimal(string text)
    {
        return text.Length > 0 && text.All(char.IsAsciiDigit);
    }
}
