using System;

namespace Editor.Core.Models.Panels;

public sealed class EditorPanelFrameContext
{
    public EditorPanelFrameContext(
        EditorPanelLifecycleContext panel,
        DateTimeOffset nowUtc,
        TimeSpan elapsedSinceLastFrame,
        long sequence)
    {
        ArgumentNullException.ThrowIfNull(panel);

        Panel = panel;
        NowUtc = nowUtc;
        ElapsedSinceLastFrame = elapsedSinceLastFrame;
        Sequence = sequence;
    }

    public EditorPanelLifecycleContext Panel { get; }

    public DateTimeOffset NowUtc { get; }

    public TimeSpan ElapsedSinceLastFrame { get; }

    public long Sequence { get; }

    public bool IsRepaintRequested { get; private set; }

    public void RequestRepaint()
    {
        IsRepaintRequested = true;
    }
}
