using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodeImplicitSourceSnapshot
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeImplicitSourceSnapshot(
        string projectRelativePath,
        string absolutePath,
        long size,
        string sha256)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRelativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(absolutePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(sha256);
        if (size < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(size));
        }

        if (!Sha256Pattern.IsMatch(sha256))
        {
            throw new ArgumentException(
                "Source SHA-256 must use sixty-four lowercase hex digits.",
                nameof(sha256));
        }

        ProjectRelativePath = projectRelativePath;
        AbsolutePath = absolutePath;
        Size = size;
        Sha256 = sha256;
    }

    public string ProjectRelativePath { get; }

    public string AbsolutePath { get; }

    public long Size { get; }

    public string Sha256 { get; }
}

internal sealed record ProjectCodeImplicitWorkspaceFile
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeImplicitWorkspaceFile(
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
                "Workspace SHA-256 must use sixty-four lowercase hex digits.",
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

internal sealed record ProjectCodeImplicitSdkWorkspace(
    string WorkspaceId,
    Guid ProjectId,
    string AssemblyName,
    string CredentialId,
    string SdkVersion,
    string TargetFramework,
    string AbsoluteRoot,
    string EntryProjectRelativePath,
    string OutputAssemblyRelativePath,
    string ReferenceAssemblyRelativePath,
    string PortablePdbRelativePath,
    string DependencyFileRelativePath,
    IReadOnlyList<ProjectCodeImplicitSourceSnapshot> Sources,
    IReadOnlyList<ProjectCodeImplicitWorkspaceFile> Files);

internal sealed class ProjectCodeImplicitSdkWorkspaceLease
{
    private readonly ProjectCodeBuildEnvironmentCredentialLease credentialLease_;
    private readonly string projectRoot_;
    private readonly object stateGate_ = new();
    private int isCurrent_ = 1;

    internal ProjectCodeImplicitSdkWorkspaceLease(
        ProjectCodeBuildEnvironmentCredentialLease credentialLease,
        string projectRoot,
        ProjectCodeImplicitSdkWorkspace workspace)
    {
        ArgumentNullException.ThrowIfNull(credentialLease);
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        ArgumentNullException.ThrowIfNull(workspace);
        if (!credentialLease.IsCurrent
            || !string.Equals(
                credentialLease.Credential.CredentialId,
                workspace.CredentialId,
                StringComparison.Ordinal)
            || workspace.Sources.Count == 0
            || workspace.Files.Count == 0)
        {
            throw new ArgumentException(
                "Implicit workspace requires one current credential-bound source snapshot.",
                nameof(workspace));
        }

        credentialLease_ = credentialLease;
        projectRoot_ = projectRoot;
        Workspace = workspace;
    }

    public ProjectCodeImplicitSdkWorkspace Workspace { get; }

    public bool IsCurrent =>
        Volatile.Read(ref isCurrent_) != 0 && credentialLease_.IsCurrent;

    internal ProjectCodeBuildEnvironmentCredentialLease CredentialLease =>
        credentialLease_;

    internal string ProjectRoot => projectRoot_;

    internal void Revoke()
    {
        lock (stateGate_)
        {
            Interlocked.Exchange(ref isCurrent_, 0);
        }
    }
}

internal sealed record ProjectCodeImplicitSdkWorkspaceRequest(
    string ProjectRoot,
    Guid ProjectId,
    string WorkspaceRoot,
    ProjectCodeBuildEnvironmentCredentialLease CredentialLease);

internal sealed record ProjectCodeImplicitSdkWorkspaceDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeImplicitSdkWorkspaceResult
{
    private ProjectCodeImplicitSdkWorkspaceResult(
        ProjectCodeImplicitSdkWorkspaceLease? lease,
        IReadOnlyList<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics)
    {
        Lease = lease;
        Diagnostics = diagnostics;
    }

    public ProjectCodeImplicitSdkWorkspaceLease? Lease { get; }

    public IReadOnlyList<ProjectCodeImplicitSdkWorkspaceDiagnostic> Diagnostics { get; }

    public bool Succeeded => Diagnostics.Count == 0;

    public bool RequiresBuild => Lease is not null;

    internal static ProjectCodeImplicitSdkWorkspaceResult Empty() =>
        new(null, []);

    internal static ProjectCodeImplicitSdkWorkspaceResult Success(
        ProjectCodeImplicitSdkWorkspaceLease lease) =>
        new(lease, []);

    internal static ProjectCodeImplicitSdkWorkspaceResult Failure(
        IEnumerable<ProjectCodeImplicitSdkWorkspaceDiagnostic> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        var snapshot = diagnostics
            .Distinct()
            .OrderBy(item => item.Location, StringComparer.Ordinal)
            .ThenBy(item => item.Code, StringComparer.Ordinal)
            .ThenBy(item => item.Message, StringComparer.Ordinal)
            .ToArray();
        if (snapshot.Length == 0)
        {
            throw new ArgumentException(
                "Failed workspace result requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
