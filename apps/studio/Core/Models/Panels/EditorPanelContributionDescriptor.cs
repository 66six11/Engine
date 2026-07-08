namespace Editor.Core.Models.Panels;

public sealed record EditorPanelContributionDescriptor(
    string Id,
    string Title,
    PanelKind Kind,
    DockArea DefaultDockArea,
    string MenuPath,
    DockContentCachePolicy CachePolicy,
    EditorPanelContentModelReference ContentModel,
    EditorPanelLifecycleDescriptor Lifecycle,
    EditorPanelFrameUpdateDescriptor FrameUpdate);
