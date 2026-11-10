using System.Diagnostics;
using System.Text;
using DhrT100.SequenceDebug.Core;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>
/// One command exchange over a byte channel. Mirrors the Python tool on purpose:
/// a reply is delimited by CR/LF <em>or</em> by a 50 ms idle gap, because the
/// firmware does not guarantee a trailing newline for every reply; diagnostic
/// log lines interleaved on the stream are skipped; a bare ACK is consumed
/// inside the write transaction that produced it.
/// </summary>
public sealed class ScpiSession : IDisposable
{
    public const double DefaultIdleGapSeconds = 0.05;

    private readonly IScpiChannel _channel;
    private readonly Func<double> _monotonic;
    private readonly Queue<byte> _pending = new();
    private readonly byte[] _slice = new byte[1024];
    private readonly double _timeoutSeconds;

    public ScpiSession(IScpiChannel channel, double timeoutSeconds, Func<double>? monotonic = null)
    {
        _channel = channel;
        _timeoutSeconds = timeoutSeconds;
        _monotonic = monotonic ?? (() => Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency);
    }

    public string Endpoint => _channel.Endpoint;

    public double IdleGapSeconds { get; init; } = DefaultIdleGapSeconds;

    /// <summary>Send one command and return its first non-log reply, or "&lt;timeout&gt;".</summary>
    public string Exchange(string command)
    {
        // These two produce no reply at all; reading would always time out.
        if (WriteOnlyHeaders.Contains(ScpiText.FirstToken(command)))
        {
            Write(command);
            return "已发送，等待读回";
        }

        _channel.PrepareForWrite();
        _channel.Write(Encoding.ASCII.GetBytes(command + "\n"));
        _channel.Flush();
        _pending.Clear();
        var response = ReadResponse(command);
        if (response is null)
        {
            _channel.RecoverFromTimeout();
            return SequenceConstants.TimeoutResponse;
        }
        return response;
    }

    private static readonly HashSet<string> WriteOnlyHeaders = new(StringComparer.OrdinalIgnoreCase)
    {
        "*CLS", "*RST",
    };

    /// <summary>Send without waiting for a reply (used for reboot commands).</summary>
    public void Write(string command)
    {
        _channel.PrepareForWrite();
        _channel.Write(Encoding.ASCII.GetBytes(command + "\n"));
        _channel.Flush();
    }

    private string? ReadResponse(string command)
    {
        var deadline = _monotonic() + _timeoutSeconds;
        var query = ScpiText.IsQuery(command);
        while (_monotonic() < deadline)
        {
            var line = ReadLine(deadline);
            if (line is null)
            {
                continue;
            }
            if (ScpiText.IsLogLine(line))
            {
                continue;
            }

            if (query)
            {
                line = ScpiText.StripQueryAck(ScpiText.TrimEmbeddedLog(line));
                if (line.Length == 0)
                {
                    continue;
                }
                return line;
            }

            line = ScpiText.TrimEmbeddedLog(line);
            if (line.Length == 0)
            {
                continue;
            }
            // A bare ACK terminates a write transaction; it must be consumed here
            // or it would be returned as the answer to the next query.
            if (ScpiText.IsBareAck(line) || line.StartsWith("\"OK\"", StringComparison.Ordinal) ||
                line.StartsWith("OK\"", StringComparison.Ordinal) ||
                line.StartsWith("\"OK[", StringComparison.Ordinal) ||
                line.StartsWith("OK[", StringComparison.Ordinal))
            {
                return "\"OK\"";
            }
            return line;
        }
        return null;
    }

    private string? ReadLine(double deadline)
    {
        var buffer = new List<byte>(64);
        var lastReceive = 0d;
        var received = false;
        while (_monotonic() < deadline)
        {
            var value = ReadByte();
            if (value < 0)
            {
                if (received && (_monotonic() - lastReceive) >= IdleGapSeconds)
                {
                    break;
                }
                continue;
            }
            buffer.Add((byte)value);
            received = true;
            lastReceive = _monotonic();
            if (value is '\n' or '\r')
            {
                break;
            }
        }
        if (buffer.Count == 0)
        {
            return null;
        }
        return Encoding.UTF8.GetString(buffer.ToArray()).Trim();
    }

    /// <summary>One buffered byte, or -1 when the channel had nothing in this slice.</summary>
    private int ReadByte()
    {
        if (_pending.Count == 0)
        {
            var read = _channel.Read(_slice, 0, _slice.Length);
            if (read <= 0)
            {
                return -1;
            }
            for (var index = 0; index < read; index++)
            {
                _pending.Enqueue(_slice[index]);
            }
        }
        return _pending.Dequeue();
    }

    public void Dispose()
    {
        _channel.Dispose();
    }
}
