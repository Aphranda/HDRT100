using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class StatusFormatTests
{
    [Fact]
    public void Link_status_counts_every_measurement()
    {
        var row = new[]
        {
            1, 8, 0, 1, 2, 3, 4, 7, 48, 16, 0, 8, 8, 7, 2, 3, 0, 8, 10, 5000, 0, 1, 99, 2, 21, 1, 24,
        };
        var text = StatusFormat.FormatLinkStatus(string.Join(",", row), 8);
        Assert.Contains("已完成", text);
        Assert.Contains("轮次 1/1", text);
        Assert.Contains("切换完成 7", text);
        Assert.Contains("交换 99", text);
        Assert.Contains("延迟 2/21/1/24 ms", text);
        Assert.Throws<ValueError>(() => StatusFormat.FormatLinkStatus("1,8,0", 8));
    }

    [Fact]
    public void Counter_status_shows_progress_and_faults()
    {
        var text = StatusFormat.FormatCounterStatus("1,1,2,1000,12345,12,456,0,12,10,9,0");
        Assert.Contains("计数槽位 1", text);
        Assert.Contains("IN2", text);
        Assert.Contains("累计脉冲 12345", text);
        Assert.Contains("当前 456/1000", text);
        Assert.Contains("等待计数阈值", text);
        Assert.Throws<ValueError>(() => StatusFormat.FormatCounterStatus("1,2,3"));
    }

    [Fact]
    public void Counter_history_decodes_outcomes_and_labels_the_snapshot()
    {
        var text = StatusFormat.FormatCounterHistory("5,1,1,3,4,1000,955,7");
        Assert.Contains("记录 5", text);
        Assert.Contains("位置 3", text);
        Assert.Contains("序列索引 4", text);
        Assert.Contains("已请求切换 / 切换完成 / 采样完成", text);
        Assert.Contains("不是实际开关输出的物理边沿测量", text);

        var none = StatusFormat.FormatCounterHistory("1,1,1,0,0,10,0,0");
        Assert.Contains("无完成标记", none);
    }

    [Fact]
    public void Switch_position_uses_a_one_based_operator_position()
    {
        const string response =
            "\"READY\",1,1,8,0,0,1,4294967295,4294967295,4294967295,4294967295,0,0,0,0,0,0,0,\"NONE\",0,0,0,0,0";
        Assert.Equal("当前开关：第 1/8 位 （状态 0，运行态 READY）",
            StatusFormat.FormatSwitchPosition(response));
    }

    [Fact]
    public void Switch_position_reports_released_output_while_idle()
    {
        const string response = "\"IDLE\",1,1,8,1,1,2,1,1,1,1,0,1,1,0,0,0,0,\"NONE\",0,0,0,0,0";
        Assert.Equal("当前开关：未运行（输出已释放）", StatusFormat.FormatSwitchPosition(response));
    }

    [Fact]
    public void Switch_position_accepts_the_uint32_idle_sentinel()
    {
        // Captured from DHRT100 build 20261110154633: an idle sequence reports
        // 4294967295 for current_index, which does not fit a signed 32-bit int.
        const string response =
            "\"IDLE\",0,0,0,4294967295,4294967295,0,4294967295,4294967295,4294967295," +
            "4294967295,0,0,0,0,0,0,0,\"NONE\",0,0,0,0,0";
        Assert.Equal("当前开关：未运行（输出已释放）", StatusFormat.FormatSwitchPosition(response));
    }

    [Fact]
    public void Operating_status_accepts_the_device_crc_fields()
    {
        // Captured from the same build: the fifth field exceeds Int32.MaxValue.
        var text = OperatingProfile.FormatStatus(
            "0,10000000,2000000,4096,0,3449001068,0,10000000,2000000,4096,0,3449001068,0,0,0,0");
        Assert.Contains("当前 10MHz / 2ms", text);
        Assert.Contains("挡号 0", text);
    }

    [Theory]
    [InlineData("\"CDC\"", "CDC")]
    [InlineData("USBTMC", "USBTMC")]
    [InlineData(" \"cdc\" ", "CDC")]
    public void Usb_mode_parser_accepts_runtime_switch_responses(string response, string expected)
    {
        Assert.Equal(expected, StatusFormat.ParseUsbMode(response));
    }

    [Theory]
    [InlineData("\"UNKNOWN\"")]
    [InlineData("")]
    public void Usb_mode_parser_explains_unsupported_firmware(string response)
    {
        var exception = Assert.Throws<ValueError>(() => StatusFormat.ParseUsbMode(response));
        Assert.Contains("PROJECT_ENABLE_USB_RUNTIME_SWITCH", exception.Message);
    }
}
