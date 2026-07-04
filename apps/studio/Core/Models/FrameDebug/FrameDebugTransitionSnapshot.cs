namespace Editor.Core.Models.FrameDebug;

public sealed record FrameDebugTransitionSnapshot(
    string Id,
    string Phase,
    string PassId,
    string ResourceId,
    string PassName,
    string ResourceName,
    string OldAccess,
    string NewAccess)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug transition id");

    public string Phase { get; init; } = FrameDebugModelGuard.Require(
        Phase,
        nameof(Phase),
        "Frame debug transition phase");

    public string PassId { get; init; } = FrameDebugModelGuard.Require(
        PassId,
        nameof(PassId),
        "Frame debug transition pass id");

    public string ResourceId { get; init; } = FrameDebugModelGuard.Require(
        ResourceId,
        nameof(ResourceId),
        "Frame debug transition resource id");

    public string PassName { get; init; } = FrameDebugModelGuard.DisplayOrId(PassName, PassId);

    public string ResourceName { get; init; } =
        FrameDebugModelGuard.DisplayOrId(ResourceName, ResourceId);

    public string OldAccess { get; init; } = OldAccess ?? string.Empty;

    public string NewAccess { get; init; } = NewAccess ?? string.Empty;
}
