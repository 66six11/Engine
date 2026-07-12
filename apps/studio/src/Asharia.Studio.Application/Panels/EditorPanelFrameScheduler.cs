using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Panels;

namespace Asharia.Studio.Application.Panels;

public sealed class EditorPanelFrameScheduler
{
    private readonly Dictionary<string, ScheduledPanel> panelsById_ =
        new(StringComparer.Ordinal);
    private long nextSequence_;

    public void AttachPanel(
        EditorPanelLifecycleContext context,
        IEditorPanelFrameUpdateSink sink)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(sink);

        panelsById_[context.PanelId] = new ScheduledPanel(context, sink);
    }

    public void UpdatePanel(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (panelsById_.TryGetValue(context.PanelId, out var panel))
        {
            panel.Context = context;
        }
    }

    public void ActivatePanel(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (panelsById_.TryGetValue(context.PanelId, out var panel))
        {
            panel.Context = context;
            panel.IsActive = true;
        }
    }

    public void DeactivatePanel(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (panelsById_.TryGetValue(context.PanelId, out var panel))
        {
            panel.Context = context;
            panel.IsActive = false;
        }
    }

    public void DetachPanel(EditorPanelLifecycleContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        panelsById_.Remove(context.PanelId);
    }

    public IReadOnlyList<EditorPanelFrameContext> Tick(DateTimeOffset nowUtc)
    {
        var frames = new List<EditorPanelFrameContext>();
        foreach (var panel in panelsById_.Values.ToArray())
        {
            if (!ShouldTick(panel, nowUtc))
            {
                continue;
            }

            var elapsed = panel.LastFrameAtUtc is { } lastFrameAtUtc
                ? nowUtc - lastFrameAtUtc
                : TimeSpan.Zero;
            var context = new EditorPanelFrameContext(
                panel.Context,
                nowUtc,
                elapsed,
                ++nextSequence_);
            panel.Sink.OnEditorPanelFrame(context);
            panel.LastFrameAtUtc = nowUtc;
            frames.Add(context);
        }

        return frames;
    }

    private static bool ShouldTick(ScheduledPanel panel, DateTimeOffset nowUtc)
    {
        var request = panel.Sink.FrameUpdateRequest;
        if (request.Mode == EditorPanelFrameUpdateMode.Manual)
        {
            return false;
        }

        if (request.Mode == EditorPanelFrameUpdateMode.Active && !panel.IsActive)
        {
            return false;
        }

        if (request.TargetFramesPerSecond is not { } targetFramesPerSecond
            || panel.LastFrameAtUtc is null)
        {
            return true;
        }

        return nowUtc - panel.LastFrameAtUtc.Value >= GetTargetFrameInterval(targetFramesPerSecond);
    }

    private static TimeSpan GetTargetFrameInterval(double targetFramesPerSecond)
    {
        return TimeSpan.FromTicks(
            (long)Math.Ceiling(TimeSpan.TicksPerSecond / targetFramesPerSecond));
    }

    private sealed class ScheduledPanel(
        EditorPanelLifecycleContext context,
        IEditorPanelFrameUpdateSink sink)
    {
        public EditorPanelLifecycleContext Context { get; set; } = context;

        public IEditorPanelFrameUpdateSink Sink { get; } = sink;

        public bool IsActive { get; set; }

        public DateTimeOffset? LastFrameAtUtc { get; set; }
    }
}
