namespace Editor.Shell.ViewModels.Dialogs;

public sealed class EditorDialogHostDesignViewModel : EditorDialogHostViewModel
{
    public EditorDialogHostDesignViewModel()
    {
        _ = ShowAsync(StudioDialogRequests.About());
    }
}
