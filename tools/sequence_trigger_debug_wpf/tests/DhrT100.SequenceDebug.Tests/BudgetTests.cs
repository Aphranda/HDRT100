using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class BudgetTests
{
    private const int ClockHz = 1_000_000;

    private static Budget.Table EvenTable(int cycleUs = 1000, int windowUs = 100, int wcetUs = 10)
    {
        return Budget.TableFromMicroseconds(ClockHz, cycleUs.ToString(),
            Enumerable.Repeat(windowUs.ToString(), Budget.Phases.Length - 1).ToList(),
            Enumerable.Repeat(wcetUs.ToString(), Budget.Phases.Length - 1).ToList());
    }

    [Fact]
    public void Table_from_microseconds_keeps_every_window_and_guards_the_remainder()
    {
        var table = EvenTable();
        Assert.Equal(1000, table.Cycle);
        Assert.Equal(10, table.Phases.Count);
        Assert.Equal(new Budget.PhaseWindow(0, 100, 10), table.Phases[0]);
        Assert.Equal(new Budget.PhaseWindow(900, 1000, 0), table.Phases[^1]);
        Budget.ValidateTable(table);
    }

    [Fact]
    public void Table_equality_is_member_wise()
    {
        var left = EvenTable();
        var right = EvenTable();
        Assert.Equal(left, right);
        Assert.NotEqual(left, EvenTable(wcetUs: 11));
    }

    [Theory]
    [InlineData("0.5")]
    [InlineData("-1")]
    [InlineData("abc")]
    public void Cycles_rejects_non_integral_or_negative_budgets(string value)
    {
        Assert.Throws<ValueError>(() => Budget.Cycles(value, 1_000_000));
    }

    [Fact]
    public void Cycles_requires_an_integral_clock_count()
    {
        Assert.Equal(100, Budget.Cycles("100", 1_000_000));
        Assert.Throws<ValueError>(() => Budget.Cycles("0.5", 1_000_000));
    }

    [Fact]
    public void Microseconds_renders_a_fixed_point_text()
    {
        Assert.Equal("1000", Budget.Microseconds(1000, 1_000_000));
        Assert.Equal("0.5", Budget.Microseconds(1, 2_000_000));
    }

    [Fact]
    public void Validate_rejects_a_gap_and_an_oversized_wcet()
    {
        var table = EvenTable();
        var gap = new Budget.Table(table.Cycle, new[]
        {
            new Budget.PhaseWindow(0, 100, 10),
            new Budget.PhaseWindow(150, 200, 10),
            new Budget.PhaseWindow(200, 300, 10),
            new Budget.PhaseWindow(300, 400, 10),
            new Budget.PhaseWindow(400, 500, 10),
            new Budget.PhaseWindow(500, 600, 10),
            new Budget.PhaseWindow(600, 700, 10),
            new Budget.PhaseWindow(700, 800, 10),
            new Budget.PhaseWindow(800, 900, 10),
            new Budget.PhaseWindow(900, 1000, 0),
        });
        Assert.Throws<ValueError>(() => Budget.ValidateTable(gap));

        var oversized = new Budget.Table(table.Cycle, new[]
        {
            new Budget.PhaseWindow(0, 100, 200),
            new Budget.PhaseWindow(100, 200, 10),
            new Budget.PhaseWindow(200, 300, 10),
            new Budget.PhaseWindow(300, 400, 10),
            new Budget.PhaseWindow(400, 500, 10),
            new Budget.PhaseWindow(500, 600, 10),
            new Budget.PhaseWindow(600, 700, 10),
            new Budget.PhaseWindow(700, 800, 10),
            new Budget.PhaseWindow(800, 900, 10),
            new Budget.PhaseWindow(900, 1000, 0),
        });
        Assert.Throws<ValueError>(() => Budget.ValidateTable(oversized));
    }

    private static string Snapshot(Budget.Table table, int clockHz = ClockHz, int quarantined = 0,
                                   int misses = 0)
    {
        var values = new List<int> { 2, clockHz, table.Cycle, Budget.Phases.Length, 0, quarantined, 0, misses };
        foreach (var phase in table.Phases)
        {
            values.Add(phase.Start);
            values.Add(phase.End);
            values.Add(phase.Wcet);
            for (var index = 0; index < 8; index++)
            {
                values.Add(0);
            }
        }
        return string.Join(",", values);
    }

    [Fact]
    public void Snapshot_round_trips()
    {
        var table = EvenTable();
        var parsed = Budget.ParseSnapshot(Snapshot(table, quarantined: 5, misses: 2));
        Assert.Equal(ClockHz, parsed.ClockHz);
        Assert.Equal(table, parsed.Table);
        Assert.Equal(5, parsed.QuarantinedMask);
        Assert.Equal(2, parsed.ScheduleMisses);
        Assert.Throws<ValueError>(() => Budget.ParseSnapshot("2,1,1000,10,0,0,0,0"));
    }

    private static string StatusRow(bool persisted)
    {
        return $"1,0,0,0,0,{(persisted ? 1 : 0)},0,0,0,0,0,1";
    }

    private static Func<string, string> FakeExchange(Budget.Snapshot snapshot, bool persisted)
    {
        return command => command switch
        {
            Budget.Prefix + "?" => Snapshot(snapshot.Table, snapshot.ClockHz),
            Budget.Prefix + ":STATUS?" => StatusRow(persisted),
            "SYST:TDMA:PERIOD?" => "0,0,0,0,0",
            Budget.Prefix + ":CAPABILITIES?" => "1,1",
            _ => "0",
        };
    }

    [Fact]
    public void Parses_the_live_schedule_row()
    {
        // Captured from DHRT100 build 20261110154633: 118 fields, 250 MHz clock,
        // 375000-cycle period, GUARD ending exactly at the cycle with WCET 0.
        const string row =
            "2,250000000,375000,10,91,72,894637,69547,0,217500,212500,144,30033,722563,894637,0,0," +
            "4613,4590,217500,260500,42000,217501,2105,10617,890029,4608,4608,0,0,260500,295500," +
            "34000,260509,4434,51057,890295,4342,4342,1,1,295500,310500,14000,0,0,0,0,894637,0,0,0," +
            "310500,315500,4000,310508,5983,5983,28,894609,0,1,1,315500,340500,24000,315502,719," +
            "17432,891574,3063,3063,0,0,340500,343500,3000,0,0,0,0,894636,0,0,0,343500,365500,21800," +
            "343667,41870,41870,669316,225320,52916,1,3,365500,367500,2000,0,0,0,0,894636,0,0,0," +
            "367500,375000,0,0,0,0,0,0,0,0,0";

        var snapshot = Budget.ParseSnapshot(row);
        Assert.Equal(250_000_000, snapshot.ClockHz);
        Assert.Equal(375_000, snapshot.Table.Cycle);
        Assert.Equal(Budget.Phases.Length, snapshot.Table.Phases.Count);
        Assert.Equal(new Budget.PhaseWindow(0, 217_500, 212_500), snapshot.Table.Phases[0]);
        Assert.Equal(367_500, snapshot.Table.Phases[^1].Start);
        Assert.Equal(375_000, snapshot.Table.Phases[^1].End);
        Assert.Equal(0, snapshot.Table.Phases[^1].Wcet);
    }

    [Fact]
    public void Read_operation_reports_the_live_budget()
    {
        var snapshot = new Budget.Snapshot(ClockHz, EvenTable(), 3, 1);
        var result = Budget.RunOperation("read", new Dictionary<string, object?>(),
            FakeExchange(snapshot, true), (_, _) => { }, () => { }, sleep: _ => { });
        Assert.Equal("read", result.Action);
        Assert.True(result.Persisted);
        Assert.Equal(snapshot.Table, result.Snapshot.Table);
    }

    [Fact]
    public void Save_requires_an_applied_edit_first()
    {
        var baseline = new Budget.Snapshot(ClockHz, EvenTable());
        var edited = Budget.TableFromMicroseconds(ClockHz, "1000",
            Enumerable.Repeat("100", 9).ToList(), Enumerable.Repeat("20", 9).ToList());
        var payload = new Dictionary<string, object?>
        {
            ["baseline"] = baseline,
            ["table"] = edited,
        };
        var error = Assert.Throws<ValueError>(() => Budget.RunOperation("save", payload,
            FakeExchange(baseline, true), (_, _) => { }, () => { }, sleep: _ => { }));
        Assert.Contains("尚未临时应用", error.Message);
    }

    [Fact]
    public void Operation_requires_capabilities_before_any_write()
    {
        var baseline = new Budget.Snapshot(ClockHz, EvenTable());
        var commands = new List<string>();
        string Exchange(string command)
        {
            commands.Add(command);
            return command == Budget.Prefix + ":CAPABILITIES?" ? "1,0" : "0";
        }

        var payload = new Dictionary<string, object?>
        {
            ["baseline"] = baseline,
            ["table"] = baseline.Table,
        };
        var error = Assert.Throws<RuntimeError>(() => Budget.RunOperation("apply", payload,
            Exchange, (_, _) => { }, () => { }, sleep: _ => { }));
        Assert.Contains("未确认临时应用", error.Message);
        Assert.Equal(new[] { Budget.Prefix + ":CAPABILITIES?" }, commands);
    }
}
