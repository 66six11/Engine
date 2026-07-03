using System;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Editor.Core.CodeFirstUI;
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
        if (string.Equals(e.PropertyName, nameof(CodeFirstPanelHostViewModel.CurrentTree), StringComparison.Ordinal)
            || string.Equals(e.PropertyName, nameof(CodeFirstPanelHostViewModel.LastBuildErrorMessage), StringComparison.Ordinal)
            || string.Equals(e.PropertyName, nameof(CodeFirstPanelHostViewModel.LastValidationResult), StringComparison.Ordinal))
        {
            RebuildContent();
        }
    }

    private void RebuildContent()
    {
        if (viewModel_ is not { } viewModel)
        {
            GuiHost.Content = null;
            return;
        }

        var statusContent = CreateStatusContent(viewModel);
        var treeContent = viewModel.CurrentTree is { } tree
            ? new GuiAvaloniaControlFactory(viewModel).Build(tree)
            : null;

        if (statusContent is null)
        {
            GuiHost.Content = treeContent;
            return;
        }

        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        grid.RowDefinitions.Add(new RowDefinition(new GridLength(1, GridUnitType.Star)));
        grid.Children.Add(statusContent);
        Grid.SetRow(statusContent, 0);

        if (treeContent is not null)
        {
            grid.Children.Add(treeContent);
            Grid.SetRow(treeContent, 1);
        }

        GuiHost.Content = grid;
    }

    private static Control? CreateStatusContent(CodeFirstPanelHostViewModel viewModel)
    {
        if (viewModel.HasBuildError)
        {
            return CreatePlaceholder(
                "Code-first panel failed",
                viewModel.LastBuildErrorMessage ?? "Unknown build failure.");
        }

        if (viewModel.HasValidationErrors)
        {
            return CreatePlaceholder(
                "Code-first panel rejected invalid tree",
                FormatValidationErrors(viewModel.LastValidationResult));
        }

        return null;
    }

    private static Control CreatePlaceholder(string title, string message)
    {
        var titleBlock = new TextBlock
        {
            Text = title,
        };
        titleBlock.Classes.Add("code-first-placeholder-title");

        var messageBlock = new TextBlock
        {
            Text = message,
            TextWrapping = TextWrapping.Wrap,
        };
        messageBlock.Classes.Add("code-first-placeholder-message");

        var content = new StackPanel
        {
            Spacing = 4,
        };
        content.Children.Add(titleBlock);
        content.Children.Add(messageBlock);

        var border = new Border
        {
            Padding = new Thickness(10, 8),
            Margin = new Thickness(0, 0, 0, 8),
            Child = content,
        };
        border.Classes.Add("code-first-placeholder");

        return border;
    }

    private static string FormatValidationErrors(GuiTreeValidationResult validationResult)
    {
        return string.Join(
            Environment.NewLine,
            validationResult.Errors.Select(error => $"{error.Code}: {error.Message}"));
    }
}
