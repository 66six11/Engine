using System;
using System.Runtime.InteropServices;
using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Interop.FrameDebugger.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct FrameDebuggerNativeSnapshotBuffer
{
    public const uint ExpectedAbiVersion = 1;

    private readonly uint abiVersion_;
    private readonly uint structSize_;
    private readonly IntPtr data_;
    private readonly ulong byteLength_;
    private readonly uint format_;

    public FrameDebuggerNativeSnapshotBuffer(
        IntPtr data,
        ulong byteLength,
        NativeFrameDebuggerSnapshotFormat format)
        : this(ExpectedAbiVersion, CurrentStructSize, data, byteLength, format)
    {
    }

    public FrameDebuggerNativeSnapshotBuffer(
        uint abiVersion,
        uint structSize,
        IntPtr data,
        ulong byteLength,
        NativeFrameDebuggerSnapshotFormat format)
    {
        abiVersion_ = abiVersion;
        structSize_ = structSize;
        data_ = data;
        byteLength_ = byteLength;
        format_ = (uint)format;
    }

    public static uint CurrentStructSize => checked((uint)Marshal.SizeOf<FrameDebuggerNativeSnapshotBuffer>());

    public static FrameDebuggerNativeSnapshotBuffer CreateForCall()
    {
        return new FrameDebuggerNativeSnapshotBuffer(
            IntPtr.Zero,
            0UL,
            NativeFrameDebuggerSnapshotFormat.Unknown);
    }

    public uint AbiVersion => abiVersion_;

    public uint StructSize => structSize_;

    public bool HasSupportedHeader =>
        abiVersion_ == ExpectedAbiVersion &&
        structSize_ >= CurrentStructSize;

    public IntPtr Data => data_;

    public ulong ByteLength => byteLength_;

    public NativeFrameDebuggerSnapshotFormat Format => (NativeFrameDebuggerSnapshotFormat)format_;
}
