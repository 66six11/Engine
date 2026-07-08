using System.Collections.Generic;
using System.Collections.ObjectModel;
using Editor.Core.Models.Panels;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Docking;

public sealed class EditorDockWindowViewModel : ViewModelBase
{
    private readonly HashSet<EditorDockTabViewModel> tabs_ = [];
    private readonly Dictionary<EditorDockTabViewModel, EditorDockTabStripItemViewModel> tabStripItemsByTab_ = [];
    private EditorDockTabViewModel? activeTab_;
    private bool isHostFocused_ = true;
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
    private bool tabInsertPlaceholderShowsTab_;
    private EditorDockTabStripItemViewModel? tabInsertPlaceholderItem_;
    private EditorDockTabViewModel? hiddenDragSourceTab_;

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

    public bool IsHostFocused
    {
        get => isHostFocused_;
        private set => SetProperty(ref isHostFocused_, value);
    }

    public bool IsDragSourceWindow
    {
        get => isDragSourceWindow_;
        private set => SetProperty(ref isDragSourceWindow_, value);
    }

    public int TabCount => Tabs.Count;

    public bool HasTabs => TabCount > 0;

    internal int? TabInsertPlaceholderIndex => tabInsertPlaceholderIndex_;

    public void Add(EditorDockTabViewModel tab)
    {
        tab.Area = Area;
        Tabs.Add(tab);
        tabs_.Add(tab);
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
        tabs_.Add(tab);
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

        tabs_.Remove(tab);
        tab.IsActive = false;
        tab.SetDragSourceState(false);
        NotifyTabCompositionChanged();
        RebuildTabStripItems();

        if (ReferenceEquals(ActiveTab, tab))
        {
            ActiveTab = Tabs.Count > 0 ? Tabs[0] : null;

            if (ActiveTab is not null)
            {
                ActiveTab.IsActive = true;
            }

            RefreshTabSelectionStates();
        }
    }

    internal void ResetTabs()
    {
        foreach (var tab in Tabs)
        {
            tab.IsActive = false;
            tab.SetDragSourceState(false);
        }

        Tabs.Clear();
        tabs_.Clear();
        TabStripItems.Clear();
        tabStripItemsByTab_.Clear();
        tabInsertPlaceholderTab_ = null;
        tabInsertPlaceholderIndex_ = null;
        tabInsertPlaceholderShowsTab_ = false;
        tabInsertPlaceholderItem_ = null;
        hiddenDragSourceTab_ = null;
        ActiveTab = null;
        NotifyTabCompositionChanged();
    }

    public void Activate(EditorDockTabViewModel tab)
    {
        foreach (var item in Tabs)
        {
            item.IsActive = ReferenceEquals(item, tab);
        }

        ActiveTab = tab;
        RefreshTabSelectionStates();
    }

    internal void SetActiveWindowState(bool isActiveWindow)
    {
        IsActiveWindow = isActiveWindow;
        UpdateTabStripWindowFocusState();
    }

    internal void SetHostFocusState(bool isHostFocused)
    {
        IsHostFocused = isHostFocused;
        UpdateTabStripWindowFocusState();
    }

    internal void SetDragSourceWindowState(bool isDragSourceWindow)
    {
        IsDragSourceWindow = isDragSourceWindow;
    }

    internal bool ShowTabInsertPlaceholder(EditorDockTabViewModel tab, int index, bool showsTab)
    {
        var insertIndex = System.Math.Clamp(index, 0, Tabs.Count);
        if (IsTabInsertPlaceholderCurrent(tab, insertIndex, showsTab))
        {
            return false;
        }

        tabInsertPlaceholderTab_ = tab;
        tabInsertPlaceholderIndex_ = insertIndex;
        tabInsertPlaceholderShowsTab_ = showsTab;
        if (!tabs_.Contains(tab))
        {
            tabInsertPlaceholderItem_ = ReferenceEquals(tabInsertPlaceholderItem_?.Tab, tab)
                ? tabInsertPlaceholderItem_
                : new EditorDockTabStripItemViewModel(tab, isPlaceholder: true, isPreview: showsTab);
            tabInsertPlaceholderItem_.SetWindowFocusState(IsTabStripFocused);
            tabInsertPlaceholderItem_.SetPresentation(isPlaceholder: true, isPreview: showsTab);
        }

        RebuildTabStripItems();
        return true;
    }

