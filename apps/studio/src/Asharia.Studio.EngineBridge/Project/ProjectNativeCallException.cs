using System;

namespace Asharia.Studio.EngineBridge.Project;

public sealed class ProjectNativeCallException : Exception
{
    internal ProjectNativeCallException(
        string operation,
        ProjectNativeStatus? status,
        string message,
        Exception? innerException = null)
        : base(message, innerException)
    {
        Operation = operation;
        Status = status;
    }

    public string Operation { get; }

    public ProjectNativeStatus? Status { get; }
}
