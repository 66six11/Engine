using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Commands;
using Asharia.Editor.UI.CodeFirst.Abstractions;
using Avalonia.Input;
using Editor.Core.Models.Workbench;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchShortcutRouterTests
{
    [Fact]
    public void TryExecute_runs_matching_command()
    {
        var commandRouter = new CapturingCommandRouter();
        var router = WorkbenchShortcutRouter.FromActions(
            [CreateAction("workbench.commandPalette.open", "Ctrl+Shift+P")],
            commandRouter);

        var result = router.TryExecute(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift,
            isTextInputFocused: false);

        Assert.NotNull(result);
        Assert.True(result.Succeeded);
        Assert.Equal("workbench.commandPalette.open", commandRouter.ExecutedCommandIds.Single());
    }

    [Fact]
    public void TryExecute_returns_null_when_no_shortcut_matches()
    {
        var router = WorkbenchShortcutRouter.FromActions(
            [CreateAction("workbench.commandPalette.open", "Ctrl+Shift+P")],
            new CapturingCommandRouter());

        var result = router.TryExecute(
            Key.P,
            KeyModifiers.Control,
            isTextInputFocused: false);

        Assert.Null(result);
    }

    [Fact]
    public void TryExecute_uses_first_registered_command_for_duplicate_shortcuts()
    {
        var commandRouter = new CapturingCommandRouter();
        var router = WorkbenchShortcutRouter.FromActions(
            [
                CreateAction("workbench.first", "Ctrl+Shift+P"),
                CreateAction("workbench.second", "Ctrl+Shift+P"),
            ],
            commandRouter);

        router.TryExecute(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift,
            isTextInputFocused: false);

        Assert.Equal(["workbench.first"], commandRouter.ExecutedCommandIds);
    }

    [Fact]
    public void FromActions_ignores_invalid_shortcut_text()
    {
        var commandRouter = new CapturingCommandRouter();
        var router = WorkbenchShortcutRouter.FromActions(
            [CreateAction("workbench.invalid", "Ctrl+NotAKey")],
            commandRouter);

        var result = router.TryExecute(
            Key.P,
            KeyModifiers.Control,
            isTextInputFocused: false);

        Assert.Null(result);
        Assert.Empty(commandRouter.ExecutedCommandIds);
    }

    [Fact]
    public void TryExecute_ignores_plain_shortcut_when_text_input_is_focused()
    {
        var commandRouter = new CapturingCommandRouter();
        var router = WorkbenchShortcutRouter.FromActions(
            [CreateAction("workbench.find", "F")],
            commandRouter);

        var result = router.TryExecute(
            Key.F,
            KeyModifiers.None,
            isTextInputFocused: true);

        Assert.Null(result);
        Assert.Empty(commandRouter.ExecutedCommandIds);
    }

    [Fact]
    public void TryExecute_allows_modifier_shortcut_when_text_input_is_focused()
    {
        var commandRouter = new CapturingCommandRouter();
        var router = WorkbenchShortcutRouter.FromActions(
            [CreateAction("workbench.commandPalette.open", "Ctrl+Shift+P")],
            commandRouter);

        var result = router.TryExecute(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift,
            isTextInputFocused: true);

        Assert.NotNull(result);
        Assert.True(result.Succeeded);
        Assert.Equal(["workbench.commandPalette.open"], commandRouter.ExecutedCommandIds);
    }

    private static WorkbenchActionDescriptor CreateAction(string id, string shortcut)
    {
        return new WorkbenchActionDescriptor(
            id,
            id,
            WorkbenchActionKind.OpenCommandPalette,
            "Tools/Test",
            DefaultShortcut: shortcut);
    }

    private sealed class CapturingCommandRouter : IEditorGuiCommandExecutor
    {
        private readonly List<string> executedCommandIds_ = [];

        public IReadOnlyList<string> ExecutedCommandIds => executedCommandIds_;

        public EditorCommandExecutionResult Execute(string commandId)
        {
            executedCommandIds_.Add(commandId);
            return EditorCommandExecutionResult.Success(commandId);
        }
    }
}
