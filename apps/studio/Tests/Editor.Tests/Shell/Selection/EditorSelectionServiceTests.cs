using System.Collections.Generic;
using Asharia.Editor.Selection;
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Shell.Selection;

public sealed class EditorSelectionServiceTests
{
    [Fact]
    public void Current_starts_empty()
    {
        var service = new EditorSelectionService();

        Assert.Null(service.Current.ActiveContextId);
        Assert.False(service.Current.HasSelection);
        Assert.Null(service.Current.PrimaryItem);
    }

    [Fact]
    public void ReplaceSelection_updates_snapshot_and_notifies_subscribers()
    {
        var service = new EditorSelectionService();
        var events = new List<EditorSelectionChangedEventArgs>();
        service.SelectionChanged += (_, e) => events.Add(e);
        var item = new EditorSelectionItem("entity:1", "scene-object", "Cube");

        service.ReplaceSelection("hierarchy", [item]);

        Assert.Equal("hierarchy", service.Current.ActiveContextId);
        Assert.Equal([item], service.Current.Items);
        var selectionEvent = Assert.Single(events);
        Assert.False(selectionEvent.Previous.HasSelection);
        Assert.Equal([item], selectionEvent.Current.Items);
    }

    [Fact]
    public void ReplaceSelection_copies_items_before_storing_snapshot()
    {
        var service = new EditorSelectionService();
        var items = new List<EditorSelectionItem>
        {
            new("entity:1", "scene-object", "Cube"),
        };

        service.ReplaceSelection("hierarchy", items);
        items.Add(new EditorSelectionItem("entity:2", "scene-object", "Light"));

        var storedItem = Assert.Single(service.Current.Items);
        Assert.Equal("Cube", storedItem.DisplayName);
    }

    [Fact]
    public void ReplaceSelection_does_not_notify_when_snapshot_is_unchanged()
    {
        var service = new EditorSelectionService();
        var item = new EditorSelectionItem("entity:1", "scene-object", "Cube");
        service.ReplaceSelection("hierarchy", [item]);
        var notifications = 0;
        service.SelectionChanged += (_, _) => notifications++;

        service.ReplaceSelection("hierarchy", [item]);

        Assert.Equal(0, notifications);
    }

    [Fact]
    public void ClearSelection_preserves_active_context_and_notifies_once()
    {
        var service = new EditorSelectionService();
        service.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("entity:1", "scene-object", "Cube")]);
        var events = new List<EditorSelectionChangedEventArgs>();
        service.SelectionChanged += (_, e) => events.Add(e);

        service.ClearSelection("hierarchy");

        Assert.Equal("hierarchy", service.Current.ActiveContextId);
        Assert.False(service.Current.HasSelection);
        Assert.Single(events);
    }

    [Fact]
    public void SetActiveContext_updates_context_without_selection_items()
    {
        var service = new EditorSelectionService();

        service.SetActiveContext("scene-view");

        Assert.Equal("scene-view", service.Current.ActiveContextId);
        Assert.Empty(service.Current.Items);
    }
}
