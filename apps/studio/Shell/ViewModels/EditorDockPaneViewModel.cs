using System.Collections.ObjectModel;
using Editor.Core.Models;

namespace Editor.Shell.ViewModels;

public sealed class EditorDockPaneViewModel : ViewModelBase
{
    private EditorDockTabViewModel? activeTab_;

    public EditorDockPaneViewModel(string id, string title, DockArea area, string role)
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

    public EditorDockTabViewModel? ActiveTab
    {
        get => activeTab_;
        private set => SetProperty(ref activeTab_, value);
    }

    public string TabCountText => Tabs.Count.ToString(System.Globalization.CultureInfo.InvariantCulture);

    public void Add(EditorDockTabViewModel tab)
    {
        tab.Area = Area;
        Tabs.Add(tab);
        OnPropertyChanged(nameof(TabCountText));

        if (ActiveTab is null)
        {
            Activate(tab);
        }
    }

    public void Remove(EditorDockTabViewModel tab)
    {
        if (!Tabs.Remove(tab))
        {
            return;
        }

        OnPropertyChanged(nameof(TabCountText));

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
}
