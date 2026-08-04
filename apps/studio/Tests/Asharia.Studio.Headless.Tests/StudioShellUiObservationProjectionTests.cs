#if DEBUG
using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.TestSupport;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Editor.Shell.Composition;
using Editor.Shell.Observation;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioShellUiObservationProjectionTests
{
    [AvaloniaFact]
    public async Task Real_shell_projects_stable_bounded_semantics_across_state_change()
    {
        using var viewModel = StudioShellTestFactory.Create();
        var window = ShowWindow(viewModel);
        var projection = new StudioShellUiObservationProjection(window);

        try
        {
            var list = await projection.ListWindowsAsync(
                new UiListWindowsParameters(),
                CancellationToken.None);
            var startingTree = await projection.ReadTreeAsync(Request(), CancellationToken.None);

            Assert.True(list.Succeeded);
            var listedWindow = Assert.Single(list.Value!.Windows);
            Assert.Equal("StudioShellWindow", listedWindow.WindowId);
            Assert.Equal("Asharia Studio", listedWindow.Name);
            Assert.True(listedWindow.IsVisible);
            Assert.True(listedWindow.IsEnabled);

            Assert.True(startingTree.Succeeded);
            Assert.False(startingTree.Value!.IsTruncated);
            Assert.Null(startingTree.Value.TruncationReason);
            Assert.Equal(
                [
                    "StudioShellWindow",
                    "StudioShellStartingState",
                    "StudioShellNoProjectState",
                    "StudioShellNoDocumentState",
                    "StudioShellActiveProjectState",
                    "StudioHierarchyPanel",
                    "StudioInspectorPanel",
                ],
                startingTree.Value.Nodes.Select(static node => node.ElementId));
            Assert.All(
                startingTree.Value.Nodes.Skip(1).Take(4),
                node => Assert.Equal("StudioShellWindow", node.ParentElementId));
            Assert.Equal(
                "StudioShellActiveProjectState",
                Node(startingTree.Value, "StudioHierarchyPanel").ParentElementId);
            Assert.Equal(
                "StudioShellActiveProjectState",
                Node(startingTree.Value, "StudioInspectorPanel").ParentElementId);
            Assert.Equal(
                ObservationUiRoles.Status,
                Node(startingTree.Value, "StudioShellStartingState").Role);
            Assert.True(Node(startingTree.Value, "StudioShellStartingState").IsVisible);
            Assert.False(Node(startingTree.Value, "StudioShellNoProjectState").IsVisible);
            Assert.False(Node(startingTree.Value, "StudioShellActiveProjectState").IsVisible);
            Assert.False(Node(startingTree.Value, "StudioShellNoDocumentState").IsVisible);

            viewModel.MarkReady();
            Dispatcher.UIThread.RunJobs();
            var readyTree = await projection.ReadTreeAsync(Request(), CancellationToken.None);

            Assert.True(readyTree.Succeeded);
            var ready = Assert.IsType<UiTreeReadResult>(readyTree.Value);
            Assert.False(Node(ready, "StudioShellStartingState").IsVisible);
            Assert.True(Node(ready, "StudioShellNoProjectState").IsVisible);
            Assert.False(Node(ready, "StudioShellActiveProjectState").IsVisible);
            Assert.True(Node(ready, "StudioShellNoDocumentState").IsVisible);
            Assert.False(Node(ready, "StudioHierarchyPanel").IsVisible);
            Assert.False(Node(ready, "StudioInspectorPanel").IsVisible);

            var envelope = new ObservationResponse<UiTreeReadResult>(
                ObservationProtocolVersion.Current,
                new ObservationRequestId(Guid.NewGuid()),
                new StudioInstanceId(Guid.NewGuid()),
                EndpointGeneration: 1,
                ObservationOutcome.Complete,
                ready);
            Assert.True(
                ObservationProtocolJson.ReadResponse<UiTreeReadResult>(
                    ObservationProtocolJson.WriteResponse(envelope)).Succeeded);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Read_tree_reports_typed_failure_and_explicit_truncation()
    {
        using var viewModel = StudioShellTestFactory.Create();
        var window = ShowWindow(viewModel);
        var projection = new StudioShellUiObservationProjection(window);

        try
        {
            var invalidBudget = await projection.ReadTreeAsync(
                new UiReadTreeParameters("StudioShellWindow", MaxDepth: -1, MaxNodes: 0),
                CancellationToken.None);
            var wrongWindow = await projection.ReadTreeAsync(
                new UiReadTreeParameters("OtherWindow", MaxDepth: 1, MaxNodes: 8),
                CancellationToken.None);
            var depthLimited = await projection.ReadTreeAsync(
                new UiReadTreeParameters("StudioShellWindow", MaxDepth: 0, MaxNodes: 8),
                CancellationToken.None);
            var nodeLimited = await projection.ReadTreeAsync(
                new UiReadTreeParameters("StudioShellWindow", MaxDepth: 4, MaxNodes: 2),
                CancellationToken.None);

            Assert.Equal("observation.ui.request-invalid", invalidBudget.Failure!.Code);
            Assert.Equal("observation.ui.window-not-found", wrongWindow.Failure!.Code);
            Assert.True(depthLimited.Succeeded);
            Assert.True(depthLimited.Value!.IsTruncated);
            Assert.Equal("ui.max-depth", depthLimited.Value.TruncationReason);
            Assert.Single(depthLimited.Value.Nodes);
            Assert.True(nodeLimited.Succeeded);
            Assert.True(nodeLimited.Value!.IsTruncated);
            Assert.Equal("ui.max-nodes", nodeLimited.Value.TruncationReason);
            Assert.Equal(2, nodeLimited.Value.Nodes.Length);

            var partialEnvelope = new ObservationResponse<UiTreeReadResult>(
                ObservationProtocolVersion.Current,
                new ObservationRequestId(Guid.NewGuid()),
                new StudioInstanceId(Guid.NewGuid()),
                EndpointGeneration: 1,
                ObservationOutcome.Partial,
                nodeLimited.Value,
                Truncation: new ObservationTruncation(
                    IsTruncated: true,
                    Reason: nodeLimited.Value.TruncationReason));
            Assert.True(
                ObservationProtocolJson.ReadResponse<UiTreeReadResult>(
                    ObservationProtocolJson.WriteResponse(partialEnvelope)).Succeeded);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Projection_marshals_worker_reads_and_honors_cancellation_and_window_shutdown()
    {
        using var viewModel = StudioShellTestFactory.Create();
        var window = ShowWindow(viewModel);
        var projection = new StudioShellUiObservationProjection(window);

        var workerRead = Task.Run(async () => await projection.ReadTreeAsync(
            Request(),
            CancellationToken.None));
        PumpUiUntil(workerRead, TimeSpan.FromSeconds(5));
        var workerResult = await workerRead;
        Assert.True(workerResult.Succeeded);

        using var deadline = new CancellationTokenSource(TimeSpan.FromMilliseconds(50));
        using var queued = new ManualResetEventSlim();
        var deadlineRead = Task.Run(async () =>
        {
            var pending = projection.ReadTreeAsync(Request(), deadline.Token).AsTask();
            queued.Set();
            return await pending;
        });
        Assert.True(queued.Wait(TimeSpan.FromSeconds(1)));
        Assert.True(SpinWait.SpinUntil(
            () => deadline.IsCancellationRequested,
            TimeSpan.FromSeconds(1)));
        PumpUiUntil(deadlineRead, TimeSpan.FromSeconds(5));
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await deadlineRead);

        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            async () => await projection.ReadTreeAsync(Request(), cancelled.Token));

        window.Close();
        Dispatcher.UIThread.RunJobs();
        var listAfterClose = await projection.ListWindowsAsync(
            new UiListWindowsParameters(),
            CancellationToken.None);
        var readAfterClose = await projection.ReadTreeAsync(Request(), CancellationToken.None);

        Assert.True(listAfterClose.Succeeded);
        Assert.Empty(listAfterClose.Value!.Windows);
        Assert.Equal("observation.ui.window-not-found", readAfterClose.Failure!.Code);
    }

    [AvaloniaFact]
    public async Task Debug_product_composition_advertises_and_tears_down_the_real_shell_provider()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        var viewModel = StudioShellTestFactory.Create();
        var window = ShowWindow(viewModel);
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        StudioCompositionSession? composition = null;
        try
        {
            var owned = await StudioCompositionSession.CreateAsync(
                viewModel,
                window,
                hub,
                CancellationToken.None,
                enableReadOnlyDevelopmentObservation: true);
            composition = Assert.IsType<StudioCompositionSession>(owned);
            var host = Assert.IsType<StudioDevelopmentHost>(composition.DevelopmentHost);
            var endpoint = Assert.IsType<StudioDevelopmentPipeEndpoint>(
                composition.DevelopmentEndpoint);
            Assert.True(System.IO.File.Exists(endpoint.ManifestPath));

            var descriptor = await host.DescribeAsync(Request(
                host,
                ObservationMethodId.SessionDescribe,
                new SessionDescribeParameters()));
            var windows = await host.ListWindowsAsync(Request(
                host,
                ObservationMethodId.UiListWindows,
                new UiListWindowsParameters()));
            var tree = await host.ReadTreeAsync(Request(
                host,
                ObservationMethodId.UiReadTree,
                Request()));

            Assert.Equal(ObservationOutcome.Complete, descriptor.Outcome);
            Assert.Contains(
                descriptor.Value!.Capabilities,
                capability => capability.CapabilityId == "ui.listWindows");
            Assert.Contains(
                descriptor.Value.Capabilities,
                capability => capability.CapabilityId == "ui.readTree");
            Assert.Equal("StudioShellWindow", Assert.Single(windows.Value!.Windows).WindowId);
            Assert.Equal(7, tree.Value!.Nodes.Length);

            await composition.DisposeAsync();
            composition = null;
            Assert.False(System.IO.File.Exists(endpoint.ManifestPath));
            Assert.Equal(StudioDevelopmentPipeServerState.Stopped, endpoint.PipeState);
            var afterStop = await host.ListWindowsAsync(Request(
                host,
                ObservationMethodId.UiListWindows,
                new UiListWindowsParameters()));
            Assert.Equal(ObservationOutcome.Failed, afterStop.Outcome);
            Assert.Equal("observation.host.unavailable", afterStop.Failure!.Code);
        }
        finally
        {
            if (composition is not null)
            {
                await composition.DisposeAsync();
            }

            window.Close();
            viewModel.Dispose();
        }
    }

    private static MainWindow ShowWindow(StudioShellViewModel viewModel)
    {
        var window = new MainWindow
        {
            DataContext = viewModel,
        };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        return window;
    }

    private static UiReadTreeParameters Request() =>
        new(
            "StudioShellWindow",
            ObservationProtocolLimits.MaxUiDepth,
            ObservationProtocolLimits.MaxUiNodes);

    private static ObservationRequest<TParameters> Request<TParameters>(
        StudioDevelopmentHost host,
        ObservationMethodId method,
        TParameters parameters)
        where TParameters : class =>
        new(
            ObservationProtocolVersion.Current,
            new ObservationRequestId(Guid.NewGuid()),
            host.StudioInstanceId,
            host.EndpointGeneration,
            method,
            TimeoutMilliseconds: 1_000,
            parameters);

    private static ObservationUiNode Node(UiTreeReadResult tree, string id) =>
        Assert.Single(tree.Nodes, node => string.Equals(
            node.ElementId,
            id,
            StringComparison.Ordinal));

    private static void PumpUiUntil(Task task, TimeSpan timeout)
    {
        var timer = Stopwatch.StartNew();
        while (!task.IsCompleted && timer.Elapsed < timeout)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Yield();
        }

        Assert.True(task.IsCompleted, "Worker UI projection did not complete within its test deadline.");
    }
}
#endif
