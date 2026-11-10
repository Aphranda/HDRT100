namespace DhrT100.SequenceDebug.Transport;

/// <summary>Raised when the transport itself fails (open, write, bus error).</summary>
public sealed class TransportException : Exception
{
    public TransportException(string message) : base(message) { }
    public TransportException(string message, Exception inner) : base(message, inner) { }
}

/// <summary>
/// A byte channel to the instrument. Framing, log filtering and timeouts live in
/// <see cref="ScpiSession"/> so the CDC and USBTMC paths cannot drift apart.
/// </summary>
public interface IScpiChannel : IDisposable
{
    /// <summary>Human readable endpoint, used in logs.</summary>
    string Endpoint { get; }

    /// <summary>Bounded read slice; returns 0 when no byte arrived in the slice.</summary>
    int Read(byte[] buffer, int offset, int count);

    void Write(byte[] payload);

    void Flush();

    /// <summary>
    /// Called immediately before a command is written. CDC discards buffered
    /// bytes; USBTMC must not issue a speculative bulk-in read here.
    /// </summary>
    void PrepareForWrite();

    /// <summary>Recover after a read timeout so the next exchange is not shifted.</summary>
    void RecoverFromTimeout();
}
