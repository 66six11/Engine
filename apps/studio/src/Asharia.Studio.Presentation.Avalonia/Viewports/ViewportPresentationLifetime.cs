using System;
using System.Threading.Tasks;
using Asharia.Studio.EngineBridge.Viewports;
using Avalonia.Rendering.Composition;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public sealed class ViewportPresentationLifetime : IAsyncDisposable
{
    private const int MaximumQuarantinedFrames = 4;
    private readonly object gate_ = new();
    private readonly QuarantinedFrame?[] quarantinedFrames_ =
        new QuarantinedFrame?[MaximumQuarantinedFrames];
    private TaskCompletionSource<bool>? drained_;
    private int activeOperations_;
    private int pauseCount_;
    private bool isStopping_;

    internal event EventHandler? Resumed;

    internal bool TryBeginFrame(out IDisposable admission)
    {
        lock (gate_)
        {
            if (isStopping_ || pauseCount_ != 0)
            {
                admission = null!;
                return false;
            }

            activeOperations_++;
            admission = new Admission(this);
            return true;
        }
    }

    internal IDisposable BeginCleanup()
    {
        lock (gate_)
        {
            activeOperations_++;
            return new Admission(this);
        }
    }

    internal void QuarantineFrame(
        ViewportFrameLease lease,
        ICompositionImportedGpuImage? image,
        ICompositionImportedGpuSemaphore? waitSemaphore,
        ICompositionImportedGpuSemaphore? signalSemaphore)
    {
        ArgumentNullException.ThrowIfNull(lease);
        // A failed Avalonia wrapper disposal has no safe retry contract. Retain both sides for
        // process exit so the outstanding native packet also prevents Vulkan context teardown.
        lock (gate_)
        {
            for (var index = 0; index < quarantinedFrames_.Length; index++)
            {
                if (quarantinedFrames_[index] is not null)
                {
                    continue;
                }

                quarantinedFrames_[index] = new QuarantinedFrame(
                    lease,
                    image,
                    waitSemaphore,
                    signalSemaphore);
                return;
            }
        }

        throw new InvalidOperationException(
            "Viewport presentation quarantine exceeded the native frame-lane bound.");
    }

    public async ValueTask<IAsyncDisposable> PauseAndDrainAsync()
    {
        Task drain;
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(isStopping_, this);
            pauseCount_++;
            drain = GetDrainTaskLocked();
        }

        await drain.ConfigureAwait(false);
        return new Pause(this);
    }

    public ValueTask StopAndDrainAsync()
    {
        lock (gate_)
        {
            isStopping_ = true;
            return new ValueTask(GetDrainTaskLocked());
        }
    }

    public ValueTask DisposeAsync() => StopAndDrainAsync();

    private Task GetDrainTaskLocked()
    {
        if (activeOperations_ == 0)
        {
            return Task.CompletedTask;
        }

        drained_ ??= new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        return drained_.Task;
    }

    private void CompleteOperation()
    {
        TaskCompletionSource<bool>? drained = null;
        lock (gate_)
        {
            if (--activeOperations_ == 0)
            {
                drained = drained_;
                drained_ = null;
            }
        }

        drained?.TrySetResult(true);
    }

    private void Resume()
    {
        EventHandler? resumed = null;
        lock (gate_)
        {
            if (pauseCount_ == 0)
            {
                return;
            }

            pauseCount_--;
            if (pauseCount_ == 0 && !isStopping_)
            {
                resumed = Resumed;
            }
        }

        resumed?.Invoke(this, EventArgs.Empty);
    }

    private sealed class Admission(ViewportPresentationLifetime owner) : IDisposable
    {
        private ViewportPresentationLifetime? owner_ = owner;

        public void Dispose()
        {
            var owner = owner_;
            owner_ = null;
            owner?.CompleteOperation();
        }
    }

    private sealed class Pause(ViewportPresentationLifetime owner) : IAsyncDisposable
    {
        private ViewportPresentationLifetime? owner_ = owner;

        public ValueTask DisposeAsync()
        {
            var owner = owner_;
            owner_ = null;
            owner?.Resume();
            return ValueTask.CompletedTask;
        }
    }

    private sealed record QuarantinedFrame(
        ViewportFrameLease Lease,
        ICompositionImportedGpuImage? Image,
        ICompositionImportedGpuSemaphore? WaitSemaphore,
        ICompositionImportedGpuSemaphore? SignalSemaphore);
}
