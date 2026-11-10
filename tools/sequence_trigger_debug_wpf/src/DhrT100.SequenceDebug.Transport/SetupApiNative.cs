using System.Runtime.InteropServices;
using System.Text;

namespace DhrT100.SequenceDebug.Transport;

/// <summary>SetupAPI enumeration used to prefer the board's own CDC port.</summary>
internal static class SetupApiNative
{
    public const uint DigcfPresent = 0x2;
    public const uint SpdrpFriendlyName = 0x0000000C;
    public const uint SpdrpHardwareId = 0x00000001;

    [StructLayout(LayoutKind.Sequential)]
    public struct SpDevInfoData
    {
        public uint Size;
        public Guid ClassGuid;
        public uint DeviceInstance;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator,
                                                   IntPtr parent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiEnumDeviceInfo(IntPtr deviceInfoSet, uint memberIndex,
                                                   ref SpDevInfoData deviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetupDiGetDeviceInstanceId(IntPtr deviceInfoSet,
                                                        ref SpDevInfoData deviceInfoData,
                                                        StringBuilder instanceId,
                                                        uint instanceIdSize,
                                                        out uint requiredSize);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool SetupDiGetDeviceRegistryProperty(IntPtr deviceInfoSet,
                                                               ref SpDevInfoData deviceInfoData,
                                                               uint property,
                                                               out uint propertyRegDataType,
                                                               byte[]? propertyBuffer,
                                                               uint propertyBufferSize,
                                                               out uint requiredSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);
}
