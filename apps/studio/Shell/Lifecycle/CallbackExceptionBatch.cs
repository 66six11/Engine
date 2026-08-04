using System;
using System.Collections.Generic;
using System.Runtime.ExceptionServices;

namespace Editor.Shell.Lifecycle;

internal sealed class CallbackExceptionBatch
{
    private readonly List<Exception> exceptions_ = [];

    public bool HasExceptions => exceptions_.Count > 0;

    public void Capture(Action action)
    {
        try
        {
            action();
        }
        catch (Exception exception)
        {
            Add(exception);
        }
    }

    public void Add(Exception exception)
    {
        if (exception is AggregateException aggregateException)
        {
            foreach (var innerException in aggregateException.InnerExceptions)
            {
                Add(innerException);
            }

            return;
        }

        exceptions_.Add(exception);
    }

    public void ThrowIfAny()
    {
        if (exceptions_.Count == 0)
        {
            return;
        }

        if (exceptions_.Count == 1)
        {
            ExceptionDispatchInfo.Capture(exceptions_[0]).Throw();
        }

        throw new AggregateException(exceptions_);
    }
}
