using System;
using Asharia.Studio.Presentation.Avalonia.Diagnostics;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Shell.Docking.Panels;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.Views.Docking;

namespace Editor.Shell.Composition;

internal sealed class StudioViewportDockSmokeLayout
{
    private StudioViewportDockSmokeLayout(
        Grid root,
        ColumnDefinition first,
        ColumnDefinition second,
        EditorDockSplitNodeViewModel split,
        EditorDockStagedGridSplitter splitter)
    {
        Root = root;
        First = first;
        Second = second;
        Split = split;
        Splitter = splitter;
    }

    public Grid Root { get; }

    public ColumnDefinition First { get; }

    public ColumnDefinition Second { get; }

    public EditorDockSplitNodeViewModel Split { get; }

    public EditorDockStagedGridSplitter Splitter { get; }

    public static StudioViewportDockSmokeLayout Create(
        Control firstContent,
        Control? secondContent = null,
        double firstWidth = 640)
    {
        ArgumentNullException.ThrowIfNull(firstContent);
        secondContent ??= new Border();
        var firstWindow = new EditorDockWindowViewModel(
            "transaction-first-window",
            "First",
            EditorDockArea.Center,
            "first");
        var secondWindow = new EditorDockWindowViewModel(
            "transaction-second-window",
            "Second",
            EditorDockArea.Right,
            "second");
        var split = new EditorDockSplitNodeViewModel(
            "transaction-split",
            Orientation.Horizontal,
            new EditorDockWindowNodeViewModel("transaction-first-node", firstWindow),
            new EditorDockWindowNodeViewModel("transaction-second-node", secondWindow),
            new GridLength(firstWidth, GridUnitType.Pixel),
            new GridLength(1, GridUnitType.Star));
        var first = new ColumnDefinition(split.FirstLength);
        var second = new ColumnDefinition(split.SecondLength);
        var root = new Grid { DataContext = split };
        root.ColumnDefinitions.Add(first);
        root.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(5)));
        root.ColumnDefinitions.Add(second);
        root.Children.Add(firstContent);
        var splitter = (EditorDockStagedGridSplitter)EditorDockSplitNodeView.CreateSplitter(
            split,
            GridResizeDirection.Columns,
            "vertical",
            StudioAvaloniaDiagnosticHubResolver.RequireCurrent());
        Grid.SetColumn(splitter, 1);
        root.Children.Add(splitter);
        Grid.SetColumn(secondContent, 2);
        root.Children.Add(secondContent);
        return new StudioViewportDockSmokeLayout(root, first, second, split, splitter);
    }
}
