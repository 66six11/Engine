using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using Asharia.Editor.Viewports;
using Avalonia;

namespace Editor.Features.SceneView.Interop;

internal sealed record SceneViewFrameObservation
{
    private SceneViewFrameObservation(
        ViewportId viewportId,
        Size displaySizeDip,
        ViewportExtent pixelExtent,
        bool hasScene,
        ulong sceneRevision)
    {
        ViewportId = viewportId;
        DisplaySizeDip = displaySizeDip;
        PixelExtent = pixelExtent;
        HasScene = hasScene;
        SceneRevision = sceneRevision;
    }

    public ViewportId ViewportId { get; }

    public Size DisplaySizeDip { get; }

    public ViewportExtent PixelExtent { get; }

    public bool HasScene { get; }

    public ulong SceneRevision { get; }

    public static SceneViewFrameObservation? TryCreate(
        ViewportId viewportId,
        Size displaySizeDip,
        double renderScale,
        bool hasScene,
        ulong sceneRevision)
    {
        if (viewportId.IsDefault ||
            displaySizeDip.Width <= 0 ||
            displaySizeDip.Height <= 0 ||
            !double.IsFinite(displaySizeDip.Width) ||
            !double.IsFinite(displaySizeDip.Height) ||
            renderScale <= 0 ||
            !double.IsFinite(renderScale))
        {
            return null;
        }

        var widthPixels = checked((int)Math.Ceiling(displaySizeDip.Width * renderScale));
        var heightPixels = checked((int)Math.Ceiling(displaySizeDip.Height * renderScale));
        if (widthPixels <= 0 || heightPixels <= 0)
        {
            return null;
        }

        return new SceneViewFrameObservation(
            viewportId,
            displaySizeDip,
            new ViewportExtent(widthPixels, heightPixels, renderScale),
            hasScene,
            sceneRevision);
    }
}

internal sealed record SceneViewFrameRequest(
    ViewportId ViewportId,
    Size DisplaySizeDip,
    ViewportExtent PixelExtent,
    bool HasScene,
    ulong SceneRevision,
    ulong SessionEpoch,
    ulong SurfaceGeneration,
    ulong FrameSequence);

internal enum SceneViewPresentationWorkKind
{
    CreateSlot,
    RenderSlot,
}

internal readonly record struct SceneViewPresentationWork(
    SceneViewPresentationWorkKind Kind,
    int SlotId,
    SceneViewFrameRequest Request,
    SceneViewNativeStartAdmission NativeStartAdmission);

internal sealed class SceneViewNativeStartAdmission : IDisposable
{
    private const int Pending = 0;
    private const int Started = 1;
    private const int Canceled = 2;
    private const int Disposed = 3;

    private readonly CancellationTokenSource cancellation_ = new();
    private int state_;

    public CancellationToken CancellationToken => cancellation_.Token;

    public bool WasCanceled => Volatile.Read(ref state_) == Canceled;

    public bool IsDisposed => Volatile.Read(ref state_) == Disposed;

    public bool TryBegin()
    {
        return Interlocked.CompareExchange(
                   ref state_,
                   Started,
                   Pending) == Pending;
    }

    public void Cancel()
    {
        if (Interlocked.CompareExchange(
                ref state_,
                Canceled,
                Pending) == Pending)
        {
            cancellation_.Cancel();
        }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref state_, Disposed) != Disposed)
        {
            cancellation_.Dispose();
        }
    }
}

internal sealed class SceneViewPresentationState
{
    internal const int MaximumConcurrentFrames = 2;
    internal const int MaximumActiveSlots = 2;
    internal const int MaximumSlotsPerGeneration = 2;

    private readonly Dictionary<int, SlotState> activeSlots_ = [];
    private readonly Queue<int> pendingRetirements_ = [];
    private bool isAccepting_;
    private int nextSlotId_;
    private ulong sessionEpoch_;
    private ulong surfaceGeneration_;
    private ulong frameSequence_;
    private FrameSignature? currentSignature_;
    private SceneViewFrameRequest? currentRequest_;
    private SceneViewFrameRequest? latestPendingRequest_;
    private bool isWarmupPending_;

    public int ActiveSlotCount => activeSlots_.Count;

    public int PendingRetirementCount => pendingRetirements_.Count;

    public SceneViewFrameRequest? LatestPendingRequest => latestPendingRequest_;

    public void Attach()
    {
        sessionEpoch_++;
        isAccepting_ = true;
        currentSignature_ = null;
        currentRequest_ = null;
        latestPendingRequest_ = null;
        isWarmupPending_ = false;
    }

    public IReadOnlyList<int> Detach()
    {
        sessionEpoch_++;
        isAccepting_ = false;
        return ResetCurrentRequest();
    }

    public IReadOnlyList<int> Reset()
    {
        if (!isAccepting_)
        {
            return [];
        }

        sessionEpoch_++;
        return ResetCurrentRequest();
    }

