using System.Globalization;

namespace DhrT100.SequenceDebug.Core;

/// <summary>Core1 schedule editor model: applying changes RAM; only SAVE writes device Flash.</summary>
public static class Budget
{
    public const string Prefix = "SYST:TDMA:SCHEDULE";

    public static readonly string[] Phases =
    {
        "TDMA", "VDC", "DPLL", "CALIBRATION", "SYNC_CAPTURE", "REFMEM",
        "MODEL", "SYNC_TRIGGER", "TRIGGER_MEASURE", "GUARD",
    };

    public sealed record PhaseWindow(int Start, int End, int Wcet);

    public sealed record Table(int Cycle, IReadOnlyList<PhaseWindow> Phases)
    {
        // Member-wise value equality: the record's generated comparer would
        // compare the phase list by reference and never match a re-read table.
        public bool Equals(Table? other)
        {
            return other is not null && Cycle == other.Cycle && Phases.SequenceEqual(other.Phases);
        }

        public override int GetHashCode() => HashCode.Combine(Cycle, Phases.Count);
    }

    public sealed record Snapshot(int ClockHz, Table Table, int QuarantinedMask = 0, int ScheduleMisses = 0);

    public static void ValidateTable(Table table)
    {
        if (table.Phases.Count != Phases.Length)
        {
            throw new ValueError("预算阶段数量不匹配");
        }
        if (table.Cycle <= 0 || table.Cycle > 0xffffff)
        {
            throw new ValueError("总周期超出设备计数范围");
        }

        var previous = 0;
        for (var index = 0; index < Phases.Length; index++)
        {
            var phase = table.Phases[index];
            if (phase.Start != previous || phase.Start >= phase.End || phase.End > table.Cycle ||
                phase.Wcet < 0 || phase.Wcet > phase.End - phase.Start)
            {
                throw new ValueError($"{Phases[index]}：时窗必须连续且为正，WCET 不得超出时窗");
            }
            previous = phase.End;
        }
        if (previous != table.Cycle || table.Phases[^1].Wcet != 0)
        {
            throw new ValueError("GUARD 必须结束于总周期，且 WCET 为 0");
        }
    }

    public static Snapshot ParseSnapshot(string response)
    {
        var values = Csv.ParseLine(response).Select(Csv.ParseInt).ToArray();
        if (values.Length != 8 + Phases.Length * 11 || values[0] != 2 || values[1] <= 0 ||
            values[3] != Phases.Length)
        {
            throw new ValueError("设备调度快照格式不支持");
        }

        var phases = new List<PhaseWindow>(Phases.Length);
        for (var index = 0; index < Phases.Length; index++)
        {
            var offset = 8 + index * 11;
            phases.Add(new PhaseWindow(values[offset], values[offset + 1], values[offset + 2]));
        }
        var table = new Table(values[2], phases);
        ValidateTable(table);
        return new Snapshot(values[1], table, values[5], values[7]);
    }

    public static string Microseconds(int cycles, int clockHz)
    {
        var value = (decimal)cycles * 1000000m / clockHz;
        return value.ToString("0.############################", CultureInfo.InvariantCulture);
    }

    public static int Cycles(string value, int clockHz)
    {
        if (!decimal.TryParse((value ?? string.Empty).Trim(), NumberStyles.Float,
                              CultureInfo.InvariantCulture, out var number) ||
            number < 0m)
        {
            throw new ValueError("预算必须是计数范围内的非负数");
        }

        var maximum = (decimal)0xffffff * 1000000m / clockHz;
        if (number > maximum)
        {
            throw new ValueError("预算必须是计数范围内的非负数");
        }
        var result = number * clockHz / 1000000m;
        if (result != decimal.Truncate(result))
        {
            throw new ValueError("预算必须对应整数个设备时钟周期");
        }
        return (int)result;
    }

    /// <summary>Keep every entered phase budget; only GUARD consumes the remainder.</summary>
    public static Table TableFromMicroseconds(int clockHz, string cycleUs,
                                             IReadOnlyList<string> windowsUs,
                                             IReadOnlyList<string> wcetsUs)
    {
        if (clockHz <= 0 || windowsUs.Count != Phases.Length - 1 ||
            wcetsUs.Count != windowsUs.Count)
        {
            throw new ValueError("请先读取设备时钟和完整预算");
        }

        var cycle = Cycles(cycleUs, clockHz);
        var phases = new List<PhaseWindow>(Phases.Length);
        var start = 0;
        for (var index = 0; index < windowsUs.Count; index++)
        {
            var end = start + Cycles(windowsUs[index], clockHz);
            phases.Add(new PhaseWindow(start, end, Cycles(wcetsUs[index], clockHz)));
            start = end;
        }
        phases.Add(new PhaseWindow(start, cycle, 0));
        var table = new Table(cycle, phases);
        ValidateTable(table);
        return table;
    }

    public sealed record OperationResult(string Action, Snapshot Snapshot, string Status, bool Persisted);

