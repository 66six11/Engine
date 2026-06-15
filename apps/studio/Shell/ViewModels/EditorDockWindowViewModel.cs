using System.Collections.ObjectModel;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockWindowViewModel : ViewModelBase
{
    private EditorDockTabViewModel? activeTab_;
    private bool isActiveWindow_;
    private bool isDragSourceWindow_;

    public EditorDockWindowViewModel(
        string id,
        string title,
        DockArea area,
        string role)
    {
        Id = id;
        Title = title;
        Area = area;
        Role = role;
    }

    public string Id { get; }

    public string Title { get; }

    public DockArea Area { get; }

    public string Role { get; }

    public ObservableCollection<EditorDockTabViewModel> Tabs { get; } = [];

    public ObservableCollection<EditorDockTabStripItemViewModel> TabStripItems { get; } = [];

    private EditorDockTabViewModel? tabInsertPlaceholderTab_;
    private int? tabInsertPlaceholderIndex_;

    public EditorDockTabViewModel? ActiveTab
    {
        get => activeTab_;
        private set => SetProperty(ref activeTab_, value);
    }

    public bool IsActiveWindow
    {
        get => isActiveWindow_;
        private set => SetProperty(ref isActiveWindow_, value);
    }

    public bool IsDragSourceWindow
    {
        get => isDragSourceWindow_;
        private set => SetProperty(ref isDragSourceWindow_, value);
    }

    public int TabCount => Tabs.Count;

    public bool HasTabs => TabCount > 0;

    public void Add(EditorDockTabViewModel tab)
    {
        tab.Area = Area;
        Tabs.Add(tab);
        NotifyTabCompositionChanged();
        RebuildTabStripItems();

        if (ActiveTab is null)
        {
            Activate(tab);
        }
    }

    public void Insert(EditorDockTabViewModel tab, int index)
    {
        tab.Area = Area;
        Tabs.Insert(System.Math.Clamp(index, 0, Tabs.Count), tab);
        NotifyTabCompositionChanged();
        RebuildTabStripItems();

        if (ActiveTab is null)
        {
            Activate(tab);
        }
    }

    public bool Move(EditorDockTabViewModel tab, int targetIndex)
    {
        var sourceIndex = Tabs.IndexOf(tab);
        if (sourceIndex < 0)
        {
            return false;
        }

        var insertIndex = System.Math.Clamp(targetIndex, 0, Tabs.Count);
        if (insertIndex > sourceIndex)
        {
            insertIndex--;
        }

        if (insertIndex == sourceIndex)
        {
            return false;
        }

        Tabs.Move(sourceIndex, insertIndex);
        NotifyTabCompositionChanged();
        RebuildTabStripItems();
        return true;
    }

    public void Remove(EditorDockTabViewModel tab)
    {
        if (!Tabs.Remove(tab))
        {
            return;
        }

        NotifyTabCompositionChanged();
        RebuildTabStripItems();

        if (ReferenceEquals(ActiveTab, tab))
        {
            ActiveTab = Tabs.Count > 0 ? Tabs[0] : null;

            if (ActiveTab is not null)
            {
                ActiveTab.IsActive = true;
            }
        }
    }

    public void Activate(EditorDockTabViewModel tab)
    {
        foreach (var item in Tabs)
        {
            item.IsActive = ReferenceEquals(item, tab);
        }

        ActiveTab = tab;
    }

    internal void SetActiveWindowState(bool isActiveWindow)
    {
        IsActiveWindow = isActiveWindow;
    }

    internal void SetDragSourceWindowState(bool isDragSourceWindow)
    {
        IsDragSourceWindow = isDragSourceWindow;
    }

    internal bool ShowTabInsertPlaceholder(EditorDockTabViewModel tab, int index)
    {
        var insertIndex = System.Math.Clamp(index, 0, Tabs.Count);
        if (ReferenceEquals(tabInsertPlaceholderTab_, tab)
            && tabInsertPlaceholderIndex_ == insertIndex)
        {
            return false;
        }

        tabInsertPlaceholderTab_ = tab;
        tabInsertPlaceholderIndex_ = insertIndex;
        RebuildTabStripItems();
        return true;
    }

    internal bool ClearTabInsertPlaceholder()
    {
        if (tabInsertPlaceholderTab_ is null && tabInsertPlaceholderIndex_ is null)
        {
            return false;
        }

        tabInsertPlaceholderTab_ = null;
        tabInsertPlaceholderIndex_ = null;
        RebuildTabStripItems();
        return true;
    }

    private void NotifyTabCompositionChanged()
    {
        OnPropertyChanged(nameof(TabCount));
        OnPropertyChanged(nameof(HasTabs));
    }

    private void RebuildTabStripItems()
    {
        TabStripItems.Clear();
        for (var index = 0; index <= Tabs.Count; index++)
        {
            if (tabInsertPlaceholderTab_ is not null
                && tabInsertPlaceholderIndex_ == index)
            {
                TabStripItems.Add(new EditorDockTabStripItemViewModel(tabInsertPlaceholderTab_, isPlaceholder: true));
            }

            if (index < Tabs.Count)
            {
                TabStripItems.Add(new EditorDockTabStripItemViewModel(Tabs[index], isPlaceholder: false));
            }
        }
    }
}
