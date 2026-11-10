namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// Starts the mode once, then observes real input-driven progress without
/// software NEXT. Returns an operator summary; throws when the device shows a
/// fault, a counter regression or a mode mismatch.
/// </summary>
public static class LoopbackObserver
{
    public static string Observe(string mode, Func<string, string> exchange,
                                 Action<string, string> emit, int duration,
                                 Func<double>? monotonic = null,
                                 Action<double>? sleep = null,
                                 Func<bool>? cancelled = null)
    {
        monotonic ??= () => Environment.TickCount64 / 1000.0;
        sleep ??= seconds => Thread.Sleep((int)Math.Round(Math.Max(0d, seconds) * 1000d));
        cancelled ??= () => false;

        string CheckedExchange(string command)
        {
            if (cancelled())
            {
                throw new OperationCanceledException("观察已取消；未继续发送启动命令。");
            }
            return exchange(command);
        }

        string Query(string command)
        {
            var response = CheckedExchange(command);
            emit(command, response);
            if (SequenceConstants.IsTimeout(response))
            {
                throw new RuntimeError($"{command} 超时");
            }
            return response;
        }

        Query("*IDN?");
        Query("SYST:FW:BUILD?");
        var before = SequenceStatus.Parse(Query("TRIG:SEQ:NEXT?"));
        if (before.State != "IDLE")
        {
            throw new ValueError("请先停止序列，再启动回环观察");
        }

        var link = Csv.SplitUnsigned(Query("READ:SEQ:LINK?"));
        if (link.Length != 27 || (link[0] != 0) != (mode != SequenceConstants.ModeIndependent))
        {
            throw new ValueError("设备链路模式与当前页面不一致，请重新配置");
        }

        if (mode == SequenceConstants.ModeIndependent)
        {
            var source = Csv.ParseLine(Query("READ:SEQ:SOUR?"));
            var output = Csv.ParseLine(Query("READ:SEQ:OUTPUT?"));
            if (source.Count == 0 || !SequenceCommandBuilder.IsInput(source[0]) ||
                output.Count != 7 || output[2] != "PULSE" ||
                Csv.ParseInt(output[1]) == 0 || output[^1] != "1")
            {
                throw new ValueError("回环需要有效的外部输入和状态脉冲输出配置");
            }
        }
        else if (link[16] == 0)
        {
            throw new ValueError("回环 READY 必须选择物理输入");
        }

        if (mode == SequenceConstants.ModeTurntable)
        {
            var counter = Csv.SplitUnsigned(Query("READ:SEQ:COUNTER?"));
            if (counter.Length != 12 || counter[0] == 0)
            {
                throw new ValueError("转台计数角色未启用");
            }
        }
        else if (mode == SequenceConstants.ModeRj45)
        {
            var counter = Csv.SplitUnsigned(Query("READ:SEQ:COUNTER?"));
            if (counter.Length != 12 || counter[0] != 0)
            {
                throw new ValueError("设备当前为转台模式，请重新配置双槽位模式");
            }
        }

        new BatchExecutor(CheckedExchange, emit, monotonic, sleep)
            .Execute(SequenceCommandBuilder.BuildStartCommands(mode));

        var deadline = monotonic() + duration;
        var samples = new List<SequenceStatus>();
        var counterDetail = string.Empty;
        (long RunId, long Generation)? identity = null;
        while (monotonic() < deadline)
        {
            if (cancelled())
            {
                return "观察已取消；停止命令按队列执行。";
            }

            var row = SequenceStatus.Parse(Query("TRIG:SEQ:NEXT?"));
            var current = (row.RunId, row.Generation);
            if (identity is null && current == (before.RunId, before.Generation))
            {
                sleep(0.25);
                continue;
            }
            if (identity is not null && current != identity)
            {
                throw new RuntimeError("观察期间运行代次改变");
            }
            identity = current;

            if (row.State == "FAULT" || row.Error != "NONE" || row.Faults != 0 || row.BackendFault != 0)
            {
                throw new RuntimeError($"序列异常：{Describe(row)}");
            }
            if (samples.Count > 0 &&
                (row.Accepted < samples[^1].Accepted || row.Completed < samples[^1].Completed))
            {
                throw new RuntimeError("序列计数回退");
            }
            samples.Add(row);

            Query("READ:IO:STAT?");
            if (mode != SequenceConstants.ModeIndependent)
            {
                var fields = Csv.SplitUnsigned(Query("READ:SEQ:LINK?"));
                if (fields.Length != 27 || fields[2] != 0)
                {
                    throw new RuntimeError($"链路异常：{string.Join(",", fields)}");
                }
            }
            if (mode == SequenceConstants.ModeTurntable)
            {
                var counter = Csv.SplitUnsigned(Query("READ:SEQ:COUNTER?"));
                if (counter.Length != 12 || counter[7] != 0 || counter[11] != 0)
                {
                    throw new RuntimeError($"计数角色异常：{string.Join(",", counter)}");
                }
                counterDetail = $"累计脉冲 {counter[4]}，位置 {counter[5]}，当前 {counter[6]}/{counter[3]}。";
            }

            if (row.State == "IDLE")
            {
                break;
            }
            sleep(0.5);
        }

        if (samples.Count == 0)
        {
            throw new RuntimeError("启动后没有观察到新运行");
        }

        var last = samples[^1];
        var repeat = Query("READ:SEQ:REPEAT?");
        var repeatFields = Csv.SplitInts(repeat);
        if (repeatFields.Length != 3)
        {
            throw new ValueError("循环次数读回格式异常");
        }
        var finished = repeatFields[^1] == 1 && last.State == "IDLE";
        var progressed = last.Completed > 0;
        var detail = finished
            ? "有限轮次已结束"
            : progressed
                ? "已观察到自动推进"
                : "未观察到推进，请检查输入及接线";
        return $"{detail}；接纳 {last.Accepted} / 完成 {last.Completed}，" +
               $"回绕 {last.Cycles}，忙拒绝 {last.BusyRejected}，状态 {last.State}。" +
               counterDetail +
               (last.State != "IDLE" ? "观察结束，设备仍在运行。" : string.Empty);
    }

    private static string Describe(SequenceStatus row)
    {
        return $"state={row.State}, error={row.Error}, faults={row.Faults}, " +
               $"backend_fault={row.BackendFault}, accepted={row.Accepted}, completed={row.Completed}";
    }
}
