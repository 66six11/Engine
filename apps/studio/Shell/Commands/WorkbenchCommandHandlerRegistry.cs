using System;
using System.Collections.Generic;
using Editor.Core.Models.Workbench;

namespace Editor.Shell.Commands;

internal sealed class WorkbenchCommandHandlerRegistry
{
    private readonly Dictionary<string, Func<WorkbenchActionDescriptor, bool>> handlers_ =
        new(StringComparer.Ordinal);

    public static WorkbenchCommandHandlerRegistry CreateBuiltIn(
        IReadOnlyList<WorkbenchActionDescriptor> actions,
        PanelCommandService panelCommandService,
        Func<bool>? openCommandPalette = null,
        Func<bool>? openAboutDialog = null)
    {
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentNullException.ThrowIfNull(panelCommandService);

        var registry = new WorkbenchCommandHandlerRegistry();
        foreach (var action in actions)
        {
            ArgumentNullException.ThrowIfNull(action);
            switch (action.Kind)
            {
                case WorkbenchActionKind.OpenPanel:
                    registry.Register(
                        action.Id,
                        descriptor => panelCommandService.OpenOrFocusPanel(descriptor.TargetId));
                    break;

                case WorkbenchActionKind.OpenCommandPalette:
                    registry.Register(
                        action.Id,
                        _ => openCommandPalette?.Invoke() ?? false);
                    break;

                case WorkbenchActionKind.OpenAboutDialog:
                    registry.Register(
                        action.Id,
                        _ => openAboutDialog?.Invoke() ?? false);
                    break;
            }
        }

        return registry;
    }

    public void Register(
        string commandId,
        Func<WorkbenchActionDescriptor, bool> handler)
    {
        if (string.IsNullOrWhiteSpace(commandId))
        {
            throw new ArgumentException("Workbench command id must not be empty.", nameof(commandId));
        }

        ArgumentNullException.ThrowIfNull(handler);

        if (!handlers_.TryAdd(commandId, handler))
        {
            throw new InvalidOperationException(
                $"Workbench command handler '{commandId}' is already registered.");
        }
    }

    public bool TryExecute(
        WorkbenchActionDescriptor action,
        out bool completed)
    {
        ArgumentNullException.ThrowIfNull(action);

        if (!handlers_.TryGetValue(action.Id, out var handler))
        {
            completed = false;
            return false;
        }

        completed = handler(action);
        return true;
    }
}
