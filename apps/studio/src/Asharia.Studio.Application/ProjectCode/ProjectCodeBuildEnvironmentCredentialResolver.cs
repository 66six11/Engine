using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using System.Xml.Linq;
using Asharia.Studio.Application.Bootstrap.Distribution;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeBuildEnvironmentCredentialResolver
{
    private const int CopyBufferSize = 1024 * 1024;
    private const int MaxClosedTreeEntries = 131_072;
    private const long MaxSemanticFileBytes = 64L * 1024 * 1024;
    private const long MaxSdkMetadataBytes = 4L * 1024 * 1024;
    private const string SupportedPlatform = "com.asharia.platform.windows";
    private const string SupportedArchitecture = "x86_64";
    private const string SupportedRuntimeIdentifier = "win-x64";
    private const string DotnetSdkPublicKeyToken = "adb9793829ddae60";
    private const string CoreLibraryPublicKeyToken = "7cec85d7bea7798e";
    private const string FrameworkReferencePublicKeyToken =
        "b03f5f7f11d50a3a";
    private static readonly Version StudioManagedAssemblyVersion =
        new(1, 0, 0, 0);
    private static readonly Version Net10FrameworkAssemblyVersion =
        new(10, 0, 0, 0);
    private static readonly UTF8Encoding Utf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private static readonly StringComparer FileSystemPathComparer =
        OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal;
    private static readonly IComparer<string> Utf8Comparer =
        Comparer<string>.Create(CompareUtf8);

    public static async Task<ProjectCodeBuildEnvironmentCredentialResolveResult>
        ResolveAsync(
            VerifiedManagedBuildEnvironmentLease sourceLease,
            CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sourceLease);
        var diagnostics = new List<ProjectCodeBuildEnvironmentDiagnostic>();
        if (!sourceLease.IsCurrent)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.projection-not-current",
                "projectionId",
                "The managed build environment projection lease is no longer current."));
            return Failure(diagnostics);
        }

        var projection = sourceLease.Projection;
        if (!string.Equals(
                projection.TargetPlatform,
                SupportedPlatform,
                StringComparison.Ordinal)
            || !string.Equals(
                projection.TargetArchitecture,
                SupportedArchitecture,
                StringComparison.Ordinal))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.context-unsupported",
                "context",
                "The current semantic credential supports only the producer's Windows x64 Editor Image contract."));
            return Failure(diagnostics);
        }

        var closureBefore = CaptureDotnetClosure(projection, diagnostics);
        if (closureBefore is null)
        {
            return Failure(diagnostics);
        }

        var semanticPaths = GetSemanticPaths(projection);
        var captures = new Dictionary<string, CapturedFile>(
            StringComparer.Ordinal);
        foreach (var evidence in projection.SelectedFiles)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!sourceLease.TryGetCurrentFile(
                    evidence.RelativePath,
                    out var currentEvidence)
                || currentEvidence is null
                || !HasSameEvidence(evidence, currentEvidence))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build-environment.projection-not-current",
                    evidence.RelativePath,
                    "Selected file evidence is no longer available from the current projection lease."));
                return Failure(diagnostics);
            }

            var includeContents = semanticPaths.Contains(
                evidence.RelativePath);
            var contentLimit = IsSdkMetadataPath(
                projection,
                evidence.RelativePath)
                    ? MaxSdkMetadataBytes
                    : MaxSemanticFileBytes;
            if (includeContents && evidence.Size > contentLimit)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build-environment.semantic-file-budget-exceeded",
                    evidence.RelativePath,
                    "Semantic identity input exceeds its bounded in-memory inspection limit."));
                return Failure(diagnostics);
            }

            var captured = await CaptureFileAsync(
                evidence,
                includeContents,
                cancellationToken).ConfigureAwait(false);
            if (captured is null)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build-environment.file-evidence-mismatch",
                    evidence.RelativePath,
                    "Selected file bytes no longer match the managed environment projection."));
                return Failure(diagnostics);
            }

            captures.Add(evidence.RelativePath, captured);
        }

        var sdk = CreateTreeSnapshot(projection.Sdk, captures);
        var hostFxr = CreateTreeSnapshot(projection.HostFxr, captures);
        var hostRuntime = CreateTreeSnapshot(
            projection.HostRuntime,
            captures);
        var referencePack = CreateTreeSnapshot(
            projection.ReferencePack,
            captures);
        var dotnetHost = captures[projection.DotnetHost.RelativePath]
            .Snapshot;
        var hostFxrEntryPath =
            projection.HostFxr.RelativeRoot + "/hostfxr.dll";
        if (!ValidateNativePe(
                RequiredContents(captures, projection.DotnetHost.RelativePath),
                expectDll: false)
            || !ValidateNativePe(
                RequiredContents(captures, hostFxrEntryPath),
                expectDll: true))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.native-host-invalid",
                "managed/dotnet",
                "The dotnet host and hostfxr entry must be native Windows x64 PE32+ executable/DLL images."));
            return Failure(diagnostics);
        }

        if (!ValidateSdkEvidence(projection, captures, diagnostics))
        {
            return Failure(diagnostics);
        }

        var sdkEntryPath = projection.Sdk.RelativeRoot + "/dotnet.dll";
        var hostRuntimeCorePath = projection.HostRuntime.RelativeRoot
            + "/System.Private.CoreLib.dll";
        var referenceSystemRuntimePath =
            projection.ReferencePack.RelativeRoot
            + "/ref/net10.0/System.Runtime.dll";
        var sdkEntry = ReadManagedAssembly(
            sdkEntryPath,
            RequiredContents(captures, sdkEntryPath),
            diagnostics);
        var hostRuntimeCore = ReadManagedAssembly(
            hostRuntimeCorePath,
            RequiredContents(captures, hostRuntimeCorePath),
            diagnostics);
        if (sdkEntry is null
            || hostRuntimeCore is null
            || !HasExactIdentity(
                sdkEntry.Identity,
                "dotnet",
                Version.Parse(projection.SdkVersion + ".0"),
                DotnetSdkPublicKeyToken)
            || !HasExactIdentity(
                hostRuntimeCore.Identity,
                "System.Private.CoreLib",
                Net10FrameworkAssemblyVersion,
                CoreLibraryPublicKeyToken))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.runtime-identity-mismatch",
                "managed/dotnet",
                "SDK entry and Host runtime core CLR identities must match the declared .NET 10 environment."));
            return Failure(diagnostics);
        }

        var frameworkAssemblies = ReadFrameworkAssemblies(
            projection,
            captures,
            diagnostics);
        if (frameworkAssemblies is null)
        {
            return Failure(diagnostics);
        }

        var systemRuntime = frameworkAssemblies.SingleOrDefault(assembly =>
            string.Equals(
                assembly.Path,
                referenceSystemRuntimePath,
                StringComparison.Ordinal));
        if (systemRuntime is null
            || !HasExactIdentity(
                systemRuntime.Metadata.Identity,
                "System.Runtime",
                Net10FrameworkAssemblyVersion,
                FrameworkReferencePublicKeyToken))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.framework-set-invalid",
                projection.ReferencePack.RelativeRoot,
                "Reference assembly set must contain the exact .NET 10 System.Runtime identity."));
            return Failure(diagnostics);
        }

        var runtimeContract = ReadContract(
            projection.RuntimeContract.RelativePath,
            "Asharia.Runtime.Contracts",
            captures,
            diagnostics);
        var editorContract = ReadContract(
            projection.EditorContract.RelativePath,
            "Asharia.Editor",
            captures,
            diagnostics);
        if (runtimeContract is null || editorContract is null)
        {
            return Failure(diagnostics);
        }

        var frameworkReferences = frameworkAssemblies
            .Select(assembly => assembly.Metadata.Identity)
            .OrderBy(identity => identity.SimpleName, Utf8Comparer)
            .ToArray();
        if (frameworkReferences
                .GroupBy(
                    identity => identity.SimpleName,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1)
            || frameworkReferences.Any(identity =>
                string.Equals(
                    identity.SimpleName,
                    runtimeContract.Identity.SimpleName,
                    StringComparison.OrdinalIgnoreCase)
                || string.Equals(
                    identity.SimpleName,
                    editorContract.Identity.SimpleName,
                    StringComparison.OrdinalIgnoreCase))
            || !ValidateContractClosure(
                runtimeContract,
                editorContract,
                frameworkReferences,
                diagnostics))
        {
            return Failure(diagnostics);
        }

        var closureAfter = CaptureDotnetClosure(projection, diagnostics);
        if (closureAfter is null
            || !closureBefore.SequenceEqual(
                closureAfter,
                StringComparer.Ordinal)
            || !sourceLease.IsCurrent)
        {
            if (diagnostics.Count == 0)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build-environment.selection-changed",
                    projection.DotnetHost.RelativePath,
                    "The exact managed build environment changed during semantic credential issuance."));
            }

            return Failure(diagnostics);
        }

        var referenceAssembliesRoot = Path.Combine(
            referencePack.AbsoluteRoot,
            "ref",
            "net10.0");
        var credentialId = ComputeCredentialId(
            projection,
            sdkEntry.Identity,
            hostRuntimeCore.Identity,
            runtimeContract,
            editorContract,
            frameworkReferences);
        var credential = new ProjectCodeBuildEnvironmentCredential(
            credentialId,
            projection.EngineGenerationId,
            projection.EnvironmentId,
            projection.TargetFramework,
            projection.TargetPlatform,
            projection.TargetArchitecture,
            projection.ProjectionId,
            projection.SdkVersion,
            projection.HostFxrVersion,
            projection.HostRuntimeVersion,
            projection.ReferencePackVersion,
            dotnetHost,
            sdk,
            hostFxr,
            hostRuntime,
            referencePack,
            referenceAssembliesRoot,
            sdkEntry.Identity,
            hostRuntimeCore.Identity,
            runtimeContract,
            editorContract,
            Array.AsReadOnly(frameworkReferences));
        return ProjectCodeBuildEnvironmentCredentialResolveResult.Success(
            new ProjectCodeBuildEnvironmentCredentialLease(
                sourceLease,
                credential));
    }

    public static async Task<bool> IsExecutionSelectionCurrentAsync(
        ProjectCodeBuildEnvironmentCredentialLease lease,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(lease);
        if (!lease.IsCurrent)
        {
            return false;
        }

        var diagnostics = new List<ProjectCodeBuildEnvironmentDiagnostic>();
        var projection = lease.SourceLease.Projection;
        var closureBefore = CaptureDotnetClosure(projection, diagnostics);
        if (closureBefore is null)
        {
            return false;
        }

        foreach (var evidence in projection.SelectedFiles)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!lease.TryGetCurrentFile(
                    evidence.RelativePath,
                    out var currentEvidence)
                || currentEvidence is null
                || !HasSameEvidence(evidence, currentEvidence)
                || await CaptureFileAsync(
                        evidence,
                        includeContents: false,
                        cancellationToken).ConfigureAwait(false) is null)
            {
                return false;
            }
        }

        var closureAfter = CaptureDotnetClosure(projection, diagnostics);
        return lease.IsCurrent
            && closureAfter is not null
            && closureBefore.SequenceEqual(
                closureAfter,
                StringComparer.Ordinal);
    }

    private static ProjectCodeBuildEnvironmentCredentialResolveResult Failure(
        IReadOnlyList<ProjectCodeBuildEnvironmentDiagnostic> diagnostics) =>
        ProjectCodeBuildEnvironmentCredentialResolveResult.Failure(
            diagnostics);

    private static HashSet<string> GetSemanticPaths(
        VerifiedManagedBuildEnvironmentProjection projection)
    {
        var paths = new HashSet<string>(StringComparer.Ordinal)
        {
            projection.DotnetHost.RelativePath,
            projection.Sdk.RelativeRoot + "/dotnet.dll",
            projection.Sdk.RelativeRoot
                + "/Microsoft.NETCoreSdk.BundledVersions.props",
            projection.Sdk.RelativeRoot + "/dotnet.runtimeconfig.json",
            projection.HostFxr.RelativeRoot + "/hostfxr.dll",
            projection.HostRuntime.RelativeRoot
                + "/System.Private.CoreLib.dll",
            projection.RuntimeContract.RelativePath,
            projection.EditorContract.RelativePath,
        };
        var referencePrefix =
            projection.ReferencePack.RelativeRoot + "/ref/net10.0/";
        foreach (var file in projection.ReferencePack.Files.Where(file =>
            IsDirectDll(file.RelativePath, referencePrefix)))
        {
            paths.Add(file.RelativePath);
        }

        return paths;
    }

    private static bool IsSdkMetadataPath(
        VerifiedManagedBuildEnvironmentProjection projection,
        string relativePath) =>
        string.Equals(
            relativePath,
            projection.Sdk.RelativeRoot
                + "/Microsoft.NETCoreSdk.BundledVersions.props",
            StringComparison.Ordinal)
        || string.Equals(
            relativePath,
            projection.Sdk.RelativeRoot + "/dotnet.runtimeconfig.json",
            StringComparison.Ordinal);

    private static ProjectCodeBuildEnvironmentTreeSnapshot CreateTreeSnapshot(
        VerifiedManagedBuildEnvironmentTree tree,
        IReadOnlyDictionary<string, CapturedFile> captures)
    {
        var files = tree.Files
            .Select(file =>
            {
                var snapshot = captures[file.RelativePath].Snapshot;
                return new ProjectCodeBuildEnvironmentFileSnapshot(
                    file.RelativePath[(tree.RelativeRoot.Length + 1)..],
                    snapshot.AbsolutePath,
                    snapshot.Size,
                    snapshot.Sha256);
            })
            .OrderBy(file => file.RelativePath, Utf8Comparer)
            .ToArray();
        long totalSize = 0;
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var file in files)
        {
            totalSize = checked(totalSize + file.Size);
            AppendString(hash, file.RelativePath);
            AppendEnvelope(hash, file.Size, file.Sha256);
        }

        return new ProjectCodeBuildEnvironmentTreeSnapshot(
            tree.RelativeRoot,
            tree.AbsoluteRoot,
            totalSize,
            Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant(),
            Array.AsReadOnly(files));
    }

    private static string[]? CaptureDotnetClosure(
        VerifiedManagedBuildEnvironmentProjection projection,
        ICollection<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        var dotnetRoot = Path.GetDirectoryName(
            projection.DotnetHost.AbsolutePath);
        if (string.IsNullOrEmpty(dotnetRoot))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.dotnet-closure-invalid",
                projection.DotnetHost.RelativePath,
                "dotnet host must have one exact installation root."));
            return null;
        }

        try
        {
            var root = Path.TrimEndingDirectorySeparator(
                Path.GetFullPath(dotnetRoot));
            if (!Directory.Exists(root) || HasReparsePointInPath(root))
            {
                throw new IOException();
            }

            var expected = projection.SelectedFiles
                .Where(file => IsSameOrDescendant(
                    file.RelativePath,
                    "managed/dotnet"))
                .Select(file => file.RelativePath["managed/dotnet/".Length..])
                .SelectMany(EnumerateExpectedClosureEntries)
                .Distinct(StringComparer.Ordinal)
                .OrderBy(path => path, Utf8Comparer)
                .ToArray();
            var actual = EnumerateClosedEntries(root);
            if (!expected.SequenceEqual(actual, StringComparer.Ordinal))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build-environment.dotnet-closure-invalid",
                    "managed/dotnet",
                    "Exact dotnet root contains an unregistered, missing, linked, or non-regular entry."));
                return null;
            }

            return actual;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.dotnet-closure-invalid",
                "managed/dotnet",
                "Exact dotnet root could not be enumerated as one closed regular-file inventory."));
            return null;
        }
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

    private static string[] EnumerateClosedEntries(string root)
    {
        var directories = new Stack<string>();
        directories.Push(root);
        var entries = new List<string>();
        var entryCount = 0;
        while (directories.TryPop(out var directory))
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(
                directory))
            {
                if (++entryCount > MaxClosedTreeEntries)
                {
                    throw new IOException(
                        "dotnet closure entry budget exceeded.");
                }

                var attributes = File.GetAttributes(entry);
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                {
                    throw new IOException(
                        "dotnet closure contains a reparse point.");
                }

                if ((attributes & FileAttributes.Directory) != 0)
                {
                    var relativeDirectory = Path.GetRelativePath(root, entry)
                        .Replace(Path.DirectorySeparatorChar, '/');
                    if (!IsPortableRelativePath(relativeDirectory))
                    {
                        throw new IOException(
                            "dotnet closure contains a non-portable directory.");
                    }

                    entries.Add("d/" + relativeDirectory);
                    directories.Push(entry);
                    continue;
                }

                if (!File.Exists(entry))
                {
                    throw new IOException(
                        "dotnet closure contains a special entry.");
                }

                var relativePath = Path.GetRelativePath(root, entry)
                    .Replace(Path.DirectorySeparatorChar, '/');
                if (!IsPortableRelativePath(relativePath))
                {
                    throw new IOException(
                        "dotnet closure contains a non-portable path.");
                }

                entries.Add("f/" + relativePath);
            }
        }

        return entries.OrderBy(path => path, Utf8Comparer).ToArray();
    }

    private static async Task<CapturedFile?> CaptureFileAsync(
        VerifiedEditorImageFile evidence,
        bool includeContents,
        CancellationToken cancellationToken)
    {
        try
        {
            if (evidence.Size < 0
                || HasReparsePointInPath(evidence.AbsolutePath))
            {
                return null;
            }

            var before = new FileInfo(evidence.AbsolutePath);
            var beforeLength = before.Length;
            var beforeWrite = before.LastWriteTimeUtc;
            if (beforeLength != evidence.Size
                || (includeContents
                    && beforeLength > MaxSemanticFileBytes))
            {
                return null;
            }

            var contents = includeContents
                ? new byte[checked((int)beforeLength)]
                : null;
            var buffer = ArrayPool<byte>.Shared.Rent(CopyBufferSize);
            try
            {
                using var hash = IncrementalHash.CreateHash(
                    HashAlgorithmName.SHA256);
                await using var stream = new FileStream(
                    evidence.AbsolutePath,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.Read,
                    CopyBufferSize,
                    FileOptions.Asynchronous
                        | FileOptions.SequentialScan);
                if (stream.Length != beforeLength)
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

                    if (read > beforeLength - total)
                    {
                        return null;
                    }

                    hash.AppendData(buffer, 0, read);
                    if (contents is not null)
                    {
                        buffer.AsSpan(0, read).CopyTo(
                            contents.AsSpan(checked((int)total), read));
                    }

                    total = checked(total + read);
                }

                var sha256 = Convert.ToHexString(hash.GetHashAndReset())
                    .ToLowerInvariant();
                before.Refresh();
                if (total != beforeLength
                    || !before.Exists
                    || before.Length != beforeLength
                    || before.LastWriteTimeUtc != beforeWrite
                    || HasReparsePointInPath(evidence.AbsolutePath)
                    || !string.Equals(
                        sha256,
                        evidence.Sha256,
                        StringComparison.Ordinal))
                {
                    return null;
                }

                return new CapturedFile(
                    new ProjectCodeBuildEnvironmentFileSnapshot(
                        evidence.RelativePath,
                        evidence.AbsolutePath,
                        beforeLength,
                        sha256),
                    contents);
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

    private static bool ValidateSdkEvidence(
        VerifiedManagedBuildEnvironmentProjection projection,
        IReadOnlyDictionary<string, CapturedFile> captures,
        ICollection<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        var propsPath = projection.Sdk.RelativeRoot
            + "/Microsoft.NETCoreSdk.BundledVersions.props";
        var runtimeConfigPath =
            projection.Sdk.RelativeRoot + "/dotnet.runtimeconfig.json";
        try
        {
            var settings = new XmlReaderSettings
            {
                DtdProcessing = DtdProcessing.Prohibit,
                MaxCharactersInDocument = MaxSdkMetadataBytes,
                XmlResolver = null,
            };
            using var propsStream = new MemoryStream(
                RequiredContents(captures, propsPath),
                writable: false);
            using var reader = XmlReader.Create(propsStream, settings);
            var document = XDocument.Load(reader, LoadOptions.None);
            EnsureClosedSdkDocument(document);
            var runtimeVersion = ParseStableVersion(
                projection.HostRuntimeVersion);
            var expectedTargetFrameworkVersion =
                $"{runtimeVersion.Major}.{runtimeVersion.Minor}";
            if (!string.Equals(
                    SingleProperty(
                        document,
                        "BundledNETCoreAppTargetFrameworkVersion"),
                    expectedTargetFrameworkVersion,
                    StringComparison.Ordinal)
                || !string.Equals(
                    SingleProperty(
                        document,
                        "BundledNETCoreAppPackageVersion"),
                    projection.HostRuntimeVersion,
                    StringComparison.Ordinal)
                || !string.Equals(
                    SingleProperty(document, "NETCoreSdkVersion"),
                    projection.SdkVersion,
                    StringComparison.Ordinal)
                || !string.Equals(
                    SingleProperty(
                        document,
                        "NETCoreSdkRuntimeIdentifier"),
                    SupportedRuntimeIdentifier,
                    StringComparison.Ordinal)
                || !string.Equals(
                    SingleProperty(
                        document,
                        "NETCoreSdkPortableRuntimeIdentifier"),
                    SupportedRuntimeIdentifier,
                    StringComparison.Ordinal)
                || !HasExactKnownFrameworkReference(
                    document,
                    projection))
            {
                throw new InvalidDataException();
            }
        }
        catch (Exception error) when (
            error is ArgumentException
                or InvalidDataException
                or IOException
                or XmlException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.sdk-metadata-invalid",
                propsPath,
                "SDK bundled metadata must bind the declared SDK, runtime, win-x64 RID, and net10.0 reference pack without imports or conditions."));
            return false;
        }

        try
        {
            using var document = JsonDocument.Parse(
                RequiredContents(captures, runtimeConfigPath),
                new JsonDocumentOptions
                {
                    AllowTrailingCommas = false,
                    CommentHandling = JsonCommentHandling.Disallow,
                    MaxDepth = 32,
                });
            EnsureNoDuplicateJsonProperties(document.RootElement);
            var runtimeOptions = RequiredObject(
                document.RootElement,
                "runtimeOptions");
            if (runtimeOptions.TryGetProperty("rollForward", out _)
                || runtimeOptions.TryGetProperty("applyPatches", out _)
                || runtimeOptions.TryGetProperty(
                    "rollForwardOnNoCandidateFx",
                    out _)
                || !string.Equals(
                    RequiredString(runtimeOptions, "tfm"),
                    projection.TargetFramework,
                    StringComparison.Ordinal))
            {
                throw new InvalidDataException();
            }

            var framework = RequiredObject(runtimeOptions, "framework");
            if (!string.Equals(
                    RequiredString(framework, "name"),
                    "Microsoft.NETCore.App",
                    StringComparison.Ordinal)
                || !string.Equals(
                    RequiredString(framework, "version"),
                    projection.HostRuntimeVersion,
                    StringComparison.Ordinal))
            {
                throw new InvalidDataException();
            }
        }
        catch (Exception error) when (
            error is InvalidDataException or JsonException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.sdk-runtime-config-invalid",
                runtimeConfigPath,
                "SDK runtimeconfig must bind the declared net10.0 Host runtime without roll-forward overrides or duplicate properties."));
            return false;
        }

        return true;
    }

    private static void EnsureClosedSdkDocument(XDocument document)
    {
        var root = document.Root;
        if (root is null
            || !string.Equals(
                root.Name.LocalName,
                "Project",
                StringComparison.Ordinal)
            || root.Attributes().Any(attribute => string.Equals(
                attribute.Name.LocalName,
                "Sdk",
                StringComparison.OrdinalIgnoreCase))
            || root.Descendants().Any(element =>
                string.Equals(
                    element.Name.LocalName,
                    "Import",
                    StringComparison.OrdinalIgnoreCase)
                || string.Equals(
                    element.Name.LocalName,
                    "Sdk",
                    StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidDataException();
        }
    }

    private static string SingleProperty(
        XDocument document,
        string localName)
    {
        var root = document.Root ?? throw new InvalidDataException();
        var properties = root.Descendants()
            .Where(element => string.Equals(
                element.Name.LocalName,
                localName,
                StringComparison.OrdinalIgnoreCase))
            .Where(element => string.Equals(
                element.Parent?.Name.LocalName,
                "PropertyGroup",
                StringComparison.Ordinal))
            .ToArray();
        if (properties.Length != 1)
        {
            throw new InvalidDataException();
        }

        var property = properties[0];
        var group = property.Parent;
        if (group is null
            || group.Parent != root
            || group.Attribute("Condition") is not null
            || property.HasAttributes
            || property.HasElements
            || string.IsNullOrWhiteSpace(property.Value))
        {
            throw new InvalidDataException();
        }

        return property.Value.Trim();
    }

    private static bool HasExactKnownFrameworkReference(
        XDocument document,
        VerifiedManagedBuildEnvironmentProjection projection)
    {
        var matches = document.Descendants()
            .Where(element => string.Equals(
                element.Name.LocalName,
                "KnownFrameworkReference",
                StringComparison.Ordinal))
            .Where(element => string.Equals(
                    (string?)element.Attribute("Include"),
                    "Microsoft.NETCore.App",
                    StringComparison.Ordinal)
                && string.Equals(
                    (string?)element.Attribute("TargetFramework"),
                    projection.TargetFramework,
                    StringComparison.Ordinal))
            .ToArray();
        return matches.Length == 1
            && matches[0].Attribute("Condition") is null
            && matches[0].Parent?.Attribute("Condition") is null
            && string.Equals(
                (string?)matches[0].Attribute("RuntimeFrameworkName"),
                "Microsoft.NETCore.App",
                StringComparison.Ordinal)
            && string.Equals(
                (string?)matches[0].Attribute("LatestRuntimeFrameworkVersion"),
                projection.HostRuntimeVersion,
                StringComparison.Ordinal)
            && string.Equals(
                (string?)matches[0].Attribute("TargetingPackName"),
                "Microsoft.NETCore.App.Ref",
                StringComparison.Ordinal)
            && string.Equals(
                (string?)matches[0].Attribute("TargetingPackVersion"),
                projection.ReferencePackVersion,
                StringComparison.Ordinal);
    }

    private static JsonElement RequiredObject(
        JsonElement value,
        string propertyName)
    {
        if (value.ValueKind != JsonValueKind.Object
            || !value.TryGetProperty(propertyName, out var property)
            || property.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException();
        }

        return property;
    }

    private static string RequiredString(
        JsonElement value,
        string propertyName)
    {
        if (value.ValueKind != JsonValueKind.Object
            || !value.TryGetProperty(propertyName, out var property)
            || property.ValueKind != JsonValueKind.String)
        {
            throw new InvalidDataException();
        }

        return property.GetString() ?? throw new InvalidDataException();
    }

    private static void EnsureNoDuplicateJsonProperties(JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in value.EnumerateObject())
            {
                if (!names.Add(property.Name))
                {
                    throw new InvalidDataException();
                }

                EnsureNoDuplicateJsonProperties(property.Value);
            }
        }
        else if (value.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in value.EnumerateArray())
            {
                EnsureNoDuplicateJsonProperties(item);
            }
        }
    }

    private static Version ParseStableVersion(string value)
    {
        var parts = value.Split('.');
        if (parts.Length != 3
            || !Version.TryParse(value, out var parsed)
            || parsed.Major.ToString() != parts[0]
            || parsed.Minor.ToString() != parts[1]
            || parsed.Build.ToString() != parts[2])
        {
            throw new InvalidDataException();
        }

        return parsed;
    }

    private static IReadOnlyList<FrameworkAssembly>? ReadFrameworkAssemblies(
        VerifiedManagedBuildEnvironmentProjection projection,
        IReadOnlyDictionary<string, CapturedFile> captures,
        ICollection<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        var prefix =
            projection.ReferencePack.RelativeRoot + "/ref/net10.0/";
        var assemblies = new List<FrameworkAssembly>();
        foreach (var file in projection.ReferencePack.Files.Where(file =>
            IsDirectDll(file.RelativePath, prefix)))
        {
            var metadata = ReadManagedAssembly(
                file.RelativePath,
                RequiredContents(captures, file.RelativePath),
                diagnostics);
            if (metadata is null)
            {
                return null;
            }

            var expectedSimpleName =
                Path.GetFileNameWithoutExtension(file.RelativePath);
            if (!string.Equals(
                    metadata.Identity.SimpleName,
                    expectedSimpleName,
                    StringComparison.Ordinal)
                || !string.Equals(
                    metadata.Identity.Culture,
                    "neutral",
                    StringComparison.Ordinal)
                || string.Equals(
                    metadata.Identity.PublicKeyToken,
                    "null",
                    StringComparison.Ordinal))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.build-environment.framework-set-invalid",
                    file.RelativePath,
                    "Each direct reference DLL must bind its exact file name to one neutral strong-named CLR identity."));
                return null;
            }

            assemblies.Add(new FrameworkAssembly(
                file.RelativePath,
                metadata));
        }

        if (assemblies.Count == 0
            || assemblies
                .GroupBy(
                    assembly => assembly.Metadata.Identity.SimpleName,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.framework-set-invalid",
                projection.ReferencePack.RelativeRoot,
                "Reference pack must expose one non-empty set of unique direct net10.0 CLR identities."));
            return null;
        }

        return assemblies;
    }

    private static ProjectCodeContractFileBinding? ReadContract(
        string path,
        string expectedSimpleName,
        IReadOnlyDictionary<string, CapturedFile> captures,
        ICollection<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        var metadata = ReadManagedAssembly(
            path,
            RequiredContents(captures, path),
            diagnostics);
        if (metadata is null
            || !HasExactIdentity(
                metadata.Identity,
                expectedSimpleName,
                StudioManagedAssemblyVersion,
                "null"))
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.contract-identity-mismatch",
                path,
                "Host contract CLR identity does not match its fixed Distribution role."));
            return null;
        }

        return new ProjectCodeContractFileBinding(
            metadata.Identity,
            metadata.References,
            captures[path].Snapshot);
    }

    private static bool ValidateContractClosure(
        ProjectCodeContractFileBinding runtimeContract,
        ProjectCodeContractFileBinding editorContract,
        IReadOnlyList<ProjectCodeAssemblyIdentity> frameworkReferences,
        ICollection<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        var known = frameworkReferences
            .Concat(
            [
                runtimeContract.Identity,
                editorContract.Identity,
            ])
            .ToDictionary(
                identity => identity.SimpleName,
                StringComparer.OrdinalIgnoreCase);
        foreach (var contract in new[] { runtimeContract, editorContract })
        {
            foreach (var reference in contract.References)
            {
                if (!known.TryGetValue(
                        reference.SimpleName,
                        out var expected)
                    || !expected.HasSameBindingIdentity(reference))
                {
                    diagnostics.Add(Diagnostic(
                        "project-code.build-environment.contract-reference-closure-invalid",
                        contract.File.RelativePath,
                        $"Contract reference '{reference.FullName}' is outside the fixed framework/Host contract closure."));
                    return false;
                }
            }
        }

        return true;
    }

    private static ManagedAssemblyMetadata? ReadManagedAssembly(
        string path,
        byte[] contents,
        ICollection<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        try
        {
            using var stream = new MemoryStream(contents, writable: false);
            using var peReader = new PEReader(
                stream,
                PEStreamOptions.LeaveOpen);
            if (!peReader.HasMetadata
                || peReader.PEHeaders.CorHeader is null)
            {
                throw new BadImageFormatException();
            }

            var reader = peReader.GetMetadataReader(
                MetadataReaderOptions.None);
            if (!reader.IsAssembly)
            {
                throw new BadImageFormatException();
            }

            var definition = reader.GetAssemblyDefinition();
            var identity = ReadDefinitionIdentity(reader, definition);
            var references = reader.AssemblyReferences
                .Select(handle => ReadReferenceIdentity(
                    reader,
                    reader.GetAssemblyReference(handle)))
                .OrderBy(reference => reference.SimpleName, Utf8Comparer)
                .ToArray();
            if (references
                .GroupBy(
                    reference => reference.SimpleName,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
            {
                throw new InvalidDataException();
            }

            return new ManagedAssemblyMetadata(
                identity,
                Array.AsReadOnly(references));
        }
        catch (Exception error) when (
            error is ArgumentException
                or BadImageFormatException
                or InvalidDataException
                or InvalidOperationException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.build-environment.managed-metadata-invalid",
                path,
                "Semantic credential input is not one readable managed assembly with a valid CLR identity."));
            return null;
        }
    }

    private static ProjectCodeAssemblyIdentity ReadDefinitionIdentity(
        MetadataReader reader,
        AssemblyDefinition definition)
    {
        var publicKey = reader.GetBlobBytes(definition.PublicKey);
        var hasPublicKey =
            (definition.Flags & AssemblyFlags.PublicKey) != 0;
        if (hasPublicKey != (publicKey.Length != 0))
        {
            throw new InvalidDataException();
        }

        return new ProjectCodeAssemblyIdentity(
            reader.GetString(definition.Name),
            definition.Version,
            definition.Culture.IsNil
                ? "neutral"
                : NormalizeCulture(reader.GetString(definition.Culture)),
            hasPublicKey ? ComputePublicKeyToken(publicKey) : "null");
    }

    private static ProjectCodeAssemblyIdentity ReadReferenceIdentity(
        MetadataReader reader,
        AssemblyReference reference)
    {
        var keyOrToken = reader.GetBlobBytes(reference.PublicKeyOrToken);
        var hasPublicKey =
            (reference.Flags & AssemblyFlags.PublicKey) != 0;
        var token = keyOrToken.Length == 0
            ? "null"
            : hasPublicKey
                ? ComputePublicKeyToken(keyOrToken)
                : Convert.ToHexString(keyOrToken).ToLowerInvariant();
        return new ProjectCodeAssemblyIdentity(
            reader.GetString(reference.Name),
            reference.Version,
            reference.Culture.IsNil
                ? "neutral"
                : NormalizeCulture(reader.GetString(reference.Culture)),
            token);
    }

    private static string NormalizeCulture(string culture) =>
        string.IsNullOrEmpty(culture) ? "neutral" : culture;

    private static string ComputePublicKeyToken(byte[] publicKey)
    {
#pragma warning disable CA5350 // CLR public-key tokens are defined using SHA-1.
        var hash = SHA1.HashData(publicKey);
#pragma warning restore CA5350
        var token = hash[^8..];
        Array.Reverse(token);
        return Convert.ToHexString(token).ToLowerInvariant();
    }

    private static bool HasExactIdentity(
        ProjectCodeAssemblyIdentity identity,
        string simpleName,
        Version version,
        string publicKeyToken) =>
        string.Equals(
            identity.SimpleName,
            simpleName,
            StringComparison.Ordinal)
        && identity.Version == version
        && string.Equals(
            identity.Culture,
            "neutral",
            StringComparison.Ordinal)
        && string.Equals(
            identity.PublicKeyToken,
            publicKeyToken,
            StringComparison.Ordinal);

    private static bool ValidateNativePe(
        byte[] contents,
        bool expectDll)
    {
        try
        {
            using var stream = new MemoryStream(contents, writable: false);
            using var reader = new PEReader(
                stream,
                PEStreamOptions.LeaveOpen);
            var headers = reader.PEHeaders;
            var isDll =
                (headers.CoffHeader.Characteristics & Characteristics.Dll)
                    != 0;
            return headers.PEHeader is not null
                && !reader.HasMetadata
                && headers.CoffHeader.Machine == Machine.Amd64
                && headers.PEHeader.Magic == PEMagic.PE32Plus
                && isDll == expectDll;
        }
        catch (BadImageFormatException)
        {
            return false;
        }
    }

    private static byte[] RequiredContents(
        IReadOnlyDictionary<string, CapturedFile> captures,
        string path) =>
        captures.TryGetValue(path, out var captured)
            ? captured.Contents
                ?? throw new InvalidOperationException(
                    $"Semantic contents were not captured for '{path}'.")
            : throw new InvalidOperationException(
                $"Semantic path '{path}' is absent from the projection.");

    private static bool IsDirectDll(string path, string prefix)
    {
        if (!path.StartsWith(prefix, StringComparison.Ordinal))
        {
            return false;
        }

        var suffix = path[prefix.Length..];
        return !suffix.Contains('/', StringComparison.Ordinal)
            && suffix.EndsWith(".dll", StringComparison.OrdinalIgnoreCase);
    }

    private static bool HasSameEvidence(
        VerifiedEditorImageFile left,
        VerifiedEditorImageFile right) =>
        string.Equals(
            left.RelativePath,
            right.RelativePath,
            StringComparison.Ordinal)
        && IsSamePath(left.AbsolutePath, right.AbsolutePath)
        && left.Size == right.Size
        && string.Equals(
            left.Sha256,
            right.Sha256,
            StringComparison.Ordinal);

    private static bool IsSamePath(string? left, string? right) =>
        left is not null
        && right is not null
        && FileSystemPathComparer.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(left)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(right)));

    private static bool IsSameOrDescendant(string path, string root) =>
        string.Equals(path, root, StringComparison.Ordinal)
        || path.StartsWith(root + "/", StringComparison.Ordinal);

    private static bool IsPortableRelativePath(string value) =>
        !string.IsNullOrWhiteSpace(value)
        && value.Length <= 500
        && value.IsNormalized(NormalizationForm.FormC)
        && !value.StartsWith("/", StringComparison.Ordinal)
        && !value.Contains('\\')
        && !value.Contains(':')
        && !value.Any(char.IsControl)
        && !value.Split('/').Any(part => part is "" or "." or "..");

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

    private static string ComputeCredentialId(
        VerifiedManagedBuildEnvironmentProjection projection,
        ProjectCodeAssemblyIdentity sdkEntry,
        ProjectCodeAssemblyIdentity hostRuntimeCore,
        ProjectCodeContractFileBinding runtimeContract,
        ProjectCodeContractFileBinding editorContract,
        IReadOnlyList<ProjectCodeAssemblyIdentity> frameworkReferences)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(
            hash,
            "distribution-project-code-build-environment-credential-v1");
        AppendString(hash, projection.EngineGenerationId);
        AppendString(hash, projection.EnvironmentId);
        AppendString(hash, projection.TargetFramework);
        AppendString(hash, projection.TargetPlatform);
        AppendString(hash, projection.TargetArchitecture);
        AppendString(hash, projection.ProjectionId);
        AppendString(hash, sdkEntry.FullName);
        AppendString(hash, hostRuntimeCore.FullName);
        AppendContract(hash, runtimeContract);
        AppendContract(hash, editorContract);
        foreach (var identity in frameworkReferences)
        {
            AppendString(hash, identity.FullName);
        }

        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static void AppendContract(
        IncrementalHash hash,
        ProjectCodeContractFileBinding contract)
    {
        AppendString(hash, contract.Identity.FullName);
        AppendEnvelope(hash, contract.Size, contract.Sha256);
        foreach (var reference in contract.References)
        {
            AppendString(hash, reference.FullName);
        }
    }

    private static void AppendEnvelope(
        IncrementalHash hash,
        long size,
        string sha256)
    {
        Span<byte> sizeBytes = stackalloc byte[sizeof(long)];
        BinaryPrimitives.WriteInt64LittleEndian(sizeBytes, size);
        hash.AppendData(sizeBytes);
        hash.AppendData(Convert.FromHexString(sha256));
    }

    private static void AppendString(
        IncrementalHash hash,
        string value)
    {
        var bytes = Utf8.GetBytes(value);
        Span<byte> lengthBytes = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(
            lengthBytes,
            bytes.Length);
        hash.AppendData(lengthBytes);
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

    private static ProjectCodeBuildEnvironmentDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    private sealed record CapturedFile(
        ProjectCodeBuildEnvironmentFileSnapshot Snapshot,
        byte[]? Contents);

    private sealed record ManagedAssemblyMetadata(
        ProjectCodeAssemblyIdentity Identity,
        IReadOnlyList<ProjectCodeAssemblyIdentity> References);

    private sealed record FrameworkAssembly(
        string Path,
        ManagedAssemblyMetadata Metadata);
}
