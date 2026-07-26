using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using System.Xml.Linq;
using Asharia.Studio.Application.ProjectCode;
using Xunit;
using SemanticEnvironmentFixture =
    Asharia.Studio.Application.Tests.ProjectCode.ProjectCodeBuildEnvironmentCredentialResolverTests.SemanticEnvironmentFixture;

namespace Asharia.Studio.Application.Tests.ProjectCode;

public sealed class ProjectCodeImplicitSdkWorkspaceBuilderTests
{
    private static readonly Guid ProjectId =
        Guid.Parse("5dce7138-bf1b-4451-92dc-5e2daf73391d");

    [Fact]
    public async Task Equivalent_roots_publish_identical_immutable_workspace()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var first = new ProjectFixture();
        using var second = new ProjectFixture();
        first.WriteEditorSource(
            "Zeta/Z.cs",
            "namespace Fixture; public sealed class Z {}\n");
        first.WriteEditorSource(
            "Alpha/A.cs",
            "namespace Fixture; public sealed class A {}\n");
        second.WriteEditorSource(
            "Alpha/A.cs",
            "namespace Fixture; public sealed class A {}\n");
        second.WriteEditorSource(
            "Zeta/Z.cs",
            "namespace Fixture; public sealed class Z {}\n");

        var firstResult = await CreateAsync(first, credential);
        var secondResult = await CreateAsync(second, credential);

        Assert.True(firstResult.Succeeded, Render(firstResult));
        Assert.True(secondResult.Succeeded, Render(secondResult));
        var firstWorkspace = firstResult.Lease!.Workspace;
        var secondWorkspace = secondResult.Lease!.Workspace;
        Assert.Equal(firstWorkspace.WorkspaceId, secondWorkspace.WorkspaceId);
        Assert.Equal(firstWorkspace.AssemblyName, secondWorkspace.AssemblyName);
        Assert.Matches(
            "^Asharia\\.Project\\.[0-9a-f]{32}\\.Editor$",
            firstWorkspace.AssemblyName);
        Assert.Equal(
            firstWorkspace.Files.Select(FileEnvelope),
            secondWorkspace.Files.Select(FileEnvelope));
        Assert.Equal(
            [
                "Editor/Alpha/A.cs",
                "Editor/Zeta/Z.cs",
            ],
            firstWorkspace.Sources
                .Select(source => source.ProjectRelativePath)
                .ToArray());
        Assert.DoesNotContain(
            first.ProjectRoot,
            ReadGeneratedText(firstWorkspace),
            StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain(
            first.WorkspaceRoot,
            ReadGeneratedText(firstWorkspace),
            StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain(
            credential.Credential.RuntimeContract.AbsolutePath,
            ReadGeneratedText(firstWorkspace),
            StringComparison.OrdinalIgnoreCase);
        Assert.True(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(firstResult.Lease));
        Assert.True(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(secondResult.Lease));
    }

    [Fact]
    public async Task Renderer_pins_sdk_contracts_and_msbuild_barriers()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource(
            "Tools/Module.cs",
            "namespace Fixture; public sealed class Module {}\n");

        var result = await CreateAsync(project, credential);

        Assert.True(result.Succeeded, Render(result));
        var workspace = result.Lease!.Workspace;
        using var global = JsonDocument.Parse(
            File.ReadAllBytes(Path.Combine(
                workspace.AbsoluteRoot,
                "global.json")));
        var sdk = global.RootElement.GetProperty("sdk");
        Assert.Equal(workspace.SdkVersion, sdk.GetProperty("version").GetString());
        Assert.Equal("disable", sdk.GetProperty("rollForward").GetString());
        Assert.False(sdk.GetProperty("allowPrerelease").GetBoolean());
        var props = XDocument.Load(Path.Combine(
            workspace.AbsoluteRoot,
            "Directory.Build.props"));
        AssertProperty(props, "TargetFramework", workspace.TargetFramework);
        AssertProperty(props, "LangVersion", "14.0");
        AssertProperty(props, "Deterministic", "true");
        AssertProperty(props, "ContinuousIntegrationBuild", "true");
        AssertProperty(props, "EnableDefaultItems", "false");
        AssertProperty(props, "MSBuildEnableWorkloadResolver", "false");
        AssertProperty(
            props,
            "ImportUserLocationsByWildcardBeforeMicrosoftCommonProps",
            "false");
        AssertProperty(
            props,
            "ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets",
            "false");
        Assert.Equal(
            "<Project />\n",
            File.ReadAllText(Path.Combine(
                workspace.AbsoluteRoot,
                "Directory.Build.targets")));
        Assert.Empty(File.ReadAllBytes(Path.Combine(
            workspace.AbsoluteRoot,
            "Directory.Build.rsp")));
        var nuget = XDocument.Load(Path.Combine(
            workspace.AbsoluteRoot,
            "NuGet.Config"));
        Assert.NotNull(nuget.Root?
            .Element("packageSources")?
            .Element("clear"));
        Assert.NotNull(nuget.Root?
            .Element("auditSources")?
            .Element("clear"));
        var projectText = File.ReadAllText(Path.Combine(
            workspace.AbsoluteRoot,
            workspace.EntryProjectRelativePath));
        Assert.Contains(
            $"<AssemblyName>{workspace.AssemblyName}</AssemblyName>",
            projectText,
            StringComparison.Ordinal);
        Assert.Contains(
            "input/project/Editor/Tools/Module.cs",
            projectText,
            StringComparison.Ordinal);
        Assert.Contains(
            credential.Credential.RuntimeContract.Identity.FullName,
            projectText,
            StringComparison.Ordinal);
        Assert.Contains(
            credential.Credential.EditorContract.Identity.FullName,
            projectText,
            StringComparison.Ordinal);
        Assert.Equal(
            $"out/{workspace.AssemblyName}.dll",
            workspace.OutputAssemblyRelativePath);
        Assert.Equal(
            $"obj/ref/{workspace.AssemblyName}.dll",
            workspace.ReferenceAssemblyRelativePath);
        Assert.Equal(
            $"out/{workspace.AssemblyName}.pdb",
            workspace.PortablePdbRelativePath);
        Assert.Equal(
            $"out/{workspace.AssemblyName}.deps.json",
            workspace.DependencyFileRelativePath);
        Assert.DoesNotContain(
            "<PackageReference",
            projectText,
            StringComparison.Ordinal);
        AssertContractCopy(
            workspace,
            credential.Credential.RuntimeContract);
        AssertContractCopy(
            workspace,
            credential.Credential.EditorContract);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Empty_editor_source_set_does_not_publish_workspace(
        bool createEditorDirectory)
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        if (createEditorDirectory)
        {
            project.WriteEditorFile("README.md", "not a compile input\n");
        }

        var result = await CreateAsync(project, credential);

        Assert.True(result.Succeeded, Render(result));
        Assert.False(result.RequiresBuild);
        Assert.Null(result.Lease);
        Assert.False(Directory.Exists(project.WorkspaceRoot));
    }

