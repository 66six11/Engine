namespace Editor.Core.Models.FrameDebug;

public sealed record FrameDebugCommandSnapshot(
    string Id,
    string PassId,
    int CommandIndex,
    int DeclarationIndex,
    string PassName,
    string Kind,
    string? Detail)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug command id");

    public string PassId { get; init; } = FrameDebugModelGuard.Require(
        PassId,
        nameof(PassId),
        "Frame debug command pass id");

    public string PassName { get; init; } = FrameDebugModelGuard.DisplayOrId(PassName, PassId);

    public string Kind { get; init; } = FrameDebugModelGuard.Require(
        Kind,
        nameof(Kind),
        "Frame debug command kind");

    public string Detail { get; init; } = Detail ?? string.Empty;

    public int CommandIndex { get; init; } = CommandIndex < 0 ? 0 : CommandIndex;

    public int DeclarationIndex { get; init; } = DeclarationIndex < 0 ? 0 : DeclarationIndex;
}
