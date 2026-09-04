using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Asharia.Runtime;
using Asharia.Studio.Application.Viewports;

namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal enum ViewportNativeStatus : uint
{
    Success = 0,
    InvalidArgument = 1,
    Unavailable = 2,
    UnsupportedAbi = 3,
    UnsupportedCompositionInterop = 4,
    DeviceMismatch = 5,
    UnsupportedHandleType = 6,
    RenderFailed = 7,
    DeviceLost = 8,
    InternalError = 9,
    Backpressure = 10,
    FeatureUnavailable = 11,
}

internal enum ViewportNativeHandleType : uint
{
    Unknown = 0,
    VulkanOpaqueNt = 1,
}

internal enum ViewportNativeImageFormat : uint
{
    Unknown = 0,
    Rgba8Unorm = 1,
    Bgra8Unorm = 2,
}

internal enum ViewportNativeSceneRasterMode : uint
{
    Solid = 0,
    Wireframe = 1,
}

internal enum ViewportNativeFieldOfViewAxis : uint
{
    MaintainHorizontal = 0,
    MaintainVertical = 1,
}

[Flags]
internal enum ViewportNativeStreamCapabilitiesV8 : uint
{
    None = 0,
    Wireframe = 1U << 0,
}

internal enum ViewportNativePresentCompletionKind : uint
{
    NotSubmittedToConsumer = 0,
    ConsumerAccessed = 1,
}

internal enum ViewportNativeStreamLifecycle : uint
{
    Open = 0,
    Closing = 1,
    Closed = 2,
    Faulted = 3,
}

