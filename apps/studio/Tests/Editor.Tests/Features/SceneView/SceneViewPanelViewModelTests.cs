using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Viewports;
using Editor.Core.Abstractions;
using Editor.Core.Models.Panels;
using Editor.Core.Models.Viewports;
using Editor.Core.Services;
using Editor.Features.SceneView.ViewModels;
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Features.SceneView;

public sealed class SceneViewPanelViewModelTests
{
    [Fact]
    public void Scene_view_exposes_stable_viewport_id_and_initial_composition_status()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());

        Assert.Equal("scene-view/main", viewModel.ViewportId.Value);
        Assert.Null(viewModel.CompositionCapabilities);
        Assert.Equal("composition pending", viewModel.ViewportStatusText);
        Assert.Equal(
            "Scene View is waiting for Avalonia composition GPU interop probing.",
            viewModel.ViewportStateMessage);
    }

    [Fact]
    public void Update_composition_capabilities_projects_status_and_notifies_dependents()
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
        Assert.Equal("Supported", viewModel.ViewportStatusText);
        Assert.Equal(snapshot.Message, viewModel.ViewportStateMessage);
        Assert.Contains(nameof(SceneViewPanelViewModel.CompositionCapabilities), changedProperties);
        Assert.Contains(nameof(SceneViewPanelViewModel.ViewportStatusText), changedProperties);
        Assert.Contains(nameof(SceneViewPanelViewModel.ViewportStateMessage), changedProperties);
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
        Assert.Equal("GpuInteropUnavailable", viewModel.ViewportStatusText);
        Assert.Equal("Avalonia composition GPU interop is unavailable.", viewModel.ViewportStateMessage);
        Assert.Contains(nameof(SceneViewPanelViewModel.NativePresent), changedProperties);
        Assert.Contains(nameof(SceneViewPanelViewModel.ViewportStatusText), changedProperties);
        Assert.Contains(nameof(SceneViewPanelViewModel.ViewportStateMessage), changedProperties);
    }

    [Fact]
    public void Update_native_present_projects_status_and_notifies_dependents()
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
        Assert.Equal("Success", viewModel.ViewportStatusText);
        Assert.Equal("Presented native Vulkan viewport frame.", viewModel.ViewportStateMessage);
        Assert.Contains(nameof(SceneViewPanelViewModel.NativePresent), changedProperties);
        Assert.Contains(nameof(SceneViewPanelViewModel.ViewportStatusText), changedProperties);
        Assert.Contains(nameof(SceneViewPanelViewModel.ViewportStateMessage), changedProperties);
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
    public void Scene_view_requests_active_frame_updates_for_native_viewport()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var frameSink = Assert.IsAssignableFrom<IEditorPanelFrameUpdateSink>(viewModel);

        Assert.Equal(EditorPanelFrameUpdateMode.Active, frameSink.FrameUpdateRequest.Mode);
        Assert.Equal(30d, frameSink.FrameUpdateRequest.TargetFramesPerSecond);
    }

    [Fact]
    public void Scene_view_publishes_panel_frame_context_to_view_subscribers()
    {
        var viewModel = new SceneViewPanelViewModel(new EditorSelectionService());
        var frameSink = Assert.IsAssignableFrom<IEditorPanelFrameUpdateSink>(viewModel);
        var context = new EditorPanelFrameContext(
            new EditorPanelLifecycleContext(
                "scene-view",
                "Scene View",
                EditorDockArea.Center,
                IsFloatingWorkspace: false),
            DateTimeOffset.UnixEpoch,
            TimeSpan.FromMilliseconds(16),
            sequence: 3);
        EditorPanelFrameContext? receivedContext = null;
        viewModel.FrameRequested += (_, frameContext) => receivedContext = frameContext;

        frameSink.OnEditorPanelFrame(context);

        Assert.Same(context, receivedContext);
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
}