    private IReadOnlyList<int> ResetCurrentRequest()
    {
        currentSignature_ = null;
        currentRequest_ = null;
        latestPendingRequest_ = null;
        isWarmupPending_ = false;

        foreach (var slot in activeSlots_.Values)
        {
            slot.CancelNativeStart();
        }

        foreach (var slot in activeSlots_.Values
                     .Where(static slot =>
                         slot.Lifecycle == SlotLifecycle.Available)
                     .ToArray())
        {
            MoveToRetirement(slot);
        }

        return CollectRetirements();
    }

    public SceneViewFrameRequest Observe(SceneViewFrameObservation observation)
    {
        ArgumentNullException.ThrowIfNull(observation);
        if (!isAccepting_)
        {
            throw new InvalidOperationException(
                "The Scene View presentation is not attached.");
        }

        if (currentRequest_ is { } currentRequest &&
            IsSameObservation(currentRequest, observation) &&
            (latestPendingRequest_ is not null || ActiveFrameCount() > 0))
        {
            return currentRequest;
        }

        var signature =
            new FrameSignature(observation.ViewportId, observation.PixelExtent);
        if (currentSignature_ != signature)
        {
            currentSignature_ = signature;
            surfaceGeneration_++;
        }

        var request =
            new SceneViewFrameRequest(
                observation.ViewportId,
                observation.DisplaySizeDip,
                observation.PixelExtent,
                observation.HasScene,
                observation.SceneRevision,
                sessionEpoch_,
                surfaceGeneration_,
                ++frameSequence_);
        currentRequest_ = request;
        latestPendingRequest_ = request;
        isWarmupPending_ = false;

        foreach (var slot in activeSlots_.Values)
        {
            if (slot.ActiveRequest is { } activeRequest &&
                !IsSameRequest(activeRequest, request))
            {
                slot.CancelNativeStart();
            }
        }

        foreach (var slot in activeSlots_.Values
                     .Where(slot =>
                         slot.SurfaceGeneration != request.SurfaceGeneration &&
                         slot.Lifecycle == SlotLifecycle.Available)
                     .ToArray())
        {
            MoveToRetirement(slot);
        }

        return request;
    }

    private static bool IsSameObservation(
        SceneViewFrameRequest request,
        SceneViewFrameObservation observation)
    {
        return request.ViewportId == observation.ViewportId &&
               request.DisplaySizeDip == observation.DisplaySizeDip &&
               request.PixelExtent == observation.PixelExtent &&
               request.HasScene == observation.HasScene &&
               request.SceneRevision == observation.SceneRevision;
    }

    private static bool IsSameRequest(
        SceneViewFrameRequest first,
        SceneViewFrameRequest second)
    {
        return first.SessionEpoch == second.SessionEpoch &&
               first.FrameSequence == second.FrameSequence;
    }

    public bool TryBeginWork(
        bool allowSlotCreation,
        out SceneViewPresentationWork work)
    {
        work = default;
        if (!isAccepting_ ||
            latestPendingRequest_ is not { } request ||
            ActiveFrameCount() >= MaximumConcurrentFrames)
        {
            return false;
        }

        var reusableSlot =
            activeSlots_.Values.FirstOrDefault(
                slot =>
                    slot.SurfaceGeneration == request.SurfaceGeneration &&
                    slot.Lifecycle == SlotLifecycle.Available);
        var generationSlotCount =
            activeSlots_.Values.Count(
                slot =>
                    slot.SurfaceGeneration == request.SurfaceGeneration);

        if (allowSlotCreation &&
            activeSlots_.Count < MaximumActiveSlots &&
            generationSlotCount < MaximumSlotsPerGeneration)
        {
            var slotId = nextSlotId_++;
            var createAdmission = new SceneViewNativeStartAdmission();
            activeSlots_.Add(
                slotId,
                new SlotState(
                    slotId,
                    request.SurfaceGeneration,
                    SlotLifecycle.Creating,
                    request,
                    createAdmission));
            latestPendingRequest_ = null;
            isWarmupPending_ = false;
            work =
                new SceneViewPresentationWork(
                    SceneViewPresentationWorkKind.CreateSlot,
                    slotId,
                    request,
                    createAdmission);
            return true;
        }

        if (isWarmupPending_ || reusableSlot is null)
        {
            return false;
        }

        var reuseAdmission =
            reusableSlot.BeginFrame(request);
        latestPendingRequest_ = null;
        isWarmupPending_ = false;
        work =
            new SceneViewPresentationWork(
                SceneViewPresentationWorkKind.RenderSlot,
                reusableSlot.SlotId,
                request,
                reuseAdmission);
        return true;
    }

    public void CompleteSlotCreation(int slotId)
    {
        var slot = GetSlot(slotId);
        if (slot.Lifecycle != SlotLifecycle.Creating)
        {
            throw new InvalidOperationException(
                "Only a creating Scene View slot can complete creation.");
        }

        slot.Lifecycle = SlotLifecycle.InFlight;
    }

    public void AbandonSlotCreation(int slotId)
    {
        var slot = GetSlot(slotId);
        if (slot.Lifecycle != SlotLifecycle.Creating)
        {
            throw new InvalidOperationException(
                "Only a creating Scene View slot can be abandoned.");
        }

        slot.CompleteActiveWork();
        activeSlots_.Remove(slotId);
    }

