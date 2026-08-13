using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Shell.Views;

public sealed class AppSourceTests
{
    [Fact]
    public void App_owns_explicit_async_process_teardown()
    {
        var source = LoadSource("App.axaml.cs");

        Assert.Contains("ShutdownMode.OnExplicitShutdown", source, StringComparison.Ordinal);
        Assert.Contains("StudioProcessSession", source, StringComparison.Ordinal);
        Assert.Contains("ShutdownRequested += OnShutdownRequested", source, StringComparison.Ordinal);
        Assert.Contains("await processSession.StopAsync", source, StringComparison.Ordinal);
        Assert.Contains("LastTeardownReceipt", source, StringComparison.Ordinal);
        Assert.Contains("IStudioDiagnosticHub diagnostics_", source, StringComparison.Ordinal);
        Assert.Contains("new ProjectSession(", source, StringComparison.Ordinal);
        Assert.Contains("new ProjectDescriptorBridge()", source, StringComparison.Ordinal);
        Assert.Contains("new SceneDocumentBridge()", source, StringComparison.Ordinal);
        Assert.Contains("new ProjectDocumentTransitionCoordinator(", source, StringComparison.Ordinal);
        Assert.Contains("new StudioShellViewModel(", source, StringComparison.Ordinal);
        Assert.Contains("documentTransitions", source, StringComparison.Ordinal);
        Assert.Matches(
            @"ProjectAssetCatalog\? projectAssetCatalog = null;[\s\S]*?try\s*\{\s*projectAssetCatalog = new ProjectAssetCatalog\([\s\S]*?shellViewModel = new StudioShellViewModel\([\s\S]*?mainWindow = new MainWindow",
            source);
        Assert.Matches(
            @"catch \(Exception exception\)[\s\S]*?shellViewModel\?\.Dispose\(\);[\s\S]*?if \(projectAssetCatalog is not null\)[\s\S]*?await projectAssetCatalog\.DisposeAsync\(\);[\s\S]*?await projectSession\.DisposeAsync\(\);",
            source);
        Assert.Contains(
            "studio.lifecycle.window-create-cleanup.failed",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "PublishActionRegistrationFailure(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "studio.action.registration.failed",
            source,
            StringComparison.Ordinal);
        Assert.Contains("projectDialogs.Attach(mainWindow)", source, StringComparison.Ordinal);
        Assert.Contains("StudioCompositionSession.CreateAsync", source, StringComparison.Ordinal);
        Assert.Matches(
            @"StudioCompositionSession\.CreateAsync\(\s*shellViewModel,\s*projectSession,\s*mainWindow,\s*diagnostics_,\s*cancellationToken,",
            source);
        Assert.Contains("await Task.Yield()", source, StringComparison.Ordinal);
        Assert.Contains("shellViewModel.MarkReady()", source, StringComparison.Ordinal);
        Assert.Contains("startupTask_ = StartDesktopAsync(desktop)", source, StringComparison.Ordinal);
        Assert.DoesNotContain("await startupTask_", source, StringComparison.Ordinal);
        Assert.Contains("BeginShutdown(exitCode: 1)", source, StringComparison.Ordinal);
        Assert.Matches(
            @"var shellViewModel = mainWindow\?\.DataContext as StudioShellViewModel;[\s\S]*?mainWindow\.DataContext = null;[\s\S]*?shellViewModel\.MarkStopping\(\);[\s\S]*?catch \(Exception exception\)[\s\S]*?studio\.lifecycle\.shell-stop\.failed[\s\S]*?finally[\s\S]*?await processSession\.StopAsync[\s\S]*?BeginFinalShutdown",
            source);
        Assert.DoesNotContain("\n            _ = StartDesktopAsync", source, StringComparison.Ordinal);
        Assert.DoesNotContain("new StudioCompositionRoot", source, StringComparison.Ordinal);
        Assert.Contains("diagnostics_.ProcessIdentity", source, StringComparison.Ordinal);
        Assert.Contains("Logger.Sink = new StudioAvaloniaLogSink(diagnostics_)", source, StringComparison.Ordinal);
        Assert.DoesNotContain(".GetAwaiter().GetResult()", source, StringComparison.Ordinal);
        Assert.DoesNotContain("ViewportNativeLibraryApi", source, StringComparison.Ordinal);
        Assert.DoesNotContain("StudioNativeTeardown", source, StringComparison.Ordinal);
        Assert.DoesNotContain("IStudioNativeTeardown", source, StringComparison.Ordinal);
        Assert.DoesNotContain("OutstandingNativeOperationCount", source, StringComparison.Ordinal);
        Assert.DoesNotContain("ProcessExitFallbackRequired", source, StringComparison.Ordinal);

        var appXaml = LoadSource("App.axaml");
        Assert.DoesNotContain("ViewLocator", appXaml, StringComparison.Ordinal);
        Assert.Contains("Application.DataTemplates", appXaml, StringComparison.Ordinal);
        Assert.Contains("EditorDockSplitNodeViewModel", appXaml, StringComparison.Ordinal);
        Assert.Contains("EditorDockWindowNodeViewModel", appXaml, StringComparison.Ordinal);
        Assert.Contains("StudioHierarchyPanelViewModel", appXaml, StringComparison.Ordinal);
        Assert.Contains("StudioInspectorPanelViewModel", appXaml, StringComparison.Ordinal);
        Assert.Contains("StudioDiagnosticsPanelViewModel", appXaml, StringComparison.Ordinal);
        Assert.Contains("StudioDiagnosticsPanelView", appXaml, StringComparison.Ordinal);
        Assert.Contains("RequestedThemeVariant=\"Dark\"", appXaml, StringComparison.Ordinal);
        Assert.Contains("<FluentTheme DensityStyle=\"Compact\" />", appXaml, StringComparison.Ordinal);
        Assert.Contains("ResourceInclude", appXaml, StringComparison.Ordinal);
        Assert.DoesNotContain("StyleInclude", appXaml, StringComparison.Ordinal);
        Assert.Contains(
            "avares://Editor/UI/Styles/Tokens/DeepDarkColors.axaml",
            appXaml,
            StringComparison.Ordinal);
        Assert.Contains(
            "avares://Editor/UI/Styles/Tokens/EditorMetrics.axaml",
            appXaml,
            StringComparison.Ordinal);
    }

    [Fact]
    public void App_routes_window_and_lifetime_exit_through_one_dirty_document_guard()
    {
        var source = LoadSource("App.axaml.cs");

        Assert.Matches(
            @"OnShutdownRequested[\s\S]*?e\.Cancel = true;[\s\S]*?RequestUserShutdown\(\);",
            source);
        Assert.Matches(
            @"OnMainWindowClosing[\s\S]*?e\.Cancel = true;[\s\S]*?RequestUserShutdown\(\);",
            source);
        Assert.Contains(
            "shutdownTask_ is not null || userExitResolutionTask_ is not null",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "userExitResolutionTask_ = completion.Task",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "_ = ResolveUserShutdownAsync(completion)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "await transitions.PrepareExitAsync()",
            source,
            StringComparison.Ordinal);
        Assert.Matches(
            @"if \(result\.MayProceed\)[\s\S]*?BeginShutdown\(\);",
            source);
        Assert.Contains(
            "userExitResolutionTask_ = null",
            source,
            StringComparison.Ordinal);
    }

    private static string LoadSource(params string[] pathParts)
    {
        var root = FindRepositoryRoot();
        return File.ReadAllText(Path.Combine(new[] { root }.Concat(pathParts).ToArray()));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Asharia.Studio.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Asharia.Studio.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Asharia.Studio.sln.");
    }
}
