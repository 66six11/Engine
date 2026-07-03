using System;

namespace Editor.Core.Models.Panels;

public sealed record PanelDescriptor(
    string Id,
    string Title,
    PanelKind Kind,
    DockArea DefaultArea,
    string MenuPath,
    DockContentCachePolicy CachePolicy,
    Func<object> CreateContent,
    string? IconKey = null,
    string? Tag = null,
    string? TitleDetail = null,
    string? StatusText = null);
