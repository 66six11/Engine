using System;
using Editor.Core.Abstractions;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.FrameDebug;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Scene;
using Editor.Core.Models.Workbench;
using Editor.Core.Services;
using Editor.Features.Console.ViewModels;
using Editor.Features.FrameDebugger;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Problems.ViewModels;
using Editor.Features.SceneView.ViewModels;
using Editor.Features.UiStyle;
using Editor.Shell.CodeFirstUI;
using Editor.UI.Icons;
using Editor.Shell.Services;

namespace Editor.Features.Workbench;

public sealed class WorkbenchFeatureModule : IEditorFeatureModule
{
    private readonly IEditorSelectionService selectionService_;
    private readonly ISceneSnapshotProvider sceneSnapshotProvider_;
    private readonly IFrameDebuggerSnapshotProvider frameDebuggerSnapshotProvider_;
    private readonly IEditorDiagnosticService diagnostics_;
    private readonly IEditorUiDispatcher uiDispatcher_;

    public WorkbenchFeatureModule(IEditorSelectionService selectionService)
        : this(
            selectionService,
            new EditorDiagnosticService(),
            CreateDefaultSceneSnapshotProvider(),
            CreateDefaultFrameDebuggerSnapshotProvider())
    {
    }

    public WorkbenchFeatureModule(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics)
        : this(
            selectionService,
            diagnostics,
            CreateDefaultSceneSnapshotProvider(),
            CreateDefaultFrameDebuggerSnapshotProvider())
    {
    }

    internal WorkbenchFeatureModule(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider)
        : this(selectionService, new EditorDiagnosticService(), sceneSnapshotProvider)
    {
    }

    internal WorkbenchFeatureModule(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics,
        ISceneSnapshotProvider sceneSnapshotProvider,
        IEditorUiDispatcher? uiDispatcher = null)
        : this(
            selectionService,
            diagnostics,
            sceneSnapshotProvider,
            CreateDefaultFrameDebuggerSnapshotProvider(),
            uiDispatcher)
    {
    }

    internal WorkbenchFeatureModule(
        IEditorSelectionService selectionService,
        IEditorDiagnosticService diagnostics,
        ISceneSnapshotProvider sceneSnapshotProvider,
        IFrameDebuggerSnapshotProvider frameDebuggerSnapshotProvider,
        IEditorUiDispatcher? uiDispatcher = null)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(diagnostics);
        ArgumentNullException.ThrowIfNull(sceneSnapshotProvider);
        ArgumentNullException.ThrowIfNull(frameDebuggerSnapshotProvider);

