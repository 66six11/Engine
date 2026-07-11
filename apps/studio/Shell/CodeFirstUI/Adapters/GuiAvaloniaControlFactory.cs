using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Editor.Core.CodeFirstUI.Models;
using Asharia.Editor.Diagnostics;
using Editor.UI.Icons;
using Editor.UI.Controls.Base;
using Editor.UI.Controls.Tree;

namespace Editor.Shell.CodeFirstUI.Adapters;

internal sealed class GuiAvaloniaControlFactory
{
    private const double SplitterBreadth = 4d;
    private static readonly TimeSpan DefaultDebounceDelay = TimeSpan.FromMilliseconds(250);
    private readonly IGuiAvaloniaHost host_;
    private readonly IGuiTextCommitScheduler textCommitScheduler_;

    public GuiAvaloniaControlFactory(
        IGuiAvaloniaHost host,
        IGuiTextCommitScheduler? textCommitScheduler = null)
    {
        host_ = host ?? throw new ArgumentNullException(nameof(host));
        textCommitScheduler_ = textCommitScheduler ?? DispatcherGuiTextCommitScheduler.Instance;
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
            GuiNodeKind.Foldout => BuildFoldout(node),
            GuiNodeKind.Split => BuildSplit(node),
            GuiNodeKind.NavigationView => BuildNavigationView(node),
            GuiNodeKind.Scroll => BuildScroll(node),
            GuiNodeKind.Label => BuildLabel(node),
            GuiNodeKind.Separator => BuildSeparator(),
            GuiNodeKind.Button => BuildButton(node),
            GuiNodeKind.Property => BuildProperty(node),
            GuiNodeKind.TextField => BuildTextField(node),
            GuiNodeKind.Toggle => BuildToggle(node),
            GuiNodeKind.ComboBox => BuildComboBox(node),
            GuiNodeKind.RadioGroup => BuildRadioGroup(node),
            GuiNodeKind.ColorField => BuildColorField(node),
            GuiNodeKind.Vector2Field => BuildVector2Field(node),
            GuiNodeKind.Vector3Field => BuildVector3Field(node),
            GuiNodeKind.Vector4Field => BuildVector4Field(node),
            GuiNodeKind.Slider => BuildSlider(node),
            GuiNodeKind.NumberInput => BuildNumberInput(node),
            GuiNodeKind.ProgressBar => BuildProgressBar(node),
            GuiNodeKind.List => BuildList(node),
            GuiNodeKind.ValidationMessage => BuildValidationMessage(node),
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
            TextWrapping = TextWrapping.NoWrap,
        };
        title.Classes.Add("code-first-panel-title");
        Grid.SetRow(title, 0);
        grid.Children.Add(title);

        var body = BuildPanelBody(node.Children);
        Grid.SetRow(body, 1);
        grid.Children.Add(body);

