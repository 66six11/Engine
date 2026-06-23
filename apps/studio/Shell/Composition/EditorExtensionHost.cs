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

    public EditorExtensionComposition Compose()
    {
        ValidateUniqueExtensionIds();

        var contributions = DeclareContributions();
        ValidateUniquePanelIds(contributions);
        ValidateUniqueActionIds(contributions);

        var panelRegistry = new PanelRegistry();
        var actionRegistry = new WorkbenchActionRegistry();

        foreach (var contribution in contributions)
        {
            foreach (var panel in contribution.Panels)
            {
                panelRegistry.Register(panel);
            }

            foreach (var action in contribution.Actions)
            {
                actionRegistry.Register(action);
            }
        }

        return new EditorExtensionComposition(panelRegistry, actionRegistry);
    }

    public async ValueTask ActivateAsync(CancellationToken cancellationToken = default)
    {
        var startedLeases = new List<IAsyncDisposable>();

        try
        {
            foreach (var module in modules_)
            {
                var lease = await module.ActivateAsync(
                    EditorExtensionActivationContext.Instance,
                    cancellationToken);

                if (lease is not null)
                {
                    startedLeases.Add(lease);
                }
            }
        }
        catch
        {
            await DisposeLeasesAsync(startedLeases);
            throw;
        }

        activationLeases_.AddRange(startedLeases);
    }

    public async ValueTask DisposeAsync()
    {
        await DisposeLeasesAsync(activationLeases_);
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

    private static async ValueTask DisposeLeasesAsync(List<IAsyncDisposable> leases)
    {
        try
        {
            for (var index = leases.Count - 1; index >= 0; index--)
            {
                await leases[index].DisposeAsync();
            }
        }
        finally
        {
            leases.Clear();
        }
    }
}