        selectionService_ = selectionService;
        diagnostics_ = diagnostics;
        sceneSnapshotProvider_ = sceneSnapshotProvider;
        frameDebuggerSnapshotProvider_ = frameDebuggerSnapshotProvider;
        uiDispatcher_ = uiDispatcher ?? new AvaloniaEditorUiDispatcher();
    }

    public EditorExtensionId Id { get; } = new("studio.workbench");

    public void Declare(IEditorContributionBuilder builder)
    {
        ArgumentNullException.ThrowIfNull(builder);

        builder.AddSceneProvider(new SceneProviderDescriptor(
            "workbench.scene.fixture",
            EditorProviderRoles.ActiveScene,
            () => sceneSnapshotProvider_));

        var panelDescriptors = CreatePanelDescriptors();
        foreach (var descriptor in panelDescriptors)
        {
            builder.AddPanel(descriptor);
        }

        builder.AddAction(new WorkbenchActionDescriptor(
            "workbench.commandPalette.open",
            "Command Palette",
            WorkbenchActionKind.OpenCommandPalette,
            "Tools/Command Palette",
            IconKey: EditorIconKey.UiSearch,
            Category: "Tools",
            DefaultShortcut: "Ctrl+Shift+P",
            SearchText: "command palette launcher"));
        builder.AddAction(new WorkbenchActionDescriptor(
            "workbench.about.open",
            "About",
            WorkbenchActionKind.OpenAboutDialog,
            "Help/About",
            Category: "Help",
            SearchText: "about studio version information"));

        foreach (var descriptor in panelDescriptors)
        {
            builder.AddAction(new WorkbenchActionDescriptor(
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
                TitleDetail: "viewport deferred",
                StatusText: "deferred"),
            new PanelDescriptor(
                "hierarchy",
                "Hierarchy",
                PanelKind.Tool,
                DockArea.Left,
                "Window/Panels/Hierarchy",
                DockContentCachePolicy.KeepAlive,
                () => new HierarchyPanelViewModel(selectionService_, sceneSnapshotProvider_, uiDispatcher_),
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
                () => new InspectorPanelViewModel(selectionService_, sceneSnapshotProvider_, uiDispatcher_),
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
                () => new ConsolePanelViewModel(diagnostics_, uiDispatcher_),
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
                () => new ProblemsPanelViewModel(diagnostics_, uiDispatcher_),
                IconKey: "studio.problems",
                Tag: "BOTTOM",
                TitleDetail: "validation queue",
                StatusText: "0"),

            new PanelDescriptor(
                "frame-debugger",
                "Frame Debugger",
                PanelKind.Tool,
                DockArea.Right,
                "Window/Panels/Frame Debugger",
                DockContentCachePolicy.KeepAlive,
                () => new CodeFirstPanelHostViewModel(
                    new FrameDebuggerPanel(frameDebuggerSnapshotProvider_, diagnostics_)),
                IconKey: EditorIconKey.PanelFrameDebugger,
                Tag: "DEBUG",
                TitleDetail: "read-only snapshot",
                StatusText: "snapshot"),

            new PanelDescriptor(
                "ui-style",
                "UI Style",
                PanelKind.Tool,
                DockArea.Center,
                "Window/Panels/UI Style",
                DockContentCachePolicy.KeepAlive,
                () => new CodeFirstPanelHostViewModel(new UiStylePanel()),
                IconKey: EditorIconKey.PanelUiStyle,
                Tag: "STYLE",
                TitleDetail: "code-first component guide",
                StatusText: "samples"),
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
            "frame-debugger" => "frame debugger render graph pass snapshot",
            "ui-style" => "code-first ui style guide samples",
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

    private static IFrameDebuggerSnapshotProvider CreateDefaultFrameDebuggerSnapshotProvider()
    {
        var capture = new FrameDebugCaptureSnapshot(
            "capture:fixture",
            12,
            42UL,
            "Scene",
            1280,
            720,
            DateTimeOffset.Parse("2026-07-04T10:30:00Z"));
        var sceneColor = new FrameDebugPassSnapshot(
            "pass:scene-color",
            0,
            0,
            "Scene Color",
            "Raster",
            "BasicRenderView",
            AllowCulling: true,
            HasSideEffects: false,
            CommandCount: 1,
            ImageTransitionCount: 1,
            BufferTransitionCount: 0);
        var postProcess = new FrameDebugPassSnapshot(
            "pass:post-process",
            1,
            1,
            "Post Process",
            "Raster",
            "PostProcessView",
            AllowCulling: false,
            HasSideEffects: true,
            CommandCount: 1,
            ImageTransitionCount: 1,
            BufferTransitionCount: 0);
        var command = new FrameDebugCommandSnapshot(
            "command:scene-color:0",
            sceneColor.Id,
            0,
            0,
            sceneColor.Name,
            "Draw",
            "Draw scene color triangle.");
        var resource = new FrameDebugResourceSnapshot(
            "image:scene-color",
            "Image",
            0,
            "Scene Color",
            "Imported",
            "Rgba8Unorm",
            "1280x720",
            "Undefined",
            "ColorWrite");
        var access = new FrameDebugAccessEdgeSnapshot(
            "access:scene-color:color",
            sceneColor.Id,
            resource.Id,
            sceneColor.Name,
            resource.Name,
            "color",
            "ColorWrite",
            "Fragment");
        var dependency = new FrameDebugDependencyEdgeSnapshot(
            "dependency:scene-color:post-process",
            sceneColor.Id,
            postProcess.Id,
            resource.Id,
            resource.Name,
            "Post Process samples Scene Color.");
        var transition = new FrameDebugTransitionSnapshot(
            "transition:scene-color:before",
            "BeforePass",
            sceneColor.Id,
            resource.Id,
            sceneColor.Name,
            resource.Name,
            "Undefined",
            "ColorWrite");
        var executionEvent = new FrameDebugExecutionEventSnapshot(
            "event:scene-color:0",
            0,
            "Draw",
            sceneColor.Id,
            sceneColor.Name,
            command.Id,
            "Draw scene color triangle.",
            null,
            resource.Id,
            VertexCount: 3,
            IndexCount: 0,
            InstanceCount: 1,
            GroupCountX: 0,
            GroupCountY: 0,
            GroupCountZ: 0);

        return new InMemoryFrameDebuggerSnapshotProvider(new FrameDebuggerSnapshot(
            1,
            FrameDebuggerState.PausedFrameDebug,
            capture,
            passes: [sceneColor, postProcess],
            commands: [command],
            resources: [resource],
            accessEdges: [access],
            dependencyEdges: [dependency],
            transitions: [transition],
            executionEvents: [executionEvent],
            preview: new FrameDebugPreviewSnapshot(
                "NotRequested",
                sceneColor.Id,
                executionEvent.Id,
                "Preview capture is not requested."),
            message: "Fixture frame debugger snapshot."));
    }
}
