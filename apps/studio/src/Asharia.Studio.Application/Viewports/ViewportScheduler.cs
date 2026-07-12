using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Viewports;

namespace Asharia.Studio.Application.Viewports;

public sealed class ViewportScheduler
{
    private readonly ViewportSchedulerOptions options_;

    public ViewportScheduler(ViewportSchedulerOptions? options = null)
    {
        options_ = options ?? ViewportSchedulerOptions.Default;
    }

    public IReadOnlyList<ViewportRenderRequest> BuildRenderPlan(
        IReadOnlyList<ViewportStateSnapshot> viewports,
        ViewportSchedulerContext context)
    {
        ArgumentNullException.ThrowIfNull(viewports);
        ArgumentNullException.ThrowIfNull(context);

        var requests = new List<ViewportRenderRequest>();
        foreach (var viewport in viewports)
        {
            ArgumentNullException.ThrowIfNull(viewport);

            var reason = GetRenderReason(viewport, context.NowUtc);
            if (reason == ViewportRenderReason.None)
            {
                continue;
            }

            requests.Add(new ViewportRenderRequest(
                viewport.Id,
                viewport.Kind,
                viewport.Extent,
                viewport.Clock,
                viewport.UpdatePolicy,
                reason,
                GetPriority(viewport, reason),
                context.NowUtc));
        }

        return requests
            .OrderByDescending(request => request.Priority)
            .ThenBy(request => request.Id.Value, StringComparer.Ordinal)
            .Take(context.MaxViewportRendersThisTick)
            .ToArray();
    }

    private ViewportRenderReason GetRenderReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc)
    {
        if (!viewport.IsVisible)
        {
            return ViewportRenderReason.None;
        }

        var reason = viewport.PendingReasons;
        if (viewport.LastRenderedAtUtc is null)
        {
            reason |= ViewportRenderReason.InitialFrameMissing;
        }

        if (viewport.IsDirty)
        {
            reason |= ViewportRenderReason.AssetChanged;
        }

        return viewport.UpdatePolicy switch
        {
            ViewportUpdatePolicy.DirtyOnly => reason,
            ViewportUpdatePolicy.InteractiveBurst =>
                GetInteractiveBurstReason(viewport, nowUtc, reason),
            ViewportUpdatePolicy.TimePlayback =>
                GetTimePlaybackReason(viewport, nowUtc, reason),
            ViewportUpdatePolicy.RuntimePlay =>
                GetRuntimeReason(viewport, nowUtc, reason),
            ViewportUpdatePolicy.FrameDebug =>
                GetFrameDebugReason(reason),
            ViewportUpdatePolicy.PerformancePreview =>
                GetRuntimeReason(viewport, nowUtc, reason),
            _ => throw new ArgumentOutOfRangeException(
                nameof(viewport),
                viewport.UpdatePolicy,
                "Viewport update policy is not defined."),
        };
    }

    private ViewportRenderReason GetInteractiveBurstReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc,
        ViewportRenderReason reason)
    {
        if (viewport.IsFocused
            && viewport.InteractiveBurstRemaining > TimeSpan.Zero
            && (reason != ViewportRenderReason.None
                || ShouldTick(
                    viewport.LastRenderedAtUtc,
                    nowUtc,
                    options_.InteractiveBurstInterval)))
        {
            reason |= ViewportRenderReason.InputActive;
        }

        if (reason == ViewportRenderReason.None
            && ShouldTick(viewport.LastRenderedAtUtc, nowUtc, options_.SceneIdleInterval))
        {
            reason |= ViewportRenderReason.VisibleExposed;
        }

        return reason;
    }

    private ViewportRenderReason GetTimePlaybackReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc,
        ViewportRenderReason reason)
    {
        if (ShouldTick(viewport.LastRenderedAtUtc, nowUtc, options_.PreviewInterval))
        {
            reason |= ViewportRenderReason.TimeAdvanced;
        }

        return reason;
    }

    private ViewportRenderReason GetRuntimeReason(
        ViewportStateSnapshot viewport,
        DateTimeOffset nowUtc,
        ViewportRenderReason reason)
    {
        if (ShouldTick(viewport.LastRenderedAtUtc, nowUtc, options_.RuntimeInterval))
        {
            reason |= ViewportRenderReason.RuntimePlaying;
        }

        return reason;
    }

    private static ViewportRenderReason GetFrameDebugReason(
        ViewportRenderReason reason)
    {
        return reason
            & (ViewportRenderReason.InitialFrameMissing
                | ViewportRenderReason.VisibleExposed
                | ViewportRenderReason.Resized
                | ViewportRenderReason.AssetChanged
                | ViewportRenderReason.ShaderChanged
                | ViewportRenderReason.FrameDebugStep
                | ViewportRenderReason.CaptureRequested);
    }

    private static bool ShouldTick(
        DateTimeOffset? lastRenderedAtUtc,
        DateTimeOffset nowUtc,
        TimeSpan interval)
    {
        return lastRenderedAtUtc is null
            || lastRenderedAtUtc.Value > nowUtc
            || nowUtc - lastRenderedAtUtc.Value >= interval;
    }

    private static int GetPriority(
        ViewportStateSnapshot viewport,
        ViewportRenderReason reason)
    {
        if (reason.HasFlag(ViewportRenderReason.CaptureRequested))
        {
            return 100;
        }

        if (reason.HasFlag(ViewportRenderReason.FrameDebugStep))
        {
            return 90;
        }

        if (reason.HasFlag(ViewportRenderReason.RuntimePlaying)
            && viewport.IsFocused)
        {
            return 85;
        }

        if (reason.HasFlag(ViewportRenderReason.InputActive))
        {
            return 80;
        }

        if (reason.HasFlag(ViewportRenderReason.InitialFrameMissing))
        {
            return 70;
        }

        if (viewport.IsFocused)
        {
            return 60;
        }

        return 40;
    }
}
