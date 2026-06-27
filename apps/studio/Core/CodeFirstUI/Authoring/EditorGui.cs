using System;
using Editor.Core.Models;

namespace Editor.Core.CodeFirstUI;

public sealed class EditorGui
{
    private readonly GuiFrameBuilder builder_;
    private readonly IEditorGuiCommandExecutor commandExecutor_;
    private readonly GuiEventQueue events_;

    public EditorGui(
        GuiFrameBuilder builder,
        GuiEventQueue events,
        GuiStateStore stateStore,
        IEditorGuiCommandExecutor commandExecutor)
    {
        ArgumentNullException.ThrowIfNull(builder);
        ArgumentNullException.ThrowIfNull(events);
        ArgumentNullException.ThrowIfNull(stateStore);
        ArgumentNullException.ThrowIfNull(commandExecutor);

        builder_ = builder;
        events_ = events;
        StateStore = stateStore;
        commandExecutor_ = commandExecutor;
    }

    public GuiStateStore StateStore { get; }

    public void Label(string key, string label)
    {
        builder_.Label(key, label);
    }

    public bool Button(string key, string label)
    {
        var nodeId = builder_.Button(key, label);
        return events_.ConsumeButtonClicked(nodeId);
    }

    public WorkbenchCommandExecutionResult? CommandButton(
        string key,
        string label,
        string commandId)
    {
        return Button(key, label)
            ? ExecuteCommand(commandId)
            : null;
    }

    public WorkbenchCommandExecutionResult ExecuteCommand(string commandId)
    {
        return commandExecutor_.Execute(commandId);
    }

    public IDisposable Toolbar(string key)
    {
        return builder_.Toolbar(key);
    }

    public IDisposable Vertical(string key)
    {
        return builder_.Vertical(key);
    }
}
