using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class CommandBuilderTests
{
    [Fact]
    public void Maps_output_codes_to_positional_state_ids()
    {
        var commands = SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 1, 2, 4 }, "MANUAL", "RIS", 10, 5);

        Assert.Contains("CONF:TRIG 3,0,1,1", commands);
        Assert.Contains("CONF:SEQ PLAN,0,1,2", commands);
        Assert.Contains("CONF:SEQ:CODE 0,1", commands);
        Assert.Contains("CONF:SEQ:CODE 1,2", commands);
        Assert.Contains("CONF:SEQ:CODE 2,4", commands);
        Assert.Contains("CONF:SEQ:OUTPUT 7,8,PULSE,10,5", commands);
        Assert.DoesNotContain("CONF:SEQ PLAN,1,2,4", commands);
    }

    [Theory]
    [InlineData(new int[0])]
    [InlineData(new[] { -1 })]
    [InlineData(new[] { 16 })]
    public void Rejects_empty_or_unrepresentable_codes(int[] codes)
    {
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", codes, "MANUAL", "RIS", 10, 5));
    }

    [Fact]
    public void Accepts_the_full_four_bit_code_range()
    {
        // Firmware rule: (value >> SP8T_SHIFT) & ~sequence_output_mask == 0.
        // Bit 0 drives OUT0 (SP2T), so 0..15 fit a 3-bit SP8T mask.
        var codes = Enumerable.Range(0, 16).ToList();
        var commands = SequenceCommandBuilder.BuildConfigurationCommands(
            "SP8T", codes, "MANUAL", "RIS", 10, 5);
        // CONF:TRIG carries (channels, polarity): 16 codes = 8 channels × 2 pol.
        Assert.Contains("CONF:TRIG 8,2,1,1", commands);
        Assert.Contains("CONF:SEQ SP8T,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15", commands);
        Assert.Contains("CONF:SEQ:CODE 15,15", commands);
    }

    [Theory]
    [InlineData(1, 1, 0)]
    [InlineData(3, 3, 0)]
    [InlineData(8, 8, 0)]
    [InlineData(10, 5, 2)]
    [InlineData(16, 8, 2)]
    public void Trigger_parameters_are_channel_and_polarity_based(int codes, int channels, int polarity)
    {
        var (actualChannels, actualPolarity) = SequenceCommandBuilder.TriggerParameters(codes);
        Assert.Equal(channels, actualChannels);
        Assert.Equal(polarity, actualPolarity);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(9)]
    [InlineData(17)]
    [InlineData(18)]
    public void Trigger_parameters_reject_counts_above_the_channel_limit(int codes)
    {
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.TriggerParameters(codes));
    }

    [Fact]
    public void A_narrower_sp8t_mask_rejects_codes_above_it()
    {
        // mask 3 covers OUT1/OUT2 only, so codes 8..15 shift outside the mask.
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 8 }, "MANUAL", "RIS", 10, 5, 3, 12));
        SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 7 }, "MANUAL", "RIS", 10, 5, 3, 12);
    }

    [Fact]
    public void Builds_level_mode_and_rejects_overlapping_outputs()
    {
        var commands = SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 0, 1, 2, 3 }, "MANUAL", "RIS", 10, 99,
            3, 12, "电平");
        Assert.Contains("CONF:SEQ:OUTPUT 3,12,LEVEL,10,0", commands);

        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 0, 1 }, "MANUAL", "RIS", 10, 5, 3, 2));
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 8 }, "MANUAL", "RIS", 10, 5, 3, 12));
    }

    [Theory]
    [InlineData("无")]
    [InlineData("NONE")]
    [InlineData("none")]
    public void Dut_only_output_does_not_claim_a_status_mask(string mode)
    {
        var commands = SequenceCommandBuilder.BuildConfigurationCommands(
            "SP8T", Enumerable.Range(0, 8).ToList(), "IN1", "RIS", 10, 99, 7, 0, mode);
        Assert.Equal(new[] { "CONF:SEQ:OUTPUT 7,0,NONE,10,0" },
            commands.Where(command => command.StartsWith("CONF:SEQ:OUTPUT")).ToArray());
        Assert.Contains("CONF:SEQ:SOUR IN1,RIS", commands);
        Assert.Contains("CONF:SEQ:CODE 7,7", commands);
    }

    [Theory]
    [InlineData(1)]
    [InlineData(8)]
    [InlineData(-1)]
    [InlineData(16)]
    public void Rejects_a_contradictory_status_mask(int mask)
    {
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "SP8T", new[] { 0 }, "MANUAL", "RIS", 10, 0, 7, mask, "NONE"));
    }

    [Theory]
    [InlineData("PULSE")]
    [InlineData("LEVEL")]
    public void Requires_a_status_output_for_level_and_pulse_modes(string mode)
    {
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "SP8T", new[] { 0 }, "MANUAL", "RIS", 10, 5, 7, 0, mode));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    [InlineData(16)]
    public void Rejects_an_invalid_sequence_mask(int mask)
    {
        Assert.Throws<ValueError>(() => SequenceCommandBuilder.BuildConfigurationCommands(
            "SP8T", new[] { 0 }, "MANUAL", "RIS", 10, 0, mask, 0, "NONE"));
    }

    [Fact]
    public void Dut_only_may_use_all_outputs_for_encoding()
    {
        var commands = SequenceCommandBuilder.BuildConfigurationCommands(
            "PLAN", new[] { 0, 15 }, "IN4", "FALL", 0, 0, 15, 0, "NONE");
        Assert.Contains("CONF:SEQ:OUTPUT 15,0,NONE,0,0", commands);
    }

    private static List<string> TurntableConfiguration(AngleScan? angleScan = null,
                                                      int counterThreshold = 1000,
                                                      int repeatCount = 10)
    {
        return SequenceCommandBuilder.BuildModeConfiguration(
            SequenceConstants.ModeTurntable, "SP8T", Enumerable.Range(0, 8).ToList(),
            "MANUAL", "RIS", 25, 10, 7, 0, "NONE", "IN2", 5000, repeatCount, "OUT4",
            1, 2, 3, "IN1", counterThreshold, angleScan, 7);
    }

    [Fact]
    public void Turntable_configures_three_roles_and_physical_transport_without_starting()
    {
        var commands = TurntableConfiguration(counterThreshold: 1234);
        var counterRole = "CONF:SEQ:NODE:ROLE 1,2,COUNTER";
        Assert.Contains(counterRole, commands);
        Assert.True(commands.IndexOf(counterRole) < commands.IndexOf("CONF:SEQ:NODE:ACT"));
        Assert.Contains("CONF:SEQ:NODE:ROLE 2,5,DUT", commands);
        Assert.Contains("CONF:SEQ:NODE:ROLE 3,7,VNA", commands);
        Assert.Contains("CONF:SEQ:LINK POSITION,1,2,3,IN1,1234,IN2,OUT4,10,5000,RIS", commands);
        Assert.DoesNotContain(commands, command => command.EndsWith(":START", StringComparison.Ordinal));
    }

    [Fact]
    public void Angle_scan_overrides_raw_repeat_and_threshold_and_reads_back_the_real_config()
    {
        var scan = AngleScanCalculator.Calculate("0", "0.05", "0.05", "0.05", "1000");
        var commands = TurntableConfiguration(scan, counterThreshold: 999, repeatCount: 999);
        var link = "CONF:SEQ:LINK POSITION,1,2,3,IN1,50,IN2,OUT4,10,5000,RIS";
        var sweep = "CONF:ANGLE:SWEEP 0,0.05,0.05,0.05";

        Assert.Contains(link, commands);
        Assert.Contains("CONF:SEQ:REPEAT 2", commands);
        Assert.True(commands.IndexOf(link) < commands.IndexOf(sweep));
        Assert.True(commands.IndexOf(sweep) < commands.IndexOf("CONF:ANGLE:INPUT IN1,1000"));
        Assert.Equal(
            new[] { "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?", "READ:ANGLE:SPEED?", "READ:ANGLE:POSITION?" },
            commands.GetRange(commands.Count - 7, 4).ToArray());
        Assert.DoesNotContain(commands, command => command.EndsWith(":START", StringComparison.Ordinal));
        Assert.DoesNotContain(TurntableConfiguration(), command => command.Contains("ANGLE"));
    }

    [Fact]
    public void Combined_start_arms_and_trains_the_ring_before_the_first_state()
    {
        var expected = new[]
        {
            "SYST:TDMA:RING:STOP", "SYST:TDMA:RING:ARM", "SYST:TDMA:RING:TRAIN 4096",
            "SYST:TDMA:RING:START", "TRIG:START",
        };
        Assert.Equal(expected, SequenceCommandBuilder.BuildStartCommands(SequenceConstants.ModeRj45));
        Assert.Equal(expected, SequenceCommandBuilder.BuildStartCommands(SequenceConstants.ModeTurntable));
        Assert.Equal(new[] { "TRIG:START" },
            SequenceCommandBuilder.BuildStartCommands(SequenceConstants.ModeIndependent));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(8)]
    public void Accepts_explicit_repeat_counts(int repeat)
    {
        var commands = TurntableConfiguration(repeatCount: repeat);
        Assert.Contains($"CONF:SEQ:REPEAT {repeat}", commands);
    }

    [Fact]
    public void Accepts_the_maximum_repeat_count()
    {
        var commands = SequenceCommandBuilder.BuildModeConfiguration(
            SequenceConstants.ModeTurntable, "SP8T", Enumerable.Range(0, 1).ToList(), "MANUAL",
            "RIS", 25, 10, 7, 0, "NONE", "IN2", 5000, 0xffffffffL, "OUT4");
        Assert.Contains($"CONF:SEQ:REPEAT {0xffffffff}", commands);
    }

    [Fact]
    public void Combined_configuration_can_leave_the_ring_profile_alone()
    {
        var commands = SequenceCommandBuilder.BuildModeConfiguration(
            SequenceConstants.ModeRj45, "SP8T", Enumerable.Range(0, 8).ToList(), "MANUAL", "RIS",
            10, 10, 7, 0, "NONE", "IN1", 5000, 1, "OUT4", operatingLevel: null);
        Assert.DoesNotContain(commands, command => command.StartsWith("SYST:TDMA:OPMODE", StringComparison.Ordinal));
    }

    [Theory]
    [InlineData(10, 1000000, "10MHz / 1ms")]
    [InlineData(10, 500000, "10MHz / 500µs")]
    [InlineData(50, 100000, "50MHz / 100µs")]
    public void Operating_labels_describe_the_wire(int mhz, int cycleNs, string expected)
    {
        Assert.Equal(expected, OperatingProfile.LevelLabel(mhz * 1_000_000L, cycleNs));
    }
}
