using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Scene;
using Asharia.Studio.EngineBridge.Viewports;
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using Xunit;

namespace Asharia.Studio.Architecture.Tests;

public sealed class ProjectReferenceGraphTests
{
    [Fact]
    public void Scene_world_bridge_is_owned_only_by_engine_bridge()
    {
        var studioRoot = FindStudioRoot();
        var bridgeRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.EngineBridge");
        var bridgeProjectPath = Path.Combine(
            bridgeRoot,
            "Asharia.Studio.EngineBridge.csproj");

        Assert.True(
            File.Exists(bridgeProjectPath),
            $"Engine Bridge project is missing from {bridgeRoot}.");
        Assert.Equal(
            "Asharia.Studio.EngineBridge",
            typeof(SceneWorld).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Studio.EngineBridge",
            typeof(SceneNativeCallException).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Studio.EngineBridge",
            typeof(SceneNativeStatus).Assembly.GetName().Name);

        var bridgeProject = XDocument.Load(bridgeProjectPath);
        var bridgeReferences = bridgeProject
            .Descendants("ProjectReference")
            .Select(reference => reference.Attribute("Include")?.Value.Replace('\\', '/'))
            .OfType<string>()
            .ToArray();
        Assert.Equal(
            [
                "../Asharia.Runtime.Contracts/Asharia.Runtime.Contracts.csproj",
                "../Asharia.Studio.Application/Asharia.Studio.Application.csproj",
            ],
            bridgeReferences);
        Assert.Equal(
            "true",
            RequiredProperty(bridgeProject, "DisableRuntimeMarshalling"));

        var publicMethods = typeof(SceneWorld).GetMethods(
            BindingFlags.Public
            | BindingFlags.Instance
            | BindingFlags.Static
            | BindingFlags.DeclaredOnly);
        Assert.DoesNotContain(
            publicMethods,
            method => method.ReturnType == typeof(IntPtr)
                || method.GetParameters().Any(
                    parameter => parameter.ParameterType == typeof(IntPtr)));

        var applicationProject = XDocument.Load(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Asharia.Studio.Application.csproj"));
        Assert.DoesNotContain(
            applicationProject.Descendants("ProjectReference"),
            reference => reference.Attribute("Include")?.Value.Contains(
                "EngineBridge",
                StringComparison.Ordinal) == true);

        var bridgeSources = Directory
            .EnumerateFiles(bridgeRoot, "*.cs", SearchOption.AllDirectories)
            .Where(path => !path.Contains(
                $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                StringComparison.Ordinal))
            .Select(File.ReadAllText)
            .ToArray();
        var forbiddenTokens = new[]
        {
            "Avalonia",
            "Editor.Shell",
            "Editor.Features",
            "Dispatcher",
            "Marshal.Alloc",
            "NativeMemory",
            "SafeHandle",
            "StringBuilder",
            "~SceneWorld",
        };

        Assert.DoesNotContain(
            forbiddenTokens,
            token => bridgeSources.Any(source => source.Contains(
                token,
                StringComparison.Ordinal)));

        var importSource = File.ReadAllText(Path.Combine(
            bridgeRoot,
            "Scene",
            "Abi",
            "SceneNativeLibraryApi.cs"));
        Assert.Contains(
            "LibraryName = \"asharia_scene_native\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_create\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_destroy\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_create_entity\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_destroy_entity\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_is_alive\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_get_local_transform\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_set_local_transform\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_get_entity_name\"",
            importSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "EntryPoint = \"asharia_scene_world_set_entity_name\"",
            importSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "WorldTransform",
            string.Join(Environment.NewLine, bridgeSources),
            StringComparison.Ordinal);
        Assert.Equal(
            9,
            Regex.Matches(importSource, @"\[LibraryImport\(").Count);
        Assert.Equal(
            9,
            Regex.Matches(importSource, @"CallConvCdecl").Count);
    }

    [Fact]
    public void Disconnected_scene_provider_snapshot_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Worlds", "Snapshots");
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Providers");
        var inMemoryProviderPath = Path.Combine(
            studioRoot,
            "Core",
            "Services",
            "InMemorySceneSnapshotProvider.cs");
        var publicTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Editor.Tests",
            "Worlds",
            "Snapshots");
        var applicationTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Providers");
        var inMemoryProviderTestPath = Path.Combine(
            studioRoot,
            "Tests",
            "Editor.Tests",
            "Core",
            "SceneSnapshotProviderTests.cs");

        Assert.False(Directory.Exists(publicRoot), $"Public scene snapshot source remains at {publicRoot}.");
        Assert.False(
            Directory.Exists(applicationRoot),
            $"Application provider host source remains at {applicationRoot}.");
        Assert.False(File.Exists(inMemoryProviderPath), $"In-memory provider remains at {inMemoryProviderPath}.");
        Assert.False(Directory.Exists(publicTestRoot), $"Public scene snapshot tests remain at {publicTestRoot}.");
        Assert.False(
            Directory.Exists(applicationTestRoot),
            $"Application provider host tests remain at {applicationTestRoot}.");
        Assert.False(
            File.Exists(inMemoryProviderTestPath),
            $"In-memory provider tests remain at {inMemoryProviderTestPath}.");

