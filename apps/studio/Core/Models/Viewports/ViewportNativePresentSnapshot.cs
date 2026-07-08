using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportNativePresentSnapshot
{
    public ViewportNativePresentSnapshot(
        ViewportId viewportId,
        ViewportExtent requestedExtent,
        ViewportExtent? actualExtent,
        string formatName,
        string colorSpace,
        ulong frameIndex,
        ViewportNativePresentStatus status,
        string message,
        DateTimeOffset presentedAtUtc)
    {
        if (viewportId.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(viewportId));
        }

        ArgumentNullException.ThrowIfNull(requestedExtent);

        if (!Enum.IsDefined(status))
        {
            throw new ArgumentOutOfRangeException(
                nameof(status),
                status,
                "Viewport native present status is not defined.");
        }

        ViewportId = viewportId;
        RequestedExtent = requestedExtent;
        ActualExtent = actualExtent;
        FormatName = string.IsNullOrWhiteSpace(formatName) ? "Unknown" : formatName.Trim();
        ColorSpace = string.IsNullOrWhiteSpace(colorSpace) ? "Unknown" : colorSpace.Trim();
        FrameIndex = frameIndex;
        Status = status;
        Message = string.IsNullOrWhiteSpace(message) ? status.ToString() : message.Trim();
        PresentedAtUtc = presentedAtUtc;
    }

    public ViewportId ViewportId { get; }

    public ViewportExtent RequestedExtent { get; }

    public ViewportExtent? ActualExtent { get; }

    public string FormatName { get; }

    public string ColorSpace { get; }

    public ulong FrameIndex { get; }

    public ViewportNativePresentStatus Status { get; }

    public string Message { get; }

    public DateTimeOffset PresentedAtUtc { get; }
}
