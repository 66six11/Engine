using Avalonia.Controls;
using Avalonia.Input;
using Editor.Shell.Views;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class MainWindowShortcutTests
{
    [Fact]
    public void IsTextInputShortcutSource_accepts_text_box()
    {
        Assert.True(MainWindow.IsTextInputShortcutSource(new TextBox()));
    }

    [Fact]
    public void IsTextInputShortcutSource_rejects_non_text_controls()
    {
        Assert.False(MainWindow.IsTextInputShortcutSource(new Border()));
    }
}
