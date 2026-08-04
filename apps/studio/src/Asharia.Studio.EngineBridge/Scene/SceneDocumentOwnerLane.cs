using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.EngineBridge.Scene;

internal sealed class SceneDocumentOwnerLane : IDisposable
{
    private readonly BlockingCollection<Action> work_ = new();
    private readonly Thread thread_;
    private readonly object stateGate_ = new();
    private bool isDisposed_;

    public SceneDocumentOwnerLane()
    {
        thread_ = new Thread(Run)
        {
            IsBackground = true,
            Name = "Asharia SceneDocument owner",
        };
        thread_.Start();
    }

    public Task<T> InvokeAsync<T>(Func<T> operation)
    {
        ArgumentNullException.ThrowIfNull(operation);
        var completion = new TaskCompletionSource<T>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        lock (stateGate_)
        {
            ObjectDisposedException.ThrowIf(isDisposed_, this);
            work_.Add(() =>
            {
                try
                {
                    completion.SetResult(operation());
                }
                catch (Exception exception)
                {
                    completion.SetException(exception);
                }
            });
        }
        return completion.Task;
    }

    public void Dispose()
    {
        lock (stateGate_)
        {
            if (isDisposed_)
            {
                return;
            }
            isDisposed_ = true;
            work_.CompleteAdding();
        }

        if (Thread.CurrentThread == thread_)
        {
            throw new InvalidOperationException("A scene document owner lane cannot join itself.");
        }
        thread_.Join();
        work_.Dispose();
    }

    private void Run()
    {
        foreach (var operation in work_.GetConsumingEnumerable())
        {
            operation();
        }
    }
}
