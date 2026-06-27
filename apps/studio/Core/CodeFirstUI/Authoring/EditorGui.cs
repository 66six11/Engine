using System;
using System.Collections.Generic;
using System.Linq;
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

    public void Text(string key, string text)
    {
        Label(key, text);
    }

    public bool Button(string key, string label)
    {
        var nodeId = builder_.Button(key, label);
        return events_.ConsumeButtonClicked(nodeId);
    }

    public string TextInput(
        string key,
        string label,
        string text = "",
        GuiTextInputCommitMode commitMode = GuiTextInputCommitMode.OnLostFocus)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.TextField);
        var resolvedText = StateStore.TryGetText(nodeId, out var storedText)
            ? storedText ?? string.Empty
            : text;
        StateStore.SetText(nodeId, resolvedText);
        builder_.TextField(key, label, resolvedText, commitMode);
        return resolvedText;
    }

    public bool Toggle(
        string key,
        string label,
        bool isChecked = false)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Toggle);
        var resolvedValue = StateStore.TryGetToggle(nodeId, out var storedValue)
            ? storedValue
            : isChecked;
        StateStore.SetToggle(nodeId, resolvedValue);
        builder_.Toggle(key, label, resolvedValue);
        return resolvedValue;
    }

    public string? List(
        string key,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.List);
        var selected = ResolveListSelection(nodeId, items, selectedItemId);
        if (selected is not null)
        {
            StateStore.SetSelectedItem(nodeId, selected);
        }

        builder_.List(key, items, selected);
        return selected;
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

    public IDisposable Horizontal(string key)
    {
        return builder_.Horizontal(key);
    }

    public IDisposable Panel(string key, string label)
    {
        return builder_.Panel(key, label);
    }

    public IDisposable Split(
        string key,
        GuiSplitDirection direction,
        double ratio)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Split);
        var resolvedRatio = StateStore.TryGetSplitRatio(nodeId, out var storedRatio)
            ? storedRatio
            : ratio;
        return builder_.Split(key, direction, resolvedRatio);
    }

    public IDisposable Vertical(string key)
    {
        return builder_.Vertical(key);
    }

    private string? ResolveListSelection(
        GuiNodeId nodeId,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId)
    {
        if (items.Count == 0)
        {
            return null;
        }

        if (StateStore.TryGetSelectedItem(nodeId, out var storedSelection)
            && ContainsItem(items, storedSelection))
        {
            return storedSelection;
        }

        if (ContainsItem(items, selectedItemId))
        {
            return selectedItemId;
        }

        return items[0].Id;
    }

    private static bool ContainsItem(
        IReadOnlyList<GuiListItem> items,
        string? itemId)
    {
        return !string.IsNullOrWhiteSpace(itemId)
            && items.Any(item => string.Equals(item.Id, itemId, StringComparison.Ordinal));
    }
}
