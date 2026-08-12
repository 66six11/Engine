using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports.Abi;

namespace Asharia.Studio.EngineBridge.Viewports;

public sealed record ViewportDeviceCompatibility(
    ulong DeviceLuidLowPart,
    int DeviceLuidHighPart,
    bool HasDeviceLuid,
    ulong DeviceUuidLow,
    ulong DeviceUuidHigh,
    bool HasDeviceUuid)
{
    public static ViewportDeviceCompatibility VulkanOpaqueNt { get; } = new(
        0,
        0,
        HasDeviceLuid: false,
        0,
        0,
        HasDeviceUuid: false);
}

public enum ViewportFrameFormat
{
    Rgba8Unorm,
    Bgra8Unorm,
}

public enum ViewportFrameFailureKind
{
    InvalidRequest,
    Backpressure,
    NativeUnavailable,
    UnsupportedInterop,
    UnsupportedFeature,
    DeviceMismatch,
    RenderFailed,
    InternalError,
}

public enum ViewportFrameCompletionKind
{
    NotSubmittedToConsumer,
    ConsumerAccessed,
}

public enum ViewportRenderStreamLifecycle
{
    Open,
    Closing,
    Closed,
    Faulted,
}

public sealed record ViewportFrameFailure(ViewportFrameFailureKind Kind, string Message);

public sealed record ViewportStreamOpenResult(
    ViewportRenderStream? Stream,
    ViewportFrameFailure? Failure)
{
    public bool Succeeded => Stream is not null;
}

public sealed record ViewportSubmitResult(ViewportFrameFailure? Failure)
{
    public bool Succeeded => Failure is null;

    internal static ViewportSubmitResult Success { get; } = new((ViewportFrameFailure?)null);
}

public sealed record ViewportFrameTakeResult(
    ViewportFrameLease? Lease,
    ViewportFrameFailure? Failure)
{
    public bool Succeeded => Failure is null;

    public bool HasFrame => Lease is not null;
}

public sealed record ViewportSceneMeshReceipt(
    uint InputCount,
    uint ResolvedCount,
    uint RejectedCount,
    uint IndexedDrawCount,
    ViewportSceneRasterMode RasterMode,
    bool EvidenceAvailable,
    EntityId? RepresentativeSourceEntityId,
    Guid? RepresentativeObjectId,
    Guid? RepresentativeAssetId,
    ulong MeshResourceKey,
    ulong MaterialResourceKey,
    ulong ProductHash,
    ulong SceneRevision);

public sealed record ViewportRenderStreamSnapshot(
    ViewportRenderStreamLifecycle Lifecycle,
    bool HasPendingLatest,
    bool HasReadyFrame,
    bool RenderExecuting,
    uint SlotCount,
    uint PresentedSlotCount,
    ulong SubmittedRequests,
    ulong CoalescedRequests,
    ulong RenderedFrames);

[Flags]
internal enum ViewportRenderDiagnosticOverlay
{
    None = 0,
    FlashSentinelCorners = 1 << 0,
    CaptureSceneMeshEvidence = 1 << 1,
}

public sealed class ViewportRenderStream : IDisposable, IAsyncDisposable
{
    private readonly object gate_ = new();
    private readonly ViewportBridge bridge_;
    private bool closeRequested_;
    private bool destroyed_;

    internal ViewportRenderStream(
        ViewportBridge bridge,
        ulong streamId,
        bool supportsWireframe)
    {
        bridge_ = bridge;
        StreamId = streamId;
        SupportsWireframe = supportsWireframe;
    }

    internal ulong StreamId { get; }

    public bool SupportsWireframe { get; }

    public ViewportSubmitResult SubmitLatest(ViewportRenderRequest request)
    {
        return SubmitLatest(request, ViewportRenderDiagnosticOverlay.None);
    }

    internal ViewportSubmitResult SubmitLatest(
        ViewportRenderRequest request,
        ViewportRenderDiagnosticOverlay diagnosticOverlay)
    {
        ArgumentNullException.ThrowIfNull(request);
        if ((diagnosticOverlay &
             ~(ViewportRenderDiagnosticOverlay.FlashSentinelCorners |
               ViewportRenderDiagnosticOverlay.CaptureSceneMeshEvidence)) !=
            ViewportRenderDiagnosticOverlay.None)
        {
            throw new ArgumentOutOfRangeException(nameof(diagnosticOverlay));
        }
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(destroyed_, this);
            if (closeRequested_)
            {
                return new ViewportSubmitResult(new ViewportFrameFailure(
                    ViewportFrameFailureKind.NativeUnavailable,
                    "Viewport render stream is closing."));
            }
            if (request.SceneRasterMode == ViewportSceneRasterMode.Wireframe &&
                !SupportsWireframe)
            {
                return new ViewportSubmitResult(new ViewportFrameFailure(
                    ViewportFrameFailureKind.UnsupportedFeature,
                    "Viewport wireframe is unavailable because the native stream device did " +
                    "not enable fillModeNonSolid."));
            }

            var submitted = bridge_.SubmitLatest(StreamId, request, diagnosticOverlay);
            if (!submitted.Succeeded)
            {
                return submitted;
            }
            return ViewportSubmitResult.Success;
        }
    }

    public ViewportFrameTakeResult TryTakeReady()
    {
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(destroyed_, this);
            return bridge_.TryTakeReady(this);
        }
    }

    public ViewportRenderStreamSnapshot Poll()
    {
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(destroyed_, this);
            return bridge_.Poll(this);
        }
    }

    public void ReleaseSlotImport(nint nativeSlot)
    {
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(destroyed_, this);
            bridge_.ReleaseSlotImport(this, nativeSlot);
        }
    }

    public void RequestClose()
    {
        lock (gate_)
        {
            if (destroyed_ || closeRequested_)
            {
                return;
            }
            bridge_.RequestClose(this);
            closeRequested_ = true;
        }
    }

    public void DestroyClosed()
    {
        lock (gate_)
        {
            if (destroyed_)
            {
                return;
            }
            bridge_.DestroyClosed(this);
            destroyed_ = true;
        }
    }

    public void Dispose() => RequestClose();

    public ValueTask DisposeAsync()
    {
        RequestClose();
        return ValueTask.CompletedTask;
    }

    internal void Complete(nint nativeSlot, ViewportFrameCompletionKind completionKind) =>
        bridge_.CompleteFrame(this, nativeSlot, completionKind);

}

