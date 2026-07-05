using System;
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.FrameDebugger.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct FrameDebuggerNativeStringView
{
    private readonly IntPtr data_;
    private readonly ulong byteLength_;

    public FrameDebuggerNativeStringView(IntPtr data, ulong byteLength)
    {
        data_ = data;
        byteLength_ = byteLength;
    }

    public IntPtr Data => data_;

    public ulong ByteLength => byteLength_;
}
