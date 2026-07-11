using System;
using System.Linq;
using Asharia.Editor.Dialogs;
using Xunit;

namespace Asharia.Editor.Tests.Dialogs;

public sealed class EditorDialogActionTests
{
    [Fact]
    public void Dialog_enums_have_stable_names_and_values()
    {
        Assert.Equal(
            ["Information", "Warning", "Error"],
            Enum.GetNames<EditorDialogSeverity>());
        Assert.Equal(
            [0, 1, 2],
            Enum.GetValues<EditorDialogSeverity>().Select(value => (int)value));
        Assert.Equal(
            ["Primary", "Secondary", "Dismiss"],
            Enum.GetNames<EditorDialogActionRole>());
        Assert.Equal(
            [0, 1, 2],
            Enum.GetValues<EditorDialogActionRole>().Select(value => (int)value));
    }

    [Theory]
    [InlineData("save", true)]
    [InlineData("dont-save", true)]
    [InlineData("retry-2", true)]
    [InlineData("Save", false)]
    [InlineData("dont_save", false)]
    [InlineData("dont--save", false)]
    [InlineData("-save", false)]
    [InlineData("save-", false)]
    [InlineData("", false)]
    public void Action_id_uses_lowercase_kebab_syntax(string value, bool valid)
    {
        Assert.Equal(valid, EditorDialogActionId.TryCreate(value, out _));
    }

    [Fact]
    public void Default_action_id_is_invalid_and_renders_empty()
    {
        Assert.False(default(EditorDialogActionId).IsValid);
        Assert.Equal(string.Empty, default(EditorDialogActionId).Value);
        Assert.Equal(string.Empty, default(EditorDialogActionId).ToString());
    }

    [Fact]
    public void Action_descriptor_preserves_valid_values()
    {
        var action = new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("delete"),
            "Delete",
            EditorDialogActionRole.Primary,
            isDefault: true,
            isDestructive: true);

        Assert.Equal(EditorDialogActionId.Create("delete"), action.Id);
        Assert.Equal("Delete", action.Text);
        Assert.Equal(EditorDialogActionRole.Primary, action.Role);
        Assert.True(action.IsDefault);
        Assert.True(action.IsDestructive);
    }

    [Fact]
    public void Action_descriptor_rejects_invalid_values()
    {
        Assert.Throws<ArgumentException>(() => new EditorDialogActionDescriptor(
            default,
            "Save",
            EditorDialogActionRole.Primary));
        Assert.Throws<ArgumentException>(() => new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("save"),
            "   ",
            EditorDialogActionRole.Primary));
        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("save"),
            "Save",
            (EditorDialogActionRole)42));
    }
}
