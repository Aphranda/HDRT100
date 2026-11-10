namespace DhrT100.SequenceDebug.Transport;

/// <summary>
/// USBTMC channel through the installed VISA DLL (NI-VISA / IVI shared
/// components). Reads are terminated by LF; a timeout triggers
/// <c>viClear</c>, which aborts the pending bulk-in and clears the device
/// output buffer so the following exchange cannot be shifted by one reply.
/// </summary>
public sealed class VisaScpiChannel : IScpiChannel
{
    private int _resourceManager;
    private int _session;
    private bool _disposed;
    private readonly int _writeGapMs;

    public VisaScpiChannel(string resource, int timeoutMs = 2000)
    {
        Endpoint = resource;
        // Diagnostic/mitigation knob: the device answers some setter commands
        // only after the host has already issued Bulk-In, and a request that
        // arrives before the reply stays pending until the host times out.
        // DHRT100_TMC_WRITE_GAP_MS inserts a short pause between write and read.
        _writeGapMs = Math.Clamp(
            int.TryParse(Environment.GetEnvironmentVariable("DHRT100_TMC_WRITE_GAP_MS"),
                out var gap) ? gap : 0, 0, 2000);
        var status = VisaNative.viOpenDefaultRM(out _resourceManager);
        if (status < 0)
        {
            throw new TransportException(
                $"无法初始化 VISA（visa32.dll）：{VisaNative.Describe(0, status)}。" +
                "请确认已安装 NI-VISA 或 IVI VISA 运行库。");
        }

        status = VisaNative.viOpen(_resourceManager, resource, VisaNative.ViNullAccessMode, timeoutMs,
                                   out _session);
        if (status < 0)
        {
            var detail = VisaNative.Describe(_resourceManager, status);
            VisaNative.viClose(_resourceManager);
            _resourceManager = 0;
            throw new TransportException($"无法打开 VISA 资源 {resource}：{detail}");
        }

        // The device does not guarantee a trailing LF on every reply (setter
        // acknowledgements arrive as a bare "1"). With TERMCHAR enabled NI-VISA
        // waits the whole timeout for the missing terminator and returns the
        // bytes late, which costs ~2 s per configuration write. End the read on
        // the USBTMC short packet (EOM) instead and let ScpiSession frame lines.
        TrySet(VisaNative.ViAttrTermChar, (uint)'\n');
        TrySet(VisaNative.ViAttrTermCharEn, 0);
        TrySet(VisaNative.ViAttrSendEndEn, 1);
        TrySet(VisaNative.ViAttrTmoValue, (uint)timeoutMs);
    }

    public string Endpoint { get; }

    public int Read(byte[] buffer, int offset, int count)
    {
        if (offset != 0)
        {
            throw new ArgumentException("VISA 读取只支持 offset=0", nameof(offset));
        }
        var status = VisaNative.viRead(_session, buffer, (uint)count, out var received);
        if (status == VisaNative.ViSuccess || status == VisaNative.ViSuccessMaxCnt)
        {
            return (int)received;
        }
        if (VisaNative.IsTimeout(status))
        {
            return 0;
        }
        throw new TransportException($"VISA 读取失败 {Endpoint}：{VisaNative.Describe(_session, status)}");
    }

    public void Write(byte[] payload)
    {
        var status = VisaNative.viWrite(_session, payload, (uint)payload.Length, out var written);
        if (status < 0)
        {
            throw new TransportException($"VISA 写入失败 {Endpoint}：{VisaNative.Describe(_session, status)}");
        }
        if (written != payload.Length)
        {
            throw new TransportException(
                $"VISA 写入不完整 {Endpoint}：{written}/{payload.Length} 字节");
        }
        if (_writeGapMs > 0)
        {
            Thread.Sleep(_writeGapMs);
        }
    }

    public void Flush()
    {
        // viWrite is synchronous for USBTMC; there is no user-space buffer.
    }

    public void PrepareForWrite()
    {
        // Deliberately empty. A USBTMC bulk-in request that the device cannot
        // satisfy stays pending (tud_usbtmc_msgBulkIn_request_cb returns true
        // without data), so a speculative drain read would swallow the next
        // reply. Stale data is removed by RecoverFromTimeout instead.
    }

    public void RecoverFromTimeout()
    {
        try
        {
            // Abort the pending bulk-in and clear the device FIFOs.
            VisaNative.viClear(_session);
        }
        catch (Exception)
        {
            // The device may be rebooting; the next open reports the real state.
        }
    }

    private void TrySet(int attribute, uint value)
    {
        try
        {
            VisaNative.viSetAttribute(_session, attribute, value);
        }
        catch (Exception)
        {
            // Terminator support is a convenience; the idle-gap framing remains.
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        if (_session != 0)
        {
            try
            {
                VisaNative.viClose(_session);
            }
            catch (Exception)
            {
            }
            _session = 0;
        }
        if (_resourceManager != 0)
        {
            try
            {
                VisaNative.viClose(_resourceManager);
            }
            catch (Exception)
            {
            }
            _resourceManager = 0;
        }
    }
}
