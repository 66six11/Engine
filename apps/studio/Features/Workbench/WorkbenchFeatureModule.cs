using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Problems.ViewModels;
using Editor.Features.SceneView.ViewModels;
using Editor.Shell.Icons;

namespace Editor.Features.Workbench;

public sealed class WorkbenchFeatureModule : IEditorFeatureModule
{
    private readonly IEditorSelectionService selectionService_;
    private readonly ISceneSnapshotProvider sceneSnapshotProvider_;

    public WorkbenchFeatureModule(IEditorSelectionService selectionService)
        : this(selectionService, CreateDefaultSceneSnapshotProvider())
    {
    }

    internal WorkbenchFeatureModule(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        selectionService_ = selectionService;
        sceneSnapshotProvider_ = sceneSnapshotProvider;
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
        actions.Register(new WorkbenchActionDescriptor(
            "workbench.commandPalette.open",
            "Command Palette",
            WorkbenchActionKind.OpenCommandPalette,
            "Tools/Command Palette",
            IconKey: EditorIconKey.UiSearch,
            Category: "Tools",
            DefaultShortcut: "Ctrl+Shift+P",
            SearchText: "command palette launcher"));
        actions.Register(new WorkbenchActionDescriptor(
            "workbench.about.open",
            "About",
            WorkbenchActionKind.OpenAboutDialog,
            "Help/About",
            Category: "Help",
            SearchText: "about studio version information"));

        foreach (var descriptor in CreatePanelDescriptors())
        {
            actions.Register(new WorkbenchActionDescriptor(
                $"workbench.panel.{descriptor.Id}",
                descriptor.Title,
                WorkbenchActionKind.OpenPanel,
                descriptor.MenuPath,
                TargetId: descriptor.Id,
                IconKey: descriptor.IconKey,
                Category: "Window",
                SearchText: CommandSearchTextForPanel(descriptor.Id)));
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
                () => new HierarchyPanelViewModel(selectionService_, sceneSnapshotProvider_),
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
                () => new InspectorPanelViewModel(selectionService_, sceneSnapshotProvider_),
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

    private static string? CommandSearchTextForPanel(string panelId)
    {
        return panelId switch
        {
            "scene-view" => "viewport document",
            "hierarchy" => "scene tree outliner",
            "inspector" => "properties selection",
            "console" => "log output diagnostics",
            "problems" => "validation diagnostics",
            _ => null,
        };
    }

    private static ISceneSnapshotProvider CreateDefaultSceneSnapshotProvider()
    {
        return new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:main",
            "Main Scene",
            1,
            [
                new SceneObjectSnapshot("scene:main", "Main Scene", "scene"),
                new SceneObjectSnapshot("scene:main/camera", "Main Camera", "camera", parentId: "scene:main"),
                new SceneObjectSnapshot("scene:main/key-light", "Key Light", "light", parentId: "scene:main"),
                new SceneObjectSnapshot(
                    "scene:main/cube",
                    "Demo Cube",
                    "mesh",
                    parentId: "scene:main",
                    properties:
                    [
                        new SceneObjectPropertySnapshot("mesh", "Mesh", "Primitive Cube"),
                        new SceneObjectPropertySnapshot("triangles", "Triangles", "12", SceneObjectPropertyValueKind.Count),
                    ]),
                new SceneObjectSnapshot("scene:main/cube/renderer", "Mesh Renderer", "component", parentId: "scene:main/cube"),
                new SceneObjectSnapshot("scene:main/physics-volume", "Physics Volume", "volume", parentId: "scene:main"),
            ]));
    }
}
