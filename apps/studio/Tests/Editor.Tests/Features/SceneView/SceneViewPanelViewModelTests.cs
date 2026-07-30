using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Editor.Viewports;
using Asharia.Editor.Worlds.Snapshots;
using Editor.Core.Abstractions;
using Editor.Core.Models.Viewports;
using Editor.Core.Services;
using Editor.Features.SceneView.ViewModels;
using Asharia.Studio.Application.Selection;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPanelViewModelTests
{
    [Fact]
    public void Scene_view_exposes_stable_viewport_id_and_initial_snapshots()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());

        Assert.Equal("scene-view/main", viewModel.ViewportId.Value);
        Assert.Null(viewModel.CompositionCapabilities);
        Assert.Null(viewModel.NativePresent);
    }

    [Fact]
    public void Update_composition_capabilities_stores_snapshot_and_notifies()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var changedProperties = new List<string>();
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);
        var snapshot = new ViewportCompositionCapabilitiesSnapshot(
            viewModel.ViewportId,
            ViewportCompositionStatus.Supported,
            deviceLuid: "0011223344556677",
            deviceUuid: "8899aabbccddeeff0011223344556677",
            imageHandleTypes: ["VulkanOpaqueNtHandle"],
            semaphoreHandleTypes: ["VulkanOpaqueNtHandle"],
            synchronizationCapabilities: ["Semaphores"],
            "Avalonia composition GPU interop supports Vulkan opaque NT images and semaphores.",
            DateTimeOffset.UnixEpoch);

        viewModel.UpdateCompositionCapabilities(snapshot);

        Assert.Same(snapshot, viewModel.CompositionCapabilities);
        Assert.Contains(nameof(SceneViewPanelViewModel.CompositionCapabilities), changedProperties);
    }

    [Fact]
    public void Update_composition_capabilities_rejects_mismatched_viewport()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var snapshot = new ViewportCompositionCapabilitiesSnapshot(
            new ViewportId("scene-view/other"),
            ViewportCompositionStatus.Supported,
            deviceLuid: null,
            deviceUuid: null,
            imageHandleTypes: [],
            semaphoreHandleTypes: [],
            synchronizationCapabilities: [],
            "supported",
            DateTimeOffset.UnixEpoch);

        Assert.Throws<ArgumentException>(() => viewModel.UpdateCompositionCapabilities(snapshot));
    }

    [Fact]
    public void Update_composition_capabilities_clears_stale_native_present_status()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var changedProperties = new List<string>();
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);
        viewModel.UpdateNativePresent(
            new ViewportNativePresentSnapshot(
                viewModel.ViewportId,
                new ViewportExtent(640, 360, renderScale: 1),
                new ViewportExtent(640, 360, renderScale: 1),
                "B8G8R8A8_UNORM",
                "SrgbNonlinear",
                frameIndex: 2UL,
                ViewportNativePresentStatus.Success,
                "Presented native Vulkan viewport frame.",
                DateTimeOffset.UnixEpoch));
        changedProperties.Clear();
        var compositionSnapshot = new ViewportCompositionCapabilitiesSnapshot(
            viewModel.ViewportId,
            ViewportCompositionStatus.GpuInteropUnavailable,
            deviceLuid: null,
            deviceUuid: null,
            imageHandleTypes: [],
            semaphoreHandleTypes: [],
            synchronizationCapabilities: [],
            "Avalonia composition GPU interop is unavailable.",
            DateTimeOffset.UnixEpoch);

        viewModel.UpdateCompositionCapabilities(compositionSnapshot);

        Assert.Null(viewModel.NativePresent);
        Assert.Same(compositionSnapshot, viewModel.CompositionCapabilities);
        Assert.Contains(nameof(SceneViewPanelViewModel.NativePresent), changedProperties);
    }

    [Fact]
    public void Update_native_present_stores_snapshot_and_notifies()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var changedProperties = new List<string>();
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName ?? string.Empty);
        var snapshot = new ViewportNativePresentSnapshot(
            viewModel.ViewportId,
            new ViewportExtent(640, 360, renderScale: 1),
            new ViewportExtent(640, 360, renderScale: 1),
            "B8G8R8A8_UNORM",
            "SrgbNonlinear",
            frameIndex: 2UL,
            ViewportNativePresentStatus.Success,
            "Presented native Vulkan viewport frame.",
            DateTimeOffset.UnixEpoch);

        viewModel.UpdateNativePresent(snapshot);

        Assert.Same(snapshot, viewModel.NativePresent);
        Assert.Contains(nameof(SceneViewPanelViewModel.NativePresent), changedProperties);
    }

    [Fact]
    public void Update_native_present_publishes_problem_diagnostic_for_failed_native_viewport_once()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService(), diagnostics);
        var firstFailure = CreateNativePresentSnapshot(
            viewModel,
            ViewportNativePresentStatus.RenderFailed,
            "Native present failed.",
            frameIndex: 2UL);
        var repeatedFailure = CreateNativePresentSnapshot(
            viewModel,
            ViewportNativePresentStatus.RenderFailed,
            "Native present failed.",
            frameIndex: 3UL);

        viewModel.UpdateNativePresent(firstFailure);
        viewModel.UpdateNativePresent(repeatedFailure);

        var record = Assert.Single(diagnostics.GetProblemDiagnostics());
        Assert.Equal(EditorDiagnosticSeverity.Error, record.Severity);
        Assert.Equal(EditorDiagnosticChannel.Problem, record.Channel);
        Assert.Equal("scene-view", record.Source);
        Assert.Equal("native-viewport", record.Category);
        Assert.Equal("Native present failed.", record.Message);
    }

    [Fact]
    public void Update_native_present_does_not_publish_success_and_resets_failure_diagnostic_deduplication()
    {
        var diagnostics = new EditorDiagnosticService();
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService(), diagnostics);
        var success = CreateNativePresentSnapshot(
            viewModel,
            ViewportNativePresentStatus.Success,
            "Presented native Vulkan viewport frame.",
            frameIndex: 1UL);
        var failure = CreateNativePresentSnapshot(
            viewModel,
            ViewportNativePresentStatus.UnsupportedCompositionInterop,
            "Avalonia composition GPU interop is unsupported.",
            frameIndex: 2UL);

        viewModel.UpdateNativePresent(success);
        viewModel.UpdateNativePresent(failure);
        viewModel.UpdateNativePresent(failure);
        viewModel.UpdateNativePresent(success);
        viewModel.UpdateNativePresent(failure);

        var records = diagnostics.GetProblemDiagnostics();
        Assert.Equal(2, records.Count);
        Assert.All(records, record =>
        {
            Assert.Equal(EditorDiagnosticSeverity.Warning, record.Severity);
            Assert.Equal("scene-view", record.Source);
            Assert.Equal("native-viewport", record.Category);
        });
    }

    [Fact]
    public void Update_native_present_rejects_mismatched_viewport()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var snapshot = new ViewportNativePresentSnapshot(
            new ViewportId("scene-view/other"),
            new ViewportExtent(640, 360, renderScale: 1),
            actualExtent: null,
            formatName: "Unknown",
            colorSpace: "Unknown",
            frameIndex: 0UL,
            ViewportNativePresentStatus.RenderFailed,
            "failed",
            DateTimeOffset.UnixEpoch);

        Assert.Throws<ArgumentException>(() => viewModel.UpdateNativePresent(snapshot));
    }

    [Fact]
    public void Scene_view_does_not_join_the_periodic_panel_frame_scheduler()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());

        Assert.DoesNotContain(
            typeof(IEditorPanelFrameUpdateSink),
            viewModel.GetType().GetInterfaces());
    }

    [Fact]
    public void Scene_view_reads_shared_scene_availability_and_revision()
    {
        var scenes = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        var viewModel = new SceneViewPanelViewModel(
            new EditorSelectionService(),
            sceneSnapshots: scenes);

        Assert.Equal((false, 0UL), viewModel.GetSceneRenderState());

        scenes.ReplaceSnapshot(new SceneSnapshot(
            "scene:minimal",
            "Untitled Scene",
            7,
            [new SceneObjectSnapshot("scene:minimal", "Untitled Scene", "scene")]));

        Assert.Equal((true, 7UL), viewModel.GetSceneRenderState());
    }

    [Fact]
    public void Scene_change_requests_one_on_demand_render()
    {
        var scenes = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        using var viewModel = new SceneViewPanelViewModel(
            new EditorSelectionService(),
            sceneSnapshots: scenes);
        var renderRequests = 0;
        viewModel.RenderRequested += (_, _) => renderRequests++;

        scenes.ReplaceSnapshot(new SceneSnapshot(
            "scene:minimal",
            "Untitled Scene",
            1,
            [new SceneObjectSnapshot("scene:minimal", "Untitled Scene", "scene")]));

        Assert.Equal(1, renderRequests);
    }

    [Fact]
    public void Dispose_stops_scene_change_render_requests()
    {
        var scenes = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        var viewModel = new SceneViewPanelViewModel(
            new EditorSelectionService(),
            sceneSnapshots: scenes);
        var renderRequests = 0;
        viewModel.RenderRequested += (_, _) => renderRequests++;

        viewModel.Dispose();
        scenes.ReplaceSnapshot(new SceneSnapshot(
            "scene:minimal",
            "Untitled Scene",
            1,
            [new SceneObjectSnapshot("scene:minimal", "Untitled Scene", "scene")]));

        Assert.Equal(0, renderRequests);
    }

    [Fact]
    public void Scene_change_marshals_render_request_to_ui_dispatcher()
    {
        var scenes = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        using var viewModel = new SceneViewPanelViewModel(
            new EditorSelectionService(),
            diagnostics: null,
            sceneSnapshots: scenes,
            uiDispatcher: dispatcher);
        var renderRequests = 0;
        viewModel.RenderRequested += (_, _) => renderRequests++;

        scenes.ReplaceSnapshot(new SceneSnapshot(
            "scene:minimal",
            "Untitled Scene",
            1,
            [new SceneObjectSnapshot("scene:minimal", "Untitled Scene", "scene")]));

        Assert.Equal(0, renderRequests);
        var request = Assert.Single(dispatcher.PostedActions);
        request();
        Assert.Equal(1, renderRequests);
    }

    private static ViewportNativePresentSnapshot CreateNativePresentSnapshot(
        SceneViewPanelViewModel viewModel,
        ViewportNativePresentStatus status,
        string message,
        ulong frameIndex)
    {
        return new ViewportNativePresentSnapshot(
            viewModel.ViewportId,
            new ViewportExtent(640, 360, renderScale: 1),
            new ViewportExtent(640, 360, renderScale: 1),
            "B8G8R8A8_UNORM",
            "SrgbNonlinear",
            frameIndex,
            status,
            message,
            DateTimeOffset.UnixEpoch);
    }

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        public List<Action> PostedActions { get; } = [];

        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            PostedActions.Add(action);
        }
    }
}
