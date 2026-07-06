using System;
using System.Collections.Generic;
using Editor.Core.Models.Viewports;
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
}
