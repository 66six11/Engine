using System;
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativeCompatibilityResult
{
    private readonly ViewportNativeAbiHeader header_;
    private readonly uint status_;
    private readonly uint producedImageHandleType_;
    private readonly uint producedSemaphoreHandleType_;
    private readonly uint nativeDeviceVendorId_;
    private readonly uint nativeDeviceId_;
    private readonly ulong nativeDeviceUuidLow_;
    private readonly ulong nativeDeviceUuidHigh_;
    private readonly IntPtr messageUtf8_;
    private readonly ulong messageByteLength_;

    public ViewportNativeCompatibilityResult(
        ViewportNativeAbiHeader header,
        uint status,
        uint producedImageHandleType,
        uint producedSemaphoreHandleType,
        uint nativeDeviceVendorId,
        uint nativeDeviceId,
        ulong nativeDeviceUuidLow,
        ulong nativeDeviceUuidHigh,
        IntPtr messageUtf8,
        ulong messageByteLength)
    {
        header_ = header;
        status_ = status;
        producedImageHandleType_ = producedImageHandleType;
        producedSemaphoreHandleType_ = producedSemaphoreHandleType;
        nativeDeviceVendorId_ = nativeDeviceVendorId;
        nativeDeviceId_ = nativeDeviceId;
        nativeDeviceUuidLow_ = nativeDeviceUuidLow;
        nativeDeviceUuidHigh_ = nativeDeviceUuidHigh;
        messageUtf8_ = messageUtf8;
        messageByteLength_ = messageByteLength;
    }

    public static uint CurrentStructSize => checked((uint)Marshal.SizeOf<ViewportNativeCompatibilityResult>());

    public static ViewportNativeCompatibilityResult CreateForCall()
    {
        return new ViewportNativeCompatibilityResult(
            new ViewportNativeAbiHeader(CurrentStructSize),
            ViewportNativeStatus.Unavailable,
            ViewportNativeHandleTypes.Unknown,
            ViewportNativeHandleTypes.Unknown,
            nativeDeviceVendorId: 0,
            nativeDeviceId: 0,
            nativeDeviceUuidLow: 0UL,
            nativeDeviceUuidHigh: 0UL,
            IntPtr.Zero,
            messageByteLength: 0UL);
    }

    public ViewportNativeAbiHeader Header => header_;

    public uint Status => status_;

    public uint ProducedImageHandleType => producedImageHandleType_;

    public uint ProducedSemaphoreHandleType => producedSemaphoreHandleType_;

    public uint NativeDeviceVendorId => nativeDeviceVendorId_;

    public uint NativeDeviceId => nativeDeviceId_;

    public ulong NativeDeviceUuidLow => nativeDeviceUuidLow_;

    public ulong NativeDeviceUuidHigh => nativeDeviceUuidHigh_;

    public IntPtr MessageUtf8 => messageUtf8_;

    public ulong MessageByteLength => messageByteLength_;
}
