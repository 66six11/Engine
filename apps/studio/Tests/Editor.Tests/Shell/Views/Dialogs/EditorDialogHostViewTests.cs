using System.Threading.Tasks;
using Asharia.Editor.Dialogs;
using Editor.Shell.ViewModels.Dialogs;
using Editor.Shell.Views.Dialogs;
using Xunit;

namespace Editor.Tests.Shell.Views.Dialogs;

public sealed class EditorDialogHostViewTests
{
    [Fact]
    public void TrySystemDismissDialog_rejects_non_dialog_context()
    {
        Assert.False(EditorDialogHostView.TrySystemDismissDialog(new object()));
    }

    [Fact]
    public async Task TrySystemDismissDialog_dismisses_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "About",
            "Studio editor shell",
            allowSystemDismiss: true,
            [
                new EditorDialogActionDescriptor(
                    EditorDialogActionId.Create("close"),
                    "Close",
                    EditorDialogActionRole.Dismiss,
                    isDefault: true),
            ]));

        Assert.True(EditorDialogHostView.TrySystemDismissDialog(host));

        var result = await resultTask;
        Assert.Equal(EditorDialogCompletionKind.SystemDismissed, result.Completion);
        Assert.False(host.IsOpen);
    }
}