public sealed class ViewportFrameLease : IDisposable, IAsyncDisposable
{
    private readonly ViewportRenderStream stream_;
    private readonly nint nativeSlot_;
    private int completionState_;

    internal ViewportFrameLease(
        ViewportRenderStream stream,
        ViewportNativeReadyFrameV7 frame,
        ViewportFrameFormat format)
    {
        stream_ = stream;
        nativeSlot_ = frame.NativeSlot;
        SessionId = new ViewportSessionId(frame.SessionId.ToGuid());
        TargetId = frame.TargetId.ToGuid();
        TargetRevision = frame.TargetRevision;
        RequestSequence = frame.RequestSequence;
        Kind = (ViewportRenderKind)frame.Kind;
        TargetKind = (ViewportTargetKind)frame.TargetKind;
        LogicalExtent = new ViewportExtent(
            frame.LogicalWidthPixels,
            frame.LogicalHeightPixels);
        AllocationExtent = new ViewportExtent(frame.WidthPixels, frame.HeightPixels);
        SceneMeshReceipt = CreateSceneMeshReceipt(frame.SceneMeshReceipt);
        Format = format;
        MemorySizeBytes = frame.MemorySizeBytes;
        FrameIndex = frame.FrameIndex;
        NativeHandles = new ViewportFrameNativeHandles(
            frame.ImageHandle,
            frame.WaitSemaphoreHandle,
            frame.SignalSemaphoreHandle);
    }

    public ViewportSessionId SessionId { get; }

    public ulong RequestSequence { get; }

    public Guid TargetId { get; }

    public ulong TargetRevision { get; }

    public ViewportRenderKind Kind { get; }

    public ViewportTargetKind TargetKind { get; }

    public ViewportExtent LogicalExtent { get; }

    public ViewportExtent AllocationExtent { get; }

    public ViewportFrameFormat Format { get; }

    public ulong MemorySizeBytes { get; }

    public ulong FrameIndex { get; }

    public ViewportSceneMeshReceipt SceneMeshReceipt { get; }

    public ViewportFrameCompletionKind? CompletionKind
    {
        get
        {
            var state = Volatile.Read(ref completionState_);
            return state <= 0 ? null : (ViewportFrameCompletionKind)(state - 1);
        }
    }

    public nint SlotIdentity => nativeSlot_;

    public ViewportFrameNativeHandles NativeHandles { get; }

    public bool Release(ViewportFrameCompletionKind completionKind)
    {
        if (!Enum.IsDefined(completionKind))
        {
            throw new ArgumentOutOfRangeException(nameof(completionKind), completionKind, null);
        }
        var encoded = checked((int)completionKind + 1);
        if (Interlocked.CompareExchange(ref completionState_, encoded, 0) != 0)
        {
            return false;
        }
        stream_.Complete(nativeSlot_, completionKind);
        return true;
    }

    public ValueTask<bool> ReleaseAsync(ViewportFrameCompletionKind completionKind) =>
        ValueTask.FromResult(Release(completionKind));

    public void Dispose() => Release(ViewportFrameCompletionKind.NotSubmittedToConsumer);

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    public void Quarantine() => Interlocked.Exchange(ref completionState_, int.MinValue);

    private static ViewportSceneMeshReceipt CreateSceneMeshReceipt(
        ViewportNativeSceneMeshReceiptV7 receipt)
    {
        var hasResolved = receipt.ResolvedCount != 0;
        return new ViewportSceneMeshReceipt(
            receipt.InputCount,
            receipt.ResolvedCount,
            receipt.RejectedCount,
            receipt.IndexedDrawCount,
            (ViewportSceneRasterMode)receipt.RasterMode,
            receipt.EvidenceAvailable != 0,
            hasResolved
                ? new EntityId(
                    receipt.RepresentativeSourceEntityIndex,
                    receipt.RepresentativeSourceEntityGeneration)
                : null,
            hasResolved ? receipt.RepresentativeObjectId.ToGuid() : null,
            hasResolved ? receipt.RepresentativeAssetId.ToGuid() : null,
            receipt.MeshResourceKey,
            receipt.MaterialResourceKey,
            receipt.ProductHash,
            receipt.SceneRevision);
    }

}

public readonly record struct ViewportFrameNativeHandles(
    nint Image,
    nint WaitSemaphore,
    nint SignalSemaphore);
