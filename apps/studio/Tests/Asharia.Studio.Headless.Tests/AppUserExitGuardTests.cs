using System;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.TestSupport;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Editor;
using Editor.Shell.Composition;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class AppUserExitGuardTests
{
    private const BindingFlags PrivateInstance =
        BindingFlags.Instance | BindingFlags.NonPublic;

    [AvaloniaFact]
    public async Task Repeated_exit_requests_share_one_prompt_and_cancel_allows_retry()
    {
        var app = Assert.IsType<App>(Avalonia.Application.Current);
        var session = new TestProjectSession();
        session.Publish(DirtySnapshot());
        var prompt = new BlockingCancelPrompt();
        SetField(
            app,
            "documentTransitions_",
            new ProjectDocumentTransitionCoordinator(session, prompt));

        RequestUserShutdown(app);
        await prompt.FirstRequestEntered.Task.WaitAsync(TimeSpan.FromSeconds(1));

        RequestUserShutdown(app);

        Assert.Equal(1, prompt.RequestCount);
        Assert.NotNull(GetField<Task>(app, "userExitResolutionTask_"));
        Assert.Null(GetField<Task>(app, "shutdownTask_"));

        prompt.ReleaseFirstRequest();
        await WaitUntilAsync(
            () => GetField<Task>(app, "userExitResolutionTask_") is null);

        Assert.Null(GetField<Task>(app, "shutdownTask_"));
        RequestUserShutdown(app);
        await WaitUntilAsync(() => prompt.RequestCount == 2);

        Assert.Equal(2, prompt.RequestCount);
        Assert.Null(GetField<Task>(app, "shutdownTask_"));
    }

    [AvaloniaFact]
    public async Task Disposed_startup_shell_does_not_block_process_stop_or_final_shutdown()
    {
        var app = Assert.IsType<App>(Avalonia.Application.Current);
        var diagnostics = Assert.IsType<StudioDiagnosticHub>(
            Assert.IsAssignableFrom<IStudioDiagnosticHubProvider>(app).Diagnostics);
        var shell = StudioShellTestFactory.Create();
        var mainWindow = new MainWindow { DataContext = shell };
        shell.Dispose();
        var process = new StudioProcessSession(
            _ => throw new InvalidOperationException("Injected composition startup failure."));
        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await process.StartAsync());
        var desktop = DispatchProxy.Create<
            IClassicDesktopStyleApplicationLifetime,
            RecordingDesktopLifetime>();
        var recordingDesktop = Assert.IsAssignableFrom<RecordingDesktopLifetime>(desktop);

        await app.StopAndShutdownOwnedSessionAsync(
            desktop,
            mainWindow,
            process,
            requestedExitCode: 1);

        Assert.Null(mainWindow.DataContext);
        Assert.Equal(StudioProcessSessionState.Stopped, process.State);
        Assert.NotNull(app.LastTeardownReceipt);
        Assert.Equal(StudioProcessStopStatus.Completed, app.LastTeardownReceipt.Status);
        Assert.Equal(
            StudioCompositionTeardownStatus.NotCreated,
            app.LastTeardownReceipt.CompositionStatus);
        Assert.Equal(1, recordingDesktop.ShutdownCallCount);
        Assert.Equal(1, recordingDesktop.ExitCode);
        Assert.Single(diagnostics.ReadDiagnostics(
                maxCount: diagnostics.DiagnosticCapacity,
                channel: StudioDiagnosticChannel.Problem)
            .Items
            .Where(record => record.Code == "studio.lifecycle.shell-stop.failed"));
    }

    private static void RequestUserShutdown(App app)
    {
        var method = typeof(App).GetMethod("RequestUserShutdown", PrivateInstance);
        Assert.NotNull(method);
        method.Invoke(app, parameters: null);
    }

    private static void SetField<T>(App app, string name, T value)
    {
        var field = typeof(App).GetField(name, PrivateInstance);
        Assert.NotNull(field);
        field.SetValue(app, value);
    }

    private static T? GetField<T>(App app, string name)
        where T : class
    {
        var field = typeof(App).GetField(name, PrivateInstance);
        Assert.NotNull(field);
        return field.GetValue(app) as T;
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
        while (!condition())
        {
            Dispatcher.UIThread.RunJobs();
            await Task.Delay(10, timeout.Token);
        }
        Dispatcher.UIThread.RunJobs();
    }

    private static ProjectSessionSnapshot DirtySnapshot()
    {
        var project = new ActiveProjectSnapshot(
            ProjectSessionId.CreateNew(),
            Guid.NewGuid(),
            "Sample",
            "C:\\Projects\\Sample");
        return ProjectSessionSnapshot.Ready(
            project,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision: 2,
                savedRevision: 1,
                entities: []),
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: true,
            canRedo: false,
            undoLabel: "Create Entity",
            redoLabel: null);
    }

    private sealed class BlockingCancelPrompt : IProjectDocumentTransitionPrompt
    {
        private readonly TaskCompletionSource<ProjectDocumentTransitionDecision>
            firstDecision_ = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private int requestCount_;

        public TaskCompletionSource FirstRequestEntered { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int RequestCount => Volatile.Read(ref requestCount_);

        public ValueTask<ProjectDocumentTransitionDecision> ChooseAsync(
            ProjectDocumentTransitionPrompt prompt,
            CancellationToken cancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(prompt);
            cancellationToken.ThrowIfCancellationRequested();
            var request = Interlocked.Increment(ref requestCount_);
            if (request == 1)
            {
                FirstRequestEntered.TrySetResult();
                return new ValueTask<ProjectDocumentTransitionDecision>(
                    firstDecision_.Task);
            }

            return ValueTask.FromResult(ProjectDocumentTransitionDecision.Cancel);
        }

        public void ReleaseFirstRequest() =>
            firstDecision_.TrySetResult(ProjectDocumentTransitionDecision.Cancel);
    }

    public class RecordingDesktopLifetime : DispatchProxy
    {
        public int ShutdownCallCount { get; private set; }

        public int? ExitCode { get; private set; }

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            if (string.Equals(targetMethod.Name, "Shutdown", StringComparison.Ordinal))
            {
                ShutdownCallCount++;
                ExitCode = Assert.IsType<int>(Assert.Single(args ?? []));
            }

            return targetMethod.ReturnType == typeof(void)
                ? null
                : targetMethod.ReturnType.IsValueType
                    ? Activator.CreateInstance(targetMethod.ReturnType)
                    : null;
        }
    }
}
