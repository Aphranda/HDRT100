namespace DhrT100.SequenceDebug.Core;

/// <summary>Protocol constants shared by the UI, the builders and the parsers.</summary>
public static class SequenceConstants
{
    public const int MaxLogLines = 3000;

    /// <summary>Snapshot of SYNC_IO_SEQUENCE_TICKS_PER_US for the 4 ns PIO sequence clock.</summary>
    public const long TimeMaxUs = 0xffffffffL / 250;

    public const long CounterThresholdMax = 0xffffffdeL;

    /// <summary>Snapshot of SYNC_IO_SEQUENCE_CODE_MAX: a code is four bits wide.</summary>
    public const int CodeMax = 0x0F;

    /// <summary>Snapshot of SYNC_IO_SEQUENCE_CODE_SP8T_SHIFT. Bit 0 of a code drives
    /// OUT0 (the SP2T on the IN3 pad); bits 1..3 are the SP8T position, which is
    /// what <c>sequence_output_mask</c> gates.
    /// </summary>
    public const int CodeSp8tShift = 1;

    /// <summary>
    /// Snapshot of TRIGGER_SEQUENCE_CHANNEL_MAX. <c>CONF:TRIG</c> takes
    /// (chan_count, pol, freq, wave) — not the state count — and the firmware
    /// rejects chan_count above this. A 16-code SP8T+SP2T plan is therefore
    /// <c>CONF:TRIG 8,2,1,1</c> (pol=2 doubles the states).
    /// </summary>
    public const int ChannelMax = 8;

    public const string RoleSequence = "编码";
    public const string RoleStatus = "状态";
    public const string RoleGateway = "触发";

    public const string ModeIndependent = "独立 SP8T 序列";
    public const string ModeRj45 = "RJ45 物理回环 · SP8T + VNA 网关";
    public const string ModeTurntable = "转台计数 · RJ45 · SP8T + VNA 网关";

    public const string RingStatusQuery = "SYST:TDMA:RING:STAT?";

    /// <summary>SYSTem:TDMA:OPMode:* owns the profile catalog; this tool reads it instead of mirroring it.</summary>
    public const string OperatingProfileQuery = "SYST:TDMA:OPMODE?";

    public const string OperatingCatalogQuery = "SYST:TDMA:OPMODE:CATALOG?";
    public const int OperatingCatalogFields = 6;
    public const int OperatingProfileHilValidated = 0x1;
    public const int OperatingGuiMinBaudHz = 10_000_000;
    public const int OperatingGuiMaxBaudHz = 30_000_000;

    public static readonly string[] RingAckOnly =
    {
        "SYST:TDMA:RING:STOP", "SYST:TDMA:RING:ARM", "SYST:TDMA:RING:START",
    };

    public static readonly IReadOnlyDictionary<int, string> LinkPhases = new Dictionary<int, string>
    {
        [0] = "未启用",
        [1] = "等待启动",
        [2] = "等待链路通知回环",
        [3] = "等待 READY",
        [4] = "等待 READY 回环",
        [5] = "等待序列切换",
        [6] = "暂停",
        [7] = "异常",
        [8] = "已完成",
        [9] = "等待计数阈值",
        [10] = "等待计数通知回环",
        [11] = "等待下一位置重新武装",
    };

    /// <summary>
    /// Wire order of <c>SYST:TDMA:RING:STAT?</c>; kept identical to
    /// <c>tools/tdma_ring_monitor/tdma_field_parse.RUNTIME_FIELDS</c>.
    /// </summary>
    public static readonly string[] RuntimeFields =
    {
        "ring_enabled", "ring_node_count", "ring_local_slot_id", "ring_reference_slot_id",
        "ring_up_running", "ring_down_running", "ring_seq", "ring_last_error",
        "ring_adapter_started", "ring_adapter_service_count", "ring_up_tx_sequence",
        "ring_down_rx_sequence", "ring_up_tx_frame_crc32", "ring_down_rx_frame_crc32",
        "ring_idle_beacon_tx_count", "ring_idle_beacon_rx_count", "ring_feedback_round_trip_ns",
        "ring_timestamp_resolution_ns", "ring_timestamp_flags",
        "ring_reference_tx_timestamp_ns_lo", "ring_reference_tx_timestamp_ns_hi",
        "ring_feedback_rx_timestamp_ns_lo", "ring_feedback_rx_timestamp_ns_hi",
        "ring_adapter_last_error", "ring_adapter_tx_count", "ring_adapter_rx_count",
        "ring_adapter_rx_bad_count", "ring_adapter_rx_transport_bad_count",
        "ring_adapter_rx_schedule_bad_count", "ring_adapter_rx_profile_bad_count",
        "ring_adapter_last_bad_transport_result", "ring_adapter_last_bad_sequence",
        "ring_adapter_last_bad_schedule_crc32", "ring_adapter_last_bad_profile_crc32",
        "ring_adapter_last_bad_header_diff_count", "ring_adapter_last_bad_header_first_diff_offset",
        "ring_adapter_last_bad_header_expected_byte", "ring_adapter_last_bad_header_observed_byte",
        "ring_config_seq", "ring_applied_config_seq",
    };

    public static readonly int RuntimeFieldCount = RuntimeFields.Length;

    /// <summary>Values that mean "the device produced no answer for this command".</summary>
    public const string TimeoutResponse = "<timeout>";

    public static bool IsTimeout(string response)
    {
        return response == TimeoutResponse;
    }

    public static int Field(this int[] values, string name)
    {
        var index = IndexOf(name);
        if (index >= values.Length)
        {
            throw new ValueError($"TDMA 运行字段不存在：{name}");
        }
        return values[index];
    }

    public static long Field(this long[] values, string name)
    {
        var index = IndexOf(name);
        if (index >= values.Length)
        {
            throw new ValueError($"TDMA 运行字段不存在：{name}");
        }
        return values[index];
    }

    private static int IndexOf(string name)
    {
        var index = Array.IndexOf(RuntimeFields, name);
        if (index < 0)
        {
            throw new ValueError($"TDMA 运行字段不存在：{name}");
        }
        return index;
    }
}
