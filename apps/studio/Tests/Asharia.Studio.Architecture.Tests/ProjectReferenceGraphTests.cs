using Asharia.Editor.Dialogs;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Lifecycle;
using Asharia.Editor.Panels;
using Asharia.Editor.Selection;
using Asharia.Editor.Tasks;
using Asharia.Editor.Transactions;
using Asharia.Editor.UI.CodeFirst.Abstractions;
using Asharia.Editor.Viewports;
using Asharia.Editor.Worlds.Snapshots;
using Asharia.Studio.Application.Commands;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Lifecycle;
using Asharia.Studio.Application.Panels;
using Asharia.Studio.Application.Providers;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Tasks;
using Asharia.Studio.Application.Transactions;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Scene;
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
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
            ["../Asharia.Runtime.Contracts/Asharia.Runtime.Contracts.csproj"],
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
    public void Scene_provider_runtime_host_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationProviderRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Providers");

        Assert.True(
            File.Exists(Path.Combine(applicationProviderRoot, "EditorProviderHost.cs")),
            $"Application provider host is missing from {applicationProviderRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorProviderHost).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorProviderStatusSnapshot).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorProviderState).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Editor",
            typeof(ISceneSnapshotProvider).Assembly.GetName().Name);

        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "Shell",
            "Composition",
            "EditorProviderHost.cs")));
        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "Core",
            "Models",
            "Scene",
            "EditorProviderStatusSnapshot.cs")));
        Assert.False(File.Exists(Path.Combine(
            studioRoot,
            "Core",
            "Models",
            "Scene",
            "EditorProviderState.cs")));

        var registrationConsumers = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => new
            {
                Path = Path.GetRelativePath(studioRoot, path).Replace('\\', '/'),
                Text = File.ReadAllText(path),
            })
            .Where(file => !file.Path.StartsWith("Tests/", StringComparison.Ordinal)
                && !file.Path.StartsWith("src/", StringComparison.Ordinal)
                && !file.Path.Contains("/bin/", StringComparison.Ordinal)
                && !file.Path.Contains("/obj/", StringComparison.Ordinal))
            .Where(file => file.Text.Contains(
                nameof(EditorSceneProviderRegistration),
                StringComparison.Ordinal))
            .Select(file => file.Path)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            ["Shell/Compatibility/LegacyEditorModuleCompatibilityAdapter.cs"],
            registrationConsumers);
    }

    [Fact]
    public void Command_status_router_uses_the_public_command_executor_contract()
    {
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorCommandStatusMessageRouter).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IEditorGuiCommandExecutor),
            typeof(EditorCommandStatusMessageRouter).GetInterfaces());

        var constructor = Assert.Single(typeof(EditorCommandStatusMessageRouter).GetConstructors());
        Assert.Equal(
            typeof(IEditorGuiCommandExecutor),
            constructor.GetParameters()[0].ParameterType);

        var legacySource = File.ReadAllText(Path.Combine(
            FindStudioRoot(),
            "Shell",
            "Commands",
            "WorkbenchCommandRouter.cs"));
        Assert.DoesNotContain("interface IWorkbenchCommandRouter", legacySource, StringComparison.Ordinal);
    }

    [Fact]
    public void Panel_frame_scheduler_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Panels");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "EditorPanelFrameScheduler.cs")),
            $"Application panel frame scheduler is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorPanelFrameScheduler).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Editor",
            typeof(IEditorPanelFrameUpdateSink).Assembly.GetName().Name);

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class EditorPanelFrameScheduler", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void Viewport_scheduler_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Viewports");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "ViewportScheduler.cs")),
            $"Application viewport scheduler is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(ViewportScheduler).Assembly.GetName().Name);
        Assert.Equal(
            "Asharia.Editor",
            typeof(ViewportSchedulerContext).Assembly.GetName().Name);

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class ViewportScheduler", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void Diagnostic_service_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Diagnostics");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "EditorDiagnosticService.cs")),
            $"Application diagnostic service is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorDiagnosticService).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IEditorDiagnosticService),
            typeof(EditorDiagnosticService).GetInterfaces());

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class EditorDiagnosticService", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void Transaction_service_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Transactions");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "EditorTransactionService.cs")),
            $"Application transaction service is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorTransactionService).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IEditorTransactionService),
            typeof(EditorTransactionService).GetInterfaces());

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class EditorTransactionService", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void Lifecycle_event_service_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Lifecycle");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "EditorLifecycleEventService.cs")),
            $"Application lifecycle event service is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorLifecycleEventService).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IEditorLifecycleEventService),
            typeof(EditorLifecycleEventService).GetInterfaces());

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class EditorLifecycleEventService", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void Background_task_service_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Tasks");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "EditorBackgroundTaskService.cs")),
            $"Application background task service is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorBackgroundTaskService).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IEditorBackgroundTaskService),
            typeof(EditorBackgroundTaskService).GetInterfaces());

        var legacyOwners = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("class EditorBackgroundTaskService", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyOwners);
    }

    [Fact]
    public void Selection_service_implementation_is_owned_only_by_application()
    {
        var studioRoot = FindStudioRoot();
        var applicationRoot = Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Selection");

        Assert.True(
            File.Exists(Path.Combine(applicationRoot, "EditorSelectionService.cs")),
            $"Application selection service is missing from {applicationRoot}.");
        Assert.Equal(
            "Asharia.Studio.Application",
            typeof(EditorSelectionService).Assembly.GetName().Name);
        Assert.Contains(
            typeof(IEditorSelectionService),
            typeof(EditorSelectionService).GetInterfaces());

        var legacyConsumers = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("namespace Editor.Shell.Selection", StringComparison.Ordinal))
            .ToArray();

        Assert.Empty(legacyConsumers);
    }

    [Fact]
    public void Public_dialog_contracts_replace_legacy_dialog_models()
    {
        var studioRoot = FindStudioRoot();
        var legacyRoot = Path.Combine(studioRoot, "Core", "Models", "Dialogs");
        Assert.False(Directory.Exists(legacyRoot), $"Legacy Dialog models remain at {legacyRoot}.");

        var dialogTypes = typeof(EditorDialogRequest).Assembly
            .GetExportedTypes()
            .Where(type => type.Namespace == "Asharia.Editor.Dialogs")
            .OrderBy(type => type.Name, StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            [
                "EditorDialogActionDescriptor",
                "EditorDialogActionId",
                "EditorDialogActionRole",
                "EditorDialogCompletionKind",
                "EditorDialogRequest",
                "EditorDialogResult",
                "EditorDialogSeverity",
            ],
            dialogTypes.Select(type => type.Name));
        Assert.All(
            dialogTypes,
            type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));

        var properties = dialogTypes.SelectMany(type => type.GetProperties()).ToArray();
        Assert.DoesNotContain(properties, property => property.PropertyType == typeof(Type));
        Assert.DoesNotContain(properties, property => property.PropertyType == typeof(object));
        Assert.DoesNotContain(
            properties,
            property => typeof(Delegate).IsAssignableFrom(property.PropertyType));

        var apiParameterTypes = dialogTypes
            .SelectMany(type => type.GetConstructors().SelectMany(constructor => constructor.GetParameters())
                .Concat(type.GetMethods()
                    .Where(method => method.IsStatic && method.DeclaringType == type)
                    .SelectMany(method => method.GetParameters())))
            .Select(parameter => parameter.ParameterType)
            .ToArray();
        Assert.DoesNotContain(apiParameterTypes, type => type == typeof(Type));
        Assert.DoesNotContain(apiParameterTypes, type => type == typeof(object));
        Assert.DoesNotContain(apiParameterTypes, type => type == typeof(CancellationToken));
        Assert.DoesNotContain(
            apiParameterTypes,
            type => typeof(Delegate).IsAssignableFrom(type));

        var sourceRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "Dialogs");
        var source = string.Join(
            Environment.NewLine,
            Directory.EnumerateFiles(sourceRoot, "*.cs", SearchOption.AllDirectories)
                .Select(File.ReadAllText));
        foreach (var forbidden in new[]
        {
            "Avalonia",
            "Window",
            "Control",
            "ViewModel",
            "Func<object>",
            "LibraryImport",
            "DllImport",
            "Vulkan",
            "Asharia.Studio.",
            "Editor.Core",
            "CancellationToken",
        })
        {
            Assert.DoesNotContain(forbidden, source, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void Public_editor_project_is_a_dependency_free_net10_library()
    {
        var projectPath = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Editor",
            "Asharia.Editor.csproj");

        Assert.True(File.Exists(projectPath), $"Expected public Editor project at {projectPath}.");

        var project = XDocument.Load(projectPath);
        Assert.Empty(project.Descendants("ProjectReference"));
        Assert.Empty(project.Descendants("PackageReference"));
        Assert.Equal("net10.0", RequiredProperty(project, "TargetFramework"));
        Assert.Equal("Asharia.Editor", RequiredProperty(project, "AssemblyName"));
        Assert.Equal("Asharia.Editor", RequiredProperty(project, "RootNamespace"));
        Assert.Equal("enable", RequiredProperty(project, "Nullable"));
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
    public void Legacy_editor_references_only_the_runtime_public_editor_and_application_projects()
    {
        var projectPath = Path.Combine(FindStudioRoot(), "Editor.csproj");
        var project = XDocument.Load(projectPath);

        var references = project
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")?.Value.Replace('\\', '/'))
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            [
                "src/Asharia.Editor/Asharia.Editor.csproj",
                "src/Asharia.Runtime.Contracts/Asharia.Runtime.Contracts.csproj",
                "src/Asharia.Studio.Application/Asharia.Studio.Application.csproj",
            ],
            references);
    }

    [Fact]
    public void Legacy_module_contract_is_consumed_only_by_the_compatibility_adapter()
    {
        var studioRoot = FindStudioRoot();
        var allowedFiles = new[]
        {
            "Core/Abstractions/IEditorExtensionModule.cs",
            "Core/Abstractions/IEditorFeatureModule.cs",
            "Shell/Compatibility/LegacyEditorModuleCompatibilityAdapter.cs",
        };
        var consumers = Directory
            .EnumerateFiles(studioRoot, "*.cs", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(studioRoot, path).Replace('\\', '/'))
            .Where(path => !path.StartsWith("Tests/", StringComparison.Ordinal)
                && !path.StartsWith("src/", StringComparison.Ordinal)
                && !path.Contains("/bin/", StringComparison.Ordinal)
                && !path.Contains("/obj/", StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(studioRoot, path))
                .Contains("IEditorExtensionModule", StringComparison.Ordinal))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(allowedFiles, consumers);
    }

    [Fact]
    public void Application_project_references_only_the_public_editor_project()
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

        Assert.Equal(["../Asharia.Editor/Asharia.Editor.csproj"], references);
        Assert.Empty(project.Descendants("PackageReference"));
        Assert.Equal("net10.0", RequiredProperty(project, "TargetFramework"));
        Assert.Equal("Asharia.Studio.Application", RequiredProperty(project, "AssemblyName"));
        Assert.Equal("enable", RequiredProperty(project, "Nullable"));
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
                "Editor.csproj",
                "Tests/Asharia.Editor.Tests/Asharia.Editor.Tests.csproj",
                "Tests/Asharia.Studio.Application.Tests/Asharia.Studio.Application.Tests.csproj",
                "Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj",
                "Tests/Asharia.Studio.EngineBridge.Tests/Asharia.Studio.EngineBridge.Tests.csproj",
                "Tests/Editor.Tests/Editor.Tests.csproj",
                "src/Asharia.Editor/Asharia.Editor.csproj",
                "src/Asharia.Runtime.Contracts/Asharia.Runtime.Contracts.csproj",
                "src/Asharia.Studio.Application/Asharia.Studio.Application.csproj",
                "src/Asharia.Studio.EngineBridge/Asharia.Studio.EngineBridge.csproj",
            ],
            projectPaths);
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
    public void Project_code_environment_credential_does_not_execute_or_discover_global_dotnet()
    {
        var sourceRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode");
        var forbiddenTokens = new[]
        {
            "ProcessStartInfo",
            "Process.Start",
            "Assembly.Load(",
            "AssemblyLoadContext",
            "AssemblyName.GetAssemblyName",
            "DOTNET_ROOT",
            "GetEnvironmentVariable",
            "EnumerateDirectories",
            "--list-sdks",
            "--list-runtimes",
            "--version",
        };
        var offenders = Directory
            .EnumerateFiles(
                sourceRoot,
                "ProjectCodeBuildEnvironmentCredential*.cs",
                SearchOption.TopDirectoryOnly)
            .Select(path => new
            {
                Path = Path.GetRelativePath(sourceRoot, path),
                Text = File.ReadAllText(path),
            })
            .SelectMany(file => forbiddenTokens
                .Where(token => file.Text.Contains(
                    token,
                    StringComparison.Ordinal))
                .Select(token => $"{file.Path}: {token}"))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Project_code_artifact_inspector_reads_only_the_raw_output_lease()
    {
        var sourcePath = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodeArtifactInspector.cs");
        var source = File.ReadAllText(sourcePath);
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "AssemblyName.GetAssemblyName",
            "AssemblyLoadContext",
            "MetadataLoadContext",
            "DllImport",
            "LibraryImport",
            "Mono.Cecil",
            "Process.Start",
        };

        Assert.Contains(
            "ProjectCodeRawBuildOutputLease lease",
            source,
            StringComparison.Ordinal);
        Assert.Contains("PEReader", source, StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_artifact_publisher_only_copies_inspected_evidence()
    {
        var sourcePath = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodeArtifactPublisher.cs");
        var source = File.ReadAllText(sourcePath);
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "AssemblyName.GetAssemblyName",
            "AssemblyLoadContext",
            "MetadataLoadContext",
            "DllImport",
            "LibraryImport",
            "Mono.Cecil",
            "Process.Start",
            "Avalonia",
            "current.json",
            "latest.json",
        };

        Assert.Contains(
            "ProjectCodeRawBuildOutputLease lease",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ProjectCodeArtifactInspector",
            source,
            StringComparison.Ordinal);
        Assert.Contains("Directory.Move", source, StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_module_indexer_reads_publication_metadata_without_loading()
    {
        var sourcePath = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodeModuleIndexer.cs");
        var source = File.ReadAllText(sourcePath);
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "AssemblyName.GetAssemblyName",
            "AssemblyLoadContext",
            "MetadataLoadContext",
            "Activator.",
            "Type.GetType",
            ".GetTypes(",
            "DllImport",
            "LibraryImport",
            "Mono.Cecil",
            "Process.Start",
            "Avalonia",
            "File.Write",
            "Directory.Create",
            "Directory.Move",
        };

        Assert.Contains(
            "ProjectCodeArtifactPublicationReceipt publication",
            source,
            StringComparison.Ordinal);
        Assert.Contains("PEReader", source, StringComparison.Ordinal);
        Assert.Contains("DecodeValue", source, StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_staging_candidate_rebuilds_index_without_loading()
    {
        var sourcePath = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodeStagingCandidateAdmitter.cs");
        var source = File.ReadAllText(sourcePath);
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "AssemblyName.GetAssemblyName",
            "AssemblyLoadContext",
            "MetadataLoadContext",
            "Activator.",
            "Type.GetType",
            ".GetTypes(",
            "DllImport",
            "LibraryImport",
            "Mono.Cecil",
            "Process.Start",
            "Avalonia",
            "File.Write",
            "Directory.Create",
            "Directory.Move",
        };

        Assert.Contains(
            "ProjectCodeArtifactPublicationReceipt publication",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ProjectCodeModuleIndexer",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "ProjectCodeModuleIndexReport moduleIndex",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_host_policy_is_selected_before_any_load()
    {
        var sourceRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode");
        var source = string.Join(
            Environment.NewLine,
            new[]
            {
                "ProjectCodeHostPolicy.cs",
                "ProjectCodeHostPolicySelector.cs",
            }.Select(name => File.ReadAllText(Path.Combine(sourceRoot, name))));
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "AssemblyName.GetAssemblyName",
            "AssemblyLoadContext",
            "MetadataLoadContext",
            "Activator.",
            "Type.GetType",
            ".GetTypes(",
            "DllImport",
            "LibraryImport",
            "Mono.Cecil",
            "Process.Start",
            "Avalonia",
            "File.",
            "Directory.",
        };

        Assert.Contains(
            "ProjectCodeStagingCandidateReceipt candidate",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "IsCandidateCurrentAsync",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ProjectCodeHostKind.Pinned",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ProjectCodeReplacementPolicy.RestartRequired",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "ProjectCodeHostKind hostKind,",
            File.ReadAllText(Path.Combine(
                sourceRoot,
                "ProjectCodeHostPolicySelector.cs")),
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_load_image_is_snapshotted_without_loading()
    {
        var sourceRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode");
        var builderSource = File.ReadAllText(Path.Combine(
            sourceRoot,
            "ProjectCodePinnedLoadImageBuilder.cs"));
        var snapshotSource = File.ReadAllText(Path.Combine(
            sourceRoot,
            "ProjectCodePinnedLoadImage.cs"));
        var source = builderSource + Environment.NewLine + snapshotSource;
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "AssemblyName.GetAssemblyName",
            "AssemblyLoadContext",
            "MetadataLoadContext",
            "LoadFromStream",
            "Activator.",
            "Type.GetType",
            ".GetTypes(",
            "DllImport",
            "LibraryImport",
            "Mono.Cecil",
            "Process.Start",
            "Avalonia",
            "File.Write",
            "Directory.Create",
            "Directory.Move",
        };

        Assert.Contains(
            "BuildAsync(\n        ProjectCodeHostPolicyReceipt policy,",
            builderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "IsPolicyCurrentAsync",
            builderSource,
            StringComparison.Ordinal);
        Assert.Contains("PEReader", builderSource, StringComparison.Ordinal);
        Assert.Contains("\".cctor\"", builderSource, StringComparison.Ordinal);
        Assert.Contains(
            "publiclyVisible: false",
            snapshotSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "byte[] ImplementationBytes",
            snapshotSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_assembly_loader_stops_before_type_resolution()
    {
        var sourceRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode");
        var loaderSource = File.ReadAllText(Path.Combine(
            sourceRoot,
            "ProjectCodePinnedAssemblyLoader.cs"));
        var hostSource = File.ReadAllText(Path.Combine(
            sourceRoot,
            "ProjectCodePinnedAssemblyHost.cs"));
        var source = loaderSource + Environment.NewLine + hostSource;
        var forbiddenTokens = new[]
        {
            "Assembly.Load(",
            "LoadFromAssemblyPath",
            "AssemblyDependencyResolver",
            "MetadataLoadContext",
            "EnterContextualReflection",
            "Resolving +=",
            "LoadUnmanagedDll",
            ".GetTypes(",
            ".GetType(",
            "Activator.",
            "Configure(",
            "ActivateAsync(",
            ".Unload(",
            "Process.Start",
            "Avalonia",
            "File.Write",
            "Directory.Create",
            "Directory.Move",
        };

        Assert.Contains(
            "ProjectCodePinnedLoadImageSnapshot image,",
            loaderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "IsSnapshotCurrentAsync",
            loaderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "AssemblyLoadContext(name, isCollectible: false)",
            loaderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "LoadFromStream(implementation, portablePdb)",
            loaderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "Load(AssemblyName assemblyName) => null",
            loaderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "Dictionary<Guid, Reservation>",
            loaderSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "AssemblyLoadContext.GetLoadContext(assembly)",
            hostSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_module_type_resolution_stops_before_construction()
    {
        var source = File.ReadAllText(Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleTypeResolver.cs"));
        var forbiddenTokens = new[]
        {
            "GetTypes(",
            "DefinedTypes",
            "Type.GetType(",
            "GetCustomAttribute",
            "CustomAttributeData",
            "Activator.",
            ".Invoke(",
            "CreateDelegate",
            "RuntimeHelpers.RunClassConstructor",
            "Configure(",
            "ActivateAsync(",
            "StaticEditorModuleRegistration",
            "EditorModuleRegistry",
            "EditorModuleHost",
            "AssemblyLoadContext",
            "LoadFrom",
            "MetadataLoadContext",
            "EnterContextualReflection",
            "Func<",
            "CancellationToken",
            "File.",
            "Directory.",
        };

        Assert.Contains(
            "Resolve(\n        ProjectCodePinnedAssemblyHost host)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "host.Assembly.GetType(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "throwOnError: false",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ignoreCase: false",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ReferenceEquals(type.Assembly, host.Assembly)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ReferenceEquals(type.BaseType, typeof(EditorModule))",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "type.GetConstructor(Type.EmptyTypes)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "host.Image.Policy.Candidate.ModuleIndex.Entries",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "host.Image.Policy.Candidate.ModuleIndex.IndexId",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_module_construction_stops_before_configuration()
    {
        var studioRoot = FindStudioRoot();
        var source = File.ReadAllText(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleConstructor.cs"));
        var typeSource = File.ReadAllText(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleTypeResolver.cs"));
        var forbiddenTokens = new[]
        {
            "Assembly.GetType(",
            "GetTypes(",
            "DefinedTypes",
            "Type.GetType(",
            "GetConstructor(",
            "GetCustomAttribute",
            "CustomAttributeData",
            "Activator.",
            "CreateDelegate",
            "RuntimeHelpers.RunClassConstructor",
            "Configure(",
            "ActivateAsync(",
            "StaticEditorModuleRegistration",
            "StaticPackageGenerationHost",
            "EditorModuleDefinition",
            "EditorModuleRegistry",
            "EditorModuleHost",
            "AssemblyLoadContext",
            "LoadFrom",
            "MetadataLoadContext",
            "EnterContextualReflection",
            "Func<",
            "CancellationToken",
            "Task<",
            "File.",
            "Directory.",
        };

        Assert.Contains(
            "Construct(\n        ProjectCodePinnedModuleTypeSet moduleTypes)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "moduleType.Constructor.Invoke(null)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "value is not EditorModule module",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "retainedModules_.Add(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "result_ is not null",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ReferenceEquals(expected.Host, candidate.Host)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "project-code.pinned-module-construction.constructor-failed-restart-required",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "public ConstructorInfo Constructor { get; }",
            typeSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            ".Invoke(",
            typeSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_module_configuration_stops_before_registry()
    {
        var source = File.ReadAllText(Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleConfigurator.cs"));
        var forbiddenTokens = new[]
        {
            "Assembly.GetType(",
            "GetTypes(",
            "DefinedTypes",
            "Type.GetType(",
            "GetConstructor(",
            "GetCustomAttribute",
            "CustomAttributeData",
            "Constructor.Invoke(",
            "Activator.",
            "CreateDelegate",
            "RuntimeHelpers.RunClassConstructor",
            "ActivateAsync(",
            "new ProjectCodePinnedModuleConstructor",
            "StaticEditorModuleRegistration",
            "StaticPackageGenerationHost",
            "new EditorModuleDefinition(",
            "EditorScopeTransaction",
            "EditorModuleRegistry",
            "EditorModuleHost",
            "AssemblyLoadContext",
            "LoadFrom",
            "MetadataLoadContext",
            "EnterContextualReflection",
            "Func<",
            "CancellationToken",
            "Task<",
            "File.",
            "Directory.",
        };

        Assert.Contains(
            "Configure(\n        ProjectCodePinnedModuleConstruction construction)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "var builder = new EditorModuleBuilder(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "new EditorModuleDefinitionContext(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "moduleObject.Module.Configure(builder)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "var declaration = builder.Build()",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "var metadata = new EditorModuleMetadata(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "retainedModules_.Add(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "result_ is not null",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ReferenceEquals(\n                        expectedModule.Module,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "project-code.pinned-module-configuration.configure-failed-restart-required",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_module_definitions_stop_before_registry()
    {
        var studioRoot = FindStudioRoot();
        var source = File.ReadAllText(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleDefinitionSet.cs"));
        var sharedDefinitionSource = File.ReadAllText(Path.Combine(
            studioRoot,
            "src",
            "Asharia.Studio.Application",
            "Extensions",
            "EditorModuleDefinition.cs"));
        var forbiddenTokens = new[]
        {
            "StaticEditorModuleRegistration",
            "StaticPackageGenerationHost",
            "Func<",
            "CreateDefinition",
            ".Configure(",
            ".Build(",
            "GetCustomAttribute",
            "Constructor.Invoke(",
            "Activator.",
            "RuntimeHelpers.RunClassConstructor",
            "ActivateAsync(",
            "EditorScopeTransaction",
            "EditorModuleRegistry",
            "EditorModuleHost",
            "ScopeInstanceId",
            "Commit(",
            "AssemblyLoadContext",
            "LoadFrom",
            "CancellationToken",
            "Task<",
            "File.",
            "Directory.",
        };

        Assert.Contains(
            "ProjectCodePinnedModuleConfiguration configuration",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            ".Select(module => new EditorModuleDefinition(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "module.Metadata,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "module.ModuleObject.Module,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "module.Declaration))",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "Configuration = configuration",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "DefinitionsById",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "definitionsById.TryAdd(definition.Id, definition)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "EditorModuleMetadata metadata,",
            sharedDefinitionSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "Id => Metadata.DefinitionId",
            sharedDefinitionSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "StaticEditorModuleRegistration",
            sharedDefinitionSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_pinned_module_scope_preparation_stops_before_commit()
    {
        var source = File.ReadAllText(Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleScopePreparer.cs"));
        var forbiddenTokens = new[]
        {
            "ProjectId",
            "ScopeInstanceId.ForProject",
            "ProjectSession(",
            "ProjectCodePinnedModuleConstructor",
            "ProjectCodePinnedModuleConfigurator",
            "StaticEditorModuleRegistration",
            "StaticPackageGenerationHost",
            "Func<",
            ".Configure(",
            ".Build(",
            "GetCustomAttribute",
            "Constructor.Invoke(",
            "Activator.",
            "RuntimeHelpers.RunClassConstructor",
            "ActivateAsync(",
            ".Commit(",
            "TryReserve",
            "Reservation",
            "OwnerToken",
            "Revision",
            "CommitObserver",
            "AssemblyLoadContext",
            "LoadFrom",
            "CancellationToken",
            "Task<",
            "File.",
            "Directory.",
        };

        Assert.Contains(
            "ProjectCodePinnedModuleDefinitionSet definitionSet,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ScopeInstanceId scopeInstanceId,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "EditorModuleRegistry registry,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "hostCapabilities?.ToArray() ?? []",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "EditorScopeTransaction.Prepare(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "definitionSet.Definitions,",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "new ProjectCodePinnedModuleScopePreparation(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "candidate.RegistrationOrder[index]",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "catch (EditorScopeValidationException error)",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "project-code.pinned-module-scope-preparation.validation-failed",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_initial_scope_commit_owns_only_exact_registration()
    {
        var source = File.ReadAllText(Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode",
            "ProjectCodePinnedModuleScopeCommitter.cs"));
        var forbiddenTokens = new[]
        {
            "ProjectId",
            "ScopeInstanceId.ForProject",
            "ProjectSession(",
            "EditorModuleHost",
            "ActivateScopeAsync",
            "ActivateAsync(",
            ".Commit(",
            "TryReserve",
            "Reservation",
            "OwnerToken",
            "Revision",
            "CommitObserver",
            "AssemblyLoadContext",
            "LoadFrom",
            "CancellationToken",
            "Task<",
            "File.",
            "Directory.",
            "Avalonia",
            "Unreal",
            "Unity",
            "Godot",
            "O3DE",
        };

        Assert.Contains(
            "ProjectCodePinnedModuleScopePreparation preparation",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "preparation.Transaction.TryCommitInitial(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "ReferenceEquals(",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "registration_.Dispose()",
            source,
            StringComparison.Ordinal);
        Assert.Contains(
            "project-code.pinned-module-scope-registration.conflict",
            source,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            forbiddenTokens,
            token => source.Contains(token, StringComparison.Ordinal));
    }

    [Fact]
    public void Project_code_implicit_workspace_does_not_execute_or_load_candidates()
    {
        var sourceRoot = Path.Combine(
            FindStudioRoot(),
            "src",
            "Asharia.Studio.Application",
            "ProjectCode");
        var forbiddenTokens = new[]
        {
            "ProcessStartInfo",
            "Process.Start",
            "Assembly.Load(",
            "AssemblyLoadContext",
            "DOTNET_ROOT",
            "GetEnvironmentVariable",
            "--restore",
            "dotnet build",
            "NuGet.Protocol",
            "Avalonia",
            "Runtime.InteropServices",
        };
        var offenders = Directory
            .EnumerateFiles(
                sourceRoot,
                "ProjectCodeImplicitSdkWorkspace*.cs",
                SearchOption.TopDirectoryOnly)
            .Select(path => new
            {
                Path = Path.GetRelativePath(sourceRoot, path),
                Text = File.ReadAllText(path),
            })
            .SelectMany(file => forbiddenTokens
                .Where(token => file.Text.Contains(
                    token,
                    StringComparison.Ordinal))
                .Select(token => $"{file.Path}: {token}"))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Public_editor_sources_do_not_reference_ui_native_or_studio_implementation()
    {
        var sourceRoot = Path.Combine(FindStudioRoot(), "src", "Asharia.Editor");
        var forbiddenTokens = new[]
        {
            "Avalonia",
            "LibraryImport",
            "DllImport",
            "System.Runtime.InteropServices",
            "Editor.Core",
            "Editor.Shell",
            "Editor.Features",
            "Asharia.Studio.",
            "Vulkan",
            "Func<object>",
            "GenerationScopedFactoryHandle",
            "PackageGenerationId",
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
    public void Public_panel_descriptor_exposes_only_declaration_time_contracts()
    {
        var descriptorType = typeof(Asharia.Editor.Panels.EditorPanelDescriptor);
        var properties = descriptorType.GetProperties();

        Assert.Equal("Asharia.Editor", descriptorType.Assembly.GetName().Name);
        Assert.DoesNotContain(properties, property => property.PropertyType == typeof(Type));
        Assert.DoesNotContain(properties, property => property.PropertyType == typeof(object));
        Assert.DoesNotContain(
            properties,
            property => typeof(Delegate).IsAssignableFrom(property.PropertyType));
        Assert.DoesNotContain(
            properties,
            property => property.PropertyType.Name.Contains(
                "GenerationScopedFactoryHandle",
                StringComparison.Ordinal));
        Assert.DoesNotContain(
            properties,
            property => property.Name.Contains("Scope", StringComparison.Ordinal));
    }

    [Fact]
    public void Code_first_source_is_owned_only_by_public_editor()
    {
        var studioRoot = FindStudioRoot();
        var legacyRoot = Path.Combine(studioRoot, "Core", "CodeFirstUI");
        var publicRoot = Path.Combine(studioRoot, "src", "Asharia.Editor", "UI", "CodeFirst");

        Assert.False(Directory.Exists(legacyRoot), $"Legacy Code-first source remains at {legacyRoot}.");
        Assert.True(Directory.Exists(publicRoot), $"Public Code-first source is missing at {publicRoot}.");

        var publicFiles = Directory
            .EnumerateFiles(publicRoot, "*.cs", SearchOption.AllDirectories)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.NotEmpty(publicFiles);

        var publicSource = string.Join(
            Environment.NewLine,
            publicFiles.Select(File.ReadAllText));

        Assert.DoesNotContain("Editor.Core", publicSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Avalonia", publicSource, StringComparison.Ordinal);
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

    private static string FindStudioRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln"))
                && File.Exists(Path.Combine(directory.FullName, "Editor.csproj")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln"))
                && File.Exists(Path.Combine(directory.FullName, "Editor.csproj")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate apps/studio from Editor.sln.");
    }
}
