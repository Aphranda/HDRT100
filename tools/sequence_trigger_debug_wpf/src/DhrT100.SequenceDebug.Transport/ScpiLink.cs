using DhrT100.SequenceDebug.Core;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>
/// An open command connection. One link serves one operation batch, exactly like
/// the Python tool opening the port per transaction.
/// </summary>
public sealed class ScpiLink : IDisposable
{
    private readonly ScpiSession _session;

    private ScpiLink(ScpiSession session)
    {
        _session = session;
    }

    public string Endpoint => _session.Endpoint;

    /// <summary>
    /// Open a link. A non-positive <paramref name="timeoutMs"/> falls back to
    /// <c>DHRT100_SCPI_TIMEOUT_MS</c> (default 2000), which keeps the timeout
    /// tunable for bench diagnosis without rebuilding the UI.
    /// </summary>
    public static ScpiLink Open(Backend backend, string resource, int timeoutMs = 0)
    {
        if (string.IsNullOrWhiteSpace(resource))
        {
            throw new TransportException("请先扫描并选择通信资源");
        }
        if (timeoutMs <= 0)
        {
            timeoutMs = int.TryParse(Environment.GetEnvironmentVariable("DHRT100_SCPI_TIMEOUT_MS"),
                out var configured) ? Math.Clamp(configured, 200, 60000) : 2000;
        }

        IScpiChannel channel = backend == Backend.UsbTmc
            ? new VisaScpiChannel(resource.Trim(), timeoutMs)
            : new SerialScpiChannel(resource.Trim(), 115200, 50, 200);
        return new ScpiLink(new ScpiSession(channel, timeoutMs / 1000.0));
    }

    /// <summary>Send one command and return its reply, or "&lt;timeout&gt;".</summary>
    public string Exchange(string command)
    {
        return _session.Exchange(command);
    }

    /// <summary>Send a command without reading its reply.</summary>
    public void WriteOnly(string command)
    {
        _session.Write(command);
    }

    public void Dispose()
    {
        _session.Dispose();
    }
}
