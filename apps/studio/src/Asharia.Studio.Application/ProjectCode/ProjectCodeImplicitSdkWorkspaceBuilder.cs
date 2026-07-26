using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeImplicitSdkWorkspaceBuilder
{
    private const int CopyBufferSize = 1024 * 1024;
    private const int MaxSourceCount = 4096;
    private const int MaxTreeEntries = 32_768;
    private const long MaxSourceFileBytes = 16L * 1024 * 1024;
    private const long MaxSourceBytes = 256L * 1024 * 1024;
    private const string EditorDirectoryName = "Editor";
    private const string EntryProjectRelativePath = "project.csproj";
    private static readonly UTF8Encoding Utf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private static readonly HashSet<string> IgnoredDirectories = new(
        [
            ".asharia",
            ".git",
            ".generated",
            ".vs",
            "bin",
            "Cache",
            "obj",
            "Temp",
        ],
        StringComparer.OrdinalIgnoreCase);
    private static readonly StringComparer FileSystemPathComparer =
        OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
    private static readonly IComparer<string> Utf8Comparer =
        Comparer<string>.Create(CompareUtf8);

    public static async Task<ProjectCodeImplicitSdkWorkspaceResult> CreateAsync(
        ProjectCodeImplicitSdkWorkspaceRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(request.CredentialLease);
        var diagnostics = new List<ProjectCodeImplicitSdkWorkspaceDiagnostic>();
        if (request.ProjectId == Guid.Empty)
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.project-id-invalid",
                "projectId",
                "Implicit workspace project id must be one non-empty UUID."));
        }

        var projectRoot = ResolveExistingDirectory(
            request.ProjectRoot,
            "project-code.workspace.project-root-invalid",
            "projectRoot",
            diagnostics);
        var workspacePath = ResolveNewWorkspacePath(
            request.WorkspaceRoot,
            diagnostics);
        if (!request.CredentialLease.IsCurrent
            || !await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(
                    request.CredentialLease,
                    cancellationToken).ConfigureAwait(false))
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.credential-not-current",
                "credentialId",
                "Implicit workspace requires one current semantic build credential."));
        }

        if (projectRoot is not null && workspacePath is not null)
        {
            ValidateWorkspaceSeparation(
                projectRoot,
                workspacePath.Value.Root,
                request.CredentialLease.Credential,
                diagnostics);
        }

        if (projectRoot is null
            || workspacePath is null
            || diagnostics.Count != 0)
        {
            return ProjectCodeImplicitSdkWorkspaceResult.Failure(diagnostics);
        }

        var selection = await CaptureSourceSelectionAsync(
            projectRoot,
            diagnostics,
            cancellationToken).ConfigureAwait(false);
        if (selection is null)
        {
            return ProjectCodeImplicitSdkWorkspaceResult.Failure(diagnostics);
        }

        if (selection.Count == 0)
        {
            return ProjectCodeImplicitSdkWorkspaceResult.Empty();
        }

        return await MaterializeAsync(
            request,
            projectRoot,
            workspacePath.Value,
            selection,
            diagnostics,
            cancellationToken).ConfigureAwait(false);
    }

    public static async Task<bool> IsBuildInputCurrentAsync(
        ProjectCodeImplicitSdkWorkspaceLease lease,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(lease);
        if (!lease.IsCurrent
            || !await ProjectCodeBuildEnvironmentCredentialResolver
                .IsExecutionSelectionCurrentAsync(
                    lease.CredentialLease,
                    cancellationToken).ConfigureAwait(false))
        {
            return false;
        }

        var diagnostics = new List<ProjectCodeImplicitSdkWorkspaceDiagnostic>();
        var sources = await CaptureSourceSelectionAsync(
            lease.ProjectRoot,
            diagnostics,
            cancellationToken).ConfigureAwait(false);
        if (sources is null
            || !HasSameSources(lease.Workspace.Sources, sources))
        {
            return false;
        }

        var workspaceFiles = await SnapshotClosedWorkspaceAsync(
            lease.Workspace.AbsoluteRoot,
            cancellationToken).ConfigureAwait(false);
        return lease.IsCurrent
            && workspaceFiles is not null
            && HasSameWorkspaceFiles(
                lease.Workspace.Files,
                workspaceFiles);
    }

    private static async Task<ProjectCodeImplicitSdkWorkspaceResult>
        MaterializeAsync(
            ProjectCodeImplicitSdkWorkspaceRequest request,
            string projectRoot,
            WorkspacePath workspacePath,
            IReadOnlyList<ProjectCodeImplicitSourceSnapshot> sources,
            ICollection<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics,
            CancellationToken cancellationToken)
    {
        var candidate = Path.Combine(
            workspacePath.Parent,
            $".{Path.GetFileName(workspacePath.Root)}.candidate-{Guid.NewGuid():N}");
        var published = false;
        try
        {
            Directory.CreateDirectory(candidate);
            foreach (var source in sources)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var destination = Path.Combine(
                    candidate,
                    "input",
                    "project",
                    source.ProjectRelativePath.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                if (!await CopyVerifiedFileAsync(
                        source.AbsolutePath,
                        destination,
                        source.Size,
                        source.Sha256,
                        cancellationToken).ConfigureAwait(false))
                {
                    throw new MaterializationFailureException(
                        "project-code.workspace.source-changed",
                        source.ProjectRelativePath,
                        "Editor source changed before workspace publication.");
                }
            }

            var credential = request.CredentialLease.Credential;
            foreach (var contract in new[]
            {
                credential.RuntimeContract,
                credential.EditorContract,
            }.OrderBy(item => item.Identity.SimpleName, Utf8Comparer))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var destination = Path.Combine(
                    candidate,
                    "input",
                    "contracts",
                    contract.Identity.SimpleName + ".dll");
                if (!await CopyVerifiedFileAsync(
                        contract.AbsolutePath,
                        destination,
                        contract.Size,
                        contract.Sha256,
                        cancellationToken).ConfigureAwait(false))
                {
                    throw new MaterializationFailureException(
                        "project-code.workspace.contract-changed",
                        contract.Identity.SimpleName,
                        "Host contract changed before workspace publication.");
                }
            }

            var assemblyName = CreateAssemblyName(request.ProjectId);
            foreach (var rendered in RenderWorkspace(
                credential,
                request.ProjectId,
                assemblyName,
                sources))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var destination = Path.Combine(
                    candidate,
                    rendered.RelativePath.Replace(
                        '/',
                        Path.DirectorySeparatorChar));
                var parent = Path.GetDirectoryName(destination)
                    ?? throw new IOException();
                Directory.CreateDirectory(parent);
                await File.WriteAllBytesAsync(
                    destination,
                    rendered.Contents,
                    cancellationToken).ConfigureAwait(false);
            }

            var sourceCheck = await CaptureSourceSelectionAsync(
                projectRoot,
                diagnostics,
                cancellationToken).ConfigureAwait(false);
            if (sourceCheck is null || !HasSameSources(sources, sourceCheck))
            {
                throw new MaterializationFailureException(
                    "project-code.workspace.source-changed",
                    EditorDirectoryName,
                    "Editor source selection changed during workspace publication.");
            }

            if (!request.CredentialLease.IsCurrent
                || !await ProjectCodeBuildEnvironmentCredentialResolver
                    .IsExecutionSelectionCurrentAsync(
                        request.CredentialLease,
                        cancellationToken).ConfigureAwait(false))
            {
                throw new MaterializationFailureException(
                    "project-code.workspace.credential-not-current",
                    "credentialId",
                    "Semantic build credential changed during workspace publication.");
            }

            var candidateFiles = await SnapshotClosedWorkspaceAsync(
                candidate,
                cancellationToken).ConfigureAwait(false)
                ?? throw new IOException();
            var workspaceId = ComputeWorkspaceId(
                request.ProjectId,
                credential.CredentialId,
                assemblyName,
                candidateFiles);
            var finalFiles = candidateFiles
                .Select(file => new ProjectCodeImplicitWorkspaceFile(
                    file.RelativePath,
                    Path.Combine(
                        workspacePath.Root,
                        file.RelativePath.Replace(
                            '/',
                            Path.DirectorySeparatorChar)),
                    file.Size,
                    file.Sha256))
                .ToArray();
            var workspace = new ProjectCodeImplicitSdkWorkspace(
                workspaceId,
                request.ProjectId,
                assemblyName,
                credential.CredentialId,
                credential.SdkVersion,
                credential.TargetFramework,
                workspacePath.Root,
                EntryProjectRelativePath,
                $"out/{assemblyName}.dll",
                $"obj/ref/{assemblyName}.dll",
                $"out/{assemblyName}.pdb",
                $"out/{assemblyName}.deps.json",
                Array.AsReadOnly(sources.ToArray()),
                Array.AsReadOnly(finalFiles));
            var lease = new ProjectCodeImplicitSdkWorkspaceLease(
                request.CredentialLease,
                projectRoot,
                workspace);
            Directory.Move(candidate, workspacePath.Root);
            published = true;
            return ProjectCodeImplicitSdkWorkspaceResult.Success(lease);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (MaterializationFailureException error)
        {
            diagnostics.Add(Diagnostic(
                error.Code,
                error.Location,
                error.Message));
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.materialization-failed",
                "workspaceRoot",
                "Immutable SDK workspace could not be published."));
        }
        finally
        {
            if (!published
                && Directory.Exists(candidate)
                && !TryDeleteOwnedCandidate(
                    candidate,
                    workspacePath.Parent))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.workspace.cleanup-failed",
                    "workspaceRoot",
                    "Builder-owned workspace candidate could not be removed."));
            }
        }

        return ProjectCodeImplicitSdkWorkspaceResult.Failure(diagnostics);
    }

    private static async Task<IReadOnlyList<ProjectCodeImplicitSourceSnapshot>?>
        CaptureSourceSelectionAsync(
            string projectRoot,
            ICollection<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics,
            CancellationToken cancellationToken)
    {
        string? editorRoot = null;
        try
        {
            var matches = Directory.EnumerateFileSystemEntries(projectRoot)
                .Where(path => string.Equals(
                    Path.GetFileName(path),
                    EditorDirectoryName,
                    StringComparison.OrdinalIgnoreCase))
                .ToArray();
            if (matches.Length == 0)
            {
                return [];
            }

            if (matches.Length != 1
                || !string.Equals(
                    Path.GetFileName(matches[0]),
                    EditorDirectoryName,
                    StringComparison.Ordinal)
                || !Directory.Exists(matches[0])
                || HasReparsePointInPath(matches[0]))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.workspace.editor-root-invalid",
                    EditorDirectoryName,
                    "Project root Editor directory must use exact casing and contain no reparse point."));
                return null;
            }

            editorRoot = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(matches[0]));
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.editor-root-invalid",
                EditorDirectoryName,
                "Project root Editor directory could not be enumerated."));
            return null;
        }

        var paths = EnumerateSourcePaths(editorRoot, diagnostics);
        if (paths is null)
        {
            return null;
        }

        var snapshots = new List<ProjectCodeImplicitSourceSnapshot>(
            paths.Count);
        long totalBytes = 0;
        foreach (var path in paths)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var relative = Path.GetRelativePath(projectRoot, path)
                .Replace(Path.DirectorySeparatorChar, '/');
            var snapshot = await SnapshotStableFileAsync(
                relative,
                path,
                MaxSourceFileBytes,
                cancellationToken).ConfigureAwait(false);
            if (snapshot is null)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.workspace.source-unavailable",
                    relative,
                    "Editor source is not one stable bounded regular file."));
                return null;
            }

            totalBytes = checked(totalBytes + snapshot.Size);
            if (snapshots.Count == MaxSourceCount
                || totalBytes > MaxSourceBytes)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.workspace.source-budget-exceeded",
                    EditorDirectoryName,
                    "Implicit Editor source selection exceeds its file or byte budget."));
                return null;
            }

            snapshots.Add(snapshot);
        }

        if (snapshots
            .GroupBy(
                source => source.ProjectRelativePath,
                StringComparer.OrdinalIgnoreCase)
            .Any(group => group.Count() != 1))
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.source-path-alias",
                EditorDirectoryName,
                "Implicit Editor source paths must be unique ignoring case."));
            return null;
        }

        return snapshots;
    }

    private static IReadOnlyList<string>? EnumerateSourcePaths(
        string editorRoot,
        ICollection<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics)
    {
        try
        {
            var directories = new Stack<string>();
            directories.Push(editorRoot);
            var sources = new List<string>();
            var entryCount = 0;
            while (directories.TryPop(out var directory))
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(
                    directory))
                {
                    if (++entryCount > MaxTreeEntries)
                    {
                        diagnostics.Add(Diagnostic(
                            "project-code.workspace.tree-budget-exceeded",
                            EditorDirectoryName,
                            "Implicit Editor discovery exceeded its tree entry budget."));
                        return null;
                    }

                    var attributes = File.GetAttributes(entry);
                    var relative = Path.GetRelativePath(editorRoot, entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!IsPortableRelativePath(relative)
                        || (attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        diagnostics.Add(Diagnostic(
                            "project-code.workspace.entry-invalid",
                            EditorDirectoryName + "/" + relative,
                            "Editor discovery accepts only normalized regular entries without reparse points."));
                        return null;
                    }

                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        if (!IgnoredDirectories.Contains(
                                Path.GetFileName(entry)))
                        {
                            directories.Push(entry);
                        }

                        continue;
                    }

                    if (!File.Exists(entry))
                    {
                        diagnostics.Add(Diagnostic(
                            "project-code.workspace.entry-invalid",
                            EditorDirectoryName + "/" + relative,
                            "Editor discovery encountered one non-regular entry."));
                        return null;
                    }

                    var extension = Path.GetExtension(entry);
                    if (string.Equals(
                            extension,
                            ".asmdef",
                            StringComparison.OrdinalIgnoreCase))
                    {
                        diagnostics.Add(Diagnostic(
                            "project-code.workspace.asmdef-unsupported",
                            EditorDirectoryName + "/" + relative,
                            "Explicit asmdef graphs require their dedicated workspace contract."));
                        return null;
                    }

                    if (!string.Equals(
                            extension,
                            ".cs",
                            StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    if (!string.Equals(
                            extension,
                            ".cs",
                            StringComparison.Ordinal))
                    {
                        diagnostics.Add(Diagnostic(
                            "project-code.workspace.source-extension-case-mismatch",
                            EditorDirectoryName + "/" + relative,
                            "C# source extension must use exact lowercase .cs."));
                        return null;
                    }

                    if (!IsMsBuildLiteralSafe(
                            EditorDirectoryName + "/" + relative))
                    {
                        diagnostics.Add(Diagnostic(
                            "project-code.workspace.source-path-unrepresentable",
                            EditorDirectoryName + "/" + relative,
                            "C# source path contains characters unsupported by the deterministic MSBuild renderer."));
                        return null;
                    }

                    sources.Add(entry);
                }
            }

            return sources
                .OrderBy(
                    path => Path.GetRelativePath(editorRoot, path)
                        .Replace(Path.DirectorySeparatorChar, '/'),
                    Utf8Comparer)
                .ToArray();
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.editor-tree-unavailable",
                EditorDirectoryName,
                "Project root Editor tree could not be inspected."));
            return null;
        }
    }

    private static IReadOnlyList<RenderedFile> RenderWorkspace(
        ProjectCodeBuildEnvironmentCredential credential,
        Guid projectId,
        string assemblyName,
        IReadOnlyList<ProjectCodeImplicitSourceSnapshot> sources)
    {
        var files = new List<RenderedFile>
        {
            TextFile(
                "global.json",
                $$"""
                {
                  "sdk": {
                    "allowPrerelease": false,
                    "rollForward": "disable",
                    "version": "{{credential.SdkVersion}}"
                  }
                }
                """ + "\n"),
            TextFile(
                "Directory.Build.props",
                RenderBuildProps(credential.TargetFramework)),
            TextFile("Directory.Build.targets", "<Project />\n"),
            TextFile("Directory.Build.rsp", string.Empty),
            TextFile("Directory.Packages.props", "<Project />\n"),
            TextFile(
                "NuGet.Config",
                """
                <?xml version="1.0" encoding="utf-8"?>
                <configuration>
                  <packageSources>
                    <clear />
                  </packageSources>
                  <auditSources>
                    <clear />
                  </auditSources>
                </configuration>
                """ + "\n"),
            TextFile(
                "Generated/AssemblyInfo.cs",
                "// Generated by Asharia implicit SDK workspace renderer.\n"
                + "using System.Reflection;\n\n"
                + "[assembly: AssemblyVersion(\"1.0.0.0\")]\n"
                + "[assembly: AssemblyFileVersion(\"1.0.0.0\")]\n"),
            TextFile(
                EntryProjectRelativePath,
                RenderProject(
                    credential,
                    projectId,
                    assemblyName,
                    sources)),
        };
        return files
            .OrderBy(file => file.RelativePath, Utf8Comparer)
            .ToArray();
    }

    private static string RenderBuildProps(string targetFramework) =>
        $$"""
        <Project>
          <PropertyGroup>
            <OutputType>Library</OutputType>
            <TargetFramework>{{Xml(targetFramework)}}</TargetFramework>
            <LangVersion>14.0</LangVersion>
            <Nullable>enable</Nullable>
            <AllowUnsafeBlocks>false</AllowUnsafeBlocks>
            <ImplicitUsings>disable</ImplicitUsings>
            <GenerateAssemblyInfo>false</GenerateAssemblyInfo>
            <Deterministic>true</Deterministic>
            <ContinuousIntegrationBuild>true</ContinuousIntegrationBuild>
            <DebugType>portable</DebugType>
            <DebugSymbols>true</DebugSymbols>
            <EnableDynamicLoading>true</EnableDynamicLoading>
            <GenerateDependencyFile>true</GenerateDependencyFile>
            <EnableDefaultItems>false</EnableDefaultItems>
            <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
            <EnableDefaultEmbeddedResourceItems>false</EnableDefaultEmbeddedResourceItems>
            <EnableDefaultNoneItems>false</EnableDefaultNoneItems>
            <DiscoverEditorConfigFiles>false</DiscoverEditorConfigFiles>
            <DiscoverGlobalAnalyzerConfigFiles>false</DiscoverGlobalAnalyzerConfigFiles>
            <EnableNETAnalyzers>false</EnableNETAnalyzers>
            <RunAnalyzers>false</RunAnalyzers>
            <SuppressImplicitGitSourceLink>true</SuppressImplicitGitSourceLink>
            <UseAppHost>false</UseAppHost>
            <EnableTargetingPackDownload>false</EnableTargetingPackDownload>
            <EnableRuntimePackDownload>false</EnableRuntimePackDownload>
            <EnableAppHostPackDownload>false</EnableAppHostPackDownload>
            <DisableTransitiveFrameworkReferenceDownloads>true</DisableTransitiveFrameworkReferenceDownloads>
            <ImportDirectoryPackagesProps>false</ImportDirectoryPackagesProps>
            <MSBuildEnableWorkloadResolver>false</MSBuildEnableWorkloadResolver>
            <ImportUserLocationsByWildcardBeforeMicrosoftCommonProps>false</ImportUserLocationsByWildcardBeforeMicrosoftCommonProps>
            <ImportUserLocationsByWildcardAfterMicrosoftCommonProps>false</ImportUserLocationsByWildcardAfterMicrosoftCommonProps>
            <ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets>false</ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets>
            <ImportUserLocationsByWildcardAfterMicrosoftCommonTargets>false</ImportUserLocationsByWildcardAfterMicrosoftCommonTargets>
            <ImportUserLocationsByWildcardBeforeMicrosoftCSharpTargets>false</ImportUserLocationsByWildcardBeforeMicrosoftCSharpTargets>
            <ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets>false</ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets>
            <DirectoryBuildTargetsPath>$(MSBuildThisFileDirectory)Directory.Build.targets</DirectoryBuildTargetsPath>
            <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)obj/</BaseIntermediateOutputPath>
            <IntermediateOutputPath>$(BaseIntermediateOutputPath)</IntermediateOutputPath>
            <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</MSBuildProjectExtensionsPath>
            <OutputPath>$(MSBuildThisFileDirectory)out/</OutputPath>
            <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
            <AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>
            <PathMap>$(MSBuildThisFileDirectory)input/project=/_/Project,$(MSBuildThisFileDirectory)=/_/Build</PathMap>
            <NuGetAudit>false</NuGetAudit>
          </PropertyGroup>
        </Project>
        """ + "\n";

    private static string RenderProject(
        ProjectCodeBuildEnvironmentCredential credential,
        Guid projectId,
        string assemblyName,
        IReadOnlyList<ProjectCodeImplicitSourceSnapshot> sources)
    {
        var lines = new List<string>
        {
            "<Project Sdk=\"Microsoft.NET.Sdk\">",
            "  <PropertyGroup>",
            $"    <AssemblyName>{Xml(assemblyName)}</AssemblyName>",
            $"    <RootNamespace>{Xml(assemblyName)}</RootNamespace>",
            $"    <AshariaProjectId>{projectId:D}</AshariaProjectId>",
            $"    <AshariaBuildCredentialId>{Xml(credential.CredentialId)}</AshariaBuildCredentialId>",
            "    <ProduceReferenceAssembly>true</ProduceReferenceAssembly>",
            "  </PropertyGroup>",
            "  <ItemGroup>",
        };
        foreach (var source in sources)
        {
            var include = "input/project/" + source.ProjectRelativePath;
            lines.Add(
                $"    <Compile Include=\"{Xml(include)}\" Link=\"{Xml(source.ProjectRelativePath)}\" />");
        }

        lines.Add(
            "    <Compile Include=\"Generated/AssemblyInfo.cs\" Link=\"Generated/AssemblyInfo.cs\" />");
        lines.Add("  </ItemGroup>");
        lines.Add("  <ItemGroup>");
        foreach (var contract in new[]
        {
            credential.RuntimeContract,
            credential.EditorContract,
        }.OrderBy(item => item.Identity.SimpleName, Utf8Comparer))
        {
            lines.Add(
                $"    <Reference Include=\"{Xml(contract.Identity.FullName)}\">");
            lines.Add(
                $"      <HintPath>input/contracts/{Xml(contract.Identity.SimpleName)}.dll</HintPath>");
            lines.Add("      <Private>false</Private>");
            lines.Add("      <SpecificVersion>true</SpecificVersion>");
            lines.Add("    </Reference>");
        }

        lines.Add("  </ItemGroup>");
        lines.Add("</Project>");
        return string.Join('\n', lines) + "\n";
    }

    private static async Task<ProjectCodeImplicitSourceSnapshot?>
        SnapshotStableFileAsync(
            string relativePath,
            string absolutePath,
            long maxBytes,
            CancellationToken cancellationToken)
    {
        var snapshot = await HashStableFileAsync(
            absolutePath,
            maxBytes,
            cancellationToken).ConfigureAwait(false);
        return snapshot is null
            ? null
            : new ProjectCodeImplicitSourceSnapshot(
                relativePath,
                absolutePath,
                snapshot.Size,
                snapshot.Sha256);
    }

    private static async Task<FileHash?> HashStableFileAsync(
        string path,
        long maxBytes,
        CancellationToken cancellationToken)
    {
        try
        {
            if (HasReparsePointInPath(path))
            {
                return null;
            }

            var before = new FileInfo(path);
            var length = before.Length;
            var writeTime = before.LastWriteTimeUtc;
            if (length < 0 || length > maxBytes)
            {
                return null;
            }

            var buffer = ArrayPool<byte>.Shared.Rent(CopyBufferSize);
            try
            {
                using var hash = IncrementalHash.CreateHash(
                    HashAlgorithmName.SHA256);
                await using var stream = new FileStream(
                    path,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read,
                    CopyBufferSize,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                if (stream.Length != length)
                {
                    return null;
                }

                long total = 0;
                while (true)
                {
                    var read = await stream.ReadAsync(
                        buffer.AsMemory(0, buffer.Length),
                        cancellationToken).ConfigureAwait(false);
                    if (read == 0)
                    {
                        break;
                    }

                    if (read > length - total)
                    {
                        return null;
                    }

                    hash.AppendData(buffer, 0, read);
                    total = checked(total + read);
                }

                before.Refresh();
                if (!before.Exists
                    || total != length
                    || before.Length != length
                    || before.LastWriteTimeUtc != writeTime
                    || HasReparsePointInPath(path))
                {
                    return null;
                }

                return new FileHash(
                    length,
                    Convert.ToHexString(hash.GetHashAndReset())
                        .ToLowerInvariant());
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or OverflowException
                or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static async Task<bool> CopyVerifiedFileAsync(
        string source,
        string destination,
        long expectedSize,
        string expectedSha256,
        CancellationToken cancellationToken)
    {
        try
        {
            var parent = Path.GetDirectoryName(destination)
                ?? throw new IOException();
            Directory.CreateDirectory(parent);
            var buffer = ArrayPool<byte>.Shared.Rent(CopyBufferSize);
            try
            {
                using var hash = IncrementalHash.CreateHash(
                    HashAlgorithmName.SHA256);
                await using var input = new FileStream(
                    source,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read,
                    CopyBufferSize,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                await using var output = new FileStream(
                    destination,
                    FileMode.CreateNew,
                    FileAccess.Write,
                    FileShare.None,
                    CopyBufferSize,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                if (input.Length != expectedSize
                    || HasReparsePointInPath(source))
                {
                    return false;
                }

                long total = 0;
                while (true)
                {
                    var read = await input.ReadAsync(
                        buffer.AsMemory(0, buffer.Length),
                        cancellationToken).ConfigureAwait(false);
                    if (read == 0)
                    {
                        break;
                    }

                    if (read > expectedSize - total)
                    {
                        return false;
                    }

                    await output.WriteAsync(
                        buffer.AsMemory(0, read),
                        cancellationToken).ConfigureAwait(false);
                    hash.AppendData(buffer, 0, read);
                    total = checked(total + read);
                }

                await output.FlushAsync(cancellationToken).ConfigureAwait(false);
                return total == expectedSize
                    && string.Equals(
                        Convert.ToHexString(hash.GetHashAndReset())
                            .ToLowerInvariant(),
                        expectedSha256,
                        StringComparison.Ordinal);
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or OverflowException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static async Task<IReadOnlyList<ProjectCodeImplicitWorkspaceFile>?>
        SnapshotClosedWorkspaceAsync(
            string root,
            CancellationToken cancellationToken)
    {
        try
        {
            if (!Directory.Exists(root) || HasReparsePointInPath(root))
            {
                return null;
            }

            var directories = new Stack<string>();
            directories.Push(root);
            var paths = new List<string>();
            var entryCount = 0;
            while (directories.TryPop(out var directory))
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(
                    directory))
                {
                    if (++entryCount > MaxTreeEntries)
                    {
                        return null;
                    }

                    var attributes = File.GetAttributes(entry);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        return null;
                    }

                    var relative = Path.GetRelativePath(root, entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!IsPortableRelativePath(relative))
                    {
                        return null;
                    }

                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        directories.Push(entry);
                    }
                    else if (File.Exists(entry))
                    {
                        paths.Add(entry);
                    }
                    else
                    {
                        return null;
                    }
                }
            }

            var files = new List<ProjectCodeImplicitWorkspaceFile>(
                paths.Count);
            foreach (var path in paths.OrderBy(
                path => Path.GetRelativePath(root, path)
                    .Replace(Path.DirectorySeparatorChar, '/'),
                Utf8Comparer))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var relative = Path.GetRelativePath(root, path)
                    .Replace(Path.DirectorySeparatorChar, '/');
                var hash = await HashStableFileAsync(
                    path,
                    long.MaxValue,
                    cancellationToken).ConfigureAwait(false);
                if (hash is null)
                {
                    return null;
                }

                files.Add(new ProjectCodeImplicitWorkspaceFile(
                    relative,
                    path,
                    hash.Size,
                    hash.Sha256));
            }

            var actualClosure = EnumerateWorkspaceClosureEntries(root);
            var expectedClosure = files
                .SelectMany(file =>
                    EnumerateExpectedClosureEntries(file.RelativePath))
                .Distinct(StringComparer.Ordinal)
                .OrderBy(value => value, Utf8Comparer)
                .ToArray();
            return actualClosure.SequenceEqual(
                expectedClosure,
                StringComparer.Ordinal)
                    ? files
                    : null;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static string[] EnumerateWorkspaceClosureEntries(string root)
    {
        var directories = new Stack<string>();
        directories.Push(root);
        var entries = new List<string>();
        while (directories.TryPop(out var directory))
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(
                directory))
            {
                var attributes = File.GetAttributes(entry);
                var relative = Path.GetRelativePath(root, entry)
                    .Replace(Path.DirectorySeparatorChar, '/');
                if ((attributes & FileAttributes.Directory) != 0)
                {
                    entries.Add("d/" + relative);
                    directories.Push(entry);
                }
                else
                {
                    entries.Add("f/" + relative);
                }
            }
        }

        return entries.OrderBy(value => value, Utf8Comparer).ToArray();
    }

    private static IEnumerable<string> EnumerateExpectedClosureEntries(
        string filePath)
    {
        yield return "f/" + filePath;
        var separator = filePath.LastIndexOf('/');
        while (separator > 0)
        {
            filePath = filePath[..separator];
            yield return "d/" + filePath;
            separator = filePath.LastIndexOf('/');
        }
    }

    private static string ComputeWorkspaceId(
        Guid projectId,
        string credentialId,
        string assemblyName,
        IReadOnlyList<ProjectCodeImplicitWorkspaceFile> files)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, "project-code-implicit-sdk-workspace-v1");
        AppendString(hash, projectId.ToString("D").ToLowerInvariant());
        AppendString(hash, credentialId);
        AppendString(hash, assemblyName);
        Span<byte> size = stackalloc byte[sizeof(long)];
        foreach (var file in files)
        {
            AppendString(hash, file.RelativePath);
            BinaryPrimitives.WriteInt64LittleEndian(size, file.Size);
            hash.AppendData(size);
            hash.AppendData(Convert.FromHexString(file.Sha256));
        }

        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static string CreateAssemblyName(Guid projectId)
    {
        var projectIdBytes = Utf8.GetBytes(
            projectId.ToString("D").ToLowerInvariant());
        var hash = SHA256.HashData(projectIdBytes);
        return "Asharia.Project."
            + Convert.ToHexString(hash.AsSpan(0, 16)).ToLowerInvariant()
            + ".Editor";
    }

    private static string? ResolveExistingDirectory(
        string value,
        string code,
        string location,
        ICollection<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics)
    {
        try
        {
            if (!Path.IsPathFullyQualified(value))
            {
                throw new ArgumentException();
            }

            var resolved = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(value));
            if (!Directory.Exists(resolved)
                || HasReparsePointInPath(resolved))
            {
                throw new IOException();
            }

            return resolved;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                code,
                location,
                "Path must identify one existing absolute regular directory without reparse points."));
            return null;
        }
    }

    private static WorkspacePath? ResolveNewWorkspacePath(
        string value,
        ICollection<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics)
    {
        try
        {
            if (!Path.IsPathFullyQualified(value))
            {
                throw new ArgumentException();
            }

            var root = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(value));
            var parent = Path.GetDirectoryName(root);
            if (string.IsNullOrEmpty(parent)
                || Directory.Exists(root)
                || File.Exists(root)
                || !Directory.Exists(parent)
                || HasReparsePointInPath(parent)
                || !IsPathMapSafe(root))
            {
                throw new IOException();
            }

            return new WorkspacePath(root, parent);
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.path-invalid",
                "workspaceRoot",
                "Workspace root must be one new absolute path below an existing regular directory."));
            return null;
        }
    }

    private static void ValidateWorkspaceSeparation(
        string projectRoot,
        string workspaceRoot,
        ProjectCodeBuildEnvironmentCredential credential,
        ICollection<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics)
    {
        if (IsDescendantOrSame(projectRoot, workspaceRoot)
            || IsDescendantOrSame(workspaceRoot, projectRoot))
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.overlaps-project",
                "workspaceRoot",
                "Workspace and project roots must not contain one another."));
        }

        var protectedRoots = new[]
        {
            Path.GetDirectoryName(credential.DotnetExecutable),
            credential.Sdk.AbsoluteRoot,
            credential.HostFxr.AbsoluteRoot,
            credential.HostRuntime.AbsoluteRoot,
            credential.ReferencePack.AbsoluteRoot,
            Path.GetDirectoryName(credential.RuntimeContract.AbsolutePath),
            Path.GetDirectoryName(credential.EditorContract.AbsolutePath),
        };
        if (protectedRoots
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Select(value => Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(value!)))
            .Distinct(FileSystemPathComparer)
            .Any(root => IsDescendantOrSame(root, workspaceRoot)
                || IsDescendantOrSame(workspaceRoot, root)))
        {
            diagnostics.Add(Diagnostic(
                "project-code.workspace.overlaps-build-environment",
                "workspaceRoot",
                "Workspace must be disjoint from the semantic build environment."));
        }
    }

    private static bool HasSameSources(
        IReadOnlyList<ProjectCodeImplicitSourceSnapshot> expected,
        IReadOnlyList<ProjectCodeImplicitSourceSnapshot> actual) =>
        expected.Count == actual.Count
        && expected.Zip(actual).All(pair =>
            string.Equals(
                pair.First.ProjectRelativePath,
                pair.Second.ProjectRelativePath,
                StringComparison.Ordinal)
            && IsSamePath(
                pair.First.AbsolutePath,
                pair.Second.AbsolutePath)
            && pair.First.Size == pair.Second.Size
            && string.Equals(
                pair.First.Sha256,
                pair.Second.Sha256,
                StringComparison.Ordinal));

    private static bool HasSameWorkspaceFiles(
        IReadOnlyList<ProjectCodeImplicitWorkspaceFile> expected,
        IReadOnlyList<ProjectCodeImplicitWorkspaceFile> actual) =>
        expected.Count == actual.Count
        && expected.Zip(actual).All(pair =>
            string.Equals(
                pair.First.RelativePath,
                pair.Second.RelativePath,
                StringComparison.Ordinal)
            && IsSamePath(
                pair.First.AbsolutePath,
                pair.Second.AbsolutePath)
            && pair.First.Size == pair.Second.Size
            && string.Equals(
                pair.First.Sha256,
                pair.Second.Sha256,
                StringComparison.Ordinal));

    private static bool IsSamePath(string left, string right) =>
        FileSystemPathComparer.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(left)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(right)));

    private static bool IsDescendantOrSame(string root, string candidate)
    {
        var normalizedRoot = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(root));
        var normalizedCandidate = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(candidate));
        return FileSystemPathComparer.Equals(
                normalizedRoot,
                normalizedCandidate)
            || normalizedCandidate.StartsWith(
                normalizedRoot + Path.DirectorySeparatorChar,
                OperatingSystem.IsWindows()
                    ? StringComparison.OrdinalIgnoreCase
                    : StringComparison.Ordinal);
    }

    private static bool IsPortableRelativePath(string value) =>
        !string.IsNullOrWhiteSpace(value)
        && value.Length <= 500
        && value.IsNormalized(NormalizationForm.FormC)
        && !value.StartsWith("/", StringComparison.Ordinal)
        && !value.Contains('\\')
        && !value.Contains(':')
        && !value.Any(char.IsControl)
        && !value.Split('/').Any(part => part is "" or "." or "..");

    private static bool IsMsBuildLiteralSafe(string value) =>
        value.IndexOfAny(['$', '@', '%', ';', '*', '?']) < 0;

    private static bool IsPathMapSafe(string value) =>
        IsMsBuildLiteralSafe(value)
        && value.IndexOfAny([',', '=']) < 0;

    private static bool HasReparsePointInPath(string path)
    {
        var current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current))
        {
            if ((File.Exists(current) || Directory.Exists(current))
                && (File.GetAttributes(current)
                    & FileAttributes.ReparsePoint) != 0)
            {
                return true;
            }

            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrEmpty(parent)
                || FileSystemPathComparer.Equals(parent, current))
            {
                break;
            }

            current = parent;
        }

        return false;
    }

    private static string Xml(string value)
    {
        var builder = new StringBuilder();
        using var writer = XmlWriter.Create(
            builder,
            new XmlWriterSettings
            {
                ConformanceLevel = ConformanceLevel.Fragment,
                OmitXmlDeclaration = true,
            });
        writer.WriteString(value);
        writer.Flush();
        return builder.ToString();
    }

    private static RenderedFile TextFile(
        string relativePath,
        string contents) =>
        new(relativePath, Utf8.GetBytes(contents));

    private static void AppendString(
        IncrementalHash hash,
        string value)
    {
        var bytes = Utf8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }

    private static int CompareUtf8(string? left, string? right)
    {
        if (ReferenceEquals(left, right))
        {
            return 0;
        }

        if (left is null)
        {
            return -1;
        }

        if (right is null)
        {
            return 1;
        }

        return Encoding.UTF8.GetBytes(left)
            .AsSpan()
            .SequenceCompareTo(Encoding.UTF8.GetBytes(right));
    }

    private static bool TryDeleteOwnedCandidate(
        string candidate,
        string parent)
    {
        try
        {
            var resolvedCandidate = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(candidate));
            var resolvedParent = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(parent));
            if (!IsDescendantOrSame(resolvedParent, resolvedCandidate)
                || FileSystemPathComparer.Equals(
                    resolvedParent,
                    resolvedCandidate)
                || !Path.GetFileName(resolvedCandidate).Contains(
                    ".candidate-",
                    StringComparison.Ordinal))
            {
                return false;
            }

            Directory.Delete(resolvedCandidate, recursive: true);
            return true;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static ProjectCodeImplicitSdkWorkspaceDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    private readonly record struct WorkspacePath(
        string Root,
        string Parent);

    private sealed record FileHash(
        long Size,
        string Sha256);

    private sealed record RenderedFile(
        string RelativePath,
        byte[] Contents);

    private sealed class MaterializationFailureException(
        string code,
        string location,
        string message) : Exception(message)
    {
        public string Code { get; } = code;

        public string Location { get; } = location;
    }
}
