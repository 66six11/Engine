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
