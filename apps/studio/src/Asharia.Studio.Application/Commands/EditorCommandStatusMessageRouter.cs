using System;
using Asharia.Editor.Commands;
using Asharia.Editor.UI.CodeFirst.Abstractions;

namespace Asharia.Studio.Application.Commands;

public sealed class EditorCommandStatusMessageRouter : IEditorGuiCommandExecutor
{
    private readonly IEditorGuiCommandExecutor inner_;
    private readonly Action<EditorCommandExecutionResult> publishResult_;

    public EditorCommandStatusMessageRouter(
        IEditorGuiCommandExecutor inner,
        Action<EditorCommandExecutionResult> publishResult)
    {
        ArgumentNullException.ThrowIfNull(inner);
        ArgumentNullException.ThrowIfNull(publishResult);

        inner_ = inner;
        publishResult_ = publishResult;
    }

    public EditorCommandExecutionResult Execute(string commandId)
    {
        var result = inner_.Execute(commandId);
        publishResult_(result);
        return result;
    }
}
