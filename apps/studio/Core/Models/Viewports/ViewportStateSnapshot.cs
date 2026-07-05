using System;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportStateSnapshot
{
    public ViewportStateSnapshot(
        ViewportId id,
        string displayName,
        ViewportKind kind,
        ViewportExtent extent,
        ViewportUpdatePolicy updatePolicy,
        ViewportClockSnapshot clock,
        bool isVisible,
        bool isFocused,
        bool isDirty,
        DateTimeOffset? lastRenderedAtUtc,
        TimeSpan interactiveBurstRemaining,
        ViewportRenderReason pendingReasons)
    {
        if (id.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(id));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(
                nameof(kind),
                kind,
                "Viewport kind is not defined.");
        }

        if (!Enum.IsDefined(updatePolicy))
        {
            throw new ArgumentOutOfRangeException(
                nameof(updatePolicy),
                updatePolicy,
                "Viewport update policy is not defined.");
        }

        ArgumentNullException.ThrowIfNull(extent);
        ArgumentNullException.ThrowIfNull(clock);

        if (interactiveBurstRemaining < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(interactiveBurstRemaining),
                interactiveBurstRemaining,
                "Interactive burst remaining time must be greater than or equal to zero.");
        }

        if ((pendingReasons & ~ViewportRenderReason.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(pendingReasons),
                pendingReasons,
                "Viewport render reasons include undefined flags.");
        }

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName)
            ? id.Value
            : displayName.Trim();
        Kind = kind;
        Extent = extent;
        UpdatePolicy = updatePolicy;
        Clock = clock;
        IsVisible = isVisible;
        IsFocused = isFocused;
        IsDirty = isDirty;
        LastRenderedAtUtc = lastRenderedAtUtc;
        InteractiveBurstRemaining = interactiveBurstRemaining;
        PendingReasons = pendingReasons;
    }

    public ViewportId Id { get; }

    public string DisplayName { get; }

    public ViewportKind Kind { get; }

    public ViewportExtent Extent { get; }

    public ViewportUpdatePolicy UpdatePolicy { get; }

    public ViewportClockSnapshot Clock { get; }

    public bool IsVisible { get; }

    public bool IsFocused { get; }

    public bool IsDirty { get; }

    public DateTimeOffset? LastRenderedAtUtc { get; }

    public TimeSpan InteractiveBurstRemaining { get; }

    public ViewportRenderReason PendingReasons { get; }
}
