using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;

namespace Asharia.Studio.Application.ProjectCode;

internal enum ProjectCodeSdkBuildOutcome
{
    Succeeded = 0,
    Failed = 1,
    Canceled = 2,
    Superseded = 3,
    TimedOut = 4,
}

internal enum ProjectCodeSdkBuildStepKind
{
    ProbeSdk = 0,
    Restore = 1,
    Build = 2,
}

internal sealed record ProjectCodeSdkBuildStepReceipt
{
    public ProjectCodeSdkBuildStepReceipt(
        ProjectCodeSdkBuildStepKind kind,
        int? exitCode,
        TimeSpan duration,
        string standardOutput,
        string standardError,
        bool outputTruncated)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind));
        }

        if (duration < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(duration));
        }

        ArgumentNullException.ThrowIfNull(standardOutput);
        ArgumentNullException.ThrowIfNull(standardError);
        Kind = kind;
        ExitCode = exitCode;
        Duration = duration;
        StandardOutput = standardOutput;
        StandardError = standardError;
        OutputTruncated = outputTruncated;
    }

    public ProjectCodeSdkBuildStepKind Kind { get; }

    public int? ExitCode { get; }

    public TimeSpan Duration { get; }

    public string StandardOutput { get; }

    public string StandardError { get; }

    public bool OutputTruncated { get; }
}

internal sealed record ProjectCodeSdkBuildDiagnostic(
    string Code,
    ProjectCodeSdkBuildStepKind? Step,
    string Location,
    string Message);

