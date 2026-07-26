using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeArtifactPublisher
{
    private const int CopyBufferSize = 1024 * 1024;
    private const string ManifestPath = "artifact.json";
    private const string Schema =
        "com.asharia.project-code-artifact-publication";
    private static readonly UTF8Encoding Utf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    public static async Task<ProjectCodeArtifactPublicationResult> PublishAsync(
        ProjectCodeRawBuildOutputLease lease,
        string outputRoot,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(lease);
        cancellationToken.ThrowIfCancellationRequested();

        var pathResult = ResolveOutputPath(lease, outputRoot);
        if (pathResult.Diagnostic is not null)
        {
            return ProjectCodeArtifactPublicationResult.Failure(
                [pathResult.Diagnostic]);
        }

        var outputPath = pathResult.Path!;
        var inspection = await ProjectCodeArtifactInspector
            .InspectAsync(lease, cancellationToken)
            .ConfigureAwait(false);
        if (!inspection.Succeeded)
        {
            return ProjectCodeArtifactPublicationResult.Failure(
                inspection.Diagnostics.Select(diagnostic =>
                    new ProjectCodeArtifactPublicationDiagnostic(
                        diagnostic.Code,
                        diagnostic.Location,
                        diagnostic.Message)));
        }

        var report = inspection.Report!;
        var stage = Path.Combine(
            outputPath.Parent,
            $".{outputPath.Leaf}.candidate-{Guid.NewGuid():N}");
        var stageCreated = false;
        var finalCreated = false;
        try
        {
            var publishedFiles = CreatePublishedFiles(report);
            var publicationId = ComputePublicationId(
                report,
                publishedFiles);
            var manifestBytes = RenderManifest(
                publicationId,
                report,
                publishedFiles);
            var manifest = new ProjectCodeArtifactFileEvidence(
                ManifestPath,
                manifestBytes.LongLength,
                Hash(manifestBytes));
            cancellationToken.ThrowIfCancellationRequested();
            if (Directory.Exists(outputPath.Root)
                || File.Exists(outputPath.Root)
                || Directory.Exists(stage)
                || File.Exists(stage)
                || HasReparsePointInPath(outputPath.Parent))
            {
                return Failure(
                    "project-code.artifact-publication.output-path-changed",
                    "outputRoot",
                    "Publication output path changed before staging began.");
            }

            Directory.CreateDirectory(stage);
            stageCreated = true;
            var sources = lease.Output.Files.ToDictionary(
                file => file.RelativePath,
                StringComparer.Ordinal);
            foreach (var file in publishedFiles)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var source = sources[file.SourceRelativePath];
                var destination = CombinePortable(
                    stage,
                    file.Published.RelativePath);
                if (!await CopyVerifiedFileAsync(
                        source.AbsolutePath,
                        destination,
                        file.Published,
                        cancellationToken).ConfigureAwait(false))
                {
                    return Failure(
                        "project-code.artifact-publication.source-changed",
                        file.Location,
                        "Inspected artifact bytes changed while they were copied.");
                }
            }

            var manifestAbsolutePath = CombinePortable(
                stage,
                ManifestPath);
            await WriteNewFileAsync(
                manifestAbsolutePath,
                manifestBytes,
                cancellationToken).ConfigureAwait(false);
            var expected = publishedFiles
                .Select(file => file.Published)
                .Append(manifest)
                .ToArray();
            if (!await VerifyClosedTreeAsync(
                    stage,
                    expected,
                    manifestBytes,
                    cancellationToken).ConfigureAwait(false))
            {
                return Failure(
                    "project-code.artifact-publication.staging-changed",
                    "staging",
                    "Publication staging tree was not the expected closed five-file tree.");
            }

            if (!await ProjectCodeSdkBuildController
                    .IsRawOutputCurrentAsync(lease, cancellationToken)
                    .ConfigureAwait(false))
            {
                return Failure(
                    "project-code.artifact-publication.raw-output-changed",
                    "raw-output",
                    "Raw SDK build output changed before publication commit.");
            }

            cancellationToken.ThrowIfCancellationRequested();
            if (Directory.Exists(outputPath.Root)
                || File.Exists(outputPath.Root)
                || HasReparsePointInPath(outputPath.Parent)
                || HasReparsePointInPath(stage))
            {
                return Failure(
                    "project-code.artifact-publication.output-path-changed",
                    "outputRoot",
                    "Publication output path changed before commit.");
            }

            Directory.Move(stage, outputPath.Root);
            stageCreated = false;
            finalCreated = true;
            if (!await ProjectCodeSdkBuildController
                    .IsRawOutputCurrentAsync(lease, cancellationToken)
                    .ConfigureAwait(false))
            {
                return Failure(
                    "project-code.artifact-publication.raw-output-changed",
                    "raw-output",
                    "Raw SDK build output changed during publication commit.");
            }

            var receipt = new ProjectCodeArtifactPublicationReceipt(
                publicationId,
                outputPath.Root,
                report,
                manifest,
                publishedFiles.Single(file =>
                    file.Location == "implementation").Published,
                publishedFiles.Single(file =>
                    file.Location == "reference-assembly").Published,
                publishedFiles.Single(file =>
                    file.Location == "portable-pdb").Published,
                publishedFiles.Single(file =>
                    file.Location == "dependencies").Published);
            finalCreated = false;
            return ProjectCodeArtifactPublicationResult.Success(receipt);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or OverflowException
                or UnauthorizedAccessException)
        {
            return Failure(
                "project-code.artifact-publication.io-failed",
                "publication",
                "Artifact publication failed without committing a receipt.");
        }
        finally
        {
            if (stageCreated)
            {
                TryDeleteOwnedTree(
                    stage,
                    outputPath.Parent,
                    ".candidate-");
            }

            if (finalCreated)
            {
                TryDeleteOwnedTree(
                    outputPath.Root,
                    outputPath.Parent,
                    outputPath.Leaf);
            }
        }
    }

    private static OutputPathResult ResolveOutputPath(
        ProjectCodeRawBuildOutputLease lease,
        string outputRoot)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(outputRoot)
                || !Path.IsPathFullyQualified(outputRoot))
            {
                throw new ArgumentException();
            }

            var root = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(outputRoot));
            var parent = Path.GetDirectoryName(root);
            var leaf = Path.GetFileName(root);
            if (string.IsNullOrEmpty(parent)
                || string.IsNullOrWhiteSpace(leaf)
                || leaf.Length > 100
                || leaf.IndexOfAny(
                    ['$', '@', '%', ';', '*', '?', ',', '=']) >= 0
                || Directory.Exists(root)
                || File.Exists(root)
                || !Directory.Exists(parent)
                || HasReparsePointInPath(parent))
            {
                throw new IOException();
            }

            ValidateOutputSeparation(lease, root);
            return new(new OutputPath(root, parent, leaf), null);
        }
        catch (PublicationPathException error)
        {
            return new(null, error.Diagnostic);
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return new(
                null,
                Diagnostic(
                    "project-code.artifact-publication.output-path-invalid",
                    "outputRoot",
                    "Publication root must be one new absolute regular path with a safe bounded leaf."));
        }
    }

    private static void ValidateOutputSeparation(
        ProjectCodeRawBuildOutputLease lease,
        string outputRoot)
    {
        var workspaceLease = lease.WorkspaceLease;
        var credential = workspaceLease.CredentialLease.Credential;
        var protectedRoots = new[]
        {
            workspaceLease.ProjectRoot,
            workspaceLease.Workspace.AbsoluteRoot,
            lease.Output.AbsoluteRoot,
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
            .Any(root => Overlaps(root!, outputRoot)))
        {
            throw new PublicationPathException(
                Diagnostic(
                    "project-code.artifact-publication.output-overlap",
                    "outputRoot",
                    "Publication root must be disjoint from project, workspace, raw output, and credential roots."));
        }
    }

    private static PublishedFile[] CreatePublishedFiles(
        ProjectCodeArtifactMetadataReport report)
    {
        var assemblyName = report.AssemblyName;
        var files = new[]
        {
            new PublishedFile(
                "implementation",
                report.Implementation.File.RelativePath,
                new ProjectCodeArtifactFileEvidence(
                    $"bin/{assemblyName}.dll",
                    report.Implementation.File.Size,
                    report.Implementation.File.Sha256)),
            new PublishedFile(
                "reference-assembly",
                report.ReferenceAssembly.File.RelativePath,
                new ProjectCodeArtifactFileEvidence(
                    $"ref/{assemblyName}.dll",
                    report.ReferenceAssembly.File.Size,
                    report.ReferenceAssembly.File.Sha256)),
            new PublishedFile(
                "portable-pdb",
                report.PortablePdb.File.RelativePath,
                new ProjectCodeArtifactFileEvidence(
                    $"bin/{assemblyName}.pdb",
                    report.PortablePdb.File.Size,
                    report.PortablePdb.File.Sha256)),
            new PublishedFile(
                "dependencies",
                report.Dependencies.File.RelativePath,
                new ProjectCodeArtifactFileEvidence(
                    $"bin/{assemblyName}.deps.json",
                    report.Dependencies.File.Size,
                    report.Dependencies.File.Sha256)),
        };
        if (files
            .Select(file => file.Published.RelativePath)
            .Append(ManifestPath)
            .Any(path => !ProjectCodeSdkBuildPath.IsPortableRelativePath(path)))
        {
            throw new InvalidDataException(
                "Inspected assembly name cannot form publication paths.");
        }

        return files;
    }

    private static string ComputePublicationId(
        ProjectCodeArtifactMetadataReport report,
        IReadOnlyList<PublishedFile> files)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, "1");
        AppendString(hash, report.ReportId);
        foreach (var file in files.OrderBy(
            file => file.Published.RelativePath,
            StringComparer.Ordinal))
        {
            AppendString(hash, file.SourceRelativePath);
            AppendEvidence(hash, file.Published);
        }

        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static byte[] RenderManifest(
        string publicationId,
        ProjectCodeArtifactMetadataReport report,
        IReadOnlyList<PublishedFile> files)
    {
        var buffer = new ArrayBufferWriter<byte>();
        using var writer = new Utf8JsonWriter(
            buffer,
            new JsonWriterOptions
            {
                Indented = true,
                SkipValidation = false,
            });
        writer.WriteStartObject();
        writer.WriteString("schema", Schema);
        writer.WriteNumber("schemaVersion", 1);
        writer.WriteString("publicationId", publicationId);
        writer.WriteString("reportId", report.ReportId);
        writer.WriteString("rawOutputId", report.RawOutputId);
        writer.WriteString(
            "projectId",
            report.ProjectId.ToString("D").ToLowerInvariant());
        writer.WriteString("workspaceId", report.WorkspaceId);
        writer.WriteString("credentialId", report.CredentialId);
        writer.WriteString("sdkVersion", report.SdkVersion);
        writer.WriteString("targetFramework", report.TargetFramework);
        writer.WriteString("assemblyName", report.AssemblyName);
        WriteAssembly(
            writer,
            "implementation",
            report.Implementation,
            FindFile(files, "implementation"));
        WriteAssembly(
            writer,
            "referenceAssembly",
            report.ReferenceAssembly,
            FindFile(files, "reference-assembly"));
        writer.WriteStartObject("portablePdb");
        WriteFileBinding(
            writer,
            report.PortablePdb.File,
            FindFile(files, "portable-pdb"));
        writer.WriteString(
            "contentId",
            report.PortablePdb.ContentId.ToString("D").ToLowerInvariant());
        writer.WriteNumber("stamp", report.PortablePdb.Stamp);
        writer.WriteStartArray("documents");
        foreach (var document in report.PortablePdb.Documents.Order(
            StringComparer.Ordinal))
        {
            writer.WriteStringValue(document);
        }

        writer.WriteEndArray();
        writer.WriteEndObject();
        writer.WriteStartObject("dependencies");
        WriteFileBinding(
            writer,
            report.Dependencies.File,
            FindFile(files, "dependencies"));
        writer.WriteString(
            "runtimeTarget",
            report.Dependencies.RuntimeTarget);
        writer.WriteString("library", report.Dependencies.Library);
        writer.WriteString(
            "runtimeAsset",
            report.Dependencies.RuntimeAsset);
        writer.WriteEndObject();
        writer.WriteEndObject();
        writer.Flush();
        return buffer.WrittenSpan.ToArray();
    }

    private static void WriteAssembly(
        Utf8JsonWriter writer,
        string propertyName,
        ProjectCodeInspectedAssembly assembly,
        ProjectCodeArtifactFileEvidence published)
    {
        writer.WriteStartObject(propertyName);
        WriteFileBinding(writer, assembly.File, published);
        writer.WriteString("moduleName", assembly.ModuleName);
        writer.WriteString(
            "mvid",
            assembly.Mvid.ToString("D").ToLowerInvariant());
        WriteIdentity(writer, "identity", assembly.Identity);
        writer.WriteStartArray("references");
        foreach (var reference in assembly.References.OrderBy(
            value => value.FullName,
            StringComparer.Ordinal))
        {
            writer.WriteStartObject();
            WriteIdentityProperties(writer, reference);
            writer.WriteEndObject();
        }

        writer.WriteEndArray();
        writer.WriteNumber("imageFlags", (int)assembly.ImageFlags);
        writer.WriteBoolean(
            "isReferenceAssembly",
            assembly.IsReferenceAssembly);
        writer.WriteEndObject();
    }

    private static void WriteFileBinding(
        Utf8JsonWriter writer,
        ProjectCodeArtifactFileEvidence inspected,
        ProjectCodeArtifactFileEvidence published)
    {
        writer.WriteString("sourceRelativePath", inspected.RelativePath);
        writer.WriteStartObject("file");
        writer.WriteString("relativePath", published.RelativePath);
        writer.WriteNumber("size", published.Size);
        writer.WriteString("sha256", published.Sha256);
        writer.WriteEndObject();
    }

    private static void WriteIdentity(
        Utf8JsonWriter writer,
        string propertyName,
        ProjectCodeAssemblyIdentity identity)
    {
        writer.WriteStartObject(propertyName);
        WriteIdentityProperties(writer, identity);
        writer.WriteEndObject();
    }

    private static void WriteIdentityProperties(
        Utf8JsonWriter writer,
        ProjectCodeAssemblyIdentity identity)
    {
        writer.WriteString("simpleName", identity.SimpleName);
        writer.WriteString("version", identity.Version.ToString());
        writer.WriteString("culture", identity.Culture);
        writer.WriteString("publicKeyToken", identity.PublicKeyToken);
    }

    private static ProjectCodeArtifactFileEvidence FindFile(
        IEnumerable<PublishedFile> files,
        string location) =>
        files.Single(file => file.Location == location).Published;

    private static async Task<bool> CopyVerifiedFileAsync(
        string source,
        string destination,
        ProjectCodeArtifactFileEvidence expected,
        CancellationToken cancellationToken)
    {
        try
        {
            if (HasReparsePointInPath(source))
            {
                return false;
            }

            var before = new FileInfo(source);
            var length = before.Length;
            var writeTime = before.LastWriteTimeUtc;
            if (length != expected.Size)
            {
                return false;
            }

            Directory.CreateDirectory(
                Path.GetDirectoryName(destination)
                    ?? throw new IOException());
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
                    FileOptions.Asynchronous
                        | FileOptions.SequentialScan);
                if (input.Length != expected.Size)
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

                    if (read > expected.Size - total)
                    {
                        return false;
                    }

                    await output.WriteAsync(
                        buffer.AsMemory(0, read),
                        cancellationToken).ConfigureAwait(false);
                    hash.AppendData(buffer, 0, read);
                    total = checked(total + read);
                }

                await output.FlushAsync(cancellationToken)
                    .ConfigureAwait(false);
                output.Flush(flushToDisk: true);
                before.Refresh();
                return before.Exists
                    && before.Length == length
                    && before.LastWriteTimeUtc == writeTime
                    && total == expected.Size
                    && string.Equals(
                        Convert.ToHexString(hash.GetHashAndReset())
                            .ToLowerInvariant(),
                        expected.Sha256,
                        StringComparison.Ordinal)
                    && !HasReparsePointInPath(source);
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

    private static async Task WriteNewFileAsync(
        string path,
        byte[] contents,
        CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            path,
            FileMode.CreateNew,
            FileAccess.Write,
            FileShare.None,
            CopyBufferSize,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        await stream.WriteAsync(contents, cancellationToken)
            .ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
        stream.Flush(flushToDisk: true);
    }

    private static async Task<bool> VerifyClosedTreeAsync(
        string root,
        IReadOnlyList<ProjectCodeArtifactFileEvidence> expected,
        byte[] manifestBytes,
        CancellationToken cancellationToken)
    {
        if (!TryEnumerateTree(root, out var files, out var closure)
            || !files
                .Order(StringComparer.Ordinal)
                .SequenceEqual(
                    expected
                        .Select(file => file.RelativePath)
                        .Order(StringComparer.Ordinal),
                    StringComparer.Ordinal)
            || !closure.SequenceEqual(
                ExpectedClosure(expected),
                StringComparer.Ordinal))
        {
            return false;
        }

        foreach (var file in expected)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var path = CombinePortable(root, file.RelativePath);
            if (string.Equals(
                    file.RelativePath,
                    ManifestPath,
                    StringComparison.Ordinal))
            {
                var actual = await File.ReadAllBytesAsync(
                    path,
                    cancellationToken).ConfigureAwait(false);
                if (!actual.AsSpan().SequenceEqual(manifestBytes))
                {
                    return false;
                }
            }

            var hash = await HashStableFileAsync(
                path,
                file.Size,
                cancellationToken).ConfigureAwait(false);
            if (hash is null
                || hash.Value.Size != file.Size
                || !string.Equals(
                    hash.Value.Sha256,
                    file.Sha256,
                    StringComparison.Ordinal))
            {
                return false;
            }
        }

        return TryEnumerateTree(
                root,
                out var filesAfter,
                out var closureAfter)
            && filesAfter.SequenceEqual(files, StringComparer.Ordinal)
            && closureAfter.SequenceEqual(closure, StringComparer.Ordinal);
    }

    private static bool TryEnumerateTree(
        string root,
        out string[] filePaths,
        out string[] closure)
    {
        filePaths = [];
        closure = [];
        try
        {
            if (!Directory.Exists(root) || HasReparsePointInPath(root))
            {
                return false;
            }

            var files = new List<string>();
            var entriesFound = new List<string>();
            var directories = new Stack<string>();
            directories.Push(root);
            var entries = 0;
            while (directories.TryPop(out var directory))
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(
                    directory))
                {
                    if (++entries > 16)
                    {
                        return false;
                    }

                    var attributes = File.GetAttributes(entry);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        return false;
                    }

                    var relative = Path.GetRelativePath(root, entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(
                            relative))
                    {
                        return false;
                    }

                    if ((attributes & FileAttributes.Directory) != 0)
                    {
                        entriesFound.Add("d/" + relative);
                        directories.Push(entry);
                    }
                    else if (File.Exists(entry))
                    {
                        files.Add(relative);
                        entriesFound.Add("f/" + relative);
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            filePaths = files.Order(StringComparer.Ordinal).ToArray();
            closure = entriesFound.Order(StringComparer.Ordinal).ToArray();
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

    private static string[] ExpectedClosure(
        IEnumerable<ProjectCodeArtifactFileEvidence> files) =>
        files
            .SelectMany(file =>
                EnumerateExpectedClosureEntries(file.RelativePath))
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToArray();

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

    private static async Task<FileHash?> HashStableFileAsync(
        string path,
        long expectedSize,
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
            if (length != expectedSize)
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

                    if (read > expectedSize - total)
                    {
                        return null;
                    }

                    hash.AppendData(buffer, 0, read);
                    total = checked(total + read);
                }

                before.Refresh();
                if (!before.Exists
                    || before.Length != length
                    || before.LastWriteTimeUtc != writeTime
                    || total != expectedSize
                    || HasReparsePointInPath(path))
                {
                    return null;
                }

                return new(
                    total,
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

    private static string CombinePortable(string root, string relativePath) =>
        Path.Combine(
            root,
            relativePath.Replace('/', Path.DirectorySeparatorChar));

    private static bool Overlaps(string left, string right) =>
        IsDescendantOrSame(left, right)
        || IsDescendantOrSame(right, left);

    private static bool IsDescendantOrSame(string root, string candidate)
    {
        var normalizedRoot = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(root));
        var normalizedCandidate = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(candidate));
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        return string.Equals(normalizedRoot, normalizedCandidate, comparison)
            || normalizedCandidate.StartsWith(
                normalizedRoot + Path.DirectorySeparatorChar,
                comparison);
    }

    private static bool HasReparsePointInPath(string path)
    {
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
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
                || string.Equals(parent, current, comparison))
            {
                break;
            }

            current = parent;
        }

        return false;
    }

    private static bool TryDeleteOwnedTree(
        string root,
        string parent,
        string marker)
    {
        try
        {
            var resolvedRoot = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(root));
            var resolvedParent = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(parent));
            if (!IsDescendantOrSame(resolvedParent, resolvedRoot)
                || string.Equals(
                    resolvedParent,
                    resolvedRoot,
                    OperatingSystem.IsWindows()
                        ? StringComparison.OrdinalIgnoreCase
                        : StringComparison.Ordinal)
                || !Path.GetFileName(resolvedRoot).Contains(
                    marker,
                    StringComparison.Ordinal))
            {
                return false;
            }

            Directory.Delete(resolvedRoot, recursive: true);
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

    private static string Hash(ReadOnlySpan<byte> contents) =>
        Convert.ToHexString(SHA256.HashData(contents)).ToLowerInvariant();

    private static void AppendEvidence(
        IncrementalHash hash,
        ProjectCodeArtifactFileEvidence file)
    {
        AppendString(hash, file.RelativePath);
        Span<byte> size = stackalloc byte[sizeof(long)];
        BinaryPrimitives.WriteInt64LittleEndian(size, file.Size);
        hash.AppendData(size);
        hash.AppendData(Convert.FromHexString(file.Sha256));
    }

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

    private static ProjectCodeArtifactPublicationResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodeArtifactPublicationResult.Failure(
            [Diagnostic(code, location, message)]);

    private static ProjectCodeArtifactPublicationDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    private sealed record OutputPath(
        string Root,
        string Parent,
        string Leaf);

    private sealed record OutputPathResult(
        OutputPath? Path,
        ProjectCodeArtifactPublicationDiagnostic? Diagnostic);

    private sealed record PublishedFile(
        string Location,
        string SourceRelativePath,
        ProjectCodeArtifactFileEvidence Published);

    private readonly record struct FileHash(long Size, string Sha256);

    private sealed class PublicationPathException(
        ProjectCodeArtifactPublicationDiagnostic diagnostic) : Exception
    {
        public ProjectCodeArtifactPublicationDiagnostic Diagnostic
        {
            get;
        } = diagnostic;
    }
}
