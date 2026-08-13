using System;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.TestSupport;
using Editor.Shell.Composition;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioProcessSessionTests
{
    [Fact]
    public async Task Composition_teardown_disposes_selection_before_catalog_and_project_session()
    {
        var order = new List<string>();
        var projectSession = new TestProjectSession
        {
            DisposeHandler = () => order.Add("session"),
        };
        var catalog = new TestProjectAssetCatalog
        {
            DisposeHandler = () => order.Add("catalog"),
        };
        var selection = new TestEditorSelectionService
        {
            DisposeHandler = () => order.Add("selection"),
        };
        var shellViewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            catalog,
            selection);
        var composition = new StudioCompositionSession(shellViewModel);

        await composition.DisposeAsync();

        Assert.Equal(["selection", "catalog", "session"], order);
        Assert.Equal(1, selection.DisposeCount);
        Assert.Equal(1, catalog.DisposeCount);
        Assert.Equal(1, projectSession.DisposeCount);
    }

    [Fact]
    public async Task Start_and_stop_publish_complete_managed_teardown_evidence()
    {
        var shellViewModel = StudioShellTestFactory.Create();
        var composition = new StudioCompositionSession(shellViewModel);
        var process = CreateProcess(composition);

        await process.StartAsync();
        var receipt = await process.StopAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(StudioProcessSessionState.Stopped, process.State);
        Assert.Equal(StudioProcessStopStatus.Completed, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.Disposed,
            receipt.CompositionStatus);
        Assert.Empty(receipt.Failures);
        Assert.Throws<ObjectDisposedException>(shellViewModel.MarkReady);
    }

    [Fact]
    public async Task Stop_during_cooperative_start_cancels_start_and_reaches_stopped()
    {
        var factoryEntered = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var process = new StudioProcessSession(
            async cancellationToken =>
            {
                factoryEntered.SetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
                throw new InvalidOperationException("Unreachable.");
            });
        var startTask = InvokeWithoutTestSynchronizationContext(
            () => process.StartAsync().AsTask());
        Assert.True(factoryEntered.Task.IsCompleted);
        var stopTask = InvokeWithoutTestSynchronizationContext(
            () => process.StopAsync(TimeSpan.FromSeconds(1)).AsTask());
        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            async () => await startTask.WaitAsync(TimeSpan.FromSeconds(1)));
        var receipt = await stopTask.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Equal(StudioProcessSessionState.Stopped, process.State);
        Assert.Equal(StudioProcessStopStatus.Completed, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.NotCreated,
            receipt.CompositionStatus);
    }

    [Fact]
    public async Task Start_failure_is_observed_and_later_stop_reports_no_owned_composition()
    {
        var process = new StudioProcessSession(
            _ => throw new InvalidOperationException("composition failure"));

        var failure = await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await process.StartAsync());

        Assert.Contains("composition failure", failure.Message, StringComparison.Ordinal);
        Assert.Equal(StudioProcessSessionState.Faulted, process.State);

        var receipt = await process.StopAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(StudioProcessStopStatus.Completed, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.NotCreated,
            receipt.CompositionStatus);
        Assert.Equal(StudioProcessSessionState.Stopped, process.State);
    }

    [Fact]
    public async Task Managed_dispose_failure_is_typed_and_requires_process_exit()
    {
        var composition = new ControlledCompositionSession(
            blockUntilReleased: false,
            new InvalidOperationException("managed dispose failure"));
        var process = CreateProcess(composition);
        await process.StartAsync();

        var receipt = await process.StopAsync(TimeSpan.FromSeconds(1));

        Assert.Equal(StudioProcessStopStatus.Faulted, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.DisposeFaulted,
            receipt.CompositionStatus);
        var failure = Assert.Single(receipt.Failures);
        Assert.Equal("studio.teardown.managed-session.failed", failure.Code);
        Assert.Contains("managed dispose failure", failure.Message, StringComparison.Ordinal);
        Assert.Equal(StudioProcessSessionState.Faulted, process.State);
        Assert.Equal(1, composition.DisposeCount);
    }

    [Fact]
    public async Task Managed_dispose_timeout_reports_incomplete_owner_without_claiming_stop()
    {
        var composition = new ControlledCompositionSession();
        var process = CreateProcess(composition);
        await process.StartAsync();

        var receipt = await process.StopAsync(TimeSpan.Zero);

        Assert.Equal(StudioProcessStopStatus.TimedOut, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.DisposeTimedOut,
            receipt.CompositionStatus);
        Assert.Empty(receipt.Failures);
        Assert.Equal(StudioProcessSessionState.Stopping, process.State);
        Assert.Equal(1, composition.DisposeCount);

        composition.Release();
        await composition.DisposeCompleted.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Same(
            receipt,
            await process.StopAsync(TimeSpan.FromSeconds(1)));
    }

    [Fact]
    public void Deadline_covers_a_start_owner_that_ignores_lifetime_cancellation()
    {
        RunScenarioWithoutTestSynchronizationContext(
            async () =>
            {
                var factoryEntered = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                var releaseFactory = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                var process = new StudioProcessSession(
                    async _ =>
                    {
                        factoryEntered.SetResult();
                        await releaseFactory.Task;
                        return new StudioCompositionSession(StudioShellTestFactory.Create());
                    });

                var startTask = process.StartAsync().AsTask();
                await factoryEntered.Task.WaitAsync(TimeSpan.FromSeconds(1));

                var receipt = await process.StopAsync(TimeSpan.FromSeconds(1))
                    .AsTask()
                    .WaitAsync(TimeSpan.FromSeconds(2));

                Assert.Equal(StudioProcessStopStatus.TimedOut, receipt.Status);
                Assert.Equal(
                    StudioCompositionTeardownStatus.LifecycleGateTimedOut,
                    receipt.CompositionStatus);
                Assert.Equal(StudioProcessSessionState.Stopping, process.State);

                releaseFactory.SetResult();
                await Assert.ThrowsAnyAsync<OperationCanceledException>(
                    async () => await startTask.WaitAsync(TimeSpan.FromSeconds(1)));
                Assert.Same(
                    receipt,
                    await process.StopAsync(TimeSpan.FromSeconds(1)));
            });
    }

    [Fact]
    public async Task Canceling_one_stop_waiter_does_not_cancel_owned_teardown()
    {
        var composition = new ControlledCompositionSession();
        var process = CreateProcess(composition);
        await process.StartAsync();
        using var cancellation = new CancellationTokenSource();

        var canceledWait = process.StopAsync(
            Timeout.InfiniteTimeSpan,
            cancellation.Token).AsTask();
        await composition.DisposeEntered.Task;
        cancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            async () => await canceledWait);

        composition.Release();
        var firstReceipt = await process.StopAsync(TimeSpan.FromSeconds(1));
        var secondReceipt = await process.StopAsync(TimeSpan.FromSeconds(1));

        Assert.Same(firstReceipt, secondReceipt);
        Assert.Equal(StudioProcessStopStatus.Completed, firstReceipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.Disposed,
            firstReceipt.CompositionStatus);
        Assert.Equal(1, composition.DisposeCount);
    }

    [Fact]
    public async Task Lifetime_cancellation_callback_failure_is_preserved_in_the_receipt()
    {
        var factoryEntered = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var process = new StudioProcessSession(
            async cancellationToken =>
            {
                using var registration = cancellationToken.Register(
                    static () => throw new InvalidOperationException("cancel callback failure"));
                factoryEntered.SetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
                throw new InvalidOperationException("Unreachable.");
            });

        var startTask = InvokeWithoutTestSynchronizationContext(
            () => process.StartAsync().AsTask());
        await factoryEntered.Task.WaitAsync(TimeSpan.FromSeconds(1));
        var stopTask = InvokeWithoutTestSynchronizationContext(
            () => process.StopAsync(TimeSpan.FromSeconds(1)).AsTask());

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            async () => await startTask);
        var receipt = await stopTask;

        Assert.Equal(StudioProcessStopStatus.Faulted, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.NotCreated,
            receipt.CompositionStatus);
        var failure = Assert.Single(receipt.Failures);
        Assert.Equal("studio.teardown.lifetime-cancel.failed", failure.Code);
        Assert.Contains("cancel callback failure", failure.Message, StringComparison.Ordinal);
        Assert.Equal(StudioProcessSessionState.Faulted, process.State);
    }

    [Fact]
    public async Task Lifetime_cancellation_timeout_is_typed_and_late_failure_does_not_mutate_receipt()
    {
        using var callbackEntered = new ManualResetEventSlim();
        using var callbackCompleted = new ManualResetEventSlim();
        using var releaseCallback = new ManualResetEventSlim();
        var process = CreateProcess(
            new StudioCompositionSession(StudioShellTestFactory.Create()));
        var lifetimeCancellationField = typeof(StudioProcessSession).GetField(
            "lifetimeCancellation_",
            BindingFlags.Instance | BindingFlags.NonPublic);
        var lifetimeCancellation = Assert.IsType<CancellationTokenSource>(
            lifetimeCancellationField?.GetValue(process));
        using var registration = lifetimeCancellation.Token.Register(
            () =>
            {
                callbackEntered.Set();
                try
                {
                    releaseCallback.Wait();
                    throw new InvalidOperationException("late cancel callback failure");
                }
                finally
                {
                    callbackCompleted.Set();
                }
            });

        StudioTeardownReceipt receipt;
        try
        {
            receipt = await process.StopAsync(TimeSpan.Zero)
                .AsTask()
                .WaitAsync(TimeSpan.FromSeconds(1));
            Assert.True(callbackEntered.Wait(TimeSpan.FromSeconds(1)));

            Assert.Equal(StudioProcessStopStatus.TimedOut, receipt.Status);
            Assert.Equal(
                StudioCompositionTeardownStatus.LifetimeCancellationTimedOut,
                receipt.CompositionStatus);
            Assert.Empty(receipt.Failures);
            Assert.Equal(StudioProcessSessionState.Stopping, process.State);
        }
        finally
        {
            releaseCallback.Set();
        }

        Assert.True(callbackCompleted.Wait(TimeSpan.FromSeconds(1)));
        Assert.Same(
            receipt,
            await process.StopAsync(TimeSpan.FromSeconds(1)));
        Assert.Equal(StudioProcessSessionState.Stopping, process.State);
    }

    [Fact]
    public async Task Late_dispose_failure_after_timeout_does_not_mutate_receipt()
    {
        var composition = new ControlledCompositionSession(
            disposeException: new InvalidOperationException("late dispose failure"));
        var process = CreateProcess(composition);
        await process.StartAsync();

        var receipt = await process.StopAsync(TimeSpan.Zero);

        Assert.Equal(StudioProcessStopStatus.TimedOut, receipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.DisposeTimedOut,
            receipt.CompositionStatus);

        composition.Release();
        await composition.DisposeCompleted.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Same(
            receipt,
            await process.StopAsync(TimeSpan.FromSeconds(1)));
        Assert.Empty(receipt.Failures);
        Assert.Equal(StudioProcessSessionState.Stopping, process.State);
    }

    [Fact]
    public void Stop_rejects_invalid_timeout_before_starting_owned_teardown()
    {
        var process = CreateProcess(
            new StudioCompositionSession(StudioShellTestFactory.Create()));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => process.StopAsync(TimeSpan.FromMilliseconds(-2)));
        Assert.Equal(StudioProcessSessionState.Created, process.State);
    }

    private static T InvokeWithoutTestSynchronizationContext<T>(Func<T> invocation)
        where T : class
    {
        T? result = null;
        Exception? failure = null;
        var invocationThread = new Thread(
            () =>
            {
                try
                {
                    result = invocation();
                }
                catch (Exception exception)
                {
                    failure = exception;
                }
            })
        {
            IsBackground = true,
        };

        invocationThread.Start();
        Assert.True(invocationThread.Join(TimeSpan.FromSeconds(1)));
        Assert.Null(failure);
        return Assert.IsAssignableFrom<T>(result);
    }

    private static void RunScenarioWithoutTestSynchronizationContext(
        Func<Task> scenario)
    {
        Exception? failure = null;
        var completed = new ManualResetEventSlim();
        _ = Task.Run(
            async () =>
            {
                try
                {
                    await scenario().ConfigureAwait(false);
                }
                catch (Exception exception)
                {
                    failure = exception;
                }
                finally
                {
                    completed.Set();
                }
            });

        var finished = completed.Wait(TimeSpan.FromSeconds(5));
        if (finished)
        {
            completed.Dispose();
        }

        Assert.True(finished);
        Assert.Null(failure);
    }

    private static StudioProcessSession CreateProcess(IAsyncDisposable composition)
    {
        return new StudioProcessSession(
            cancellationToken =>
            {
                cancellationToken.ThrowIfCancellationRequested();
                return ValueTask.FromResult(composition);
            });
    }

    private sealed class ControlledCompositionSession : IAsyncDisposable
    {
        private readonly TaskCompletionSource release_ = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly Exception? disposeException_;
        private int disposeCount_;

        public ControlledCompositionSession(
            bool blockUntilReleased = true,
            Exception? disposeException = null)
        {
            disposeException_ = disposeException;
            if (!blockUntilReleased)
            {
                release_.SetResult();
            }
        }

        public TaskCompletionSource DisposeEntered { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource DisposeCompleted { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public int DisposeCount => Volatile.Read(ref disposeCount_);

        public async ValueTask DisposeAsync()
        {
            Interlocked.Increment(ref disposeCount_);
            DisposeEntered.TrySetResult();
            try
            {
                await release_.Task.ConfigureAwait(false);
                if (disposeException_ is not null)
                {
                    throw disposeException_;
                }
            }
            finally
            {
                DisposeCompleted.TrySetResult();
            }
        }

        public void Release()
        {
            release_.TrySetResult();
        }
    }
}
