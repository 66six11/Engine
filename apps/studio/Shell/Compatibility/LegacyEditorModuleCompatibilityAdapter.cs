using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;
using Editor.Core.Abstractions;
using Editor.Core.Models.Contributions;
using Editor.Core.Models.Extensions;
using Editor.Core.Services;
using Editor.Shell.Commands;
using Editor.Shell.Composition;
using Editor.Shell.Docking.Panels;

namespace Editor.Shell.Compatibility;

// Task 8 deletes this adapter after built-in features move to the public module API.
internal sealed class LegacyEditorModuleCompatibilityAdapter : IAsyncDisposable
{
    private static readonly EditorAssemblyId LegacyAssemblyId = EditorAssemblyId.Create(
        PackageName.Create("asharia.studio"),
        EditorAssemblyName.Create("Editor.Legacy"));

    private readonly IEditorExtensionModule[] modules_;
    private readonly EditorModuleHost moduleHost_ = new();
    private readonly EditorModuleRegistry moduleRegistry_ = new();
    private readonly List<IDisposable> contributionLeases_ = [];
    private LegacyEditorModuleBridge[]? bridges_;
    private EditorScopePartition? applicationPartition_;
    private EditorScopeActivation? applicationActivation_;

    public LegacyEditorModuleCompatibilityAdapter(IEnumerable<IEditorExtensionModule> modules)
    {
        modules_ = CreateModuleArray(modules);
    }

    internal EditorScopePartition ApplicationPartition
    {
        get
        {
            EnsurePrepared();
            return applicationPartition_!;
        }
    }

    public EditorExtensionComposition Compose()
    {
        EnsurePrepared();

        var contributions = bridges_!.Select(bridge => bridge.Contributions).ToArray();
        ValidateUniquePanelIds(contributions);
        ValidateUniqueActionIds(contributions);
        ValidateUniqueSceneProviderIds(contributions);
        ValidateUniqueSceneProviderRoles(contributions);
        ValidateContributionDescriptorSets(contributions);

        var panelRegistry = new PanelRegistry();
        var actionRegistry = new WorkbenchActionRegistry();
        var providerHost = new EditorProviderHost();
        var committedContributions = new List<IDisposable>();

        try
        {
            foreach (var contribution in contributions)
            {
                foreach (var panel in contribution.Panels)
                {
                    committedContributions.Add(panelRegistry.RegisterOwned(
                        panel,
                        contribution.OwnerId));
                }

                foreach (var action in contribution.Actions)
                {
                    committedContributions.Add(actionRegistry.RegisterOwned(
                        action,
                        contribution.OwnerId));
                }

                foreach (var sceneProvider in contribution.SceneProviders)
                {
                    committedContributions.Add(providerHost.RegisterOwned(
                        sceneProvider,
                        contribution.OwnerId));
                }
            }
        }
        catch
        {
            DisposeRegistrationLeases(committedContributions);
            throw;
        }

        contributionLeases_.AddRange(committedContributions);

        return new EditorExtensionComposition(panelRegistry, actionRegistry, providerHost);
    }

    public async ValueTask ActivateAsync(CancellationToken cancellationToken = default)
    {
        EnsurePrepared();

        EditorScopeActivation activation;
        try
        {
            activation = await moduleHost_.ActivateScopeAsync(
                applicationPartition_!,
                [],
                cancellationToken);
        }
        catch (Exception activationException)
        {
            var reportedException = activationException is OperationCanceledException
                && cancellationToken.IsCancellationRequested
                ? new OperationCanceledException(cancellationToken)
                : activationException;
            var disposeFailures = DisposeRegistrationLeases(contributionLeases_);
            if (disposeFailures.Count > 0)
            {
                throw new AggregateException([reportedException, .. disposeFailures]);
            }

            throw reportedException;
        }

        var activationFailure = activation.Instances.Values
            .FirstOrDefault(instance => instance.State == EditorModuleInstanceState.Faulted)
            ?.Failure;
        if (activationFailure is null)
        {
            applicationActivation_ = activation;
            return;
        }

        var failures = new List<Exception> { activationFailure };
        await AddDisposeFailureAsync(failures, activation.DisposeAsync());
        failures.AddRange(DisposeRegistrationLeases(contributionLeases_));
        if (failures.Count > 1)
        {
            throw new AggregateException(failures);
        }

        throw activationFailure;
    }

    public async ValueTask DisposeAsync()
    {
        var failures = new List<Exception>();
        if (applicationActivation_ is not null)
        {
            await AddDisposeFailureAsync(failures, applicationActivation_.DisposeAsync());
            applicationActivation_ = null;
        }

        await AddDisposeFailureAsync(failures, moduleHost_.DisposeAsync());
        failures.AddRange(DisposeRegistrationLeases(contributionLeases_));
        ThrowFailures(failures);
    }

