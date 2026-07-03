using System;
using System.Linq;
using System.Threading.Tasks;
using Editor.Core.Models.Dialogs;
using Editor.Shell.ViewModels.Dialogs;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Dialogs;

public sealed class EditorDialogHostViewModelTests
{
    [Fact]
    public void Initial_state_is_closed()
    {
        var host = new EditorDialogHostViewModel();

        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public void Design_preview_opens_about_dialog()
    {
        var host = new EditorDialogHostDesignViewModel();

        Assert.True(host.IsOpen);
        Assert.Equal("About Studio", host.Title);
        Assert.Single(host.Buttons);
    }

    [Fact]
    public void ShowAsync_opens_request_and_projects_buttons()
    {
        var host = new EditorDialogHostViewModel();

        var resultTask = host.ShowAsync(EditorDialogRequest.Information("About", "Studio editor shell"));

        Assert.True(host.IsOpen);
        Assert.Equal("About", host.Title);
        Assert.Equal("Studio editor shell", host.Message);
        var button = Assert.Single(host.Buttons);
        Assert.Equal("ok", button.Id);
        Assert.Equal("OK", button.Text);
        Assert.True(button.IsDefault);
        Assert.False(resultTask.IsCompleted);
    }

    [Fact]
    public async Task Button_command_completes_result_and_closes_host()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(EditorDialogRequest.Information("About", "Studio editor shell"));

        host.Buttons.Single().Command.Execute(null);

        var result = await resultTask;
        Assert.Equal(EditorDialogResultKind.Accepted, result.Kind);
        Assert.Equal("ok", result.ButtonId);
        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public async Task TryCancel_completes_cancelable_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(EditorDialogRequest.Confirmation(
            "Close Tab",
            "Close the active tab?",
            acceptText: "Close",
            rejectText: "Keep Open"));

        Assert.True(host.TryCancel());

        var result = await resultTask;
        Assert.Equal(EditorDialogResultKind.Canceled, result.Kind);
        Assert.Null(result.ButtonId);
        Assert.False(host.IsOpen);
    }

    [Fact]
    public void TryCancel_ignores_non_cancelable_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(new EditorDialogRequest(
            EditorDialogKind.Information,
            "Blocking",
            "This dialog must be acknowledged.",
            isCancelable: false,
            Buttons:
            [
                new EditorDialogButtonDescriptor("ok", "OK", EditorDialogButtonRole.Accept, IsDefault: true),
            ]));

        Assert.False(host.TryCancel());
        Assert.True(host.IsOpen);
    }

    [Fact]
    public void ShowAsync_rejects_second_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(EditorDialogRequest.Information("First", "Already open"));

        var exception = Assert.Throws<InvalidOperationException>(
            () => { _ = host.ShowAsync(EditorDialogRequest.Information("Second", "Should fail")); });
        Assert.Contains("already active", exception.Message);
    }
}