    [Fact]
    public async Task Discovery_uses_only_exact_project_root_editor_sources()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Root.cs", "public sealed class Root {}\n");
        project.WriteEditorSource(
            "Nested/Child.cs",
            "public sealed class Child {}\n");
        project.WriteProjectFile(
            "Assets/Editor/Outside.cs",
            "public sealed class Outside {}\n");
        project.WriteEditorFile(
            "obj/Ignored.cs",
            "public sealed class Ignored {}\n");

        var result = await CreateAsync(project, credential);

        Assert.True(result.Succeeded, Render(result));
        Assert.Equal(
            [
                "Editor/Nested/Child.cs",
                "Editor/Root.cs",
            ],
            result.Lease!.Workspace.Sources
                .Select(source => source.ProjectRelativePath)
                .ToArray());
        Assert.False(File.Exists(Path.Combine(
            result.Lease.Workspace.AbsoluteRoot,
            "input",
            "project",
            "Assets",
            "Editor",
            "Outside.cs")));
        Assert.False(File.Exists(Path.Combine(
            result.Lease.Workspace.AbsoluteRoot,
            "input",
            "project",
            "Editor",
            "obj",
            "Ignored.cs")));
    }

    [Theory]
    [InlineData(
        "Feature.asmdef",
        "project-code.workspace.asmdef-unsupported")]
    [InlineData(
        "Feature.CS",
        "project-code.workspace.source-extension-case-mismatch")]
    [InlineData(
        "Bad;Injected.cs",
        "project-code.workspace.source-path-unrepresentable")]
    public async Task Unsupported_implicit_authoring_fails_before_publication(
        string relativePath,
        string expectedCode)
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Valid.cs", "public sealed class Valid {}\n");
        project.WriteEditorFile(relativePath, "{}\n");

        var result = await CreateAsync(project, credential);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == expectedCode);
        Assert.False(Directory.Exists(project.WorkspaceRoot));
    }

    [Fact]
    public async Task Oversized_source_fails_before_publication()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteSizedEditorSource(
            "Oversized.cs",
            (16L * 1024 * 1024) + 1);

        var result = await CreateAsync(project, credential);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.workspace.source-unavailable");
        Assert.False(Directory.Exists(project.WorkspaceRoot));
    }

    [Fact]
    public async Task Reparse_source_fails_before_publication_when_supported()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        if (!project.TryWriteEditorSourceSymbolicLink("Linked.cs"))
        {
            return;
        }

        var result = await CreateAsync(project, credential);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.workspace.entry-invalid");
        Assert.False(Directory.Exists(project.WorkspaceRoot));
    }

    [Fact]
    public async Task Case_alias_sources_fail_when_supported_by_file_system()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Alias.cs", "public sealed class First {}\n");
        project.WriteEditorSource("alias.cs", "public sealed class Second {}\n");
        if (project.EditorSourceCount != 2)
        {
            return;
        }

        var result = await CreateAsync(project, credential);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.workspace.source-path-alias");
        Assert.False(Directory.Exists(project.WorkspaceRoot));
    }

    [Fact]
    public async Task Materialization_failure_does_not_publish_partial_workspace()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Valid.cs", "public sealed class Valid {}\n");
        var workspaceRoot = Path.Combine(
            Path.GetDirectoryName(project.WorkspaceRoot)!,
            new string('w', 220));

        var result = await ProjectCodeImplicitSdkWorkspaceBuilder.CreateAsync(
            new ProjectCodeImplicitSdkWorkspaceRequest(
                project.ProjectRoot,
                ProjectId,
                workspaceRoot,
                credential));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.workspace.materialization-failed");
        Assert.False(Directory.Exists(workspaceRoot));
        Assert.DoesNotContain(
            Directory.EnumerateFileSystemEntries(
                Path.GetDirectoryName(workspaceRoot)!),
            path => Path.GetFileName(path).Contains(
                ".candidate-",
                StringComparison.Ordinal));
    }

    [Fact]
    public async Task Workspace_path_unrepresentable_by_pathmap_is_rejected()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Valid.cs", "public sealed class Valid {}\n");
        var workspaceRoot = Path.Combine(
            Path.GetDirectoryName(project.WorkspaceRoot)!,
            "bad,workspace");

        var result = await ProjectCodeImplicitSdkWorkspaceBuilder.CreateAsync(
            new ProjectCodeImplicitSdkWorkspaceRequest(
                project.ProjectRoot,
                ProjectId,
                workspaceRoot,
                credential));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.workspace.path-invalid");
        Assert.False(Directory.Exists(workspaceRoot));
    }

    [Fact]
    public async Task Existing_workspace_is_never_overwritten()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Valid.cs", "public sealed class Valid {}\n");
        Directory.CreateDirectory(project.WorkspaceRoot);
        var marker = Path.Combine(project.WorkspaceRoot, "owned.txt");
        File.WriteAllText(marker, "preserve");

        var result = await CreateAsync(project, credential);

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code
                == "project-code.workspace.path-invalid");
        Assert.Equal("preserve", File.ReadAllText(marker));
    }

    [Theory]
    [InlineData("project")]
    [InlineData("environment")]
    public async Task Workspace_overlap_is_rejected(string overlap)
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Valid.cs", "public sealed class Valid {}\n");
        var workspaceRoot = overlap == "project"
            ? Path.Combine(project.ProjectRoot, ".asharia", "workspace")
            : Path.Combine(
                credential.Credential.Sdk.AbsoluteRoot,
                "workspace");
        Directory.CreateDirectory(Path.GetDirectoryName(workspaceRoot)!);

        var result = await ProjectCodeImplicitSdkWorkspaceBuilder.CreateAsync(
            new ProjectCodeImplicitSdkWorkspaceRequest(
                project.ProjectRoot,
                ProjectId,
                workspaceRoot,
                credential));

        Assert.False(result.Succeeded);
        Assert.Contains(
            result.Diagnostics,
            diagnostic => diagnostic.Code == (overlap == "project"
                ? "project-code.workspace.overlaps-project"
                : "project-code.workspace.overlaps-build-environment"));
        Assert.False(Directory.Exists(workspaceRoot));
    }

    [Fact]
    public async Task Current_check_detects_source_workspace_and_credential_drift()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var sourceProject = new ProjectFixture();
        sourceProject.WriteEditorSource(
            "Source.cs",
            "public sealed class Source {}\n");
        var sourceResult = await CreateAsync(sourceProject, credential);
        Assert.True(sourceResult.Succeeded, Render(sourceResult));
        sourceProject.WriteEditorSource(
            "Source.cs",
            "public sealed class Changed {}\n");
        Assert.False(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(sourceResult.Lease!));

        using var workspaceProject = new ProjectFixture();
        workspaceProject.WriteEditorSource(
            "Workspace.cs",
            "public sealed class Workspace {}\n");
        var workspaceResult = await CreateAsync(workspaceProject, credential);
        Assert.True(workspaceResult.Succeeded, Render(workspaceResult));
        File.AppendAllText(
            Path.Combine(
                workspaceResult.Lease!.Workspace.AbsoluteRoot,
                "project.csproj"),
            "drift");
        Assert.False(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(workspaceResult.Lease));

        using var closureProject = new ProjectFixture();
        closureProject.WriteEditorSource(
            "Closure.cs",
            "public sealed class Closure {}\n");
        var closureResult = await CreateAsync(closureProject, credential);
        Assert.True(closureResult.Succeeded, Render(closureResult));
        Directory.CreateDirectory(Path.Combine(
            closureResult.Lease!.Workspace.AbsoluteRoot,
            "rogue-empty"));
        Assert.False(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(closureResult.Lease));

        using var credentialProject = new ProjectFixture();
        credentialProject.WriteEditorSource(
            "Credential.cs",
            "public sealed class Credential {}\n");
        var credentialResult = await CreateAsync(
            credentialProject,
            credential);
        Assert.True(credentialResult.Succeeded, Render(credentialResult));
        credential.Revoke();
        Assert.False(credentialResult.Lease!.IsCurrent);
        Assert.False(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(credentialResult.Lease));
    }

    [Fact]
    public async Task Derived_revocation_does_not_revoke_source_credential()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var project = new ProjectFixture();
        project.WriteEditorSource("Valid.cs", "public sealed class Valid {}\n");
        var result = await CreateAsync(project, credential);
        Assert.True(result.Succeeded, Render(result));

        result.Lease!.Revoke();

        Assert.False(result.Lease.IsCurrent);
        Assert.True(credential.IsCurrent);
        Assert.False(
            await ProjectCodeImplicitSdkWorkspaceBuilder
                .IsBuildInputCurrentAsync(result.Lease));
    }

    [Fact]
    public async Task Project_id_changes_logical_identity()
    {
        using var environment = new SemanticEnvironmentFixture();
        var credential = await CreateCredentialAsync(environment);
        using var first = new ProjectFixture();
        using var second = new ProjectFixture();
        first.WriteEditorSource("Same.cs", "public sealed class Same {}\n");
        second.WriteEditorSource("Same.cs", "public sealed class Same {}\n");

        var firstResult = await CreateAsync(first, credential, ProjectId);
        var secondResult = await CreateAsync(
            second,
            credential,
            Guid.Parse("c375502e-c353-4248-a9da-9687ab7868da"));

        Assert.True(firstResult.Succeeded, Render(firstResult));
        Assert.True(secondResult.Succeeded, Render(secondResult));
        Assert.NotEqual(
            firstResult.Lease!.Workspace.AssemblyName,
            secondResult.Lease!.Workspace.AssemblyName);
        Assert.NotEqual(
            firstResult.Lease.Workspace.WorkspaceId,
            secondResult.Lease.Workspace.WorkspaceId);
    }

    private static async Task<ProjectCodeBuildEnvironmentCredentialLease>
        CreateCredentialAsync(SemanticEnvironmentFixture environment)
    {
        var managed = await environment.LoadManagedLeaseAsync();
        var result =
            await ProjectCodeBuildEnvironmentCredentialResolver.ResolveAsync(
                managed);
        Assert.True(
            result.Succeeded,
            string.Join(
                Environment.NewLine,
                result.Diagnostics.Select(diagnostic =>
                    $"{diagnostic.Code}: {diagnostic.Message}")));
        return result.Lease!;
    }

    private static Task<ProjectCodeImplicitSdkWorkspaceResult> CreateAsync(
        ProjectFixture project,
        ProjectCodeBuildEnvironmentCredentialLease credential,
        Guid? projectId = null) =>
        ProjectCodeImplicitSdkWorkspaceBuilder.CreateAsync(
            new ProjectCodeImplicitSdkWorkspaceRequest(
                project.ProjectRoot,
                projectId ?? ProjectId,
                project.WorkspaceRoot,
                credential));

    private static string FileEnvelope(
        ProjectCodeImplicitWorkspaceFile file) =>
        $"{file.RelativePath}|{file.Size}|{file.Sha256}";

    private static string ReadGeneratedText(
        ProjectCodeImplicitSdkWorkspace workspace) =>
        string.Join(
            "\n",
            workspace.Files
                .Where(file => file.RelativePath is
                    "global.json"
                    or "Directory.Build.props"
                    or "Directory.Build.targets"
                    or "Directory.Build.rsp"
                    or "Directory.Packages.props"
                    or "NuGet.Config"
                    or "Generated/AssemblyInfo.cs"
                    or "project.csproj")
                .Select(file => File.ReadAllText(file.AbsolutePath)));

    private static void AssertProperty(
        XDocument document,
        string name,
        string expected)
    {
        var value = document.Descendants()
            .Single(element => element.Name.LocalName == name)
            .Value;
        Assert.Equal(expected, value);
    }

    private static void AssertContractCopy(
        ProjectCodeImplicitSdkWorkspace workspace,
        ProjectCodeContractFileBinding contract)
    {
        var copied = Assert.Single(
            workspace.Files,
            file => file.RelativePath
                == $"input/contracts/{contract.Identity.SimpleName}.dll");
        Assert.Equal(contract.Size, copied.Size);
        Assert.Equal(contract.Sha256, copied.Sha256);
    }

    private static string Render(
        ProjectCodeImplicitSdkWorkspaceResult result) =>
        string.Join(
            Environment.NewLine,
            result.Diagnostics.Select(diagnostic =>
                $"{diagnostic.Code} {diagnostic.Location}: {diagnostic.Message}"));

    private sealed class ProjectFixture : IDisposable
    {
        private readonly string root_;

        public ProjectFixture()
        {
            root_ = Path.Combine(
                Path.GetTempPath(),
                "asharia-implicit-workspace-tests",
                Guid.NewGuid().ToString("N"));
            ProjectRoot = Path.Combine(root_, "project");
            WorkspaceRoot = Path.Combine(root_, "workspaces", "candidate");
            Directory.CreateDirectory(ProjectRoot);
            Directory.CreateDirectory(Path.GetDirectoryName(WorkspaceRoot)!);
        }

        public string ProjectRoot { get; }

        public string WorkspaceRoot { get; }

        public int EditorSourceCount
        {
            get
            {
                var editorRoot = Path.Combine(ProjectRoot, "Editor");
                return Directory.Exists(editorRoot)
                    ? Directory.EnumerateFiles(
                        editorRoot,
                        "*",
                        SearchOption.AllDirectories).Count()
                    : 0;
            }
        }

        public void WriteEditorSource(
            string relativePath,
            string contents) =>
            WriteEditorFile(relativePath, contents);

        public void WriteSizedEditorSource(
            string relativePath,
            long size)
        {
            var path = EditorPath(relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var stream = new FileStream(
                path,
                FileMode.CreateNew,
                FileAccess.Write,
                FileShare.None);
            stream.SetLength(size);
        }

        public bool TryWriteEditorSourceSymbolicLink(string relativePath)
        {
            var target = Path.Combine(root_, "external-link-target.cs");
            File.WriteAllText(
                target,
                "public sealed class External {}\n",
                new UTF8Encoding(false));
            var link = EditorPath(relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(link)!);
            try
            {
                File.CreateSymbolicLink(link, target);
                return true;
            }
            catch (Exception error) when (
                error is IOException
                    or PlatformNotSupportedException
                    or UnauthorizedAccessException)
            {
                return false;
            }
        }

        public void WriteEditorFile(
            string relativePath,
            string contents) =>
            WriteProjectFile(
                "Editor/" + relativePath.Replace('\\', '/'),
                contents);

        public void WriteProjectFile(
            string projectRelativePath,
            string contents)
        {
            var path = Path.Combine(
                ProjectRoot,
                projectRelativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, contents, new UTF8Encoding(false));
        }

        private string EditorPath(string relativePath) =>
            Path.Combine(
                ProjectRoot,
                "Editor",
                relativePath.Replace(
                    '/',
                    Path.DirectorySeparatorChar));

        public void Dispose()
        {
            if (Directory.Exists(root_))
            {
                Directory.Delete(root_, recursive: true);
            }
        }
    }
}
