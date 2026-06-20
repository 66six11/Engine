using Editor.Core.Models;
using Editor.Shell.ViewModels;
using Editor.Shell.Views;
using System.Threading.Tasks;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class EditorDialogHostViewTests
{
    [Fact]
    public void TryCancelDialog_rejects_non_dialog_context()
    {
        Assert.False(EditorDialogHostView.TryCancelDialog(new object()));
    }

    [Fact]
    public async Task TryCancelDialog_cancels_active_dialog()
    {
        var host = new EditorDialogHostViewModel();
        var resultTask = host.ShowAsync(EditorDialogRequest.Information("About", "Studio editor shell"));

        Assert.True(EditorDialogHostView.TryCancelDialog(host));

        var result = await resultTask;
        Assert.Equal(EditorDialogResultKind.Canceled, result.Kind);
        Assert.False(host.IsOpen);
    }
}
