using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDialogHostDesignViewModel : EditorDialogHostViewModel
{
    public EditorDialogHostDesignViewModel()
    {
        _ = ShowAsync(EditorDialogRequest.Information(
            "About Studio",
            "Studio editor shell for VkEngine."));
    }
}
