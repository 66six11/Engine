namespace Editor.Shell.ViewModels;

public sealed class EditorDockTabStripItemViewModel
{
    public EditorDockTabStripItemViewModel(EditorDockTabViewModel tab, bool isPlaceholder)
    {
        Tab = tab;
        IsPlaceholder = isPlaceholder;
    }

    public EditorDockTabViewModel Tab { get; }

    public bool IsPlaceholder { get; }

    public bool IsRealTab => !IsPlaceholder;
}
