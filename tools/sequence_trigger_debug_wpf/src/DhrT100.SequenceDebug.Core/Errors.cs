namespace DhrT100.SequenceDebug.Core;

/// <summary>输入校验失败（对应 Python 侧 <c>ValueError</c> 的语义）。</summary>
public sealed class ValueError : Exception
{
    public ValueError(string message) : base(message) { }
    public ValueError(string message, Exception inner) : base(message, inner) { }
}

/// <summary>设备交互失败或状态不符合预期（对应 Python 侧 <c>RuntimeError</c>）。</summary>
public sealed class RuntimeError : Exception
{
    public RuntimeError(string message) : base(message) { }
    public RuntimeError(string message, Exception inner) : base(message, inner) { }
}

/// <summary>等待设备状态超时（对应 Python 侧 <c>TimeoutError</c>）。</summary>
public sealed class TimeoutError : Exception
{
    public TimeoutError(string message) : base(message) { }
}
