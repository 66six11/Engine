using System;

namespace Asharia.Editor.UI.CodeFirst.Authoring;

public readonly struct GuiFoldoutScope : IDisposable
{
    private readonly IDisposable scope_;

    public GuiFoldoutScope(
        IDisposable scope,
        bool isExpanded)
    {
        scope_ = scope ?? throw new ArgumentNullException(nameof(scope));
        IsExpanded = isExpanded;
    }

    public bool IsExpanded { get; }

    public void Dispose()
    {
        scope_.Dispose();
    }
}
