using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativeCompatibilityRequest
{
    private readonly ViewportNativeAbiHeader header_;
    private readonly uint imageHandleType_;
    private readonly uint semaphoreHandleType_;
    private readonly ulong deviceLuidLowPart_;
    private readonly int deviceLuidHighPart_;
    private readonly uint hasDeviceLuid_;
    private readonly ulong deviceUuidLow_;
    private readonly ulong deviceUuidHigh_;
    private readonly uint hasDeviceUuid_;

    public ViewportNativeCompatibilityRequest(
        uint imageHandleType,
        uint semaphoreHandleType,
        ulong deviceLuidLowPart,
        int deviceLuidHighPart,
        uint hasDeviceLuid,
        ulong deviceUuidLow,
        ulong deviceUuidHigh,
        uint hasDeviceUuid)
    {
        header_ = new ViewportNativeAbiHeader(CurrentStructSize);
        imageHandleType_ = imageHandleType;
        semaphoreHandleType_ = semaphoreHandleType;
        deviceLuidLowPart_ = deviceLuidLowPart;
        deviceLuidHighPart_ = deviceLuidHighPart;
        hasDeviceLuid_ = hasDeviceLuid;
        deviceUuidLow_ = deviceUuidLow;
        deviceUuidHigh_ = deviceUuidHigh;
        hasDeviceUuid_ = hasDeviceUuid;
    }

    public static uint CurrentStructSize => checked((uint)Marshal.SizeOf<ViewportNativeCompatibilityRequest>());

    public ViewportNativeAbiHeader Header => header_;

    public uint ImageHandleType => imageHandleType_;

    public uint SemaphoreHandleType => semaphoreHandleType_;

    public ulong DeviceLuidLowPart => deviceLuidLowPart_;

    public int DeviceLuidHighPart => deviceLuidHighPart_;

    public uint HasDeviceLuid => hasDeviceLuid_;

    public ulong DeviceUuidLow => deviceUuidLow_;

    public ulong DeviceUuidHigh => deviceUuidHigh_;

    public uint HasDeviceUuid => hasDeviceUuid_;
}
