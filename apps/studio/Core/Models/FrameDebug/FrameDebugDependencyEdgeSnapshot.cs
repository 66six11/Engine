namespace Editor.Core.Models.FrameDebug;

public sealed record FrameDebugDependencyEdgeSnapshot(
    string Id,
    string FromPassId,
    string ToPassId,
    string ResourceId,
    string ResourceName,
    string Reason)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug dependency edge id");

    public string FromPassId { get; init; } = FrameDebugModelGuard.Require(
        FromPassId,
        nameof(FromPassId),
        "Frame debug dependency source pass id");

    public string ToPassId { get; init; } = FrameDebugModelGuard.Require(
        ToPassId,
        nameof(ToPassId),
        "Frame debug dependency target pass id");

    public string ResourceId { get; init; } = FrameDebugModelGuard.Require(
        ResourceId,
        nameof(ResourceId),
        "Frame debug dependency resource id");

    public string ResourceName { get; init; } =
        FrameDebugModelGuard.DisplayOrId(ResourceName, ResourceId);

    public string Reason { get; init; } = Reason ?? string.Empty;
}
