using Asharia.Editor.Dialogs;
using Asharia.Editor.Diagnostics;
using Asharia.Editor.Lifecycle;
using Asharia.Editor.Selection;
using Asharia.Editor.Tasks;
using Asharia.Editor.Transactions;
using Asharia.Editor.Viewports;
using Asharia.Studio.Application.Lifecycle;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Tasks;
using Asharia.Studio.Application.Transactions;
using Asharia.Studio.Application.Viewports;
using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Xml.Linq;
using Xunit;

namespace Asharia.Studio.Architecture.Tests;

public sealed class ProjectReferenceGraphTests
{
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
    public void Legacy_editor_references_only_the_public_editor_and_application_projects()
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
                "Tests/Editor.Tests/Editor.Tests.csproj",
                "src/Asharia.Editor/Asharia.Editor.csproj",
                "src/Asharia.Studio.Application/Asharia.Studio.Application.csproj",
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
