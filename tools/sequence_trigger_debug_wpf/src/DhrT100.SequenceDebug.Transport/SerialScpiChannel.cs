using System.IO.Ports;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>USB CDC (virtual COM) channel. This is the backend the Python tool uses.</summary>
public sealed class SerialScpiChannel : IScpiChannel
{
    private readonly SerialPort _port;

    public SerialScpiChannel(string portName, int baud = 115200, int readSliceMs = 50,
                             int settleMs = 200)
    {
        Endpoint = portName;
        _port = new SerialPort(portName, baud)
        {
            ReadTimeout = Math.Max(10, readSliceMs),
            WriteTimeout = 2000,
            Parity = Parity.None,
            DataBits = 8,
            StopBits = StopBits.One,
            Handshake = Handshake.None,
            DtrEnable = true,
            RtsEnable = true,
        };
        try
        {
            _port.Open();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException
                                          or ArgumentException or InvalidOperationException)
        {
            throw new TransportException($"无法打开串口 {portName}：{exception.Message}", exception);
        }

        // A CDC port needs to settle before the device answers; the stale bytes
        // it emits on enumeration must not be mistaken for a reply.
        if (settleMs > 0)
        {
            Thread.Sleep(settleMs);
        }
        TryDiscard();
    }

    public string Endpoint { get; }

    public int Read(byte[] buffer, int offset, int count)
    {
        try
        {
            return _port.Read(buffer, offset, count);
        }
        catch (TimeoutException)
        {
            return 0;
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException
                                          or UnauthorizedAccessException)
        {
            throw new TransportException($"串口 {Endpoint} 读取失败：{exception.Message}", exception);
        }
    }

    public void Write(byte[] payload)
    {
        try
        {
            _port.Write(payload, 0, payload.Length);
        }
        catch (Exception exception) when (exception is IOException or InvalidOperationException
                                          or TimeoutException or UnauthorizedAccessException)
        {
            throw new TransportException($"串口 {Endpoint} 写入失败：{exception.Message}", exception);
        }
    }

    public void Flush()
    {
        try
        {
            _port.BaseStream.Flush();
        }
        catch (Exception)
        {
            // Best effort: the device may drop CDC immediately after a reset.
        }
    }

    public void PrepareForWrite() => TryDiscard();

    public void RecoverFromTimeout() => TryDiscard();

    private void TryDiscard()
    {
        try
        {
            if (_port.IsOpen)
            {
                _port.DiscardInBuffer();
            }
        }
        catch (Exception)
        {
            // Ignore: a vanished device is reported by the next read or write.
        }
    }

    public void Dispose()
    {
        try
        {
            if (_port.IsOpen)
            {
                _port.BaseStream.Flush();
            }
        }
        catch (Exception)
        {
        }
        try
        {
            _port.Close();
        }
        catch (Exception)
        {
            // A target-side USB reset intentionally invalidates the handle.
        }
        _port.Dispose();
    }
}
