using Avalonia.Controls;
using Avalonia.Input;
using Editor.Features.Hierarchy.ViewModels;

namespace Editor.Features.Hierarchy.Views;

public partial class HierarchyPanelView : UserControl
{
    public HierarchyPanelView()
    {
        InitializeComponent();
    }

    private void OnHierarchyRowDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: HierarchyNodeRowViewModel { HasChildren: true } row })
        {
            row.ToggleExpandedCommand.Execute(row);
            e.Handled = true;
        }
    }
}
