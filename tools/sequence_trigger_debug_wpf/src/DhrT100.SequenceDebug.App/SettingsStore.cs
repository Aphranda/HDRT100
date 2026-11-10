using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using DhrT100.SequenceDebug.Core;

namespace DhrT100.SequenceDebug.App;

/// <summary>
/// Local GUI drafts only; never device state and never queued commands. The
/// schema, path and field names match the Python tool so both share one file.
/// </summary>
public static class SettingsStore
{
    public const int SchemaVersion = 1;
    public const int MaxSettingsBytes = 65536;

    public static string DefaultPath
    {
        get
        {
            var root = Environment.GetEnvironmentVariable("LOCALAPPDATA")
                       ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config");
            return Path.Combine(root, "DHRT100", "sequence_trigger_debug_ui.json");
        }
    }

    public static Dictionary<string, JsonNode?> Load(string path)
    {
        if (!File.Exists(path))
        {
            return new Dictionary<string, JsonNode?>(StringComparer.Ordinal);
        }

        var raw = ReadBounded(path);
        if (raw.Length > MaxSettingsBytes)
        {
            throw new ValueError("configuration file is too large");
        }

        JsonNode? root;
        try
        {
            root = JsonNode.Parse(raw);
        }
        catch (JsonException exception)
        {
            throw new ValueError("unsupported configuration schema", exception);
        }

        if (root is not JsonObject obj ||
            obj["version"] is not JsonValue version ||
            !version.TryGetValue<int>(out var number) ||
            number != SchemaVersion ||
            obj["values"] is not JsonObject values)
        {
            throw new ValueError("unsupported configuration schema");
        }

        var result = new Dictionary<string, JsonNode?>(StringComparer.Ordinal);
        foreach (var pair in values)
        {
            result[pair.Key] = pair.Value?.DeepClone();
        }
        return result;
    }

    public static void Save(string path, IReadOnlyDictionary<string, JsonNode?> values)
    {
        var payload = new JsonObject
        {
            ["version"] = SchemaVersion,
            ["values"] = BuildValues(values),
        };
        var raw = payload.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n";
        if (System.Text.Encoding.UTF8.GetByteCount(raw) > MaxSettingsBytes)
        {
            throw new ValueError("configuration file is too large");
        }

        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }
        var temporary = path + ".tmp";
        using (var stream = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var writer = new StreamWriter(stream, new System.Text.UTF8Encoding(false)))
        {
            writer.Write(raw);
            writer.Flush();
            stream.Flush(true);
        }
        File.Move(temporary, path, true);
    }

    private static JsonObject BuildValues(IReadOnlyDictionary<string, JsonNode?> values)
    {
        var result = new JsonObject();
        foreach (var pair in values)
        {
            result[pair.Key] = pair.Value?.DeepClone();
        }
        return result;
    }

    private static string ReadBounded(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        using var reader = new StreamReader(stream, System.Text.Encoding.UTF8, true);
        var buffer = new char[MaxSettingsBytes + 1];
        var read = reader.ReadBlock(buffer, 0, buffer.Length);
        return new string(buffer, 0, read);
    }
}
