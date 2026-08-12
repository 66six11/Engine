using System;
using System.Linq;
using System.Runtime.InteropServices;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports.Abi;

namespace Asharia.Studio.EngineBridge.Viewports;

public sealed class ViewportBridge
{
    private const ViewportNativeStreamCapabilitiesV7 KnownStreamCapabilities =
        ViewportNativeStreamCapabilitiesV7.Wireframe;
    private readonly IViewportNativeApi nativeApi_;

    public ViewportBridge()
        : this(ViewportNativeLibraryApi.Instance)
    {
    }

    internal ViewportBridge(IViewportNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        nativeApi_ = nativeApi;
    }

    public ViewportStreamOpenResult OpenStream(ViewportDeviceCompatibility compatibility)
    {
        ArgumentNullException.ThrowIfNull(compatibility);
        ViewportNativeStatus status;
        ViewportNativeStreamHandleV7 nativeStream;
        try
        {
            var nativeCompatibility = CreateCompatibilityRequest(compatibility);
            status = nativeApi_.OpenStreamV7(in nativeCompatibility, out nativeStream);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            return FailedOpen(
                ViewportFrameFailureKind.NativeUnavailable,
                $"The native viewport backend is unavailable: {exception.Message}");
        }
        catch (Exception exception)
        {
            return FailedOpen(
                ViewportFrameFailureKind.InternalError,
                $"The native viewport stream failed to open: {exception.Message}");
        }

        if (status != ViewportNativeStatus.Success ||
            nativeStream.Header.AbiVersion != ViewportNativeAbiHeader.CurrentAbiVersion ||
            nativeStream.Header.StructSize < Marshal.SizeOf<ViewportNativeStreamHandleV7>() ||
            nativeStream.Status != (uint)ViewportNativeStatus.Success ||
            (nativeStream.Capabilities & ~(uint)KnownStreamCapabilities) != 0 ||
            nativeStream.StreamId == 0)
        {
            var failureStatus = status == ViewportNativeStatus.Success &&
                Enum.IsDefined((ViewportNativeStatus)nativeStream.Status)
                    ? (ViewportNativeStatus)nativeStream.Status
                    : status;
            return FailedOpen(
                MapFailure(failureStatus),
                $"Native viewport stream open failed with {failureStatus}.");
        }

        return new ViewportStreamOpenResult(
            new ViewportRenderStream(
                this,
                nativeStream.StreamId,
                (nativeStream.Capabilities &
                 (uint)ViewportNativeStreamCapabilitiesV7.Wireframe) != 0),
            null);
    }

