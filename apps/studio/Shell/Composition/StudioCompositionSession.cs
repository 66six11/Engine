using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
#if DEBUG
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Editor.Shell.Observation;
#endif
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Composition;

internal sealed class StudioCompositionSession : IAsyncDisposable
{
    private bool isDisposed_;
    private readonly StudioShellViewModel shellViewModel_;
    private readonly IProjectSession projectSession_;
    private readonly IProjectAssetCatalog projectAssetCatalog_;
    private readonly IEditorSelectionService editorSelection_;
    private readonly ViewportPresentationLifetime viewportPresentationLifetime_;
    private readonly ViewportRuntimeBridge viewportRuntime_;
    private readonly Task<ViewportFrameFailure?> viewportWarmUpTask_;
#if DEBUG
    private readonly StudioDevelopmentHost? developmentHost_;
    private readonly StudioDevelopmentPipeEndpoint? developmentEndpoint_;
#endif

    public StudioCompositionSession(
        StudioShellViewModel shellViewModel,
        IProjectSession projectSession)
        : this(shellViewModel, projectSession, startViewportWarmUp: true)
    {
    }

    private StudioCompositionSession(
        StudioShellViewModel shellViewModel,
        IProjectSession projectSession,
        bool startViewportWarmUp)
    {
        ArgumentNullException.ThrowIfNull(shellViewModel);
        ArgumentNullException.ThrowIfNull(projectSession);
        shellViewModel_ = shellViewModel;
        projectSession_ = projectSession;
        projectAssetCatalog_ = shellViewModel.ProjectAssetCatalog;
        editorSelection_ = shellViewModel.EditorSelection;
        viewportPresentationLifetime_ = shellViewModel.ViewportPresentationLifetime;
        viewportRuntime_ = new ViewportRuntimeBridge();
        viewportWarmUpTask_ = startViewportWarmUp
            ? viewportRuntime_.WarmUpAsync()
            : Task.FromResult<ViewportFrameFailure?>(null);
#if DEBUG
        developmentHost_ = null;
        developmentEndpoint_ = null;
#endif
    }

    internal StudioCompositionSession(StudioShellViewModel shellViewModel)
        : this(shellViewModel, shellViewModel.ProjectSession, startViewportWarmUp: false)
    {
    }

#if DEBUG
    private StudioCompositionSession(
        StudioShellViewModel shellViewModel,
        IProjectSession projectSession,
        StudioDevelopmentHost developmentHost,
        StudioDevelopmentPipeEndpoint? developmentEndpoint)
    {
        ArgumentNullException.ThrowIfNull(shellViewModel);
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(developmentHost);
        shellViewModel_ = shellViewModel;
        projectSession_ = projectSession;
        projectAssetCatalog_ = shellViewModel.ProjectAssetCatalog;
        editorSelection_ = shellViewModel.EditorSelection;
        viewportPresentationLifetime_ = shellViewModel.ViewportPresentationLifetime;
        viewportRuntime_ = new ViewportRuntimeBridge();
        viewportWarmUpTask_ = viewportRuntime_.WarmUpAsync();
        developmentHost_ = developmentHost;
        developmentEndpoint_ = developmentEndpoint;
    }
#endif

    public StudioShellViewModel ShellViewModel => shellViewModel_;

#if DEBUG
    internal StudioDevelopmentHost? DevelopmentHost => developmentHost_;

    internal StudioDevelopmentPipeEndpoint? DevelopmentEndpoint =>
        developmentEndpoint_;
#endif

    public static async ValueTask<IAsyncDisposable> CreateAsync(
        StudioShellViewModel shellViewModel,
        IProjectSession projectSession,
        MainWindow? mainWindow,
        IStudioDiagnosticHub diagnostics,
        CancellationToken cancellationToken,
        bool enableReadOnlyDevelopmentObservation = false)
    {
        ArgumentNullException.ThrowIfNull(shellViewModel);
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(diagnostics);
        if (cancellationToken.IsCancellationRequested)
        {
            var cleanupFailures = await DisposeUnpublishedOwnersAsync(
                shellViewModel,
                shellViewModel.ProjectAssetCatalog,
                shellViewModel.EditorSelection,
                projectSession);
            if (cleanupFailures.Count != 0)
            {
                cleanupFailures.Insert(
                    0,
                    new OperationCanceledException(cancellationToken));
                throw new AggregateException(
                    "Studio composition was canceled and teardown did not complete cleanly.",
                    cleanupFailures);
            }
            cancellationToken.ThrowIfCancellationRequested();
        }
#if DEBUG
        return await CreateDebugAsync(
            shellViewModel,
            projectSession,
            mainWindow,
            diagnostics,
            cancellationToken,
            enableReadOnlyDevelopmentObservation);
#else
        return new StudioCompositionSession(shellViewModel, projectSession);
#endif
    }

