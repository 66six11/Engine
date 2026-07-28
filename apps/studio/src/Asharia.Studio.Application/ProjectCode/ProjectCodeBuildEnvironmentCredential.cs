using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using Asharia.Studio.Application.Bootstrap.Distribution;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodeAssemblyIdentity
{
    private static readonly Regex PublicKeyTokenPattern = new(
        "^[0-9a-f]{16}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeAssemblyIdentity(
        string simpleName,
        Version version,
        string culture,
        string publicKeyToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(simpleName);
        ArgumentNullException.ThrowIfNull(version);
        ArgumentException.ThrowIfNullOrWhiteSpace(culture);
        ArgumentException.ThrowIfNullOrWhiteSpace(publicKeyToken);
        if (simpleName.Any(character =>
                char.IsControl(character) || character is ',' or '='))
        {
            throw new ArgumentException(
                "Assembly simple name contains an unsupported character.",
                nameof(simpleName));
        }

        if (version.Build < 0 || version.Revision < 0)
        {
            throw new ArgumentException(
                "Assembly identity requires one four-part version.",
                nameof(version));
        }

        var normalizedToken = publicKeyToken.ToLowerInvariant();
        if (normalizedToken != "null"
            && !PublicKeyTokenPattern.IsMatch(normalizedToken))
        {
            throw new ArgumentException(
                "Public key token must be null or sixteen lowercase hex digits.",
                nameof(publicKeyToken));
        }

        SimpleName = simpleName;
        Version = version;
        Culture = culture.Equals(
            "neutral",
            StringComparison.OrdinalIgnoreCase)
                ? "neutral"
                : culture;
        PublicKeyToken = normalizedToken;
    }

    public string SimpleName { get; }

    public Version Version { get; }

    public string Culture { get; }

    public string PublicKeyToken { get; }

    public string FullName =>
        $"{SimpleName}, Version={Version}, Culture={Culture}, "
        + $"PublicKeyToken={PublicKeyToken}";

    internal bool HasSameBindingIdentity(ProjectCodeAssemblyIdentity other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return string.Equals(
                SimpleName,
                other.SimpleName,
                StringComparison.OrdinalIgnoreCase)
            && Version == other.Version
            && string.Equals(
                Culture,
                other.Culture,
                StringComparison.OrdinalIgnoreCase)
            && string.Equals(
                PublicKeyToken,
                other.PublicKeyToken,
                StringComparison.OrdinalIgnoreCase);
    }
}

internal sealed record ProjectCodeBuildEnvironmentFileSnapshot
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeBuildEnvironmentFileSnapshot(
        string relativePath,
        string absolutePath,
        long size,
        string sha256)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(relativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(absolutePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(sha256);
        if (size < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(size));
        }

        if (!Sha256Pattern.IsMatch(sha256))
        {
            throw new ArgumentException(
                "File SHA-256 must use sixty-four lowercase hex digits.",
                nameof(sha256));
        }

        RelativePath = relativePath;
        AbsolutePath = absolutePath;
        Size = size;
        Sha256 = sha256;
    }

    public string RelativePath { get; }

    public string AbsolutePath { get; }

    public long Size { get; }

    public string Sha256 { get; }
}

internal sealed record ProjectCodeBuildEnvironmentTreeSnapshot
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeBuildEnvironmentTreeSnapshot(
        string relativeRoot,
        string absoluteRoot,
        long totalSize,
        string sha256,
        IReadOnlyList<ProjectCodeBuildEnvironmentFileSnapshot> files)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(relativeRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(absoluteRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(sha256);
        ArgumentNullException.ThrowIfNull(files);
        var snapshot = files.ToArray();
        if (totalSize < 0
            || snapshot.Length == 0
            || snapshot.Any(file => file is null)
            || snapshot.GroupBy(
                    file => file.RelativePath,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1)
            || snapshot.Sum(file => file.Size) != totalSize)
        {
            throw new ArgumentException(
                "Build environment tree snapshot is invalid.",
                nameof(files));
        }

        if (!Sha256Pattern.IsMatch(sha256))
        {
            throw new ArgumentException(
                "Tree SHA-256 must use sixty-four lowercase hex digits.",
                nameof(sha256));
        }

        RelativeRoot = relativeRoot;
        AbsoluteRoot = absoluteRoot;
        TotalSize = totalSize;
        Sha256 = sha256;
        Files = Array.AsReadOnly(snapshot);
    }

    public string RelativeRoot { get; }

    public string AbsoluteRoot { get; }

    public int FileCount => Files.Count;

    public long TotalSize { get; }

    public string Sha256 { get; }

    public IReadOnlyList<ProjectCodeBuildEnvironmentFileSnapshot> Files { get; }
}

