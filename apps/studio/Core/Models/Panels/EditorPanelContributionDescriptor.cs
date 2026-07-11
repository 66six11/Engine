using Asharia.Editor.Panels;
namespace Editor.Core.Models.Panels;

public sealed record EditorPanelContributionDescriptor(
    string Id,
    string Title,
    PanelKind Kind,
    EditorDockArea DefaultDockArea,
    string MenuPath,
    DockContentCachePolicy CachePolicy,
    EditorPanelContentModelReference ContentModel,
    EditorPanelLifecycleDescriptor Lifecycle,
    EditorPanelFrameUpdateDescriptor FrameUpdate);
