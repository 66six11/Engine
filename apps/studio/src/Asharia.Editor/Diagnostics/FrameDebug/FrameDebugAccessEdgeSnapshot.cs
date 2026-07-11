namespace Asharia.Editor.Diagnostics.FrameDebug;

public sealed record FrameDebugAccessEdgeSnapshot(
    string Id,
    string PassId,
    string ResourceId,
    string PassName,
    string ResourceName,
    string? SlotName,
    string? Access,
    string? ShaderStage)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug access edge id");

    public string PassId { get; init; } = FrameDebugModelGuard.Require(
        PassId,
        nameof(PassId),
        "Frame debug access edge pass id");

    public string ResourceId { get; init; } = FrameDebugModelGuard.Require(
        ResourceId,
        nameof(ResourceId),
        "Frame debug access edge resource id");

    public string PassName { get; init; } = FrameDebugModelGuard.DisplayOrId(PassName, PassId);

    public string ResourceName { get; init; } =
        FrameDebugModelGuard.DisplayOrId(ResourceName, ResourceId);

    public string SlotName { get; init; } = SlotName ?? string.Empty;

    public string Access { get; init; } = Access ?? string.Empty;

    public string ShaderStage { get; init; } = ShaderStage ?? string.Empty;
}
