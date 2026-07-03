using System;
using Editor.Core.Models;
using Editor.Core.Models.Workbench;
using Editor.Shell.Commands;
using Xunit;

namespace Editor.Tests.Shell.Commands;

public sealed class WorkbenchCommandRouterTests
{
    [Fact]
    public void Execute_returns_succeeded_when_executor_completes_command()
    {
        var action = CreateAction("workbench.panel.console");
        var registry = CreateRegistry(action);
        var executor = new CapturingActionExecutor(_ => true);
        var router = new WorkbenchCommandRouter(registry, executor);

        var result = router.Execute("workbench.panel.console");

        Assert.True(result.Succeeded);
        Assert.Equal(WorkbenchCommandExecutionStatus.Succeeded, result.Status);
        Assert.Equal("workbench.panel.console", result.CommandId);
        Assert.Equal("workbench.panel.console", executor.ExecutedActionId);
    }

    [Fact]
    public void Execute_returns_not_found_for_unknown_command()
    {
        var router = new WorkbenchCommandRouter(new WorkbenchActionRegistry(), new CapturingActionExecutor(_ => true));

        var result = router.Execute("missing.command");

        Assert.Equal(WorkbenchCommandExecutionStatus.NotFound, result.Status);
        Assert.Equal("missing.command", result.CommandId);
        Assert.Contains("not registered", result.Message);
    }

    [Fact]
    public void Execute_returns_disabled_without_dispatching()
    {
        var action = CreateAction("workbench.panel.disabled") with
        {
            IsEnabled = false,
            DisabledReason = "Disabled by test",
        };
        var executor = new CapturingActionExecutor(_ => true);
        var router = new WorkbenchCommandRouter(CreateRegistry(action), executor);

        var result = router.Execute("workbench.panel.disabled");

        Assert.Equal(WorkbenchCommandExecutionStatus.Disabled, result.Status);
        Assert.Equal("Disabled by test", result.Message);
        Assert.Null(executor.ExecutedActionId);
    }

    [Fact]
    public void Execute_returns_failed_when_executor_returns_false()
    {
        var router = new WorkbenchCommandRouter(
            CreateRegistry(CreateAction("workbench.panel.console")),
            new CapturingActionExecutor(_ => false));

        var result = router.Execute("workbench.panel.console");

        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, result.Status);
        Assert.Contains("did not complete", result.Message);
    }

    [Fact]
    public void Execute_returns_failed_when_executor_throws()
    {
        var router = new WorkbenchCommandRouter(
            CreateRegistry(CreateAction("workbench.panel.console")),
            new CapturingActionExecutor(_ => throw new InvalidOperationException("boom")));

        var result = router.Execute("workbench.panel.console");

        Assert.Equal(WorkbenchCommandExecutionStatus.Failed, result.Status);
        Assert.Equal("boom", result.Message);
    }

    private static WorkbenchActionRegistry CreateRegistry(WorkbenchActionDescriptor action)
    {
        var registry = new WorkbenchActionRegistry();
        registry.Register(action);
        return registry;
    }

    private static WorkbenchActionDescriptor CreateAction(string id)
    {
        return new WorkbenchActionDescriptor(
            id,
            "Console",
            WorkbenchActionKind.OpenPanel,
            "Window/Panels/Console",
            TargetId: "console",
            Category: "Window");
    }

    private sealed class CapturingActionExecutor(Func<WorkbenchActionDescriptor, bool> execute) : IWorkbenchActionExecutor
    {
        public string? ExecutedActionId { get; private set; }

        public bool Execute(WorkbenchActionDescriptor action)
        {
            ExecutedActionId = action.Id;
            return execute(action);
        }
    }
}
