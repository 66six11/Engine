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
