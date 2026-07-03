using Editor.Core.Models.Dialogs;

namespace Editor.Shell.ViewModels.Dialogs;

public sealed class EditorDialogHostDesignViewModel : EditorDialogHostViewModel
{
    public EditorDialogHostDesignViewModel()
    {
        _ = ShowAsync(EditorDialogRequest.Information(
            "About Studio",
            "Studio editor shell for VkEngine."));
    }
}
