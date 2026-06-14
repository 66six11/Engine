using System;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Orientation = Dock.Model.Core.Orientation;

namespace Editor.Shell.Docking;

public sealed class EditorDockFactory : Factory
{
    private const string SceneViewPanelId = "scene-view";
    private const string HierarchyPanelId = "hierarchy";
    private const string InspectorPanelId = "inspector";
    private const string ConsolePanelId = "console";
    private const string ProblemsPanelId = "problems";

    private readonly IPanelRegistry panelRegistry_;

    public EditorDockFactory(IPanelRegistry panelRegistry)
    {
        panelRegistry_ = panelRegistry;
    }

    public override IRootDock CreateLayout()
    {
        var sceneView = CreateDocumentDockable(SceneViewPanelId);
        var hierarchy = CreateToolDockable(HierarchyPanelId);
        var inspector = CreateToolDockable(InspectorPanelId);
        var console = CreateToolDockable(ConsolePanelId);
        var problems = CreateToolDockable(ProblemsPanelId);

        var leftDock = new ToolDock
        {
            Id = "dock-left",
            Title = "Left",
            Proportion = 0.20,
            VisibleDockables = CreateList<IDockable>(hierarchy),
            ActiveDockable = hierarchy,
            DefaultDockable = hierarchy,
            CanCloseLastDockable = false,
        };

        var documentDock = new DocumentDock
        {
            Id = "dock-documents",
            Title = "Documents",
            Proportion = 0.72,
            VisibleDockables = CreateList<IDockable>(sceneView),
            ActiveDockable = sceneView,
            DefaultDockable = sceneView,
            CanCreateDocument = false,
            CanCloseLastDockable = false,
        };

        var bottomDock = new ToolDock
        {
            Id = "dock-bottom",
            Title = "Bottom",
            Proportion = 0.28,
            VisibleDockables = CreateList<IDockable>(console, problems),
            ActiveDockable = console,
            DefaultDockable = console,
            CanCloseLastDockable = false,
        };

        var centerDock = new ProportionalDock
        {
            Id = "dock-center",
            Title = "Center",
            Proportion = 0.56,
            Orientation = Orientation.Vertical,
            VisibleDockables = CreateList<IDockable>(
                documentDock,
                CreateProportionalDockSplitter(),
                bottomDock),
        };

        var rightDock = new ToolDock
        {
            Id = "dock-right",
            Title = "Right",
            Proportion = 0.24,
            VisibleDockables = CreateList<IDockable>(inspector),
            ActiveDockable = inspector,
            DefaultDockable = inspector,
            CanCloseLastDockable = false,
        };

        var workspace = new ProportionalDock
        {
            Id = "dock-workspace",
            Title = "Workspace",
            Orientation = Orientation.Horizontal,
            VisibleDockables = CreateList<IDockable>(
                leftDock,
                CreateProportionalDockSplitter(),
                centerDock,
                CreateProportionalDockSplitter(),
                rightDock),
        };

        var root = CreateRootDock();
        root.Id = "root";
        root.Title = "Root";
        root.IsFocusableRoot = true;
        root.VisibleDockables = CreateList<IDockable>(workspace);
        root.ActiveDockable = workspace;
        root.DefaultDockable = workspace;

        return root;
    }

    private Document CreateDocumentDockable(string panelId)
    {
        var descriptor = panelRegistry_.GetRequired(panelId);
        EnsureKind(descriptor, PanelKind.Document);

        return new Document
        {
            Id = descriptor.Id,
            Title = descriptor.Title,
            Context = descriptor.CreateContent(),
            CanClose = false,
            CanFloat = true,
            CanDrag = true,
            CanDrop = true,
        };
    }

    private Tool CreateToolDockable(string panelId)
    {
        var descriptor = panelRegistry_.GetRequired(panelId);
        EnsureKind(descriptor, PanelKind.Tool);

        return new Tool
        {
            Id = descriptor.Id,
            Title = descriptor.Title,
            Context = descriptor.CreateContent(),
            CanClose = false,
            CanFloat = true,
            CanDrag = true,
            CanDrop = true,
            CanPin = true,
        };
    }

    private static void EnsureKind(PanelDescriptor descriptor, PanelKind expectedKind)
    {
        if (descriptor.Kind != expectedKind)
        {
            throw new InvalidOperationException(
                $"Panel '{descriptor.Id}' must be registered as {expectedKind}.");
        }
    }
}
