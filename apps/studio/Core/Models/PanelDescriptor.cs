using System;

namespace Editor.Core.Models;

public sealed record PanelDescriptor(
    string Id,
    string Title,
    PanelKind Kind,
    DockArea DefaultArea,
    string MenuPath,
    DockContentCachePolicy CachePolicy,
    Func<object> CreateContent,
    string? IconKey = null);
