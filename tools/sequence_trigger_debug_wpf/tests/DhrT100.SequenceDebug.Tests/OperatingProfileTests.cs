using DhrT100.SequenceDebug.Core;
using Xunit;

namespace DhrT100.SequenceDebug.Tests;

public sealed class OperatingProfileTests
{
    private static string CatalogRows(params (long Level, long Baud, long Cycle)[] entries)
    {
        var rows = new List<string> { entries.Length.ToString() };
        foreach (var (level, baud, cycle) in entries)
        {
            rows.Add(level.ToString());
            rows.Add(baud.ToString());
            rows.Add(cycle.ToString());
            rows.Add("4096");
            rows.Add("0");
            rows.Add("0x11223344".Replace("0x", string.Empty));
        }
        return string.Join(",", rows);
    }

    [Theory]
    [InlineData("0", 0)]
    [InlineData("7", 7)]
    [InlineData("255", 255)]
    [InlineData(" 21 ", 21)]
    public void Accepts_a_numeric_draft_without_a_catalog(string value, int expected)
    {
        Assert.Equal(expected, OperatingProfile.ParseLevel(value));
    }

    [Theory]
    [InlineData("")]
    [InlineData("no")]
    [InlineData("-1")]
    [InlineData("256")]
    [InlineData("1.5")]
    [InlineData("7x")]
    [InlineData("０")]
    [InlineData("1\n2")]
    public void Rejects_malformed_level_drafts(string value)
    {
        Assert.Throws<ValueError>(() => OperatingProfile.ParseLevel(value));
    }

    [Fact]
    public void Numeric_bound_follows_the_device_catalog()
    {
        Assert.Equal(21, OperatingProfile.ParseLevel("21", 21));
        Assert.Throws<ValueError>(() => OperatingProfile.ParseLevel("22", 21));
    }

    [Fact]
    public void Catalog_parses_every_advertised_level()
    {
        var entries = OperatingProfile.ParseCatalog(CatalogRows(
            (0, 10000000, 2000000), (7, 10000000, 1000000),
            (20, 10000000, 500000), (21, 10000000, 250000)));
        Assert.Equal(4, entries.Count);
        Assert.Equal(7, entries[1].Level);

        var labels = OperatingProfile.CatalogLabels(entries);
        Assert.Equal(0, labels["10MHz / 2ms"]);
        Assert.Equal(7, labels["10MHz / 1ms"]);
        Assert.Equal(20, labels["10MHz / 500µs"]);
        Assert.Equal(21, labels["10MHz / 250µs"]);

        var text = OperatingProfile.FormatCatalog(entries);
        Assert.Contains("4 档", text);
        Assert.Contains("10MHz / 500µs", text);
        Assert.DoesNotContain("0:", text);
    }

    [Fact]
    public void Repeated_profiles_keep_their_level_visible()
    {
        var labels = OperatingProfile.CatalogLabels(new[]
        {
            new OperatingProfile.Entry(3, 10000000, 1000000),
            new OperatingProfile.Entry(8, 10000000, 1000000),
        });
        Assert.Equal(3, labels["10MHz / 1ms (#3)"]);
        Assert.Equal(8, labels["10MHz / 1ms (#8)"]);
    }

    [Fact]
    public void Resolve_accepts_labels_and_catalog_levels_only()
    {
        var labels = new Dictionary<string, int>(StringComparer.Ordinal)
        {
            ["10MHz / 1ms"] = 7,
            ["10MHz / 500µs"] = 20,
        };
        Assert.Equal(20, OperatingProfile.ResolveLevel("10MHz / 500µs", labels));
        Assert.Equal(20, OperatingProfile.ResolveLevel("20", labels));
        Assert.Throws<ValueError>(() => OperatingProfile.ResolveLevel("21", labels));
        Assert.Throws<ValueError>(() => OperatingProfile.ResolveLevel("35MHz / 1ms", labels));
        Assert.Throws<ValueError>(() =>
            OperatingProfile.ResolveLevel("21", new Dictionary<string, int>()));
    }

    [Fact]
    public void Catalog_filters_inclusively_without_renumbering()
    {
        var entries = new[]
        {
            new OperatingProfile.Entry(0, 5_000_000, 2000000),
            new OperatingProfile.Entry(3, 10_000_000, 1000000),
            new OperatingProfile.Entry(8, 20_000_000, 500000),
            new OperatingProfile.Entry(12, 30_000_000, 250000),
            new OperatingProfile.Entry(16, 35_000_000, 100000),
            new OperatingProfile.Entry(21, 10_000_000, 250000),
        };
        var labels = OperatingProfile.CatalogLabels(entries);
        Assert.Equal(new[] { 3, 8, 12, 21 }, labels.Values.ToArray());
        Assert.Equal(12, OperatingProfile.ResolveLevel("30MHz / 250µs", labels));
        foreach (var draft in new[] { "0", "16", "35MHz / 100µs" })
        {
            Assert.Throws<ValueError>(() => OperatingProfile.ResolveLevel(draft, labels));
        }
        Assert.Empty(OperatingProfile.CatalogLabels(new[]
        {
            new OperatingProfile.Entry(0, 50_000_000, 100000),
        }));
    }

    [Theory]
    [InlineData("")]
    [InlineData("0")]
    [InlineData("1,0,10000000,1000000,4096,0")]
    [InlineData("1,0,10000000")]
    [InlineData("1,0,10000000,1000000,4096,-1")]
    [InlineData("1,0,10000000,1000000,4096,4294967296")]
    [InlineData("2,0,10000000,1000000,4096,0")]
    [InlineData("2,7,10000000,1000000,4096,0,7,10000000,1000000,4096,0")]
    public void Catalog_rejects_malformed_readback(string response)
    {
        Assert.Throws<ValueError>(() => OperatingProfile.ParseCatalog(response));
    }

    [Fact]
    public void Status_names_the_tier_hil_state_and_transactions()
    {
        var fast = OperatingProfile.FormatStatus(
            "15,40000000,100000,4096,1,456,15,40000000,100000,4096,0,456,5,5,1,3");
        Assert.Contains("当前 40MHz / 100µs", fast);
        Assert.Contains("挡号 15", fast);
        Assert.Contains("已 HIL 验证", fast);
        Assert.Contains("暂存 40MHz / 100µs", fast);

        var slow = OperatingProfile.FormatStatus(
            "7,10000000,1000000,4096,0,123,9,25000000,1000000,4096,0,321,3,2,1,0");
        Assert.Contains("当前 10MHz / 1ms", slow);
        Assert.Contains("未 HIL 验证", slow);
        Assert.Contains("暂存 25MHz / 1ms", slow);
        Assert.Contains("reject 1", slow);
    }

    [Theory]
    [InlineData("1,2,3")]
    [InlineData("")]
    [InlineData("not,numbers")]
    [InlineData("1,2,3,4,5,-1,7,8,9,10,11,12,13,14,15,16")]
    public void Status_rejects_malformed_readback(string response)
    {
        Assert.Throws<ValueError>(() => OperatingProfile.FormatStatus(response));
    }
}
