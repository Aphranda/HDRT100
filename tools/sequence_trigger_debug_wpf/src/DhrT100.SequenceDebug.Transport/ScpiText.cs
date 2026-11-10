using System.Text;
using System.Text.RegularExpressions;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>
/// Line semantics shared with the Python helpers: the device emits diagnostic
/// log text on the same stream as SCPI replies, and some replies carry a leading
/// ACK field.
/// </summary>
public static class ScpiText
{
    private static readonly Regex EmbeddedLog = new(
        @"(?<!^)\[\s*\d+\]\s+(?:DBG|INF|WRN|ERR)\s+", RegexOptions.Compiled);

    /// <summary>Python <c>str.split(maxsplit=1)[0]</c>.</summary>
    public static string FirstToken(string command)
    {
        var trimmed = (command ?? string.Empty).Trim();
        if (trimmed.Length == 0)
        {
            return string.Empty;
        }
        var index = 0;
        while (index < trimmed.Length && !char.IsWhiteSpace(trimmed[index]))
        {
            index++;
        }
        return trimmed[..index];
    }

    /// <summary>True when the SCPI header itself is a query, not when a parameter contains '?'.</summary>
    public static bool IsQuery(string command)
    {
        return FirstToken(command).Contains('?');
    }

    public static bool IsLogLine(string line)
    {
        var text = (line ?? string.Empty).Trim();
        var maybeLog = text.StartsWith('"') && text.Length > 1 && text[1] == '[' ? text[1..] : text;
        return text.Length == 0 ||
               maybeLog.StartsWith('[') ||
               maybeLog.StartsWith("log:", StringComparison.Ordinal) ||
               maybeLog.Contains(" initialized", StringComparison.Ordinal) ||
               maybeLog.Contains(" service initialized", StringComparison.Ordinal);
    }

    public static string TrimEmbeddedLog(string line)
    {
        var text = line ?? string.Empty;
        var match = EmbeddedLog.Match(text);
        return (match.Success ? text[..match.Index] : text).Trim();
    }

    public static string StripAckPrefix(string line)
    {
        var text = (line ?? string.Empty).Trim();
        if (text is "\"OK\"" or "OK" or "OK\"")
        {
            return string.Empty;
        }
        if (text.StartsWith("\"OK[", StringComparison.Ordinal) ||
            text.StartsWith("OK[", StringComparison.Ordinal))
        {
            return string.Empty;
        }
        if (text.StartsWith("\"OK\"[", StringComparison.Ordinal))
        {
            return text[4..].Trim();
        }
        if (text.StartsWith("OK\"[", StringComparison.Ordinal))
        {
            return text[3..].Trim();
        }
        if (text.StartsWith("\"OK\"", StringComparison.Ordinal) && text.Length > 4)
        {
            return text[4..].Trim();
        }
        return text;
    }

    /// <summary>
    /// Query-side ACK removal. A composite reply may legitimately begin with its
    /// own "OK" field (<c>"OK",2,0,0,7</c>), so only a bare ACK or a fused
    /// log fragment is dropped here.
    /// </summary>
    public static string StripQueryAck(string line)
    {
        var text = (line ?? string.Empty).Trim();
        if (text is "\"OK\"" or "OK" or "OK\"")
        {
            return string.Empty;
        }
        if (text.StartsWith("\"OK[", StringComparison.Ordinal) ||
            text.StartsWith("OK[", StringComparison.Ordinal) ||
            text.StartsWith("\"OK[\"", StringComparison.Ordinal))
        {
            return string.Empty;
        }
        return text;
    }

    public static bool IsBareAck(string line)
    {
        var text = (line ?? string.Empty).Trim();
        return text is "\"OK\"" or "OK" or "OK\"";
    }
}