    private static IEditorExtensionModule[] CreateModuleArray(
        IEnumerable<IEditorExtensionModule> modules)
    {
        ArgumentNullException.ThrowIfNull(modules);

        var moduleArray = modules.ToArray();
        foreach (var module in moduleArray)
        {
            ArgumentNullException.ThrowIfNull(module);
        }

        return moduleArray;
    }

    private void ValidateUniqueExtensionIds()
    {
        var ownerIds = new HashSet<EditorExtensionId>();
        foreach (var module in modules_)
        {
            if (!ownerIds.Add(module.Id))
            {
                throw new InvalidOperationException(
                    $"Editor extension id '{module.Id}' is registered more than once.");
            }
        }
    }

    private void EnsurePrepared()
    {
        if (applicationPartition_ is not null)
        {
            return;
        }

        ValidateUniqueExtensionIds();
        bridges_ = new LegacyEditorModuleBridge[modules_.Length];
        var registrations = new StaticEditorModuleRegistration[modules_.Length];
        EditorModuleDefinitionId? previousDefinitionId = null;

        for (var index = 0; index < modules_.Length; index++)
        {
            var module = modules_[index];
            var definitionId = CreateDefinitionId(module.Id);
            var bridge = new LegacyEditorModuleBridge(
                module,
                definitionId,
                previousDefinitionId);
            var metadata = new EditorModuleMetadata(
                definitionId,
                module.GetType().FullName ?? module.GetType().Name,
                EditorModuleActivationPolicy.OnScopeReady,
                EditorModuleHandoverPolicy.Coexist);
            bridges_[index] = bridge;
            registrations[index] = new StaticEditorModuleRegistration(
                definitionId,
                () => bridge,
                metadata);
            previousDefinitionId = definitionId;
        }

        var generation = StaticPackageGenerationHost.Create(registrations);
        var definitions = bridges_
            .Select(bridge => generation.GetRequiredDefinition(bridge.DefinitionId))
            .ToArray();
        var transaction = EditorScopeTransaction.Prepare(
            moduleRegistry_,
            ScopeInstanceId.Application,
            definitions);
        transaction.Commit();
        applicationPartition_ = transaction.Candidate;
    }

    private static EditorModuleDefinitionId CreateDefinitionId(EditorExtensionId extensionId)
    {
        return EditorModuleDefinitionId.Create(
            LegacyAssemblyId,
            ModuleLocalId.Create(extensionId.Value),
            EditorModuleScopeKind.Application);
    }

    private static void ValidateUniquePanelIds(
        IReadOnlyList<EditorDeclaredContributions> contributions)
    {
        var panelOwners = new Dictionary<string, EditorExtensionId>(StringComparer.Ordinal);

        foreach (var contribution in contributions)
        {
            foreach (var panel in contribution.Panels)
            {
                if (!panelOwners.TryAdd(panel.Id, contribution.OwnerId))
                {
                    throw new InvalidOperationException(
                        $"Panel id '{panel.Id}' is contributed by both "
                        + $"'{panelOwners[panel.Id]}' and '{contribution.OwnerId}'.");
                }
            }
        }
    }

    private static void ValidateUniqueActionIds(
        IReadOnlyList<EditorDeclaredContributions> contributions)
    {
        var actionOwners = new Dictionary<string, EditorExtensionId>(StringComparer.Ordinal);

        foreach (var contribution in contributions)
        {
            foreach (var action in contribution.Actions)
            {
                if (!actionOwners.TryAdd(action.Id, contribution.OwnerId))
                {
                    throw new InvalidOperationException(
                        $"Workbench action id '{action.Id}' is contributed by both "
                        + $"'{actionOwners[action.Id]}' and '{contribution.OwnerId}'.");
                }
            }
        }
    }

    private static void ValidateUniqueSceneProviderIds(
        IReadOnlyList<EditorDeclaredContributions> contributions)
    {
        var providerOwners = new Dictionary<string, EditorExtensionId>(StringComparer.Ordinal);

        foreach (var contribution in contributions)
        {
            foreach (var sceneProvider in contribution.SceneProviders)
            {
                if (!providerOwners.TryAdd(sceneProvider.Id, contribution.OwnerId))
                {
                    throw new InvalidOperationException(
                        $"Scene provider id '{sceneProvider.Id}' is contributed by both "
                        + $"'{providerOwners[sceneProvider.Id]}' and '{contribution.OwnerId}'.");
                }
            }
        }
    }

