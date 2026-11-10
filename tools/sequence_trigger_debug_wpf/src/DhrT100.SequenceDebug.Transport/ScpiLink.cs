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

    public static ScpiLink Open(Backend backend, string resource, int timeoutMs = 2000)
    {
        if (string.IsNullOrWhiteSpace(resource))
        {
            throw new TransportException("请先扫描并选择通信资源");
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
