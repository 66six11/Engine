using System;
using Asharia.Editor.Contributions;

namespace Asharia.Editor.Panels;

public sealed record EditorPanelDescriptor
{
    public EditorPanelDescriptor(
        EditorContributionId id,
        string title,
        EditorPanelKind kind,
        EditorDockPreference defaultDock,
        EditorPanelCachePolicy cachePolicy,
        UiBackendId backend,
        EditorFactoryLocalId contentFactory)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Panel contribution identity is invalid.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(title))
        {
            throw new ArgumentException("Panel title must not be empty.", nameof(title));
        }

        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, "Panel kind is invalid.");
        }

        if (!Enum.IsDefined(defaultDock))
        {
            throw new ArgumentOutOfRangeException(
                nameof(defaultDock),
                defaultDock,
                "Panel dock preference is invalid.");
        }

        if (!Enum.IsDefined(cachePolicy))
        {
            throw new ArgumentOutOfRangeException(
                nameof(cachePolicy),
                cachePolicy,
                "Panel cache policy is invalid.");
        }

        if (!backend.IsValid)
        {
            throw new ArgumentException("UI backend identity is invalid.", nameof(backend));
        }

        if (!contentFactory.IsValid)
        {
            throw new ArgumentException(
                "Panel content factory identity is invalid.",
                nameof(contentFactory));
        }

        Id = id;
        Title = title;
        Kind = kind;
        DefaultDock = defaultDock;
        CachePolicy = cachePolicy;
        Backend = backend;
        ContentFactory = contentFactory;
    }

    public EditorContributionId Id { get; }

    public string Title { get; }

    public EditorPanelKind Kind { get; }

    public EditorDockPreference DefaultDock { get; }

    public EditorPanelCachePolicy CachePolicy { get; }

    public UiBackendId Backend { get; }

    public EditorFactoryLocalId ContentFactory { get; }
}
