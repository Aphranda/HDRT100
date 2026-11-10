using System.Text;
using DhrT100.SequenceDebug.Core;
using DhrT100.SequenceDebug.Transport;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class ScpiSessionTests
{
    private sealed class FakeChannel : IScpiChannel
    {
        private readonly Queue<byte> _rx;

        public FakeChannel(string incoming)
        {
            _rx = new Queue<byte>(Encoding.UTF8.GetBytes(incoming));
        }

        public List<string> Writes { get; } = new();
        public int Recoveries { get; private set; }
        public int Prepares { get; private set; }

        /// <summary>Time advances only while the link is idle, like a real read slice.</summary>
        public double Clock { get; private set; }

        public string Endpoint => "FAKE";

        public int Read(byte[] buffer, int offset, int count)
        {
            if (_rx.Count == 0)
            {
                Clock += 0.02;
                return 0;
            }
            var read = 0;
            while (read < count && _rx.Count > 0)
            {
                buffer[offset + read] = _rx.Dequeue();
                read++;
            }
            return read;
        }

        public void Write(byte[] payload) => Writes.Add(Encoding.ASCII.GetString(payload));

        public void Flush()
        {
        }

        public void PrepareForWrite() => Prepares++;

        public void RecoverFromTimeout() => Recoveries++;

        public void Dispose()
        {
        }
    }

    private static ScpiSession Session(FakeChannel channel, double timeoutSeconds = 1.0)
    {
        return new ScpiSession(channel, timeoutSeconds, () => channel.Clock);
    }

    [Fact]
    public void Skips_diagnostic_log_lines_sharing_the_stream()
    {
        var channel = new FakeChannel("[ 7] INF SCPI service initialized\n[ 9] INF scpi initialized\nDHRT100,1,SN,1.0\n");
        var session = Session(channel);

        var response = session.Exchange("*IDN?");

        Assert.Equal("DHRT100,1,SN,1.0", response);
        Assert.Equal(new[] { "*IDN?\n" }, channel.Writes);
    }

    [Fact]
    public void A_bare_ack_line_before_the_data_is_skipped_for_a_query()
    {
        var channel = new FakeChannel("\"OK\"\nDHRT100,1,SN,1.0\n");
        var session = Session(channel);

        Assert.Equal("DHRT100,1,SN,1.0", session.Exchange("*IDN?"));
    }

    [Fact]
    public void A_bare_ack_is_consumed_by_the_write_that_produced_it()
    {
        var channel = new FakeChannel("\"OK\"\n");
        var session = Session(channel);

        Assert.Equal("\"OK\"", session.Exchange("SYST:USB:MODE USBTMC"));
        Assert.Equal(1, channel.Prepares);
    }

    [Fact]
    public void A_composite_reply_keeps_its_own_ok_field()
    {
        var channel = new FakeChannel("\"QUEUED\",2,0,0,7\n");
        var session = Session(channel);

        Assert.Equal("\"QUEUED\",2,0,0,7", session.Exchange("SYST:TDMA:RING:TOPOLOGY 2,0,0"));
    }

    [Fact]
    public void An_idle_gap_terminates_a_reply_without_a_newline()
    {
        var channel = new FakeChannel("\"OK\",1,2");
        var session = Session(channel);

        Assert.Equal("\"OK\",1,2", session.Exchange("SYST:ERR?"));
    }

    [Fact]
    public void A_missing_reply_times_out_and_recovers_the_link()
    {
        var channel = new FakeChannel(string.Empty);
        var session = Session(channel);

        Assert.Equal(SequenceConstants.TimeoutResponse, session.Exchange("SYST:TDMA:RING:STOP"));
        Assert.Equal(1, channel.Recoveries);
    }

    [Fact]
    public void An_embedded_log_fragment_is_trimmed_from_the_reply()
    {
        var channel = new FakeChannel("1[ 12] WRN scpi late\n");
        var session = Session(channel);

        Assert.Equal("1", session.Exchange("TRIG:START"));
    }

    [Fact]
    public void Stale_bytes_from_the_previous_transaction_are_dropped()
    {
        var channel = new FakeChannel("stale\n1\n");
        var session = Session(channel);

        // The first exchange consumes the stale line; the ACK is dropped after it.
        Assert.Equal("stale", session.Exchange("TRIG:PAUS"));
        Assert.Equal(SequenceConstants.TimeoutResponse, session.Exchange("TRIG:CONT"));
    }
}
