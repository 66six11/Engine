using System;

namespace Editor.Core.Models.FrameDebug;

public sealed class NativeFrameDebuggerSnapshotPayload
{
    private NativeFrameDebuggerSnapshotPayload(
        NativeFrameDebuggerSnapshotFormat format,
        int schemaVersion,
        byte[] bytes)
    {
        Format = format;
        SchemaVersion = Math.Max(1, schemaVersion);
        Bytes = bytes;
    }

    public NativeFrameDebuggerSnapshotFormat Format { get; }

    public int SchemaVersion { get; }

    public ReadOnlyMemory<byte> Bytes { get; }

    public static NativeFrameDebuggerSnapshotPayload JsonUtf8(ReadOnlySpan<byte> bytes)
    {
        return new NativeFrameDebuggerSnapshotPayload(
            NativeFrameDebuggerSnapshotFormat.JsonUtf8,
            schemaVersion: 1,
            bytes.ToArray());
    }
}
