using System;
using System.Runtime.InteropServices;
using System.Text;
using Editor.Core.Abstractions;
using Editor.Core.Interop.FrameDebugger.Api;
using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Interop.FrameDebugger.Adapters;

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
        var snapshot = FrameDebuggerNativeSnapshotBuffer.CreateForCall();
        if (!FrameDebuggerNativeStatus.IsSuccess(api_.AcquireSnapshot(ref snapshot)))
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

        var executionEventIdUtf8 = Encoding.UTF8.GetBytes(executionEventId);
        var nativeBytes = Marshal.AllocHGlobal(executionEventIdUtf8.Length);
        try
        {
            Marshal.Copy(executionEventIdUtf8, 0, nativeBytes, executionEventIdUtf8.Length);
            return FrameDebuggerNativeStatus.IsSuccess(
                api_.SelectExecutionEvent(
                    new FrameDebuggerNativeStringView(
                        nativeBytes,
                        checked((ulong)executionEventIdUtf8.Length))));
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
        if (!snapshot.HasSupportedHeader ||
            snapshot.Data == IntPtr.Zero ||
            snapshot.ByteLength == 0UL ||
            snapshot.Format != NativeFrameDebuggerSnapshotFormat.JsonUtf8)
        {
            return false;
        }

        if (snapshot.ByteLength > int.MaxValue)
        {
            return false;
        }

        bytes = new byte[(int)snapshot.ByteLength];
        Marshal.Copy(snapshot.Data, bytes, 0, bytes.Length);
        return true;
    }
}
