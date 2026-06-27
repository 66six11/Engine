using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Editor.Core.CodeFirstUI;
using Editor.Shell.CodeFirstUI;
using Editor.Shell.CodeFirstUI.Adapters;
using Xunit;

namespace Editor.Tests.Shell.CodeFirstUI.Adapters;

public sealed class GuiAvaloniaControlFactoryTests
{
    [Fact]
    public void Build_maps_split_with_catalog_list_to_grid_and_list_box()
    {
        var builder = new GuiFrameBuilder("ui-style");
        using (builder.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            using (builder.Panel("catalog", "Catalog"))
            {
                builder.List(
                    "sections",
                    [new GuiListItem("overview", "Overview"), new GuiListItem("buttons", "Buttons")],
                    "overview");
            }

            using (builder.Panel("preview", "Preview"))
            {
                builder.Label("title", "Overview");
            }
        }

        var factory = new GuiAvaloniaControlFactory(new NoopCodeFirstPanelHost());

        var control = factory.Build(builder.Build());

        var grid = Assert.IsType<Grid>(control);
        Assert.Equal(3, grid.Children.Count);
        Assert.Equal(3, grid.ColumnDefinitions.Count);
        var splitter = Assert.Single(grid.Children.OfType<GridSplitter>());
        Assert.Equal(1, Grid.GetColumn(splitter));
        Assert.Equal(GridResizeDirection.Columns, splitter.ResizeDirection);
        Assert.Contains("code-first-splitter", splitter.Classes);
        Assert.Contains("vertical", splitter.Classes);
        Assert.NotNull(FindDescendant<ListBox>(grid));
        Assert.NotNull(FindDescendant<TextBlock>(grid, text => text.Text == "Overview"));
    }

    [Fact]
    public void Splitter_width_changes_are_reported_to_host_as_split_ratio()
    {
        var builder = new GuiFrameBuilder("ui-style");
        using (builder.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            builder.Label("catalog", "Catalog");
            builder.Label("preview", "Preview");
        }

        var host = new RecordingCodeFirstPanelHost();
        var factory = new GuiAvaloniaControlFactory(host);
        var grid = Assert.IsType<Grid>(factory.Build(builder.Build()));

        grid.ColumnDefinitions[0].Width = new GridLength(0.40d, GridUnitType.Star);
        grid.ColumnDefinitions[2].Width = new GridLength(0.60d, GridUnitType.Star);

        var resize = Assert.Single(host.SplitResizes, resize => resize.Ratio == 0.40d);
        Assert.Equal(new GuiNodeId("ui-style", "layout", GuiNodeKind.Split), resize.NodeId);
    }

    private static T? FindDescendant<T>(Control control, Predicate<T>? predicate = null)
        where T : Control
    {
        if (control is T typed
            && (predicate is null || predicate(typed)))
        {
            return typed;
        }

        return control switch
        {
            Panel panel => panel.Children.Select(child => FindDescendant(child, predicate)).FirstOrDefault(found => found is not null),
            ContentControl content when content.Content is Control child => FindDescendant(child, predicate),
            Decorator decorator when decorator.Child is Control child => FindDescendant(child, predicate),
            _ => null,
        };
    }

    private sealed class NoopCodeFirstPanelHost : IGuiAvaloniaHost
    {
        public void ClickButton(GuiNodeId nodeId)
        {
        }

        public void SelectListItem(GuiNodeId nodeId, string itemId)
        {
        }

        public void ResizeSplit(GuiNodeId nodeId, double ratio)
        {
        }
    }

    private sealed class RecordingCodeFirstPanelHost : IGuiAvaloniaHost
    {
        private readonly List<SplitResize> splitResizes_ = [];

        public IReadOnlyList<SplitResize> SplitResizes => splitResizes_;

        public void ClickButton(GuiNodeId nodeId)
        {
        }

        public void SelectListItem(GuiNodeId nodeId, string itemId)
        {
        }

        public void ResizeSplit(GuiNodeId nodeId, double ratio)
        {
            splitResizes_.Add(new SplitResize(nodeId, ratio));
        }
    }

    private sealed record SplitResize(
        GuiNodeId NodeId,
        double Ratio);
}
