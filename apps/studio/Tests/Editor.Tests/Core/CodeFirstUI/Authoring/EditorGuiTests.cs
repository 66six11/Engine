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
}
