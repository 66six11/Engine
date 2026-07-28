using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Application.Bootstrap.Distribution;

internal sealed record ManagedBuildProcessContext(
    string TargetPlatform,
    string TargetArchitecture);

internal sealed record VerifiedManagedBuildEnvironmentTree(
    string RelativeRoot,
    string AbsoluteRoot,
    IReadOnlyList<VerifiedEditorImageFile> Files);

internal sealed record VerifiedManagedBuildEnvironmentProjection(
    string EngineGenerationId,
    string TargetPlatform,
    string TargetArchitecture,
    string EnvironmentId,
    string TargetFramework,
    string SdkVersion,
    string HostFxrVersion,
    string HostRuntimeVersion,
    string ReferencePackVersion,
    VerifiedEditorImageFile DotnetHost,
    VerifiedManagedBuildEnvironmentTree Sdk,
    VerifiedManagedBuildEnvironmentTree HostFxr,
    VerifiedManagedBuildEnvironmentTree HostRuntime,
    VerifiedManagedBuildEnvironmentTree ReferencePack,
    VerifiedEditorImageFile RuntimeContract,
    VerifiedEditorImageFile EditorContract,
    IReadOnlyList<VerifiedEditorImageFile> SelectedFiles,
    string ProjectionId);

internal sealed class VerifiedManagedBuildEnvironmentLease
{
    private readonly VerifiedEditorImageInventoryLease editorImageLease_;
    private readonly HashSet<string> selectedPaths_;
    private readonly object stateGate_ = new();
    private int isCurrent_ = 1;

    internal VerifiedManagedBuildEnvironmentLease(
        VerifiedEditorImageInventoryLease editorImageLease,
        VerifiedManagedBuildEnvironmentProjection projection)
    {
        ArgumentNullException.ThrowIfNull(editorImageLease);
        ArgumentNullException.ThrowIfNull(projection);
        if (!editorImageLease.IsCurrent
            || projection.SelectedFiles.Count == 0
            || projection.SelectedFiles
                .GroupBy(file => file.RelativePath, StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Managed build environment requires one current exact Editor Image projection.",
                nameof(projection));
        }

        editorImageLease_ = editorImageLease;
        Projection = projection;
        selectedPaths_ = projection.SelectedFiles
            .Select(file => file.RelativePath)
            .ToHashSet(StringComparer.Ordinal);
    }

    public VerifiedManagedBuildEnvironmentProjection Projection { get; }

    public bool IsCurrent =>
        Volatile.Read(ref isCurrent_) != 0 && editorImageLease_.IsCurrent;

    internal bool TryGetCurrentFile(
        string relativePath,
        out VerifiedEditorImageFile? file)
    {
        lock (stateGate_)
        {
            file = null;
            return Volatile.Read(ref isCurrent_) != 0
                && selectedPaths_.Contains(relativePath)
                && editorImageLease_.TryGetCurrentFile(relativePath, out file);
        }
    }

    internal void Revoke()
    {
        lock (stateGate_)
        {
            Interlocked.Exchange(ref isCurrent_, 0);
        }
    }
}

internal sealed record VerifiedManagedBuildEnvironmentDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class VerifiedManagedBuildEnvironmentLoadResult
{
    private VerifiedManagedBuildEnvironmentLoadResult(
        VerifiedManagedBuildEnvironmentLease? lease,
        IReadOnlyList<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics)
    {
        Lease = lease;
        Diagnostics = diagnostics;
    }

    public VerifiedManagedBuildEnvironmentLease? Lease { get; }

    public IReadOnlyList<VerifiedManagedBuildEnvironmentDiagnostic> Diagnostics { get; }

    public bool Succeeded => Lease is not null && Diagnostics.Count == 0;

    public static VerifiedManagedBuildEnvironmentLoadResult Success(
        VerifiedManagedBuildEnvironmentLease lease) =>
        new(lease, []);

    public static VerifiedManagedBuildEnvironmentLoadResult Failure(
        IReadOnlyList<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics) =>
        new(null, diagnostics);
}

internal static class EngineDistributionManagedBuildEnvironmentLoader
{
    internal const string MetadataRelativePath =
        "metadata/managed-build-environment.json";