    internal unsafe ViewportSubmitResult SubmitLatest(
        ulong streamId,
        ViewportRenderRequest request,
        ViewportRenderDiagnosticOverlay diagnosticOverlay)
    {
        var proxies = request.DebugProxies
            .Select(proxy => new ViewportNativeDebugProxy(
                ViewportNativeId.FromGuid(proxy.ObjectId),
                proxy.Transform))
            .ToArray();
        var meshes = request.AuthoredMeshes
            .Select(mesh => new ViewportNativeAuthoredMeshSnapshotV7(
                ViewportNativeCanonicalUuid.FromGuid(mesh.ObjectId),
                mesh.RuntimeEntityId.Index,
                mesh.RuntimeEntityId.Generation,
                ViewportNativeCanonicalUuid.FromGuid(mesh.AssetId),
                mesh.ExpectedType,
                mesh.Transform))
            .ToArray();
        fixed (ViewportNativeDebugProxy* proxyPointer = proxies)
        fixed (ViewportNativeAuthoredMeshSnapshotV7* meshPointer = meshes)
        {
            var nativeFlags = ViewportNativePresentRequestV7Flags.HasLogicalExtent;
            if ((diagnosticOverlay & ViewportRenderDiagnosticOverlay.FlashSentinelCorners) != 0)
            {
                nativeFlags |= ViewportNativePresentRequestV7Flags.FlashSentinelCorners;
            }
            if ((diagnosticOverlay & ViewportRenderDiagnosticOverlay.CaptureSceneMeshEvidence) != 0)
            {
                nativeFlags |= ViewportNativePresentRequestV7Flags.CaptureSceneMeshEvidence;
            }
            var nativeRequest = new ViewportNativePresentRequestV7(
                ViewportNativeAbiHeader.Current<ViewportNativePresentRequestV7>(),
                ViewportNativeId.FromGuid(request.SessionId.Value),
                ViewportNativeId.FromGuid(request.TargetId),
                request.TargetRevision,
                request.Sequence,
                (nint)proxyPointer,
                checked((uint)proxies.Length),
                (uint)request.Kind,
                (uint)request.TargetKind,
                request.AllocationExtent.Width,
                request.AllocationExtent.Height,
                (uint)nativeFlags,
                ViewportNativeCamera.FromSnapshot(request.Camera),
                request.LogicalExtent.Width,
                request.LogicalExtent.Height,
                (nint)meshPointer,
                checked((uint)meshes.Length),
                request.SceneRasterMode switch
                {
                    ViewportSceneRasterMode.Solid => (uint)ViewportNativeSceneRasterMode.Solid,
                    ViewportSceneRasterMode.Wireframe => (uint)ViewportNativeSceneRasterMode.Wireframe,
                    _ => throw new ArgumentOutOfRangeException(nameof(request)),
                });
            try
            {
                var status = nativeApi_.SubmitLatestV7(streamId, in nativeRequest);
                return status == ViewportNativeStatus.Success
                    ? ViewportSubmitResult.Success
                    : new ViewportSubmitResult(new ViewportFrameFailure(
                        MapFailure(status),
                        $"Native viewport frame submit failed with {status}."));
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return new ViewportSubmitResult(new ViewportFrameFailure(
                    ViewportFrameFailureKind.NativeUnavailable,
                    $"The native viewport backend is unavailable: {exception.Message}"));
            }
            catch (Exception exception)
            {
                return new ViewportSubmitResult(new ViewportFrameFailure(
                    ViewportFrameFailureKind.InternalError,
                    $"Native viewport frame submit failed: {exception.Message}"));
            }
        }
    }

    internal ViewportFrameTakeResult TryTakeReady(ViewportRenderStream stream)
    {
        ViewportNativeStatus status;
        ViewportNativeReadyFrameV7 frame;
        try
        {
            status = nativeApi_.TryTakeReadyV7(stream.StreamId, out frame);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            return FailedTake(
                ViewportFrameFailureKind.NativeUnavailable,
                $"The native viewport backend is unavailable: {exception.Message}");
        }
        catch (Exception exception)
        {
            return FailedTake(
                ViewportFrameFailureKind.InternalError,
                $"Native viewport ready-frame query failed: {exception.Message}");
        }

        if (status != ViewportNativeStatus.Success)
        {
            return FailedTake(
                MapFailure(status),
                $"Native viewport ready-frame query failed with {status}.");
        }
        if (frame.Header.AbiVersion != ViewportNativeAbiHeader.CurrentAbiVersion ||
            frame.Header.StructSize < Marshal.SizeOf<ViewportNativeReadyFrameV7>() ||
            frame.Status != (uint)ViewportNativeStatus.Success || frame.Reserved != 0 ||
            frame.StreamId is not 0 && frame.StreamId != stream.StreamId || frame.HasFrame > 1)
        {
            return FailedTake(
                ViewportFrameFailureKind.InternalError,
                "Native viewport returned an invalid V7 ready-frame header.");
        }
        if (frame.HasFrame == 0)
        {
            return new ViewportFrameTakeResult(null, null);
        }

        var format = (ViewportNativeImageFormat)frame.Format switch
        {
            ViewportNativeImageFormat.Rgba8Unorm => ViewportFrameFormat.Rgba8Unorm,
            ViewportNativeImageFormat.Bgra8Unorm => ViewportFrameFormat.Bgra8Unorm,
            _ => (ViewportFrameFormat?)null,
        };
        if (frame.StreamId != stream.StreamId || frame.NativeSlot == 0 ||
            frame.ImageHandle == 0 || frame.WaitSemaphoreHandle == 0 ||
            frame.SignalSemaphoreHandle == 0 || frame.WidthPixels == 0 ||
            frame.HeightPixels == 0 || frame.MemorySizeBytes == 0 ||
            frame.FrameIndex == 0 || frame.RequestSequence == 0 ||
            frame.TargetRevision == 0 || frame.SessionId.ToGuid() == Guid.Empty ||
            frame.TargetId.ToGuid() == Guid.Empty ||
            !Enum.IsDefined((ViewportRenderKind)frame.Kind) ||
            frame.TargetKind != (uint)ViewportTargetKind.DocumentScene ||
            frame.LogicalWidthPixels == 0 || frame.LogicalHeightPixels == 0 ||
            frame.LogicalWidthPixels > frame.WidthPixels ||
            frame.LogicalHeightPixels > frame.HeightPixels || format is null ||
            !ValidSceneMeshReceipt(frame.SceneMeshReceipt, frame.TargetRevision))
        {
            if (frame.NativeSlot != 0)
            {
                nativeApi_.CompleteFrameV7(
                    stream.StreamId,
                    frame.NativeSlot,
                    ViewportNativePresentCompletionKind.NotSubmittedToConsumer);
            }
            return FailedTake(
                ViewportFrameFailureKind.InternalError,
                "Native viewport returned an invalid V7 ready frame.");
        }

        return new ViewportFrameTakeResult(
            new ViewportFrameLease(stream, frame, format.Value),
            null);
    }