    /// <summary>
    /// Runs in the serial worker. <paramref name="stop"/> must stop trigger/ring
    /// and verify ACKs. Firmware capability 1,1 guarantees APPLY/CLEAR are
    /// volatile and SAVE is explicit; nothing is written before this preflight.
    /// </summary>
    public static OperationResult RunOperation(string action, IReadOnlyDictionary<string, object?> payload,
                                               Func<string, string> exchange,
                                               Action<string, string> emit,
                                               Action stop,
                                               Func<double>? monotonic = null,
                                               Action<double>? sleep = null)
    {
        monotonic ??= () => Environment.TickCount64 / 1000.0;
        sleep ??= seconds => Thread.Sleep((int)Math.Round(Math.Max(0d, seconds) * 1000d));
        if (action is not ("read" or "validate" or "apply" or "save"))
        {
            throw new ValueError("未知预算操作");
        }

        string Query(string command)
        {
            var response = exchange(command);
            emit(command, response);
            if (string.IsNullOrEmpty(response) || SequenceConstants.IsTimeout(response))
            {
                throw new RuntimeError($"{command} 超时或无响应");
            }
            return response;
        }

        void Write(string command)
        {
            var response = Query(command);
            var error = Csv.ParseLine(Query("SYST:ERR?"));
            if (error.Count == 0 || error[0] != "0")
            {
                throw new RuntimeError($"{command} 被拒绝：{string.Join(",", error)}");
            }
            var fields = Csv.ParseLine(response);
            if (fields.Count == 0 || fields[0] != "OK")
            {
                throw new RuntimeError($"{command} 未确认执行：{response}");
            }
        }

        void Settled()
        {
            var deadline = monotonic() + 5.0;
            while (true)
            {
                var fields = Csv.ParseLine(Query("SYST:TDMA:PERIOD?")).Select(Csv.ParseInt).ToArray();
                if (fields.Length != 5)
                {
                    throw new RuntimeError("周期状态字段不匹配");
                }
                // Completion is absence of pending/applying work.
                if (fields[2] == 0 && fields[4] == 0)
                {
                    return;
                }
                if (monotonic() >= deadline)
                {
                    throw new RuntimeError("等待预算实际生效超时；请重新读取设备");
                }
                sleep(0.05);
            }
        }

        Table? table = null;
        Snapshot? baseline = null;
        if (action != "read")
        {
            var capabilities = Csv.ParseLine(Query(Prefix + ":CAPABILITIES?"));
            if (capabilities.Count != 2 || capabilities[0] != "1" || capabilities[1] != "1")
            {
                throw new RuntimeError("固件未确认临时应用/显式 Flash 保存能力，请更新固件");
            }
            baseline = payload.GetValueOrDefault("baseline") as Snapshot;
            table = payload.GetValueOrDefault("table") as Table;
            if (baseline is null)
            {
                throw new ValueError("请先读取当前设备预算");
            }
            if (table is null)
            {
                throw new ValueError("请先读取当前设备预算");
            }
            ValidateTable(table);
            if (action == "save" && table != baseline.Table)
            {
                throw new ValueError("编辑内容尚未临时应用；请先应用并读回，再保存到 Flash");
            }
            if (action is "apply" or "save")
            {
                stop();
            }
            Settled();
            var current = ParseSnapshot(Query(Prefix + "?"));
            if (current.ClockHz != baseline.ClockHz || current.Table != baseline.Table)
            {
                throw new RuntimeError("设备预算已变化，请重新读取后编辑");
            }
            if (action == "save")
            {
                Write(Prefix + ":SAVE");
            }
            else
            {
                Write(Prefix + ":BEGIN");
                for (var index = 0; index < table.Phases.Count; index++)
                {
                    var phase = table.Phases[index];
                    Write(Prefix + $":PHASE {index}," + $"{phase.Start},{phase.End},{phase.Wcet}");
                }
                Write(Prefix + $":CYCLE {table.Cycle}");
                var validation = Csv.ParseLine(Query(Prefix + ":VALIDATE?"));
                if (validation.Count != 4 || validation[0] != "1" ||
                    Csv.ParseInt(validation[3]) != table.Cycle)
                {
                    throw new RuntimeError("设备预算校验拒绝：" + string.Join(",", validation));
                }
                if (action == "apply")
                {
                    Write(Prefix + ":APPLY");
                    Settled();
                }
                else
                {
                    Write(Prefix + ":DISCARD");
                }
            }
        }

        Settled();
        var snapshot = ParseSnapshot(Query(Prefix + "?"));
        if ((action is "apply" or "save") &&
            (snapshot.Table != table || snapshot.ClockHz != baseline!.ClockHz))
        {
            throw new RuntimeError("实际预算与提交值不一致，请重新读取设备");
        }

        var status = Query(Prefix + ":STATUS?");
        var statusFields = Csv.ParseLine(status);
        if (statusFields.Count != 12 || statusFields[0] != "1")
        {
            throw new RuntimeError("预算保存状态格式不支持");
        }
        if (action == "save" && statusFields[5] != "1")
        {
            throw new RuntimeError("设备未确认当前预算已保存到 Flash");
        }
        return new OperationResult(action, snapshot, status, statusFields[5] == "1");
    }
}