    internal static ValueTask<IAsyncDisposable> CreateAsync(
        StudioShellViewModel shellViewModel,
        MainWindow? mainWindow,
        IStudioDiagnosticHub diagnostics,
        CancellationToken cancellationToken,
        bool enableReadOnlyDevelopmentObservation = false) =>
        CreateAsync(
            shellViewModel,
            shellViewModel.ProjectSession,
            mainWindow,
            diagnostics,
            cancellationToken,
            enableReadOnlyDevelopmentObservation);

#if DEBUG
    private static async ValueTask<IAsyncDisposable> CreateDebugAsync(
        StudioShellViewModel shellViewModel,
        IProjectSession projectSession,
        MainWindow? mainWindow,
        IStudioDiagnosticHub diagnostics,
        CancellationToken cancellationToken,
        bool enableReadOnlyDevelopmentObservation)
    {
        StudioDevelopmentHost? host = null;
        try
        {
            host = StudioDevelopmentHost.StartForCurrentProcess(
                diagnostics,
                new StudioInstanceId(diagnostics.ProcessIdentity.Value),
                new StudioSessionId(Guid.NewGuid()),
                $"editor/{typeof(StudioCompositionSession).Module.ModuleVersionId:D}",
                "Debug",
                uiObservationSource: mainWindow is null
                    ? null
                    : new StudioShellUiObservationProjection(mainWindow));
            StudioDevelopmentPipeEndpoint? endpoint = null;
            if (enableReadOnlyDevelopmentObservation)
            {
                if (!OperatingSystem.IsWindows())
                {
                    throw new PlatformNotSupportedException(
                        "Studio development observation endpoint is currently Windows-only.");
                }

                endpoint = await StudioDevelopmentPipeEndpoint.StartAsync(
                    host,
                    cancellationToken);
            }

            return new StudioCompositionSession(
                shellViewModel,
                projectSession,
                host,
                endpoint);
        }
        catch (Exception startError)
        {
            var cleanupFailures = new List<Exception>(capacity: 4);
            if (host is not null)
            {
                try
                {
                    await host.DisposeAsync();
                }
                catch (Exception error)
                {
                    cleanupFailures.Add(error);
                }
            }

            cleanupFailures.AddRange(await DisposeUnpublishedOwnersAsync(
                shellViewModel,
                shellViewModel.ProjectAssetCatalog,
                shellViewModel.EditorSelection,
                projectSession));
            if (cleanupFailures.Count != 0)
            {
                cleanupFailures.Insert(0, startError);
                throw new AggregateException(
                    "Studio composition startup failed and teardown did not complete cleanly.",
                    cleanupFailures);
            }

            throw;
        }
    }
#endif

    private static async ValueTask<List<Exception>> DisposeUnpublishedOwnersAsync(
        StudioShellViewModel shellViewModel,
        IProjectAssetCatalog projectAssetCatalog,
        IEditorSelectionService editorSelection,
        IProjectSession projectSession)
    {
        var failures = new List<Exception>(capacity: 4);
        try
        {
            shellViewModel.Dispose();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            editorSelection.Dispose();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            await projectAssetCatalog.DisposeAsync();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            await projectSession.DisposeAsync();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }
        return failures;
    }

    public async ValueTask DisposeAsync()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        var failures = new List<Exception>(capacity: 5);
#if DEBUG
        if (developmentEndpoint_ is not null)
        {
            if (!OperatingSystem.IsWindows())
            {
                failures.Add(new PlatformNotSupportedException(
                    "A Windows-only development endpoint was owned on a non-Windows process."));
            }
            else
            {
                try
                {
                    await developmentEndpoint_.DisposeAsync();
                }
                catch (Exception error)
                {
                    failures.Add(error);
                }
            }
        }

        if (developmentHost_ is not null)
        {
            try
            {
                await developmentHost_.DisposeAsync();
            }
            catch (Exception error)
            {
                failures.Add(error);
            }
        }
#endif
        try
        {
            await viewportPresentationLifetime_.StopAndDrainAsync();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            shellViewModel_.Dispose();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            _ = await viewportWarmUpTask_;
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            viewportRuntime_.Shutdown();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            editorSelection_.Dispose();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            await projectAssetCatalog_.DisposeAsync();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            await projectSession_.DisposeAsync();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        if (failures.Count != 0)
        {
            throw new AggregateException(
                "Studio composition teardown did not complete cleanly.",
                failures);
        }
    }
}
