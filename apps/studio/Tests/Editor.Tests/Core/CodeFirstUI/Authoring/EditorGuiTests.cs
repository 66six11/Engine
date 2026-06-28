using System;
using System.Collections.Generic;
using Editor.Core.CodeFirstUI;
using Editor.Core.Models;
using Xunit;

namespace Editor.Tests.Core.CodeFirstUI;

public sealed class EditorGuiTests
{
    [Fact]
    public void Button_returns_true_once_when_matching_click_event_exists()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");
        var events = new GuiEventQueue();
        var state = new GuiStateStore();
        var capture = new GuiNodeId(
            "render.frameDebugger",
            "toolbar/capture",
            GuiNodeKind.Button);
        events.EnqueueButtonClicked(capture);

        var gui = new EditorGui(builder, events, state, new RecordingCommandExecutor());

        using (gui.Toolbar("toolbar"))
        {
            Assert.True(gui.Button("capture", "Capture Frame"));
            Assert.False(gui.Button("capture-again", "Capture Frame"));
        }

        Assert.False(events.ConsumeButtonClicked(capture));
    }

    [Fact]
    public void Command_button_executes_command_only_when_button_was_clicked()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");
        var events = new GuiEventQueue();
        var state = new GuiStateStore();
        var executor = new RecordingCommandExecutor();
        events.EnqueueButtonClicked(new GuiNodeId(
            "render.frameDebugger",
            "toolbar/capture",
            GuiNodeKind.Button));
        var gui = new EditorGui(builder, events, state, executor);

        using (gui.Toolbar("toolbar"))
        {
            var clickedResult = gui.CommandButton(
                "capture",
                "Capture Frame",
                "render.captureFrame");
            var idleResult = gui.CommandButton(
                "capture-secondary",
                "Capture Frame",
                "render.captureFrame");

            Assert.NotNull(clickedResult);
            Assert.True(clickedResult.Succeeded);
            Assert.Null(idleResult);
        }

        Assert.Equal(["render.captureFrame"], executor.CommandIds);
    }

    [Fact]
    public void Text_emits_label_tone_and_size_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        gui.Text("hint", "Muted caption", GuiTextTone.Muted, GuiTextSize.Caption);

        var label = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Label, label.Kind);
        Assert.Equal("Muted caption", label.Label);
        Assert.Equal(GuiTextTone.Muted, label.Payload.TextTone);
        Assert.Equal(GuiTextSize.Caption, label.Payload.TextSize);
    }

    [Fact]
    public void Text_defaults_to_secondary_body_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        gui.Text("summary", "Default label");

        var label = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiTextTone.Secondary, label.Payload.TextTone);
        Assert.Equal(GuiTextSize.Body, label.Payload.TextSize);
    }

    [Fact]
    public void Separator_emits_separator_node()
    {
        var builder = new GuiFrameBuilder("render.frameDebugger");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        using (gui.Toolbar("toolbar"))
        {
            gui.Button("capture", "Capture Frame");
            gui.Separator("capture-group");
            gui.Button("clear", "Clear");
        }

        var toolbar = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Separator, toolbar.Children[1].Kind);
        Assert.Equal("toolbar/capture-group", toolbar.Children[1].Id.KeyPath);
    }

    [Fact]
    public void Navigation_view_uses_stored_route_and_draws_selected_page()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetSelectedRoute(
            new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView),
            "render/debug/frame-debugger");
        state.SetNavigationRouteExpanded(
            new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView),
            "overview",
            isExpanded: false);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());
        var pages = new[]
        {
            new GuiNavigationPage("overview", "Overview", editorGui => editorGui.Text("title", "Overview")),
            new GuiNavigationPage(
                "render/debug/frame-debugger",
                "Frame Debugger",
                editorGui => editorGui.Text("title", "Frame Debugger")),
        };

        using (var navigation = gui.NavigationView("catalog", pages, "overview", 0.35d))
        {
            Assert.Equal("render/debug/frame-debugger", navigation.SelectedRoute);
            Assert.True(navigation.DrawSelected(gui));
        }

        var node = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.NavigationView, node.Kind);
        Assert.Equal("render/debug/frame-debugger", node.Payload.SelectedRoute);
        Assert.Equal(0.35d, node.Payload.SplitRatio);
        Assert.Equal(["overview"], node.Payload.CollapsedNavigationRoutes);
        Assert.Equal("Frame Debugger", Assert.Single(node.Children).Label);
    }

    [Fact]
    public void Navigation_view_falls_back_to_default_route()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        using (var navigation = gui.NavigationView(
            "catalog",
            [
                new GuiNavigationPage("overview", "Overview", editorGui => editorGui.Text("title", "Overview")),
                new GuiNavigationPage("controls/buttons", "Buttons", editorGui => editorGui.Text("title", "Buttons")),
            ],
            "controls/buttons"))
        {
            Assert.Equal("controls/buttons", navigation.SelectedRoute);
            Assert.True(navigation.DrawSelected(gui));
        }

        var node = Assert.Single(builder.Build().Root.Children);
        Assert.Equal("controls/buttons", node.Payload.SelectedRoute);
        Assert.Equal("Buttons", Assert.Single(node.Children).Label);
    }

    [Fact]
    public void Navigation_view_falls_back_when_stored_route_is_removed()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var nodeId = new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView);
        state.SetSelectedRoute(nodeId, "removed/page");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        using (var navigation = gui.NavigationView(
            "catalog",
            [
                new GuiNavigationPage("overview", "Overview", editorGui => editorGui.Text("title", "Overview")),
                new GuiNavigationPage("controls/buttons", "Buttons", editorGui => editorGui.Text("title", "Buttons")),
            ],
            "overview"))
        {
            Assert.Equal("overview", navigation.SelectedRoute);
            Assert.True(navigation.DrawSelected(gui));
        }

        Assert.True(state.TryGetSelectedRoute(nodeId, out var storedRoute));
        Assert.Equal("overview", storedRoute);
        var node = Assert.Single(builder.Build().Root.Children);
        Assert.Equal("overview", node.Payload.SelectedRoute);
        Assert.Equal("Overview", Assert.Single(node.Children).Label);
    }

    [Fact]
    public void Navigation_view_uses_stored_split_ratio()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetSplitRatio(
            new GuiNodeId("ui.style", "catalog", GuiNodeKind.NavigationView),
            0.42d);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        using (gui.NavigationView(
            "catalog",
            [new GuiNavigationPage("overview", "Overview", editorGui => editorGui.Text("title", "Overview"))],
            "overview",
            0.30d))
        {
        }

        var node = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(0.42d, node.Payload.SplitRatio);
    }

    [Fact]
    public void Navigation_view_allows_duplicate_routes_to_reach_tree_validation()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        using (var navigation = gui.NavigationView(
            "catalog",
            [
                new GuiNavigationPage("controls/buttons", "Buttons", editorGui => editorGui.Text("title", "Buttons")),
                new GuiNavigationPage("controls/buttons", "Duplicate Buttons", editorGui => editorGui.Text("title", "Duplicate Buttons")),
            ],
            "controls/buttons"))
        {
            Assert.Equal("controls/buttons", navigation.SelectedRoute);
            Assert.True(navigation.DrawSelected(gui));
        }

        var node = Assert.Single(builder.Build().Root.Children);
        Assert.Equal("controls/buttons", node.Payload.SelectedRoute);
        Assert.Equal(2, node.Payload.NavigationItems.Count);
    }

    [Fact]
    public void List_returns_stored_selection_and_emits_selected_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var events = new GuiEventQueue();
        var state = new GuiStateStore();
        var sections = new[]
        {
            new GuiListItem("overview", "Overview"),
            new GuiListItem("buttons", "Buttons"),
        };
        state.SetSelectedItem(new GuiNodeId("ui.style", "sections", GuiNodeKind.List), "buttons");

        var gui = new EditorGui(builder, events, state, new RecordingCommandExecutor());

        var selected = gui.List("sections", sections, selectedItemId: "overview");

        Assert.Equal("buttons", selected);

        var list = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.List, list.Kind);
        Assert.Equal("buttons", list.Payload.SelectedItemId);
        Assert.Equal(sections, list.Payload.ListItems);
    }

    [Fact]
    public void List_falls_back_to_first_item_when_selection_is_empty()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        var selected = gui.List(
            "sections",
            [new GuiListItem("overview", "Overview"), new GuiListItem("buttons", "Buttons")]);

        Assert.Equal("overview", selected);
    }

    [Fact]
    public void Split_uses_stored_ratio_when_rebuilding_tree()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetSplitRatio(new GuiNodeId("ui.style", "layout", GuiNodeKind.Split), 0.42d);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        using (gui.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            gui.Text("left", "Left");
            gui.Text("right", "Right");
        }

        var split = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(0.42d, split.Payload.SplitRatio);
    }

    [Fact]
    public void Inputs_return_stored_values_and_emit_payloads()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetText(new GuiNodeId("ui.style", "filter", GuiNodeKind.TextField), "gbuffer");
        state.SetToggle(new GuiNodeId("ui.style", "show-disabled", GuiNodeKind.Toggle), isChecked: false);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var filter = gui.TextInput(
            "filter",
            "Filter",
            "albedo",
            GuiTextInputCommitMode.OnEnter);
        var showDisabled = gui.Toggle("show-disabled", "Show Disabled", isChecked: true);

        Assert.Equal("gbuffer", filter);
        Assert.False(showDisabled);

        var tree = builder.Build();
        var textField = tree.Root.Children[0];
        Assert.Equal(GuiNodeKind.TextField, textField.Kind);
        Assert.Equal("gbuffer", textField.Payload.TextValue);
        Assert.Equal(GuiTextInputCommitMode.OnEnter, textField.Payload.TextCommitMode);

        var toggle = tree.Root.Children[1];
        Assert.Equal(GuiNodeKind.Toggle, toggle.Kind);
        Assert.False(toggle.Payload.IsChecked);
    }

    [Fact]
    public void Combo_box_returns_stored_selection_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var modes = new[]
        {
            new GuiListItem("forward", "Forward"),
            new GuiListItem("deferred", "Deferred"),
        };
        state.SetSelectedItem(new GuiNodeId("ui.style", "render-mode", GuiNodeKind.ComboBox), "deferred");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var selected = gui.ComboBox("render-mode", "Render Mode", modes, selectedItemId: "forward");

        Assert.Equal("deferred", selected);

        var comboBox = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.ComboBox, comboBox.Kind);
        Assert.Equal("Render Mode", comboBox.Label);
        Assert.Equal("deferred", comboBox.Payload.SelectedItemId);
        Assert.Equal(modes, comboBox.Payload.ListItems);
    }

    [Fact]
    public void Radio_group_returns_stored_selection_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var modes = new[]
        {
            new GuiListItem("lit", "Lit"),
            new GuiListItem("wireframe", "Wireframe"),
        };
        state.SetSelectedItem(new GuiNodeId("ui.style", "shading-mode", GuiNodeKind.RadioGroup), "wireframe");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var selected = gui.RadioGroup("shading-mode", "Shading", modes, selectedItemId: "lit");

        Assert.Equal("wireframe", selected);

        var radioGroup = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.RadioGroup, radioGroup.Kind);
        Assert.Equal("Shading", radioGroup.Label);
        Assert.Equal("wireframe", radioGroup.Payload.SelectedItemId);
        Assert.Equal(modes, radioGroup.Payload.ListItems);
    }

    [Fact]
    public void Enum_popup_returns_stored_enum_and_emits_combo_box_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetSelectedItem(new GuiNodeId("ui.style", "render-path", GuiNodeKind.ComboBox), "RayTracing");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var selected = gui.EnumPopup("render-path", "Render Path", RenderPathMode.Deferred);

        Assert.Equal(RenderPathMode.RayTracing, selected);

        var comboBox = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.ComboBox, comboBox.Kind);
        Assert.Equal("Render Path", comboBox.Label);
        Assert.Equal("RayTracing", comboBox.Payload.SelectedItemId);
        Assert.Contains(comboBox.Payload.ListItems, item => item.Id == "Forward" && item.Label == "Forward");
        Assert.Contains(comboBox.Payload.ListItems, item => item.Id == "RayTracing" && item.Label == "Ray Tracing");
    }

    [Fact]
    public void Enum_popup_falls_back_to_selected_value_when_stored_selection_is_removed()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var nodeId = new GuiNodeId("ui.style", "render-path", GuiNodeKind.ComboBox);
        state.SetSelectedItem(nodeId, "Removed");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var selected = gui.EnumPopup("render-path", "Render Path", RenderPathMode.Deferred);

        Assert.Equal(RenderPathMode.Deferred, selected);
        Assert.True(state.TryGetSelectedItem(nodeId, out var storedSelection));
        Assert.Equal("Deferred", storedSelection);
        var comboBox = Assert.Single(builder.Build().Root.Children);
        Assert.Equal("Deferred", comboBox.Payload.SelectedItemId);
    }

    [Fact]
    public void Color_field_returns_stored_value_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var storedColor = new GuiColorValue(16, 32, 48, 128);
        state.SetColorValue(new GuiNodeId("ui.style", "albedo", GuiNodeKind.ColorField), storedColor);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var value = gui.ColorField(
            "albedo",
            "Albedo",
            new GuiColorValue(255, 128, 64),
            showAlpha: true);

        Assert.Equal(storedColor, value);

        var colorField = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.ColorField, colorField.Kind);
        Assert.Equal("Albedo", colorField.Label);
        Assert.Equal(storedColor, colorField.Payload.ColorValue);
        Assert.True(colorField.Payload.ShowAlpha);
    }

    [Fact]
    public void Vector3_field_returns_stored_value_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var storedValue = new GuiVector3Value(1d, 2d, 3d);
        state.SetVector3Value(new GuiNodeId("ui.style", "position", GuiNodeKind.Vector3Field), storedValue);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var value = gui.Vector3Field(
            "position",
            "Position",
            new GuiVector3Value(0d, 0d, 0d),
            minimum: -10d,
            maximum: 10d,
            increment: 0.25d,
            formatString: "0.00");

        Assert.Equal(storedValue, value);

        var vector3Field = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Vector3Field, vector3Field.Kind);
        Assert.Equal("Position", vector3Field.Label);
        Assert.Equal(storedValue, vector3Field.Payload.Vector3Value);
        Assert.Equal(-10d, vector3Field.Payload.NumericMinimum);
        Assert.Equal(10d, vector3Field.Payload.NumericMaximum);
        Assert.Equal(0.25d, vector3Field.Payload.NumericSmallChange);
        Assert.Equal("0.00", vector3Field.Payload.NumericFormatString);
    }

    [Fact]
    public void Vector2_field_returns_stored_value_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var storedValue = new GuiVector2Value(1d, 2d);
        state.SetVector2Value(new GuiNodeId("ui.style", "uv-scale", GuiNodeKind.Vector2Field), storedValue);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var value = gui.Vector2Field(
            "uv-scale",
            "UV Scale",
            new GuiVector2Value(0d, 0d),
            minimum: 0d,
            maximum: 8d,
            increment: 0.125d,
            formatString: "0.000");

        Assert.Equal(storedValue, value);

        var vector2Field = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Vector2Field, vector2Field.Kind);
        Assert.Equal("UV Scale", vector2Field.Label);
        Assert.Equal(storedValue, vector2Field.Payload.Vector2Value);
        Assert.Equal(0d, vector2Field.Payload.NumericMinimum);
        Assert.Equal(8d, vector2Field.Payload.NumericMaximum);
        Assert.Equal(0.125d, vector2Field.Payload.NumericSmallChange);
        Assert.Equal("0.000", vector2Field.Payload.NumericFormatString);
    }

    [Fact]
    public void Vector4_field_returns_stored_value_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        var storedValue = new GuiVector4Value(1d, 2d, 3d, 4d);
        state.SetVector4Value(new GuiNodeId("ui.style", "tiling-offset", GuiNodeKind.Vector4Field), storedValue);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var value = gui.Vector4Field(
            "tiling-offset",
            "Tiling Offset",
            new GuiVector4Value(0d, 0d, 0d, 0d),
            minimum: -8d,
            maximum: 8d,
            increment: 0.125d,
            formatString: "0.000");

        Assert.Equal(storedValue, value);

        var vector4Field = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Vector4Field, vector4Field.Kind);
        Assert.Equal("Tiling Offset", vector4Field.Label);
        Assert.Equal(storedValue, vector4Field.Payload.Vector4Value);
        Assert.Equal(-8d, vector4Field.Payload.NumericMinimum);
        Assert.Equal(8d, vector4Field.Payload.NumericMaximum);
        Assert.Equal(0.125d, vector4Field.Payload.NumericSmallChange);
        Assert.Equal("0.000", vector4Field.Payload.NumericFormatString);
    }

    [Fact]
    public void Slider_returns_stored_value_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetNumericValue(new GuiNodeId("ui.style", "exposure", GuiNodeKind.Slider), 1.25d);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var value = gui.Slider(
            "exposure",
            "Exposure",
            value: 0.50d,
            minimum: 0d,
            maximum: 2d,
            smallChange: 0.05d,
            largeChange: 0.25d);

        Assert.Equal(1.25d, value);

        var slider = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Slider, slider.Kind);
        Assert.Equal("Exposure", slider.Label);
        Assert.Equal(1.25d, slider.Payload.NumericValue);
        Assert.Equal(0d, slider.Payload.NumericMinimum);
        Assert.Equal(2d, slider.Payload.NumericMaximum);
        Assert.Equal(0.05d, slider.Payload.NumericSmallChange);
        Assert.Equal(0.25d, slider.Payload.NumericLargeChange);
    }

    [Fact]
    public void Number_input_returns_stored_value_and_emits_payload()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetNumericValue(new GuiNodeId("ui.style", "roughness", GuiNodeKind.NumberInput), 0.65d);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());

        var value = gui.NumberInput(
            "roughness",
            "Roughness",
            value: 0.50d,
            minimum: 0d,
            maximum: 1d,
            increment: 0.05d,
            formatString: "0.00");

        Assert.Equal(0.65d, value);

        var numberInput = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.NumberInput, numberInput.Kind);
        Assert.Equal("Roughness", numberInput.Label);
        Assert.Equal(0.65d, numberInput.Payload.NumericValue);
        Assert.Equal(0d, numberInput.Payload.NumericMinimum);
        Assert.Equal(1d, numberInput.Payload.NumericMaximum);
        Assert.Equal(0.05d, numberInput.Payload.NumericSmallChange);
        Assert.Equal("0.00", numberInput.Payload.NumericFormatString);
    }

    [Fact]
    public void Progress_bar_emits_read_only_feedback_node()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        gui.ProgressBar(
            "shader-import",
            "Shader Import",
            value: 42d,
            minimum: 0d,
            maximum: 100d,
            isIndeterminate: false,
            showProgressText: true,
            progressTextFormat: "{1:0}%");

        var progressBar = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.ProgressBar, progressBar.Kind);
        Assert.Equal("Shader Import", progressBar.Label);
        Assert.Equal(42d, progressBar.Payload.NumericValue);
        Assert.Equal(0d, progressBar.Payload.NumericMinimum);
        Assert.Equal(100d, progressBar.Payload.NumericMaximum);
        Assert.False(progressBar.Payload.IsIndeterminate);
        Assert.True(progressBar.Payload.ShowProgressText);
        Assert.Equal("{1:0}%", progressBar.Payload.ProgressTextFormat);
    }

    [Fact]
    public void Text_input_emits_debounce_commit_delay()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        gui.TextInput(
            "filter",
            "Filter",
            "default",
            GuiTextInputCommitMode.Debounced,
            TimeSpan.FromMilliseconds(200));

        var textField = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiTextInputCommitMode.Debounced, textField.Payload.TextCommitMode);
        Assert.Equal(TimeSpan.FromMilliseconds(200), textField.Payload.TextCommitDelay);
    }

    [Fact]
    public void Scroll_and_validation_message_emit_container_and_feedback_nodes()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            new GuiStateStore(),
            new RecordingCommandExecutor());

        using (gui.Scroll("details"))
        {
            gui.ValidationMessage(
                "missing-shader",
                "Shader metadata is missing.",
                EditorDiagnosticSeverity.Warning);
        }

        var scroll = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Scroll, scroll.Kind);

        var message = Assert.Single(scroll.Children);
        Assert.Equal(GuiNodeKind.ValidationMessage, message.Kind);
        Assert.Equal("Shader metadata is missing.", message.Label);
        Assert.Equal(EditorDiagnosticSeverity.Warning, message.Payload.DiagnosticSeverity);
    }

    [Fact]
    public void Foldout_uses_stored_expanded_state_and_allows_collapsed_content_to_be_skipped()
    {
        var builder = new GuiFrameBuilder("ui.style");
        var state = new GuiStateStore();
        state.SetFoldoutExpanded(new GuiNodeId("ui.style", "advanced", GuiNodeKind.Foldout), isExpanded: false);
        var gui = new EditorGui(
            builder,
            new GuiEventQueue(),
            state,
            new RecordingCommandExecutor());
        var expensiveContentWasBuilt = false;

        using (var foldout = gui.Foldout("advanced", "Advanced", defaultExpanded: true))
        {
            Assert.False(foldout.IsExpanded);
            if (foldout.IsExpanded)
            {
                expensiveContentWasBuilt = true;
                gui.Text("expensive", "Deferred details");
            }
        }

        Assert.False(expensiveContentWasBuilt);
        var node = Assert.Single(builder.Build().Root.Children);
        Assert.Equal(GuiNodeKind.Foldout, node.Kind);
        Assert.False(node.Payload.IsExpanded);
        Assert.Empty(node.Children);
    }

    private sealed class RecordingCommandExecutor : IEditorGuiCommandExecutor
    {
        private readonly List<string> commandIds_ = [];

        public IReadOnlyList<string> CommandIds => commandIds_;

        public WorkbenchCommandExecutionResult Execute(string commandId)
        {
            commandIds_.Add(commandId);
            return WorkbenchCommandExecutionResult.Success(commandId);
        }
    }

    private enum RenderPathMode
    {
        Forward,
        Deferred,
        RayTracing,
    }
}