        var applicationProviderTypes = typeof(StudioDiagnosticHub).Assembly
            .GetExportedTypes()
            .Where(type => type.Namespace is not null
                && type.Namespace.StartsWith("Asharia.Studio.Application.Providers", StringComparison.Ordinal))
            .ToArray();
        Assert.Empty(applicationProviderTypes);
    }

    [Fact]
    public void Legacy_workbench_commands_stay_deleted_while_shell_owns_one_async_command()
    {
        var studioRoot = FindStudioRoot();
        Assert.True(File.Exists(Path.Combine(
            studioRoot,
            "Shell",
            "Commands",
            "AsyncCommand.cs")));
        Assert.False(Directory.Exists(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Editor",
            "Commands")));
        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Commands",
            "EditorCommandStatusMessageRouter.cs")));
    }

    [Fact]
    public void Disconnected_panel_runtime_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Panels");
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Panels");
        var publicSinkTestPath = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Editor.Tests",
            "Panels",
            "EditorPanelSinkContractTests.cs");
        var applicationTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Panels");
        var retiredPublicFiles = new[]
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

        Assert.All(
            retiredPublicFiles,
            fileName => Assert.False(
                File.Exists(Path.Combine(publicRoot, fileName)),
                $"Retired public panel runtime source remains: {fileName}."));
        Assert.False(Directory.Exists(applicationRoot), $"Application panel runtime remains at {applicationRoot}.");
        Assert.False(File.Exists(publicSinkTestPath), $"Panel sink self-test remains at {publicSinkTestPath}.");
        Assert.False(
            Directory.Exists(applicationTestRoot),
            $"Application panel scheduler tests remain at {applicationTestRoot}.");

        var applicationPanelTypes = typeof(StudioDiagnosticHub).Assembly
            .GetExportedTypes()
            .Where(type => type.Namespace?.StartsWith(
                "Asharia.Studio.Application.Panels",
                StringComparison.Ordinal) == true)
            .ToArray();
        Assert.Empty(applicationPanelTypes);

        var applicationProject = XDocument.Load(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Asharia.Studio.Application.csproj"));
        var applicationTestProject = XDocument.Load(Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Asharia.Studio.Application.Tests.csproj"));
        Assert.DoesNotContain(
            applicationProject.Descendants("ProjectReference"),
            reference => reference.Attribute("Include")?.Value.Contains(
                "Asharia.Editor",
                StringComparison.Ordinal) == true);
        Assert.DoesNotContain(
            applicationTestProject.Descendants("ProjectReference"),
            reference => reference.Attribute("Include")?.Value.Contains(
                "Asharia.Editor",
                StringComparison.Ordinal) == true);
    }

    [Fact]
    public void Viewport_foundation_is_application_owned_without_the_legacy_public_surface()
    {
        var studioRoot = FindStudioRoot();
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Viewports");
        var applicationRoot = Path.Combine(studioRoot, "src", "Asharia.Studio.Application", "Viewports");
        var publicTestRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests", "Viewports");
        var bridgeRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.EngineBridge",
            "Viewports");

        Assert.False(Directory.Exists(publicRoot), $"Public viewport source remains at {publicRoot}.");
        Assert.False(Directory.Exists(publicTestRoot), $"Public viewport tests remain at {publicTestRoot}.");
        Assert.True(Directory.Exists(applicationRoot));
        Assert.True(Directory.Exists(bridgeRoot));
        Assert.Equal("Asharia.Studio.Application", typeof(ViewportSession).Assembly.GetName().Name);
        Assert.Equal("Asharia.Studio.EngineBridge", typeof(ViewportBridge).Assembly.GetName().Name);
        Assert.DoesNotContain(
            Directory.EnumerateFiles(applicationRoot, "*.cs", SearchOption.AllDirectories),
            path => Path.GetFileName(path).Contains("Scheduler", StringComparison.Ordinal));
        Assert.All(
            Directory.EnumerateFiles(applicationRoot, "*.cs", SearchOption.AllDirectories),
            path =>
            {
                var source = File.ReadAllText(path);
                Assert.DoesNotContain("Avalonia", source, StringComparison.Ordinal);
                Assert.DoesNotContain("IntPtr", source, StringComparison.Ordinal);
                Assert.DoesNotContain("DllImport", source, StringComparison.Ordinal);
                Assert.DoesNotContain("LibraryImport", source, StringComparison.Ordinal);
            });
    }

    [Fact]
    public void Diagnostic_hub_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Diagnostics");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "StudioDiagnosticHub.cs")),
            $"Application diagnostic hub is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(StudioDiagnosticHub).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IStudioDiagnosticHub),
            typeof(StudioDiagnosticHub).GetInterfaces());

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class StudioDiagnosticHub", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void App_is_the_only_production_diagnostic_hub_creator()
    {
        var studioRoot = FindStudioRoot();
        var creators = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => new
            {
                Path = Path.GetRelativePath(studioRoot, path).Replace('\\', '/'),
                Text = File.ReadAllText(path),
            })
            .Where(file => !file.Path.StartsWith("Tests/", StringComparison.Ordinal)
                && !file.Path.Contains("/bin/", StringComparison.Ordinal)
                && !file.Path.Contains("/obj/", StringComparison.Ordinal)
                && file.Text.Contains("new StudioDiagnosticHub(", StringComparison.Ordinal))
            .Select(file => file.Path)
            .ToArray();

        Assert.Equal(["App.axaml.cs"], creators);

        var programSource = File.ReadAllText(Path.Combine(studioRoot, "Program.cs"));
        Assert.DoesNotContain("LogToTrace", programSource, StringComparison.Ordinal);
        var hubSource = File.ReadAllText(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Diagnostics",
            "StudioDiagnosticHub.cs"));
        Assert.DoesNotContain("List<", hubSource, StringComparison.Ordinal);
        Assert.DoesNotContain("RemoveAt(0)", hubSource, StringComparison.Ordinal);
        Assert.Contains("BoundedConcurrentRing", hubSource, StringComparison.Ordinal);

        var avaloniaSinkSource = File.ReadAllText(Path.Combine(
            studioRoot,
            "Shell",
            "Diagnostics",
            "StudioAvaloniaLogSink.cs"));
        Assert.Contains("NormalizeValues", avaloniaSinkSource, StringComparison.Ordinal);
        Assert.Contains("_ => TypeMarker(value)", avaloniaSinkSource, StringComparison.Ordinal);
        Assert.DoesNotContain("SafeValue", avaloniaSinkSource, StringComparison.Ordinal);
        Assert.DoesNotContain("IFormattable", avaloniaSinkSource, StringComparison.Ordinal);
    }

    [Fact]
    public void App_composes_the_scene_document_shell_and_headless_has_an_isolated_dispatcher()
    {
        var studioRoot = FindStudioRoot();
        var appSource = File.ReadAllText(Path.Combine(studioRoot, "App.axaml.cs"));
        Assert.Contains(
            "new StudioShellViewModel(projectSession, projectDialogs)",
            appSource,
            StringComparison.Ordinal);
        Assert.Contains("new ProjectSession(", appSource);
        Assert.Contains("new ProjectDescriptorBridge()", appSource);
        Assert.Contains("new SceneDocumentBridge()", appSource);
        Assert.Contains("StudioCompositionSession.CreateAsync", appSource, StringComparison.Ordinal);
        Assert.Matches(
            @"StudioCompositionSession\.CreateAsync\(\s*shellViewModel,\s*projectSession,\s*mainWindow,\s*diagnostics_,\s*cancellationToken,",
            appSource);
        Assert.DoesNotContain("new StudioCompositionRoot", appSource, StringComparison.Ordinal);
        Assert.DoesNotContain("MainWindowViewModel", appSource, StringComparison.Ordinal);

        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "Shell",
            "Composition",
            "StudioCompositionRoot.cs")));
        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "Shell",
            "Composition",
            "EditorExtensionComposition.cs")));
        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "Shell",
            "ViewModels",
            "Windowing",
            "MainWindowViewModel.cs")));

        var shellXaml = File.ReadAllText(Path.Combine(
            studioRoot,
            "Shell",
            "Views",
            "Windowing",
            "MainWindow.axaml"));
        Assert.Contains("x:DataType=\"vm:StudioShellViewModel\"", shellXaml, StringComparison.Ordinal);
        Assert.Contains("StudioShellStartingState", shellXaml, StringComparison.Ordinal);
        Assert.Contains("StudioShellNoProjectState", shellXaml, StringComparison.Ordinal);
        Assert.Contains("StudioShellNoDocumentState", shellXaml, StringComparison.Ordinal);
        Assert.Contains("EditorDockWorkspaceView", shellXaml, StringComparison.Ordinal);
        Assert.Contains("DataContext=\"{Binding DockWorkspace}\"", shellXaml, StringComparison.Ordinal);

        var hierarchyXaml = File.ReadAllText(Path.Combine(
            studioRoot,
            "Shell",
            "Views",
            "Panels",
            "StudioHierarchyPanelView.axaml"));
        var inspectorXaml = File.ReadAllText(Path.Combine(
            studioRoot,
            "Shell",
            "Views",
            "Panels",
            "StudioInspectorPanelView.axaml"));
        Assert.Contains("StudioHierarchyPanel", hierarchyXaml, StringComparison.Ordinal);
        Assert.Contains("StudioInspectorPanel", inspectorXaml, StringComparison.Ordinal);
        Assert.Contains("Shell.SceneEntities", hierarchyXaml, StringComparison.Ordinal);
        Assert.Contains("Shell.ApplyEntityTransformCommand", inspectorXaml, StringComparison.Ordinal);

        var headlessProject = XDocument.Load(Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Headless.Tests",
            "Asharia.Studio.Headless.Tests.csproj"));
        var packages = headlessProject
            .Descendants("PackageReference")
            .Select(element => element.Attribute("Include")?.Value)
            .OfType<string>()
            .ToArray();
        Assert.Contains("Avalonia.Headless.XUnit", packages);
        Assert.Contains("xunit.v3", packages);
        Assert.Contains(
            headlessProject.Descendants("ProjectReference"),
            reference => reference.Attribute("Include")?.Value.Replace('\\', '/').EndsWith(
                    "src/Asharia.Studio.Application/Asharia.Studio.Application.csproj",
                    StringComparison.Ordinal) == true
                && reference.Attribute("Condition") is null);
        Assert.DoesNotContain("Avalonia.Headless.XUnit", XDocument.Load(Path.Combine(
            studioRoot,
            "Tests",
            "Editor.Tests",
            "Editor.Tests.csproj"))
            .Descendants("PackageReference")
            .Select(element => element.Attribute("Include")?.Value));
    }

    [Fact]
    public void Disconnected_editor_transaction_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var publicEditingRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Editing");
        var publicTransactionRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Transactions");
        var applicationRoot = Path.Combine(studioRoot, "src", "Asharia.Studio.Application", "Transactions");
        var publicEditingTestRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests", "Editing");
        var publicTransactionTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Editor.Tests",
            "Transactions");
        var applicationTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Transactions");

        Assert.False(Directory.Exists(publicEditingRoot), $"Public editing source remains at {publicEditingRoot}.");
        Assert.False(
            Directory.Exists(publicTransactionRoot),
            $"Public transaction source remains at {publicTransactionRoot}.");
        Assert.False(Directory.Exists(applicationRoot), $"Application transaction source remains at {applicationRoot}.");
        Assert.False(
            Directory.Exists(publicEditingTestRoot),
            $"Public editing tests remain at {publicEditingTestRoot}.");
        Assert.False(
            Directory.Exists(publicTransactionTestRoot),
            $"Public transaction tests remain at {publicTransactionTestRoot}.");
        Assert.False(
            Directory.Exists(applicationTestRoot),
            $"Application transaction tests remain at {applicationTestRoot}.");

    }

    [Fact]
    public void Disconnected_lifecycle_event_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Lifecycle");
        var applicationRoot = Path.Combine(studioRoot, "src", "Asharia.Studio.Application", "Lifecycle");
        var publicTestRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests", "Lifecycle");
        var applicationTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Lifecycle");

        Assert.False(Directory.Exists(publicRoot), $"Public lifecycle source remains at {publicRoot}.");
        Assert.False(Directory.Exists(applicationRoot), $"Application lifecycle source remains at {applicationRoot}.");
        Assert.False(Directory.Exists(publicTestRoot), $"Public lifecycle tests remain at {publicTestRoot}.");
        Assert.False(
            Directory.Exists(applicationTestRoot),
            $"Application lifecycle tests remain at {applicationTestRoot}.");

    }

    [Fact]
    public void Disconnected_background_task_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Tasks");
        var applicationRoot = Path.Combine(studioRoot, "src", "Asharia.Studio.Application", "Tasks");
        var publicTestRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests", "Tasks");
        var applicationTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Tasks");

        Assert.False(Directory.Exists(publicRoot), $"Public task source remains at {publicRoot}.");
        Assert.False(Directory.Exists(applicationRoot), $"Application task source remains at {applicationRoot}.");
        Assert.False(Directory.Exists(publicTestRoot), $"Public task tests remain at {publicTestRoot}.");
        Assert.False(
            Directory.Exists(applicationTestRoot),
            $"Application task tests remain at {applicationTestRoot}.");

    }

    [Fact]
    public void Disconnected_selection_surface_and_synthetic_distribution_anchor_are_deleted()
    {
        var studioRoot = FindStudioRoot();
        var repositoryRoot = Path.GetFullPath(Path.Combine(studioRoot, "..", ".."));
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Selection");
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Selection");
        var publicTestRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests", "Selection");
        var applicationTestRoot = Path.Combine(
            studioRoot,
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Selection");

        Assert.False(Directory.Exists(publicRoot), $"Public Selection source remains at {publicRoot}.");
        Assert.False(
            Directory.Exists(applicationRoot),
            $"Application Selection source remains at {applicationRoot}.");
        Assert.False(Directory.Exists(publicTestRoot), $"Public Selection tests remain at {publicTestRoot}.");
        Assert.False(
            Directory.Exists(applicationTestRoot),
            $"Application Selection tests remain at {applicationTestRoot}.");

        var applicationSelectionTypes = typeof(StudioDiagnosticHub).Assembly
            .GetExportedTypes()
            .Where(type => type.Namespace is not null
                && type.Namespace.StartsWith("Asharia.Studio.Application.Selection", StringComparison.Ordinal))
            .ToArray();
        Assert.Empty(applicationSelectionTypes);

        var distributionTestRoot = Path.Combine(repositoryRoot, "tools", "studio-distribution.Tests");
        var distributionTestProject = XDocument.Load(Path.Combine(
            distributionTestRoot,
            "Asharia.Studio.Distribution.Tests.csproj"));
        Assert.DoesNotContain(
            distributionTestProject.Descendants("ProjectReference"),
            reference => reference.Attribute("Include")?.Value.Replace('\\', '/')
                .EndsWith("apps/studio/src/Asharia.Editor/Asharia.Editor.csproj", StringComparison.Ordinal)
                == true);

        var fixtureSource = File.ReadAllText(Path.Combine(
            distributionTestRoot,
            "StudioEditorImageTestInputs.cs"));
        var producerTestSource = File.ReadAllText(Path.Combine(
            distributionTestRoot,
            "StudioEditorImageProducerTests.cs"));
        Assert.DoesNotContain("IEditorSelectionService", fixtureSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Asharia.Editor.Selection", fixtureSource, StringComparison.Ordinal);
        Assert.DoesNotContain("IEditorSelectionService", producerTestSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Asharia.Editor.Selection", producerTestSource, StringComparison.Ordinal);
    }

    [Fact]
    public void Disconnected_public_dialog_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var legacyRoot = Path.Combine(studioRoot, "Core", "Models", "Dialogs");
        Assert.False(Directory.Exists(legacyRoot), $"Legacy Dialog models remain at {legacyRoot}.");

        var sourceRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Dialogs");
        var testRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests", "Dialogs");
        Assert.False(Directory.Exists(sourceRoot), $"Public Dialog source remains at {sourceRoot}.");
        Assert.False(Directory.Exists(testRoot), $"Public Dialog self-tests remain at {testRoot}.");

    }

    [Fact]
    public void Disconnected_public_editor_project_and_test_project_are_deleted()
    {
        var studioRoot = FindStudioRoot();
        Assert.False(Directory.Exists(Path.Combine(studioRoot, "src", "Asharia.Editor")));
        Assert.False(Directory.Exists(Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests")));
    }

    [Fact]
    public void Legacy_editor_project_excludes_the_target_source_and_test_trees()
    {
        var projectPath = Path.Combine(FindStudioRoot(), "Editor.csproj");
        var project = XDocument.Load(projectPath);

        var compileRemoves = RemovePatterns(project, "Compile");
        Assert.Contains("src/**/*.cs", compileRemoves);
        Assert.Contains("Tests/**/*.cs", compileRemoves);

        var resourceRemoves = RemovePatterns(project, "AvaloniaResource");
        Assert.Contains("src/**/*.axaml", resourceRemoves);
    }

    [Fact]
    public void Studio_app_references_application_and_debug_only_development_boundaries()
    {
        var projectPath = Path.Combine(FindStudioRoot(), "Editor.csproj");
        var project = XDocument.Load(projectPath);

        var references = project
            .Descendants("ProjectReference")
            .Select(element => new
            {
                Include = element.Attribute("Include")?.Value.Replace('\\', '/'),
                Condition = element.Attribute("Condition")?.Value,
            })
            .Where(reference => reference.Include is not null)
            .OrderBy(reference => reference.Include, StringComparer.Ordinal)
            .ToArray();

        Assert.Collection(
            references,
            reference =>
            {
                Assert.Equal(
                    "src/Asharia.Studio.Application/Asharia.Studio.Application.csproj",
                    reference.Include);
                Assert.Null(reference.Condition);
            },
            reference =>
            {
                Assert.Equal(
                    "src/Asharia.Studio.DevelopmentHost/Asharia.Studio.DevelopmentHost.csproj",
                    reference.Include);
                Assert.Equal("'$(Configuration)' == 'Debug'", reference.Condition);
            },
            reference =>
            {
                Assert.Equal(
                    "src/Asharia.Studio.DevelopmentProtocol/Asharia.Studio.DevelopmentProtocol.csproj",
                    reference.Include);
                Assert.Equal("'$(Configuration)' == 'Debug'", reference.Condition);
            },
            reference =>
            {
                Assert.Equal(
                    "src/Asharia.Studio.EngineBridge/Asharia.Studio.EngineBridge.csproj",
                    reference.Include);
                Assert.Null(reference.Condition);
            },
            reference =>
            {
                Assert.Equal(
                    "src/Asharia.Studio.Presentation.Avalonia/Asharia.Studio.Presentation.Avalonia.csproj",
                    reference.Include);
                Assert.Null(reference.Condition);
            });
    }

    [Fact]
    public void Studio_ui_observation_projection_is_a_debug_only_semantic_adapter()
    {
        var studioRoot = FindStudioRoot();
        var projectionPath = Path.Combine(
            studioRoot,
            "Shell",
            "Observation",
            "StudioShellUiObservationProjection.cs");
        Assert.True(File.Exists(projectionPath), $"Studio UI projection is missing at {projectionPath}.");

        var source = File.ReadAllText(projectionPath);
        Assert.StartsWith("#if DEBUG", source, StringComparison.Ordinal);
        Assert.Contains("Dispatcher.UIThread.InvokeAsync", source, StringComparison.Ordinal);
        Assert.Contains("AutomationProperties.GetAutomationId", source, StringComparison.Ordinal);
        Assert.Contains("ObservationProtocolLimits.MaxUiVisualsVisited", source, StringComparison.Ordinal);
        Assert.Contains("ObservationProtocolLimits.MaxUiVisualDepth", source, StringComparison.Ordinal);
        Assert.Contains(": IStudioUiObservationSource", source, StringComparison.Ordinal);
        Assert.DoesNotContain(
            new[]
            {
                "DataContext",
                "System.Reflection",
                "GetType(",
                "Screenshot",
                "PointerPressed",
                "KeyDown",
                "ObservationMethodId.Capture",
                "CaptureAsync",
                "Mutate",
            },
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Legacy_module_contract_and_compatibility_adapter_are_deleted()
    {
        var studioRoot = FindStudioRoot();
        var deletedFiles = new[]
        {
            "Core/Abstractions/IEditorExtensionModule.cs",
            "Core/Abstractions/IEditorFeatureModule.cs",
            "Core/Abstractions/IEditorExtensionActivationContext.cs",
            "Core/Abstractions/IEditorContributionBuilder.cs",
            "Shell/Compatibility/LegacyEditorModuleCompatibilityAdapter.cs",
            "Shell/Composition/EditorExtensionActivationContext.cs",
            "Shell/Composition/EditorFeatureCatalog.cs",
        };

        Assert.All(
            deletedFiles,
            path => Assert.False(
                File.Exists(Path.Combine(studioRoot, path.Replace('/', Path.DirectorySeparatorChar))),
                $"Legacy production source still exists: {path}"));
    }

    [Fact]
    public void Application_project_depends_only_on_runtime_value_contracts()
    {
        var studioRoot = FindStudioRoot();
        var projectPath = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Asharia.Studio.Application.csproj");
        var project = XDocument.Load(projectPath);

        var references = project
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")?.Value.Replace('\\', '/'))
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            ["../Asharia.Runtime.Contracts/Asharia.Runtime.Contracts.csproj"],
            references);
        Assert.Empty(project.Descendants("PackageReference"));
        Assert.Equal("net10.0", RequiredProperty(project, "TargetFramework"));
        Assert.Equal("Asharia.Studio.Application", RequiredProperty(project, "AssemblyName"));
        Assert.Equal("enable", RequiredProperty(project, "Nullable"));
    }

    [Fact]
    public void Development_protocol_is_a_dependency_free_typed_contract_boundary()
    {
        var studioRoot = FindStudioRoot();
        var protocolRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.DevelopmentProtocol");
        var projectPath = Path.Combine(
            protocolRoot,
            "Asharia.Studio.DevelopmentProtocol.csproj");
        Assert.True(File.Exists(projectPath), $"Development Protocol project is missing at {projectPath}.");

        var project = XDocument.Load(projectPath);
        Assert.Empty(project.Descendants("ProjectReference"));
        Assert.Empty(project.Descendants("PackageReference"));
        Assert.Equal("net10.0", RequiredProperty(project, "TargetFramework"));
        Assert.Equal(
            "Asharia.Studio.DevelopmentProtocol",
            RequiredProperty(project, "AssemblyName"));
        Assert.Equal("enable", RequiredProperty(project, "Nullable"));

        var sources = Directory
            .EnumerateFiles(protocolRoot, "*.cs", SearchOption.AllDirectories)
            .Where(path => !path.Contains(
                $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                StringComparison.Ordinal)
                && !path.Contains(
                    $"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                    StringComparison.Ordinal))
            .Select(File.ReadAllText)
            .ToArray();
        var forbiddenTokens = new[]
        {
            "Avalonia",
            "Asharia.Studio.Application",
            "Asharia.Studio.EngineBridge",
            "System.IO",
            "NamedPipe",
            "PipeStream",
            "LibraryImport",
            "DllImport",
            "IntPtr",
            "Dictionary<string, object>",
            "JsonElement",
            "Mcp",
            "MCP",
        };
        Assert.DoesNotContain(
            forbiddenTokens,
            token => sources.Any(source => source.Contains(token, StringComparison.Ordinal)));
    }

    [Fact]
    public void Development_host_has_one_bounded_pipe_adapter_and_no_other_io_or_capture_surface()
    {
        var studioRoot = FindStudioRoot();
        var hostRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.DevelopmentHost");
        var projectPath = Path.Combine(
            hostRoot,
            "Asharia.Studio.DevelopmentHost.csproj");
        Assert.True(File.Exists(projectPath), $"Development Host project is missing at {projectPath}.");

        var project = XDocument.Load(projectPath);
        var references = project
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")?.Value.Replace('\\', '/'))
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();
        Assert.Equal(
            [
                "../Asharia.Studio.Application/Asharia.Studio.Application.csproj",
                "../Asharia.Studio.DevelopmentProtocol/Asharia.Studio.DevelopmentProtocol.csproj",
            ],
            references);
        Assert.Empty(project.Descendants("PackageReference"));
        Assert.Equal("net10.0", RequiredProperty(project, "TargetFramework"));
        Assert.Equal("Asharia.Studio.DevelopmentHost", RequiredProperty(project, "AssemblyName"));
        Assert.Equal("enable", RequiredProperty(project, "Nullable"));

        var sources = Directory
            .EnumerateFiles(hostRoot, "*.cs", SearchOption.AllDirectories)
            .Where(path => !path.Contains(
                $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                StringComparison.Ordinal)
                && !path.Contains(
                    $"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                    StringComparison.Ordinal))
            .Select(path => new
            {
                Path = Path.GetRelativePath(hostRoot, path).Replace('\\', '/'),
                Text = File.ReadAllText(path),
            })
            .ToArray();
        var forbiddenTokens = new[]
        {
            "Avalonia",
            "Asharia.Studio.EngineBridge",
            "LibraryImport",
            "DllImport",
            "Mcp",
            "MCP",
            "Capture",
            "Mutate",
        };
        Assert.DoesNotContain(
            forbiddenTokens,
            token => sources.Any(source => source.Text.Contains(token, StringComparison.Ordinal)));
        Assert.Single(
            sources,
            source => source.Text.Contains(
                "public sealed class StudioDevelopmentHost",
                StringComparison.Ordinal));
        Assert.Single(
            sources,
            source => source.Text.Contains(
                "internal sealed class DevelopmentObservationSession",
                StringComparison.Ordinal));

        var nonTransportSources = sources
            .Where(source => !source.Path.StartsWith("Transport/", StringComparison.Ordinal))
            .ToArray();
        Assert.DoesNotContain(
            new[] { "System.IO", "NamedPipe", "PipeStream" },
            token => nonTransportSources.Any(
                source => source.Text.Contains(token, StringComparison.Ordinal)));

        var uiPortSource = Assert.Single(
            sources,
            source => source.Path == "Hosting/IStudioUiObservationSource.cs").Text;
        Assert.Contains("UiListWindowsParameters", uiPortSource, StringComparison.Ordinal);
        Assert.Contains("UiReadTreeParameters", uiPortSource, StringComparison.Ordinal);
        Assert.DoesNotContain("object", uiPortSource, StringComparison.Ordinal);
        Assert.DoesNotContain("UiReadElement", uiPortSource, StringComparison.Ordinal);
        Assert.DoesNotContain("UiFind", uiPortSource, StringComparison.Ordinal);

        var transportSource = string.Join(
            Environment.NewLine,
            sources
                .Where(source => source.Path.StartsWith("Transport/", StringComparison.Ordinal))
                .Select(source => source.Text));
        Assert.Contains("PipeOptions.CurrentUserOnly", transportSource, StringComparison.Ordinal);
        Assert.Contains("public const int MaxClients = 4", transportSource, StringComparison.Ordinal);
        Assert.Contains("ObservationProtocolLimits.MaxRequestBytes", transportSource, StringComparison.Ordinal);
        Assert.Contains("ObservationProtocolLimits.MaxResponseBytes", transportSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Task.Run", transportSource, StringComparison.Ordinal);
        Assert.DoesNotContain(
            new[]
            {
                "System.Net",
                "TcpListener",
                "HttpListener",
                "WebSocket",
                "NetworkStream",
            },
            token => transportSource.Contains(token, StringComparison.Ordinal));

        var pipeTransportSource = string.Join(
            Environment.NewLine,
            sources
                .Where(source => source.Path is
                    "Transport/PipeFrameProtocol.cs"
                    or "Transport/StudioDevelopmentPipeServer.cs")
                .Select(source => source.Text));
        Assert.DoesNotContain(
            new[]
            {
                "FileStream",
                "File.Write",
                "File.Replace",
                "Directory.Create",
                "FileSystemAclExtensions",
            },
            token => pipeTransportSource.Contains(token, StringComparison.Ordinal));

        var manifestStore = Assert.Single(
            sources,
            source => source.Path == "Transport/CurrentUserManifestStore.cs").Text;
        Assert.Contains("Environment.SpecialFolder.LocalApplicationData", manifestStore, StringComparison.Ordinal);
        Assert.Contains("development-sessions", manifestStore, StringComparison.Ordinal);
        Assert.Contains("SetAccessRuleProtection(isProtected: true", manifestStore, StringComparison.Ordinal);
        Assert.Contains("FileSystemRights.FullControl", manifestStore, StringComparison.Ordinal);
        Assert.Contains("FileSystemAclExtensions.Create", manifestStore, StringComparison.Ordinal);
        Assert.Contains("File.Replace", manifestStore, StringComparison.Ordinal);
        Assert.Contains("Flush(flushToDisk: true)", manifestStore, StringComparison.Ordinal);
        Assert.DoesNotContain("NamedPipe", manifestStore, StringComparison.Ordinal);

        var endpoint = Assert.Single(
            sources,
            source => source.Path == "Transport/StudioDevelopmentPipeEndpoint.cs").Text;
        Assert.Contains("manifestStore.Remove()", endpoint, StringComparison.Ordinal);
        Assert.Contains("pipeServer_.StopAsync", endpoint, StringComparison.Ordinal);
        Assert.True(
            endpoint.IndexOf("manifestStore_.Remove()", StringComparison.Ordinal)
            < endpoint.IndexOf("pipeServer_.StopAsync", StringComparison.Ordinal),
            "Endpoint teardown must remove discovery before stopping its Pipe listener.");
    }

    [Fact]
    public void Development_endpoint_requires_an_exact_command_line_readonly_grant()
    {
        var studioRoot = FindStudioRoot();
        var startupSource = File.ReadAllText(Path.Combine(
            studioRoot,
            "Shell",
            "Composition",
            "StudioDevelopmentStartup.cs"));
        var appSource = File.ReadAllText(Path.Combine(studioRoot, "App.axaml.cs"));
        var compositionSource = File.ReadAllText(Path.Combine(
            studioRoot,
            "Shell",
            "Composition",
            "StudioCompositionSession.cs"));

        Assert.Contains(
            "--development-observation=readonly",
            startupSource,
            StringComparison.Ordinal);
        Assert.Contains("StringComparison.Ordinal", startupSource, StringComparison.Ordinal);
        Assert.DoesNotContain("GetEnvironmentVariable", startupSource, StringComparison.Ordinal);
        Assert.Contains("Environment.GetCommandLineArgs()", appSource, StringComparison.Ordinal);
        Assert.Contains("enableReadOnlyDevelopmentObservation_", appSource, StringComparison.Ordinal);
        Assert.Contains("StudioDevelopmentPipeEndpoint.StartAsync", compositionSource, StringComparison.Ordinal);
        Assert.Contains("developmentEndpoint_.DisposeAsync", compositionSource, StringComparison.Ordinal);
        Assert.True(
            compositionSource.IndexOf("developmentEndpoint_.DisposeAsync", StringComparison.Ordinal)
            < compositionSource.IndexOf("developmentHost_.DisposeAsync", StringComparison.Ordinal),
            "Composition teardown must stop discovery/Pipe before its in-process Host.");
    }

    [Fact]
    public void Target_solution_contains_only_the_declared_boundary_projects()
    {
        var solutionPath = Path.Combine(FindStudioRoot(), "Asharia.Studio.sln");
        Assert.True(File.Exists(solutionPath), $"Expected target Studio solution at {solutionPath}.");

        var projectPaths = Regex
            .Matches(
                File.ReadAllText(solutionPath),
                "^Project\\([^\\r\\n]+\\) = \\\"[^\\\"]+\\\", \\\"(?<path>[^\\\"]+\\.csproj)\\\"",
                RegexOptions.Multiline | RegexOptions.CultureInvariant)
            .Select(match => match.Groups["path"].Value.Replace('\\', '/'))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            [
                "../../tools/asharia-studio-observe.Tests/Asharia.Studio.Observe.Tests.csproj",
                "../../tools/asharia-studio-observe/Asharia.Studio.Observe.csproj",
                "Editor.csproj",
                "Tests/Asharia.Studio.Application.Tests/Asharia.Studio.Application.Tests.csproj",
                "Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj",
                "Tests/Asharia.Studio.DevelopmentHost.Tests/Asharia.Studio.DevelopmentHost.Tests.csproj",
                "Tests/Asharia.Studio.DevelopmentProtocol.Tests/Asharia.Studio.DevelopmentProtocol.Tests.csproj",
                "Tests/Asharia.Studio.EngineBridge.Tests/Asharia.Studio.EngineBridge.Tests.csproj",
                "Tests/Asharia.Studio.Headless.Tests/Asharia.Studio.Headless.Tests.csproj",
                "Tests/Asharia.Studio.WindowsCapture.Tests/Asharia.Studio.WindowsCapture.Tests.csproj",
                "Tests/Editor.Tests/Editor.Tests.csproj",
                "src/Asharia.Runtime.Contracts/Asharia.Runtime.Contracts.csproj",
                "src/Asharia.Studio.Application/Asharia.Studio.Application.csproj",
                "src/Asharia.Studio.DevelopmentHost/Asharia.Studio.DevelopmentHost.csproj",
                "src/Asharia.Studio.DevelopmentProtocol/Asharia.Studio.DevelopmentProtocol.csproj",
                "src/Asharia.Studio.EngineBridge/Asharia.Studio.EngineBridge.csproj",
                "src/Asharia.Studio.Presentation.Avalonia/Asharia.Studio.Presentation.Avalonia.csproj",
            ],
            projectPaths);
    }

    [Fact]
    public void Observe_cli_and_mcp_are_protocol_only_bounded_read_adapters()
    {
        var studioRoot = FindStudioRoot();
        var repositoryRoot = Path.GetFullPath(Path.Combine(studioRoot, "..", ".."));
        var codexConfigPath = Path.Combine(repositoryRoot, ".codex", "config.toml");
        Assert.True(
            File.Exists(codexConfigPath),
            $"Project-scoped Codex configuration is missing at {codexConfigPath}.");
        var codexConfig = File.ReadAllText(codexConfigPath);
        Assert.Equal(
            1,
            CountExact(codexConfig, "[mcp_servers.asharia_studio_observe]"));
        Assert.Contains("command = \"dotnet\"", codexConfig, StringComparison.Ordinal);
        Assert.Contains(
            "\"tools/asharia-studio-observe/bin/Release/net10.0/asharia-studio-observe.dll\"",
            codexConfig,
            StringComparison.Ordinal);
        Assert.DoesNotContain("cwd =", codexConfig, StringComparison.Ordinal);
        Assert.Contains("enabled = true", codexConfig, StringComparison.Ordinal);
        Assert.Contains("required = false", codexConfig, StringComparison.Ordinal);
        var enabledToolsMatch = Regex.Match(
            codexConfig,
            "(?ms)^enabled_tools\\s*=\\s*\\[\\s*(?<body>.*?)^\\s*\\]");
        Assert.True(
            enabledToolsMatch.Success,
            "Project-scoped Codex configuration has no enabled_tools array.");
        var enabledTools = Regex.Matches(
                enabledToolsMatch.Groups["body"].Value,
                "\\\"(?<tool>[^\\\"]+)\\\"")
            .Select(match => match.Groups["tool"].Value)
            .ToArray();
        Assert.Equal(
            [
                "studio_list_sessions",
                "studio_describe_session",
                "studio_read_diagnostics",
                "studio_read_logs",
                "studio_list_ui_windows",
                "studio_read_ui_tree",
            ],
            enabledTools);
        Assert.True(File.Exists(Path.Combine(
            repositoryRoot,
            "tools",
            "asharia-studio-observe",
            "Asharia.Studio.Observe.csproj")));

        var cliRoot = Path.Combine(repositoryRoot, "tools", "asharia-studio-observe");
        var projectPath = Path.Combine(cliRoot, "Asharia.Studio.Observe.csproj");
        Assert.True(File.Exists(projectPath), $"Observe CLI project is missing at {projectPath}.");

        var project = XDocument.Load(projectPath);
        var references = project
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")?.Value.Replace('\\', '/'))
            .OfType<string>()
            .ToArray();
        Assert.Equal(
            ["../../apps/studio/src/Asharia.Studio.DevelopmentProtocol/Asharia.Studio.DevelopmentProtocol.csproj"],
            references);
        Assert.Empty(project.Descendants("PackageReference"));
        Assert.Equal("Exe", RequiredProperty(project, "OutputType"));
        Assert.Equal("net10.0", RequiredProperty(project, "TargetFramework"));
        Assert.Equal("asharia-studio-observe", RequiredProperty(project, "AssemblyName"));

        var sources = Directory
            .EnumerateFiles(cliRoot, "*.cs", SearchOption.AllDirectories)
            .Where(path => !path.Contains(
                $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                StringComparison.Ordinal)
                && !path.Contains(
                    $"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                    StringComparison.Ordinal))
            .Select(path => new
            {
                Path = Path.GetRelativePath(cliRoot, path).Replace('\\', '/'),
                Text = File.ReadAllText(path),
            })
            .ToArray();
        var allSource = string.Join(Environment.NewLine, sources.Select(source => source.Text));
        Assert.DoesNotContain(
            new[]
            {
                "Asharia.Studio.Application",
                "Asharia.Studio.DevelopmentHost",
                "Avalonia",
                "System.Net",
                "TcpClient",
                "HttpClient",
                "WebSocket",
                "LibraryImport",
                "DllImport",
                "System.Reflection",
                "Task.Run",
                "ObservationMethodId.Capture",
                "CaptureAsync",
                "Mutate",
            },
            token => allSource.Contains(token, StringComparison.Ordinal));
        Assert.Single(
            sources,
            source => source.Text.Contains("new NamedPipeClientStream(", StringComparison.Ordinal));
        Assert.Contains("PipeOptions.CurrentUserOnly", allSource, StringComparison.Ordinal);
        Assert.Contains("internal const int MaxSessionManifests = 64", allSource, StringComparison.Ordinal);
        Assert.Contains("MaxSessionManifestBytes", allSource, StringComparison.Ordinal);
        Assert.Contains("SetAccessRuleProtection", File.ReadAllText(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.DevelopmentHost",
            "Transport",
            "CurrentUserManifestStore.cs")), StringComparison.Ordinal);

        var commandSource = Assert.Single(
            sources,
            source => source.Path == "CommandLine/StudioObserveCommand.cs").Text;
        Assert.DoesNotContain("AttachToken", commandSource, StringComparison.Ordinal);
        Assert.DoesNotContain("PipeName", commandSource, StringComparison.Ordinal);
        Assert.Contains("StudioObserveVerb.List", commandSource, StringComparison.Ordinal);
        Assert.Contains("StudioObserveVerb.Describe", commandSource, StringComparison.Ordinal);
        Assert.DoesNotContain("StudioObserveVerb.State", commandSource, StringComparison.Ordinal);
        Assert.Contains("StudioObserveVerb.Logs", commandSource, StringComparison.Ordinal);
        Assert.Contains("StudioObserveVerb.Diagnostics", commandSource, StringComparison.Ordinal);
        Assert.Contains("StudioObserveVerb.UiListWindows", commandSource, StringComparison.Ordinal);
        Assert.Contains("StudioObserveVerb.UiReadTree", commandSource, StringComparison.Ordinal);
        Assert.DoesNotContain("ObservationMethodId.UiReadElement", allSource, StringComparison.Ordinal);
        Assert.DoesNotContain("ObservationMethodId.UiFind", allSource, StringComparison.Ordinal);

        var mcpServerSource = Assert.Single(
            sources,
            source => source.Path == "Mcp/StudioMcpServer.cs").Text;
        var mcpToolsSource = Assert.Single(
            sources,
            source => source.Path == "Mcp/StudioMcpTools.cs").Text;
        Assert.Contains("ProtocolVersion = \"2025-06-18\"", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("\"initialize\"", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("\"notifications/initialized\"", mcpServerSource, StringComparison.Ordinal);
        Assert.DoesNotContain("\"server/discover\"", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("\"tools/list\"", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("\"tools/call\"", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("MaxInflightRequests = 8", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("MaxInputBytes = ObservationProtocolLimits.MaxRequestBytes", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("MaxOutputBytes = ObservationProtocolLimits.MaxResponseBytes", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("Console.OpenStandardInput()", mcpServerSource, StringComparison.Ordinal);
        Assert.Contains("Console.OpenStandardOutput()", mcpServerSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Process.Start", mcpServerSource, StringComparison.Ordinal);
        Assert.DoesNotContain("StudioObserveCommand", mcpServerSource, StringComparison.Ordinal);
        Assert.Equal(
            1,
            CountExact(mcpToolsSource, "\"studio_list_sessions\","));
        Assert.Equal(
            1,
            CountExact(mcpToolsSource, "\"studio_describe_session\","));
        Assert.Equal(
            1,
            CountExact(mcpToolsSource, "\"studio_read_diagnostics\","));
        Assert.Equal(
            1,
            CountExact(mcpToolsSource, "\"studio_read_logs\","));
        Assert.Equal(
            1,
            CountExact(mcpToolsSource, "\"studio_list_ui_windows\","));
        Assert.Equal(
            1,
            CountExact(mcpToolsSource, "\"studio_read_ui_tree\","));
        Assert.DoesNotContain("studio_read_state", mcpToolsSource, StringComparison.Ordinal);
        Assert.DoesNotContain("studio_read_element", mcpToolsSource, StringComparison.Ordinal);
        Assert.DoesNotContain("studio_find", mcpToolsSource, StringComparison.Ordinal);
        Assert.DoesNotContain("shell", mcpToolsSource, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Application_sources_do_not_reference_ui_native_or_legacy_implementation()
    {
        var sourceRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application");
        var forbiddenTokens = new[]
        {
            "Avalonia",
            "LibraryImport",
            "DllImport",
            "System.Runtime.InteropServices",
            "Editor.Core",
            "Editor.Shell",
            "Editor.Features",
            "Vulkan",
        };

        var offenders = Directory
            .EnumerateFiles(sourceRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => new
            {
                Path = Path.GetRelativePath(sourceRoot, path),
                Text = File.ReadAllText(path),
            })
            .SelectMany(file => forbiddenTokens
                .Where(token => file.Text.Contains(token, StringComparison.Ordinal))
                .Select(token => $"{file.Path}: {token}"))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Disconnected_build_and_extension_control_planes_are_deleted()
    {
        var projectCodeRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode");
        var projectCodeTests = Path.Combine(
            FindStudioRoot(),
            "Tests",
            "Asharia.Studio.Application.Tests",
            "ProjectCode");
        var distributionBootstrapRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "Bootstrap");
        var distributionBootstrapTests = Path.Combine(
            FindStudioRoot(),
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Bootstrap");
        var applicationExtensionsRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "Extensions");
        var applicationExtensionsTests = Path.Combine(
            FindStudioRoot(),
            "Tests",
            "Asharia.Studio.Application.Tests",
            "Extensions");

        Assert.False(
            Directory.Exists(projectCodeRoot),
            $"Disconnected ProjectCode production source remains at {projectCodeRoot}.");
        Assert.False(
            Directory.Exists(projectCodeTests),
            $"ProjectCode-only tests remain at {projectCodeTests}.");
        Assert.False(
            Directory.Exists(distributionBootstrapRoot),
            $"Disconnected distribution bootstrap source remains at {distributionBootstrapRoot}.");
        Assert.False(
            Directory.Exists(distributionBootstrapTests),
            $"Distribution-bootstrap-only tests remain at {distributionBootstrapTests}.");
        Assert.False(
            Directory.Exists(applicationExtensionsRoot),
            $"Disconnected Application extension host remains at {applicationExtensionsRoot}.");
        Assert.False(
            Directory.Exists(applicationExtensionsTests),
            $"Application-extension-only tests remain at {applicationExtensionsTests}.");
    }

    [Fact]
    public void Public_editor_source_and_project_roots_are_deleted()
    {
        var studioRoot = FindStudioRoot();
        Assert.False(Directory.Exists(Path.Combine(studioRoot, "src", "Asharia.Editor")));
        Assert.False(Directory.Exists(Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests")));
    }

    [Fact]
    public void Disconnected_public_editor_sdk_surface_is_deleted()
    {
        var studioRoot = FindStudioRoot();
        var publicSourceRoot = Path.Combine(studioRoot, "src", "Asharia.Editor");
        var publicTestRoot = Path.Combine(studioRoot, "Tests", "Asharia.Editor.Tests");

        Assert.False(Directory.Exists(publicSourceRoot));
        Assert.False(Directory.Exists(publicTestRoot));
    }

    [Fact]
    public void Code_first_source_and_host_are_deleted()
    {
        var studioRoot = FindStudioRoot();
        var legacyRoot = Path.Combine(studioRoot, "Core", "CodeFirstUI");
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "UI", "CodeFirst");
        var shellRoot = Path.Combine(studioRoot, "Shell", "CodeFirstUI");

        Assert.False(Directory.Exists(legacyRoot), $"Legacy Code-first source remains at {legacyRoot}.");
        Assert.False(Directory.Exists(publicRoot), $"Public Code-first source remains at {publicRoot}.");
        Assert.False(Directory.Exists(shellRoot), $"Code-first shell host remains at {shellRoot}.");

        Assert.False(File.Exists(Path.Combine(studioRoot, "ViewLocator.cs")));
    }

    private static string RequiredProperty(XDocument project, string propertyName)
    {
        return project
            .Descendants(propertyName)
            .Select(element => element.Value.Trim())
            .Single();
    }

    private static string[] RemovePatterns(XDocument project, string itemName)
    {
        return project
            .Descendants(itemName)
            .Attributes("Remove")
            .Select(attribute => attribute.Value.Replace('\\', '/'))
            .ToArray();
    }

    private static int CountExact(string source, string value)
    {
        var count = 0;
        var offset = 0;
        while ((offset = source.IndexOf(value, offset, StringComparison.Ordinal)) >= 0)
        {
            ++count;
            offset += value.Length;
        }

        return count;
    }

    private static string FindStudioRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Asharia.Studio.sln"))
                && File.Exists(Path.Combine(directory.FullName, "Editor.csproj")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Asharia.Studio.sln"))
                && File.Exists(Path.Combine(directory.FullName, "Editor.csproj")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate apps/studio from Asharia.Studio.sln.");
    }
}
