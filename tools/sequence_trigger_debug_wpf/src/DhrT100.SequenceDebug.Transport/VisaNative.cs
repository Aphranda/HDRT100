using System.Runtime.InteropServices;
using System.Text;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>
/// Direct P/Invoke to the installed VISA implementation (NI-VISA or the IVI
/// shared components that dispatch to it). No managed wrapper and no pyvisa:
/// the app owns the terminator, timeout and clear behaviour of every exchange.
/// </summary>
internal static class VisaNative
{
    public const int ViSuccess = 0;
    public const int ViSuccessMaxCnt = 0x3FFF0006;
    public const int ViErrorTmo = unchecked((int)0xBFFF0015);
    public const int ViErrorIo = unchecked((int)0xBFFF003E);
    public const int ViNull = 0;

    public const int ViAttrTmoValue = 0x3FFF001A;
    public const int ViAttrTermChar = 0x3FFF0018;
    public const int ViAttrTermCharEn = 0x3FFF0038;
    public const int ViAttrSendEndEn = 0x3FFF0016;

    public const int ViFindBufLen = 256;
    /// <summary>VI_NULL access mode: no lock, no retry on a busy device.</summary>
    public const int ViNullAccessMode = 0;
    public const int ViDefaultTmo = 2000;

    [DllImport("visa32.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    public static extern int viOpenDefaultRM(out int sesn);

    [DllImport("visa32.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    public static extern int viFindRsrc(int sesn, string expr, out int findList, out int retCnt,
                                        StringBuilder desc);

    [DllImport("visa32.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    public static extern int viFindNext(int findList, StringBuilder desc);

    [DllImport("visa32.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    public static extern int viOpen(int sesn, string name, int accessMode, int timeout, out int vi);

    [DllImport("visa32.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int viClose(int vi);

    [DllImport("visa32.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int viWrite(int vi, byte[] buffer, uint count, out uint retCount);

    [DllImport("visa32.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int viRead(int vi, byte[] buffer, uint count, out uint retCount);

    [DllImport("visa32.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int viSetAttribute(int vi, int attrName, uint attrValue);

    [DllImport("visa32.dll", CallingConvention = CallingConvention.StdCall)]
    public static extern int viClear(int vi);

    [DllImport("visa32.dll", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    public static extern int viStatusDesc(int vi, int status, StringBuilder desc);

    /// <summary>True when this status is a timeout rather than a transport fault.</summary>
    public static bool IsTimeout(int status) => status == ViErrorTmo;

    public static string Describe(int session, int status)
    {
        var builder = new StringBuilder(256);
        try
        {
            if (viStatusDesc(session, status, builder) != ViSuccess || builder.Length == 0)
            {
                return $"VISA status 0x{status:X8}";
            }
            return $"VISA status 0x{status:X8}: {builder}";
        }
        catch (Exception)
        {
            return $"VISA status 0x{status:X8}";
        }
    }
}