        var border = new Border
        {
            Child = grid,
            Padding = new Thickness(10d),
        };
        border.Classes.Add("code-first-panel");
        return border;
    }

    private Control BuildPanelBody(IReadOnlyList<GuiNode> children)
    {
        var grid = new Grid();
        grid.Classes.Add("code-first-panel-body");

        for (var index = 0; index < children.Count; index++)
        {
            var child = children[index];
            grid.RowDefinitions.Add(new RowDefinition(GetPanelBodyRowHeight(child)));

            var control = BuildNode(child);
            Grid.SetRow(control, index);
            grid.Children.Add(control);
        }

        return grid;
    }

    private static GridLength GetPanelBodyRowHeight(GuiNode child)
    {
        return IsPanelBodyFillChild(child.Kind)
            ? new GridLength(1d, GridUnitType.Star)
            : GridLength.Auto;
    }

    private static bool IsPanelBodyFillChild(GuiNodeKind kind)
    {
        return kind is GuiNodeKind.List
            or GuiNodeKind.Panel
            or GuiNodeKind.Split
            or GuiNodeKind.NavigationView
            or GuiNodeKind.Scroll;
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
            var firstRow = new RowDefinition(new GridLength(ratio, GridUnitType.Star));
            var secondRow = new RowDefinition(new GridLength(1d - ratio, GridUnitType.Star));
            grid.RowDefinitions.Add(firstRow);
            grid.RowDefinitions.Add(new RowDefinition(new GridLength(SplitterBreadth)));
            grid.RowDefinitions.Add(secondRow);
            var splitter = CreateSplitter(node, GridResizeDirection.Rows, "horizontal");
            AttachRowSplitRatioTracking(node.Id, firstRow, secondRow, grid);
            Grid.SetRow(first, 0);
            Grid.SetRow(splitter, 1);
            Grid.SetRow(second, 2);
            grid.Children.Add(first);
            grid.Children.Add(splitter);
            grid.Children.Add(second);
        }
        else
        {
            var firstColumn = new ColumnDefinition(new GridLength(ratio, GridUnitType.Star));
            var secondColumn = new ColumnDefinition(new GridLength(1d - ratio, GridUnitType.Star));
            grid.ColumnDefinitions.Add(firstColumn);
            grid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(SplitterBreadth)));
            grid.ColumnDefinitions.Add(secondColumn);
            var splitter = CreateSplitter(node, GridResizeDirection.Columns, "vertical");
            AttachColumnSplitRatioTracking(node.Id, firstColumn, secondColumn, grid);
            Grid.SetColumn(first, 0);
            Grid.SetColumn(splitter, 1);
            Grid.SetColumn(second, 2);
            grid.Children.Add(first);
            grid.Children.Add(splitter);
            grid.Children.Add(second);
        }

        return grid;
    }

    private Control BuildNavigationView(GuiNode node)
    {
        var ratio = node.Payload.SplitRatio ?? 0.30d;
        ratio = Math.Clamp(ratio, 0.05d, 0.95d);

        var directory = BuildNavigationDirectory(node);
        var content = BuildStack(node.Children, Orientation.Vertical, "code-first-navigation-content");
        var firstColumn = new ColumnDefinition(new GridLength(ratio, GridUnitType.Star));
        var secondColumn = new ColumnDefinition(new GridLength(1d - ratio, GridUnitType.Star));
        var splitter = CreateSplitter(node, GridResizeDirection.Columns, "vertical");

        var grid = new Grid();
        grid.Classes.Add("code-first-navigation-view");
        grid.ColumnDefinitions.Add(firstColumn);
        grid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(SplitterBreadth)));
        grid.ColumnDefinitions.Add(secondColumn);
        AttachColumnSplitRatioTracking(node.Id, firstColumn, secondColumn, grid);

        Grid.SetColumn(directory, 0);
        Grid.SetColumn(splitter, 1);
        Grid.SetColumn(content, 2);
        grid.Children.Add(directory);
        grid.Children.Add(splitter);
        grid.Children.Add(content);
        return grid;
    }

    private Control BuildNavigationDirectory(GuiNode node)
    {
        var stack = new StackPanel
        {
            Spacing = 2d,
        };
        stack.Classes.Add("code-first-navigation-directory-content");

        var root = BuildNavigationRouteTree(node.Payload.NavigationItems);
        var collapsedRoutes = node.Payload.CollapsedNavigationRoutes.ToHashSet(StringComparer.Ordinal);
        var treeNodes = BuildNavigationTreeNodes(root);
        var expansionState = new EditorTreeExpansionState(treeNodes
            .Where(treeNode => treeNode.Payload.Children.Count > 0
                && !collapsedRoutes.Contains(treeNode.Id))
            .Select(treeNode => treeNode.Id));
        var treeRows = EditorTreeFlattener.Flatten(treeNodes, expansionState);
        for (var index = 0; index < treeRows.Count; index++)
        {
            stack.Children.Add(CreateNavigationTreeRow(
                node.Id,
                treeRows[index],
                node.Payload.SelectedRoute));
        }

        var scrollViewer = new ScrollViewer
        {
            Content = stack,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        scrollViewer.Classes.Add("code-first-navigation-directory");
        return scrollViewer;
    }

    private static NavigationRouteNode BuildNavigationRouteTree(
        IReadOnlyList<GuiNavigationItem> items)
    {
        var root = new NavigationRouteNode(string.Empty, string.Empty);
        foreach (var item in items)
        {
            var current = root;
            var route = string.Empty;
            foreach (var segment in item.Route.Split('/'))
            {
                route = string.IsNullOrWhiteSpace(route)
                    ? segment
                    : $"{route}/{segment}";
                current = current.GetOrAddChild(segment, route);
            }

            current.SetPage(item.Route, item.Label);
        }

        return root;
    }

    private static IReadOnlyList<EditorTreeNode<NavigationRouteNode>> BuildNavigationTreeNodes(
        NavigationRouteNode root)
    {
        var nodes = new List<EditorTreeNode<NavigationRouteNode>>();
        for (var index = 0; index < root.Children.Count; index++)
        {
            AddNavigationTreeNode(nodes, root.Children[index], parentId: null);
        }

        return nodes;
    }

    private static void AddNavigationTreeNode(
        ICollection<EditorTreeNode<NavigationRouteNode>> nodes,
        NavigationRouteNode node,
        string? parentId)
    {
        nodes.Add(new EditorTreeNode<NavigationRouteNode>(node.FullRoute, parentId, node));
        for (var index = 0; index < node.Children.Count; index++)
        {
            AddNavigationTreeNode(nodes, node.Children[index], node.FullRoute);
        }
    }

    private Control CreateNavigationTreeRow(
        GuiNodeId nodeId,
        EditorTreeRow<NavigationRouteNode> treeRow,
        string? selectedRoute)
    {
        var node = treeRow.Payload;
        var expansionState = new NavigationTreeRowExpansionState(treeRow.IsExpanded);

        void SetExpanded(bool expanded, bool notifyHost)
        {
            expansionState.IsExpanded = expanded;
            if (expansionState.Expander is not null)
            {
                expansionState.Expander.IconKey = expanded
                    ? EditorIconKey.UiChevronDown
                    : EditorIconKey.UiChevronRight;
            }

            if (notifyHost && treeRow.HasChildren)
            {
                host_.SetNavigationRouteExpanded(nodeId, node.FullRoute, expanded);
            }
        }

        void ToggleExpanded()
        {
            SetExpanded(!expansionState.IsExpanded, notifyHost: true);
        }

        var indentWidth = treeRow.Depth * EditorTreeMetrics.IndentUnit;
        var row = new Grid
        {
            ColumnDefinitions =
            {
                new ColumnDefinition(new GridLength(indentWidth)),
                new ColumnDefinition(new GridLength(EditorTreeMetrics.ExpanderWidth)),
                new ColumnDefinition(GridLength.Star),
            },
            Tag = node.Route ?? node.FullRoute,
        };
        row.Classes.Add(EditorTreeClassNames.Row);
        row.Classes.Add("code-first-navigation-tree-row");
        if (node.IsPage
            && string.Equals(node.Route, selectedRoute, StringComparison.Ordinal))
        {
            row.Classes.Add("selected");
        }

        if (treeRow.HasChildren)
        {
            row.DoubleTapped += (_, args) =>
            {
                ToggleExpanded();
                args.Handled = true;
            };
        }

        if (treeRow.HasChildren)
        {
            expansionState.Expander = CreateNavigationExpander(node.FullRoute, ToggleExpanded);
            Grid.SetColumn(expansionState.Expander, 1);
            row.Children.Add(expansionState.Expander);
        }

        var label = node.IsPage
            ? CreateNavigationRouteButton(nodeId, node, treeRow.HasChildren ? ToggleExpanded : null)
            : CreateNavigationGroupLabel(node);
        Grid.SetColumn(label, 2);
        row.Children.Add(label);

        SetExpanded(expansionState.IsExpanded, notifyHost: false);
        return row;
    }

    private Control CreateNavigationRouteButton(
        GuiNodeId nodeId,
        NavigationRouteNode node,
        Action? toggleExpanded)
    {
        var route = node.Route ?? node.FullRoute;
        var button = new Button
        {
            Content = node.Label ?? node.Segment,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Tag = route,
        };
        ToolTip.SetTip(button, route);
        button.Classes.Add("code-first-navigation-route");
        button.Classes.Add(EditorTreeClassNames.LabelButton);
        if (toggleExpanded is not null)
        {
            button.DoubleTapped += (_, args) =>
            {
                toggleExpanded();
                args.Handled = true;
            };
        }

        button.Click += (_, _) => host_.SelectNavigationRoute(nodeId, route);
        return button;
    }

    private static Control CreateNavigationGroupLabel(
        NavigationRouteNode node)
    {
        var label = new TextBlock
        {
            Text = node.Segment,
            TextTrimming = TextTrimming.CharacterEllipsis,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Tag = node.FullRoute,
        };
        label.Classes.Add(EditorTreeClassNames.PrimaryText);
        label.Classes.Add("code-first-navigation-group");
        return label;
    }

    private static IconButton CreateNavigationExpander(
        string route,
        Action toggleExpanded)
    {
        var expander = new IconButton
        {
            Focusable = false,
            IconKey = EditorIconKey.UiChevronDown,
            IconSize = EditorTreeMetrics.IconSize,
            StrokeWidth = EditorTreeMetrics.ExpanderStrokeWidth,
            Tag = route,
        };
        expander.Classes.Add(EditorTreeClassNames.Expander);
        expander.Classes.Add("code-first-navigation-expander");
        expander.Click += (_, args) =>
        {
            toggleExpanded();
            args.Handled = true;
        };
        return expander;
    }

    private GridSplitter CreateSplitter(
        GuiNode node,
        GridResizeDirection resizeDirection,
        string orientationClass)
    {
        var splitter = new GridSplitter
        {
            ResizeDirection = resizeDirection,
            Tag = node.Id,
        };
        splitter.Classes.Add("code-first-splitter");
        splitter.Classes.Add(orientationClass);
        return splitter;
    }

    private void AttachColumnSplitRatioTracking(
        GuiNodeId nodeId,
        ColumnDefinition firstColumn,
        ColumnDefinition secondColumn,
        Grid grid)
    {
        grid.Tag = new SplitRatioSubscriptions(
            firstColumn.GetObservable(ColumnDefinition.WidthProperty)
                .Subscribe(new ActionObserver<GridLength>(_ => ReportColumnSplitRatio(nodeId, firstColumn, secondColumn))),
            secondColumn.GetObservable(ColumnDefinition.WidthProperty)
                .Subscribe(new ActionObserver<GridLength>(_ => ReportColumnSplitRatio(nodeId, firstColumn, secondColumn))));
    }

    private void AttachRowSplitRatioTracking(
        GuiNodeId nodeId,
        RowDefinition firstRow,
        RowDefinition secondRow,
        Grid grid)
    {
        grid.Tag = new SplitRatioSubscriptions(
            firstRow.GetObservable(RowDefinition.HeightProperty)
                .Subscribe(new ActionObserver<GridLength>(_ => ReportRowSplitRatio(nodeId, firstRow, secondRow))),
            secondRow.GetObservable(RowDefinition.HeightProperty)
                .Subscribe(new ActionObserver<GridLength>(_ => ReportRowSplitRatio(nodeId, firstRow, secondRow))));
    }

    private void ReportColumnSplitRatio(
        GuiNodeId nodeId,
        ColumnDefinition firstColumn,
        ColumnDefinition secondColumn)
    {
        if (TryCalculateSplitRatio(firstColumn.Width, secondColumn.Width, out var ratio))
        {
            host_.ResizeSplit(nodeId, ratio);
        }
    }

    private void ReportRowSplitRatio(
        GuiNodeId nodeId,
        RowDefinition firstRow,
        RowDefinition secondRow)
    {
        if (TryCalculateSplitRatio(firstRow.Height, secondRow.Height, out var ratio))
        {
            host_.ResizeSplit(nodeId, ratio);
        }
    }

    private static bool TryCalculateSplitRatio(
        GridLength first,
        GridLength second,
        out double ratio)
    {
        var firstValue = first.Value;
        var secondValue = second.Value;
        var total = firstValue + secondValue;
        if (double.IsNaN(total)
            || double.IsInfinity(total)
            || firstValue <= 0d
            || secondValue <= 0d
            || total <= 0d)
        {
            ratio = default;
            return false;
        }

        ratio = firstValue / total;
        return ratio > 0d && ratio < 1d;
    }

    private Control BuildFoldout(GuiNode node)
    {
        var expander = new Expander
        {
            Content = BuildStack(node.Children, Orientation.Vertical, "code-first-foldout-content"),
            Header = node.Label ?? string.Empty,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsExpanded = node.Payload.IsExpanded == true,
        };
        expander.Classes.Add("code-first-foldout");
        AttachFoldoutTracking(node.Id, expander);
        return expander;
    }

    private void AttachFoldoutTracking(GuiNodeId nodeId, Expander expander)
    {
        var hasObservedInitialValue = false;
        expander.Tag = expander
            .GetObservable(Expander.IsExpandedProperty)
            .Subscribe(new ActionObserver<bool>(isExpanded =>
            {
                if (!hasObservedInitialValue)
                {
                    hasObservedInitialValue = true;
                    return;
                }

                host_.SetFoldoutExpanded(nodeId, isExpanded);
            }));
    }

    private Control BuildScroll(GuiNode node)
    {
        var scrollViewer = new ScrollViewer
        {
            Content = BuildStack(node.Children, Orientation.Vertical, "code-first-scroll-content"),
            HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            VerticalAlignment = VerticalAlignment.Stretch,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };
        scrollViewer.Classes.Add("code-first-scroll");
        return scrollViewer;
    }

    private static Control BuildLabel(GuiNode node)
    {
        var textBlock = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            TextWrapping = TextWrapping.Wrap,
        };
        textBlock.Classes.Add("code-first-label");
        textBlock.Classes.Add(GetTextToneClass(node.Payload.TextTone ?? GuiTextTone.Secondary));
        textBlock.Classes.Add(GetTextSizeClass(node.Payload.TextSize ?? GuiTextSize.Body));
        return textBlock;
    }

    private static string GetTextToneClass(GuiTextTone tone)
    {
        if (tone == GuiTextTone.Primary)
        {
            return "primary";
        }

        if (tone == GuiTextTone.Muted)
        {
            return "muted";
        }

        return "secondary";
    }

    private static string GetTextSizeClass(GuiTextSize size)
    {
        if (size == GuiTextSize.Caption)
        {
            return "caption";
        }

        if (size == GuiTextSize.Title)
        {
            return "title";
        }

        return "body";
    }

    private static Control BuildValidationMessage(GuiNode node)
    {
        var severity = node.Payload.DiagnosticSeverity ?? EditorDiagnosticSeverity.Error;
        var textBlock = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            TextWrapping = TextWrapping.Wrap,
        };
        textBlock.Classes.Add("code-first-validation-message");
        textBlock.Classes.Add(GetDiagnosticSeverityClass(severity));
        return textBlock;
    }

    private static string GetDiagnosticSeverityClass(EditorDiagnosticSeverity severity)
    {
        if (severity == EditorDiagnosticSeverity.Debug)
        {
            return "debug";
        }

        if (severity == EditorDiagnosticSeverity.Info)
        {
            return "info";
        }

        if (severity == EditorDiagnosticSeverity.Warning)
        {
            return "warning";
        }

        return "error";
    }

    private static Control BuildSeparator()
    {
        var separator = new Separator
        {
            Focusable = false,
        };
        separator.Classes.Add("code-first-separator");
        return separator;
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

    private static Control BuildProperty(GuiNode node)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var value = new TextBlock
        {
            Text = node.Payload.PropertyValue ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        value.Classes.Add("code-first-property-value");

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(value, 1);
        grid.Children.Add(label);
        grid.Children.Add(value);
        return grid;
    }

    private Control BuildTextField(GuiNode node)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var textBox = new TextBox
        {
            Text = node.Payload.TextValue ?? string.Empty,
            MinWidth = 120d,
        };
        textBox.Classes.Add("code-first-text-field");
        AttachTextTracking(
            node.Id,
            textBox,
            node.Payload.TextCommitMode ?? GuiTextInputCommitMode.OnLostFocus,
            node.Payload.TextCommitDelay);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(textBox, 1);
        grid.Children.Add(label);
        grid.Children.Add(textBox);
        return grid;
    }

    private void AttachTextTracking(
        GuiNodeId nodeId,
        TextBox textBox,
        GuiTextInputCommitMode commitMode,
        TimeSpan? commitDelay)
    {
        var hasObservedInitialValue = false;
        IDisposable? pendingCommit = null;
        textBox.Tag = textBox
            .GetObservable(TextBox.TextProperty)
            .Subscribe(new ActionObserver<string?>(text =>
            {
                if (!hasObservedInitialValue)
                {
                    hasObservedInitialValue = true;
                    return;
                }

                if (commitMode == GuiTextInputCommitMode.OnChange)
                {
                    host_.CommitText(nodeId, text ?? string.Empty);
                }
                else if (commitMode == GuiTextInputCommitMode.Debounced)
                {
                    host_.SetText(nodeId, text ?? string.Empty);
                    pendingCommit?.Dispose();
                    pendingCommit = textCommitScheduler_.Schedule(
                        commitDelay ?? DefaultDebounceDelay,
                        () => host_.CommitText(nodeId, textBox.Text ?? string.Empty));
                }
                else
                {
                    host_.SetText(nodeId, text ?? string.Empty);
                }
            }));

        if (commitMode == GuiTextInputCommitMode.OnLostFocus)
        {
            textBox.LostFocus += (_, _) => host_.CommitText(nodeId, textBox.Text ?? string.Empty);
        }
        else if (commitMode == GuiTextInputCommitMode.OnEnter)
        {
            textBox.KeyDown += (_, args) =>
            {
                if (args.Key == Key.Enter)
                {
                    host_.CommitText(nodeId, textBox.Text ?? string.Empty);
                    args.Handled = true;
                }
            };
        }
    }

    private Control BuildToggle(GuiNode node)
    {
        var checkBox = new CheckBox
        {
            Content = node.Label ?? string.Empty,
            IsChecked = node.Payload.IsChecked == true,
        };
        checkBox.Classes.Add("code-first-toggle");
        checkBox.IsCheckedChanged += (_, _) => host_.SetToggle(node.Id, checkBox.IsChecked == true);
        return checkBox;
    }

    private Control BuildComboBox(GuiNode node)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var items = node.Payload.ListItems;
        var comboBox = new ComboBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            ItemTemplate = new FuncDataTemplate<GuiListItem>((item, _) =>
            {
                var itemLabel = new TextBlock
                {
                    Text = item.Label,
                    TextWrapping = TextWrapping.NoWrap,
                };
                itemLabel.Classes.Add("code-first-combo-box-item-label");
                return itemLabel;
            }),
            ItemsSource = items,
            MinWidth = 120d,
            SelectedItem = items.FirstOrDefault(
                item => string.Equals(item.Id, node.Payload.SelectedItemId, StringComparison.Ordinal)),
        };
        comboBox.Classes.Add("code-first-combo-box");
        comboBox.SelectionChanged += (_, _) =>
        {
            if (comboBox.SelectedItem is GuiListItem item)
            {
                host_.SelectComboBoxItem(node.Id, item.Id);
            }
        };

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(comboBox, 1);
        grid.Children.Add(label);
        grid.Children.Add(comboBox);
        return grid;
    }

    private Control BuildRadioGroup(GuiNode node)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var groupName = node.Id.FullKeyPath;
        var stack = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8d,
        };
        stack.Classes.Add("code-first-radio-group");
        foreach (var item in node.Payload.ListItems)
        {
            var radioButton = new RadioButton
            {
                Content = item.Label,
                GroupName = groupName,
                IsChecked = string.Equals(item.Id, node.Payload.SelectedItemId, StringComparison.Ordinal),
                Tag = item.Id,
            };
            radioButton.Classes.Add("code-first-radio-button");
            radioButton.IsCheckedChanged += (_, _) =>
            {
                if (radioButton.IsChecked == true && radioButton.Tag is string itemId)
                {
                    host_.SelectRadioGroupItem(node.Id, itemId);
                }
            };
            stack.Children.Add(radioButton);
        }

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(stack, 1);
        grid.Children.Add(label);
        grid.Children.Add(stack);
        return grid;
    }

    private Control BuildColorField(GuiNode node)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var showAlpha = node.Payload.ShowAlpha != false;
        var colorPicker = new ColorPicker
        {
            Color = ToAvaloniaColor(node.Payload.ColorValue ?? new GuiColorValue(0, 0, 0)),
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsAlphaEnabled = showAlpha,
            IsAlphaVisible = showAlpha,
            MinWidth = 120d,
        };
        colorPicker.Classes.Add("code-first-color-field");
        AttachColorFieldTracking(node.Id, colorPicker);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(colorPicker, 1);
        grid.Children.Add(label);
        grid.Children.Add(colorPicker);
        return grid;
    }

    private void AttachColorFieldTracking(GuiNodeId nodeId, ColorPicker colorPicker)
    {
        colorPicker.ColorChanged += (_, args) =>
        {
            host_.SetColorValue(nodeId, FromAvaloniaColor(args.NewColor));
        };
    }

    private Control BuildVector3Field(GuiNode node)
    {
        var value = node.Payload.Vector3Value ?? new GuiVector3Value(0d, 0d, 0d);
        return BuildVectorField(
            node,
            "code-first-vector3-field",
            ["X", "Y", "Z"],
            [value.X, value.Y, value.Z],
            values => host_.SetVector3Value(
                node.Id,
                new GuiVector3Value(values[0], values[1], values[2])));
    }

    private Control BuildVector2Field(GuiNode node)
    {
        var value = node.Payload.Vector2Value ?? new GuiVector2Value(0d, 0d);
        return BuildVectorField(
            node,
            "code-first-vector2-field",
            ["X", "Y"],
            [value.X, value.Y],
            values => host_.SetVector2Value(
                node.Id,
                new GuiVector2Value(values[0], values[1])));
    }

    private Control BuildVector4Field(GuiNode node)
    {
        var value = node.Payload.Vector4Value ?? new GuiVector4Value(0d, 0d, 0d, 0d);
        return BuildVectorField(
            node,
            "code-first-vector4-field",
            ["X", "Y", "Z", "W"],
            [value.X, value.Y, value.Z, value.W],
            values => host_.SetVector4Value(
                node.Id,
                new GuiVector4Value(values[0], values[1], values[2], values[3])));
    }

    private Control BuildVectorField(
        GuiNode node,
        string className,
        IReadOnlyList<string> componentLabels,
        IReadOnlyList<double> componentValues,
        Action<IReadOnlyList<double>> reportChange)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var components = componentValues
            .Select(value => CreateVectorComponent(node, value))
            .ToArray();
        var vectorGrid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions(CreateVectorColumnDefinitions(componentLabels.Count)),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        vectorGrid.Classes.Add(className);
        for (var index = 0; index < components.Length; index++)
        {
            AddVectorComponent(
                vectorGrid,
                componentLabels[index],
                components[index],
                labelColumn: index * 2);
        }

        AttachVectorFieldTracking(components, reportChange);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(vectorGrid, 1);
        grid.Children.Add(label);
        grid.Children.Add(vectorGrid);
        return grid;
    }

    private static NumericUpDown CreateVectorComponent(GuiNode node, double value)
    {
        var component = new NumericUpDown
        {
            FormatString = node.Payload.NumericFormatString ?? "0.###",
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Increment = ToDecimal(node.Payload.NumericSmallChange ?? 0.1d),
            MinWidth = 64d,
            Value = ToDecimal(value),
        };
        if (node.Payload.NumericMinimum is { } minimum)
        {
            component.Minimum = ToDecimal(minimum);
        }

        if (node.Payload.NumericMaximum is { } maximum)
        {
            component.Maximum = ToDecimal(maximum);
        }

        component.Classes.Add("code-first-vector-component");
        return component;
    }

    private static string CreateVectorColumnDefinitions(int componentCount)
    {
        return string.Join(
            ",",
            Enumerable.Repeat("Auto,*", componentCount));
    }

    private static void AddVectorComponent(
        Grid grid,
        string label,
        NumericUpDown component,
        int labelColumn)
    {
        var componentLabel = new TextBlock
        {
            Text = label,
            TextWrapping = TextWrapping.NoWrap,
            VerticalAlignment = VerticalAlignment.Center,
        };
        componentLabel.Classes.Add("code-first-vector-component-label");
        Grid.SetColumn(componentLabel, labelColumn);
        Grid.SetColumn(component, labelColumn + 1);
        grid.Children.Add(componentLabel);
        grid.Children.Add(component);
    }

    private static void AttachVectorFieldTracking(
        IReadOnlyList<NumericUpDown> components,
        Action<IReadOnlyList<double>> reportChange)
    {
        foreach (var component in components)
        {
            AttachVectorComponentTracking(components, component, reportChange);
        }
    }

    private static void AttachVectorComponentTracking(
        IReadOnlyList<NumericUpDown> components,
        NumericUpDown observedInput,
        Action<IReadOnlyList<double>> reportChange)
    {
        var hasObservedInitialValue = false;
        observedInput.Tag = observedInput
            .GetObservable(NumericUpDown.ValueProperty)
            .Subscribe(new ActionObserver<decimal?>(_ =>
            {
                if (!hasObservedInitialValue)
                {
                    hasObservedInitialValue = true;
                    return;
                }

                reportChange(components.Select(component => ToDouble(component.Value)).ToArray());
            }));
    }

    private Control BuildSlider(GuiNode node)
    {
        var minimum = node.Payload.NumericMinimum ?? 0d;
        var maximum = node.Payload.NumericMaximum ?? 1d;
        var value = Math.Clamp(node.Payload.NumericValue ?? minimum, minimum, maximum);

        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var slider = new Slider
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            LargeChange = node.Payload.NumericLargeChange ?? (maximum - minimum) / 10d,
            Maximum = maximum,
            Minimum = minimum,
            SmallChange = node.Payload.NumericSmallChange ?? (maximum - minimum) / 100d,
            Value = value,
        };
        slider.Classes.Add("code-first-slider");
        AttachSliderTracking(node.Id, slider);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(slider, 1);
        grid.Children.Add(label);
        grid.Children.Add(slider);
        return grid;
    }

    private void AttachSliderTracking(GuiNodeId nodeId, Slider slider)
    {
        var hasObservedInitialValue = false;
        slider.Tag = slider
            .GetObservable(RangeBase.ValueProperty)
            .Subscribe(new ActionObserver<double>(value =>
            {
                if (!hasObservedInitialValue)
                {
                    hasObservedInitialValue = true;
                    return;
                }

                host_.SetSliderValue(nodeId, value);
            }));
    }

    private Control BuildNumberInput(GuiNode node)
    {
        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var numberInput = new NumericUpDown
        {
            FormatString = node.Payload.NumericFormatString ?? "0.###",
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Increment = ToDecimal(node.Payload.NumericSmallChange ?? 1d),
            MinWidth = 120d,
            Value = ToDecimal(node.Payload.NumericValue ?? 0d),
        };
        if (node.Payload.NumericMinimum is { } minimum)
        {
            numberInput.Minimum = ToDecimal(minimum);
        }

        if (node.Payload.NumericMaximum is { } maximum)
        {
            numberInput.Maximum = ToDecimal(maximum);
        }

        numberInput.Classes.Add("code-first-number-input");
        AttachNumberInputTracking(node.Id, numberInput);

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(numberInput, 1);
        grid.Children.Add(label);
        grid.Children.Add(numberInput);
        return grid;
    }

    private void AttachNumberInputTracking(GuiNodeId nodeId, NumericUpDown numberInput)
    {
        var hasObservedInitialValue = false;
        numberInput.Tag = numberInput
            .GetObservable(NumericUpDown.ValueProperty)
            .Subscribe(new ActionObserver<decimal?>(value =>
            {
                if (!hasObservedInitialValue)
                {
                    hasObservedInitialValue = true;
                    return;
                }

                if (value is { } numericValue)
                {
                    host_.SetNumberInputValue(nodeId, decimal.ToDouble(numericValue));
                }
            }));
    }

    private static decimal ToDecimal(double value)
    {
        return Convert.ToDecimal(value);
    }

    private static double ToDouble(decimal? value)
    {
        return value is { } numericValue
            ? decimal.ToDouble(numericValue)
            : 0d;
    }

    private static Color ToAvaloniaColor(GuiColorValue value)
    {
        return Color.FromArgb(value.Alpha, value.Red, value.Green, value.Blue);
    }

    private static GuiColorValue FromAvaloniaColor(Color value)
    {
        return new GuiColorValue(value.R, value.G, value.B, value.A);
    }

    private static Control BuildProgressBar(GuiNode node)
    {
        var minimum = node.Payload.NumericMinimum ?? 0d;
        var maximum = node.Payload.NumericMaximum ?? 100d;
        var value = Math.Clamp(node.Payload.NumericValue ?? minimum, minimum, maximum);

        var label = new TextBlock
        {
            Text = node.Label ?? string.Empty,
            VerticalAlignment = VerticalAlignment.Center,
            TextWrapping = TextWrapping.NoWrap,
        };
        label.Classes.Add("code-first-input-label");

        var progressBar = new ProgressBar
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            IsIndeterminate = node.Payload.IsIndeterminate == true,
            Maximum = maximum,
            Minimum = minimum,
            ShowProgressText = node.Payload.ShowProgressText == true,
            Value = value,
        };
        if (!string.IsNullOrWhiteSpace(node.Payload.ProgressTextFormat))
        {
            progressBar.ProgressTextFormat = node.Payload.ProgressTextFormat;
        }

        progressBar.Classes.Add("code-first-progress-bar");

        var grid = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("120,*"),
        };
        grid.Classes.Add("code-first-property-row");
        Grid.SetColumn(label, 0);
        Grid.SetColumn(progressBar, 1);
        grid.Children.Add(label);
        grid.Children.Add(progressBar);
        return grid;
    }

    private Control BuildList(GuiNode node)
    {
        var listItems = node.Payload.ListItems;
        var listBox = new ListBox
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            ItemsPanel = new FuncTemplate<Panel?>(() => new VirtualizingStackPanel()),
            ItemTemplate = new FuncDataTemplate<GuiListItem>((item, _) =>
            {
                var label = new TextBlock
                {
                    Text = item.Label,
                    TextWrapping = TextWrapping.NoWrap,
                };
                label.Classes.Add("code-first-list-item-label");
                return label;
            }),
            ItemsSource = listItems,
            SelectionMode = SelectionMode.Single,
            SelectedItem = listItems.FirstOrDefault(
                item => string.Equals(item.Id, node.Payload.SelectedItemId, StringComparison.Ordinal)),
            VerticalAlignment = VerticalAlignment.Stretch,
        };
        listBox.Classes.Add("code-first-list");

        listBox.SelectionChanged += (_, _) =>
        {
            if (listBox.SelectedItem is GuiListItem item)
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
            TextWrapping = TextWrapping.Wrap,
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

    private sealed class NavigationRouteNode
    {
        private readonly List<NavigationRouteNode> children_ = [];

        public NavigationRouteNode(string segment, string fullRoute)
        {
            Segment = segment;
            FullRoute = fullRoute;
        }

        public string Segment { get; }

        public string FullRoute { get; }

        public string? Route { get; private set; }

        public string? Label { get; private set; }

        public IReadOnlyList<NavigationRouteNode> Children => children_;

        public bool IsPage => Route is not null;

        public NavigationRouteNode GetOrAddChild(string segment, string fullRoute)
        {
            var existing = children_.FirstOrDefault(
                child => string.Equals(child.Segment, segment, StringComparison.Ordinal));
            if (existing is not null)
            {
                return existing;
            }

            var child = new NavigationRouteNode(segment, fullRoute);
            children_.Add(child);
            return child;
        }

        public void SetPage(string route, string label)
        {
            Route = route;
            Label = label;
        }
    }

    private sealed record SplitRatioSubscriptions(
        IDisposable FirstSubscription,
        IDisposable SecondSubscription) : IDisposable
    {
        public void Dispose()
        {
            FirstSubscription.Dispose();
            SecondSubscription.Dispose();
        }
    }

    private sealed class NavigationTreeRowExpansionState(bool isExpanded)
    {
        public bool IsExpanded { get; set; } = isExpanded;

        public IconButton? Expander { get; set; }
    }

    private sealed class ActionObserver<T> : IObserver<T>
    {
        private readonly Action<T> onNext_;

        public ActionObserver(Action<T> onNext)
        {
            onNext_ = onNext;
        }

        public void OnCompleted()
        {
        }

        public void OnError(Exception error)
        {
        }

        public void OnNext(T value)
        {
            onNext_(value);
        }
    }
}
