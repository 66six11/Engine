using System;

namespace Asharia.Editor.Viewports;

public sealed record ViewportRenderResult
{
    public ViewportRenderResult(
        ViewportId id,
        ViewportKind kind,
        ViewportExtent requestedExtent,
        ViewportRenderReason reason,
        bool wasRendered,
        string statusMessage,
        DateTimeOffset completedAtUtc,
        double? cpuMilliseconds)
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

        ArgumentNullException.ThrowIfNull(requestedExtent);

        if ((reason & ~ViewportRenderReason.All) != 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Viewport render result contains undefined reason flags.");
        }

        if (wasRendered && reason == ViewportRenderReason.None)
        {
            throw new ArgumentOutOfRangeException(
                nameof(reason),
                reason,
                "Rendered viewport results must preserve the render reason.");
        }

        if (cpuMilliseconds is { } duration
            && (duration < 0 || !double.IsFinite(duration)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(cpuMilliseconds),
                cpuMilliseconds,
                "Viewport render CPU duration must be finite and greater than or equal to zero.");
        }

        Id = id;
        Kind = kind;
        RequestedExtent = requestedExtent;
        Reason = reason;
        WasRendered = wasRendered;
        StatusMessage = string.IsNullOrWhiteSpace(statusMessage)
            ? id.Value
            : statusMessage.Trim();
        CompletedAtUtc = completedAtUtc;
        CpuMilliseconds = cpuMilliseconds;
    }

    public ViewportId Id { get; }

    public ViewportKind Kind { get; }

    public ViewportExtent RequestedExtent { get; }

    public ViewportRenderReason Reason { get; }

    public bool WasRendered { get; }

    public string StatusMessage { get; }

    public DateTimeOffset CompletedAtUtc { get; }

    public double? CpuMilliseconds { get; }
}
