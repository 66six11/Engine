namespace Asharia.Editor.Diagnostics.FrameDebug;

public sealed record FrameDebugResourceSnapshot(
    string Id,
    string Kind,
    uint ResourceIndex,
    string Name,
    string? Lifetime,
    string? FormatOrSize,
    string? Extent,
    string? InitialAccess,
    string? FinalAccess)
{
    public string Id { get; init; } = FrameDebugModelGuard.Require(
        Id,
        nameof(Id),
        "Frame debug resource id");

    public string Kind { get; init; } = FrameDebugModelGuard.Require(
        Kind,
        nameof(Kind),
        "Frame debug resource kind");

    public string Name { get; init; } = FrameDebugModelGuard.DisplayOrId(Name, Id);

    public string Lifetime { get; init; } = Lifetime ?? string.Empty;

    public string FormatOrSize { get; init; } = FormatOrSize ?? string.Empty;

    public string Extent { get; init; } = Extent ?? string.Empty;

    public string InitialAccess { get; init; } = InitialAccess ?? string.Empty;

    public string FinalAccess { get; init; } = FinalAccess ?? string.Empty;
}