internal sealed record ProjectCodeRawBuildOutputFile
{
    private static readonly Regex Sha256Pattern = new(
        "^[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeRawBuildOutputFile(
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

        if (!ProjectCodeSdkBuildPath.IsPortableRelativePath(relativePath))
        {
            throw new ArgumentException(
                "Raw build output path must be one portable relative path.",
                nameof(relativePath));
        }

        if (!Path.IsPathFullyQualified(absolutePath))
        {
            throw new ArgumentException(
                "Raw build output absolute path must be fully qualified.",
                nameof(absolutePath));
        }

        if (!Sha256Pattern.IsMatch(sha256))
        {
            throw new ArgumentException(
                "Raw build output SHA-256 must use sixty-four lowercase hex digits.",
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

internal sealed record ProjectCodeRawBuildOutput
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeRawBuildOutput(
        string outputId,
        Guid projectId,
        string workspaceId,
        string credentialId,
        string sdkVersion,
        string targetFramework,
        string assemblyName,
        string absoluteRoot,
        string implementationAssemblyRelativePath,
        string referenceAssemblyRelativePath,
        string portablePdbRelativePath,
        string dependencyFileRelativePath,
        IReadOnlyList<ProjectCodeRawBuildOutputFile> files)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(outputId);
        ArgumentException.ThrowIfNullOrWhiteSpace(workspaceId);
        ArgumentException.ThrowIfNullOrWhiteSpace(credentialId);
        ArgumentException.ThrowIfNullOrWhiteSpace(sdkVersion);
        ArgumentException.ThrowIfNullOrWhiteSpace(targetFramework);
        ArgumentException.ThrowIfNullOrWhiteSpace(assemblyName);
        ArgumentException.ThrowIfNullOrWhiteSpace(absoluteRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(
            implementationAssemblyRelativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(
            referenceAssemblyRelativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(portablePdbRelativePath);
        ArgumentException.ThrowIfNullOrWhiteSpace(dependencyFileRelativePath);
        ArgumentNullException.ThrowIfNull(files);
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Raw build output requires one non-empty project id.",
                nameof(projectId));
        }

        if (!IdentityPattern.IsMatch(outputId)
            || !IdentityPattern.IsMatch(workspaceId))
        {
            throw new ArgumentException(
                "Raw build and workspace identities must be canonical SHA-256 identities.");
        }

        if (!Path.IsPathFullyQualified(absoluteRoot))
        {
            throw new ArgumentException(
                "Raw build output root must be fully qualified.",
                nameof(absoluteRoot));
        }

        var expectedPaths = new[]
        {
            implementationAssemblyRelativePath,
            referenceAssemblyRelativePath,
            portablePdbRelativePath,
            dependencyFileRelativePath,
        };
        if (expectedPaths.Any(path =>
                !ProjectCodeSdkBuildPath.IsPortableRelativePath(path))
            || expectedPaths
                .GroupBy(path => path, StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Raw build output handoff paths must be unique portable relative paths.");
        }

        var snapshot = files.ToArray();
        if (snapshot.Length != expectedPaths.Length
            || snapshot.Any(file => file is null)
            || snapshot
                .GroupBy(
                    file => file.RelativePath,
                    StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() != 1)
            || !snapshot
                .Select(file => file.RelativePath)
                .ToHashSet(StringComparer.Ordinal)
                .SetEquals(expectedPaths))
        {
            throw new ArgumentException(
                "Raw build output must contain exactly its four declared handoff files.",
                nameof(files));
        }

        OutputId = outputId;
        ProjectId = projectId;
        WorkspaceId = workspaceId;
        CredentialId = credentialId;
        SdkVersion = sdkVersion;
        TargetFramework = targetFramework;
        AssemblyName = assemblyName;
        AbsoluteRoot = absoluteRoot;
        ImplementationAssemblyRelativePath =
            implementationAssemblyRelativePath;
        ReferenceAssemblyRelativePath = referenceAssemblyRelativePath;
        PortablePdbRelativePath = portablePdbRelativePath;
        DependencyFileRelativePath = dependencyFileRelativePath;
        Files = Array.AsReadOnly(snapshot);
    }

    public string OutputId { get; }

    public Guid ProjectId { get; }

    public string WorkspaceId { get; }

    public string CredentialId { get; }

    public string SdkVersion { get; }

    public string TargetFramework { get; }

    public string AssemblyName { get; }

    public string AbsoluteRoot { get; }

    public string ImplementationAssemblyRelativePath { get; }

    public string ReferenceAssemblyRelativePath { get; }

    public string PortablePdbRelativePath { get; }

    public string DependencyFileRelativePath { get; }

    public IReadOnlyList<ProjectCodeRawBuildOutputFile> Files { get; }
}

internal sealed class ProjectCodeRawBuildOutputLease
{
    private readonly ProjectCodeImplicitSdkWorkspaceLease workspaceLease_;
    private readonly object stateGate_ = new();
    private int isCurrent_ = 1;

    internal ProjectCodeRawBuildOutputLease(
        ProjectCodeImplicitSdkWorkspaceLease workspaceLease,
        ProjectCodeRawBuildOutput output)
    {
        ArgumentNullException.ThrowIfNull(workspaceLease);
        ArgumentNullException.ThrowIfNull(output);
        if (!workspaceLease.IsCurrent
            || workspaceLease.Workspace.ProjectId != output.ProjectId
            || !string.Equals(
                workspaceLease.Workspace.WorkspaceId,
                output.WorkspaceId,
                StringComparison.Ordinal)
            || !string.Equals(
                workspaceLease.Workspace.CredentialId,
                output.CredentialId,
                StringComparison.Ordinal))
        {
            throw new ArgumentException(
                "Raw build output must bind one current matching workspace.",
                nameof(output));
        }

        workspaceLease_ = workspaceLease;
        Output = output;
    }

    public ProjectCodeRawBuildOutput Output { get; }

    public bool IsCurrent =>
        Volatile.Read(ref isCurrent_) != 0 && workspaceLease_.IsCurrent;

    internal ProjectCodeImplicitSdkWorkspaceLease WorkspaceLease =>
        workspaceLease_;

    internal void Revoke()
    {
        lock (stateGate_)
        {
            Interlocked.Exchange(ref isCurrent_, 0);
        }
    }
}

internal sealed record ProjectCodeSdkBuildRequest
{
    private static readonly TimeSpan DefaultStepTimeout =
        TimeSpan.FromMinutes(5);
    private static readonly TimeSpan MinimumStepTimeout =
        TimeSpan.FromMilliseconds(100);
    private static readonly TimeSpan MaximumStepTimeout =
        TimeSpan.FromMinutes(10);

    public ProjectCodeSdkBuildRequest(
        ProjectCodeImplicitSdkWorkspaceLease workspaceLease,
        string outputRoot,
        TimeSpan? stepTimeout = null)
    {
        ArgumentNullException.ThrowIfNull(workspaceLease);
        ArgumentException.ThrowIfNullOrWhiteSpace(outputRoot);
        var resolvedTimeout = stepTimeout ?? DefaultStepTimeout;
        if (resolvedTimeout < MinimumStepTimeout
            || resolvedTimeout > MaximumStepTimeout)
        {
            throw new ArgumentOutOfRangeException(
                nameof(stepTimeout),
                "SDK build step timeout must be between 100 milliseconds and ten minutes.");
        }

        WorkspaceLease = workspaceLease;
        OutputRoot = outputRoot;
        StepTimeout = resolvedTimeout;
    }

    public ProjectCodeImplicitSdkWorkspaceLease WorkspaceLease { get; }

    public string OutputRoot { get; }

    public TimeSpan StepTimeout { get; }
}

internal sealed class ProjectCodeSdkBuildResult
{
    private ProjectCodeSdkBuildResult(
        long invocation,
        ProjectCodeSdkBuildOutcome outcome,
        ProjectCodeRawBuildOutputLease? lease,
        IReadOnlyList<ProjectCodeSdkBuildStepReceipt> steps,
        IReadOnlyList<ProjectCodeSdkBuildDiagnostic> diagnostics)
    {
        if (invocation <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(invocation));
        }

        if (!Enum.IsDefined(outcome))
        {
            throw new ArgumentOutOfRangeException(nameof(outcome));
        }

        ArgumentNullException.ThrowIfNull(steps);
        ArgumentNullException.ThrowIfNull(diagnostics);
        var stepSnapshot = steps.ToArray();
        var diagnosticSnapshot = diagnostics
            .Distinct()
            .OrderBy(item => item.Step)
            .ThenBy(item => item.Location, StringComparer.Ordinal)
            .ThenBy(item => item.Code, StringComparer.Ordinal)
            .ThenBy(item => item.Message, StringComparer.Ordinal)
            .ToArray();
        if (stepSnapshot.Any(step => step is null)
            || diagnosticSnapshot.Any(diagnostic => diagnostic is null)
            || (outcome == ProjectCodeSdkBuildOutcome.Succeeded)
                != (lease is not null && diagnosticSnapshot.Length == 0))
        {
            throw new ArgumentException(
                "SDK build result outcome, lease, steps, and diagnostics are inconsistent.");
        }

        Invocation = invocation;
        Outcome = outcome;
        Lease = lease;
        Steps = Array.AsReadOnly(stepSnapshot);
        Diagnostics = Array.AsReadOnly(diagnosticSnapshot);
    }

    public long Invocation { get; }

    public ProjectCodeSdkBuildOutcome Outcome { get; }

    public ProjectCodeRawBuildOutputLease? Lease { get; }

    public IReadOnlyList<ProjectCodeSdkBuildStepReceipt> Steps { get; }

    public IReadOnlyList<ProjectCodeSdkBuildDiagnostic> Diagnostics { get; }

    public bool Succeeded =>
        Outcome == ProjectCodeSdkBuildOutcome.Succeeded;

    internal static ProjectCodeSdkBuildResult Success(
        long invocation,
        ProjectCodeRawBuildOutputLease lease,
        IReadOnlyList<ProjectCodeSdkBuildStepReceipt> steps) =>
        new(
            invocation,
            ProjectCodeSdkBuildOutcome.Succeeded,
            lease,
            steps,
            []);

    internal static ProjectCodeSdkBuildResult Failure(
        long invocation,
        ProjectCodeSdkBuildOutcome outcome,
        IReadOnlyList<ProjectCodeSdkBuildStepReceipt> steps,
        IEnumerable<ProjectCodeSdkBuildDiagnostic> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        if (outcome == ProjectCodeSdkBuildOutcome.Succeeded)
        {
            throw new ArgumentException(
                "Failure result cannot use the succeeded outcome.",
                nameof(outcome));
        }

        var snapshot = diagnostics.ToArray();
        if (snapshot.Length == 0)
        {
            throw new ArgumentException(
                "Failed SDK build result requires diagnostics.",
                nameof(diagnostics));
        }

        return new(invocation, outcome, null, steps, snapshot);
    }
}

internal static class ProjectCodeSdkBuildPath
{
    public static bool IsPortableRelativePath(string value) =>
        !string.IsNullOrWhiteSpace(value)
        && value.Length <= 500
        && value.IsNormalized(System.Text.NormalizationForm.FormC)
        && !value.StartsWith("/", StringComparison.Ordinal)
        && !value.Contains('\\')
        && !value.Contains(':')
        && !value.Any(char.IsControl)
        && !value.Split('/').Any(part => part is "" or "." or "..");
}
