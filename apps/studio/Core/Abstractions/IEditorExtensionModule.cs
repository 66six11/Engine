using System;
using System.Threading;
using System.Threading.Tasks;
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface IEditorExtensionModule
{
    EditorExtensionId Id { get; }

    void Declare(IEditorContributionBuilder builder);

    ValueTask<IAsyncDisposable?> ActivateAsync(
        IEditorExtensionActivationContext context,
        CancellationToken cancellationToken)
    {
        return ValueTask.FromResult<IAsyncDisposable?>(null);
    }
}
