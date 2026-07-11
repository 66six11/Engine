using System;
using System.Linq;
using Asharia.Editor.Dialogs;
using Xunit;

namespace Asharia.Editor.Tests.Dialogs;

public sealed class EditorDialogResultTests
{
    [Fact]
    public void Completion_enum_has_stable_names_and_values()
    {
        Assert.Equal(
            ["ActionInvoked", "SystemDismissed"],
            Enum.GetNames<EditorDialogCompletionKind>());
        Assert.Equal(
            [0, 1],
            Enum.GetValues<EditorDialogCompletionKind>().Select(value => (int)value));
    }

    [Fact]
    public void Action_result_contains_exact_action_identity()
    {
        var id = EditorDialogActionId.Create("save");

        var result = EditorDialogResult.ActionInvoked(id);

        Assert.Equal(EditorDialogCompletionKind.ActionInvoked, result.Completion);
        Assert.Equal(id, result.ActionId);
    }

    [Fact]
    public void System_dismissed_result_contains_no_action_identity()
    {
        var result = EditorDialogResult.SystemDismissed();

        Assert.Equal(EditorDialogCompletionKind.SystemDismissed, result.Completion);
        Assert.Null(result.ActionId);
    }

    [Fact]
    public void Action_result_rejects_invalid_identity()
    {
        Assert.Throws<ArgumentException>(() => EditorDialogResult.ActionInvoked(default));
    }
}
