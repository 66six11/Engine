using System;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Editor.Dialogs;
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
    public void ShowAsync_projects_action_semantics()
    {
        var host = new EditorDialogHostViewModel();
        var request = CreateRequest(
            allowSystemDismiss: true,
            [
                new EditorDialogActionDescriptor(
                    EditorDialogActionId.Create("delete"),
                    "Delete",
                    EditorDialogActionRole.Primary,
                    isDefault: true,
                    isDestructive: true),
                DismissAction(isDefault: false),
            ]);

        var resultTask = host.ShowAsync(request);

        Assert.True(host.IsOpen);
        Assert.Equal("Title", host.Title);
        Assert.Equal("Message", host.Message);
        var button = host.Buttons.First();
        Assert.Equal("delete", button.Id);
        Assert.Equal("Delete", button.Text);
        Assert.Equal(EditorDialogActionRole.Primary, button.Role);
        Assert.True(button.IsDefault);
        Assert.True(button.IsDestructive);
        Assert.False(resultTask.IsCompleted);
    }

    [Fact]
    public async Task Action_command_returns_exact_identity_and_closes_host()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(CreateRequest(
            allowSystemDismiss: true,
            [DismissAction()]));

        host.Buttons.Single().Command.Execute(null);

        var result = await resultTask;
        Assert.Equal(EditorDialogCompletionKind.ActionInvoked, result.Completion);
        Assert.Equal(EditorDialogActionId.Create("close"), result.ActionId);
        Assert.False(host.IsOpen);
        Assert.Null(host.ActiveRequest);
        Assert.Empty(host.Buttons);
    }

    [Fact]
    public async Task TrySystemDismiss_completes_allowed_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(CreateRequest(
            allowSystemDismiss: true,
            [DismissAction()]));

        Assert.True(host.TrySystemDismiss());

        var result = await resultTask;
        Assert.Equal(EditorDialogCompletionKind.SystemDismissed, result.Completion);
        Assert.Null(result.ActionId);
        Assert.False(host.IsOpen);
    }

    [Fact]
    public void TrySystemDismiss_ignores_non_dismissible_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(CreateRequest(
            allowSystemDismiss: false,
            [DismissAction()]));

        Assert.False(host.TrySystemDismiss());
        Assert.True(host.IsOpen);
    }

    [Fact]
    public void ShowAsync_rejects_second_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        _ = host.ShowAsync(CreateRequest(true, [DismissAction()]));

        var exception = Assert.Throws<InvalidOperationException>(
            () => { _ = host.ShowAsync(CreateRequest(true, [DismissAction()])); });
        Assert.Contains("already active", exception.Message);
    }

    private static EditorDialogRequest CreateRequest(
        bool allowSystemDismiss,
        EditorDialogActionDescriptor[] actions)
    {
        return new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "Title",
            "Message",
            allowSystemDismiss,
            actions);
    }

    private static EditorDialogActionDescriptor DismissAction(bool isDefault = true)
    {
        return new EditorDialogActionDescriptor(
            EditorDialogActionId.Create("close"),
            "Close",
            EditorDialogActionRole.Dismiss,
            isDefault);
    }
}
