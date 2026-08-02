using System;
using System.Collections.Immutable;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Logging;
using Editor.Shell.Diagnostics;
using Editor.Shell.Composition;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Windowing;

namespace Editor;

// ReSharper disable once PartialTypeWithSinglePart
public partial class App : Application
{
    private readonly IStudioDiagnosticHub diagnostics_;
    private readonly bool enableReadOnlyDevelopmentObservation_;
    private StudioProcessSession? processSession_;
    private MainWindow? mainWindow_;
    private Task? startupTask_;
    private Task? shutdownTask_;
    private int requestedExitCode_;
    private bool finalShutdown_;

    internal StudioTeardownReceipt? LastTeardownReceipt { get; private set; }

    public App()
        : this(
            new StudioDiagnosticHub(),
            StudioDevelopmentStartup.IsReadOnlyObservationGranted(
                Environment.GetCommandLineArgs()))
    {
    }

    internal App(IStudioDiagnosticHub diagnostics)
        : this(diagnostics, enableReadOnlyDevelopmentObservation: false)
    {
    }

    internal App(
        IStudioDiagnosticHub diagnostics,
        bool enableReadOnlyDevelopmentObservation)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        diagnostics_ = diagnostics;
        enableReadOnlyDevelopmentObservation_ =
            enableReadOnlyDevelopmentObservation;
        Logger.Sink = new StudioAvaloniaLogSink(diagnostics_);
    }

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.ShutdownMode = ShutdownMode.OnExplicitShutdown;
            desktop.ShutdownRequested += OnShutdownRequested;
            startupTask_ = StartDesktopAsync(desktop);
        }

        base.OnFrameworkInitializationCompleted();
    }

    private async Task StartDesktopAsync(
        IClassicDesktopStyleApplicationLifetime desktop)
    {
        var shellViewModel = new StudioShellViewModel();
        MainWindow mainWindow;
        try
        {
            mainWindow = new MainWindow
            {
                DataContext = shellViewModel,
            };
        }
        catch (Exception exception)
        {
            shellViewModel.Dispose();
            PublishFailure(
                "studio.lifecycle.window-create.failed",
                "lifecycle",
                "Studio shell window failed to initialize.",
                exception);
            BeginFinalShutdown(desktop, exitCode: 1);
            return;
        }

        var processSession = new StudioProcessSession(
            cancellationToken =>
            {
                return StudioCompositionSession.CreateAsync(
                    shellViewModel,
                    mainWindow,
                    diagnostics_,
                    cancellationToken,
                    enableReadOnlyDevelopmentObservation_);
            },
            diagnostics_.ProcessIdentity);
        processSession_ = processSession;
        mainWindow.Closing += OnMainWindowClosing;
        mainWindow_ = mainWindow;
        desktop.MainWindow = mainWindow;

        try
        {
            var start = processSession.StartAsync();
            await Task.Yield();
            await start;
            if (shutdownTask_ is not null)
            {
                return;
            }

            shellViewModel.MarkReady();
            PublishManagedLog(
                StudioLogLevel.Information,
                "lifecycle",
                "Studio process session entered Running.");
        }
        catch (OperationCanceledException) when (shutdownTask_ is not null)
        {
        }
        catch (Exception exception)
        {
            PublishFailure(
                "studio.lifecycle.start.failed",
                "lifecycle",
                "Studio process session failed to start.",
                exception);
            BeginShutdown(exitCode: 1);
        }
    }

    private void OnShutdownRequested(object? sender, ShutdownRequestedEventArgs e)
    {
        if (finalShutdown_)
        {
            return;
        }

        e.Cancel = true;
        BeginShutdown();
    }

    private void OnMainWindowClosing(object? sender, WindowClosingEventArgs e)
    {
        if (finalShutdown_)
        {
            return;
        }

        e.Cancel = true;
        BeginShutdown();
    }

    private void BeginShutdown(int exitCode = 0)
    {
        requestedExitCode_ = Math.Max(requestedExitCode_, exitCode);
        shutdownTask_ ??= StopAndShutdownAsync();
    }

    private async Task StopAndShutdownAsync()
    {
        if (ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop)
        {
            return;
        }

        var mainWindow = mainWindow_;
        if (mainWindow is not null)
        {
            if (mainWindow.DataContext is StudioShellViewModel shellViewModel)
            {
                shellViewModel.MarkStopping();
            }

            mainWindow.DataContext = null;
        }

        var exitCode = requestedExitCode_;
        try
        {
            var processSession = processSession_;
            if (processSession is not null)
            {
                LastTeardownReceipt = await processSession.StopAsync(
                    TimeSpan.FromSeconds(5));
                if (LastTeardownReceipt.Status != StudioProcessStopStatus.Completed)
                {
                    exitCode = 1;
                    diagnostics_.PublishDiagnostic(new StudioDiagnosticWrite(
                        StudioDiagnosticSeverity.Error,
                        StudioDiagnosticChannel.Problem,
                        LastTeardownReceipt.Status == StudioProcessStopStatus.TimedOut
                            ? "studio.lifecycle.stop.timed-out"
                            : "studio.lifecycle.stop.failed",
                        "lifecycle",
                        CreateManagedContext("process-session"),
                        "Studio process teardown did not complete cleanly.",
                        "Exit the process and inspect the teardown receipt before restarting.",
                        [
                            new StudioDiagnosticAttribute(
                                "status",
                                LastTeardownReceipt.Status.ToString()),
                            new StudioDiagnosticAttribute(
                                "compositionStatus",
                                LastTeardownReceipt.CompositionStatus.ToString()),
                        ]));
                }
                else
                {
                    PublishManagedLog(
                        StudioLogLevel.Information,
                        "lifecycle",
                        "Studio process teardown completed cleanly.");
                }
            }
        }
        catch (Exception exception)
        {
            exitCode = 1;
            PublishFailure(
                "studio.lifecycle.stop.unhandled",
                "lifecycle",
                "Studio process teardown raised an unhandled failure.",
                exception);
        }

        BeginFinalShutdown(
            desktop,
            Math.Max(exitCode, requestedExitCode_));
    }

    private void BeginFinalShutdown(
        IClassicDesktopStyleApplicationLifetime desktop,
        int exitCode)
    {
        if (finalShutdown_)
        {
            return;
        }

        finalShutdown_ = true;
        desktop.ShutdownRequested -= OnShutdownRequested;
        if (mainWindow_ is not null)
        {
            mainWindow_.Closing -= OnMainWindowClosing;
        }

        desktop.Shutdown(exitCode);
    }

    private StudioDiagnosticContext CreateManagedContext(string component) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.studio",
            component,
            StudioDiagnosticScope.Process(diagnostics_.ProcessIdentity));

    private void PublishManagedLog(
        StudioLogLevel level,
        string channel,
        string message)
    {
        diagnostics_.PublishLog(new StudioLogWrite(
            level,
            channel,
            CreateManagedContext("process-session"),
            message,
            message));
    }

    private void PublishFailure(
        string code,
        string category,
        string message,
        Exception exception)
    {
        diagnostics_.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Error,
            StudioDiagnosticChannel.Problem,
            code,
            category,
            CreateManagedContext("process-session"),
            message,
            Attributes: ImmutableArray.Create(
                new StudioDiagnosticAttribute(
                    "exceptionType",
                    exception.GetType().FullName ?? exception.GetType().Name))));
    }
}
