using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Commands;
using Editor.Shell.Docking;

namespace Editor.Shell.Composition;

internal sealed class EditorExtensionHost(IEnumerable<IEditorExtensionModule> modules) : IAsyncDisposable
{
    private readonly IEditorExtensionModule[] modules_ = CreateModuleArray(modules);
    private readonly List<IAsyncDisposable> activationLeases_ = [];
    private readonly List<IDisposable> contributionLeases_ = [];

    public EditorExtensionComposition Compose()
    {
        ValidateUniqueExtensionIds();

        var contributions = DeclareContributions();
        ValidateUniquePanelIds(contributions);
        ValidateUniqueActionIds(contributions);
        ValidateUniqueSceneProviderIds(contributions);
        ValidateUniqueSceneProviderRoles(contributions);

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
        var startedLeases = new List<IAsyncDisposable>();

        try
        {
            foreach (var module in modules_)
            {
                cancellationToken.ThrowIfCancellationRequested();

                var lease = await module.ActivateAsync(
                    EditorExtensionActivationContext.Instance,
                    cancellationToken);

                if (lease is not null)
                {
                    startedLeases.Add(lease);
                }
            }
        }
        catch (Exception activationException)
        {
            var disposeExceptions = await DisposeLeasesAsync(startedLeases);
            disposeExceptions.AddRange(DisposeRegistrationLeases(contributionLeases_));
            if (disposeExceptions.Count > 0)
            {
                throw new AggregateException(
                    [activationException, .. disposeExceptions]);
            }

            throw;
        }

        activationLeases_.AddRange(startedLeases);
    }

    public async ValueTask DisposeAsync()
    {
        var disposeExceptions = await DisposeLeasesAsync(activationLeases_);
        disposeExceptions.AddRange(DisposeRegistrationLeases(contributionLeases_));
        if (disposeExceptions.Count > 0)
        {
            throw new AggregateException(disposeExceptions);
        }
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

    private EditorDeclaredContributions[] DeclareContributions()
    {
        var contributions = new EditorDeclaredContributions[modules_.Length];

        for (var index = 0; index < modules_.Length; index++)
        {
            var module = modules_[index];
            var builder = new EditorContributionBuilder();
            module.Declare(builder);
            contributions[index] = builder.Build(module.Id);
        }

        return contributions;
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

    private static async ValueTask<List<Exception>> DisposeLeasesAsync(
        List<IAsyncDisposable> leases)
    {
        var exceptions = new List<Exception>();

        try
        {
            for (var index = leases.Count - 1; index >= 0; index--)
            {
                try
                {
                    await leases[index].DisposeAsync();
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
}
