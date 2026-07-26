using System;
using System.Buffers;
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

internal sealed record VerifiedEditorImageFile(
    string RelativePath,
    string Role,
    string MediaType,
    long Size,
    string Sha256,
    string AbsolutePath);

internal sealed class VerifiedEditorImageInventoryLease
{
    private readonly IReadOnlyDictionary<string, VerifiedEditorImageFile> filesByPath_;
    private readonly object stateGate_ = new();
    private int isCurrent_ = 1;

    internal VerifiedEditorImageInventoryLease(
        string engineGenerationId,
        string generationRoot,
        string targetPlatform,
        string targetArchitecture,
        string entryPoint,
        IReadOnlyList<VerifiedEditorImageFile> files)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(engineGenerationId);
        ArgumentException.ThrowIfNullOrWhiteSpace(generationRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetPlatform);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetArchitecture);
        ArgumentException.ThrowIfNullOrWhiteSpace(entryPoint);
        ArgumentNullException.ThrowIfNull(files);

        var snapshot = files.ToArray();
        if (snapshot.Length == 0
            || snapshot.Any(file => file is null)
            || snapshot.GroupBy(
                    file => file.RelativePath,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Verified Editor Image inventory must contain unique files.",
                nameof(files));
        }

        EngineGenerationId = engineGenerationId;
        GenerationRoot = generationRoot;
        TargetPlatform = targetPlatform;
        TargetArchitecture = targetArchitecture;
        EntryPoint = entryPoint;
        Files = Array.AsReadOnly(snapshot);
        filesByPath_ = snapshot.ToDictionary(
            file => file.RelativePath,
            StringComparer.Ordinal);
    }

    public string EngineGenerationId { get; }

    public string GenerationRoot { get; }

    public string TargetPlatform { get; }

    public string TargetArchitecture { get; }

    public string EntryPoint { get; }

    public IReadOnlyList<VerifiedEditorImageFile> Files { get; }

    public bool IsCurrent => Volatile.Read(ref isCurrent_) != 0;

    internal bool TryGetCurrentFile(
        string relativePath,
        out VerifiedEditorImageFile? file)
    {
        lock (stateGate_)
        {
            file = null;
            return IsCurrent
                && !string.IsNullOrWhiteSpace(relativePath)
                && filesByPath_.TryGetValue(relativePath, out file);
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

internal sealed record VerifiedEditorImageInventoryDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class VerifiedEditorImageInventoryVerifyResult
{
    private VerifiedEditorImageInventoryVerifyResult(
        VerifiedEditorImageInventoryLease? lease,
        IReadOnlyList<VerifiedEditorImageInventoryDiagnostic> diagnostics)
    {
        Lease = lease;
        Diagnostics = diagnostics;
    }

    public VerifiedEditorImageInventoryLease? Lease { get; }

    public IReadOnlyList<VerifiedEditorImageInventoryDiagnostic> Diagnostics { get; }

    public bool Succeeded => Lease is not null && Diagnostics.Count == 0;

    public static VerifiedEditorImageInventoryVerifyResult Success(
        VerifiedEditorImageInventoryLease lease) =>
        new(lease, []);

    public static VerifiedEditorImageInventoryVerifyResult Failure(
        IReadOnlyList<VerifiedEditorImageInventoryDiagnostic> diagnostics) =>
        new(null, diagnostics);
}

internal static partial class EngineDistributionEditorImageVerifier
{
    private const string ManifestName = "asharia.engine-distribution.json";
    private const int MaxManifestBytes = 64 * 1024 * 1024;
    private const int MaxEditorFileCount = 65536;
    private const long MaxEditorImageBytes = 4L * 1024 * 1024 * 1024;
    private const int CopyBufferSize = 1024 * 1024;
    private static readonly Regex GenerationIdPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);
    private static readonly Regex DigestPattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);
    private static readonly Regex MediaTypePattern = new(
        "^[A-Za-z0-9][A-Za-z0-9!#$&^_.+\\-]*/[A-Za-z0-9][A-Za-z0-9!#$&^_.+\\-]*$",
        RegexOptions.CultureInvariant);
    private static readonly HashSet<string> FileRoles = new(
        [
            "executable",
            "runtime-library",
            "resource",
            "metadata",
            "debug-symbol",
        ],
        StringComparer.Ordinal);
    private static readonly HashSet<string> PythonPayloadExtensions = new(
        StringComparer.OrdinalIgnoreCase)
    {
        ".egg",
        ".pth",
        ".py",
        ".pyc",
        ".pyd",
        ".pyi",
        ".pyo",
        ".pyw",
        ".pyz",
        ".whl",
    };
    private static readonly HashSet<string> PythonPayloadSegments = new(
        StringComparer.OrdinalIgnoreCase)
    {
        ".venv",
        "__pycache__",
        "dist-packages",
        "graalpy",
        "ironpython",
        "jython",
        "pypy",
        "pypy3",
        "pythonnet",
        "site-packages",
        "venv",
        "virtualenv",
    };
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private static readonly JsonWriterOptions CanonicalWriterOptions = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        IndentCharacter = ' ',
        IndentSize = 2,
        Indented = true,
        NewLine = "\n",
    };
    private static readonly StringComparison FileSystemPathComparison =
        OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;

    public static async Task<VerifiedEditorImageInventoryVerifyResult> VerifyAsync(
        string expectedEngineGenerationId,
        string generationRoot,
        CancellationToken cancellationToken = default)
    {
        var diagnostics = new List<VerifiedEditorImageInventoryDiagnostic>();
        if (string.IsNullOrWhiteSpace(expectedEngineGenerationId)
            || !GenerationIdPattern.IsMatch(expectedEngineGenerationId))
        {
            diagnostics.Add(Diagnostic(
                "distribution.editor-image.expected-generation-invalid",
                "expectedEngineGenerationId",
                "Expected Engine generation identity must be one canonical SHA-256 ID."));
            return Failure(diagnostics);
        }

        var root = ResolveGenerationRoot(
            generationRoot,
            expectedEngineGenerationId,
            diagnostics);
        if (root is null)
        {
            return Failure(diagnostics);
        }

        var manifestPath = ResolveRegularFile(
            root,
            ManifestName,
            "manifest",
            diagnostics);
        if (manifestPath is null)
        {
            return Failure(diagnostics);
        }

        var manifestBytes = await ReadBoundedFileAsync(
            manifestPath,
            MaxManifestBytes,
            cancellationToken).ConfigureAwait(false);
        if (manifestBytes is null)
        {
            diagnostics.Add(Diagnostic(
                "distribution.editor-image.manifest-read-failed",
                ManifestName,
                "Distribution manifest must be one bounded stable regular file."));
            return Failure(diagnostics);
        }

        ParsedManifest parsed;
        try
        {
            parsed = ParseManifest(
                manifestBytes,
                expectedEngineGenerationId);
        }
        catch (ForbiddenPythonPayloadException error)
        {
            diagnostics.Add(Diagnostic(
                "distribution.editor-image.python-payload-forbidden",
                error.RelativePath,
                "Python is repository-only development tooling and must not be present in a product Editor Image."));
            return Failure(diagnostics);
        }
        catch (Exception error) when (
            error is ArgumentException
                or InvalidDataException
                or JsonException
                or DecoderFallbackException
                or OverflowException)
        {
            diagnostics.Add(Diagnostic(
                "distribution.editor-image.manifest-invalid",
                ManifestName,
                "Distribution manifest does not have the selected canonical v1 identity and Editor Image shape"
                    + (string.IsNullOrEmpty(error.Message)
                        ? "."
                        : $" ({error.Message}).")));
            return Failure(diagnostics);
        }

        var verifiedFiles = new List<VerifiedEditorImageFile>(parsed.Files.Count);
        long totalBytes = 0;
        foreach (var file in parsed.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                totalBytes = checked(totalBytes + file.Size);
            }
            catch (OverflowException)
            {
                totalBytes = long.MaxValue;
            }

            if (totalBytes > MaxEditorImageBytes)
            {
                diagnostics.Add(Diagnostic(
                    "distribution.editor-image.budget-exceeded",
                    "/editorImage/files",
                    "Verified Editor Image exceeds the supported byte budget."));
                return Failure(diagnostics);
            }

            var absolutePath = ResolveRegularFile(
                root,
                file.RelativePath,
                $"/editorImage/files/{verifiedFiles.Count}/path",
                diagnostics);
            if (absolutePath is null)
            {
                return Failure(diagnostics);
            }

            var actual = await HashStableFileAsync(
                absolutePath,
                file.Size,
                cancellationToken).ConfigureAwait(false);
            if (actual is null
                || actual.Value.Size != file.Size
                || !string.Equals(
                    actual.Value.Sha256,
                    file.Sha256,
                    StringComparison.Ordinal))
            {
                diagnostics.Add(Diagnostic(
                    "distribution.editor-image.file-integrity-mismatch",
                    file.RelativePath,
                    "Editor Image file no longer matches its generation inventory."));
                return Failure(diagnostics);
            }

            verifiedFiles.Add(new VerifiedEditorImageFile(
                file.RelativePath,
                file.Role,
                file.MediaType,
                file.Size,
                file.Sha256,
                absolutePath));
        }

        return VerifiedEditorImageInventoryVerifyResult.Success(
            new VerifiedEditorImageInventoryLease(
                expectedEngineGenerationId,
                root,
                parsed.TargetPlatform,
                parsed.TargetArchitecture,
                parsed.EntryPoint,
                Array.AsReadOnly(verifiedFiles.ToArray())));
    }

    private static ParsedManifest ParseManifest(
        byte[] manifestBytes,
        string expectedEngineGenerationId)
    {
        if (manifestBytes.Length == 0
            || (manifestBytes.Length >= 3
                && manifestBytes[0] == 0xef
                && manifestBytes[1] == 0xbb
                && manifestBytes[2] == 0xbf)
            || manifestBytes[^1] != (byte)'\n'
            || manifestBytes.AsSpan().Contains((byte)'\r'))
        {
            throw new InvalidDataException("manifest encoding is not canonical UTF-8/LF");
        }

        _ = StrictUtf8.GetString(manifestBytes);
        using var document = JsonDocument.Parse(
            manifestBytes,
            new JsonDocumentOptions
            {
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
                MaxDepth = 64,
            });
        var root = document.RootElement;
        EnsureNoDuplicateProperties(root);
        ValidateManifestShape(root);

        var manifestId = ReadString(root, "engineGenerationId");
        if (manifestId != expectedEngineGenerationId)
        {
            throw new InvalidDataException("selected generation differs");
        }

        var canonicalManifest = RenderCanonical(root, includeGenerationId: true);
        if (!manifestBytes.AsSpan().SequenceEqual(canonicalManifest))
        {
            var mismatch = FirstMismatch(manifestBytes, canonicalManifest);
            throw new InvalidDataException(
                $"manifest bytes are not canonical at byte {mismatch} "
                + $"(actual {manifestBytes.Length}, expected {canonicalManifest.Length})");
        }

        var generationPayload = RenderCanonical(root, includeGenerationId: false);
        var computedGenerationId = "sha256-"
            + Convert.ToHexString(SHA256.HashData(generationPayload)).ToLowerInvariant();
        if (computedGenerationId != expectedEngineGenerationId)
        {
            throw new InvalidDataException(
                $"canonical payload produced '{computedGenerationId}'");
        }

        var context = root.GetProperty("context");
        var toolchain = context.GetProperty("toolchain");
        var editorImage = root.GetProperty("editorImage");
        var targetPlatform = ReadRequiredString(context, "targetPlatform", 200);
        var targetArchitecture = ReadRequiredString(
            toolchain,
            "targetArchitecture",
            100);
        var entryPoint = ReadPortablePath(editorImage, "entryPoint");
        var filesElement = editorImage.GetProperty("files");
        if (filesElement.GetArrayLength() is < 1 or > MaxEditorFileCount)
        {
            throw new InvalidDataException("editor file array is invalid");
        }

        var files = new List<ManifestFile>(filesElement.GetArrayLength());
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string? previousPath = null;
        foreach (var element in filesElement.EnumerateArray())
        {
            var path = ReadPortablePath(element, "path");
            EnsureNoPythonProductPayload(path);
            var role = ReadString(element, "role");
            var mediaType = ReadString(element, "mediaType");
            var size = ReadInt64(element, "size");
            var integrity = element.GetProperty("integrity");
            var algorithm = ReadString(integrity, "algorithm");
            var digest = ReadString(integrity, "digest");
            if (!FileRoles.Contains(role)
                || mediaType.Length is < 3 or > 200
                || !MediaTypePattern.IsMatch(mediaType)
                || size < 0
                || algorithm != "sha256"
                || !DigestPattern.IsMatch(digest)
                || !paths.Add(path)
                || (previousPath is not null
                    && CompareUtf8(previousPath, path) >= 0))
            {
                throw new InvalidDataException(
                    $"editor file '{path}' is invalid or unordered");
            }

            previousPath = path;
            files.Add(new ManifestFile(path, role, mediaType, size, digest));
        }

        var entry = files.SingleOrDefault(file => file.RelativePath == entryPoint);
        if (entry is null || entry.Role != "executable")
        {
            throw new InvalidDataException(
                "editor entry point is missing or not executable");
        }

        return new ParsedManifest(
            targetPlatform,
            targetArchitecture,
            entryPoint,
            Array.AsReadOnly(files.ToArray()));
    }

    private static void ValidateManifestShape(JsonElement root)
    {
        EnsureExactObject(
            root,
            [
                "schema",
                "schemaVersion",
                "engineGenerationId",
                "distribution",
                "context",
                "editorImage",
                "bundledPackages",
                "packageArtifacts",
                "hostProfiles",
            ]);
        if (ReadString(root, "schema") != "com.asharia.engine-distribution"
            || ReadInt64(root, "schemaVersion") != 1
            || !GenerationIdPattern.IsMatch(ReadString(root, "engineGenerationId")))
        {
            throw new InvalidDataException("root discriminator is invalid");
        }

        var distribution = root.GetProperty("distribution");
        EnsureExactObject(
            distribution,
            ["id", "engineVersion", "engineApiVersion"]);
        _ = ReadRequiredString(distribution, "id", 200);
        _ = ReadRequiredString(distribution, "engineVersion", 100);
        _ = ReadRequiredString(distribution, "engineApiVersion", 100);

        var context = root.GetProperty("context");
        EnsureExactObject(
            context,
            ["targetPlatform", "configuration", "toolchain"]);
        _ = ReadRequiredString(context, "targetPlatform", 200);
        _ = ReadRequiredString(context, "configuration", 100);
        var toolchain = context.GetProperty("toolchain");
        EnsureExactObject(
            toolchain,
            [
                "compilerId",
                "compilerVersion",
                "targetSystem",
                "targetArchitecture",
                "runtimeLibrary",
            ]);
        foreach (var name in new[]
                 {
                     "compilerId",
                     "compilerVersion",
                     "targetSystem",
                     "targetArchitecture",
                     "runtimeLibrary",
                 })
        {
            _ = ReadRequiredString(toolchain, name, 100);
        }

        var editorImage = root.GetProperty("editorImage");
        EnsureExactObject(editorImage, ["entryPoint", "files"]);
        _ = ReadPortablePath(editorImage, "entryPoint");
        var editorFiles = ReadArray(editorImage, "files", requireNonEmpty: true);
        foreach (var file in editorFiles.EnumerateArray())
        {
            EnsureExactObject(
                file,
                ["path", "role", "mediaType", "size", "integrity"]);
            ValidateIntegrity(file.GetProperty("integrity"));
        }

        var bundledPackages = ReadArray(
            root,
            "bundledPackages",
            requireNonEmpty: true);
        foreach (var package in bundledPackages.EnumerateArray())
        {
            EnsureExactObject(
                package,
                [
                    "id",
                    "version",
                    "packageKind",
                    "availability",
                    "root",
                    "manifestIntegrity",
                    "payloadIntegrity",
                ]);
            _ = ReadRequiredString(package, "id", 200);
            _ = ReadRequiredString(package, "version", 100);
            _ = ReadRequiredString(package, "packageKind", 100);
            _ = ReadRequiredString(package, "availability", 100);
            _ = ReadPortablePath(package, "root");
            ValidateIntegrity(package.GetProperty("manifestIntegrity"));
            ValidateIntegrity(package.GetProperty("payloadIntegrity"));
        }

        var packageArtifacts = ReadArray(
            root,
            "packageArtifacts",
            requireNonEmpty: false);
        foreach (var artifact in packageArtifacts.EnumerateArray())
        {
            EnsureExactObject(
                artifact,
                [
                    "artifactGenerationId",
                    "package",
                    "context",
                    "manifestPath",
                    "manifestIntegrity",
                ]);
            if (!GenerationIdPattern.IsMatch(
                    ReadString(artifact, "artifactGenerationId")))
            {
                throw new InvalidDataException("artifact generation is invalid");
            }

            var package = artifact.GetProperty("package");
            EnsureExactObject(package, ["id", "version"]);
            _ = ReadRequiredString(package, "id", 200);
            _ = ReadRequiredString(package, "version", 100);
            var artifactContext = artifact.GetProperty("context");
            EnsureExactObject(
                artifactContext,
                ["hostKind", "targetPlatform", "configuration"]);
            _ = ReadRequiredString(artifactContext, "hostKind", 100);
            _ = ReadRequiredString(artifactContext, "targetPlatform", 200);
            _ = ReadRequiredString(artifactContext, "configuration", 100);
            _ = ReadPortablePath(artifact, "manifestPath");
            ValidateIntegrity(artifact.GetProperty("manifestIntegrity"));
        }

        var hostProfiles = ReadArray(
            root,
            "hostProfiles",
            requireNonEmpty: true);
        foreach (var profile in hostProfiles.EnumerateArray())
        {
            EnsureExactObject(
                profile,
                ["hostKind", "targetPlatform", "path", "integrity"]);
            _ = ReadRequiredString(profile, "hostKind", 100);
            _ = ReadRequiredString(profile, "targetPlatform", 200);
            _ = ReadPortablePath(profile, "path");
            ValidateIntegrity(profile.GetProperty("integrity"));
        }
    }

    private static byte[] RenderCanonical(
        JsonElement root,
        bool includeGenerationId)
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, CanonicalWriterOptions))
        {
            writer.WriteStartObject();
            foreach (var property in root.EnumerateObject())
            {
                if (!includeGenerationId
                    && property.NameEquals("engineGenerationId"))
                {
                    continue;
                }

                writer.WritePropertyName(property.Name);
                property.Value.WriteTo(writer);
            }

            writer.WriteEndObject();
        }

        var bytes = stream.ToArray();
        Array.Resize(ref bytes, bytes.Length + 1);
        bytes[^1] = (byte)'\n';
        return bytes;
    }

    private static void EnsureNoPythonProductPayload(string relativePath)
    {
        var segments = relativePath.Split('/');
        var fileName = segments[^1];
        var extension = Path.GetExtension(fileName);
        var containsPythonPackageTree = segments.Any(segment =>
            PythonPayloadSegments.Contains(segment)
            || segment.EndsWith(".dist-info", StringComparison.OrdinalIgnoreCase)
            || segment.EndsWith(".egg-info", StringComparison.OrdinalIgnoreCase)
            || PythonRuntimeDirectoryPattern().IsMatch(segment));
        var isPythonArtifact = PythonPayloadExtensions.Contains(extension)
            || fileName.Equals("py.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pyw.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pymanager.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pywmanager.exe", StringComparison.OrdinalIgnoreCase)
            || fileName.Equals("pyvenv.cfg", StringComparison.OrdinalIgnoreCase)
            || PythonRuntimeFilePattern().IsMatch(fileName)
            || PipRuntimeFilePattern().IsMatch(fileName)
            || LibPythonRuntimeFilePattern().IsMatch(fileName)
            || ManagedPythonRuntimeFilePattern().IsMatch(fileName)
            || AlternativePythonRuntimeFilePattern().IsMatch(fileName)
            || LibPyPyRuntimeFilePattern().IsMatch(fileName);
        if (containsPythonPackageTree || isPythonArtifact)
        {
            throw new ForbiddenPythonPayloadException(relativePath);
        }
    }

    private static string? ResolveGenerationRoot(
        string value,
        string expectedEngineGenerationId,
        ICollection<VerifiedEditorImageInventoryDiagnostic> diagnostics)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(value)
                || !Path.IsPathFullyQualified(value))
            {
                throw new ArgumentException();
            }

            var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(value));
            if (!Directory.Exists(root)
                || !string.Equals(
                    Path.GetFileName(root),
                    expectedEngineGenerationId,
                    StringComparison.Ordinal)
                || HasReparsePointInPath(root))
            {
                throw new IOException();
            }

            return root;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "distribution.editor-image.generation-root-invalid",
                "generationRoot",
                "Generation root must be an existing non-reparse directory named by the expected Engine generation."));
            return null;
        }
    }

    private static string? ResolveRegularFile(
        string root,
        string relativePath,
        string location,
        ICollection<VerifiedEditorImageInventoryDiagnostic> diagnostics)
    {
        try
        {
            if (!IsPortableRelativePath(relativePath))
            {
                throw new ArgumentException();
            }

            var candidate = Path.GetFullPath(
                Path.Combine(
                    root,
                    relativePath.Replace('/', Path.DirectorySeparatorChar)));
            var relative = Path.GetRelativePath(root, candidate)
                .Replace(Path.DirectorySeparatorChar, '/');
            if (!string.Equals(relative, relativePath, StringComparison.Ordinal)
                || !File.Exists(candidate)
                || HasReparsePointBetween(root, candidate)
                || (File.GetAttributes(candidate) & FileAttributes.Directory) != 0)
            {
                throw new IOException();
            }

            return candidate;
        }
        catch (Exception error) when (
            error is ArgumentException
                or IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "distribution.editor-image.file-invalid",
                location,
                $"Inventory file '{relativePath}' must be one contained non-reparse regular file."));
            return null;
        }
    }

    private static async Task<byte[]?> ReadBoundedFileAsync(
        string path,
        int maxBytes,
        CancellationToken cancellationToken)
    {
        try
        {
            await using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                64 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length is < 1 || stream.Length > maxBytes)
            {
                return null;
            }

            var contents = new byte[checked((int)stream.Length)];
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

            return stream.Length == contents.Length ? contents : null;
        }
        catch (Exception error) when (
            error is IOException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static async Task<(long Size, string Sha256)?> HashStableFileAsync(
        string path,
        long expectedSize,
        CancellationToken cancellationToken)
    {
        try
        {
            await using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                CopyBufferSize,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            if (stream.Length != expectedSize)
            {
                return null;
            }

            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            var buffer = ArrayPool<byte>.Shared.Rent(CopyBufferSize);
            long total = 0;
            try
            {
                int read;
                while ((read = await stream.ReadAsync(
                    buffer.AsMemory(0, buffer.Length),
                    cancellationToken).ConfigureAwait(false)) != 0)
                {
                    total = checked(total + read);
                    hash.AppendData(buffer, 0, read);
                }
            }
            finally
            {
                ArrayPool<byte>.Shared.Return(buffer);
            }

            if (total != expectedSize || stream.Length != expectedSize)
            {
                return null;
            }

            return (
                total,
                Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant());
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

    private static void EnsureNoDuplicateProperties(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                if (!names.Add(property.Name))
                {
                    throw new InvalidDataException(
                        $"duplicate property '{property.Name}'");
                }

                EnsureNoDuplicateProperties(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (var child in element.EnumerateArray())
            {
                EnsureNoDuplicateProperties(child);
            }
        }
    }

    private static void EnsureExactObject(
        JsonElement element,
        IReadOnlyList<string> expectedProperties)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("expected one JSON object");
        }

        var actual = element.EnumerateObject()
            .Select(property => property.Name)
            .ToArray();
        if (actual.Length != expectedProperties.Count
            || !actual.SequenceEqual(expectedProperties, StringComparer.Ordinal))
        {
            throw new InvalidDataException(
                $"object properties [{string.Join(",", actual)}] do not match [{string.Join(",", expectedProperties)}]");
        }
    }

    private static JsonElement ReadArray(
        JsonElement element,
        string propertyName,
        bool requireNonEmpty)
    {
        var value = element.GetProperty(propertyName);
        if (value.ValueKind != JsonValueKind.Array
            || (requireNonEmpty && value.GetArrayLength() == 0))
        {
            throw new InvalidDataException(
                $"'{propertyName}' must be a valid array");
        }

        return value;
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

    private static string ReadRequiredString(
        JsonElement element,
        string propertyName,
        int maxLength)
    {
        var value = ReadString(element, propertyName);
        if (string.IsNullOrWhiteSpace(value)
            || value.Length > maxLength
            || !value.IsNormalized(NormalizationForm.FormC)
            || value.Any(char.IsControl))
        {
            throw new InvalidDataException(
                $"'{propertyName}' is invalid");
        }

        return value;
    }

    private static long ReadInt64(
        JsonElement element,
        string propertyName)
    {
        var value = element.GetProperty(propertyName);
        return value.ValueKind == JsonValueKind.Number
            && value.TryGetInt64(out var result)
                ? result
                : throw new InvalidDataException();
    }

    private static string ReadPortablePath(
        JsonElement element,
        string propertyName)
    {
        var value = ReadString(element, propertyName);
        if (!IsPortableRelativePath(value))
        {
            throw new InvalidDataException(
                $"'{propertyName}' is not a portable relative path");
        }

        return value;
    }

    private static bool IsPortableRelativePath(string value)
    {
        return !string.IsNullOrWhiteSpace(value)
            && value.Length <= 500
            && value.IsNormalized(NormalizationForm.FormC)
            && !value.StartsWith("/", StringComparison.Ordinal)
            && !value.Contains('\\')
            && !value.Contains(':')
            && !value.Any(char.IsControl)
            && !value.Split('/').Any(part => part is "" or "." or "..");
    }

    private static void ValidateIntegrity(JsonElement integrity)
    {
        EnsureExactObject(integrity, ["algorithm", "digest"]);
        if (ReadString(integrity, "algorithm") != "sha256"
            || !DigestPattern.IsMatch(ReadString(integrity, "digest")))
        {
            throw new InvalidDataException("integrity is invalid");
        }
    }

    private static int CompareUtf8(string left, string right)
    {
        var leftBytes = Encoding.UTF8.GetBytes(left);
        var rightBytes = Encoding.UTF8.GetBytes(right);
        return leftBytes.AsSpan().SequenceCompareTo(rightBytes);
    }

    private static int FirstMismatch(
        ReadOnlySpan<byte> actual,
        ReadOnlySpan<byte> expected)
    {
        var shared = Math.Min(actual.Length, expected.Length);
        for (var index = 0; index < shared; index++)
        {
            if (actual[index] != expected[index])
            {
                return index;
            }
        }

        return shared;
    }

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
            if (string.Equals(parent, current, FileSystemPathComparison))
            {
                break;
            }

            current = parent ?? string.Empty;
        }

        return false;
    }

    private static bool HasReparsePointBetween(string root, string path)
    {
        var current = Path.GetFullPath(path);
        var canonicalRoot = Path.TrimEndingDirectorySeparator(
            Path.GetFullPath(root));
        while (true)
        {
            if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
            {
                return true;
            }

            if (string.Equals(
                    current,
                    canonicalRoot,
                    FileSystemPathComparison))
            {
                return false;
            }

            var parent = Path.GetDirectoryName(current);
            if (string.IsNullOrEmpty(parent)
                || string.Equals(parent, current, FileSystemPathComparison))
            {
                return true;
            }

            current = parent;
        }
    }

    private static VerifiedEditorImageInventoryVerifyResult Failure(
        IReadOnlyList<VerifiedEditorImageInventoryDiagnostic> diagnostics) =>
        VerifiedEditorImageInventoryVerifyResult.Failure(diagnostics);

    private static VerifiedEditorImageInventoryDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    [GeneratedRegex(
        "^python(?:(?:[0-9]+(?:\\.[0-9]+)*)t?)?(?:_d)?$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex PythonRuntimeDirectoryPattern();

    [GeneratedRegex(
        "^python[a-z0-9._-]*(?:\\.exe|\\.dll|\\.lib|\\.zip|\\._pth|\\.pth)$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex PythonRuntimeFilePattern();

    [GeneratedRegex(
        "^pip(?:[0-9]+(?:\\.[0-9]+)*)?(?:\\.exe|\\.dll|\\.zip|\\._pth|\\.pth)$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex PipRuntimeFilePattern();

    [GeneratedRegex(
        "^libpython[a-z0-9._-]*\\.(?:a|dll|dylib|lib|so)(?:\\.[0-9]+)*$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex LibPythonRuntimeFilePattern();

    [GeneratedRegex(
        "^(?:python\\.runtime|ironpython)(?:[.-][a-z0-9_-]+)*\\.dll$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex ManagedPythonRuntimeFilePattern();

    [GeneratedRegex(
        "^(?:ipyw?|pypy(?:3)?|jython|graalpy)[a-z0-9._-]*\\.(?:exe|dll|jar|zip)$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex AlternativePythonRuntimeFilePattern();

    [GeneratedRegex(
        "^libpypy(?:3)?[a-z0-9._-]*\\.(?:a|dll|dylib|lib|so)(?:\\.[0-9]+)*$",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase)]
    private static partial Regex LibPyPyRuntimeFilePattern();

    private sealed record ManifestFile(
        string RelativePath,
        string Role,
        string MediaType,
        long Size,
        string Sha256);

    private sealed record ParsedManifest(
        string TargetPlatform,
        string TargetArchitecture,
        string EntryPoint,
        IReadOnlyList<ManifestFile> Files);

    private sealed class ForbiddenPythonPayloadException(string relativePath)
        : Exception
    {
        public string RelativePath { get; } = relativePath;
    }
}