    internal bool IsLocalTabReorderPreviewCurrent(EditorDockTabViewModel tab, int index, bool showsTab)
    {
        return ReferenceEquals(hiddenDragSourceTab_, tab)
            && IsTabInsertPlaceholderCurrent(tab, index, showsTab);
    }

    internal bool ShowLocalTabReorderPreview(EditorDockTabViewModel tab, int index, bool showsTab)
    {
        if (!tabs_.Contains(tab))
        {
            return false;
        }

        var insertIndex = System.Math.Clamp(index, 0, Tabs.Count);
        if (ReferenceEquals(hiddenDragSourceTab_, tab)
            && IsTabInsertPlaceholderCurrent(tab, insertIndex, showsTab))
        {
            return false;
        }

        hiddenDragSourceTab_ = tab;
        tabInsertPlaceholderTab_ = tab;
        tabInsertPlaceholderIndex_ = insertIndex;
        tabInsertPlaceholderShowsTab_ = showsTab;
        tabInsertPlaceholderItem_ = null;
        RebuildTabStripItems();
        return true;
    }

    internal bool IsTabInsertPlaceholderCurrent(EditorDockTabViewModel tab, int index, bool showsTab)
    {
        var insertIndex = System.Math.Clamp(index, 0, Tabs.Count);
        return ReferenceEquals(tabInsertPlaceholderTab_, tab)
            && tabInsertPlaceholderIndex_ == insertIndex
            && tabInsertPlaceholderShowsTab_ == showsTab;
    }

    internal bool ContainsTab(EditorDockTabViewModel tab)
    {
        return tabs_.Contains(tab);
    }

    internal bool HideDragSourceTab(EditorDockTabViewModel tab)
    {
        if (!tabs_.Contains(tab) || ReferenceEquals(hiddenDragSourceTab_, tab))
        {
            return false;
        }

        hiddenDragSourceTab_ = tab;
        RebuildTabStripItems();
        return true;
    }

    internal bool ClearHiddenDragSourceTab()
    {
        if (hiddenDragSourceTab_ is null)
        {
            return false;
        }

        hiddenDragSourceTab_ = null;
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
        tabInsertPlaceholderShowsTab_ = false;
        tabInsertPlaceholderItem_ = null;
        RebuildTabStripItems();
        return true;
    }

    internal bool ClearLocalTabReorderPreview()
    {
        if (hiddenDragSourceTab_ is null
            && tabInsertPlaceholderTab_ is null
            && tabInsertPlaceholderIndex_ is null)
        {
            return false;
        }

        hiddenDragSourceTab_ = null;
        tabInsertPlaceholderTab_ = null;
        tabInsertPlaceholderIndex_ = null;
        tabInsertPlaceholderShowsTab_ = false;
        tabInsertPlaceholderItem_ = null;
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
        ResetCachedTabStripItems();
        var targetItems = new List<EditorDockTabStripItemViewModel>(
            Tabs.Count + (tabInsertPlaceholderTab_ is null ? 0 : 1));
        var hasLocalPlaceholder = tabInsertPlaceholderTab_ is not null
            && tabs_.Contains(tabInsertPlaceholderTab_);
        EditorDockTabStripItemViewModel? localPlaceholderItem = null;
        if (hasLocalPlaceholder && tabInsertPlaceholderTab_ is not null)
        {
            localPlaceholderItem = GetOrCreateTabStripItem(tabInsertPlaceholderTab_);
            localPlaceholderItem.SetWindowFocusState(IsTabStripFocused);
            localPlaceholderItem.SetPresentation(
                isPlaceholder: true,
                isPreview: tabInsertPlaceholderShowsTab_);
        }
        else if (tabInsertPlaceholderItem_ is not null)
        {
            tabInsertPlaceholderItem_.SetWindowFocusState(IsTabStripFocused);
            tabInsertPlaceholderItem_.SetPresentation(
                isPlaceholder: true,
                isPreview: tabInsertPlaceholderShowsTab_);
        }

        for (var index = 0; index <= Tabs.Count; index++)
        {
            if (tabInsertPlaceholderTab_ is not null
                && tabInsertPlaceholderIndex_ == index)
            {
                targetItems.Add(localPlaceholderItem
                    ?? tabInsertPlaceholderItem_
                    ?? CreateTabStripItem(
                        tabInsertPlaceholderTab_,
                        isPlaceholder: true,
                        isPreview: tabInsertPlaceholderShowsTab_));
            }

            if (index < Tabs.Count)
            {
                var tab = Tabs[index];
                if (hasLocalPlaceholder && ReferenceEquals(tab, tabInsertPlaceholderTab_))
                {
                    continue;
                }

                if (ReferenceEquals(tab, hiddenDragSourceTab_))
                {
                    continue;
                }

                var item = GetOrCreateTabStripItem(tab);
                targetItems.Add(item);
            }
        }

        SyncTabStripItems(targetItems);
        RemoveStaleTabStripItems();
    }

