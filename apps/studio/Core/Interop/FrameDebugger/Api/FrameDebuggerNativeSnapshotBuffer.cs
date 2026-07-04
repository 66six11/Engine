using System;
using System.Runtime.InteropServices;
using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Interop.FrameDebugger.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct FrameDebuggerNativeSnapshotBuffer
{
    private readonly IntPtr data_;
    private readonly UIntPtr byteLength_;
    private readonly NativeFrameDebuggerSnapshotFormat format_;

    public FrameDebuggerNativeSnapshotBuffer(
        IntPtr data,
        UIntPtr byteLength,
        NativeFrameDebuggerSnapshotFormat format)
    {
        data_ = data;
        byteLength_ = byteLength;
        format_ = format;
    }

    public IntPtr Data => data_;

    public UIntPtr ByteLength => byteLength_;

    public NativeFrameDebuggerSnapshotFormat Format => format_;
}
