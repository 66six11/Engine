using System;
using System.Diagnostics;
using System.Threading;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

public readonly record struct ViewportPresentationMetrics(
    ulong TotalPresentedFrames,
    int WindowFrameCount,
    TimeSpan WindowElapsed,
    TimeSpan P95FrameInterval,
    TimeSpan MaximumFrameInterval)
{
    public double FramesPerSecond => WindowFrameCount < 2 || WindowElapsed <= TimeSpan.Zero
        ? 0
        : (WindowFrameCount - 1) / WindowElapsed.TotalSeconds;

    public bool MeetsMinimumFramesPerSecond(double minimumFramesPerSecond)
    {
        if (!double.IsFinite(minimumFramesPerSecond) || minimumFramesPerSecond <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(minimumFramesPerSecond));
        }
        return FramesPerSecond >= minimumFramesPerSecond;
    }
}

public readonly record struct ViewportPresentationGeometryMetrics(
    ulong ExactExtentPresentedFrames,
    ulong RejectedNonExactCandidates,
    ViewportRenderSize LastPresentedSize,
    ViewportExtent LastPanelExtent,
    ulong CurrentGeometryGeneration,
    ulong SurfaceGeometryGeneration,
    bool CurrentSurfaceIsExact)
{
    public bool LastPresentationIsExact =>
        LastPresentedSize.LogicalExtent.Width != 0 &&
        LastPresentedSize.LogicalExtent == LastPresentedSize.AllocationExtent &&
        LastPresentedSize.LogicalExtent == LastPanelExtent;
}

public enum ViewportPresentationTransactionResult
{
    Published,
    Committed,
    ExtentMismatch,
    Cancelled,
    Invalidated,
    RecoverableFailure,
    Quarantined,
}

internal readonly record struct ViewportPresentationTicket(
    ulong Value,
    ViewportExtent TargetExtent,
    ulong BaseGeometryGeneration,
    ulong CandidateGeometryGeneration);

internal enum ViewportPresentationLayoutDisposition
{
    None,
    ProbeObserved,
    ArmedExact,
    ArmedMismatch,
}

internal sealed class ViewportPresentationPreparationState
{
    private enum PreparationState
    {
        None,
        Preparing,
        Prepared,
        Armed,
    }

    private ulong nextTicket_;
    private ulong layoutProbeTicket_;
    private ViewportExtent probedExtent_;
    private ViewportPresentationTicket preparation_;
    private ViewportExtent observedArmedExtent_;
    private PreparationState preparationState_;

    public bool IsLayoutProbeActive => layoutProbeTicket_ != 0;

    public bool HasPreparation => preparationState_ != PreparationState.None;

    public ulong BeginLayoutProbe()
    {
        if (layoutProbeTicket_ != 0)
        {
            throw new InvalidOperationException("A viewport layout probe is already active.");
        }
        if (preparationState_ != PreparationState.None)
        {
            throw new InvalidOperationException(
                "A viewport layout probe cannot begin while an exact resize is pending.");
        }

        layoutProbeTicket_ = checked(++nextTicket_);
        probedExtent_ = default;
        return layoutProbeTicket_;
    }

    public void ObserveLayoutProbe(ViewportExtent extent)
    {
        if (layoutProbeTicket_ == 0)
        {
            throw new InvalidOperationException("No viewport layout probe is active.");
        }
        probedExtent_ = extent;
    }

    public bool TryGetLayoutProbeExtent(ulong ticket, out ViewportExtent extent)
    {
        extent = probedExtent_;
        return ticket != 0 && ticket == layoutProbeTicket_ &&
               extent.Width != 0 && extent.Height != 0;
    }

    public void EndLayoutProbe(ulong ticket)
    {
        if (ticket == 0 || ticket != layoutProbeTicket_)
        {
            return;
        }
        layoutProbeTicket_ = 0;
        probedExtent_ = default;
    }

