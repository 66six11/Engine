using System;

namespace Asharia.Editor.Diagnostics.FrameDebug;

public sealed record FrameDebugCaptureSnapshot(
    string CaptureId,
    long FrameIndex,
    ulong SubmittedFrameEpoch,
    string ViewKind,
    int RequestedWidth,
    int RequestedHeight,
    DateTimeOffset CapturedAtUtc)
{
    public string CaptureId { get; init; } = FrameDebugModelGuard.Require(
        CaptureId,
        nameof(CaptureId),
        "Frame debug capture id");

    public string ViewKind { get; init; } = FrameDebugModelGuard.Require(
        ViewKind,
        nameof(ViewKind),
        "Frame debug capture view kind");

    public int RequestedWidth { get; init; } = Math.Max(0, RequestedWidth);

    public int RequestedHeight { get; init; } = Math.Max(0, RequestedHeight);
}
