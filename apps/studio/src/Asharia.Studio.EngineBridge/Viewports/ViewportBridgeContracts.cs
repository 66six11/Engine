using System;
using System.Threading;
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
    NativeUnavailable,
    UnsupportedInterop,
    DeviceMismatch,
    RenderFailed,
    InternalError,
}

public enum ViewportFrameCompletionKind
{
    Presented,
    Abandoned,
}

public sealed record ViewportFrameFailure(ViewportFrameFailureKind Kind, string Message);

public sealed record ViewportFrameAcquireResult
{
    private ViewportFrameAcquireResult(
        ViewportFrameLease? lease,
        ViewportFrameFailure? failure)
    {
        Lease = lease;
        Failure = failure;
    }

    public ViewportFrameLease? Lease { get; }

    public ViewportFrameFailure? Failure { get; }

    public bool Succeeded => Lease is not null;

    internal static ViewportFrameAcquireResult Success(ViewportFrameLease lease) =>
        new(lease, failure: null);

    internal static ViewportFrameAcquireResult Failed(ViewportFrameFailure failure) =>
        new(lease: null, failure);
}

public sealed class ViewportFrameLease : IDisposable
{
    private readonly IViewportNativeApi nativeApi_;
    private readonly ViewportNativePresentPacket packet_;
    private int completionState_;

    internal ViewportFrameLease(
        IViewportNativeApi nativeApi,
        ViewportRenderRequest request,
        ViewportNativePresentPacket packet,
        ViewportFrameFormat format)
    {
        nativeApi_ = nativeApi;
        packet_ = packet;
        SessionId = request.SessionId;
        RequestSequence = request.Sequence;
        TargetId = request.TargetId;
        TargetRevision = request.TargetRevision;
        Extent = new ViewportExtent(packet.WidthPixels, packet.HeightPixels);
        Format = format;
        MemorySizeBytes = packet.MemorySizeBytes;
        FrameIndex = packet.FrameIndex;
    }

    public ViewportSessionId SessionId { get; }

    public ulong RequestSequence { get; }

    public Guid TargetId { get; }

    public ulong TargetRevision { get; }

    public ViewportExtent Extent { get; }

    public ViewportFrameFormat Format { get; }

    public ulong MemorySizeBytes { get; }

    public ulong FrameIndex { get; }

    public ViewportFrameCompletionKind? CompletionKind
    {
        get
        {
            var state = Volatile.Read(ref completionState_);
            return state == 0 ? null : (ViewportFrameCompletionKind)(state - 1);
        }
    }

    internal ViewportFrameNativeHandles NativeHandles => new(
        packet_.ImageHandle,
        packet_.WaitSemaphoreHandle,
        packet_.SignalSemaphoreHandle);

    public bool Complete() => Complete(ViewportFrameCompletionKind.Presented);

    public bool Cancel() => Complete(ViewportFrameCompletionKind.Abandoned);

    public bool Complete(ViewportFrameCompletionKind completionKind)
    {
        if (!Enum.IsDefined(completionKind))
        {
            throw new ArgumentOutOfRangeException(nameof(completionKind), completionKind, null);
        }
        var desiredState = checked((int)completionKind + 1);
        if (Interlocked.CompareExchange(ref completionState_, desiredState, comparand: 0) != 0)
        {
            return false;
        }

        nativeApi_.ReleasePresentPacket(packet_);
        return true;
    }

    public void Dispose() => Cancel();
}

internal readonly record struct ViewportFrameNativeHandles(
    nint Image,
    nint WaitSemaphore,
    nint SignalSemaphore);
