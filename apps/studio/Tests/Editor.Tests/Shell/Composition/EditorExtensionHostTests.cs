using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Composition;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorExtensionHostTests
{
    [Fact]
    public void Compose_declares_each_module_once_and_registers_panels_and_actions_together()
    {
        var firstModule = new TestExtensionModule(
            "test.first",
            panels:
            [
                CreatePanel("first-panel"),
            ],
            actions:
            [
                CreateAction("first.action"),
            ]);
        var secondModule = new TestExtensionModule(
            "test.second",
            panels:
            [
                CreatePanel("second-panel"),
            ],
            actions:
            [
                CreateAction("second.action"),
            ]);
        var host = new EditorExtensionHost([firstModule, secondModule]);

        var composition = host.Compose();

        Assert.Equal(1, firstModule.DeclareCallCount);
        Assert.Equal(1, secondModule.DeclareCallCount);
        Assert.Equal(
            ["first-panel", "second-panel"],
            composition.PanelRegistry.GetAll().Select(panel => panel.Id));
        Assert.Equal(
            ["first.action", "second.action"],
            composition.ActionRegistry.GetAll().Select(action => action.Id));
    }

    [Fact]
    public void Compose_rejects_duplicate_panel_id_before_returning_composition()
    {
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "test.first",
                panels:
                [
                    CreatePanel("shared-panel"),
                ]),
            new TestExtensionModule(
                "test.second",
                panels:
                [
                    CreatePanel("shared-panel"),
                ]),
        ]);

        var exception = Assert.Throws<InvalidOperationException>(() => host.Compose());

        Assert.Contains("shared-panel", exception.Message);
        Assert.Contains("test.first", exception.Message);
        Assert.Contains("test.second", exception.Message);
        Assert.Equal(
            "Panel id 'shared-panel' is contributed by both 'test.first' and 'test.second'.",
            exception.Message);
    }

    [Fact]
    public void Compose_rejects_duplicate_action_id_before_returning_composition()
    {
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "test.first",
                actions:
                [
                    CreateAction("shared.action"),
                ]),
            new TestExtensionModule(
                "test.second",
                actions:
                [
                    CreateAction("shared.action"),
                ]),
        ]);

        var exception = Assert.Throws<InvalidOperationException>(() => host.Compose());

        Assert.Contains("shared.action", exception.Message);
        Assert.Contains("test.first", exception.Message);
        Assert.Contains("test.second", exception.Message);
        Assert.Equal(
            "Workbench action id 'shared.action' is contributed by both 'test.first' and 'test.second'.",
            exception.Message);
    }

    [Fact]
    public async Task ActivateAsync_disposes_started_leases_in_reverse_order_when_later_module_fails()
    {
        var disposalOrder = new List<string>();
        var firstLease = new RecordingLease("first", disposalOrder);
        var secondLease = new RecordingLease("second", disposalOrder);
        var expectedException = new InvalidOperationException("activation failed");
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule("test.first", lease: firstLease),
            new TestExtensionModule("test.second", lease: secondLease),
            new TestExtensionModule("test.third", activateException: expectedException),
        ]);

        var exception = await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await host.ActivateAsync(CancellationToken.None));

        Assert.Same(expectedException, exception);
        Assert.Equal(["second", "first"], disposalOrder);
    }

    private static PanelDescriptor CreatePanel(string id)
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

    private static WorkbenchActionDescriptor CreateAction(string id)
    {
        return new WorkbenchActionDescriptor(
            id,
            id,
            WorkbenchActionKind.OpenCommandPalette,
            $"Tools/{id}");
    }

    private sealed class TestExtensionModule : IEditorExtensionModule
    {
        private readonly IReadOnlyList<PanelDescriptor> panels_;
        private readonly IReadOnlyList<WorkbenchActionDescriptor> actions_;
        private readonly IAsyncDisposable? lease_;
        private readonly Exception? activateException_;

        public TestExtensionModule(
            string id,
            IReadOnlyList<PanelDescriptor>? panels = null,
            IReadOnlyList<WorkbenchActionDescriptor>? actions = null,
            IAsyncDisposable? lease = null,
            Exception? activateException = null)
        {
            Id = new EditorExtensionId(id);
            panels_ = panels ?? [];
            actions_ = actions ?? [];
            lease_ = lease;
            activateException_ = activateException;
        }

        public EditorExtensionId Id { get; }

        public int DeclareCallCount { get; private set; }

        public void Declare(IEditorContributionBuilder builder)
        {
            DeclareCallCount++;

            foreach (var panel in panels_)
            {
                builder.AddPanel(panel);
            }

            foreach (var action in actions_)
            {
                builder.AddAction(action);
            }
        }

        public ValueTask<IAsyncDisposable?> ActivateAsync(
            IEditorExtensionActivationContext context,
            CancellationToken cancellationToken)
        {
            if (activateException_ is not null)
            {
                throw activateException_;
            }

            return ValueTask.FromResult(lease_);
        }
    }

    private sealed class RecordingLease : IAsyncDisposable
    {
        private readonly string id_;
        private readonly IList<string> disposalOrder_;

        public RecordingLease(string id, IList<string> disposalOrder)
        {
            id_ = id;
            disposalOrder_ = disposalOrder;
        }

        public ValueTask DisposeAsync()
        {
            disposalOrder_.Add(id_);
            return ValueTask.CompletedTask;
        }
    }
}
