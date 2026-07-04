using System;
using System.Runtime.InteropServices;
using System.Text;
using Editor.Core.Abstractions;
using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Interop;

public sealed class FrameDebuggerNativeBridge : INativeFrameDebuggerBridge
{
    private readonly IFrameDebuggerNativeApi api_;

    public FrameDebuggerNativeBridge()
        : this(FrameDebuggerNativeLibraryApi.Instance)
    {
    }

    internal FrameDebuggerNativeBridge(IFrameDebuggerNativeApi api)
    {
        ArgumentNullException.ThrowIfNull(api);

        api_ = api;
    }

    public bool TryAcquireSnapshot(out NativeFrameDebuggerSnapshotPayload? payload)
    {
        payload = null;
        if (!FrameDebuggerNativeStatus.IsSuccess(api_.AcquireSnapshot(out var snapshot)))
        {
            return false;
        }

        try
        {
            if (!TryCopySnapshot(snapshot, out var bytes))
            {
                return false;
            }

            payload = NativeFrameDebuggerSnapshotPayload.JsonUtf8(bytes);
            return true;
        }
        finally
        {
            if (snapshot.Data != IntPtr.Zero)
            {
                api_.ReleaseSnapshot(snapshot);
            }
        }
    }

    public bool RequestCapture()
    {
        return FrameDebuggerNativeStatus.IsSuccess(api_.RequestCapture());
    }

    public bool RequestResume()
    {
        return FrameDebuggerNativeStatus.IsSuccess(api_.RequestResume());
    }

    public bool SelectExecutionEvent(string executionEventId)
    {
        if (string.IsNullOrWhiteSpace(executionEventId))
        {
            return false;
        }

        var executionEventIdUtf8 = CreateNullTerminatedUtf8(executionEventId);
        var nativeBytes = Marshal.AllocHGlobal(executionEventIdUtf8.Length);
        try
        {
            Marshal.Copy(executionEventIdUtf8, 0, nativeBytes, executionEventIdUtf8.Length);
            return FrameDebuggerNativeStatus.IsSuccess(api_.SelectExecutionEvent(nativeBytes));
        }
        finally
        {
            Marshal.FreeHGlobal(nativeBytes);
        }
    }

    private static bool TryCopySnapshot(
        FrameDebuggerNativeSnapshotBuffer snapshot,
        out byte[] bytes)
    {
        bytes = [];
        if (snapshot.Data == IntPtr.Zero ||
            snapshot.ByteLength == UIntPtr.Zero ||
            snapshot.Format != NativeFrameDebuggerSnapshotFormat.JsonUtf8)
        {
            return false;
        }

        var byteLength = snapshot.ByteLength.ToUInt64();
        if (byteLength > int.MaxValue)
        {
            return false;
        }

        bytes = new byte[(int)byteLength];
        Marshal.Copy(snapshot.Data, bytes, 0, bytes.Length);
        return true;
    }

    private static byte[] CreateNullTerminatedUtf8(string value)
    {
        var byteCount = Encoding.UTF8.GetByteCount(value);
        var bytes = new byte[byteCount + 1];
        Encoding.UTF8.GetBytes(value, 0, value.Length, bytes, 0);
        return bytes;
    }
}
