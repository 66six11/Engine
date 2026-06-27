using System;
using System.ComponentModel;
using Avalonia.Controls;
using Editor.Shell.CodeFirstUI.Adapters;

namespace Editor.Shell.CodeFirstUI.Views;

public partial class CodeFirstPanelHostView : UserControl
{
    private CodeFirstPanelHostViewModel? viewModel_;

    public CodeFirstPanelHostView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (viewModel_ is not null)
        {
            viewModel_.PropertyChanged -= OnViewModelPropertyChanged;
        }

        viewModel_ = DataContext as CodeFirstPanelHostViewModel;
        if (viewModel_ is not null)
        {
            viewModel_.PropertyChanged += OnViewModelPropertyChanged;
        }

        RebuildContent();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(CodeFirstPanelHostViewModel.CurrentTree), StringComparison.Ordinal))
        {
            RebuildContent();
        }
    }

    private void RebuildContent()
    {
        if (viewModel_?.CurrentTree is not { } tree)
        {
            GuiHost.Content = null;
            return;
        }

        GuiHost.Content = new GuiAvaloniaControlFactory(viewModel_).Build(tree);
    }
}