internal sealed record ProjectCodeContractFileBinding
{
    public ProjectCodeContractFileBinding(
        ProjectCodeAssemblyIdentity identity,
        IReadOnlyList<ProjectCodeAssemblyIdentity> references,
        ProjectCodeBuildEnvironmentFileSnapshot file)
    {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(references);
        ArgumentNullException.ThrowIfNull(file);
        var referenceSnapshot = references.ToArray();
        if (referenceSnapshot.Any(reference => reference is null)
            || referenceSnapshot.GroupBy(
                    reference => reference.SimpleName,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Contract references must expose unique CLR simple names.",
                nameof(references));
        }

        Identity = identity;
        References = Array.AsReadOnly(referenceSnapshot);
        File = file;
    }

    public ProjectCodeAssemblyIdentity Identity { get; }

    public IReadOnlyList<ProjectCodeAssemblyIdentity> References { get; }

    public ProjectCodeBuildEnvironmentFileSnapshot File { get; }

    public string AbsolutePath => File.AbsolutePath;

    public long Size => File.Size;

    public string Sha256 => File.Sha256;
}

internal sealed record ProjectCodeBuildEnvironmentCredential(
    string CredentialId,
    string EngineGenerationId,
    string EnvironmentId,
    string TargetFramework,
    string TargetPlatform,
    string TargetArchitecture,
    string ProjectionId,
    string SdkVersion,
    string HostFxrVersion,
    string HostRuntimeVersion,
    string ReferencePackVersion,
    ProjectCodeBuildEnvironmentFileSnapshot DotnetHost,
    ProjectCodeBuildEnvironmentTreeSnapshot Sdk,
    ProjectCodeBuildEnvironmentTreeSnapshot HostFxr,
    ProjectCodeBuildEnvironmentTreeSnapshot HostRuntime,
    ProjectCodeBuildEnvironmentTreeSnapshot ReferencePack,
    string ReferenceAssembliesRoot,
    ProjectCodeAssemblyIdentity SdkEntryIdentity,
    ProjectCodeAssemblyIdentity HostRuntimeCoreIdentity,
    ProjectCodeContractFileBinding RuntimeContract,
    ProjectCodeContractFileBinding EditorContract,
    IReadOnlyList<ProjectCodeAssemblyIdentity> FrameworkReferences)
{
    public string DotnetExecutable => DotnetHost.AbsolutePath;
}

internal sealed class ProjectCodeBuildEnvironmentCredentialLease
{
    private readonly VerifiedManagedBuildEnvironmentLease sourceLease_;
    private readonly HashSet<string> selectedPaths_;
    private readonly object stateGate_ = new();
    private int isCurrent_ = 1;

    internal ProjectCodeBuildEnvironmentCredentialLease(
        VerifiedManagedBuildEnvironmentLease sourceLease,
        ProjectCodeBuildEnvironmentCredential credential)
    {
        ArgumentNullException.ThrowIfNull(sourceLease);
        ArgumentNullException.ThrowIfNull(credential);
        var selectedPaths = sourceLease.Projection.SelectedFiles
            .Select(file => file.RelativePath)
            .ToArray();
        if (selectedPaths.Length == 0
            || selectedPaths
                .GroupBy(path => path, StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1)
            || !string.Equals(
                credential.ProjectionId,
                sourceLease.Projection.ProjectionId,
                StringComparison.Ordinal))
        {
            throw new ArgumentException(
                "Semantic credential must bind one exact managed environment projection.",
                nameof(credential));
        }

        sourceLease_ = sourceLease;
        Credential = credential;
        selectedPaths_ = selectedPaths.ToHashSet(StringComparer.Ordinal);
    }

    public ProjectCodeBuildEnvironmentCredential Credential { get; }

    public bool IsCurrent =>
        Volatile.Read(ref isCurrent_) != 0 && sourceLease_.IsCurrent;

    internal VerifiedManagedBuildEnvironmentLease SourceLease => sourceLease_;

    internal bool TryGetCurrentFile(
        string relativePath,
        out VerifiedEditorImageFile? file)
    {
        lock (stateGate_)
        {
            file = null;
            return Volatile.Read(ref isCurrent_) != 0
                && selectedPaths_.Contains(relativePath)
                && sourceLease_.TryGetCurrentFile(relativePath, out file);
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

internal sealed record ProjectCodeBuildEnvironmentDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeBuildEnvironmentCredentialResolveResult
{
    private ProjectCodeBuildEnvironmentCredentialResolveResult(
        ProjectCodeBuildEnvironmentCredentialLease? lease,
        IReadOnlyList<ProjectCodeBuildEnvironmentDiagnostic> diagnostics)
    {
        Lease = lease;
        Diagnostics = diagnostics;
    }

    public ProjectCodeBuildEnvironmentCredentialLease? Lease { get; }

    public IReadOnlyList<ProjectCodeBuildEnvironmentDiagnostic> Diagnostics { get; }

    public bool Succeeded => Lease is not null && Diagnostics.Count == 0;

    public static ProjectCodeBuildEnvironmentCredentialResolveResult Success(
        ProjectCodeBuildEnvironmentCredentialLease lease) =>
        new(lease, []);

    public static ProjectCodeBuildEnvironmentCredentialResolveResult Failure(
        IReadOnlyList<ProjectCodeBuildEnvironmentDiagnostic> diagnostics) =>
        new(null, diagnostics);
}
