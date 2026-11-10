using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// Minimal CSV reader matching the Python <c>csv.reader</c> subset this tool
/// relies on: comma separated fields, optional double quoted fields, doubled
/// quotes as escapes. Malformed quoting raises <see cref="FormatException"/>,
/// which is what <c>strict=True</c> did in Python.
/// </summary>
public static class Csv
{
    public static List<string> ParseLine(string line)
    {
        var fields = new List<string>();
        if (string.IsNullOrEmpty(line))
        {
            return fields;
        }

        var index = 0;
        while (true)
        {
            string field;
            if (line[index] == '"')
            {
                index++;
                var builder = new System.Text.StringBuilder();
                while (true)
                {
                    if (index >= line.Length)
                    {
                        throw new FormatException("CSV 引号未闭合");
                    }
                    if (line[index] == '"')
                    {
                        if (index + 1 < line.Length && line[index + 1] == '"')
                        {
                            builder.Append('"');
                            index += 2;
                            continue;
                        }
                        index++;
                        break;
                    }
                    builder.Append(line[index]);
                    index++;
                }
                if (index < line.Length && line[index] != ',')
                {
                    throw new FormatException("CSV 引号后缺少分隔符");
                }
                field = builder.ToString();
            }
            else
            {
                var start = index;
                while (index < line.Length && line[index] != ',')
                {
                    index++;
                }
                field = line[start..index];
            }

            fields.Add(field);
            if (index >= line.Length)
            {
                break;
            }
            index++; // 逗号
            if (index == line.Length)
            {
                fields.Add(string.Empty);
                break;
            }
        }

        return fields;
    }

    /// <summary>Parse one integer field the way Python <c>int(text)</c> does.</summary>
    public static int ParseInt(string text)
    {
        return int.Parse(text.Trim(), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture);
    }

    /// <summary>Parse one unsigned 32-bit field; firmware wire fields are uint32.</summary>
    public static long ParseUnsigned(string text)
    {
        var trimmed = text.Trim();
        if (trimmed.Length == 0 || !trimmed.All(char.IsAsciiDigit))
        {
            throw new FormatException($"非无符号整数：'{text}'");
        }
        var value = long.Parse(trimmed, NumberStyles.None, CultureInfo.InvariantCulture);
        if (value > 0xffffffffL)
        {
            throw new FormatException($"超出 uint32：'{text}'");
        }
        return value;
    }

    /// <summary>Parse a comma separated integer row (Python <c>map(int, text.split(","))</c>).</summary>
    public static int[] SplitInts(string text)
    {
        var parts = text.Split(',');
        var values = new int[parts.Length];
        for (var index = 0; index < parts.Length; index++)
        {
            values[index] = ParseInt(parts[index]);
        }
        return values;
    }

    /// <summary>Parse a comma separated uint32 row.</summary>
    public static long[] SplitUnsigned(string text)
    {
        var parts = text.Split(',');
        var values = new long[parts.Length];
        for (var index = 0; index < parts.Length; index++)
        {
            values[index] = ParseUnsigned(parts[index]);
        }
        return values;
    }

    public static int[] ParseInts(IEnumerable<string> fields)
    {
        return fields.Select(ParseInt).ToArray();
    }

    public static long[] ParseUnsigned(IEnumerable<string> fields)
    {
        return fields.Select(ParseUnsigned).ToArray();
    }
}
