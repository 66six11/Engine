using System;
using System.Collections.Generic;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedModuleScopeRegistration :
    IDisposable
{
    private readonly EditorScopeRegistration registration_;

    internal ProjectCodePinnedModuleScopeRegistration(
        ProjectCodePinnedModuleScopePreparation preparation,
        EditorScopeRegistration registration)
    {
        Preparation = preparation
            ?? throw new ArgumentNullException(nameof(preparation));
        registration_ = registration
            ?? throw new ArgumentNullException(nameof(registration));
        if (!ReferenceEquals(
                preparation.Candidate,
                registration.Partition))
        {
            throw new ArgumentException(
                "Scope registration differs from the exact prepared candidate.",
                nameof(registration));
        }
    }

    public ProjectCodePinnedModuleScopePreparation Preparation
    {
        get;
    }

    public ScopeInstanceId ScopeInstanceId =>
        Preparation.ScopeInstanceId;

    public EditorScopePartition Partition =>
        registration_.Partition;

    public void Dispose() => registration_.Dispose();
}

internal sealed class ProjectCodePinnedModuleScopeCommitResult
{
    private ProjectCodePinnedModuleScopeCommitResult(
        ProjectCodePinnedModuleScopeRegistration? registration,
        IReadOnlyList<ProjectCodePinnedModuleScopeDiagnostic>
            diagnostics)
    {
        Registration = registration;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedModuleScopeRegistration? Registration
    {
        get;
    }

    public IReadOnlyList<ProjectCodePinnedModuleScopeDiagnostic>
        Diagnostics
    {
        get;
    }

    public bool Succeeded =>
        Registration is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedModuleScopeCommitResult Success(
        ProjectCodePinnedModuleScopeRegistration registration)
    {
        ArgumentNullException.ThrowIfNull(registration);
        return new(registration, []);
    }

    internal static ProjectCodePinnedModuleScopeCommitResult Conflict() =>
        new(
            null,
            [
                new ProjectCodePinnedModuleScopeDiagnostic(
                    "project-code.pinned-module-scope-registration.conflict",
                    "project-scope",
                    "The Project scope registry changed after preparation; prepare a new candidate."),
            ]);
}

internal static class ProjectCodePinnedModuleScopeCommitter
{
    public static ProjectCodePinnedModuleScopeCommitResult CommitInitial(
        ProjectCodePinnedModuleScopePreparation preparation)
    {
        ArgumentNullException.ThrowIfNull(preparation);
        if (!preparation.Transaction.TryCommitInitial(
                out var registration))
        {
            return ProjectCodePinnedModuleScopeCommitResult.Conflict();
        }

        return ProjectCodePinnedModuleScopeCommitResult.Success(
            new ProjectCodePinnedModuleScopeRegistration(
                preparation,
                registration!));
    }
}
