namespace Asharia.Editor.Diagnostics.FrameDebug;

public sealed record FrameDebugPassSnapshot(
    string Id,
    int PassIndex,
    int DeclarationIndex,
    string Name,
    string? Type,
    string? ParamsType,
    bool AllowCulling,
    bool HasSideEffects,
    int CommandCount,
    int ImageTransitionCount,
    int BufferTransitionCount)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug pass id");

    public string Name { get; init; } = FrameDebugModelGuard.DisplayOrId(Name, Id);

    public string Type { get; init; } = Type ?? string.Empty;

    public string ParamsType { get; init; } = ParamsType ?? string.Empty;

    public int PassIndex { get; init; } = PassIndex < 0 ? 0 : PassIndex;

    public int DeclarationIndex { get; init; } = DeclarationIndex < 0 ? 0 : DeclarationIndex;

    public int CommandCount { get; init; } = CommandCount < 0 ? 0 : CommandCount;

    public int ImageTransitionCount { get; init; } =
        ImageTransitionCount < 0 ? 0 : ImageTransitionCount;

    public int BufferTransitionCount { get; init; } =
        BufferTransitionCount < 0 ? 0 : BufferTransitionCount;
}
