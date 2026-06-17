using System.Linq;
using Editor.Core.Models;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Problems.ViewModels;
using Editor.Features.SceneView.ViewModels;
using Editor.Features.Workbench;
using Editor.Shell.Docking;
using Editor.Shell.Commands;
using Editor.Shell.Icons;
using Xunit;

namespace Editor.Tests.Features.Workbench;

public sealed class WorkbenchFeatureModuleTests
{
    [Fact]
    public void RegisterPanels_registers_stable_workbench_panel_descriptors()
    {
        var registry = new PanelRegistry();

        new WorkbenchFeatureModule().RegisterPanels(registry);

        var descriptors = registry.GetAll().ToArray();
        Assert.Equal(
            [
                new PanelDescriptorSnapshot(
                    "scene-view",
                    "Scene View",
                    PanelKind.Document,
                    DockArea.Center,
                    "Window/Panels/Scene View",
                    DockContentCachePolicy.KeepAlive,
                    EditorIconKey.PanelSceneView,
                    "DOC",
                    "custom viewport shell",
                    "live"),
                new PanelDescriptorSnapshot(
                    "hierarchy",
                    "Hierarchy",
                    PanelKind.Tool,
                    DockArea.Left,
                    "Window/Panels/Hierarchy",
                    DockContentCachePolicy.KeepAlive,
                    EditorIconKey.PanelHierarchy,
                    "LEFT",
                    "selection source",
                    "tool"),
                new PanelDescriptorSnapshot(
                    "inspector",
                    "Inspector",
                    PanelKind.Tool,
                    DockArea.Right,
                    "Window/Panels/Inspector",
                    DockContentCachePolicy.KeepAlive,
                    EditorIconKey.PanelInspector,
                    "RIGHT",
                    "context target",
                    "tool"),
                new PanelDescriptorSnapshot(
                    "console",
                    "Console",
                    PanelKind.Tool,
                    DockArea.Bottom,
                    "Window/Panels/Console",
                    DockContentCachePolicy.KeepAlive,
                    EditorIconKey.PanelConsole,
                    "BOTTOM",
                    "runtime log stream",
                    "idle"),
                new PanelDescriptorSnapshot(
                    "problems",
                    "Problems",
                    PanelKind.Tool,
                    DockArea.Bottom,
                    "Window/Panels/Problems",
                    DockContentCachePolicy.KeepAlive,
                    EditorIconKey.PanelProblems,
                    "BOTTOM",
                    "validation queue",
                    "0"),
            ],
            descriptors.Select(CreateSnapshot).ToArray());

        Assert.IsType<SceneViewPanelViewModel>(descriptors[0].CreateContent());
        Assert.IsType<HierarchyPanelViewModel>(descriptors[1].CreateContent());
        Assert.IsType<InspectorPanelViewModel>(descriptors[2].CreateContent());
        Assert.IsType<ConsolePanelViewModel>(descriptors[3].CreateContent());
        Assert.IsType<ProblemsPanelViewModel>(descriptors[4].CreateContent());
    }

    [Fact]
    public void RegisterActions_registers_stable_workbench_panel_actions()
    {
        var registry = new WorkbenchActionRegistry();

        new WorkbenchFeatureModule().RegisterActions(registry);

        Assert.Equal(
            [
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    IconKey: EditorIconKey.PanelSceneView),
                new WorkbenchActionDescriptor(
                    "workbench.panel.hierarchy",
                    "Hierarchy",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Hierarchy",
                    TargetId: "hierarchy",
                    IconKey: EditorIconKey.PanelHierarchy),
                new WorkbenchActionDescriptor(
                    "workbench.panel.inspector",
                    "Inspector",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Inspector",
                    TargetId: "inspector",
                    IconKey: EditorIconKey.PanelInspector),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    IconKey: EditorIconKey.PanelConsole),
                new WorkbenchActionDescriptor(
                    "workbench.panel.problems",
                    "Problems",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Problems",
                    TargetId: "problems",
                    IconKey: EditorIconKey.PanelProblems),
            ],
            registry.GetAll().ToArray());
    }

    private static PanelDescriptorSnapshot CreateSnapshot(PanelDescriptor descriptor)
    {
        return new PanelDescriptorSnapshot(
            descriptor.Id,
            descriptor.Title,
            descriptor.Kind,
            descriptor.DefaultArea,
            descriptor.MenuPath,
            descriptor.CachePolicy,
            descriptor.IconKey,
            descriptor.Tag,
            descriptor.TitleDetail,
            descriptor.StatusText);
    }

    private sealed record PanelDescriptorSnapshot(
        string Id,
        string Title,
        PanelKind Kind,
        DockArea Area,
        string MenuPath,
        DockContentCachePolicy CachePolicy,
        string? IconKey,
        string? Tag,
        string? TitleDetail,
        string? StatusText);
}
