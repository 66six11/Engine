using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Threading.Tasks;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal sealed class ViewportStreamWorkFence
{
    private readonly HashSet<Task> presentations_ = new();

    public Task PumpTask { get; private set; } = Task.CompletedTask;

    public Task? RetirementTask { get; private set; }

    public bool IsRetiring { get; private set; }

    public int PresentationCount => presentations_.Count;

    public bool TryStartPump(Func<Task> startPump, out Task pump)
    {
        ArgumentNullException.ThrowIfNull(startPump);
        pump = PumpTask;
        if (IsRetiring || !pump.IsCompleted)
        {
            return false;
        }

        var completion = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        pump = completion.Task;
        PumpTask = pump;
        Task started;
        try
        {
            started = startPump() ?? throw new InvalidOperationException(
                "Viewport stream pump factory returned no task.");
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
            return true;
        }
        _ = ForwardPumpCompletionAsync(started, completion);
        return true;
    }

    public void TrackPresentation(Task presentation)
    {
        ArgumentNullException.ThrowIfNull(presentation);
        if (IsRetiring)
        {
            throw new InvalidOperationException(
                "A retiring viewport stream cannot admit a presentation.");
        }
        if (!presentations_.Add(presentation))
        {
            throw new InvalidOperationException(
                "Viewport stream presentation was tracked more than once.");
        }
    }

    public void UntrackPresentation(Task presentation)
    {
        ArgumentNullException.ThrowIfNull(presentation);
        if (!presentations_.Remove(presentation))
        {
            throw new InvalidOperationException(
                "Viewport stream presentation was not tracked.");
        }
    }

    public Task BeginRetirement(Action requestClose, Func<Task> releaseResources)
    {
        ArgumentNullException.ThrowIfNull(requestClose);
        ArgumentNullException.ThrowIfNull(releaseResources);
        if (RetirementTask is not null)
        {
            return RetirementTask;
        }

        IsRetiring = true;
        var owningPump = PumpTask;
        Exception? closeFailure = null;
        try
        {
            requestClose();
        }
        catch (Exception exception)
        {
            closeFailure = exception;
        }

        RetirementTask = RetireAsync(owningPump, closeFailure, releaseResources);
        return RetirementTask;
    }

    private static async Task ForwardPumpCompletionAsync(
        Task started,
        TaskCompletionSource<bool> completion)
    {
        try
        {
            await started;
            completion.TrySetResult(true);
        }
        catch (OperationCanceledException exception)
        {
            completion.TrySetCanceled(exception.CancellationToken);
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
    }

    private async Task RetireAsync(
        Task owningPump,
        Exception? closeFailure,
        Func<Task> releaseResources)
    {
        Exception? pumpFailure = null;
        try
        {
            await owningPump;
        }
        catch (Exception exception)
        {
            pumpFailure = exception;
        }

        try
        {
            await Task.WhenAll(presentations_.ToArray());
        }
        catch
        {
            // Every presentation is supervised by the control. The fence only waits until no
            // presentation can still access imports owned by this stream.
        }

        if (closeFailure is not null)
        {
            if (pumpFailure is not null)
            {
                throw new AggregateException(
                    "Viewport stream close and pump both failed.",
                    closeFailure,
                    pumpFailure);
            }
            ExceptionDispatchInfo.Capture(closeFailure).Throw();
        }

        Exception? releaseFailure = null;
        try
        {
            await releaseResources();
        }
        catch (Exception exception)
        {
            releaseFailure = exception;
        }

        if (pumpFailure is not null && releaseFailure is not null)
        {
            throw new AggregateException(
                "Viewport stream pump and resource release both failed.",
                pumpFailure,
                releaseFailure);
        }
        if (releaseFailure is not null)
        {
            ExceptionDispatchInfo.Capture(releaseFailure).Throw();
        }
        if (pumpFailure is not null)
        {
            ExceptionDispatchInfo.Capture(pumpFailure).Throw();
        }
    }
}
