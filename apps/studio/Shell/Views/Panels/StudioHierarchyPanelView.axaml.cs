using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Editor.Shell.ViewModels.Panels;

namespace Editor.Shell.Views.Panels;

public partial class StudioHierarchyPanelView : UserControl
{
    public StudioHierarchyPanelView()
    {
        InitializeComponent();
    }

    private void OnHierarchyExpanderClick(object? sender, RoutedEventArgs e)
    {
        if (sender is Button { DataContext: StudioHierarchyRowViewModel row }
            && DataContext is StudioHierarchyPanelViewModel viewModel)
        {
            viewModel.ToggleExpanded(row);
            e.Handled = true;
        }
    }

    private void OnHierarchyRowDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: StudioHierarchyRowViewModel row }
            && DataContext is StudioHierarchyPanelViewModel viewModel
            && row.HasChildren)
        {
            viewModel.ToggleExpanded(row);
            e.Handled = true;
        }
    }
}
