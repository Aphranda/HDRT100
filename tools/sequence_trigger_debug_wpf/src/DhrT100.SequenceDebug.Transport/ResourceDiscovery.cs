using System.Text;
using System.Text.RegularExpressions;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>Transport backend selected in the connection bar.</summary>
public enum Backend
{
    Serial,
    UsbTmc,
}

public static class BackendNames
{
    public const string Serial = "Serial";
    public const string UsbTmc = "USB TMC";

    public static Backend Parse(string? value)
    {
        return string.Equals(value?.Trim(), UsbTmc, StringComparison.OrdinalIgnoreCase)
            ? Backend.UsbTmc
            : Backend.Serial;
    }

    public static string Format(Backend backend) => backend == Backend.UsbTmc ? UsbTmc : Serial;
}

/// <summary>Endpoint discovery for both backends.</summary>
public static class ResourceDiscovery
{
    private static readonly Guid PortsClassGuid = new("4d36e978-e325-11ce-bfc1-08002be10318");
    private static readonly Regex PortPattern = new(@"\((COM\d+)\)", RegexOptions.IgnoreCase | RegexOptions.Compiled);
    private static readonly Regex VidPattern = new(@"VID_([0-9A-Fa-f]{4})", RegexOptions.Compiled);

    /// <summary>Product VID of the board's CDC/RP2350 USB stack.</summary>
    public const int PreferredVid = 0x2e8a;

    public static IReadOnlyList<string> Discover(Backend backend)
    {
        return backend == Backend.UsbTmc ? DiscoverVisaResources() : DiscoverSerialPorts();
    }

    public static IReadOnlyList<string> DiscoverSerialPorts()
    {
        var ports = EnumeratePorts();
        if (ports.Count == 0)
        {
            // SetupAPI gives the VID; without it fall back to the plain list so
            // a locked-down machine still sees its ports.
            ports = System.IO.Ports.SerialPort.GetPortNames()
                .Select(name => new PortInfo(name, null))
                .ToList();
        }
        return ports
            .OrderBy(port => port.Vid == PreferredVid ? 0 : 1)
            .ThenBy(port => PortNumber(port.Name))
            .ThenBy(port => port.Name, StringComparer.OrdinalIgnoreCase)
            .Select(port => port.Name)
            .ToList();
    }

    public static IReadOnlyList<string> DiscoverVisaResources()
    {
        var resources = new List<string>();
        var status = VisaNative.viOpenDefaultRM(out var resourceManager);
        if (status < 0)
        {
            throw new TransportException(
                $"无法初始化 VISA（visa32.dll）：{VisaNative.Describe(0, status)}");
        }

        var findList = 0;
        try
        {
            var description = new StringBuilder(VisaNative.ViFindBufLen);
            status = VisaNative.viFindRsrc(resourceManager, "USB?*::INSTR", out findList,
                                           out var count, description);
            if (status < 0 || count <= 0)
            {
                return resources;
            }
            resources.Add(description.ToString());
            for (var index = 1; index < count; index++)
            {
                var next = new StringBuilder(VisaNative.ViFindBufLen);
                if (VisaNative.viFindNext(findList, next) < 0)
                {
                    break;
                }
                resources.Add(next.ToString());
            }
        }
        finally
        {
            if (findList != 0)
            {
                VisaNative.viClose(findList);
            }
            VisaNative.viClose(resourceManager);
        }

        resources.Sort(StringComparer.OrdinalIgnoreCase);
        return resources;
    }

    private static List<PortInfo> EnumeratePorts()
    {
        var result = new List<PortInfo>();
        var classGuid = PortsClassGuid;
        var deviceInfoSet = SetupApiNative.SetupDiGetClassDevs(ref classGuid, IntPtr.Zero, IntPtr.Zero,
                                                               SetupApiNative.DigcfPresent);
        if (deviceInfoSet == IntPtr.Zero || deviceInfoSet == new IntPtr(-1))
        {
            return result;
        }

        try
        {
            var data = new SetupApiNative.SpDevInfoData { Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf<SetupApiNative.SpDevInfoData>() };
            for (uint index = 0; SetupApiNative.SetupDiEnumDeviceInfo(deviceInfoSet, index, ref data); index++)
            {
                var friendly = ReadProperty(deviceInfoSet, ref data, SetupApiNative.SpdrpFriendlyName);
                if (friendly is null)
                {
                    continue;
                }
                var match = PortPattern.Match(friendly);
                if (!match.Success)
                {
                    continue;
                }
                var instance = new StringBuilder(256);
                int? vid = null;
                if (SetupApiNative.SetupDiGetDeviceInstanceId(deviceInfoSet, ref data, instance,
                        (uint)instance.Capacity, out _))
                {
                    var vidMatch = VidPattern.Match(instance.ToString());
                    if (vidMatch.Success && int.TryParse(vidMatch.Groups[1].Value,
                            System.Globalization.NumberStyles.HexNumber, null, out var value))
                    {
                        vid = value;
                    }
                }
                result.Add(new PortInfo(match.Groups[1].Value.ToUpperInvariant(), vid));
            }
        }
        finally
        {
            SetupApiNative.SetupDiDestroyDeviceInfoList(deviceInfoSet);
        }
        return result;
    }

    private static string? ReadProperty(IntPtr deviceInfoSet, ref SetupApiNative.SpDevInfoData data, uint property)
    {
        var buffer = new byte[512];
        if (!SetupApiNative.SetupDiGetDeviceRegistryProperty(deviceInfoSet, ref data, property, out _,
                buffer, (uint)buffer.Length, out var required) || required == 0)
        {
            return null;
        }
        var text = Encoding.Unicode.GetString(buffer, 0, (int)Math.Min(required, buffer.Length));
        var terminator = text.IndexOf('\0');
        return (terminator >= 0 ? text[..terminator] : text).Trim();
    }

    private static int PortNumber(string name)
    {
        var digits = new string(name.Where(char.IsAsciiDigit).ToArray());
        return int.TryParse(digits, out var number) ? number : int.MaxValue;
    }

    private sealed record PortInfo(string Name, int? Vid);
}
