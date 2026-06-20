using Avalonia.Input;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchShortcutGestureTests
{
    [Fact]
    public void TryParse_accepts_ctrl_shift_key()
    {
        Assert.True(WorkbenchShortcutGesture.TryParse("Ctrl+Shift+P", out var gesture));

        Assert.Equal(Key.P, gesture.Key);
        Assert.Equal(KeyModifiers.Control | KeyModifiers.Shift, gesture.Modifiers);
        Assert.True(gesture.Matches(Key.P, KeyModifiers.Control | KeyModifiers.Shift));
    }

    [Fact]
    public void TryParse_accepts_control_alias_and_alt()
    {
        Assert.True(WorkbenchShortcutGesture.TryParse("Control+Alt+C", out var gesture));

        Assert.Equal(Key.C, gesture.Key);
        Assert.Equal(KeyModifiers.Control | KeyModifiers.Alt, gesture.Modifiers);
    }

    [Fact]
    public void TryParse_accepts_function_key_without_modifier()
    {
        Assert.True(WorkbenchShortcutGesture.TryParse("F1", out var gesture));

        Assert.Equal(Key.F1, gesture.Key);
        Assert.Equal(KeyModifiers.None, gesture.Modifiers);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("Ctrl")]
    [InlineData("Ctrl+NotAKey")]
    [InlineData("Ctrl+Shift+")]
    public void TryParse_rejects_invalid_shortcut_text(string shortcutText)
    {
        Assert.False(WorkbenchShortcutGesture.TryParse(shortcutText, out _));
    }
}
