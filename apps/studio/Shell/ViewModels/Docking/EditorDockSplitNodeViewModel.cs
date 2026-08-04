using Avalonia.Controls;
using Avalonia.Layout;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockSplitNodeViewModel : EditorDockNodeViewModel
{
    private EditorDockNodeViewModel first_;
    private EditorDockNodeViewModel second_;
    private GridLength firstLength_;
    private GridLength secondLength_;

    public EditorDockSplitNodeViewModel(
        string id,
        Orientation orientation,
        EditorDockNodeViewModel first,
        EditorDockNodeViewModel second,
        GridLength firstLength,
        GridLength secondLength)
        : base(id)
    {
        Orientation = orientation;
        first_ = first;
        second_ = second;
        firstLength_ = firstLength;
        secondLength_ = secondLength;
    }

    public Orientation Orientation { get; }

    public EditorDockNodeViewModel First
    {
        get => first_;
        set => SetProperty(ref first_, value);
    }

    public EditorDockNodeViewModel Second
    {
        get => second_;
        set => SetProperty(ref second_, value);
    }

    public GridLength FirstLength
    {
        get => firstLength_;
        set => SetProperty(ref firstLength_, value);
    }

    public GridLength SecondLength
    {
        get => secondLength_;
        set => SetProperty(ref secondLength_, value);
    }

    public bool IsHorizontal => Orientation == Orientation.Horizontal;

    public bool IsVertical => Orientation == Orientation.Vertical;
}
