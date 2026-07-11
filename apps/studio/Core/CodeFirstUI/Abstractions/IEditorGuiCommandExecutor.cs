using Asharia.Editor.Commands;

namespace Editor.Core.CodeFirstUI.Abstractions;

public interface IEditorGuiCommandExecutor
{
    EditorCommandExecutionResult Execute(string commandId);
}