    internal void CompleteFrame(
        ViewportRenderStream stream,
        nint nativeSlot,
        ViewportFrameCompletionKind completionKind) =>
        nativeApi_.CompleteFrameV7(
            stream.StreamId,
            nativeSlot,
            ToNativeCompletionKind(completionKind));

    internal void ReleaseSlotImport(ViewportRenderStream stream, nint nativeSlot) =>
        nativeApi_.ReleaseSlotImportV7(stream.StreamId, nativeSlot);

    internal void RequestClose(ViewportRenderStream stream) =>
        nativeApi_.CloseStreamV7(stream.StreamId);

    internal ViewportRenderStreamSnapshot Poll(ViewportRenderStream stream)
    {
        var status = nativeApi_.PollStreamV7(stream.StreamId, out var poll);
        if (status != ViewportNativeStatus.Success ||
            poll.Header.AbiVersion != ViewportNativeAbiHeader.CurrentAbiVersion ||
            poll.Header.StructSize < Marshal.SizeOf<ViewportNativeStreamPollV7>() ||
            poll.Status != (uint)ViewportNativeStatus.Success || poll.Reserved != 0 ||
            poll.HasPendingLatest > 1 || poll.HasReadyFrame > 1 ||
            poll.RenderExecuting > 1 ||
            !Enum.IsDefined((ViewportNativeStreamLifecycle)poll.Lifecycle))
        {
            throw new InvalidOperationException(
                $"Native viewport stream poll failed with {status}.");
        }
        return new ViewportRenderStreamSnapshot(
            (ViewportRenderStreamLifecycle)poll.Lifecycle,
            poll.HasPendingLatest != 0,
            poll.HasReadyFrame != 0,
            poll.RenderExecuting != 0,
            poll.SlotCount,
            poll.PresentedSlotCount,
            poll.SubmittedRequests,
            poll.CoalescedRequests,
            poll.RenderedFrames);
    }

    internal void DestroyClosed(ViewportRenderStream stream) =>
        nativeApi_.DestroyStreamV7(stream.StreamId);

    private static ViewportNativeCompatibilityRequest CreateCompatibilityRequest(
        ViewportDeviceCompatibility compatibility) => new(
            ViewportNativeAbiHeader.Current<ViewportNativeCompatibilityRequest>(),
            (uint)ViewportNativeHandleType.VulkanOpaqueNt,
            (uint)ViewportNativeHandleType.VulkanOpaqueNt,
            compatibility.DeviceLuidLowPart,
            compatibility.DeviceLuidHighPart,
            compatibility.HasDeviceLuid ? 1U : 0U,
            compatibility.DeviceUuidLow,
            compatibility.DeviceUuidHigh,
            compatibility.HasDeviceUuid ? 1U : 0U);

