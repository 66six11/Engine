namespace Editor.Core.Models.FrameDebug;

public sealed record FrameDebugPreviewSnapshot(
    string Status,
    string? SelectedPassId,
    string? SelectedExecutionEventId,
    string? Message)
{
    public string Status { get; init; } = FrameDebugModelGuard.Require(
        Status,
        nameof(Status),
        "Frame debug preview status");

    public string? SelectedPassId { get; init; } =
        string.IsNullOrWhiteSpace(SelectedPassId) ? null : SelectedPassId;

    public string? SelectedExecutionEventId { get; init; } =
        string.IsNullOrWhiteSpace(SelectedExecutionEventId) ? null : SelectedExecutionEventId;

    public string Message { get; init; } = Message ?? string.Empty;
}
