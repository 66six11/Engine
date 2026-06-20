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
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Features.Workbench;

public sealed class WorkbenchFeatureModuleTests
{
    [Fact]
    public void RegisterPanels_registers_stable_workbench_panel_descriptors()
    {
        var registry = new PanelRegistry();

        new WorkbenchFeatureModule(new EditorSelectionService()).RegisterPanels(registry);

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

        new WorkbenchFeatureModule(new EditorSelectionService()).RegisterActions(registry);

        Assert.Equal(
            [
                new WorkbenchActionDescriptor(
                    "workbench.commandPalette.open",
                    "Command Palette",
                    WorkbenchActionKind.OpenCommandPalette,
                    "Tools/Command Palette",
                    IconKey: EditorIconKey.UiSearch,
                    Category: "Tools",
                    DefaultShortcut: "Ctrl+Shift+P",
                    SearchText: "command palette launcher"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.scene-view",
                    "Scene View",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Scene View",
                    TargetId: "scene-view",
                    IconKey: EditorIconKey.PanelSceneView,
                    Category: "Window",
                    SearchText: "viewport document"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.hierarchy",
                    "Hierarchy",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Hierarchy",
                    TargetId: "hierarchy",
                    IconKey: EditorIconKey.PanelHierarchy,
                    Category: "Window",
                    SearchText: "scene tree outliner"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.inspector",
                    "Inspector",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Inspector",
                    TargetId: "inspector",
                    IconKey: EditorIconKey.PanelInspector,
                    Category: "Window",
                    SearchText: "properties selection"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.console",
                    "Console",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Console",
                    TargetId: "console",
                    IconKey: EditorIconKey.PanelConsole,
                    Category: "Window",
                    SearchText: "log output diagnostics"),
                new WorkbenchActionDescriptor(
                    "workbench.panel.problems",
                    "Problems",
                    WorkbenchActionKind.OpenPanel,
                    "Window/Panels/Problems",
                    TargetId: "problems",
                    IconKey: EditorIconKey.PanelProblems,
                    Category: "Window",
                    SearchText: "validation diagnostics"),
            ],
            registry.GetAll().ToArray());
    }

    [Fact]
    public void RegisterPanels_injects_shared_selection_and_scene_snapshot_provider_into_selection_panels()
    {
        var registry = new PanelRegistry();
        var selectionService = new EditorSelectionService();
        new WorkbenchFeatureModule(selectionService).RegisterPanels(registry);
        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            registry.GetRequired("hierarchy").CreateContent());
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            registry.GetRequired("inspector").CreateContent());

        var cube = hierarchy.Nodes.Single(node => node.Id == "scene:main/cube");
        hierarchy.SelectedNode = cube;

        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
        Assert.Contains(
            inspector.Document?.Sections.SelectMany(section => section.Properties) ?? [],
            property => property.Name == "Id" && property.Value == "scene:main/cube");
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