    private static ViewportNativePresentCompletionKind ToNativeCompletionKind(
        ViewportFrameCompletionKind completionKind) => completionKind switch
        {
            ViewportFrameCompletionKind.NotSubmittedToConsumer =>
                ViewportNativePresentCompletionKind.NotSubmittedToConsumer,
            ViewportFrameCompletionKind.ConsumerAccessed =>
                ViewportNativePresentCompletionKind.ConsumerAccessed,
            _ => throw new ArgumentOutOfRangeException(
                nameof(completionKind), completionKind, null),
        };

    private static bool IsNativeBindingFailure(Exception exception) =>
        exception is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException;

    private static ViewportFrameFailureKind MapFailure(ViewportNativeStatus status) => status switch
    {
        ViewportNativeStatus.InvalidArgument => ViewportFrameFailureKind.InvalidRequest,
        ViewportNativeStatus.Backpressure => ViewportFrameFailureKind.Backpressure,
        ViewportNativeStatus.Unavailable or ViewportNativeStatus.DeviceLost =>
            ViewportFrameFailureKind.NativeUnavailable,
        ViewportNativeStatus.UnsupportedAbi or
        ViewportNativeStatus.UnsupportedCompositionInterop or
        ViewportNativeStatus.UnsupportedHandleType => ViewportFrameFailureKind.UnsupportedInterop,
        ViewportNativeStatus.FeatureUnavailable => ViewportFrameFailureKind.UnsupportedFeature,
        ViewportNativeStatus.DeviceMismatch => ViewportFrameFailureKind.DeviceMismatch,
        ViewportNativeStatus.RenderFailed => ViewportFrameFailureKind.RenderFailed,
        _ => ViewportFrameFailureKind.InternalError,
    };

    private static bool ValidSceneMeshReceipt(
        ViewportNativeSceneMeshReceiptV7 receipt,
        ulong targetRevision) =>
        receipt.EvidenceAvailable <= 1 &&
        receipt.ResolvedCount <= receipt.InputCount &&
        receipt.RejectedCount <= receipt.InputCount &&
        receipt.ResolvedCount + receipt.RejectedCount == receipt.InputCount &&
        (receipt.EvidenceAvailable == 0
            ? receipt.IndexedDrawCount == 0
            : receipt.IndexedDrawCount == receipt.ResolvedCount) &&
        Enum.IsDefined((ViewportSceneRasterMode)receipt.RasterMode) &&
        receipt.SceneRevision == targetRevision &&
        (receipt.ResolvedCount != 0
            ? receipt.RepresentativeSourceEntityIndex != 0 &&
              receipt.RepresentativeSourceEntityGeneration != 0 &&
              receipt.RepresentativeObjectId.ToGuid() != Guid.Empty &&
              receipt.RepresentativeAssetId.ToGuid() != Guid.Empty &&
              receipt.MeshResourceKey != 0 && receipt.MaterialResourceKey != 0 &&
              receipt.ProductHash != 0
            : receipt.RepresentativeSourceEntityIndex == 0 &&
              receipt.RepresentativeSourceEntityGeneration == 0 &&
              receipt.RepresentativeObjectId.ToGuid() == Guid.Empty &&
              receipt.RepresentativeAssetId.ToGuid() == Guid.Empty &&
              receipt.MeshResourceKey == 0 && receipt.MaterialResourceKey == 0 &&
              receipt.ProductHash == 0);

    private static ViewportStreamOpenResult FailedOpen(
        ViewportFrameFailureKind kind,
        string message) => new(null, new ViewportFrameFailure(kind, message));

    private static ViewportFrameTakeResult FailedTake(
        ViewportFrameFailureKind kind,
        string message) => new(null, new ViewportFrameFailure(kind, message));
}
