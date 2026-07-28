using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedModuleScopeActivation :
    IAsyncDisposable
{
    private readonly object gate_ = new();
    private readonly EditorScopeRegistration registration_;
    private readonly EditorScopeActivation activation_;
    private Task? disposalTask_;

    internal ProjectCodePinnedModuleScopeActivation(
        ProjectCodePinnedModuleScopePreparation preparation,
        IReadOnlyList<EditorCapabilitySnapshot> capabilities,
        EditorScopeRegistration registration,
        EditorScopeActivation activation)
    {
        Preparation = preparation
            ?? throw new ArgumentNullException(nameof(preparation));
        ArgumentNullException.ThrowIfNull(capabilities);
        registration_ = registration
            ?? throw new ArgumentNullException(nameof(registration));
        activation_ = activation
            ?? throw new ArgumentNullException(nameof(activation));
        if (!ReferenceEquals(
                preparation.Candidate,
                registration.Partition)
            || activation.ScopeInstanceId
                != preparation.ScopeInstanceId)
        {
            throw new ArgumentException(
                "Scope activation differs from the exact registered candidate.",
                nameof(activation));
        }

        Capabilities = capabilities;
        Instances = activation.Instances;
    }

    public ProjectCodePinnedModuleScopePreparation Preparation
    {
        get;
    }

    public ScopeInstanceId ScopeInstanceId =>
        Preparation.ScopeInstanceId;

    public EditorScopePartition Partition =>
        registration_.Partition;

    public IReadOnlyList<EditorCapabilitySnapshot> Capabilities
    {
        get;
    }

    public IReadOnlyDictionary<
        EditorModuleDefinitionId,
        EditorModuleInstanceStatus> Instances
    {
        get;
    }

    public ValueTask DisposeAsync()
    {
        lock (gate_)
        {
            disposalTask_ ??= DisposeCoreAsync();
            return new ValueTask(disposalTask_);
        }
    }

    private async Task DisposeCoreAsync()
    {
        var failures = new List<Exception>();
        try
        {
            await activation_.DisposeAsync().ConfigureAwait(false);
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        try
        {
            registration_.Dispose();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        if (failures.Count > 0)
        {
            throw new AggregateException(
                "Project scope activation did not dispose cleanly.",
                failures);
        }
    }
}

internal sealed class ProjectCodePinnedModuleScopeActivationResult
{
    private ProjectCodePinnedModuleScopeActivationResult(
        ProjectCodePinnedModuleScopeActivation? activation,
        IReadOnlyList<ProjectCodePinnedModuleScopeDiagnostic>
            diagnostics)
    {
        Activation = activation;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedModuleScopeActivation? Activation
    {
        get;
    }

    public IReadOnlyList<ProjectCodePinnedModuleScopeDiagnostic>
        Diagnostics
    {
        get;
    }

    public bool Succeeded =>
        Activation is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedModuleScopeActivationResult Success(
        ProjectCodePinnedModuleScopeActivation activation)
    {
        ArgumentNullException.ThrowIfNull(activation);
        return new(activation, []);
    }

    internal static ProjectCodePinnedModuleScopeActivationResult Failure(
        string code,
        string message) =>
        new(
            null,
            [
                new ProjectCodePinnedModuleScopeDiagnostic(
                    code,
                    "project-scope",
                    message),
            ]);
}

internal static class ProjectCodePinnedModuleScopeActivator
{
    public static async ValueTask<
        ProjectCodePinnedModuleScopeActivationResult> ActivateAsync(
        ProjectCodePinnedModuleScopeRegistration registration,
        EditorModuleHost moduleHost,
        IEnumerable<EditorCapabilitySnapshot> capabilities,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(registration);
        ArgumentNullException.ThrowIfNull(moduleHost);
        var capabilitySnapshot = CopyCapabilities(
            registration.Preparation,
            capabilities);
        if (!registration.TryTransfer(
                out var ownedRegistration))
        {
            return ProjectCodePinnedModuleScopeActivationResult.Failure(
                "project-code.pinned-module-scope-activation.registration-unavailable",
                "The Project scope registration no longer owns its exact partition.");
        }

        EditorScopeActivation? activation = null;
        var cleanupAttempted = false;
        try
        {
            activation = await moduleHost.ActivateNewScopeAsync(
                registration.Partition,
                capabilitySnapshot,
                cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            var faulted = activation.Instances.Values
                .Where(instance =>
                    instance.State
                    == EditorModuleInstanceState.Faulted)
                .OrderBy(
                    instance => DescribeModule(instance.Id),
                    StringComparer.Ordinal)
                .ToArray();
            if (faulted.Length > 0)
            {
                var cleanupFailure = await CleanupAsync(
                    activation,
                    ownedRegistration!).ConfigureAwait(false);
                cleanupAttempted = true;
                if (cleanupFailure is not null)
                {
                    throw cleanupFailure;
                }

                return ProjectCodePinnedModuleScopeActivationResult.Failure(
                    "project-code.pinned-module-scope-activation.module-faulted",
                    $"Project scope activation faulted {faulted.Length} module instance(s): "
                    + string.Join(
                        ", ",
                        faulted.Select(instance =>
                            DescribeModule(instance.Id)))
                    + ".");
            }

            return ProjectCodePinnedModuleScopeActivationResult.Success(
                new ProjectCodePinnedModuleScopeActivation(
                    registration.Preparation,
                    capabilitySnapshot,
                    ownedRegistration!,
                    activation));
        }
        catch (Exception error)
        {
            if (cleanupAttempted)
            {
                throw;
            }

            var cleanupFailure = await CleanupAsync(
                activation,
                ownedRegistration!).ConfigureAwait(false);
            if (cleanupFailure is not null)
            {
                throw new AggregateException(
                    "Project scope activation and cleanup both failed.",
                    error,
                    cleanupFailure);
            }

            ExceptionDispatchInfo.Capture(error).Throw();
            throw;
        }
    }

    private static string DescribeModule(
        EditorModuleInstanceId id) =>
        $"{id.Definition.Assembly.Package.Value}/"
        + $"{id.Definition.Assembly.Assembly.Value}/"
        + $"{id.Definition.Module.Value}"
        + $" ({id.Definition.Scope})";

    private static IReadOnlyList<EditorCapabilitySnapshot>
        CopyCapabilities(
            ProjectCodePinnedModuleScopePreparation preparation,
            IEnumerable<EditorCapabilitySnapshot> capabilities)
    {
        ArgumentNullException.ThrowIfNull(capabilities);
        var snapshot = capabilities.ToArray();
        var expected = preparation.HostCapabilities.ToHashSet();
        var actual = new HashSet<EditorCapabilityId>();
        for (var index = 0; index < snapshot.Length; ++index)
        {
            var capability = snapshot[index];
            if (!capability.IsValid)
            {
                throw new ArgumentException(
                    $"Capability snapshot at index {index} is invalid.",
                    nameof(capabilities));
            }

            if (!actual.Add(capability.Id))
            {
                throw new ArgumentException(
                    $"Capability snapshot '{capability.Id}' is duplicated.",
                    nameof(capabilities));
            }
        }

        if (!actual.SetEquals(expected))
        {
            throw new ArgumentException(
                "Capability snapshots do not match the exact prepared host capabilities.",
                nameof(capabilities));
        }

        return Array.AsReadOnly(snapshot);
    }

    private static async Task<Exception?> CleanupAsync(
        EditorScopeActivation? activation,
        EditorScopeRegistration registration)
    {
        var failures = new List<Exception>();
        if (activation is not null)
        {
            try
            {
                await activation.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception error)
            {
                failures.Add(error);
            }
        }

        try
        {
            registration.Dispose();
        }
        catch (Exception error)
        {
            failures.Add(error);
        }

        return failures.Count switch
        {
            0 => null,
            1 => failures[0],
            _ => new AggregateException(
                "Project scope activation cleanup failed.",
                failures),
        };
    }
}
