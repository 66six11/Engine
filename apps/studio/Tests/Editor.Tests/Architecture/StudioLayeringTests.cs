using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using Xunit;

namespace Editor.Tests.Architecture;

public sealed class StudioLayeringTests
{
    [Fact]
    public void Disconnected_feature_surface_is_deleted()
    {
        var root = FindRepositoryRoot();
        Assert.False(Directory.Exists(Path.Combine(root, "Features")));
        Assert.False(Directory.Exists(Path.Combine(root, "Tests", "Editor.Tests", "Features")));
    }

    [Fact]
    public void Project_presentation_is_limited_to_shell_commands_and_owned_dialogs()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "ViewModels", "Projects")));
        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "Views", "Projects")));
        Assert.True(File.Exists(Path.Combine(
            root,
            "Shell",
            "Services",
            "Projects",
            "MainWindowProjectDialogService.cs")));
        Assert.False(Directory.Exists(Path.Combine(root, "UI", "Presentation")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Abstractions", "IEditorUiDispatcher.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Services", "ImmediateEditorUiDispatcher.cs")));
        Assert.False(File.Exists(
            Path.Combine(root, "Tests", "Editor.Tests", "Core", "ImmediateEditorUiDispatcherTests.cs")));
        Assert.False(Directory.Exists(
            Path.Combine(root, "Tests", "Editor.Tests", "Shell", "ViewModels", "Projects")));
        Assert.False(Directory.Exists(
            Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Views", "Projects")));
    }

    [Fact]
    public void Disconnected_project_open_session_tail_is_deleted()
    {
        var root = FindRepositoryRoot();

        var applicationFiles = new[]
        {
            "ProjectOpenSessionReportParser.cs",
            "ProjectOpenSessionSnapshotSource.cs",
        };
        var publicContractFiles = new[]
        {
            "IProjectOpenSessionSnapshotSource.cs",
            "ProjectOpenNextAction.cs",
            "ProjectOpenSessionDiagnosticSnapshot.cs",
            "ProjectOpenSessionSnapshot.cs",
            "ProjectOpenSessionState.cs",
            "ProjectOpenSummarySnapshot.cs",
        };

        Assert.All(applicationFiles, file => Assert.False(File.Exists(
            Path.Combine(root, "src", "Asharia.Studio.Application", "Projects", file))));
        Assert.All(publicContractFiles, file => Assert.False(File.Exists(
            Path.Combine(root, "src", "Asharia.Editor", "Projects", file))));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Projects",
            "ProjectOpenSessionReportParserTests.cs")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Projects",
            "ProjectOpenSessionSnapshotSourceTests.cs")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Asharia.Editor.Tests",
            "Projects",
            "ProjectOpenSessionContractTests.cs")));
        Assert.False(Directory.Exists(Path.Combine(
            root,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Projects",
            "Fixtures")));
    }

    [Fact]
    public void Active_project_session_is_application_owned_without_legacy_public_facades()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(root, "src", "Asharia.Editor", "Projects")));
        Assert.True(File.Exists(Path.Combine(
            root,
            "src",
            "Asharia.Studio.Application",
            "Projects",
            "ProjectSession.cs")));
        Assert.False(Directory.Exists(Path.Combine(root, "Core", "Interop", "Projects")));
        Assert.False(Directory.Exists(
            Path.Combine(root, "Tests", "Asharia.Editor.Tests", "Projects")));
        Assert.True(File.Exists(Path.Combine(
            root,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Projects",
            "ProjectSessionTests.cs")));
    }

    [Fact]
    public void Managed_project_bridge_is_a_real_test_covered_adapter()
    {
        var root = FindRepositoryRoot();

        Assert.True(Directory.Exists(
            Path.Combine(root, "src", "Asharia.Studio.EngineBridge", "Project")));
        Assert.True(Directory.Exists(
            Path.Combine(root, "Tests", "Asharia.Studio.EngineBridge.Tests", "Project")));
    }

    [Fact]
    public void Disconnected_native_project_bridge_tail_is_deleted()
    {
        var studioRoot = FindRepositoryRoot();
        var repositoryRoot = Path.GetFullPath(Path.Combine(studioRoot, "..", ".."));
        var editorRoot = Path.Combine(repositoryRoot, "apps", "editor");
        var bridgeRoot = Path.Combine(editorRoot, "src", "native_bridge");

        Assert.False(File.Exists(Path.Combine(bridgeRoot, "project_native_api.cpp")));
        Assert.False(File.Exists(Path.Combine(bridgeRoot, "project_native_api.hpp")));
        Assert.False(File.Exists(Path.Combine(bridgeRoot, "project_native_smoke.cpp")));
        Assert.False(File.Exists(Path.Combine(bridgeRoot, "project_native_smoke.hpp")));

        var mainSource = File.ReadAllText(Path.Combine(editorRoot, "src", "main.cpp"));
        var cmakeSource = File.ReadAllText(Path.Combine(editorRoot, "CMakeLists.txt"));
        var review = File.ReadAllText(Path.Combine(repositoryRoot, "docs", "workflow", "review.md"));
        Assert.DoesNotContain("--smoke-editor-project-native", mainSource, StringComparison.Ordinal);
        Assert.DoesNotContain("project_native_", cmakeSource, StringComparison.Ordinal);
        Assert.DoesNotContain("--smoke-editor-project-native", review, StringComparison.Ordinal);

        using var packageDocument = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(editorRoot, "asharia.package.json")));
        var targetDependencies = packageDocument.RootElement.GetProperty("targetDependencies");
        var editorDependencies = targetDependencies.GetProperty("asharia-editor")
            .EnumerateArray()
            .Select(static dependency => dependency.GetString())
            .ToArray();
        var nativeDependencies = targetDependencies.GetProperty("editor-native")
            .EnumerateArray()
            .Select(static dependency => dependency.GetString())
            .ToArray();
        Assert.Contains("asharia-project-core-io", editorDependencies);
        Assert.DoesNotContain("asharia-project-core-io", nativeDependencies);
    }

    [Fact]
    public void Code_first_ui_source_no_longer_exists()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(root, "Core", "CodeFirstUI")));
        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "CodeFirstUI")));
        Assert.False(Directory.Exists(Path.Combine(root, "src", "Asharia.Editor", "UI", "CodeFirst")));
    }

    [Fact]
    public void Dock_visual_resources_are_narrow_and_owned()
    {
        var root = FindRepositoryRoot();
        var appXaml = File.ReadAllText(Path.Combine(root, "App.axaml"));
        var projectSource = File.ReadAllText(Path.Combine(root, "Editor.csproj"));

        Assert.True(Directory.Exists(Path.Combine(root, "UI", "Icons")));
        Assert.True(Directory.Exists(Path.Combine(root, "UI", "Styles", "Tokens")));
        Assert.False(Directory.Exists(Path.Combine(root, "Assets", "Fonts")));
        Assert.False(Directory.Exists(Path.Combine(root, "Tests", "Editor.Tests", "UI")));
        Assert.Contains(
            "avares://Editor/UI/Styles/Tokens/DeepDarkColors.axaml",
            appXaml,
            StringComparison.Ordinal);
        Assert.Contains(
            "avares://Editor/UI/Styles/Tokens/EditorMetrics.axaml",
            appXaml,
            StringComparison.Ordinal);
        Assert.DoesNotContain("Avalonia.Controls.ColorPicker", projectSource, StringComparison.Ordinal);
        Assert.DoesNotContain("CommunityToolkit.Mvvm", projectSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Lucide.Avalonia", projectSource, StringComparison.Ordinal);
    }

    [Fact]
    public void Legacy_workbench_presentation_is_deleted()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "ViewModels", "CommandPalette")));
        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "Views", "CommandPalette")));
        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "ViewModels", "Menus")));
    }

    [Fact]
    public void Disconnected_dialog_shell_surface_is_deleted()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "ViewModels", "Dialogs")));
        Assert.False(Directory.Exists(Path.Combine(root, "Shell", "Views", "Dialogs")));
        Assert.False(Directory.Exists(
            Path.Combine(root, "Tests", "Editor.Tests", "Shell", "ViewModels", "Dialogs")));
        Assert.False(Directory.Exists(
            Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Views", "Dialogs")));
    }

    [Fact]
    public void Dock_shell_is_reintroduced_without_legacy_public_facades()
    {
        var root = FindRepositoryRoot();

        var dockRoots = new[]
        {
            Path.Combine(root, "Shell", "Docking"),
            Path.Combine(root, "Shell", "ViewModels", "Docking"),
            Path.Combine(root, "Shell", "Views", "Docking"),
            Path.Combine(root, "Shell", "ViewModels", "Panels"),
            Path.Combine(root, "Shell", "Views", "Panels"),
        };

        Assert.All(dockRoots, path => Assert.True(Directory.Exists(path), path));
        Assert.False(File.Exists(Path.Combine(root, "ViewLocator.cs")));
        Assert.False(Directory.Exists(Path.Combine(root, "src", "Asharia.Editor")));
        Assert.False(Directory.Exists(Path.Combine(root, "Core", "Models", "Panels")));

        var forbiddenNamespaces = new[]
        {
            "Asharia.Editor",
            "Editor.Core",
            "Asharia.Studio.Application.Panels",
            "Asharia.Studio.Application.Lifecycle",
        };
        var dockSource = dockRoots
            .SelectMany(path => Directory.EnumerateFiles(path, "*.cs", SearchOption.AllDirectories))
            .Select(File.ReadAllText)
            .ToArray();

        Assert.All(
            forbiddenNamespaces,
            forbidden => Assert.DoesNotContain(
                dockSource,
                source => source.Contains(forbidden, StringComparison.Ordinal)));
    }

    [Fact]
    public void Hard_cut_shell_files_live_in_windowing_folders()
    {
        var root = FindRepositoryRoot();

        Assert.False(File.Exists(
            Path.Combine(root, "Shell", "ViewModels", "Windowing", "MainWindowViewModel.cs")));
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Windowing", "StudioShellViewModel.cs")),
            "StudioShellViewModel is the only production Window state owner.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Windowing", "MainWindow.axaml")),
            "MainWindow is the root Avalonia window and should live under Shell/Views/Windowing.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Windowing", "MainWindow.axaml.cs")),
            "MainWindow code-behind should stay next to the root window view.");

        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "MainWindowViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "MainWindow.axaml")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "MainWindow.axaml.cs")));
    }

    [Fact]
    public void Disposable_process_acceptance_owner_remains_test_only()
    {
        var root = FindRepositoryRoot();
        var forbiddenFragments = new[]
        {
            "System.Diagnostics.Process",
            "ProcessStartInfo",
            "WaitForExitAsync(",
            "CloseMainWindow(",
            "Kill(entireProcessTree",
        };
        var productionRoots = new[]
        {
            Path.Combine(root, "Core"),
            Path.Combine(root, "Shell"),
            Path.Combine(root, "src"),
        };
        var productionFiles = productionRoots
            .Where(Directory.Exists)
            .SelectMany(path => Directory.EnumerateFiles(
                path,
                "*.cs",
                SearchOption.AllDirectories))
            .Append(Path.Combine(root, "App.axaml.cs"))
            .Append(Path.Combine(root, "Program.cs"));
        var offenders = productionFiles
            .Where(path => forbiddenFragments.Any(fragment =>
                File.ReadAllText(path).Contains(fragment, StringComparison.Ordinal)))
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
        Assert.True(File.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Shell",
            "Composition",
            "StudioProcessAcceptanceTests.cs")));
    }

    [Fact]
    public void Retired_viewport_closure_stays_deleted_while_the_new_adapter_is_deployed()
    {
        var root = FindRepositoryRoot();
        var projectSource = File.ReadAllText(Path.Combine(root, "Editor.csproj"));
        var programSource = File.ReadAllText(Path.Combine(root, "Program.cs"));
        var appSource = File.ReadAllText(Path.Combine(root, "App.axaml.cs"));
        var scenePanelXaml = File.ReadAllText(Path.Combine(
            root,
            "Shell",
            "Views",
            "Panels",
            "StudioScenePanelView.axaml"));
        var viewportControlSource = File.ReadAllText(Path.Combine(
            root,
            "src",
            "Asharia.Studio.Presentation.Avalonia",
            "Viewports",
            "ViewportCompositionControl.cs"));
        var viewportStreamFenceSource = File.ReadAllText(Path.Combine(
            root,
            "src",
            "Asharia.Studio.Presentation.Avalonia",
            "Viewports",
            "ViewportStreamWorkFence.cs"));
        var transactionCoordinatorSource = File.ReadAllText(Path.Combine(
            root,
            "src",
            "Asharia.Studio.Presentation.Avalonia",
            "Viewports",
            "ViewportPresentationTransactionCoordinator.cs"));

        Assert.False(Directory.Exists(Path.Combine(root, "Core", "Interop", "Viewports")));
        Assert.False(Directory.Exists(Path.Combine(root, "Core", "Models", "Viewports")));
        Assert.False(Directory.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Interop",
            "Viewports")));
        Assert.False(Directory.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Models",
            "Viewports")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Shell",
            "Composition",
            "StudioNativeTeardown.cs")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Build",
            "EditorNativeRuntimeCopyTests.cs")));
        Assert.Contains("editor_native.dll", projectSource, StringComparison.Ordinal);
        Assert.Contains(
            "Asharia.Studio.Presentation.Avalonia",
            projectSource,
            StringComparison.Ordinal);
        Assert.Contains("shaders\\renderer-basic", projectSource, StringComparison.Ordinal);
        Assert.DoesNotContain("slang.dll", projectSource, StringComparison.Ordinal);
        Assert.Contains("asharia_project_native.dll", projectSource, StringComparison.Ordinal);
        Assert.Contains("asharia_scene_native.dll", projectSource, StringComparison.Ordinal);
        Assert.Contains(
            "<presentation:ViewportCompositionControl",
            scenePanelXaml,
            StringComparison.Ordinal);
        Assert.Contains("Session=\"{Binding Session}\"", scenePanelXaml, StringComparison.Ordinal);
        Assert.Contains("IsRealtime=\"{Binding IsRealtime}\"", scenePanelXaml, StringComparison.Ordinal);
        Assert.Contains("Content=\"Realtime\"", scenePanelXaml, StringComparison.Ordinal);
        Assert.Contains("#SceneViewport.IsDegraded", scenePanelXaml, StringComparison.Ordinal);
        Assert.Contains(
            "AutomationProperties.ControlTypeOverride=\"Group\"",
            scenePanelXaml,
            StringComparison.Ordinal);
        Assert.DoesNotContain("DispatcherTimer", viewportControlSource, StringComparison.Ordinal);
        Assert.DoesNotContain(
            "Dispatcher.UIThread.Post(PublishLatestFrame",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain("CompositionSurfacePair", viewportControlSource, StringComparison.Ordinal);
        Assert.DoesNotContain("PromoteStagingSurfaceAsync", viewportControlSource, StringComparison.Ordinal);
        Assert.DoesNotContain("ExactResize", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains(".UpdateWithSemaphoresAsync(", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains("PreparePresentationAsync", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains("TryValidatePreparedPresentation", transactionCoordinatorSource, StringComparison.Ordinal);
        Assert.Contains("ApplyPreparedPresentation", transactionCoordinatorSource, StringComparison.Ordinal);
        Assert.Contains("RequestPresentationBatchRendered", transactionCoordinatorSource, StringComparison.Ordinal);
        Assert.Contains("WaitForReadyFrameAsync", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains("WaitForStreamClosedAsync", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains(
            "stream.WorkFence.BeginRetirement(",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "public Task PumpTask { get; private set; } = Task.CompletedTask;",
            viewportStreamFenceSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain("private Task frameTask_", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains(
            "public bool IsRetiring { get; private set; }",
            viewportStreamFenceSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain("Console.Error.WriteLine(", viewportControlSource, StringComparison.Ordinal);
        Assert.DoesNotContain("await Task.Delay(1);", viewportControlSource, StringComparison.Ordinal);
        Assert.Contains(
            "await Task.Delay(1, cancellationToken).ConfigureAwait(false);",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "await Task.Delay(1).ConfigureAwait(false);",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "!ReferenceEquals(stream.WorkFence.PumpTask, observedPump)",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "stream.WorkFence.IsRetiring || !ReferenceEquals(desiredStream_, stream)",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "ViewportInvalidationReason.Realtime",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Equal(
            2,
            viewportControlSource.Split(
                "RequestCompositionUpdate(",
                StringSplitOptions.None).Length - 1);
        Assert.Contains(
            "RequestCompositionBatchCommitAsync().Processed",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "RequestCompositionBatchCommitAsync().Rendered",
            viewportControlSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain("Win32PlatformOptions", programSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32RenderingMode", programSource, StringComparison.Ordinal);
        Assert.DoesNotContain("StudioNativeTeardown", appSource, StringComparison.Ordinal);
        Assert.DoesNotContain("OutstandingNativeOperationCount", appSource, StringComparison.Ordinal);
    }

    [Fact]
    public void Disconnected_core_frame_debugger_native_closure_is_deleted()
    {
        var root = FindRepositoryRoot();
        var coreRoot = Path.Combine(root, "Core");
        var forbiddenFragments = new[]
        {
            "Editor.Core.Interop.FrameDebugger",
            "Editor.Core.Models.FrameDebug",
            "INativeFrameDebuggerBridge",
            "FrameDebuggerNative",
            "editor_frame_debugger_",
        };

        var offenders = Directory.Exists(coreRoot)
            ? Directory.EnumerateFiles(coreRoot, "*.cs", SearchOption.AllDirectories)
                .Where(path => forbiddenFragments.Any(fragment =>
                    File.ReadAllText(path).Contains(fragment, StringComparison.Ordinal)))
                .Select(path => Path.GetRelativePath(root, path))
                .Order(StringComparer.Ordinal)
                .ToArray()
            : Array.Empty<string>();

        Assert.Empty(offenders);
        Assert.False(Directory.Exists(Path.Combine(coreRoot, "Interop", "FrameDebugger")));
        Assert.False(Directory.Exists(Path.Combine(coreRoot, "Models", "FrameDebug")));
        Assert.False(File.Exists(Path.Combine(
            coreRoot,
            "Abstractions",
            "INativeFrameDebuggerBridge.cs")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "FrameDebuggerSnapshotProviderTests.cs")));
        Assert.False(Directory.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Interop",
            "FrameDebugger")));
    }

    [Fact]
    public void Disconnected_public_diagnostics_surface_is_deleted()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(
            root,
            "src",
            "Asharia.Editor",
            "Diagnostics")));
    }

    [Fact]
    public void Legacy_workbench_action_models_are_deleted()
    {
        var root = FindRepositoryRoot();
        Assert.False(Directory.Exists(Path.Combine(root, "Core", "Models", "Workbench")));
    }

    [Fact]
    public void Disconnected_panel_runtime_contracts_are_deleted()
    {
        var root = FindRepositoryRoot();
        var retiredPanelFiles = new[]
        {
            "EditorPanelFrameContext.cs",
            "EditorPanelFrameUpdateMode.cs",
            "EditorPanelFrameUpdateRequest.cs",
            "EditorPanelLifecycleContext.cs",
            "EditorPanelLayoutContext.cs",
            "IEditorPanelFrameUpdateSink.cs",
            "IEditorPanelLayoutSink.cs",
            "IEditorPanelLifecycleSink.cs",
            "IEditorPanelVisibilitySink.cs",
        };

        foreach (var fileName in retiredPanelFiles)
        {
            var path = Path.Combine(root, "src", "Asharia.Editor", "Panels", fileName);
            Assert.False(File.Exists(path), $"Disconnected panel runtime contract remains: {fileName}.");
        }

        Assert.False(Directory.Exists(Path.Combine(root, "src", "Asharia.Editor", "Panels")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Models", "Panels", "Dock" + "Area.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Models", "Panels", "EditorPanelFrameContext.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Models", "Panels", "EditorPanelFrameUpdateMode.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Models", "Panels", "EditorPanelFrameUpdateRequest.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Core", "Models", "Panels", "EditorPanelLifecycleContext.cs")));
    }

    [Fact]
    public void Legacy_contribution_models_are_deleted()
    {
        var root = FindRepositoryRoot();
        Assert.False(Directory.Exists(Path.Combine(root, "Core", "Models", "Contributions")));
    }

    [Fact]
    public void Disconnected_core_extension_identity_is_deleted()
    {
        var root = FindRepositoryRoot();

        Assert.False(Directory.Exists(Path.Combine(
            root,
            "Core",
            "Models",
            "Extensions")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Core",
            "Models",
            "EditorExtensionId.cs")));
    }

    [Fact]
    public void Dock_callback_exception_batch_is_shell_local()
    {
        var root = FindRepositoryRoot();
        var productionRoots = new[]
        {
            Path.Combine(root, "Core"),
            Path.Combine(root, "src"),
        };
        var offenders = productionRoots
            .Where(Directory.Exists)
            .SelectMany(path => Directory.EnumerateFiles(path, "*.cs", SearchOption.AllDirectories))
            .Where(path => File.ReadAllText(path).Contains(
                "CallbackExceptionBatch",
                StringComparison.Ordinal))
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.True(File.Exists(Path.Combine(
            root,
            "Shell",
            "Lifecycle",
            "CallbackExceptionBatch.cs")));
        Assert.Empty(offenders);
    }

    [Fact]
    public void Disconnected_core_scene_provider_declarations_are_deleted()
    {
        var root = FindRepositoryRoot();
        var forbiddenFragments = new[]
        {
            "EditorProviderRoles",
            "SceneProviderDescriptor",
        };
        var productionRoots = new[]
        {
            Path.Combine(root, "Core"),
            Path.Combine(root, "Shell"),
            Path.Combine(root, "src"),
        };
        var offenders = productionRoots
            .Where(Directory.Exists)
            .SelectMany(path => Directory.EnumerateFiles(path, "*.cs", SearchOption.AllDirectories))
            .Where(path => forbiddenFragments.Any(fragment =>
                File.ReadAllText(path).Contains(fragment, StringComparison.Ordinal)))
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.False(File.Exists(Path.Combine(
            root,
            "Core",
            "Models",
            "Scene",
            "EditorProviderRoles.cs")));
        Assert.False(File.Exists(Path.Combine(
            root,
            "Core",
            "Models",
            "Scene",
            "SceneProviderDescriptor.cs")));
        Assert.Empty(offenders);
    }

    [Fact]
    public void Shared_dock_and_presentation_sources_are_platform_neutral()
    {
        var root = FindRepositoryRoot();
        var sharedRoots = new[]
        {
            Path.Combine(root, "Shell", "Views", "Docking"),
            Path.Combine(root, "src", "Asharia.Studio.Presentation.Avalonia"),
        };
        var explicitWindowsRoot = Path.Combine(
            root,
            "src",
            "Asharia.Studio.Presentation.Avalonia.Windows");
        var forbiddenTokens = new[]
        {
            "Win32",
            "HWND",
            "USER32",
            "user32.dll",
            "Win32Properties",
            "OperatingSystem.IsWindows",
            "System.Runtime.InteropServices",
            "LibraryImport",
            "DllImport",
            "Microsoft.Win32",
        };

        Assert.All(sharedRoots, path => Assert.True(
            Directory.Exists(path),
            $"Shared presentation source root is missing: {path}"));
        Assert.True(
            Directory.Exists(explicitWindowsRoot),
            $"The explicit Windows presentation adapter root is missing: {explicitWindowsRoot}");

        var offenders = sharedRoots
            .SelectMany(path => Directory.EnumerateFiles(path, "*.cs", SearchOption.AllDirectories))
            .SelectMany(path =>
            {
                var source = File.ReadAllText(path);
                return forbiddenTokens
                    .Where(token =>
                        Path.GetFileName(path).Contains(token, StringComparison.OrdinalIgnoreCase) ||
                        source.Contains(token, StringComparison.OrdinalIgnoreCase))
                    .Select(token => $"{Path.GetRelativePath(root, path)}: {token}");
            })
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
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

        directory = new DirectoryInfo(AppContext.BaseDirectory);
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
