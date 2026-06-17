using System.Collections.ObjectModel;
using Editor.Core.Models;
using Editor.Shell.Docking;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockDragStateViewModel : ViewModelBase
{
    private bool isActive_;
    private string dropLabel_ = string.Empty;
    private EditorDockTabViewModel? draggedTab_;
    private DockArea? dropArea_;
    private EditorDockDropOperation dropOperation_ = EditorDockDropOperation.Reject;
    private EditorDockDropGuideKind guideKind_ = EditorDockDropGuideKind.None;
    private string guideTitle_ = string.Empty;
    private string guideDetail_ = string.Empty;
    private double guideX_;
    private double guideY_;
    private double guideWidth_;
    private double guideHeight_;
    private bool insertGuideVertical_;
    private double previewX_;
    private double previewY_;
    private double previewWidth_;
    private double previewHeight_;

    public bool IsActive
    {
        get => isActive_;
        private set
        {
            if (SetProperty(ref isActive_, value))
            {
                OnGuideVisibilityChanged();
            }
        }
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

    public EditorDockDropOperation DropOperation
    {
        get => dropOperation_;
        private set
        {
            if (SetProperty(ref dropOperation_, value))
            {
                OnGuideVisibilityChanged();
            }
        }
    }

    public EditorDockDropGuideKind GuideKind
    {
        get => guideKind_;
        private set
        {
            if (SetProperty(ref guideKind_, value))
            {
                OnGuideVisibilityChanged();
            }
        }
    }

    public string GuideTitle
    {
        get => guideTitle_;
        private set => SetProperty(ref guideTitle_, value);
    }

    public string GuideDetail
    {
        get => guideDetail_;
        private set => SetProperty(ref guideDetail_, value);
    }

    public double GuideX
    {
        get => guideX_;
        private set => SetProperty(ref guideX_, value);
    }

    public double GuideY
    {
        get => guideY_;
        private set => SetProperty(ref guideY_, value);
    }

    public double GuideWidth
    {
        get => guideWidth_;
        private set => SetProperty(ref guideWidth_, value);
    }

    public double GuideHeight
    {
        get => guideHeight_;
        private set => SetProperty(ref guideHeight_, value);
    }

    public bool InsertGuideVertical
    {
        get => insertGuideVertical_;
        private set
        {
            if (SetProperty(ref insertGuideVertical_, value))
            {
                OnPropertyChanged(nameof(InsertGuideHorizontal));
            }
        }
    }

    public bool InsertGuideHorizontal => !InsertGuideVertical;

    public bool IsDropGuideVisible =>
        IsActive
        && GuideKind is not EditorDockDropGuideKind.None
        && GuideKind is not EditorDockDropGuideKind.Reject
        && DropOperation != EditorDockDropOperation.InsertTabAtIndex;

    public bool IsMergeGuideVisible => IsDropGuideVisible && GuideKind == EditorDockDropGuideKind.Merge;

    public bool IsInsertGuideVisible => IsDropGuideVisible && GuideKind == EditorDockDropGuideKind.Insert;

    public bool IsWindowInsertGuideVisible => IsInsertGuideVisible;

    public bool IsFloatGuideVisible => IsDropGuideVisible && GuideKind == EditorDockDropGuideKind.Float;

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

    public ObservableCollection<EditorDockTabStripItemViewModel> PreviewTabItems { get; } = [];

    public void Begin(EditorDockTabViewModel tab)
    {
        DraggedTab = tab;
        PreviewTabItems.Clear();
        PreviewTabItems.Add(new EditorDockTabStripItemViewModel(tab, isPlaceholder: false, isPreview: true));
        IsActive = true;
    }

    public void UpdateDropPreview(EditorDockDropTarget target)
    {
        DropArea = target.TargetArea;
        DropOperation = target.Operation;
        GuideKind = target.GuideKind;
        DropLabel = target.Label;
        GuideTitle = GetGuideTitle(target.GuideKind, target.Operation);
        GuideDetail = target.Label;
        GuideX = target.PreviewBounds.X;
        GuideY = target.PreviewBounds.Y;
        GuideWidth = target.PreviewBounds.Width;
        GuideHeight = target.PreviewBounds.Height;
        InsertGuideVertical = target.PreviewBounds.Width <= target.PreviewBounds.Height;
        PreviewX = target.PreviewBounds.X;
        PreviewY = target.PreviewBounds.Y;
        PreviewWidth = target.PreviewBounds.Width;
        PreviewHeight = target.PreviewBounds.Height;
    }

    public void ClearDropPreview()
    {
        DropArea = null;
        DropOperation = EditorDockDropOperation.Reject;
        GuideKind = EditorDockDropGuideKind.None;
        DropLabel = string.Empty;
        GuideTitle = string.Empty;
        GuideDetail = string.Empty;
        GuideX = 0;
        GuideY = 0;
        GuideWidth = 0;
        GuideHeight = 0;
        InsertGuideVertical = false;
        PreviewX = 0;
        PreviewY = 0;
        PreviewWidth = 0;
        PreviewHeight = 0;
    }

    public void Clear()
    {
        IsActive = false;
        DraggedTab = null;
        PreviewTabItems.Clear();
        ClearDropPreview();
    }

    private static string GetGuideTitle(
        EditorDockDropGuideKind guideKind,
        EditorDockDropOperation operation)
    {
        if (guideKind == EditorDockDropGuideKind.Insert
            && operation == EditorDockDropOperation.InsertTabAtIndex)
        {
            return "Insert tab";
        }

        return guideKind switch
        {
            EditorDockDropGuideKind.Merge => "Merge as tab",
            EditorDockDropGuideKind.Insert => "Insert window",
            EditorDockDropGuideKind.Float => "Float window",
            _ => string.Empty,
        };
    }

    private void OnGuideVisibilityChanged()
    {
        OnPropertyChanged(nameof(IsDropGuideVisible));
        OnPropertyChanged(nameof(IsMergeGuideVisible));
        OnPropertyChanged(nameof(IsInsertGuideVisible));
        OnPropertyChanged(nameof(IsWindowInsertGuideVisible));
        OnPropertyChanged(nameof(IsFloatGuideVisible));
    }
}
