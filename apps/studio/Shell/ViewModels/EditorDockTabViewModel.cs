using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockTabViewModel : ViewModelBase
{
    private bool isActive_;
    private bool isDragSource_;
    private DockArea area_;

    public EditorDockTabViewModel(
        string id,
        string title,
        string tag,
        string titleDetail,
        string statusText,
        PanelKind kind,
        DockArea area,
        object content,
        string? iconKey = null)
    {
        Id = id;
        Title = title;
        Tag = tag;
        TitleDetail = titleDetail;
        StatusText = statusText;
        Kind = kind;
        area_ = area;
        Content = content;
        IconKey = iconKey;
    }

    public string Id { get; }

    public string Title { get; }

    public string Tag { get; }

    public string TitleDetail { get; }

    public string StatusText { get; }

    public string? IconKey { get; }

    public bool HasIcon => !string.IsNullOrWhiteSpace(IconKey);

    public PanelKind Kind { get; }

    public object Content { get; }

    public DockArea Area
    {
        get => area_;
        set => SetProperty(ref area_, value);
    }

    public bool IsActive
    {
        get => isActive_;
        set => SetProperty(ref isActive_, value);
    }

    public bool IsDragSource
    {
        get => isDragSource_;
        private set => SetProperty(ref isDragSource_, value);
    }

    internal void SetDragSourceState(bool isDragSource)
    {
        IsDragSource = isDragSource;
    }
}