    private EditorDockTabStripItemViewModel GetOrCreateTabStripItem(EditorDockTabViewModel tab)
    {
        if (tabStripItemsByTab_.TryGetValue(tab, out var item))
        {
            item.SetWindowFocusState(IsTabStripFocused);
            return item;
        }

        item = CreateTabStripItem(tab, isPlaceholder: false);
        tabStripItemsByTab_.Add(tab, item);
        return item;
    }

    private EditorDockTabStripItemViewModel CreateTabStripItem(
        EditorDockTabViewModel tab,
        bool isPlaceholder,
        bool isPreview = false,
        bool isSourceGhost = false)
    {
        var item = new EditorDockTabStripItemViewModel(
            tab,
            isPlaceholder,
            isPreview,
            isSourceGhost);
        item.SetWindowFocusState(IsTabStripFocused);
        return item;
    }

    private void ResetCachedTabStripItems()
    {
        foreach (var item in tabStripItemsByTab_.Values)
        {
            item.SetPresentation(isPlaceholder: false);
        }
    }

    private void SyncTabStripItems(IReadOnlyList<EditorDockTabStripItemViewModel> targetItems)
    {
        var currentIndices = new Dictionary<EditorDockTabStripItemViewModel, int>(
            ReferenceEqualityComparer.Instance);
        for (var index = 0; index < TabStripItems.Count; index++)
        {
            currentIndices[TabStripItems[index]] = index;
        }

        for (var targetIndex = 0; targetIndex < targetItems.Count; targetIndex++)
        {
            var targetItem = targetItems[targetIndex];
            if (targetIndex < TabStripItems.Count
                && ReferenceEquals(TabStripItems[targetIndex], targetItem))
            {
                continue;
            }

            if (currentIndices.TryGetValue(targetItem, out var existingIndex))
            {
                TabStripItems.Move(existingIndex, targetIndex);
                RefreshTabStripItemIndices(
                    currentIndices,
                    System.Math.Min(existingIndex, targetIndex),
                    System.Math.Max(existingIndex, targetIndex));
                continue;
            }

            if (targetIndex == TabStripItems.Count)
            {
                TabStripItems.Add(targetItem);
                currentIndices[targetItem] = targetIndex;
                continue;
            }

            TabStripItems.Insert(targetIndex, targetItem);
            RefreshTabStripItemIndices(currentIndices, targetIndex, TabStripItems.Count - 1);
        }

        while (TabStripItems.Count > targetItems.Count)
        {
            var lastIndex = TabStripItems.Count - 1;
            currentIndices.Remove(TabStripItems[lastIndex]);
            TabStripItems.RemoveAt(lastIndex);
        }
    }

    private void RefreshTabStripItemIndices(
        Dictionary<EditorDockTabStripItemViewModel, int> currentIndices,
        int startIndex,
        int endIndex)
    {
        for (var index = startIndex; index <= endIndex; index++)
        {
            currentIndices[TabStripItems[index]] = index;
        }
    }

    private void RemoveStaleTabStripItems()
    {
        var removedTabs = new List<EditorDockTabViewModel>();
        foreach (var pair in tabStripItemsByTab_)
        {
            if (!tabs_.Contains(pair.Key))
            {
                removedTabs.Add(pair.Key);
            }
        }

        foreach (var removedTab in removedTabs)
        {
            tabStripItemsByTab_.Remove(removedTab);
        }
    }

    private void UpdateTabStripWindowFocusState()
    {
        foreach (var item in tabStripItemsByTab_.Values)
        {
            item.SetWindowFocusState(IsTabStripFocused);
        }

        tabInsertPlaceholderItem_?.SetWindowFocusState(IsTabStripFocused);
    }

    private void RefreshTabSelectionStates()
    {
        foreach (var item in tabStripItemsByTab_.Values)
        {
            item.RefreshSelectionState();
        }

        tabInsertPlaceholderItem_?.RefreshSelectionState();
    }

    private bool IsTabStripFocused => IsActiveWindow && IsHostFocused;
}
