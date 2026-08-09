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
    private readonly ViewportPresentationProcessQuarantineRegistry processQuarantineRegistry_;
    private TaskCompletionSource<bool>? drained_;
    private int activeOperations_;
    private int pauseCount_;
    private bool isStopping_;

    public ViewportPresentationLifetime()
        : this(ViewportPresentationProcessQuarantine.Registry)
    {
    }

    internal ViewportPresentationLifetime(
        ViewportPresentationProcessQuarantineRegistry processQuarantineRegistry)
    {
        ArgumentNullException.ThrowIfNull(processQuarantineRegistry);
        processQuarantineRegistry_ = processQuarantineRegistry;
    }

    public ViewportPresentationQuarantineDrainReceipt? LastQuarantineDrainReceipt
    {
        get;
        private set;
    }

    internal ViewportPresentationProcessQuarantineRegistry ProcessQuarantineRegistry =>
        processQuarantineRegistry_;

    internal event EventHandler? Resumed;

    internal bool IsAcceptingFrames
    {
        get
        {
            lock (gate_)
            {
                return !isStopping_ && pauseCount_ == 0;
            }
        }
    }

    internal int QuarantinedFrameCount
    {
        get
        {
            lock (gate_)
            {
                var count = 0;
                foreach (var frame in quarantinedFrames_)
                {
                    count += frame is null ? 0 : 1;
                }
                return count;
            }
        }
    }

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
        ICompositionImportedGpuSemaphore? signalSemaphore,
        string endpointId = "viewport-frame-lifetime")
    {
        ArgumentNullException.ThrowIfNull(lease);
        // An ambiguous compositor submission, failed Avalonia wrapper disposal, or failed native
        // release has no safe retry contract. Retain both sides for process exit so ownership is
        // never guessed during Vulkan teardown.
        QuarantinedFrame? quarantined = null;
        lock (gate_)
        {
            for (var index = 0; index < quarantinedFrames_.Length; index++)
            {
                if (quarantinedFrames_[index] is not null)
                {
                    continue;
                }

                quarantined = new QuarantinedFrame(
                    lease,
                    image,
                    waitSemaphore,
                    signalSemaphore);
                quarantinedFrames_[index] = quarantined;
                break;
            }
        }

        if (quarantined is not null)
        {
            _ = processQuarantineRegistry_.TransferFrame(
                endpointId,
                quarantined,
                "A compositor frame submission or resource release had ambiguous ownership.");
            return;
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

    public async ValueTask<ViewportPresentationQuarantineDrainReceipt>
        StopAndDrainWithQuarantineReceiptAsync()
    {
        Task drain;
        lock (gate_)
        {
            isStopping_ = true;
            drain = GetDrainTaskLocked();
        }

        await drain.ConfigureAwait(false);
        var receipt = processQuarantineRegistry_.CaptureDrainReceipt();
        LastQuarantineDrainReceipt = receipt;
        return receipt;
    }

    public async ValueTask StopAndDrainAsync() =>
        _ = await StopAndDrainWithQuarantineReceiptAsync().ConfigureAwait(false);

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
