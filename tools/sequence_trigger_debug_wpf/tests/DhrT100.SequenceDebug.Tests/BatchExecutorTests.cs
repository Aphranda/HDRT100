using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class BatchExecutorTests
{
    private static void Execute(IReadOnlyList<string> commands, Func<string, string> exchange)
    {
        new BatchExecutor(exchange, (_, _) => { }, sleep: _ => { }).Execute(commands);
    }

    [Fact]
    public void Waits_for_the_stopped_ack_and_rejects_unactivated_roles()
    {
        var calls = new List<string>();
        var states = new Queue<string>(new[] { "\"BUSY\",1,1,8", "\"IDLE\",1,1,8" });

        string Exchange(string command)
        {
            calls.Add(command);
            return command switch
            {
                "TRIG:STOP" => "1",
                "SYST:ERR?" => "0,\"No error\"",
                "CONF:SEQ:NODE:ACT" => "\"REJECTED\"",
                _ => states.Count > 0 ? states.Dequeue() : "1",
            };
        }

        var error = Assert.Throws<RuntimeError>(
            () => Execute(new[] { "TRIG:STOP", "CONF:SEQ:NODE:ACT", "TRIG:START" }, Exchange));
        Assert.Contains("未激活", error.Message);
        Assert.Equal(new[] { "TRIG:STOP", "SYST:ERR?", "TRIG:SEQ:NEXT?", "TRIG:SEQ:NEXT?" },
            calls.Take(4).ToArray());
        Assert.DoesNotContain("TRIG:START", calls);
    }

    [Theory]
    [InlineData("\"STAGED\"", true)]
    [InlineData("\"REJECTED\"", false)]
    [InlineData("1", false)]
    public void Role_staging_uses_the_real_response(string stageResponse, bool accepted)
    {
        var calls = new List<string>();
        string Exchange(string command)
        {
            calls.Add(command);
            if (command == "SYST:ERR?")
            {
                return "0,\"No error\"";
            }
            if (command == "CONF:SEQ:NODE:ROLE 2,5,DUT")
            {
                return stageResponse;
            }
            return "\"ACTIVE\",1";
        }

        var commands = new[] { "CONF:SEQ:NODE:ROLE 2,5,DUT", "CONF:SEQ:NODE:ACT" };
        if (accepted)
        {
            Execute(commands, Exchange);
            Assert.Contains("CONF:SEQ:NODE:ACT", calls);
        }
        else
        {
            var error = Assert.Throws<RuntimeError>(() => Execute(commands, Exchange));
            Assert.Contains("未暂存", error.Message);
            Assert.DoesNotContain("CONF:SEQ:NODE:ACT", calls);
        }
    }

    private static readonly string[] AngleCommands =
    {
        "CONF:ANGLE:SWEEP 0,0.05,0.05,0.05", "CONF:ANGLE:INPUT IN1,1000",
        "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?",
    };

    private static readonly Dictionary<string, string> AngleReadbacks = new(StringComparer.Ordinal)
    {
        ["READ:ANGLE:SWEEP?"] = "0,0.05,0.05,0.05,2,1",
        ["READ:ANGLE:INPUT?"] = "\"IN1\",1000,50,50,1,1,1",
    };

    [Theory]
    [InlineData("READ:ANGLE:SWEEP?", "0,0.05,0.05,0.05,2,0")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",1000,50,50,1,1,0")]
    [InlineData("READ:ANGLE:SWEEP?", "0,0.1,0.05,0.05,3,1")]
    [InlineData("READ:ANGLE:SWEEP?", "0,0.05,0.05,0.1,2,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN2\",1000,50,50,1,1,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",2000,50,50,1,1,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",1000,100,50,1,1,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",1000,50,100,1,1,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",1000,50,50,2,1,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",1000,50,50,1,2,1")]
    [InlineData("READ:ANGLE:INPUT?", "\"IN1\",1000,NaN,50,1,1,1")]
    [InlineData("READ:ANGLE:SWEEP?", "1")]
    [InlineData("READ:ANGLE:INPUT?", "1")]
    [InlineData("READ:ANGLE:INPUT?", "")]
    public void Angle_readback_rejects_unbound_stale_and_invalid_data(string key, string value)
    {
        var replies = new Dictionary<string, string>(AngleReadbacks, StringComparer.Ordinal)
        {
            [key] = value,
        };
        string Exchange(string command)
        {
            return command == "SYST:ERR?" ? "0,\"No error\"" : replies.GetValueOrDefault(command, "1");
        }

        var error = Assert.Throws<RuntimeError>(() => Execute(AngleCommands, Exchange));
        Assert.Contains("ANGLE 配置读回校验失败", error.Message);
    }

    [Fact]
    public void Angle_readback_accepts_a_matching_configuration()
    {
        string Exchange(string command)
        {
            return command == "SYST:ERR?" ? "0,\"No error\"" : AngleReadbacks.GetValueOrDefault(command, "1");
        }
        Execute(AngleCommands, Exchange);
    }

    [Fact]
    public void Angle_setters_require_a_positive_ack_and_stop_the_batch()
    {
        foreach (var ack in new[] { "", "0", "STAGED", "<timeout>" })
        {
            foreach (var setter in AngleCommands.Take(2))
            {
                var calls = new List<string>();
                string Exchange(string command)
                {
                    calls.Add(command);
                    if (command == setter)
                    {
                        return ack;
                    }
                    return command == "SYST:ERR?" ? "0,\"No error\"" : AngleReadbacks.GetValueOrDefault(command, "1");
                }

                Assert.ThrowsAny<Exception>(() => Execute(AngleCommands, Exchange));
                Assert.DoesNotContain("READ:ANGLE:SWEEP?", calls);
            }
        }
    }

    [Fact]
    public void Angle_configuration_requires_post_write_readbacks()
    {
        string Exchange(string command)
        {
            return command == "SYST:ERR?" ? "0,\"No error\"" : AngleReadbacks.GetValueOrDefault(command, "1");
        }

        var missing = Assert.Throws<RuntimeError>(() => Execute(AngleCommands.Take(2).ToArray(), Exchange));
        Assert.Contains("缺少下发完成后的", missing.Message);

        var reordered = AngleCommands.Skip(2).Concat(AngleCommands.Take(2)).ToArray();
        var stale = Assert.Throws<RuntimeError>(() => Execute(reordered, Exchange));
        Assert.Contains("缺少下发完成后的", stale.Message);

        // A manual query of the ANGLE readbacks carries no expectation.
        Execute(new[] { "READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?" }, _ => "0,0,0,0,0,0");
    }

    [Fact]
    public void Ring_ack_only_commands_require_a_verified_state_and_never_exempt_trigger_timeout()
    {
        var row = SequenceConstants.RuntimeFields.ToDictionary(name => name, _ => 0L);
        row["ring_config_seq"] = 3;
        row["ring_applied_config_seq"] = 2;
        var calls = new List<string>();

        string Exchange(string command)
        {
            calls.Add(command);
            if (command == SequenceConstants.RingStatusQuery)
            {
                var response = string.Join(",", SequenceConstants.RuntimeFields.Select(name => row[name]));
                row["ring_applied_config_seq"] = 3;
                return response;
            }
            return command == "SYST:ERR?" ? "0,\"No error\"" : "<timeout>";
        }

        Execute(new[] { "SYST:TDMA:RING:STOP" }, Exchange);
        Assert.Equal(new[]
        {
            "SYST:TDMA:RING:STOP", "SYST:ERR?", SequenceConstants.RingStatusQuery,
            SequenceConstants.RingStatusQuery,
        }, calls);

        var error = Assert.Throws<RuntimeError>(
            () => Execute(new[] { "TRIG:START", "TRIG:SEQ:NEXT" }, Exchange));
        Assert.Contains("超时", error.Message);
        Assert.DoesNotContain("TRIG:SEQ:NEXT", calls);
    }

    [Theory]
    [InlineData("1,3,0," + "0,0,0,0,0,0,0,0,0,0,0,0,0," + "1," + "0,0,0,0,0,0,0,0,0,0", "不是 MANUAL")]
    [InlineData("1,7,2," + "0,0,0,0,0,0,0,0,0,0,0,0,0,0," + "0,0,0,0,0,0,0,0,0,0", "尚不能接受 NEXT")]
    public void Next_preflights_the_link_row(string linkRow, string expected)
    {
        var calls = new List<string>();
        string Exchange(string command)
        {
            calls.Add(command);
            return command == "READ:SEQ:LINK?" ? linkRow : "1";
        }

        var error = Assert.Throws<RuntimeError>(() => Execute(new[] { "TRIG:SEQ:NEXT" }, Exchange));
        Assert.Contains(expected, error.Message);
        Assert.Equal(new[] { "READ:SEQ:LINK?" }, calls);
    }

    [Fact]
    public void Topology_owner_completion_gates_dependent_configuration()
    {
        var calls = new List<string>();
        var states = new Queue<string>(new[] { "7,6,1,1,1", "7,7,1,2,1", "7,7,1,2,0" });

        string Exchange(string command)
        {
            calls.Add(command);
            if (command == "SYST:TDMA:RING:TOPOLOGY 2,0,0")
            {
                return "\"QUEUED\",2,0,0,7";
            }
            if (command == "SYSTem:SYNC:VDC:DPLL:OWNer?")
            {
                return states.Count > 0 ? states.Dequeue() : "7,7,1,2,0";
            }
            if (command == "SYST:ERR?")
            {
                return "0,\"No error\"";
            }
            return "1";
        }

        var commands = new[] { "SYST:TDMA:RING:TOPOLOGY 2,0,0", "CAL:TOPOLOGY:PROBE 1,10" };
        Execute(commands, Exchange);
        Assert.Equal(5, calls.IndexOf(commands[1]));
    }

    [Fact]
    public void Topology_owner_rejection_stops_the_batch()
    {
        var calls = new List<string>();
        var states = new Queue<string>(new[] { "7,7,1,3,0" });
        string Exchange(string command)
        {
            calls.Add(command);
            if (command == "SYST:TDMA:RING:TOPOLOGY 2,0,0")
            {
                return "\"QUEUED\",2,0,0,7";
            }
            if (command == "SYSTem:SYNC:VDC:DPLL:OWNer?")
            {
                return states.Count > 0 ? states.Dequeue() : "7,7,1,3,0";
            }
            if (command == "SYST:ERR?")
            {
                return "0,\"No error\"";
            }
            return "1";
        }

        var commands = new[] { "SYST:TDMA:RING:TOPOLOGY 2,0,0", "CAL:TOPOLOGY:PROBE 1,10" };
        var error = Assert.Throws<RuntimeError>(() => Execute(commands, Exchange));
        Assert.Contains("rejected", error.Message);
        Assert.DoesNotContain(commands[1], calls);
    }

    [Fact]
    public void Device_rejection_stops_the_batch()
    {
        var calls = new List<string>();
        string Exchange(string command)
        {
            calls.Add(command);
            return command switch
            {
                "SYST:ERR?" => "-113,\"Undefined header\"",
                _ => "1",
            };
        }
        var error = Assert.Throws<RuntimeError>(
            () => Execute(new[] { "CONF:SEQ:ACT SP8T", "TRIG:START" }, Exchange));
        Assert.Contains("被设备拒绝", error.Message);
        Assert.DoesNotContain("TRIG:START", calls);
    }

    [Fact]
    public void Start_requires_released_outputs()
    {
        // Captured behaviour: the firmware's outputs_available() demands every SMA
        // output to read low, and a leftover manual SP8T position is reported as a
        // silent rejection (no reply at all).
        var calls = new List<string>();
        string Exchange(string command)
        {
            calls.Add(command);
            return command switch
            {
                "READ:IO:STAT?" => "4,1,0,0,0",
                "SYST:ERR?" => "0,\"No error\"",
                _ => "<timeout>",
            };
        }

        var error = Assert.Throws<RuntimeError>(() => Execute(new[] { "TRIG:START" }, Exchange));
        Assert.Contains("输出必须全部为低", error.Message);
        Assert.Contains("0x1", error.Message);
        Assert.Contains("释放输出", error.Message);
        Assert.DoesNotContain("TRIG:START", calls);
    }

    [Fact]
    public void Start_is_sent_once_the_outputs_are_low()
    {
        var calls = new List<string>();
        string Exchange(string command)
        {
            calls.Add(command);
            return command switch
            {
                "READ:IO:STAT?" => "4,0,0,0,0",
                "SYST:ERR?" => "0,\"No error\"",
                _ => "1",
            };
        }

        Execute(new[] { "TRIG:START" }, Exchange);
        Assert.Contains("TRIG:START", calls);
    }

    [Fact]
    public void A_stale_device_error_is_not_blamed_on_the_next_command()
    {
        var calls = new List<string>();
        var errors = new Queue<string>(new[]
        {
            "-200,\"Execution error\"", "0,\"No error\"", "0,\"No error\"",
        });
        string Exchange(string command)
        {
            calls.Add(command);
            return command == "SYST:ERR?" && errors.Count > 0 ? errors.Dequeue() : "1";
        }

        new BatchExecutor(Exchange, (_, _) => { }, sleep: _ => { }, drainErrorsFirst: true)
            .Execute(new[] { "TRIG:PAUS" });

        // Two SYST:ERR? calls drain the queue before the command, then one after.
        Assert.Equal(new[] { "SYST:ERR?", "SYST:ERR?", "TRIG:PAUS", "SYST:ERR?" }, calls);
    }
}
