using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using Xunit;

namespace Asharia.Studio.Architecture.Tests;

public sealed class ProjectReferenceGraphTests
{
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
    public void Legacy_editor_references_only_the_public_editor_project()
    {
        var projectPath = Path.Combine(FindStudioRoot(), "Editor.csproj");
        var project = XDocument.Load(projectPath);

        var references = project
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")?.Value.Replace('\\', '/'))
            .Where(value => value is not null)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(["src/Asharia.Editor/Asharia.Editor.csproj"], references);
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
                "Tests/Asharia.Studio.Architecture.Tests/Asharia.Studio.Architecture.Tests.csproj",
                "Tests/Editor.Tests/Editor.Tests.csproj",
                "src/Asharia.Editor/Asharia.Editor.csproj",
            ],
            projectPaths);
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
