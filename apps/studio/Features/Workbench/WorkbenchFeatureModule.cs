using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Problems.ViewModels;
using Editor.Features.SceneView.ViewModels;

namespace Editor.Features.Workbench;

public sealed class WorkbenchFeatureModule : IEditorFeatureModule
{
    public void RegisterPanels(IPanelRegistry panels)
    {
        panels.Register(new PanelDescriptor(
            "scene-view",
            "Scene View",
            PanelKind.Document,
            DockArea.Center,
            "Window/Panels/Scene View",
            DockContentCachePolicy.KeepAlive,
            () => new SceneViewPanelViewModel(),
            IconKey: "studio.scene-view",
            Tag: "DOC",
            TitleDetail: "custom viewport shell",
            StatusText: "live"));

        panels.Register(new PanelDescriptor(
            "hierarchy",
            "Hierarchy",
            PanelKind.Tool,
            DockArea.Left,
            "Window/Panels/Hierarchy",
            DockContentCachePolicy.KeepAlive,
            () => new HierarchyPanelViewModel(),
            IconKey: "studio.hierarchy",
            Tag: "LEFT",
            TitleDetail: "selection source",
            StatusText: "tool"));

        panels.Register(new PanelDescriptor(
            "inspector",
            "Inspector",
            PanelKind.Tool,
            DockArea.Right,
            "Window/Panels/Inspector",
            DockContentCachePolicy.KeepAlive,
            () => new InspectorPanelViewModel(),
            IconKey: "studio.inspector",
            Tag: "RIGHT",
            TitleDetail: "context target",
            StatusText: "tool"));

        panels.Register(new PanelDescriptor(
            "console",
            "Console",
            PanelKind.Tool,
            DockArea.Bottom,
            "Window/Panels/Console",
            DockContentCachePolicy.KeepAlive,
            () => new ConsolePanelViewModel(),
            IconKey: "studio.console",
            Tag: "BOTTOM",
            TitleDetail: "runtime log stream",
            StatusText: "idle"));

        panels.Register(new PanelDescriptor(
            "problems",
            "Problems",
            PanelKind.Tool,
            DockArea.Bottom,
            "Window/Panels/Problems",
            DockContentCachePolicy.KeepAlive,
            () => new ProblemsPanelViewModel(),
            IconKey: "studio.problems",
            Tag: "BOTTOM",
            TitleDetail: "validation queue",
            StatusText: "0"));
    }
}
