using System;
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativePresentPacket
{
    private readonly ViewportNativeAbiHeader header_;
    private readonly uint status_;
    private readonly IntPtr nativePacket_;
    private readonly IntPtr imageHandle_;
    private readonly IntPtr waitSemaphoreHandle_;
    private readonly IntPtr signalSemaphoreHandle_;
    private readonly uint widthPixels_;
    private readonly uint heightPixels_;
    private readonly uint format_;
    private readonly ulong memorySizeBytes_;
    private readonly ulong frameIndex_;
    private readonly IntPtr messageUtf8_;
    private readonly ulong messageByteLength_;

    public ViewportNativePresentPacket(
        ViewportNativeAbiHeader header,
        uint status,
        IntPtr nativePacket,
        IntPtr imageHandle,
        IntPtr waitSemaphoreHandle,
        IntPtr signalSemaphoreHandle,
        uint widthPixels,
        uint heightPixels,
        ViewportNativeImageFormat format,
        ulong memorySizeBytes,
        ulong frameIndex,
        IntPtr messageUtf8,
        ulong messageByteLength)
    {
        header_ = header;
        status_ = status;
        nativePacket_ = nativePacket;
        imageHandle_ = imageHandle;
        waitSemaphoreHandle_ = waitSemaphoreHandle;
        signalSemaphoreHandle_ = signalSemaphoreHandle;
        widthPixels_ = widthPixels;
        heightPixels_ = heightPixels;
        format_ = (uint)format;
        memorySizeBytes_ = memorySizeBytes;
        frameIndex_ = frameIndex;
        messageUtf8_ = messageUtf8;
        messageByteLength_ = messageByteLength;
    }

    public static uint CurrentStructSize => checked((uint)Marshal.SizeOf<ViewportNativePresentPacket>());

    public static ViewportNativePresentPacket CreateForCall()
    {
        return new ViewportNativePresentPacket(
            new ViewportNativeAbiHeader(CurrentStructSize),
            ViewportNativeStatus.Unavailable,
            IntPtr.Zero,
            IntPtr.Zero,
            IntPtr.Zero,
            IntPtr.Zero,
            widthPixels: 0U,
            heightPixels: 0U,
            ViewportNativeImageFormat.Unknown,
            memorySizeBytes: 0UL,
            frameIndex: 0UL,
            IntPtr.Zero,
            messageByteLength: 0UL);
    }

    public ViewportNativeAbiHeader Header => header_;

    public uint Status => status_;

    public IntPtr NativePacket => nativePacket_;

    public IntPtr ImageHandle => imageHandle_;

    public IntPtr WaitSemaphoreHandle => waitSemaphoreHandle_;

    public IntPtr SignalSemaphoreHandle => signalSemaphoreHandle_;

    public uint WidthPixels => widthPixels_;

    public uint HeightPixels => heightPixels_;

    public ViewportNativeImageFormat Format => (ViewportNativeImageFormat)format_;

    public ulong MemorySizeBytes => memorySizeBytes_;

    public ulong FrameIndex => frameIndex_;

    public IntPtr MessageUtf8 => messageUtf8_;

    public ulong MessageByteLength => messageByteLength_;
}
