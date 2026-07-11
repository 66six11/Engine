namespace Asharia.Editor.Diagnostics.FrameDebug;

public sealed record FrameDebugExecutionEventSnapshot(
    string Id,
    int EventIndex,
    string Kind,
    string PassId,
    string PassName,
    string? CommandId,
    string Label,
    string? SourceResourceId,
    string? TargetResourceId,
    uint VertexCount,
    uint IndexCount,
    uint InstanceCount,
    uint GroupCountX,
    uint GroupCountY,
    uint GroupCountZ)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug execution event id");

    public string Kind { get; init; } = FrameDebugModelGuard.Require(
        Kind,
        nameof(Kind),
        "Frame debug execution event kind");

    public string PassId { get; init; } = FrameDebugModelGuard.Require(
        PassId,
        nameof(PassId),
        "Frame debug execution event pass id");

    public string PassName { get; init; } = FrameDebugModelGuard.DisplayOrId(PassName, PassId);

    public string? CommandId { get; init; } =
        string.IsNullOrWhiteSpace(CommandId) ? null : CommandId;

    public string Label { get; init; } = string.IsNullOrWhiteSpace(Label) ? Kind : Label;

    public string? SourceResourceId { get; init; } =
        string.IsNullOrWhiteSpace(SourceResourceId) ? null : SourceResourceId;

    public string? TargetResourceId { get; init; } =
        string.IsNullOrWhiteSpace(TargetResourceId) ? null : TargetResourceId;

    public int EventIndex { get; init; } = EventIndex < 0 ? 0 : EventIndex;
}
