using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Editor.Extensions;

public enum EditorModuleStopReason
{
    Reload,
    CapabilityLost,
    ScopeClosing,
}

public enum EditorModuleQuiesceResult
{
    Ready,
    RestartRequired,
}

public interface IEditorModuleActivation : IAsyncDisposable
{
    ValueTask<EditorModuleQuiesceResult> QuiesceAsync(
        EditorModuleStopReason reason,
        CancellationToken cancellationToken);

    ValueTask ResumeAsync(
        EditorModuleResumeContext context,
        CancellationToken cancellationToken);
}

public static class EditorModuleActivation
{
    public static IEditorModuleActivation Empty { get; } = new EmptyEditorModuleActivation();

    private sealed class EmptyEditorModuleActivation : IEditorModuleActivation
    {
        public ValueTask<EditorModuleQuiesceResult> QuiesceAsync(
            EditorModuleStopReason reason,
            CancellationToken cancellationToken)
        {
            if (!Enum.IsDefined(reason))
            {
                throw new ArgumentOutOfRangeException(
                    nameof(reason),
                    reason,
                    "Module stop reason is invalid.");
            }

            return ValueTask.FromResult(EditorModuleQuiesceResult.Ready);
        }

        public ValueTask ResumeAsync(
            EditorModuleResumeContext context,
            CancellationToken cancellationToken)
        {
            ArgumentNullException.ThrowIfNull(context);
            return ValueTask.CompletedTask;
        }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
