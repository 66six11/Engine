using System;
using System.Collections.Generic;

namespace Editor.Core.Models;

public sealed record SceneObjectSnapshot
{
    public SceneObjectSnapshot(
        string id,
        string displayName,
        string kind,
        string? parentId = null,
        string? iconKey = null,
        bool isActive = true,
        IReadOnlyList<SceneObjectPropertySnapshot>? properties = null)
    {
        if (string.IsNullOrWhiteSpace(id))
        {
            throw new ArgumentException("Scene object id cannot be null or whitespace.", nameof(id));
        }

        if (string.IsNullOrWhiteSpace(kind))
        {
            throw new ArgumentException("Scene object kind cannot be null or whitespace.", nameof(kind));
        }

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? id : displayName;
        Kind = kind;
        ParentId = string.IsNullOrWhiteSpace(parentId) ? null : parentId;
        IconKey = string.IsNullOrWhiteSpace(iconKey) ? null : iconKey;
        IsActive = isActive;
        Properties = Array.AsReadOnly(
            properties is null ? Array.Empty<SceneObjectPropertySnapshot>() : [.. properties]);
    }

    public string Id { get; }

    public string DisplayName { get; }

    public string Kind { get; }

    public string? ParentId { get; }

    public string? IconKey { get; }

    public bool IsActive { get; }

    public IReadOnlyList<SceneObjectPropertySnapshot> Properties { get; }

    public EditorSelectionItem ToSelectionItem()
    {
        return new EditorSelectionItem(Id, Kind, DisplayName, IconKey);
    }
}