    public ViewportPresentationTicket BeginPreparation(
        ViewportExtent targetExtent,
        ulong baseGeometryGeneration)
    {
        if (targetExtent.Width == 0 || targetExtent.Height == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(targetExtent));
        }
        if (preparationState_ != PreparationState.None)
        {
            throw new InvalidOperationException("A viewport exact resize is already pending.");
        }
        if (layoutProbeTicket_ != 0)
        {
            throw new InvalidOperationException(
                "A viewport exact resize cannot begin while a layout probe is active.");
        }

        preparation_ = new ViewportPresentationTicket(
            checked(++nextTicket_),
            targetExtent,
            baseGeometryGeneration,
            checked(baseGeometryGeneration + 1));
        preparationState_ = PreparationState.Preparing;
        observedArmedExtent_ = default;
        return preparation_;
    }

    public bool TryMarkPrepared(ViewportPresentationTicket ticket)
    {
        if (preparationState_ != PreparationState.Preparing || preparation_ != ticket)
        {
            return false;
        }
        preparationState_ = PreparationState.Prepared;
        return true;
    }

    public bool TryArm(ViewportPresentationTicket ticket)
    {
        if (preparationState_ != PreparationState.Prepared || preparation_ != ticket)
        {
            return false;
        }
        preparationState_ = PreparationState.Armed;
        observedArmedExtent_ = default;
        return true;
    }

    public ViewportPresentationLayoutDisposition ObserveBounds(
        ViewportExtent extent,
        out ViewportPresentationTicket ticket)
    {
        ticket = default;
        if (layoutProbeTicket_ != 0)
        {
            probedExtent_ = extent;
            return ViewportPresentationLayoutDisposition.ProbeObserved;
        }
        if (preparationState_ != PreparationState.Armed)
        {
            return ViewportPresentationLayoutDisposition.None;
        }

        ticket = preparation_;
        observedArmedExtent_ = extent;
        return extent == ticket.TargetExtent
            ? ViewportPresentationLayoutDisposition.ArmedExact
            : ViewportPresentationLayoutDisposition.ArmedMismatch;
    }

    public bool IsArmedExtentExact(ViewportPresentationTicket ticket) =>
        preparationState_ == PreparationState.Armed &&
        preparation_ == ticket &&
        observedArmedExtent_ == ticket.TargetExtent;

    public bool TryCompleteArmed(ViewportPresentationTicket ticket)
    {
        if (!IsArmedExtentExact(ticket))
        {
            return false;
        }
        preparation_ = default;
        observedArmedExtent_ = default;
        preparationState_ = PreparationState.None;
        return true;
    }

    public bool TryCancel(ViewportPresentationTicket ticket)
    {
        if (preparationState_ == PreparationState.None || preparation_ != ticket)
        {
            return false;
        }
        preparation_ = default;
        observedArmedExtent_ = default;
        preparationState_ = PreparationState.None;
        return true;
    }

    public void Reset()
    {
        layoutProbeTicket_ = 0;
        probedExtent_ = default;
        preparation_ = default;
        observedArmedExtent_ = default;
        preparationState_ = PreparationState.None;
    }
}

internal sealed class ViewportGeometryGenerationState
{
    public ViewportExtent CurrentExtent { get; private set; }

    public ulong CurrentGeneration { get; private set; }

    public ViewportExtent SurfaceExtent { get; private set; }

    public ulong SurfaceGeneration { get; private set; }

    public bool HasExactSurface =>
        CurrentExtent.Width != 0 &&
        SurfaceExtent == CurrentExtent &&
        SurfaceGeneration == CurrentGeneration;

