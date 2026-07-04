using System;
using System.Runtime.InteropServices;
using System.Text;
using Editor.Core.Interop.FrameDebugger.Adapters;
using Editor.Core.Interop.FrameDebugger.Api;
using Editor.Core.Models.FrameDebug;
using Xunit;

namespace Editor.Tests.Core.Interop.FrameDebugger.Adapters;

public sealed class FrameDebuggerNativeBridgeTests
{
    [Fact]
    public void TryAcquireSnapshot_copies_json_payload_and_releases_native_buffer()
    {
        var expectedBytes = Encoding.UTF8.GetBytes("""{"schemaVersion":1}""");
        using var api = new StubFrameDebuggerNativeApi
        {
            SnapshotBytes = expectedBytes,
            SnapshotFormat = NativeFrameDebuggerSnapshotFormat.JsonUtf8,
        };
        var bridge = new FrameDebuggerNativeBridge(api);

        Assert.True(bridge.TryAcquireSnapshot(out var payload));

        Assert.NotNull(payload);
        Assert.Equal(NativeFrameDebuggerSnapshotFormat.JsonUtf8, payload.Format);
        Assert.Equal(expectedBytes, payload.Bytes.ToArray());
        Assert.Equal(1, api.ReleaseSnapshotCalls);
        Assert.Equal((ulong)expectedBytes.Length, api.LastReleasedSnapshot.ByteLength.ToUInt64());
    }

    [Fact]
    public void TryAcquireSnapshot_releases_native_buffer_when_format_is_unsupported()
    {
        using var api = new StubFrameDebuggerNativeApi
        {
            SnapshotBytes = Encoding.UTF8.GetBytes("unsupported"),
            SnapshotFormat = (NativeFrameDebuggerSnapshotFormat)99,
        };
        var bridge = new FrameDebuggerNativeBridge(api);

        Assert.False(bridge.TryAcquireSnapshot(out var payload));

        Assert.Null(payload);
        Assert.Equal(1, api.ReleaseSnapshotCalls);
    }

    [Fact]
    public void TryAcquireSnapshot_rejects_empty_native_buffer()
    {
        using var api = new StubFrameDebuggerNativeApi
        {
            SnapshotBytes = [],
            SnapshotFormat = NativeFrameDebuggerSnapshotFormat.JsonUtf8,
        };
        var bridge = new FrameDebuggerNativeBridge(api);

        Assert.False(bridge.TryAcquireSnapshot(out var payload));

        Assert.Null(payload);
        Assert.Equal(0, api.ReleaseSnapshotCalls);
    }

    [Fact]
    public void SelectExecutionEvent_sends_utf8_null_terminated_identifier()
    {
        using var api = new StubFrameDebuggerNativeApi();
        var bridge = new FrameDebuggerNativeBridge(api);

        Assert.True(bridge.SelectExecutionEvent("event:draw-7"));

        Assert.Equal("event:draw-7", api.SelectedExecutionEventId);
        Assert.True(api.SelectedExecutionEventWasNullTerminated);
    }

    [Fact]
    public void Capture_and_resume_commands_return_native_status()
    {
        using var api = new StubFrameDebuggerNativeApi
        {
            RequestCaptureResult = FrameDebuggerNativeStatus.Success,
            RequestResumeResult = FrameDebuggerNativeStatus.Unavailable,
        };
        var bridge = new FrameDebuggerNativeBridge(api);

        Assert.True(bridge.RequestCapture());
        Assert.False(bridge.RequestResume());
    }

    [Fact]
    public void SelectExecutionEvent_rejects_blank_identifier()
    {
        using var api = new StubFrameDebuggerNativeApi();
        var bridge = new FrameDebuggerNativeBridge(api);

        Assert.False(bridge.SelectExecutionEvent(" "));

        Assert.Equal(0, api.SelectExecutionEventCalls);
    }

    private sealed class StubFrameDebuggerNativeApi : IFrameDebuggerNativeApi, IDisposable
    {
        private IntPtr allocatedSnapshot_;

        public byte[]? SnapshotBytes { get; init; }

        public NativeFrameDebuggerSnapshotFormat SnapshotFormat { get; init; } =
            NativeFrameDebuggerSnapshotFormat.JsonUtf8;

        public int AcquireSnapshotResult { get; init; } = FrameDebuggerNativeStatus.Success;

        public int RequestCaptureResult { get; init; } = FrameDebuggerNativeStatus.Success;

        public int RequestResumeResult { get; init; } = FrameDebuggerNativeStatus.Success;

        public int ReleaseSnapshotCalls { get; private set; }

        public int SelectExecutionEventCalls { get; private set; }

        public string? SelectedExecutionEventId { get; private set; }

        public bool SelectedExecutionEventWasNullTerminated { get; private set; }

        public FrameDebuggerNativeSnapshotBuffer LastReleasedSnapshot { get; private set; }

        public int AcquireSnapshot(out FrameDebuggerNativeSnapshotBuffer snapshot)
        {
            if (AcquireSnapshotResult != FrameDebuggerNativeStatus.Success)
            {
                snapshot = default;
                return AcquireSnapshotResult;
            }

            if (SnapshotBytes is null)
            {
                snapshot = default;
                return FrameDebuggerNativeStatus.Unavailable;
            }

            if (SnapshotBytes.Length == 0)
            {
                snapshot = new FrameDebuggerNativeSnapshotBuffer(
                    IntPtr.Zero,
                    UIntPtr.Zero,
                    SnapshotFormat);
                return FrameDebuggerNativeStatus.Success;
            }

            allocatedSnapshot_ = Marshal.AllocHGlobal(SnapshotBytes.Length);
            Marshal.Copy(SnapshotBytes, 0, allocatedSnapshot_, SnapshotBytes.Length);
            snapshot = new FrameDebuggerNativeSnapshotBuffer(
                allocatedSnapshot_,
                (UIntPtr)SnapshotBytes.Length,
                SnapshotFormat);
            return FrameDebuggerNativeStatus.Success;
        }

        public void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot)
        {
            ReleaseSnapshotCalls++;
            LastReleasedSnapshot = snapshot;
            if (snapshot.Data != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(snapshot.Data);
                if (snapshot.Data == allocatedSnapshot_)
                {
                    allocatedSnapshot_ = IntPtr.Zero;
                }
            }
        }

        public int RequestCapture()
        {
            return RequestCaptureResult;
        }

        public int RequestResume()
        {
            return RequestResumeResult;
        }

        public int SelectExecutionEvent(IntPtr executionEventIdUtf8)
        {
            SelectExecutionEventCalls++;
            SelectedExecutionEventId = Marshal.PtrToStringUTF8(executionEventIdUtf8);
            var byteLength = Encoding.UTF8.GetByteCount(SelectedExecutionEventId ?? string.Empty);
            SelectedExecutionEventWasNullTerminated =
                Marshal.ReadByte(executionEventIdUtf8, byteLength) == 0;
            return FrameDebuggerNativeStatus.Success;
        }

        public void Dispose()
        {
            if (allocatedSnapshot_ != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(allocatedSnapshot_);
                allocatedSnapshot_ = IntPtr.Zero;
            }
        }
    }
}