    private const int MaxMetadataBytes = 256 * 1024;
    private const string MetadataSchema = "com.asharia.managed-build-environment";
    private const string TargetFramework = "net10.0";
    private const string DotnetRoot = "managed/dotnet";
    private const string ReferencePackName = "Microsoft.NETCore.App.Ref";
    private const string RuntimeContractPath =
        "bin/Asharia.Runtime.Contracts.dll";
    private const string EditorContractPath = "bin/Asharia.Editor.dll";
    private static readonly Regex StableVersionPattern = new(
        "^[0-9]+\\.[0-9]+\\.[0-9]+$",
        RegexOptions.CultureInvariant);
    private static readonly Regex EnvironmentIdPattern = new(
        "^[a-z0-9][a-z0-9.-]{0,99}$",
        RegexOptions.CultureInvariant);
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private static readonly JsonWriterOptions CanonicalWriterOptions = new()
    {
        Encoder = JavaScriptEncoder.Default,
        IndentCharacter = ' ',
        IndentSize = 2,
        Indented = true,
        NewLine = "\n",
    };
    private static readonly StringComparer FileSystemPathComparer =
        OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
    private static readonly IComparer<string> Utf8Comparer =
        Comparer<string>.Create(CompareUtf8);

    public static async Task<VerifiedManagedBuildEnvironmentLoadResult> LoadAsync(
        VerifiedEditorImageInventoryLease editorImageLease,
        ManagedBuildProcessContext processContext,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(editorImageLease);
        ArgumentNullException.ThrowIfNull(processContext);
        var diagnostics = new List<VerifiedManagedBuildEnvironmentDiagnostic>();
        if (!editorImageLease.TryGetCurrentFiles(out var editorFiles)
            || editorFiles is null)
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.generation-not-current",
                "engineGenerationId",
                "The verified Editor Image generation lease is no longer current."));
            return Failure(diagnostics);
        }

        if (!IsCurrentProcessContext(editorImageLease, processContext))
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.process-context-mismatch",
                "context",
                "The selected Editor Image platform/architecture does not match the supplied Studio process context."));
            return Failure(diagnostics);
        }

        if (!editorImageLease.TryGetCurrentFile(
                MetadataRelativePath,
                out var metadataFile)
            || metadataFile is null
            || !string.Equals(
                metadataFile.Role,
                "metadata",
                StringComparison.Ordinal)
            || !string.Equals(
                metadataFile.MediaType,
                "application/json",
                StringComparison.OrdinalIgnoreCase))
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.metadata-missing",
                MetadataRelativePath,
                "The verified Editor Image must inventory one managed build environment metadata file."));
            return Failure(diagnostics);
        }

        var metadataBytes = await ReadVerifiedFileAsync(
            metadataFile,
            MaxMetadataBytes,
            cancellationToken).ConfigureAwait(false);
        if (metadataBytes is null)
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.metadata-drift",
                MetadataRelativePath,
                "Managed build environment metadata no longer matches the verified Editor Image inventory."));
            return Failure(diagnostics);
        }

        var declaration = ParseDeclaration(metadataBytes, diagnostics);
        if (declaration is null)
        {
            return Failure(diagnostics);
        }

        var inventory = ResolveInventory(
            editorFiles,
            declaration,
            metadataFile,
            diagnostics);
        if (inventory is null)
        {
            return Failure(diagnostics);
        }

        if (!editorImageLease.IsCurrent)
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.generation-not-current",
                "engineGenerationId",
                "The verified Editor Image generation lease changed during projection."));
            return Failure(diagnostics);
        }

        var projection = new VerifiedManagedBuildEnvironmentProjection(
            editorImageLease.EngineGenerationId,
            editorImageLease.TargetPlatform,
            editorImageLease.TargetArchitecture,
            declaration.EnvironmentId,
            declaration.TargetFramework,
            declaration.Sdk.Version,
            declaration.HostFxr.Version,
            declaration.HostRuntime.Version,
            declaration.ReferencePack.Version,
            inventory.DotnetHost,
            inventory.Sdk,
            inventory.HostFxr,
            inventory.HostRuntime,
            inventory.ReferencePack,
            inventory.RuntimeContract,
            inventory.EditorContract,
            inventory.AllSelectedFiles,
            ComputeProjectionId(
                editorImageLease,
                declaration,
                inventory.AllSelectedFiles));
        return VerifiedManagedBuildEnvironmentLoadResult.Success(
            new VerifiedManagedBuildEnvironmentLease(
                editorImageLease,
                projection));
    }

    private static VerifiedManagedBuildEnvironmentLoadResult Failure(
        IReadOnlyList<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics) =>
        VerifiedManagedBuildEnvironmentLoadResult.Failure(diagnostics);

    private static ManagedBuildEnvironmentDeclaration? ParseDeclaration(
        byte[] bytes,
        ICollection<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics)
    {
        try
        {
            if (bytes.Length == 0
                || (bytes.Length >= 3
                    && bytes[0] == 0xef
                    && bytes[1] == 0xbb
                    && bytes[2] == 0xbf)
                || bytes[^1] != (byte)'\n'
                || bytes.AsSpan().Contains((byte)'\r'))
            {
                throw new InvalidDataException();
            }

            _ = StrictUtf8.GetString(bytes);
            using var document = JsonDocument.Parse(
                bytes,
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = false,
                    CommentHandling = JsonCommentHandling.Disallow,
                    MaxDepth = 16,
                });
            var root = document.RootElement;
            EnsureExactObject(
                root,
                [
                    "schema",
                    "schemaVersion",
                    "environmentId",
                    "targetFramework",
                    "dotnetRoot",
                    "dotnetHostPath",
                    "sdk",
                    "hostFxr",
                    "hostRuntime",
                    "referencePack",
                    "contracts",
                ]);
            if (ReadString(root, "schema") != MetadataSchema
                || ReadInt32(root, "schemaVersion") != 1)
            {
                throw new InvalidDataException();
            }

            var environmentId = ReadString(root, "environmentId");
            var targetFramework = ReadString(root, "targetFramework");
            if (!EnvironmentIdPattern.IsMatch(environmentId)
                || targetFramework != TargetFramework)
            {
                throw new InvalidDataException();
            }

            var declaration = new ManagedBuildEnvironmentDeclaration(
                environmentId,
                targetFramework,
                ReadPath(root, "dotnetRoot"),
                ReadPath(root, "dotnetHostPath"),
                ReadSdk(root.GetProperty("sdk")),
                ReadVersionedRoot(root.GetProperty("hostFxr")),
                ReadVersionedRoot(root.GetProperty("hostRuntime")),
                ReadReferencePack(root.GetProperty("referencePack")),
                ReadContracts(root.GetProperty("contracts")));
            ValidateDeclarationPaths(declaration);
            if (!bytes.AsSpan().SequenceEqual(RenderCanonical(root)))
            {
                throw new InvalidDataException();
            }

            return declaration;
        }
        catch (Exception error) when (
            error is ArgumentException
                or InvalidDataException
                or JsonException
                or KeyNotFoundException
                or DecoderFallbackException
                or OverflowException)
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.metadata-invalid",
                MetadataRelativePath,
                "Managed build environment metadata must use the closed canonical v1 contract and exact portable .NET layout."));
            return null;
        }
    }

    private static byte[] RenderCanonical(JsonElement root)
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, CanonicalWriterOptions))
        {
            root.WriteTo(writer);
        }

        var rendered = stream.ToArray();
        Array.Resize(ref rendered, rendered.Length + 1);
        rendered[^1] = (byte)'\n';
        return rendered;
    }

    private static ResolvedInventory? ResolveInventory(
        IReadOnlyList<VerifiedEditorImageFile> editorFiles,
        ManagedBuildEnvironmentDeclaration declaration,
        VerifiedEditorImageFile metadata,
        ICollection<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics)
    {
        var filesByPath = editorFiles.ToDictionary(
            file => file.RelativePath,
            StringComparer.Ordinal);
        var dotnetHost = GetExactFile(
            filesByPath,
            declaration.DotnetHostPath,
            "dotnetHostPath",
            diagnostics);
        var sdkEntry = GetExactFile(
            filesByPath,
            declaration.Sdk.EntryPath,
            "sdk/entryPath",
            diagnostics);
        var sdkBundledVersions = GetExactFile(
            filesByPath,
            declaration.Sdk.BundledVersionsPath,
            "sdk/bundledVersionsPath",
            diagnostics);
        var sdkRuntimeConfig = GetExactFile(
            filesByPath,
            declaration.Sdk.RuntimeConfigPath,
            "sdk/runtimeConfigPath",
            diagnostics);
        var runtimeContract = GetExactFile(
            filesByPath,
            declaration.Contracts.RuntimePath,
            "contracts/runtimePath",
            diagnostics);
        var editorContract = GetExactFile(
            filesByPath,
            declaration.Contracts.EditorPath,
            "contracts/editorPath",
            diagnostics);
        var sdk = ResolveTree(
            editorFiles,
            declaration.Sdk.Root,
            "sdk/root",
            diagnostics);
        var hostFxr = ResolveTree(
            editorFiles,
            declaration.HostFxr.Root,
            "hostFxr/root",
            diagnostics);
        var hostRuntime = ResolveTree(
            editorFiles,
            declaration.HostRuntime.Root,
            "hostRuntime/root",
            diagnostics);
        var referencePack = ResolveTree(
            editorFiles,
            declaration.ReferencePack.Root,
            "referencePack/root",
            diagnostics);
        if (dotnetHost is null
            || sdkEntry is null
            || sdkBundledVersions is null
            || sdkRuntimeConfig is null
            || runtimeContract is null
            || editorContract is null
            || sdk is null
            || hostFxr is null
            || hostRuntime is null
            || referencePack is null)
        {
            return null;
        }

        var dotnetRoot = ResolveLogicalRootAbsolutePath(
            declaration.DotnetRoot,
            declaration.DotnetHostPath,
            dotnetHost.AbsolutePath);
        if (dotnetRoot is null
            || !IsSamePath(Path.GetDirectoryName(dotnetHost.AbsolutePath), dotnetRoot)
            || !IsDescendantRoot(dotnetRoot, sdk.AbsoluteRoot)
            || !IsDescendantRoot(dotnetRoot, hostFxr.AbsoluteRoot)
            || !IsDescendantRoot(dotnetRoot, hostRuntime.AbsoluteRoot)
            || !IsDescendantRoot(dotnetRoot, referencePack.AbsoluteRoot))
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.dotnet-layout-mismatch",
                "dotnetRoot",
                "Verified .NET files do not form one coherent installation rooted at dotnetRoot."));
            return null;
        }

        var hostFxrEntry = declaration.HostFxr.Root + "/" + HostFxrFileName();
        var hostRuntimeCore =
            declaration.HostRuntime.Root + "/System.Private.CoreLib.dll";
        var referenceRuntime =
            declaration.ReferencePack.AssembliesRoot + "/System.Runtime.dll";
        if (!TreeContains(sdk, sdkEntry)
            || !TreeContains(sdk, sdkBundledVersions)
            || !TreeContains(sdk, sdkRuntimeConfig)
            || !TreeContains(hostFxr, hostFxrEntry)
            || !TreeContains(hostRuntime, hostRuntimeCore)
            || !TreeContains(referencePack, referenceRuntime))
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.semantic-path-mismatch",
                MetadataRelativePath,
                "Managed build environment semantic files must belong to their declared inventory trees."));
            return null;
        }

        var dotnetSelected = new[] { dotnetHost }
            .Concat(sdk.Files)
            .Concat(hostFxr.Files)
            .Concat(hostRuntime.Files)
            .Concat(referencePack.Files)
            .Select(file => file.RelativePath)
            .ToHashSet(StringComparer.Ordinal);
        var dotnetInventory = editorFiles
            .Where(file => IsSameOrDescendant(
                file.RelativePath,
                declaration.DotnetRoot))
            .Select(file => file.RelativePath)
            .ToHashSet(StringComparer.Ordinal);
        if (!dotnetSelected.SetEquals(dotnetInventory))
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.dotnet-inventory-not-closed",
                declaration.DotnetRoot,
                "The verified dotnet root contains inventory outside the selected host and exact component trees."));
            return null;
        }

        var selected = new[]
            {
                metadata,
                dotnetHost,
                runtimeContract,
                editorContract,
            }
            .Concat(sdk.Files)
            .Concat(hostFxr.Files)
            .Concat(hostRuntime.Files)
            .Concat(referencePack.Files)
            .GroupBy(file => file.RelativePath, StringComparer.Ordinal)
            .Select(group => group.First())
            .OrderBy(file => file.RelativePath, Utf8Comparer)
            .ToArray();
        return new ResolvedInventory(
            dotnetHost,
            sdk,
            hostFxr,
            hostRuntime,
            referencePack,
            runtimeContract,
            editorContract,
            Array.AsReadOnly(selected));
    }

    private static VerifiedEditorImageFile? GetExactFile(
        IReadOnlyDictionary<string, VerifiedEditorImageFile> filesByPath,
        string relativePath,
        string location,
        ICollection<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics)
    {
        if (filesByPath.TryGetValue(relativePath, out var file))
        {
            return file;
        }

        diagnostics.Add(Diagnostic(
            "distribution.managed-build-environment.file-not-in-inventory",
            location,
            $"Declared file '{relativePath}' is not part of the verified Editor Image inventory."));
        return null;
    }

    private static VerifiedManagedBuildEnvironmentTree? ResolveTree(
        IReadOnlyList<VerifiedEditorImageFile> editorFiles,
        string relativeRoot,
        string location,
        ICollection<VerifiedManagedBuildEnvironmentDiagnostic> diagnostics)
    {
        var prefix = relativeRoot + "/";
        var files = editorFiles
            .Where(file => file.RelativePath.StartsWith(
                prefix,
                StringComparison.Ordinal))
            .OrderBy(file => file.RelativePath, Utf8Comparer)
            .ToArray();
        if (files.Length == 0)
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.tree-not-in-inventory",
                location,
                $"Declared root '{relativeRoot}' has no verified Editor Image files."));
            return null;
        }

        string? absoluteRoot = null;
        foreach (var file in files)
        {
            var candidate = ResolveLogicalRootAbsolutePath(
                relativeRoot,
                file.RelativePath,
                file.AbsolutePath);
            if (candidate is null)
            {
                absoluteRoot = null;
                break;
            }

            absoluteRoot ??= candidate;
            if (!IsSamePath(absoluteRoot, candidate))
            {
                absoluteRoot = null;
                break;
            }
        }

        if (absoluteRoot is null)
        {
            diagnostics.Add(Diagnostic(
                "distribution.managed-build-environment.tree-layout-mismatch",
                location,
                "Verified subtree files do not share one coherent absolute root."));
            return null;
        }

        return new VerifiedManagedBuildEnvironmentTree(
            relativeRoot,
            absoluteRoot,
            Array.AsReadOnly(files));
    }

    private static string? ResolveLogicalRootAbsolutePath(
        string logicalRoot,
        string logicalFile,
        string absoluteFile)
    {
        if (!PathHasPrefix(logicalFile, logicalRoot))
        {
            return null;
        }

        var suffixParts = RelativeToRoot(logicalFile, logicalRoot).Split('/');
        var current = Path.GetFullPath(absoluteFile);
        foreach (var _ in suffixParts)
        {
            current = Path.GetDirectoryName(current) ?? string.Empty;
        }

        return string.IsNullOrEmpty(current) ? null : current;
    }

    private static bool IsDescendantRoot(string root, string child)
    {
        var relative = Path.GetRelativePath(root, child)
            .Replace(Path.DirectorySeparatorChar, '/');
        return IsPortableRelativePath(relative);
    }

    private static bool TreeContains(
        VerifiedManagedBuildEnvironmentTree tree,
        VerifiedEditorImageFile file) =>
        tree.Files.Any(candidate => string.Equals(
            candidate.RelativePath,
            file.RelativePath,
            StringComparison.Ordinal));

    private static bool TreeContains(
        VerifiedManagedBuildEnvironmentTree tree,
        string relativePath) =>
        tree.Files.Any(candidate => string.Equals(
            candidate.RelativePath,
            relativePath,
            StringComparison.Ordinal));

    private static bool IsCurrentProcessContext(
        VerifiedEditorImageInventoryLease lease,
        ManagedBuildProcessContext processContext)
    {
        return string.Equals(
                lease.TargetPlatform,
                processContext.TargetPlatform,
                StringComparison.Ordinal)
            && string.Equals(
                lease.TargetArchitecture,
                processContext.TargetArchitecture,
                StringComparison.Ordinal);
    }

    private static async Task<byte[]?> ReadVerifiedFileAsync(
        VerifiedEditorImageFile file,
        int maxBytes,
        CancellationToken cancellationToken)
    {
        if (file.Size < 1
            || file.Size > maxBytes)
        {
            return null;
        }

        try
        {
            if (HasReparsePointInPath(file.AbsolutePath))
            {
                return null;
            }

            await using var stream = new FileStream(
                file.AbsolutePath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                64 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length != file.Size)
            {
                return null;
            }

            var contents = new byte[checked((int)file.Size)];
            var offset = 0;
            while (offset < contents.Length)
            {
                var read = await stream.ReadAsync(
                    contents.AsMemory(offset),
                    cancellationToken).ConfigureAwait(false);
                if (read == 0)
                {
                    return null;
                }

                offset += read;
            }

            if (stream.Length != contents.Length)
            {
                return null;
            }

            var digest = Convert.ToHexString(SHA256.HashData(contents))
                .ToLowerInvariant();
            return string.Equals(digest, file.Sha256, StringComparison.Ordinal)
                ? contents
                : null;
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static string ComputeProjectionId(
        VerifiedEditorImageInventoryLease editorImageLease,
        ManagedBuildEnvironmentDeclaration declaration,
        IReadOnlyList<VerifiedEditorImageFile> selectedFiles)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, "distribution-managed-build-environment-projection-v1");
        AppendString(hash, editorImageLease.EngineGenerationId);
        AppendString(hash, editorImageLease.TargetPlatform);
        AppendString(hash, editorImageLease.TargetArchitecture);
        AppendString(hash, declaration.EnvironmentId);
        AppendString(hash, declaration.TargetFramework);
        AppendString(hash, declaration.DotnetRoot);
        AppendString(hash, declaration.DotnetHostPath);
        AppendString(hash, declaration.Sdk.Version);
        AppendString(hash, declaration.Sdk.Root);
        AppendString(hash, declaration.HostFxr.Version);
        AppendString(hash, declaration.HostFxr.Root);
        AppendString(hash, declaration.HostRuntime.Version);
        AppendString(hash, declaration.HostRuntime.Root);
        AppendString(hash, declaration.ReferencePack.Version);
        AppendString(hash, declaration.ReferencePack.Root);
        AppendString(hash, declaration.Contracts.RuntimePath);
        AppendString(hash, declaration.Contracts.EditorPath);
        foreach (var file in selectedFiles)
        {
            AppendFile(hash, file);
        }

        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static void AppendFile(
        IncrementalHash hash,
        VerifiedEditorImageFile file)
    {
        AppendString(hash, file.RelativePath);
        AppendString(hash, file.Role);
        AppendString(hash, file.MediaType);
        Span<byte> size = stackalloc byte[sizeof(long)];
        BinaryPrimitives.WriteInt64LittleEndian(size, file.Size);
        hash.AppendData(size);
        hash.AppendData(Convert.FromHexString(file.Sha256));
    }

    private static void AppendString(IncrementalHash hash, string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }

    private static SdkDeclaration ReadSdk(JsonElement value)
    {
        EnsureExactObject(
            value,
            [
                "version",
                "root",
                "entryPath",
                "bundledVersionsPath",
                "runtimeConfigPath",
            ]);
        return new SdkDeclaration(
            ReadVersion(value, "version"),
            ReadPath(value, "root"),
            ReadPath(value, "entryPath"),
            ReadPath(value, "bundledVersionsPath"),
            ReadPath(value, "runtimeConfigPath"));
    }

    private static VersionedRootDeclaration ReadVersionedRoot(
        JsonElement value)
    {
        EnsureExactObject(value, ["version", "root"]);
        return new VersionedRootDeclaration(
            ReadVersion(value, "version"),
            ReadPath(value, "root"));
    }

    private static ReferencePackDeclaration ReadReferencePack(
        JsonElement value)
    {
        EnsureExactObject(
            value,
            ["name", "version", "root", "assembliesRoot"]);
        var name = ReadString(value, "name");
        if (name != ReferencePackName)
        {
            throw new InvalidDataException();
        }

        return new ReferencePackDeclaration(
            name,
            ReadVersion(value, "version"),
            ReadPath(value, "root"),
            ReadPath(value, "assembliesRoot"));
    }

    private static ContractDeclaration ReadContracts(JsonElement value)
    {
        EnsureExactObject(value, ["runtimePath", "editorPath"]);
        return new ContractDeclaration(
            ReadPath(value, "runtimePath"),
            ReadPath(value, "editorPath"));
    }

    private static void ValidateDeclarationPaths(
        ManagedBuildEnvironmentDeclaration value)
    {
        if (!string.Equals(
                value.DotnetRoot,
                DotnetRoot,
                StringComparison.Ordinal))
        {
            throw new InvalidDataException();
        }

        var expectedDotnetHost = value.DotnetRoot + "/"
            + (OperatingSystem.IsWindows() ? "dotnet.exe" : "dotnet");
        var expectedSdkRoot =
            value.DotnetRoot + "/sdk/" + value.Sdk.Version;
        var expectedHostFxrRoot =
            value.DotnetRoot + "/host/fxr/" + value.HostFxr.Version;
        var expectedHostRuntimeRoot = value.DotnetRoot
            + "/shared/Microsoft.NETCore.App/"
            + value.HostRuntime.Version;
        var expectedReferencePackRoot = value.DotnetRoot
            + "/packs/"
            + ReferencePackName
            + "/"
            + value.ReferencePack.Version;
        if (!string.Equals(
                value.DotnetHostPath,
                expectedDotnetHost,
                StringComparison.Ordinal)
            || !string.Equals(
                value.Sdk.Root,
                expectedSdkRoot,
                StringComparison.Ordinal)
            || !string.Equals(
                value.Sdk.EntryPath,
                expectedSdkRoot + "/dotnet.dll",
                StringComparison.Ordinal)
            || !string.Equals(
                value.Sdk.BundledVersionsPath,
                expectedSdkRoot + "/Microsoft.NETCoreSdk.BundledVersions.props",
                StringComparison.Ordinal)
            || !string.Equals(
                value.Sdk.RuntimeConfigPath,
                expectedSdkRoot + "/dotnet.runtimeconfig.json",
                StringComparison.Ordinal)
            || !string.Equals(
                value.HostFxr.Root,
                expectedHostFxrRoot,
                StringComparison.Ordinal)
            || !string.Equals(
                value.HostRuntime.Root,
                expectedHostRuntimeRoot,
                StringComparison.Ordinal)
            || !string.Equals(
                value.ReferencePack.Root,
                expectedReferencePackRoot,
                StringComparison.Ordinal)
            || !string.Equals(
                value.ReferencePack.AssembliesRoot,
                expectedReferencePackRoot + "/ref/" + TargetFramework,
                StringComparison.Ordinal)
            || !string.Equals(
                value.Contracts.RuntimePath,
                RuntimeContractPath,
                StringComparison.Ordinal)
            || !string.Equals(
                value.Contracts.EditorPath,
                EditorContractPath,
                StringComparison.Ordinal)
            || IsSameOrDescendant(
                MetadataRelativePath,
                value.DotnetRoot))
        {
            throw new InvalidDataException();
        }

        var roots = new[]
        {
            value.Sdk.Root,
            value.HostFxr.Root,
            value.HostRuntime.Root,
            value.ReferencePack.Root,
        };
        for (var index = 0; index < roots.Length; ++index)
        {
            for (var other = index + 1; other < roots.Length; ++other)
            {
                if (IsSameOrDescendant(roots[index], roots[other])
                    || IsSameOrDescendant(roots[other], roots[index]))
                {
                    throw new InvalidDataException();
                }
            }
        }
    }

    private static void EnsureExactObject(
        JsonElement element,
        IReadOnlyList<string> expectedProperties)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException();
        }

        var properties = element.EnumerateObject()
            .Select(property => property.Name)
            .ToArray();
        if (properties.Length != expectedProperties.Count
            || !properties.SequenceEqual(
                expectedProperties,
                StringComparer.Ordinal))
        {
            throw new InvalidDataException();
        }
    }

    private static string ReadString(
        JsonElement element,
        string propertyName)
    {
        var value = element.GetProperty(propertyName);
        return value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? throw new InvalidDataException()
            : throw new InvalidDataException();
    }

    private static int ReadInt32(
        JsonElement element,
        string propertyName)
    {
        var value = element.GetProperty(propertyName);
        return value.ValueKind == JsonValueKind.Number
            && value.TryGetInt32(out var result)
                ? result
                : throw new InvalidDataException();
    }

    private static string ReadVersion(
        JsonElement element,
        string propertyName)
    {
        var value = ReadString(element, propertyName);
        var parts = value.Split('.');
        if (!StableVersionPattern.IsMatch(value)
            || !Version.TryParse(value, out var parsed)
            || parts.Length != 3
            || parsed.Major.ToString() != parts[0]
            || parsed.Minor.ToString() != parts[1]
            || parsed.Build.ToString() != parts[2])
        {
            throw new InvalidDataException();
        }

        return value;
    }

    private static string ReadPath(
        JsonElement element,
        string propertyName)
    {
        var value = ReadString(element, propertyName);
        if (!IsPortableRelativePath(value))
        {
            throw new InvalidDataException();
        }

        return value;
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

    private static bool PathHasPrefix(string path, string root) =>
        path.StartsWith(root + "/", StringComparison.Ordinal);

    private static bool IsSameOrDescendant(string path, string root) =>
        string.Equals(path, root, StringComparison.Ordinal)
        || PathHasPrefix(path, root);

    private static string RelativeToRoot(string path, string root) =>
        path[(root.Length + 1)..];

    private static bool IsSamePath(string? left, string? right) =>
        left is not null
        && right is not null
        && FileSystemPathComparer.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(left)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(right)));

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

    private static string HostFxrFileName() =>
        OperatingSystem.IsWindows()
            ? "hostfxr.dll"
            : OperatingSystem.IsMacOS()
                ? "libhostfxr.dylib"
                : "libhostfxr.so";

    private static bool HasReparsePointInPath(string path)
    {
        var current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current))
        {
            if ((File.Exists(current) || Directory.Exists(current))
                && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
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

    private static VerifiedManagedBuildEnvironmentDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    private sealed record ManagedBuildEnvironmentDeclaration(
        string EnvironmentId,
        string TargetFramework,
        string DotnetRoot,
        string DotnetHostPath,
        SdkDeclaration Sdk,
        VersionedRootDeclaration HostFxr,
        VersionedRootDeclaration HostRuntime,
        ReferencePackDeclaration ReferencePack,
        ContractDeclaration Contracts);

    private sealed record SdkDeclaration(
        string Version,
        string Root,
        string EntryPath,
        string BundledVersionsPath,
        string RuntimeConfigPath);

    private sealed record VersionedRootDeclaration(
        string Version,
        string Root);

    private sealed record ReferencePackDeclaration(
        string Name,
        string Version,
        string Root,
        string AssembliesRoot);

    private sealed record ContractDeclaration(
        string RuntimePath,
        string EditorPath);

    private sealed record ResolvedInventory(
        VerifiedEditorImageFile DotnetHost,
        VerifiedManagedBuildEnvironmentTree Sdk,
        VerifiedManagedBuildEnvironmentTree HostFxr,
        VerifiedManagedBuildEnvironmentTree HostRuntime,
        VerifiedManagedBuildEnvironmentTree ReferencePack,
        VerifiedEditorImageFile RuntimeContract,
        VerifiedEditorImageFile EditorContract,
        IReadOnlyList<VerifiedEditorImageFile> AllSelectedFiles);
}
