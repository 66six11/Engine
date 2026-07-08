using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models.Extensions;
using Editor.Core.Models.Workbench;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchActionRegistryTests
{
    [Fact]
    public void Register_preserves_registration_order()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(CreatePanelAction("first", "First", "first-panel"));
        registry.Register(CreatePanelAction("second", "Second", "second-panel"));

        Assert.Equal(["first", "second"], registry.GetAll().Select(action => action.Id));
    }

    [Fact]
    public void RegisterOwned_records_owner_and_removal_handle_removes_action()
    {
        var registry = new WorkbenchActionRegistry();
        var owner = new EditorExtensionId("test.owner");
        var lease = registry.RegisterOwned(
            CreatePanelAction("owned.action", "Owned", "owned-panel"),
            owner);

        Assert.Equal(owner, registry.GetOwnerId("owned.action"));
        Assert.Equal(["owned.action"], registry.GetAll().Select(action => action.Id));
        Assert.NotNull(registry.FindById("owned.action"));

        lease.Dispose();

        Assert.Empty(registry.GetAll());
        Assert.Null(registry.FindById("owned.action"));
        Assert.Throws<KeyNotFoundException>(() => registry.GetOwnerId("owned.action"));
    }

    [Fact]
    public void RegisterOwned_duplicate_action_diagnostic_includes_existing_and_new_owner()
    {
        var registry = new WorkbenchActionRegistry();
        registry.RegisterOwned(
            CreatePanelAction("shared.action", "First", "first-panel"),
            new EditorExtensionId("test.first"));

        var exception = Assert.Throws<InvalidOperationException>(
            () => registry.RegisterOwned(
                CreatePanelAction("shared.action", "Second", "second-panel"),
                new EditorExtensionId("test.second")));

        Assert.Equal(
            "Workbench action id 'shared.action' is already registered by 'test.first'; "
            + "new owner 'test.second' cannot register it.",
            exception.Message);
    }

    [Fact]
    public void RegisterOwned_stale_removal_handle_does_not_remove_newer_registration()
    {
        var registry = new WorkbenchActionRegistry();
        var firstLease = registry.RegisterOwned(
            CreatePanelAction("reused.action", "First", "first-panel"),
            new EditorExtensionId("test.first"));
        firstLease.Dispose();

        var secondOwner = new EditorExtensionId("test.second");
        var secondLease = registry.RegisterOwned(
            CreatePanelAction("reused.action", "Second", "second-panel"),
            secondOwner);

        firstLease.Dispose();

        Assert.Equal(secondOwner, registry.GetOwnerId("reused.action"));
        Assert.Equal(["reused.action"], registry.GetAll().Select(action => action.Id));

        secondLease.Dispose();

        Assert.Empty(registry.GetAll());
    }

    [Fact]
    public void Register_preserves_command_metadata()
    {
        var registry = new WorkbenchActionRegistry();
        var action = new WorkbenchActionDescriptor(
            "workbench.panel.console",
            "Console",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Console",
            TargetId: "console",
            IconKey: "studio.console",
            Category: "Window",
            DefaultShortcut: "Ctrl+Alt+C",
            Scope: WorkbenchActionScope.Global,
            SearchText: "log output diagnostics");

        registry.Register(action);

        var actual = Assert.Single(registry.GetAll());
        Assert.Equal(action, actual);
        Assert.True(actual.IsEnabled);
        Assert.Null(actual.DisabledReason);
    }

    [Fact]
    public void FindById_returns_registered_action()
    {
        var registry = new WorkbenchActionRegistry();
        var action = CreatePanelAction("workbench.panel.console", "Console", "console");
        registry.Register(action);

        var actual = registry.FindById("workbench.panel.console");

        Assert.Equal(action, actual);
    }

    [Fact]
    public void FindById_returns_null_for_missing_action()
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Null(registry.FindById("missing.command"));
    }

    [Fact]
    public void FindById_rejects_null_id()
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentNullException>(() => registry.FindById(null!));
    }

    [Fact]
    public void Register_accepts_disabled_action_with_reason()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(new WorkbenchActionDescriptor(
            "workbench.panel.disabled",
            "Disabled Panel",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Disabled",
            TargetId: "disabled",
            Category: "Window",
            IsEnabled: false,
            DisabledReason: "Disabled by test"));

        var action = Assert.Single(registry.GetAll());
        Assert.False(action.IsEnabled);
        Assert.Equal("Disabled by test", action.DisabledReason);
    }

    [Fact]
    public void Register_rejects_duplicate_action_ids()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(CreatePanelAction("duplicate", "First", "first-panel"));

        Assert.Throws<InvalidOperationException>(
            () => registry.Register(CreatePanelAction("duplicate", "Second", "second-panel")));
    }

    [Theory]
    [InlineData("", "Title", "Window/Panels/Panel", "Window")]
    [InlineData("id", "", "Window/Panels/Panel", "Window")]
    [InlineData("id", "Title", "", "Window")]
    [InlineData("id", "Title", "Window/Panels/Panel", "")]
    public void Register_rejects_empty_required_text(
        string id,
        string title,
        string menuPath,
        string category)
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentException>(
            () => registry.Register(new WorkbenchActionDescriptor(
                id,
                title,
                WorkbenchActionKind.OpenPanel,
                menuPath,
                TargetId: "panel",
                Category: category)));
    }

    [Fact]
    public void Register_rejects_open_panel_action_without_target_panel()
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentException>(
            () => registry.Register(new WorkbenchActionDescriptor(
                "broken",
                "Broken",
                WorkbenchActionKind.OpenPanel,
                "Window/Panels/Broken",
                Category: "Window")));
    }

    [Fact]
    public void Register_rejects_disabled_action_without_reason()
    {
        var registry = new WorkbenchActionRegistry();

        Assert.Throws<ArgumentException>(
            () => registry.Register(new WorkbenchActionDescriptor(
                "workbench.panel.disabled",
                "Disabled Panel",
                WorkbenchActionKind.OpenPanel,
                "Window/Panels/Disabled",
                TargetId: "disabled",
                Category: "Window",
                IsEnabled: false)));
    }

    private static WorkbenchActionDescriptor CreatePanelAction(
        string id,
        string title,
        string panelId)
    {
        return new WorkbenchActionDescriptor(
            id,
            title,
            WorkbenchActionKind.OpenPanel,
            $"Window/Panels/{title}",
            TargetId: panelId,
            Category: "Window");
    }
}
