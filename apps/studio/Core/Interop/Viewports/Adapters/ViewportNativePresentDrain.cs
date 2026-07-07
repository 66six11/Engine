using System;
using System.Threading;
using System.Threading.Tasks;

namespace Editor.Core.Interop.Viewports.Adapters;

internal static class ViewportNativePresentDrain
{
    private static readonly object Gate = new();
    private static TaskCompletionSource idleSource_ = CreateCompletedSource();
    private static int activePresentCount_;
    private static bool shutdownRequested_;

    public static bool CanBeginPresent
    {
        get
        {
            lock (Gate)
            {
                return !shutdownRequested_;
            }
        }
    }

    public static bool HasActivePresents
    {
        get
        {
            lock (Gate)
            {
                return activePresentCount_ > 0;
            }
        }
    }

    public static void RequestShutdown()
    {
        lock (Gate)
        {
            shutdownRequested_ = true;
        }
    }

    public static Task TrackAsync(Task presentTask)
    {
        ArgumentNullException.ThrowIfNull(presentTask);

        BeginPresent();
        return TrackCoreAsync(presentTask);
    }

    public static async Task WaitForIdleAsync(TimeSpan timeout)
    {
        if (timeout < TimeSpan.Zero && timeout != Timeout.InfiniteTimeSpan)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        Task idleTask;
        lock (Gate)
        {
            idleTask = activePresentCount_ == 0 ? Task.CompletedTask : idleSource_.Task;
        }

        if (timeout == Timeout.InfiniteTimeSpan)
        {
            await idleTask.ConfigureAwait(false);
            return;
        }

        try
        {
            await idleTask.WaitAsync(timeout).ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
        }
    }

    private static async Task TrackCoreAsync(Task presentTask)
    {
        try
        {
            await presentTask.ConfigureAwait(false);
        }
        catch
        {
            // Present errors are handled on the view path; the drain only tracks lifetime.
        }
        finally
        {
            EndPresent();
        }
    }

    private static void BeginPresent()
    {
        lock (Gate)
        {
            if (activePresentCount_ == 0)
            {
                idleSource_ = CreatePendingSource();
            }

            activePresentCount_++;
        }
    }

    private static void EndPresent()
    {
        lock (Gate)
        {
            activePresentCount_--;
            if (activePresentCount_ == 0)
            {
                idleSource_.TrySetResult();
            }
        }
    }

    private static TaskCompletionSource CreatePendingSource()
    {
        return new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private static TaskCompletionSource CreateCompletedSource()
    {
        var source = CreatePendingSource();
        source.SetResult();
        return source;
    }
}
