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
        Assert.Contains("new ProjectSession(new ProjectDescriptorBridge())", source, StringComparison.Ordinal);
        Assert.Contains("new StudioShellViewModel(projectSession, projectDialogs)", source, StringComparison.Ordinal);
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
        Assert.DoesNotContain("Application.DataTemplates", appXaml, StringComparison.Ordinal);
        Assert.Contains("RequestedThemeVariant=\"Dark\"", appXaml, StringComparison.Ordinal);
        Assert.Contains("<FluentTheme DensityStyle=\"Compact\" />", appXaml, StringComparison.Ordinal);
        Assert.DoesNotContain("ResourceInclude", appXaml, StringComparison.Ordinal);
        Assert.DoesNotContain("StyleInclude", appXaml, StringComparison.Ordinal);
        Assert.DoesNotContain("avares://Editor/UI", appXaml, StringComparison.Ordinal);
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
