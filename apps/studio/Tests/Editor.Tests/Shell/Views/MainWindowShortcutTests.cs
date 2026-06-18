using Avalonia.Input;
using Editor.Shell.Views;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class MainWindowShortcutTests
{
    [Fact]
    public void IsCommandPaletteShortcut_accepts_ctrl_shift_p()
    {
        Assert.True(MainWindow.IsCommandPaletteShortcut(
            Key.P,
            KeyModifiers.Control | KeyModifiers.Shift));
    }

    [Theory]
    [InlineData(Key.P, KeyModifiers.Control)]
    [InlineData(Key.P, KeyModifiers.Shift)]
    [InlineData(Key.P, KeyModifiers.Alt)]
    [InlineData(Key.F, KeyModifiers.Control | KeyModifiers.Shift)]
    [InlineData(Key.P, KeyModifiers.None)]
    public void IsCommandPaletteShortcut_rejects_other_gestures(
        Key key,
        KeyModifiers keyModifiers)
    {
        Assert.False(MainWindow.IsCommandPaletteShortcut(key, keyModifiers));
    }
}