    public bool Synchronize(ViewportExtent extent)
    {
        if (extent.Width == 0 || extent.Height == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(extent));
        }
        if (CurrentExtent == extent)
        {
            return false;
        }
        CurrentExtent = extent;
        CurrentGeneration = checked(CurrentGeneration + 1);
        return true;
    }

    public void MarkSurfaceUpdate(ViewportExtent extent, ulong generation)
    {
        if (extent != CurrentExtent || generation != CurrentGeneration)
        {
            throw new InvalidOperationException(
                "A non-current viewport geometry cannot update the composition surface.");
        }
        SurfaceExtent = extent;
        SurfaceGeneration = generation;
    }

    public void InvalidateSurface()
    {
        SurfaceExtent = default;
        SurfaceGeneration = 0;
        CurrentGeneration = checked(CurrentGeneration + 1);
    }

    public void Invalidate()
    {
        CurrentExtent = default;
        SurfaceExtent = default;
        SurfaceGeneration = 0;
        CurrentGeneration = checked(CurrentGeneration + 1);
    }
}

internal static class ViewportRealtimeAdmissionPolicy
{
    public static bool ShouldInvalidate(
        bool isRealtime,
        bool hasDesiredStream,
        bool desiredStreamIsPromoted) =>
        isRealtime && (!hasDesiredStream || desiredStreamIsPromoted);
}

internal sealed class ViewportPresentationCadenceTracker
{
    private const int TimestampCapacity = 512;
    private readonly object gate_ = new();
    private readonly long[] timestamps_ = new long[TimestampCapacity];
    private ulong totalPresentedFrames_;
    private int timestampCount_;
    private int nextTimestampIndex_;

    public void Reset()
    {
        lock (gate_)
        {
            totalPresentedFrames_ = 0;
            timestampCount_ = 0;
            nextTimestampIndex_ = 0;
        }
    }

    public void Record(long timestamp)
    {
        if (timestamp < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(timestamp));
        }
        lock (gate_)
        {
            timestamps_[nextTimestampIndex_] = timestamp;
            nextTimestampIndex_ = (nextTimestampIndex_ + 1) % TimestampCapacity;
            timestampCount_ = Math.Min(timestampCount_ + 1, TimestampCapacity);
            totalPresentedFrames_++;
        }
    }

    public ViewportPresentationMetrics Capture()
    {
        lock (gate_)
        {
            if (timestampCount_ < 2)
            {
                return new ViewportPresentationMetrics(
                    totalPresentedFrames_,
                    timestampCount_,
                    TimeSpan.Zero,
                    TimeSpan.Zero,
                    TimeSpan.Zero);
            }

            var oldestIndex = timestampCount_ == TimestampCapacity
                ? nextTimestampIndex_
                : 0;
            var newestIndex = (nextTimestampIndex_ + TimestampCapacity - 1) % TimestampCapacity;
            var intervals = new long[timestampCount_ - 1];
            for (var index = 0; index < intervals.Length; index++)
            {
                var previousIndex = (oldestIndex + index) % TimestampCapacity;
                var currentIndex = (oldestIndex + index + 1) % TimestampCapacity;
                intervals[index] = Math.Max(
                    0,
                    timestamps_[currentIndex] - timestamps_[previousIndex]);
            }
            Array.Sort(intervals);
            var p95Index = Math.Clamp(
                checked((int)Math.Ceiling(intervals.Length * 0.95)) - 1,
                0,
                intervals.Length - 1);
            return new ViewportPresentationMetrics(
                totalPresentedFrames_,
                timestampCount_,
                Stopwatch.GetElapsedTime(
                    timestamps_[oldestIndex],
                    timestamps_[newestIndex]),
                Stopwatch.GetElapsedTime(0, intervals[p95Index]),
                Stopwatch.GetElapsedTime(0, intervals[^1]));
        }
    }
}

internal enum CompositionConsumerAccessState
{
    NotSubmittedToConsumer,
    SubmissionStarted,
    ConsumerAccessed,
}

internal enum CompositionUpdateCompletion
{
    NotSubmittedToConsumer,
    ConsumerAccessed,
    VisibleSurfacePublished,
}

internal static class CompositionUpdatePresentationPolicy
{
    public static bool CanMarkPresented(
        CompositionUpdateCompletion completion,
        bool finalCanPresent) =>
        completion == CompositionUpdateCompletion.VisibleSurfacePublished && finalCanPresent;
}