    public void CompleteFrame(
        int slotId,
        bool canReuse,
        bool warmCurrentGeneration)
    {
        var slot = GetSlot(slotId);
        if (slot.Lifecycle is not (SlotLifecycle.InFlight or SlotLifecycle.Creating))
        {
            throw new InvalidOperationException(
                "Only an active Scene View slot can complete a frame.");
        }

        var completedRequest = slot.ActiveRequest;
        slot.CompleteActiveWork();
        var remainsActive =
            canReuse &&
            isAccepting_ &&
            currentRequest_?.SurfaceGeneration == slot.SurfaceGeneration;
        if (!remainsActive)
        {
            MoveToRetirement(slot);
            return;
        }

        slot.Lifecycle = SlotLifecycle.Available;
        if (warmCurrentGeneration &&
            completedRequest is not null &&
            IsCurrent(completedRequest) &&
            latestPendingRequest_ is null &&
            activeSlots_.Values.Count(
                candidate =>
                    candidate.SurfaceGeneration == slot.SurfaceGeneration) <
            MaximumSlotsPerGeneration)
        {
            latestPendingRequest_ = completedRequest;
            isWarmupPending_ = true;
        }
    }

    public void AbortWork(int slotId)
    {
        if (!activeSlots_.TryGetValue(slotId, out var slot))
        {
            return;
        }

        if (slot.Lifecycle == SlotLifecycle.Creating)
        {
            slot.CompleteActiveWork();
            activeSlots_.Remove(slotId);
        }
        else if (slot.Lifecycle == SlotLifecycle.InFlight)
        {
            slot.CompleteActiveWork();
            MoveToRetirement(slot);
        }
    }

    public void RequeueIfCurrent(SceneViewFrameRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (IsCurrent(request) && latestPendingRequest_ is null)
        {
            latestPendingRequest_ = request;
            isWarmupPending_ = false;
        }
    }

    public bool IsCurrent(SceneViewFrameRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return isAccepting_ &&
               currentRequest_ is { } current &&
               current.SessionEpoch == request.SessionEpoch &&
               current.ViewportId == request.ViewportId &&
               current.SurfaceGeneration == request.SurfaceGeneration &&
               current.FrameSequence == request.FrameSequence;
    }

    public IReadOnlyList<int> CollectRetirements()
    {
        var retirements = pendingRetirements_.ToArray();
        pendingRetirements_.Clear();
        return retirements;
    }

    private void MoveToRetirement(SlotState slot)
    {
        if (!activeSlots_.Remove(slot.SlotId))
        {
            throw new InvalidOperationException(
                "The Scene View slot is not part of the active present chain.");
        }

        pendingRetirements_.Enqueue(slot.SlotId);
    }

    private int ActiveFrameCount()
    {
        return activeSlots_.Values.Count(
            static slot =>
                slot.Lifecycle is SlotLifecycle.Creating or SlotLifecycle.InFlight);
    }

    private SlotState GetSlot(int slotId)
    {
        return activeSlots_.TryGetValue(slotId, out var slot)
            ? slot
            : throw new ArgumentOutOfRangeException(
                nameof(slotId),
                slotId,
                "Scene View presentation slot does not exist.");
    }

    private sealed record FrameSignature(
        ViewportId ViewportId,
        ViewportExtent PixelExtent);

    private enum SlotLifecycle
    {
        Creating,
        InFlight,
        Available,
    }

    private sealed class SlotState
    {
        public SlotState(
            int slotId,
            ulong surfaceGeneration,
            SlotLifecycle lifecycle,
            SceneViewFrameRequest? activeRequest = null,
            SceneViewNativeStartAdmission? nativeStartAdmission = null)
        {
            SlotId = slotId;
            SurfaceGeneration = surfaceGeneration;
            Lifecycle = lifecycle;
            ActiveRequest = activeRequest;
            NativeStartAdmission = nativeStartAdmission;
        }

        public int SlotId { get; }

        public ulong SurfaceGeneration { get; }

        public SlotLifecycle Lifecycle { get; set; }

        public SceneViewFrameRequest? ActiveRequest { get; private set; }

        private SceneViewNativeStartAdmission? NativeStartAdmission { get; set; }

        public SceneViewNativeStartAdmission BeginFrame(
            SceneViewFrameRequest request)
        {
            if (Lifecycle != SlotLifecycle.Available ||
                ActiveRequest is not null ||
                NativeStartAdmission is not null)
            {
                throw new InvalidOperationException(
                    "Only an idle Scene View slot can begin a frame.");
            }

            var admission = new SceneViewNativeStartAdmission();
            Lifecycle = SlotLifecycle.InFlight;
            ActiveRequest = request;
            NativeStartAdmission = admission;
            return admission;
        }

        public void CancelNativeStart()
        {
            NativeStartAdmission?.Cancel();
        }

        public void CompleteActiveWork()
        {
            var admission = NativeStartAdmission;
            NativeStartAdmission = null;
            ActiveRequest = null;
            admission?.Dispose();
        }
    }
}
