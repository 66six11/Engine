using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Editor.Shell.Composition;
using Editor.Shell.Docking;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class EditorExtensionHostTests
{
    [Fact]
    public void EditorExtensionId_rejects_empty_values_and_uses_class_value_equality()
    {
        Assert.True(typeof(EditorExtensionId).IsClass);
        Assert.True(typeof(EditorExtensionId).IsSealed);
        Assert.Throws<ArgumentException>(() => new EditorExtensionId(string.Empty));
        Assert.Throws<ArgumentException>(() => new EditorExtensionId("   "));
        Assert.Equal(new EditorExtensionId("test.extension"), new EditorExtensionId("test.extension"));
        Assert.NotEqual(new EditorExtensionId("test.extension"), new EditorExtensionId("test.other"));
    }

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
    public void Compose_registers_panel_and_action_owners()
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

        var panelRegistry = Assert.IsType<PanelRegistry>(composition.PanelRegistry);
        var actionRegistry = Assert.IsType<WorkbenchActionRegistry>(composition.ActionRegistry);
        Assert.Equal(firstModule.Id, panelRegistry.GetOwnerId("first-panel"));
        Assert.Equal(secondModule.Id, panelRegistry.GetOwnerId("second-panel"));
        Assert.Equal(firstModule.Id, actionRegistry.GetOwnerId("first.action"));
        Assert.Equal(secondModule.Id, actionRegistry.GetOwnerId("second.action"));
    }

    [Fact]
    public async Task DisposeAsync_removes_registered_panel_and_action_contributions()
    {
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "test.first",
                panels:
                [
                    CreatePanel("first-panel"),
                ],
                actions:
                [
                    CreateAction("first.action"),
                ]),
        ]);
        var composition = host.Compose();

        await host.DisposeAsync();

        Assert.Empty(composition.PanelRegistry.GetAll());
        Assert.Empty(composition.ActionRegistry.GetAll());
    }

    [Fact]
    public void Compose_rejects_duplicate_extension_id_before_declaring_modules()
    {
        var firstModule = new TestExtensionModule("test.duplicate");
        var secondModule = new TestExtensionModule("test.duplicate");
        var host = new EditorExtensionHost([firstModule, secondModule]);

        var exception = Assert.Throws<InvalidOperationException>(() => host.Compose());

        Assert.Equal(
            "Editor extension id 'test.duplicate' is registered more than once.",
            exception.Message);
        Assert.Equal(0, firstModule.DeclareCallCount);
        Assert.Equal(0, secondModule.DeclareCallCount);
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

    [Fact]
    public async Task ActivateAsync_removes_registered_contributions_when_activation_fails()
    {
        var disposalOrder = new List<string>();
        var expectedException = new InvalidOperationException("activation failed");
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "test.first",
                panels:
                [
                    CreatePanel("first-panel"),
                ],
                actions:
                [
                    CreateAction("first.action"),
                ],
                lease: new RecordingLease("first", disposalOrder)),
            new TestExtensionModule("test.second", activateException: expectedException),
        ]);
        var composition = host.Compose();

        var exception = await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await host.ActivateAsync(CancellationToken.None));

        Assert.Same(expectedException, exception);
        Assert.Equal(["first"], disposalOrder);
        Assert.Empty(composition.PanelRegistry.GetAll());
        Assert.Empty(composition.ActionRegistry.GetAll());
    }

    [Fact]
    public async Task ActivateAsync_reports_activation_and_dispose_failures_when_rollback_disposal_fails()
    {
        var disposalOrder = new List<string>();
        var activationFailure = new InvalidOperationException("activation failed");
        var disposeFailure = new InvalidOperationException("dispose failed");
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "test.first",
                lease: new RecordingLease("first", disposalOrder, disposeFailure)),
            new TestExtensionModule(
                "test.second",
                lease: new RecordingLease("second", disposalOrder)),
            new TestExtensionModule("test.third", activateException: activationFailure),
        ]);

        var exception = await Assert.ThrowsAsync<AggregateException>(
            async () => await host.ActivateAsync(CancellationToken.None));

        Assert.Equal(["second", "first"], disposalOrder);
        Assert.Equal([activationFailure, disposeFailure], exception.InnerExceptions);
    }

    [Fact]
    public async Task DisposeAsync_attempts_every_lease_in_reverse_order_and_reports_all_failures()
    {
        var disposalOrder = new List<string>();
        var firstDisposeFailure = new InvalidOperationException("first dispose failed");
        var secondDisposeFailure = new InvalidOperationException("second dispose failed");
        var host = new EditorExtensionHost(
        [
            new TestExtensionModule(
                "test.first",
                lease: new RecordingLease("first", disposalOrder, firstDisposeFailure)),
            new TestExtensionModule(
                "test.second",
                lease: new RecordingLease("second", disposalOrder, secondDisposeFailure)),
            new TestExtensionModule(
                "test.third",
                lease: new RecordingLease("third", disposalOrder)),
        ]);
        await host.ActivateAsync(CancellationToken.None);

        var exception = await Assert.ThrowsAsync<AggregateException>(
            async () => await host.DisposeAsync());

        Assert.Equal(["third", "second", "first"], disposalOrder);
        Assert.Equal([secondDisposeFailure, firstDisposeFailure], exception.InnerExceptions);
    }

    [Fact]
    public async Task ActivateAsync_checks_cancellation_before_each_module_activation()
    {
        using var cancellation = new CancellationTokenSource();
        var firstModule = new TestExtensionModule(
            "test.first",
            onActivate: _ => cancellation.Cancel());
        var secondModule = new TestExtensionModule("test.second");
        var host = new EditorExtensionHost([firstModule, secondModule]);

        await Assert.ThrowsAsync<OperationCanceledException>(
            async () => await host.ActivateAsync(cancellation.Token));

        Assert.Equal(1, firstModule.ActivateCallCount);
        Assert.Equal(0, secondModule.ActivateCallCount);
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
        private readonly Action<CancellationToken>? onActivate_;

        public TestExtensionModule(
            string id,
            IReadOnlyList<PanelDescriptor>? panels = null,
            IReadOnlyList<WorkbenchActionDescriptor>? actions = null,
            IAsyncDisposable? lease = null,
            Exception? activateException = null,
            Action<CancellationToken>? onActivate = null)
        {
            Id = new EditorExtensionId(id);
            panels_ = panels ?? [];
            actions_ = actions ?? [];
            lease_ = lease;
            activateException_ = activateException;
            onActivate_ = onActivate;
        }

        public EditorExtensionId Id { get; }

        public int DeclareCallCount { get; private set; }

        public int ActivateCallCount { get; private set; }

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
            ActivateCallCount++;
            onActivate_?.Invoke(cancellationToken);

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
        private readonly Exception? disposeException_;

        public RecordingLease(
            string id,
            IList<string> disposalOrder,
            Exception? disposeException = null)
        {
            id_ = id;
            disposalOrder_ = disposalOrder;
            disposeException_ = disposeException;
        }

        public ValueTask DisposeAsync()
        {
            disposalOrder_.Add(id_);
            if (disposeException_ is not null)
            {
                throw disposeException_;
            }

            return ValueTask.CompletedTask;
        }
    }
}