internal sealed class CompositionConsumerAccessTracker
{
    private int state_;

    public CompositionConsumerAccessState State =>
        (CompositionConsumerAccessState)Volatile.Read(ref state_);

    public void MarkSubmissionStarted()
    {
        if (Interlocked.CompareExchange(
                ref state_,
                (int)CompositionConsumerAccessState.SubmissionStarted,
                (int)CompositionConsumerAccessState.NotSubmittedToConsumer) !=
            (int)CompositionConsumerAccessState.NotSubmittedToConsumer)
        {
            throw new InvalidOperationException(
                "Viewport frame was submitted to the compositor more than once.");
        }
    }

    public void MarkConsumerAccessed()
    {
        if (Interlocked.CompareExchange(
                ref state_,
                (int)CompositionConsumerAccessState.ConsumerAccessed,
                (int)CompositionConsumerAccessState.SubmissionStarted) !=
            (int)CompositionConsumerAccessState.SubmissionStarted)
        {
            throw new InvalidOperationException(
                "Viewport compositor access completed without a submission.");
        }
    }
}

internal readonly record struct ViewportPresentationFrame(
    ViewportSessionId SessionId,
    ViewportTargetKind TargetKind,
    Guid TargetId,
    ulong TargetRevision,
    ulong Sequence)
{
    public static ViewportPresentationFrame FromRequest(ViewportRenderRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return new ViewportPresentationFrame(
            request.SessionId,
            request.TargetKind,
            request.TargetId,
            request.TargetRevision,
            request.Sequence);
    }
}

internal sealed class ViewportFramePresentationState
{
    private readonly object gate_ = new();
    private ulong presentationEpoch_;
    private ulong lastPresentedSequence_;

    public ulong LastPresentedSequence
    {
        get
        {
            lock (gate_)
            {
                return lastPresentedSequence_;
            }
        }
    }

    public void Reset(ulong presentationEpoch)
    {
        lock (gate_)
        {
            presentationEpoch_ = presentationEpoch;
            lastPresentedSequence_ = 0;
        }
    }

    public bool CanPresent(
        ulong presentationEpoch,
        ViewportPresentationFrame frame,
        ViewportSessionSnapshot current)
    {
        ArgumentNullException.ThrowIfNull(current);
        lock (gate_)
        {
            return CanPresentLocked(presentationEpoch, frame, current);
        }
    }

    public bool TryMarkPresented(
        ulong presentationEpoch,
        ViewportPresentationFrame frame,
        ViewportSessionSnapshot current)
    {
        ArgumentNullException.ThrowIfNull(current);
        lock (gate_)
        {
            if (!CanPresentLocked(presentationEpoch, frame, current))
            {
                return false;
            }

            lastPresentedSequence_ = frame.Sequence;
            return true;
        }
    }

    private bool CanPresentLocked(
        ulong presentationEpoch,
        ViewportPresentationFrame frame,
        ViewportSessionSnapshot current) =>
        presentationEpoch_ == presentationEpoch &&
        frame.SessionId == current.SessionId &&
        frame.TargetKind == current.TargetKind &&
        frame.TargetId == current.TargetId &&
        frame.TargetRevision == current.TargetRevision &&
        frame.Sequence >= current.MinimumPresentableSequence &&
        frame.Sequence > lastPresentedSequence_ &&
        !current.IsClosed;
}

internal static class ViewportResizePresentationPolicy
{
    public static bool CanPresentCompletedFrame(
        ViewportRenderSize frameSize,
        ViewportRenderSize currentSize)
    {
        // Studio does not treat allocation padding plus clipping as size synchronization. A frame
        // may cross the compositor boundary only when its render target and the panel's current
        // physical-pixel extent are identical.
        return IsExact(frameSize) && IsExact(currentSize) && frameSize == currentSize;
    }

    private static bool IsExact(ViewportRenderSize size) =>
        size.LogicalExtent.Width != 0 && size.LogicalExtent.Height != 0 &&
        size.AllocationExtent == size.LogicalExtent;
}
