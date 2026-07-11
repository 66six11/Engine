using System;

namespace Asharia.Editor.Viewports;

public sealed record ViewportRenderRequest
{
    public ViewportRenderRequest(
        ViewportId id,
        ViewportKind kind,
        ViewportExtent extent,
        ViewportClockSnapshot clock,
        ViewportUpdatePolicy updatePolicy,
        ViewportRenderReason reason,
        int priority,
        DateTimeOffset requestedAtUtc)
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

        if (reason == ViewportRenderReason.None
            || (reason & ~ViewportRenderReason.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Viewport render request must have one or more defined reasons.");
        }

        if (priority < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(priority),
                priority,
                "Viewport render request priority must be greater than or equal to zero.");
        }

        Id = id;
        Kind = kind;
        Extent = extent;
        Clock = clock;
        UpdatePolicy = updatePolicy;
        Reason = reason;
        Priority = priority;
        RequestedAtUtc = requestedAtUtc;
    }

    public ViewportId Id { get; }

    public ViewportKind Kind { get; }

    public ViewportExtent Extent { get; }

    public ViewportClockSnapshot Clock { get; }

    public ViewportUpdatePolicy UpdatePolicy { get; }

    public ViewportRenderReason Reason { get; }

    public int Priority { get; }

    public DateTimeOffset RequestedAtUtc { get; }
}
