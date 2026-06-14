using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockWorkspaceViewModel : ViewModelBase
{
    private readonly Dictionary<DockArea, EditorDockPaneViewModel> panesByArea_;

    public EditorDockWorkspaceViewModel(IPanelRegistry panelRegistry)
    {
        LeftPane = new EditorDockPaneViewModel("owned-dock-left", "Hierarchy Stack", DockArea.Left, "Scene tree");
        CenterPane = new EditorDockPaneViewModel("owned-dock-center", "Viewport Stack", DockArea.Center, "Primary work area");
        BottomPane = new EditorDockPaneViewModel("owned-dock-bottom", "Diagnostics Stack", DockArea.Bottom, "Output and validation");
        RightPane = new EditorDockPaneViewModel("owned-dock-right", "Inspector Stack", DockArea.Right, "Selection context");

        panesByArea_ = new Dictionary<DockArea, EditorDockPaneViewModel>
        {
            [DockArea.Left] = LeftPane,
            [DockArea.Center] = CenterPane,
            [DockArea.Bottom] = BottomPane,
            [DockArea.Right] = RightPane,
        };

        foreach (var descriptor in panelRegistry.GetAll())
        {
            var pane = panesByArea_[descriptor.DefaultArea];
            pane.Add(CreateTab(descriptor));
        }
    }

    public EditorDockPaneViewModel LeftPane { get; }

    public EditorDockPaneViewModel CenterPane { get; }

    public EditorDockPaneViewModel BottomPane { get; }

    public EditorDockPaneViewModel RightPane { get; }

    public EditorDockDragStateViewModel DragState { get; } = new();

    public void BeginDrag(EditorDockTabViewModel tab, double x, double y)
    {
        FindPane(tab)?.Activate(tab);
        DragState.Begin(tab, x, y);
    }

    public void UpdateDrag(double x, double y)
    {
        DragState.UpdatePointer(x, y);
    }

    public void CompleteDrag(DockArea targetArea)
    {
        var tab = DragState.DraggedTab;
        if (tab is null)
        {
            DragState.Clear();
            return;
        }

        MoveTab(tab, panesByArea_[targetArea]);
        DragState.Clear();
    }

    public void CancelDrag()
    {
        DragState.Clear();
    }

    private void MoveTab(EditorDockTabViewModel tab, EditorDockPaneViewModel targetPane)
    {
        var sourcePane = FindPane(tab);
        if (sourcePane is null)
        {
            return;
        }

        if (!ReferenceEquals(sourcePane, targetPane))
        {
            sourcePane.Remove(tab);
            targetPane.Add(tab);
        }

        targetPane.Activate(tab);
    }

    private EditorDockPaneViewModel? FindPane(EditorDockTabViewModel tab)
    {
        foreach (var pane in panesByArea_.Values)
        {
            if (pane.Tabs.Contains(tab))
            {
                return pane;
            }
        }

        return null;
    }

    private static EditorDockTabViewModel CreateTab(PanelDescriptor descriptor)
    {
        return new EditorDockTabViewModel(
            descriptor.Id,
            descriptor.Title,
            GetTag(descriptor),
            GetTitleDetail(descriptor),
            GetStatusText(descriptor),
            descriptor.Kind,
            descriptor.DefaultArea,
            descriptor.CreateContent());
    }

    private static string GetTag(PanelDescriptor descriptor)
    {
        return descriptor.Kind == PanelKind.Document ? "DOC" : descriptor.DefaultArea.ToString().ToUpperInvariant();
    }

    private static string GetTitleDetail(PanelDescriptor descriptor)
    {
        return descriptor.Id switch
        {
            "scene-view" => "custom viewport shell",
            "hierarchy" => "selection source",
            "inspector" => "context target",
            "console" => "runtime log stream",
            "problems" => "validation queue",
            _ => descriptor.MenuPath,
        };
    }

    private static string GetStatusText(PanelDescriptor descriptor)
    {
        return descriptor.Id switch
        {
            "scene-view" => "live",
            "problems" => "0",
            "console" => "idle",
            _ => descriptor.Kind.ToString().ToLowerInvariant(),
        };
    }
}
