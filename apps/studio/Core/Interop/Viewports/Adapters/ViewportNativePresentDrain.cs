using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Editor.Core.Interop.Viewports.Adapters;

internal static class ViewportNativePresentDrain
{
    private static readonly object Gate = new();
    private static readonly HashSet<Func<Task>> ShutdownParticipants = [];
    private static TaskCompletionSource idleSource_ = CreateCompletedSource();
    private static int activePresentCount_;
    private static bool shutdownRequested_;
    private static bool processExitFallbackRequested_;

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

    public static bool RequiresProcessExitFallback
    {
        get
        {
            lock (Gate)
            {
                return processExitFallbackRequested_;
            }
        }
    }

    public static void RequestShutdown()
    {
        Func<Task>[] participants;
        lock (Gate)
        {
            if (shutdownRequested_)
            {
                return;
            }

            shutdownRequested_ = true;
            participants = [.. ShutdownParticipants];
            ShutdownParticipants.Clear();
        }

        foreach (var participant in participants)
        {
            TrackShutdownParticipant(participant);
        }
    }

    public static IDisposable RegisterShutdownParticipant(Func<Task> participant)
    {
        ArgumentNullException.ThrowIfNull(participant);

        bool runImmediately;
        lock (Gate)
        {
            runImmediately = shutdownRequested_;
            if (!runImmediately)
            {
                ShutdownParticipants.Add(participant);
            }
        }

        if (runImmediately)
        {
            TrackShutdownParticipant(participant);
        }

        return new ShutdownParticipantRegistration(
            participant,
            isRegistered: !runImmediately);
    }

    public static void RequestProcessExitFallback()
    {
        lock (Gate)
        {
            shutdownRequested_ = true;
            processExitFallbackRequested_ = true;
        }
    }

    public static Task TrackAsync(Task presentTask)
    {
        ArgumentNullException.ThrowIfNull(presentTask);

        BeginPresent();
        return TrackCoreAsync(presentTask);
    }

    public static async Task<bool> WaitForIdleAsync(TimeSpan timeout)
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
            return true;
        }

        try
        {
            await idleTask.WaitAsync(timeout).ConfigureAwait(false);
            return true;
        }
        catch (TimeoutException)
        {
            return false;
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

    private static void TrackShutdownParticipant(Func<Task> participant)
    {
        Task drainTask;
        try
        {
            drainTask = participant() ?? Task.CompletedTask;
        }
        catch (Exception ex)
        {
            drainTask = Task.FromException(ex);
        }

        _ = TrackAsync(drainTask);
    }

    private static void UnregisterShutdownParticipant(Func<Task> participant)
    {
        lock (Gate)
        {
            ShutdownParticipants.Remove(participant);
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

    private sealed class ShutdownParticipantRegistration(
        Func<Task> participant,
        bool isRegistered) : IDisposable
    {
        private Func<Task>? participant_ = isRegistered ? participant : null;

        public void Dispose()
        {
            var registeredParticipant =
                Interlocked.Exchange(ref participant_, null);
            if (registeredParticipant is not null)
            {
                UnregisterShutdownParticipant(registeredParticipant);
            }
        }
    }
}
