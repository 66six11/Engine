using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockDragStateViewModel : ViewModelBase
{
    private bool isActive_;
    private string title_ = string.Empty;
    private string tag_ = string.Empty;
    private string dropLabel_ = string.Empty;
    private EditorDockTabViewModel? draggedTab_;
    private DockArea? dropArea_;
    private double adornerX_;
    private double adornerY_;
    private double previewX_;
    private double previewY_;
    private double previewWidth_;
    private double previewHeight_;

    public bool IsActive
    {
        get => isActive_;
        private set => SetProperty(ref isActive_, value);
    }

    public string Title
    {
        get => title_;
        private set => SetProperty(ref title_, value);
    }

    public string Tag
    {
        get => tag_;
        private set => SetProperty(ref tag_, value);
    }

    public string DropLabel
    {
        get => dropLabel_;
        private set => SetProperty(ref dropLabel_, value);
    }

    public EditorDockTabViewModel? DraggedTab
    {
        get => draggedTab_;
        private set => SetProperty(ref draggedTab_, value);
    }

    public DockArea? DropArea
    {
        get => dropArea_;
        private set => SetProperty(ref dropArea_, value);
    }

    public double AdornerX
    {
        get => adornerX_;
        private set => SetProperty(ref adornerX_, value);
    }

    public double AdornerY
    {
        get => adornerY_;
        private set => SetProperty(ref adornerY_, value);
    }

    public double PreviewX
    {
        get => previewX_;
        private set => SetProperty(ref previewX_, value);
    }

    public double PreviewY
    {
        get => previewY_;
        private set => SetProperty(ref previewY_, value);
    }

    public double PreviewWidth
    {
        get => previewWidth_;
        private set => SetProperty(ref previewWidth_, value);
    }

    public double PreviewHeight
    {
        get => previewHeight_;
        private set => SetProperty(ref previewHeight_, value);
    }

    public void Begin(EditorDockTabViewModel tab, double x, double y)
    {
        DraggedTab = tab;
        Title = tab.Title;
        Tag = tab.Tag;
        IsActive = true;
        UpdatePointer(x, y);
    }

    public void UpdatePointer(double x, double y)
    {
        AdornerX = x + 14;
        AdornerY = y + 12;
    }

    public void UpdateDropPreview(DockArea area, double x, double y, double width, double height)
    {
        DropArea = area;
        DropLabel = area switch
        {
            DockArea.Left => "Dock into Left pane",
            DockArea.Right => "Dock into Right pane",
            DockArea.Bottom => "Dock into Bottom pane",
            _ => "Dock into Center workspace",
        };
        PreviewX = x;
        PreviewY = y;
        PreviewWidth = width;
        PreviewHeight = height;
    }

    public void Clear()
    {
        IsActive = false;
        Title = string.Empty;
        Tag = string.Empty;
        DropLabel = string.Empty;
        DraggedTab = null;
        DropArea = null;
        AdornerX = 0;
        AdornerY = 0;
        PreviewX = 0;
        PreviewY = 0;
        PreviewWidth = 0;
        PreviewHeight = 0;
    }
}
