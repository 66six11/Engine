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
    private readonly IEditorSelectionService selectionService_;

    public WorkbenchFeatureModule(IEditorSelectionService selectionService)
    {
        selectionService_ = selectionService;
    }

    public void RegisterPanels(IPanelRegistry panels)
    {
        foreach (var descriptor in CreatePanelDescriptors())
        {
            panels.Register(descriptor);
        }
    }

    public void RegisterActions(IWorkbenchActionRegistry actions)
    {
        foreach (var descriptor in CreatePanelDescriptors())
        {
            actions.Register(new WorkbenchActionDescriptor(
                $"workbench.panel.{descriptor.Id}",
                descriptor.Title,
                WorkbenchActionKind.OpenPanel,
                descriptor.MenuPath,
                TargetId: descriptor.Id,
                IconKey: descriptor.IconKey));
        }
    }

    private PanelDescriptor[] CreatePanelDescriptors()
    {
        return
        [
            new PanelDescriptor(
                "scene-view",
                "Scene View",
                PanelKind.Document,
                DockArea.Center,
                "Window/Panels/Scene View",
                DockContentCachePolicy.KeepAlive,
                () => new SceneViewPanelViewModel(selectionService_),
                IconKey: "studio.scene-view",
                Tag: "DOC",
                TitleDetail: "custom viewport shell",
                StatusText: "live"),
            new PanelDescriptor(
                "hierarchy",
                "Hierarchy",
                PanelKind.Tool,
                DockArea.Left,
                "Window/Panels/Hierarchy",
                DockContentCachePolicy.KeepAlive,
                () => new HierarchyPanelViewModel(selectionService_),
                IconKey: "studio.hierarchy",
                Tag: "LEFT",
                TitleDetail: "selection source",
                StatusText: "tool"),

            new PanelDescriptor(
                "inspector",
                "Inspector",
                PanelKind.Tool,
                DockArea.Right,
                "Window/Panels/Inspector",
                DockContentCachePolicy.KeepAlive,
                () => new InspectorPanelViewModel(selectionService_),
                IconKey: "studio.inspector",
                Tag: "RIGHT",
                TitleDetail: "context target",
                StatusText: "tool"),

            new PanelDescriptor(
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
                StatusText: "idle"),

            new PanelDescriptor(
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
                StatusText: "0"),
        ];
    }
}