[Flags]
internal enum ViewportNativePresentRequestV8Flags : uint
{
    None = 0,
    HasLogicalExtent = 1U << 0,
    FlashSentinelCorners = 1U << 1,
    CaptureSceneMeshEvidence = 1U << 2,
    HasSelectionOutline = 1U << 3,
    HasTranslateGizmo = 1U << 4,
}

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeAbiHeader(uint AbiVersion, uint StructSize)
{
    public const uint CurrentAbiVersion = 1;

    public static ViewportNativeAbiHeader Current<T>() where T : struct =>
        new(CurrentAbiVersion, checked((uint)Marshal.SizeOf<T>()));
}

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeId(ulong Low, ulong High)
{
    public static ViewportNativeId FromGuid(Guid value)
    {
        Span<byte> bytes = stackalloc byte[16];
        if (!value.TryWriteBytes(bytes))
        {
            throw new InvalidOperationException("Could not encode a viewport UUID.");
        }
        return new ViewportNativeId(
            MemoryMarshal.Read<ulong>(bytes),
            MemoryMarshal.Read<ulong>(bytes[8..]));
    }

    public Guid ToGuid()
    {
        Span<byte> bytes = stackalloc byte[16];
        var low = Low;
        var high = High;
        MemoryMarshal.Write(bytes, in low);
        MemoryMarshal.Write(bytes[8..], in high);
        return new Guid(bytes);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeCompatibilityRequest(
    ViewportNativeAbiHeader Header,
    uint ImageHandleType,
    uint SemaphoreHandleType,
    ulong DeviceLuidLowPart,
    int DeviceLuidHighPart,
    uint HasDeviceLuid,
    ulong DeviceUuidLow,
    ulong DeviceUuidHigh,
    uint HasDeviceUuid);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeCompatibilityResult(
    ViewportNativeAbiHeader Header,
    uint Status,
    uint ProducedImageHandleType,
    uint ProducedSemaphoreHandleType,
    uint NativeDeviceVendorId,
    uint NativeDeviceId,
    ulong NativeDeviceUuidLow,
    ulong NativeDeviceUuidHigh,
    nint MessageUtf8,
    ulong MessageByteLength);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeCamera(
    Float3 Position,
    Float3 Target,
    Float3 Up,
    float FieldOfViewRadians,
    uint FieldOfViewAxis,
    float NearPlane,
    float FarPlane)
{
    public static ViewportNativeCamera FromSnapshot(ViewportCameraSnapshot camera)
    {
        var nativeAxis = camera.FieldOfViewAxis switch
        {
            ViewportFieldOfViewAxis.MaintainHorizontal =>
                ViewportNativeFieldOfViewAxis.MaintainHorizontal,
            ViewportFieldOfViewAxis.MaintainVertical =>
                ViewportNativeFieldOfViewAxis.MaintainVertical,
            _ => throw new ArgumentOutOfRangeException(nameof(camera), camera, null),
        };
        return new ViewportNativeCamera(
            camera.Position,
            camera.Target,
            camera.Up,
            camera.FieldOfViewRadians,
            (uint)nativeAxis,
            camera.NearPlane,
            camera.FarPlane);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeDebugProxy(
    ViewportNativeId ObjectId,
    TransformValue Transform);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeTranslateGizmoV8(
    ViewportNativeId ObjectId,
    Float3 Position,
    uint HoveredAxis,
    uint ActiveAxis);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeCanonicalUuid(ulong FirstBytes, ulong LastBytes)
{
    public static ViewportNativeCanonicalUuid FromGuid(Guid value)
    {
        Span<byte> bytes = stackalloc byte[16];
        if (!value.TryWriteBytes(bytes, bigEndian: true, out _))
        {
            throw new InvalidOperationException("Could not encode a canonical viewport UUID.");
        }
        return new(
            BinaryPrimitives.ReadUInt64LittleEndian(bytes),
            BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]));
    }

    public Guid ToGuid()
    {
        Span<byte> bytes = stackalloc byte[16];
        BinaryPrimitives.WriteUInt64LittleEndian(bytes, FirstBytes);
        BinaryPrimitives.WriteUInt64LittleEndian(bytes[8..], LastBytes);
        return new Guid(bytes, bigEndian: true);
    }
}

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeAuthoredMeshSnapshotV8(
    ViewportNativeCanonicalUuid ObjectId,
    uint RuntimeEntityIndex,
    uint RuntimeEntityGeneration,
    ViewportNativeCanonicalUuid AssetId,
    ulong ExpectedMeshType,
    TransformValue Transform);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeSceneMeshReceiptV8(
    uint InputCount,
    uint ResolvedCount,
    uint RejectedCount,
    uint IndexedDrawCount,
    uint RasterMode,
    uint RepresentativeSourceEntityIndex,
    uint RepresentativeSourceEntityGeneration,
    uint EvidenceAvailable,
    ViewportNativeCanonicalUuid RepresentativeObjectId,
    ViewportNativeCanonicalUuid RepresentativeAssetId,
    ulong MeshResourceKey,
    ulong MaterialResourceKey,
    ulong ProductHash,
    ulong SceneRevision);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeStreamHandleV8(
    ViewportNativeAbiHeader Header,
    uint Status,
    uint Capabilities,
    ulong StreamId);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativePresentRequestV8(
    ViewportNativeAbiHeader Header,
    ViewportNativeId SessionId,
    ViewportNativeId TargetId,
    ulong TargetRevision,
    ulong RequestSequence,
    nint DebugProxies,
    uint DebugProxyCount,
    uint Kind,
    uint TargetKind,
    uint WidthPixels,
    uint HeightPixels,
    uint Flags,
    ViewportNativeCamera Camera,
    uint LogicalWidthPixels,
    uint LogicalHeightPixels,
    nint AuthoredMeshes,
    uint AuthoredMeshCount,
    uint SceneRasterMode,
    ViewportNativeCanonicalUuid SelectedObjectId,
    ulong ViewStateRevision,
    ViewportNativeTranslateGizmoV8 TranslateGizmo);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeReadyFrameV8(
    ViewportNativeAbiHeader Header,
    uint Status,
    uint HasFrame,
    ulong StreamId,
    nint NativeSlot,
    nint ImageHandle,
    nint WaitSemaphoreHandle,
    nint SignalSemaphoreHandle,
    uint WidthPixels,
    uint HeightPixels,
    uint Format,
    uint Reserved,
    ulong MemorySizeBytes,
    ulong FrameIndex,
    ViewportNativeId SessionId,
    ViewportNativeId TargetId,
    ulong TargetRevision,
    ulong RequestSequence,
    uint Kind,
    uint TargetKind,
    uint LogicalWidthPixels,
    uint LogicalHeightPixels,
    ViewportNativeSceneMeshReceiptV8 SceneMeshReceipt,
    ulong ViewStateRevision);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeStreamPollV8(
    ViewportNativeAbiHeader Header,
    uint Status,
    uint Lifecycle,
    uint HasPendingLatest,
    uint HasReadyFrame,
    uint RenderExecuting,
    uint SlotCount,
    uint PresentedSlotCount,
    uint Reserved,
    ulong SubmittedRequests,
    ulong CoalescedRequests,
    ulong RenderedFrames);
