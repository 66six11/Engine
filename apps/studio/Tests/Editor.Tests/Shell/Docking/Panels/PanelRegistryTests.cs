using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Panels;
using Editor.Shell.Docking.Panels;
using Xunit;

namespace Editor.Tests.Shell.Docking.Panels;

public sealed class PanelRegistryTests
{
    [Fact]
    public void RegisterOwned_records_owner_and_removal_handle_removes_panel()
    {
        var registry = new PanelRegistry();
        var owner = new EditorExtensionId("test.owner");
        var lease = registry.RegisterOwned(CreateDescriptor("owned-panel"), owner);

        Assert.Equal(owner, registry.GetOwnerId("owned-panel"));
        Assert.Equal(["owned-panel"], registry.GetAll().Select(panel => panel.Id));

        lease.Dispose();

        Assert.Empty(registry.GetAll());
        Assert.Throws<KeyNotFoundException>(() => registry.GetOwnerId("owned-panel"));
        Assert.Throws<KeyNotFoundException>(() => registry.GetRequired("owned-panel"));
    }

    [Fact]
    public void RegisterOwned_duplicate_panel_diagnostic_includes_existing_and_new_owner()
    {
        var registry = new PanelRegistry();
        registry.RegisterOwned(
            CreateDescriptor("shared-panel"),
            new EditorExtensionId("test.first"));

        var exception = Assert.Throws<InvalidOperationException>(
            () => registry.RegisterOwned(
                CreateDescriptor("shared-panel"),
                new EditorExtensionId("test.second")));

        Assert.Equal(
            "Panel id 'shared-panel' is already registered by 'test.first'; "
            + "new owner 'test.second' cannot register it.",
            exception.Message);
    }

    [Fact]
    public void RegisterOwned_stale_removal_handle_does_not_remove_newer_registration()
    {
        var registry = new PanelRegistry();
        var firstLease = registry.RegisterOwned(
            CreateDescriptor("reused-panel"),
            new EditorExtensionId("test.first"));
        firstLease.Dispose();

        var secondOwner = new EditorExtensionId("test.second");
        var secondLease = registry.RegisterOwned(CreateDescriptor("reused-panel"), secondOwner);

        firstLease.Dispose();

        Assert.Equal(secondOwner, registry.GetOwnerId("reused-panel"));
        Assert.Equal(["reused-panel"], registry.GetAll().Select(panel => panel.Id));

        secondLease.Dispose();

        Assert.Empty(registry.GetAll());
    }

    private static PanelDescriptor CreateDescriptor(string id)
    {
        return new PanelDescriptor(
            id,
            id,
            PanelKind.Tool,
            DockArea.Left,
            $"Window/Panels/{id}",
            DockContentCachePolicy.KeepAlive,
            static () => new object());
    }
}
