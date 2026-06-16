using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Views;

public partial class EditorDockSplitNodeView : UserControl
{
    public EditorDockSplitNodeView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, System.EventArgs e)
    {
        RebuildSplitHost();
    }

    private void RebuildSplitHost()
    {
        if (DataContext is not EditorDockSplitNodeViewModel split)
        {
            SplitHost.Content = null;
            return;
        }

        var grid = new Grid
        {
            DataContext = split,
        };

        if (split.IsHorizontal)
        {
            var firstColumn = new ColumnDefinition();
            var secondColumn = new ColumnDefinition();
            firstColumn.Bind(ColumnDefinition.WidthProperty, CreateBinding(nameof(split.FirstLength), split));
            secondColumn.Bind(ColumnDefinition.WidthProperty, CreateBinding(nameof(split.SecondLength), split));
            grid.ColumnDefinitions.Add(firstColumn);
            grid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(6)));
            grid.ColumnDefinitions.Add(secondColumn);

            var firstHost = CreateNodeHost(nameof(split.First));
            var secondHost = CreateNodeHost(nameof(split.Second));
            var splitter = CreateSplitter(split, GridResizeDirection.Columns, "vertical");
            Grid.SetColumn(firstHost, 0);
            Grid.SetColumn(splitter, 1);
            Grid.SetColumn(secondHost, 2);
            grid.Children.Add(firstHost);
            grid.Children.Add(splitter);
            grid.Children.Add(secondHost);
        }
        else
        {
            var firstRow = new RowDefinition();
            var secondRow = new RowDefinition();
            firstRow.Bind(RowDefinition.HeightProperty, CreateBinding(nameof(split.FirstLength), split));
            secondRow.Bind(RowDefinition.HeightProperty, CreateBinding(nameof(split.SecondLength), split));
            grid.RowDefinitions.Add(firstRow);
            grid.RowDefinitions.Add(new RowDefinition(new GridLength(6)));
            grid.RowDefinitions.Add(secondRow);

            var firstHost = CreateNodeHost(nameof(split.First));
            var secondHost = CreateNodeHost(nameof(split.Second));
            var splitter = CreateSplitter(split, GridResizeDirection.Rows, "horizontal");
            Grid.SetRow(firstHost, 0);
            Grid.SetRow(splitter, 1);
            Grid.SetRow(secondHost, 2);
            grid.Children.Add(firstHost);
            grid.Children.Add(splitter);
            grid.Children.Add(secondHost);
        }

        SplitHost.Content = grid;
    }

    private static ContentControl CreateNodeHost(string propertyName)
    {
        var host = new ContentControl();
        host.Classes.Add("owned-dock-node-host");
        host.Bind(ContentControl.ContentProperty, new Binding(propertyName));
        return host;
    }

    private static GridSplitter CreateSplitter(
        EditorDockSplitNodeViewModel split,
        GridResizeDirection resizeDirection,
        string orientationClass)
    {
        var splitter = new GridSplitter
        {
            DataContext = split,
            ResizeDirection = resizeDirection,
            Tag = split.Id,
        };
        splitter.Classes.Add("owned-dock-splitter");
        splitter.Classes.Add("owned-dock-layout-splitter");
        splitter.Classes.Add(orientationClass);
        return splitter;
    }

    private static Binding CreateBinding(string propertyName, EditorDockSplitNodeViewModel source)
    {
        return new Binding(propertyName)
        {
            Source = source,
            Mode = BindingMode.TwoWay,
        };
    }
}
