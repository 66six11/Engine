using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Editor.Core.Abstractions;
using Editor.Core.CodeFirstUI.Abstractions;
using Editor.Core.CodeFirstUI.Authoring;
using Editor.Core.CodeFirstUI.Models;
using Editor.Core.CodeFirstUI.Validation;
using Editor.Core.Models.Panels;
using Editor.Shell.CodeFirstUI.Hosting;
using Xunit;

namespace Editor.Tests.Shell.CodeFirstUI.Hosting;

public sealed class CodeFirstPanelHostViewModelTests
{
    [Fact]
    public void Attach_creates_enables_and_builds_current_tree()
    {
        var panel = new RecordingCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);

        host.OnPanelAttached(CreateLifecycleContext());

        Assert.Equal(["create:render.frameDebugger", "enable", "gui"], panel.Events);
        Assert.NotNull(host.CurrentTree);
        Assert.Equal("render.frameDebugger", host.CurrentTree.PanelId);
        Assert.Equal("render.frameDebugger/title", host.CurrentTree.Root.Children[0].Id.FullKeyPath);
    }

    [Fact]
    public void Frame_rebuilds_only_when_panel_requests_repaint()
    {
        var panel = new RecordingCodeFirstPanel
        {
            RequestRepaintOnFrame = false,
        };
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext());
        var initialBuildCount = panel.GuiBuildCount;

        host.OnEditorPanelFrame(CreateFrameContext(sequence: 1));

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);

        panel.RequestRepaintOnFrame = true;
        host.OnEditorPanelFrame(CreateFrameContext(sequence: 2));

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
    }

    [Fact]
    public void Request_rebuild_coalesces_multiple_reasons_until_dispatcher_runs()
    {
        var dispatcher = new RecordingEditorUiDispatcher();
        var panel = new RecordingCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel, uiDispatcher: dispatcher);
        host.OnPanelAttached(CreateLifecycleContext());
        dispatcher.RunPending();
        var initialBuildCount = panel.GuiBuildCount;

        host.RequestRebuild(GuiRebuildReason.InputEvent);
        host.RequestRebuild(GuiRebuildReason.FrameTick);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.Equal(1, dispatcher.PendingCount);

        dispatcher.RunPending();

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.True(host.LastRebuildReasons.HasFlag(GuiRebuildReason.InputEvent));
        Assert.True(host.LastRebuildReasons.HasFlag(GuiRebuildReason.FrameTick));
    }

    [Fact]
    public void Input_events_request_input_rebuild_reason()
    {
        var dispatcher = new RecordingEditorUiDispatcher();
        var panel = new InputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel, uiDispatcher: dispatcher);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        dispatcher.RunPending();

        host.CommitText(new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField), "gbuffer");
        dispatcher.RunPending();

        Assert.Equal("gbuffer", panel.FilterText);
        Assert.Equal(GuiRebuildReason.InputEvent, host.LastRebuildReasons);
    }

    [Fact]
    public void Detach_disables_and_dispose_destroys_panel_once()
    {
        var panel = new RecordingCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);

        host.OnPanelAttached(CreateLifecycleContext());
        host.OnPanelDetached(CreateLifecycleContext());
        host.Dispose();
        host.Dispose();

        Assert.Equal(
            ["create:render.frameDebugger", "enable", "gui", "disable", "destroy"],
            panel.Events);
    }

    [Fact]
    public void Frame_update_request_comes_from_panel()
    {
        var panel = new RecordingCodeFirstPanel
        {
            RequestedFrameUpdate = EditorPanelFrameUpdateRequest.Active(30),
        };
        var host = new CodeFirstPanelHostViewModel(panel);

        Assert.Equal(EditorPanelFrameUpdateMode.Active, host.FrameUpdateRequest.Mode);
        Assert.Equal(30, host.FrameUpdateRequest.TargetFramesPerSecond);
    }

    [Fact]
    public void On_gui_exception_preserves_last_valid_tree_and_exposes_error()
    {
        var panel = new FaultingCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("tools.faulting"));
        var previousTree = Assert.IsType<GuiTreeSnapshot>(host.CurrentTree);

        panel.ThrowOnGui = true;
        host.OnPanelActivated(CreateLifecycleContext("tools.faulting"));

        Assert.Same(previousTree, host.CurrentTree);
        Assert.True(host.HasBuildError);
        Assert.False(host.HasValidationErrors);
        Assert.Contains("Injected code-first failure.", host.LastBuildErrorMessage);
        Assert.True(host.LastValidationResult.IsValid);
    }

    [Fact]
    public void Validation_failure_preserves_last_valid_tree_and_exposes_errors()
    {
        var panel = new ValidationFailureCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("tools.validation"));
        var previousTree = Assert.IsType<GuiTreeSnapshot>(host.CurrentTree);

        panel.EmitDuplicateKeys = true;
        host.OnPanelActivated(CreateLifecycleContext("tools.validation"));

        Assert.Same(previousTree, host.CurrentTree);
        Assert.False(host.HasBuildError);
        Assert.True(host.HasValidationErrors);
        var error = Assert.Single(host.LastValidationResult.Errors);
        Assert.Equal(GuiTreeValidationErrorCode.DuplicateKey, error.Code);
        Assert.Contains("Duplicate GUI key", error.Message);
    }

    [Fact]
    public void Select_list_item_updates_state_and_rebuilds_tree()
    {
        var panel = new ListDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SelectListItem(
            new GuiNodeId("ui.style", "layout/catalog/sections", GuiNodeKind.List),
            "buttons");

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.Equal("buttons", panel.SelectedSectionId);
        Assert.True(host.StateStore.TryGetSelectedItem(
            new GuiNodeId("ui.style", "layout/catalog/sections", GuiNodeKind.List),
            out var storedSelection));
        Assert.Equal("buttons", storedSelection);

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        Assert.Equal("Buttons", Assert.Single(preview.Children).Label);
    }

    [Fact]
    public void Selecting_current_list_item_does_not_rebuild_tree()
    {
        var panel = new ListDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SelectListItem(
            new GuiNodeId("ui.style", "layout/catalog/sections", GuiNodeKind.List),
            "overview");

        Assert.Equal("overview", panel.SelectedSectionId);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Select_combo_box_item_updates_state_and_rebuilds_tree()
    {
        var panel = new ComboBoxDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SelectComboBoxItem(
            new GuiNodeId("ui.style", "render-mode", GuiNodeKind.ComboBox),
            "deferred");

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.Equal("deferred", panel.SelectedRenderModeId);
        Assert.True(host.StateStore.TryGetSelectedItem(
            new GuiNodeId("ui.style", "render-mode", GuiNodeKind.ComboBox),
            out var storedSelection));
        Assert.Equal("deferred", storedSelection);

        var comboBox = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(GuiNodeKind.ComboBox, comboBox.Kind);
        Assert.Equal("deferred", comboBox.Payload.SelectedItemId);
    }

    [Fact]
    public void Selecting_current_combo_box_item_does_not_rebuild_tree()
    {
        var panel = new ComboBoxDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SelectComboBoxItem(
            new GuiNodeId("ui.style", "render-mode", GuiNodeKind.ComboBox),
            "forward");

        Assert.Equal("forward", panel.SelectedRenderModeId);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Select_radio_group_item_updates_state_and_rebuilds_tree()
    {
        var panel = new RadioGroupDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SelectRadioGroupItem(
            new GuiNodeId("ui.style", "shading-mode", GuiNodeKind.RadioGroup),
            "wireframe");

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.Equal("wireframe", panel.SelectedShadingModeId);
        Assert.True(host.StateStore.TryGetSelectedItem(
            new GuiNodeId("ui.style", "shading-mode", GuiNodeKind.RadioGroup),
            out var storedSelection));
        Assert.Equal("wireframe", storedSelection);

        var radioGroup = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(GuiNodeKind.RadioGroup, radioGroup.Kind);
        Assert.Equal("wireframe", radioGroup.Payload.SelectedItemId);
    }

    [Fact]
    public void Selecting_current_radio_group_item_does_not_rebuild_tree()
    {
        var panel = new RadioGroupDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SelectRadioGroupItem(
            new GuiNodeId("ui.style", "shading-mode", GuiNodeKind.RadioGroup),
            "lit");

        Assert.Equal("lit", panel.SelectedShadingModeId);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Select_navigation_route_updates_state_and_rebuilds_tree()
    {
        var panel = new NavigationDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));

        host.SelectNavigationRoute(
            new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView),
            "render/debug/frame-debugger");

        Assert.Equal("render/debug/frame-debugger", panel.SelectedRoute);
        Assert.True(host.StateStore.TryGetSelectedRoute(
            new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView),
            out var storedRoute));
        Assert.Equal("render/debug/frame-debugger", storedRoute);

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(GuiNodeKind.NavigationView, navigation.Kind);
        Assert.Equal("render/debug/frame-debugger", navigation.Payload.SelectedRoute);
        Assert.Equal("Frame Debugger", Assert.Single(navigation.Children).Label);
    }

    [Fact]
    public void Navigation_route_expansion_state_survives_route_selection_rebuild()
    {
        var panel = new NavigationDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        var navigationId = new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));

        host.SetNavigationRouteExpanded(navigationId, "overview", isExpanded: false);
        host.SelectNavigationRoute(navigationId, "overview");

        Assert.Equal("overview", panel.SelectedRoute);
        Assert.True(host.StateStore.TryGetNavigationRouteExpanded(
            navigationId,
            "overview",
            out var isExpanded));
        Assert.False(isExpanded);

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(["overview"], navigation.Payload.CollapsedNavigationRoutes);
        Assert.Equal("Overview", Assert.Single(navigation.Children).Label);
    }

    [Fact]
    public void Setting_current_navigation_route_expansion_does_not_rebuild_tree()
    {
        var panel = new NavigationDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        var navigationId = new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));

        host.SetNavigationRouteExpanded(navigationId, "overview", isExpanded: false);
        var buildCount = panel.GuiBuildCount;

        host.SetNavigationRouteExpanded(navigationId, "overview", isExpanded: false);

        Assert.Equal(buildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Selecting_current_navigation_route_does_not_rebuild_tree()
    {
        var panel = new NavigationDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        var navigationId = new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var buildCount = panel.GuiBuildCount;

        host.SelectNavigationRoute(navigationId, "overview");

        Assert.Equal("overview", panel.SelectedRoute);
        Assert.Equal(buildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Resize_split_updates_state_without_immediate_rebuild_and_next_rebuild_uses_ratio()
    {
        var panel = new SplitDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.ResizeSplit(new GuiNodeId("ui.style", "layout", GuiNodeKind.Split), 0.45d);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetSplitRatio(
            new GuiNodeId("ui.style", "layout", GuiNodeKind.Split),
            out var storedRatio));
        Assert.Equal(0.45d, storedRatio);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(0.45d, split.Payload.SplitRatio);
    }

    [Fact]
    public void Set_text_updates_state_without_immediate_rebuild_and_next_rebuild_uses_text()
    {
        var panel = new InputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetText(new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField), "gbuffer");

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetText(
            new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField),
            out var storedText));
        Assert.Equal("gbuffer", storedText);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var textField = host.CurrentTree?.Root.Children[0];
        Assert.Equal("gbuffer", textField?.Payload.TextValue);
        Assert.Equal("gbuffer", panel.FilterText);
    }

    [Fact]
    public void Set_slider_value_updates_state_without_immediate_rebuild_and_next_rebuild_uses_value()
    {
        var panel = new SliderDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetSliderValue(new GuiNodeId("ui.style", "exposure", GuiNodeKind.Slider), 1.25d);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetNumericValue(
            new GuiNodeId("ui.style", "exposure", GuiNodeKind.Slider),
            out var storedValue));
        Assert.Equal(1.25d, storedValue);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var slider = host.CurrentTree?.Root.Children[0];
        Assert.Equal(1.25d, slider?.Payload.NumericValue);
        Assert.Equal(1.25d, panel.Exposure);
    }

    [Fact]
    public void Setting_current_slider_value_does_not_rebuild_tree()
    {
        var panel = new SliderDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetSliderValue(new GuiNodeId("ui.style", "exposure", GuiNodeKind.Slider), 0.5d);

        Assert.Equal(0.5d, panel.Exposure);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Set_color_value_updates_state_without_immediate_rebuild_and_next_rebuild_uses_value()
    {
        var panel = new ColorFieldDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;
        var color = new GuiColorValue(16, 32, 48, 128);

        host.SetColorValue(new GuiNodeId("ui.style", "albedo", GuiNodeKind.ColorField), color);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetColorValue(
            new GuiNodeId("ui.style", "albedo", GuiNodeKind.ColorField),
            out var storedValue));
        Assert.Equal(color, storedValue);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var colorField = host.CurrentTree?.Root.Children[0];
        Assert.Equal(color, colorField?.Payload.ColorValue);
        Assert.Equal(color, panel.Albedo);
    }

    [Fact]
    public void Set_vector3_value_updates_state_without_immediate_rebuild_and_next_rebuild_uses_value()
    {
        var panel = new Vector3FieldDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;
        var position = new GuiVector3Value(1d, 2d, 3d);

        host.SetVector3Value(new GuiNodeId("ui.style", "position", GuiNodeKind.Vector3Field), position);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetVector3Value(
            new GuiNodeId("ui.style", "position", GuiNodeKind.Vector3Field),
            out var storedValue));
        Assert.Equal(position, storedValue);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var vector3Field = host.CurrentTree?.Root.Children[0];
        Assert.Equal(position, vector3Field?.Payload.Vector3Value);
        Assert.Equal(position, panel.Position);
    }

    [Fact]
    public void Set_vector2_value_updates_state_without_immediate_rebuild_and_next_rebuild_uses_value()
    {
        var panel = new Vector2FieldDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;
        var uvScale = new GuiVector2Value(2d, 4d);

        host.SetVector2Value(new GuiNodeId("ui.style", "uv-scale", GuiNodeKind.Vector2Field), uvScale);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetVector2Value(
            new GuiNodeId("ui.style", "uv-scale", GuiNodeKind.Vector2Field),
            out var storedValue));
        Assert.Equal(uvScale, storedValue);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var vector2Field = host.CurrentTree?.Root.Children[0];
        Assert.Equal(uvScale, vector2Field?.Payload.Vector2Value);
        Assert.Equal(uvScale, panel.UvScale);
    }

    [Fact]
    public void Set_vector4_value_updates_state_without_immediate_rebuild_and_next_rebuild_uses_value()
    {
        var panel = new Vector4FieldDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;
        var tilingOffset = new GuiVector4Value(1d, 2d, 3d, 4d);

        host.SetVector4Value(new GuiNodeId("ui.style", "tiling-offset", GuiNodeKind.Vector4Field), tilingOffset);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetVector4Value(
            new GuiNodeId("ui.style", "tiling-offset", GuiNodeKind.Vector4Field),
            out var storedValue));
        Assert.Equal(tilingOffset, storedValue);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var vector4Field = host.CurrentTree?.Root.Children[0];
        Assert.Equal(tilingOffset, vector4Field?.Payload.Vector4Value);
        Assert.Equal(tilingOffset, panel.TilingOffset);
    }

    [Fact]
    public void Set_number_input_value_updates_state_without_immediate_rebuild_and_next_rebuild_uses_value()
    {
        var panel = new NumberInputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetNumberInputValue(new GuiNodeId("ui.style", "roughness", GuiNodeKind.NumberInput), 0.65d);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetNumericValue(
            new GuiNodeId("ui.style", "roughness", GuiNodeKind.NumberInput),
            out var storedValue));
        Assert.Equal(0.65d, storedValue);

        host.OnPanelActivated(CreateLifecycleContext("ui.style"));

        var numberInput = host.CurrentTree?.Root.Children[0];
        Assert.Equal(0.65d, numberInput?.Payload.NumericValue);
        Assert.Equal(0.65d, panel.Roughness);
    }

    [Fact]
    public void Setting_current_number_input_value_does_not_rebuild_tree()
    {
        var panel = new NumberInputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetNumberInputValue(new GuiNodeId("ui.style", "roughness", GuiNodeKind.NumberInput), 0.5d);

        Assert.Equal(0.5d, panel.Roughness);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Commit_text_updates_state_and_rebuilds_tree()
    {
        var panel = new InputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.CommitText(new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField), "gbuffer");

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetText(
            new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField),
            out var storedText));
        Assert.Equal("gbuffer", storedText);
        Assert.Equal("gbuffer", panel.FilterText);
    }

    [Fact]
    public void Commit_current_text_does_not_rebuild_tree()
    {
        var panel = new InputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.CommitText(new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField), "default");

        Assert.Equal("default", panel.FilterText);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Set_toggle_updates_state_and_rebuilds_tree()
    {
        var panel = new InputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetToggle(new GuiNodeId("ui.style", "show-disabled", GuiNodeKind.Toggle), isChecked: true);

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetToggle(
            new GuiNodeId("ui.style", "show-disabled", GuiNodeKind.Toggle),
            out var isChecked));
        Assert.True(isChecked);
        Assert.True(panel.ShowDisabled);
    }

    [Fact]
    public void Setting_current_toggle_value_does_not_rebuild_tree()
    {
        var panel = new InputDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetToggle(new GuiNodeId("ui.style", "show-disabled", GuiNodeKind.Toggle), isChecked: false);

        Assert.False(panel.ShowDisabled);
        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    [Fact]
    public void Set_foldout_expanded_updates_state_and_rebuilds_tree()
    {
        var panel = new FoldoutDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetFoldoutExpanded(
            new GuiNodeId("ui.style", "advanced", GuiNodeKind.Foldout),
            isExpanded: true);

        Assert.Equal(initialBuildCount + 1, panel.GuiBuildCount);
        Assert.True(host.StateStore.TryGetFoldoutExpanded(
            new GuiNodeId("ui.style", "advanced", GuiNodeKind.Foldout),
            out var isExpanded));
        Assert.True(isExpanded);
        var foldout = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.True(foldout.Payload.IsExpanded);
        Assert.Single(foldout.Children);
    }

    [Fact]
    public void Setting_current_foldout_state_does_not_rebuild_tree()
    {
        var panel = new FoldoutDrivenCodeFirstPanel();
        var host = new CodeFirstPanelHostViewModel(panel);
        host.OnPanelAttached(CreateLifecycleContext("ui.style"));
        var initialBuildCount = panel.GuiBuildCount;

        host.SetFoldoutExpanded(
            new GuiNodeId("ui.style", "advanced", GuiNodeKind.Foldout),
            isExpanded: false);

        Assert.Equal(initialBuildCount, panel.GuiBuildCount);
    }

    private static EditorPanelLifecycleContext CreateLifecycleContext(string panelId = "render.frameDebugger")
    {
        return new EditorPanelLifecycleContext(
            panelId,
            "Frame Debugger",
            EditorDockArea.Right,
            IsFloatingWorkspace: false);
    }

    private static EditorPanelFrameContext CreateFrameContext(long sequence)
    {
        return new EditorPanelFrameContext(
            CreateLifecycleContext(),
            DateTimeOffset.UnixEpoch.AddMilliseconds(sequence * 16),
            TimeSpan.FromMilliseconds(16),
            sequence);
    }

    private sealed class RecordingEditorUiDispatcher : IEditorUiDispatcher
    {
        private readonly Queue<Action> pendingActions_ = [];

        public int PendingCount => pendingActions_.Count;

        public bool CheckAccess() => true;

        public void Post(Action action)
        {
            pendingActions_.Enqueue(action);
        }

        public void RunPending()
        {
            while (pendingActions_.TryDequeue(out var action))
            {
                action();
            }
        }
    }

    private sealed class RecordingCodeFirstPanel : CodeFirstEditorPanel
    {
        public List<string> Events { get; } = [];

        public int GuiBuildCount { get; private set; }

        public bool RequestRepaintOnFrame { get; set; }

        public EditorPanelFrameUpdateRequest RequestedFrameUpdate { get; set; } =
            EditorPanelFrameUpdateRequest.Manual;

        public override EditorPanelFrameUpdateRequest FrameUpdateRequest => RequestedFrameUpdate;

        protected override void OnCreate(EditorPanelLifecycleContext context)
        {
            Events.Add($"create:{context.PanelId}");
        }

        protected override void OnEnable()
        {
            Events.Add("enable");
        }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            Events.Add("gui");
            gui.Label("title", "RenderGraph");
        }

        protected override void OnFrame(EditorPanelFrameContext context)
        {
            Events.Add($"frame:{context.Sequence}");
            if (RequestRepaintOnFrame)
            {
                context.RequestRepaint();
            }
        }

        protected override void OnDisable()
        {
            Events.Add("disable");
        }

        protected override void OnDestroy()
        {
            Events.Add("destroy");
        }
    }

    private sealed class FaultingCodeFirstPanel : CodeFirstEditorPanel
    {
        public bool ThrowOnGui { get; set; }

        protected override void OnGui(EditorGui gui)
        {
            if (ThrowOnGui)
            {
                throw new InvalidOperationException("Injected code-first failure.");
            }

            gui.Label("title", "Valid");
        }
    }

    private sealed class ValidationFailureCodeFirstPanel : CodeFirstEditorPanel
    {
        public bool EmitDuplicateKeys { get; set; }

        protected override void OnGui(EditorGui gui)
        {
            if (!EmitDuplicateKeys)
            {
                gui.Label("title", "Valid");
                return;
            }

            using (gui.Panel("container", "Container"))
            {
                gui.Label("duplicate", "First");
                gui.Label("duplicate", "Second");
            }
        }
    }

    private sealed class ListDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        private static readonly GuiListItem[] Sections =
        [
            new("overview", "Overview"),
            new("buttons", "Buttons"),
        ];

        public int GuiBuildCount { get; private set; }

        public string? SelectedSectionId { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            using (gui.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
            {
                using (gui.Panel("catalog", "Catalog"))
                {
                    SelectedSectionId = gui.List("sections", Sections, "overview");
                }

                using (gui.Panel("preview", "Preview"))
                {
                    gui.Label("title", SelectedSectionId == "buttons" ? "Buttons" : "Overview");
                }
            }
        }
    }

    private sealed class SplitDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            using (gui.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
            {
                gui.Text("left", "Left");
                gui.Text("right", "Right");
            }
        }
    }

    private sealed class ComboBoxDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        private static readonly GuiListItem[] RenderModes =
        [
            new("forward", "Forward"),
            new("deferred", "Deferred"),
        ];

        public int GuiBuildCount { get; private set; }

        public string? SelectedRenderModeId { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            SelectedRenderModeId = gui.ComboBox("render-mode", "Render Mode", RenderModes, "forward");
        }
    }

    private sealed class RadioGroupDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        private static readonly GuiListItem[] ShadingModes =
        [
            new("lit", "Lit"),
            new("wireframe", "Wireframe"),
        ];

        public int GuiBuildCount { get; private set; }

        public string? SelectedShadingModeId { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            SelectedShadingModeId = gui.RadioGroup("shading-mode", "Shading", ShadingModes, "lit");
        }
    }

    private sealed class NavigationDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        private static readonly GuiNavigationPage[] Pages =
        [
            new("overview", "Overview", gui => gui.Text("title", "Overview")),
            new("overview/foundations/typography", "Typography", gui => gui.Text("title", "Typography")),
            new("render/debug/frame-debugger", "Frame Debugger", gui => gui.Text("title", "Frame Debugger")),
        ];

        public int GuiBuildCount { get; private set; }

        public string? SelectedRoute { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            using (var navigation = gui.NavigationView("catalog", Pages, "overview"))
            {
                SelectedRoute = navigation.SelectedRoute;
                navigation.DrawSelected(gui);
            }
        }
    }

    private sealed class InputDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public string? FilterText { get; private set; }

        public bool ShowDisabled { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            FilterText = gui.TextInput("filter", "Filter", "default");
            ShowDisabled = gui.Toggle("show-disabled", "Show Disabled");
        }
    }

    private sealed class SliderDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public double Exposure { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            Exposure = gui.Slider("exposure", "Exposure", 0.5d, 0d, 2d);
        }
    }

    private sealed class ColorFieldDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public GuiColorValue Albedo { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            Albedo = gui.ColorField("albedo", "Albedo", new GuiColorValue(255, 128, 64), showAlpha: true);
        }
    }

    private sealed class Vector3FieldDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public GuiVector3Value Position { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            Position = gui.Vector3Field("position", "Position", new GuiVector3Value(0d, 0d, 0d));
        }
    }

    private sealed class Vector2FieldDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public GuiVector2Value UvScale { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            UvScale = gui.Vector2Field("uv-scale", "UV Scale", new GuiVector2Value(1d, 1d));
        }
    }

    private sealed class Vector4FieldDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public GuiVector4Value TilingOffset { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            TilingOffset = gui.Vector4Field(
                "tiling-offset",
                "Tiling Offset",
                new GuiVector4Value(0d, 0d, 0d, 0d));
        }
    }

    private sealed class NumberInputDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        public double Roughness { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            Roughness = gui.NumberInput("roughness", "Roughness", 0.5d, 0d, 1d, 0.05d, "0.00");
        }
    }

    private sealed class FoldoutDrivenCodeFirstPanel : CodeFirstEditorPanel
    {
        public int GuiBuildCount { get; private set; }

        protected override void OnGui(EditorGui gui)
        {
            GuiBuildCount++;
            using (var foldout = gui.Foldout("advanced", "Advanced", defaultExpanded: false))
            {
                if (foldout.IsExpanded)
                {
                    gui.Text("details", "Deferred details");
                }
            }
        }
    }
}
