using System;

namespace Asharia.Studio.EngineBridge.Scene;

public sealed class SceneNativeCallException : Exception
{
    internal SceneNativeCallException(
        string operation,
        SceneNativeStatus? status,
        string message,
        Exception? innerException = null)
        : base(message, innerException)
    {
        Operation = operation;
        Status = status;
    }

    public string Operation { get; }

    public SceneNativeStatus? Status { get; }
}
