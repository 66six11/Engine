using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Editor.Core.CodeFirstUI;

namespace Editor.Shell.CodeFirstUI.Adapters;

internal sealed class GuiAvaloniaControlFactory
{
    private readonly IGuiAvaloniaHost host_;

    public GuiAvaloniaControlFactory(IGuiAvaloniaHost host)
    {
        host_ = host ?? throw new ArgumentNullException(nameof(host));
    }

    public Control Build(GuiTreeSnapshot tree)
    {
        ArgumentNullException.ThrowIfNull(tree);

        return BuildRoot(tree.Root);
    }

    private Control BuildRoot(GuiNode node)
    {
        return node.Children.Count == 1
            ? BuildNode(node.Children[0])
            : BuildStack(node.Children, Orientation.Vertical, "code-first-root");
    }

    private Control BuildNode(GuiNode node)
    {
        return node.Kind switch
        {
            GuiNodeKind.Root => BuildRoot(node),
            GuiNodeKind.Vertical => BuildStack(node.Children, Orientation.Vertical, "code-first-vertical"),
            GuiNodeKind.Horizontal => BuildStack(node.Children, Orientation.Horizontal, "code-first-horizontal"),
            GuiNodeKind.Toolbar => BuildStack(node.Children, Orientation.Horizontal, "code-first-toolbar"),
            GuiNodeKind.Panel => BuildPanel(node),
            GuiNodeKind.Split => BuildSplit(node),
            GuiNodeKind.Label => BuildLabel(node),
            GuiNodeKind.Button => BuildButton(node),
            GuiNodeKind.List => BuildList(node),
            _ => BuildUnsupportedNode(node),
        };
    }

    private Control BuildStack(
        IReadOnlyList<GuiNode> children,
        Orientation orientation,
        string className)
    {
        var stack = new StackPanel
        {
            Orientation = orientation,
            Spacing = orientation == Orientation.Horizontal ? 6d : 8d,
        };
        stack.Classes.Add(className);

        foreach (var child in children)
        {
            stack.Children.Add(BuildNode(child));
        }

        return stack;
    }

    private Control BuildPanel(GuiNode node)
    {
        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
        grid.RowDefinitions.Add(new RowDefinition(new GridLength(1d, GridUnitType.Star)));

        var title = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            TextWrapping = Avalonia.Media.TextWrapping.NoWrap,
        };
        title.Classes.Add("code-first-panel-title");
        Grid.SetRow(title, 0);
        grid.Children.Add(title);

        var body = BuildStack(node.Children, Orientation.Vertical, "code-first-panel-body");
        Grid.SetRow(body, 1);
        grid.Children.Add(body);

        var border = new Border
        {
            Child = grid,
            Padding = new Avalonia.Thickness(10d),
        };
        border.Classes.Add("code-first-panel");
        return border;
    }

    private Control BuildSplit(GuiNode node)
    {
        var first = node.Children.Count > 0
            ? BuildNode(node.Children[0])
            : CreateEmptyNode();
        var second = node.Children.Count > 1
            ? BuildNode(node.Children[1])
            : CreateEmptyNode();
        var ratio = node.Payload.SplitRatio ?? 0.5d;
        ratio = Math.Clamp(ratio, 0.05d, 0.95d);

        var grid = new Grid();
        grid.Classes.Add("code-first-split");

        if (node.Payload.SplitDirection == GuiSplitDirection.Vertical)
        {
            grid.RowDefinitions.Add(new RowDefinition(new GridLength(ratio, GridUnitType.Star)));
            grid.RowDefinitions.Add(new RowDefinition(new GridLength(1d - ratio, GridUnitType.Star)));
            Grid.SetRow(first, 0);
            Grid.SetRow(second, 1);
        }
        else
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(ratio, GridUnitType.Star)));
            grid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(1d - ratio, GridUnitType.Star)));
            Grid.SetColumn(first, 0);
            Grid.SetColumn(second, 1);
        }

        grid.Children.Add(first);
        grid.Children.Add(second);
        return grid;
    }

    private static Control BuildLabel(GuiNode node)
    {
        var textBlock = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            TextWrapping = Avalonia.Media.TextWrapping.Wrap,
        };
        textBlock.Classes.Add("code-first-label");
        return textBlock;
    }

    private Control BuildButton(GuiNode node)
    {
        var button = new Button
        {
            Content = node.Label ?? node.Id.KeyPath,
            HorizontalAlignment = HorizontalAlignment.Left,
        };
        button.Classes.Add("code-first-button");
        button.Click += (_, _) => host_.ClickButton(node.Id);
        return button;
    }

    private Control BuildList(GuiNode node)
    {
        var listItems = node.Payload.ListItems
            .Select(item => new GuiAvaloniaListItem(item.Id, item.Label))
            .ToArray();
        var listBox = new ListBox
        {
            ItemTemplate = new FuncDataTemplate<GuiAvaloniaListItem>((item, _) =>
            {
                var label = new TextBlock
                {
                    Text = item?.Label ?? string.Empty,
                    TextWrapping = Avalonia.Media.TextWrapping.NoWrap,
                };
                label.Classes.Add("code-first-list-item-label");
                return label;
            }),
            ItemsSource = listItems,
            SelectionMode = SelectionMode.Single,
            SelectedItem = listItems.FirstOrDefault(
                item => string.Equals(item.Id, node.Payload.SelectedItemId, StringComparison.Ordinal)),
        };
        listBox.Classes.Add("code-first-list");

        listBox.SelectionChanged += (_, _) =>
        {
            if (listBox.SelectedItem is GuiAvaloniaListItem item)
            {
                host_.SelectListItem(node.Id, item.Id);
            }
        };

        return listBox;
    }

    private static Control BuildUnsupportedNode(GuiNode node)
    {
        var textBlock = new TextBlock
        {
            Text = $"{node.Kind}: {node.Label ?? node.Id.KeyPath}",
            TextWrapping = Avalonia.Media.TextWrapping.Wrap,
        };
        textBlock.Classes.Add("code-first-unsupported");
        return textBlock;
    }

    private static Control CreateEmptyNode()
    {
        var textBlock = new TextBlock
        {
            Text = string.Empty,
        };
        textBlock.Classes.Add("code-first-empty");
        return textBlock;
    }

    private sealed record GuiAvaloniaListItem(
        string Id,
        string Label);
}
