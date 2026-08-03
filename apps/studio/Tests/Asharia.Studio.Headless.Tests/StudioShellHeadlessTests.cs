using Avalonia;
using Avalonia.Automation;
using Avalonia.Automation.Peers;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Editor;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioShellHeadlessTests
{
    [AvaloniaFact]
    public void Production_shell_realizes_starting_and_empty_states_with_stable_semantics()
    {
        Assert.IsType<App>(Avalonia.Application.Current);
        using var viewModel = new StudioShellViewModel();
        var window = new MainWindow
        {
            DataContext = viewModel,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();

            var starting = Assert.IsType<Border>(window.FindControl<Border>("StartingState"));
            var startingText = Assert.IsType<TextBlock>(
                window.FindControl<TextBlock>("StartingStateText"));
            Assert.True(starting.IsVisible);
            Assert.Equal("Starting", startingText.Text);
            Assert.Equal(
                "StudioShellStartingState",
                AutomationProperties.GetAutomationId(starting));
            Assert.Equal("Studio startup state", AutomationProperties.GetName(starting));
            Assert.Equal(
                AutomationControlType.StatusBar,
                AutomationProperties.GetControlTypeOverride(starting));

            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();

            var emptyWorkspace = Assert.IsType<Grid>(
                window.FindControl<Grid>("EmptyWorkspaceState"));
            var noProject = Assert.IsType<Border>(
                window.FindControl<Border>("NoProjectState"));
            var noDocument = Assert.IsType<Border>(
                window.FindControl<Border>("NoDocumentState"));
            Assert.False(starting.IsVisible);
            Assert.True(emptyWorkspace.IsVisible);
            Assert.Equal(
                "No Project",
                window.FindControl<TextBlock>("NoProjectStateText")?.Text);
            Assert.Equal(
                "No Document",
                window.FindControl<TextBlock>("NoDocumentStateText")?.Text);
            Assert.Equal(
                "StudioShellNoProjectState",
                AutomationProperties.GetAutomationId(noProject));
            Assert.Equal(
                "StudioShellNoDocumentState",
                AutomationProperties.GetAutomationId(noDocument));
            Assert.Equal(
                AutomationControlType.Group,
                AutomationProperties.GetControlTypeOverride(noProject));
            Assert.Equal(
                AutomationControlType.Group,
                AutomationProperties.GetControlTypeOverride(noDocument));
        }
        finally
        {
            window.Close();
        }
    }
}
