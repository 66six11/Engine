using System;
using System.Linq;
using Editor.Core.Models;
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
    public void Register_rejects_duplicate_action_ids()
    {
        var registry = new WorkbenchActionRegistry();

        registry.Register(CreatePanelAction("duplicate", "First", "first-panel"));

        Assert.Throws<InvalidOperationException>(
            () => registry.Register(CreatePanelAction("duplicate", "Second", "second-panel")));
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
                "Window/Panels/Broken")));
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
            TargetId: panelId);
    }
}
