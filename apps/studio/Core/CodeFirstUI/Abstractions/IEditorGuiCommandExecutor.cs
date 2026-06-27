using Editor.Core.Models;

namespace Editor.Core.CodeFirstUI;

public interface IEditorGuiCommandExecutor
{
    WorkbenchCommandExecutionResult Execute(string commandId);
}
