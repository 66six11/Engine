using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Shell.Commands;

public sealed class WorkbenchActionRegistry : IWorkbenchActionRegistry
{
    private readonly Dictionary<string, WorkbenchActionDescriptor> descriptors_ = new(StringComparer.Ordinal);
    private readonly List<WorkbenchActionDescriptor> descriptorsInRegistrationOrder_ = [];

    public void Register(WorkbenchActionDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);

        if (string.IsNullOrWhiteSpace(descriptor.Id))
        {
            throw new ArgumentException("Workbench action id must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.Title))
        {
            throw new ArgumentException("Workbench action title must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.MenuPath))
        {
            throw new ArgumentException("Workbench action menu path must not be empty.", nameof(descriptor));
        }

        if (string.IsNullOrWhiteSpace(descriptor.Category))
        {
            throw new ArgumentException("Workbench action category must not be empty.", nameof(descriptor));
        }

        if (!descriptor.IsEnabled && string.IsNullOrWhiteSpace(descriptor.DisabledReason))
        {
            throw new ArgumentException("Disabled workbench actions must specify a disabled reason.", nameof(descriptor));
        }

        if (descriptor.Kind == WorkbenchActionKind.OpenPanel
            && string.IsNullOrWhiteSpace(descriptor.TargetId))
        {
            throw new ArgumentException("OpenPanel workbench actions must specify a target panel id.", nameof(descriptor));
        }

        if (!descriptors_.TryAdd(descriptor.Id, descriptor))
        {
            throw new InvalidOperationException($"Workbench action id '{descriptor.Id}' is already registered.");
        }

        descriptorsInRegistrationOrder_.Add(descriptor);
    }

    public IReadOnlyList<WorkbenchActionDescriptor> GetAll()
    {
        return descriptorsInRegistrationOrder_.ToArray();
    }

    public WorkbenchActionDescriptor? FindById(string id)
    {
        ArgumentNullException.ThrowIfNull(id);

        return descriptors_.GetValueOrDefault(id);
    }
}