    private static void ValidateUniqueSceneProviderRoles(
        IReadOnlyList<EditorDeclaredContributions> contributions)
    {
        var providerOwners = new Dictionary<string, EditorExtensionId>(StringComparer.Ordinal);

        foreach (var contribution in contributions)
        {
            foreach (var sceneProvider in contribution.SceneProviders)
            {
                if (!providerOwners.TryAdd(sceneProvider.Role, contribution.OwnerId))
                {
                    throw new InvalidOperationException(
                        $"Scene provider role '{sceneProvider.Role}' is contributed by both "
                        + $"'{providerOwners[sceneProvider.Role]}' and '{contribution.OwnerId}'.");
                }
            }
        }
    }

    private static void ValidateContributionDescriptorSets(
        IReadOnlyList<EditorDeclaredContributions> contributions)
    {
        var adapter = new BuiltInContributionDescriptorAdapter();
        var validator = new EditorContributionDescriptorValidator();
        var registeredPanelIds = new List<string>();
        var registeredActionIds = new List<string>();

        foreach (var contribution in contributions)
        {
            var descriptorSet = adapter.Adapt(contribution);
            var validation = validator.Validate(
                descriptorSet,
                new EditorContributionValidationContext(
                    registeredPanelIds,
                    registeredActionIds,
                    RegisteredDiagnosticSourceIds: []));

            if (!validation.IsValid)
            {
                throw CreateContributionValidationException(validation);
            }

            foreach (var panel in descriptorSet.Panels)
            {
                registeredPanelIds.Add(panel.Id);
            }

            foreach (var action in descriptorSet.Actions)
            {
                registeredActionIds.Add(action.Id);
            }
        }
    }

    private static InvalidOperationException CreateContributionValidationException(
        EditorContributionValidationResult validation)
    {
        var messages = validation.Errors.Select(error =>
            $"[{error.SourceId}] {error.ContributionId}.{error.Field}: {error.Message}");
        return new InvalidOperationException(
            "Editor contribution descriptor validation failed. "
            + string.Join(" ", messages));
    }

    private static IReadOnlyList<Exception> DisposeRegistrationLeases(
        List<IDisposable> leases)
    {
        var exceptions = new List<Exception>();

        try
        {
            for (var index = leases.Count - 1; index >= 0; index--)
            {
                try
                {
                    leases[index].Dispose();
                }
                catch (Exception exception)
                {
                    exceptions.Add(exception);
                }
            }
        }
        finally
        {
            leases.Clear();
        }

        return exceptions;
    }

    private static async ValueTask AddDisposeFailureAsync(
        ICollection<Exception> failures,
        ValueTask disposal)
    {
        try
        {
            await disposal;
        }
        catch (AggregateException exception)
        {
            foreach (var innerException in exception.InnerExceptions)
            {
                failures.Add(innerException);
            }
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }
    }

    private static void ThrowFailures(IReadOnlyList<Exception> failures)
    {
        if (failures.Count == 1)
        {
            throw failures[0];
        }

        if (failures.Count > 1)
        {
            throw new AggregateException(failures);
        }
    }

    private sealed class LegacyEditorModuleBridge(
        IEditorExtensionModule module,
        EditorModuleDefinitionId definitionId,
        EditorModuleDefinitionId? previousDefinitionId)
        : EditorModule
    {
        private EditorDeclaredContributions? contributions_;

        public EditorModuleDefinitionId DefinitionId { get; } = definitionId;

        public EditorDeclaredContributions Contributions =>
            contributions_ ?? throw new InvalidOperationException(
                $"Legacy module '{DefinitionId}' has not been configured.");

        public override void Configure(EditorModuleBuilder editor)
        {
            if (previousDefinitionId is not null)
            {
                editor.Dependencies.RequireModule(previousDefinitionId.Value);
            }

            var legacyBuilder = new EditorContributionBuilder();
            module.Declare(legacyBuilder);
            contributions_ = legacyBuilder.Build(module.Id);
        }

        public override async ValueTask<IEditorModuleActivation> ActivateAsync(
            EditorModuleContext context,
            CancellationToken cancellationToken)
        {
            var lease = await module.ActivateAsync(
                EditorExtensionActivationContext.Instance,
                cancellationToken);
            return lease is null
                ? EditorModuleActivation.Empty
                : new LegacyEditorModuleActivation(lease);
        }
    }

    private sealed class LegacyEditorModuleActivation(IAsyncDisposable lease)
        : IEditorModuleActivation
    {
        public ValueTask<EditorModuleQuiesceResult> QuiesceAsync(
            EditorModuleStopReason reason,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(EditorModuleQuiesceResult.Ready);
        }

        public ValueTask ResumeAsync(
            EditorModuleResumeContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }

        public ValueTask DisposeAsync()
        {
            return lease.DisposeAsync();
        }
    }
}
