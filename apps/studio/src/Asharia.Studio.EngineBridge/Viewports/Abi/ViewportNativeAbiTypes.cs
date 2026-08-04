using System;
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
internal readonly record struct ViewportNativeCamera(
    Float3 Position,
    Float3 Target,
    Float3 Up,
    float VerticalFovRadians,
    float NearPlane,
    float FarPlane)
{
    public static ViewportNativeCamera FromSnapshot(ViewportCameraSnapshot camera) => new(
        camera.Position,
        camera.Target,
        camera.Up,
        camera.VerticalFovRadians,
        camera.NearPlane,
        camera.FarPlane);
}

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativeDebugProxy(
    ViewportNativeId ObjectId,
    TransformValue Transform);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativePresentRequestV4(
    ViewportNativeAbiHeader Header,
    ViewportNativeCompatibilityRequest Compatibility,
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
    uint Reserved,
    ViewportNativeCamera Camera);

[StructLayout(LayoutKind.Sequential)]
internal readonly record struct ViewportNativePresentPacket(
    ViewportNativeAbiHeader Header,
    uint Status,
    nint NativePacket,
    nint ImageHandle,
    nint WaitSemaphoreHandle,
    nint SignalSemaphoreHandle,
    uint WidthPixels,
    uint HeightPixels,
    uint Format,
    ulong MemorySizeBytes,
    ulong FrameIndex,
    nint MessageUtf8,
    ulong MessageByteLength)
{
    public static ViewportNativePresentPacket ForCall() => new(
        ViewportNativeAbiHeader.Current<ViewportNativePresentPacket>(),
        (uint)ViewportNativeStatus.Unavailable,
        0,
        0,
        0,
        0,
        0,
        0,
        (uint)ViewportNativeImageFormat.Unknown,
        0,
        0,
        0,
        0);

    internal static ViewportNativePresentPacket Success(
        nint nativePacket,
        nint imageHandle,
        nint waitSemaphoreHandle,
        nint signalSemaphoreHandle,
        uint widthPixels,
        uint heightPixels,
        ViewportNativeImageFormat format,
        ulong memorySizeBytes,
        ulong frameIndex) => new(
            ViewportNativeAbiHeader.Current<ViewportNativePresentPacket>(),
            (uint)ViewportNativeStatus.Success,
            nativePacket,
            imageHandle,
            waitSemaphoreHandle,
            signalSemaphoreHandle,
            widthPixels,
            heightPixels,
            (uint)format,
            memorySizeBytes,
            frameIndex,
            0,
            0);

    internal static ViewportNativePresentPacket Failure(
        ViewportNativeStatus status,
        nint messageUtf8,
        ulong messageByteLength) => new(
            ViewportNativeAbiHeader.Current<ViewportNativePresentPacket>(),
            (uint)status,
            0,
            0,
            0,
            0,
            0,
            0,
            (uint)ViewportNativeImageFormat.Unknown,
            0,
            0,
            messageUtf8,
            messageByteLength);
}
