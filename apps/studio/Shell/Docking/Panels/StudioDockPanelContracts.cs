using System;
using System.Collections.Generic;

namespace Editor.Shell.Docking.Panels;

public enum EditorDockArea
{
    Center,
    Left,
    Right,
    Bottom,
}

public enum PanelKind
{
    Document,
    Tool,
}

public enum DockContentCachePolicy
{
    KeepAlive,
    RecreateOnOpen,
}

public sealed record PanelDescriptor(
    string Id,
    string Title,
    PanelKind Kind,
    EditorDockArea DefaultArea,
    string MenuPath,
    DockContentCachePolicy CachePolicy,
    Func<object> CreateContent,
    string? IconKey = null,
    string? Tag = null,
    string? TitleDetail = null,
    string? StatusText = null);

public interface IPanelRegistry
{
    void Register(PanelDescriptor descriptor);

    IReadOnlyList<PanelDescriptor> GetAll();

    PanelDescriptor GetRequired(string id);
}

public sealed record EditorPanelLifecycleContext(
    string PanelId,
    string Title,
    EditorDockArea DockArea,
    bool IsFloatingWorkspace)
{
    public bool IsMainWorkspace => !IsFloatingWorkspace;
}

public sealed record EditorPanelLayoutContext
{
    public EditorPanelLayoutContext(
        EditorPanelLifecycleContext panel,
        double logicalWidth,
        double logicalHeight,
        double renderScale)
    {
        ArgumentNullException.ThrowIfNull(panel);
        if (!double.IsFinite(logicalWidth) || logicalWidth < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(logicalWidth));
        }
        if (!double.IsFinite(logicalHeight) || logicalHeight < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(logicalHeight));
        }
        if (!double.IsFinite(renderScale) || renderScale <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(renderScale));
        }

        Panel = panel;
        LogicalWidth = logicalWidth;
        LogicalHeight = logicalHeight;
        RenderScale = renderScale;
    }

    public EditorPanelLifecycleContext Panel { get; }
    public double LogicalWidth { get; }
    public double LogicalHeight { get; }
    public double RenderScale { get; }
}

public interface IEditorPanelLifecycleSink
{
    void OnPanelAttached(EditorPanelLifecycleContext context);
    void OnPanelActivated(EditorPanelLifecycleContext context);
    void OnPanelDeactivated(EditorPanelLifecycleContext context);
    void OnPanelDetached(EditorPanelLifecycleContext context);
}

public interface IEditorPanelVisibilitySink
{
    void OnPanelShown(EditorPanelLifecycleContext context);
    void OnPanelHidden(EditorPanelLifecycleContext context);
}

public interface IEditorPanelLayoutSink
{
    void OnPanelLayoutChanged(EditorPanelLayoutContext context);
}
