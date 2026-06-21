using System;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.Services;
using Xunit;

namespace Editor.Tests.Shell.Services;

public sealed class EditorTransactionServiceTests
{
    [Fact]
    public void Commit_publishes_dirty_undoable_snapshot()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var changeCount = 0;
        service.StateChanged += (_, _) => changeCount++;
        var command = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0");

        var transactionId = service.Begin("Move Cube");
        service.AddCommand(transactionId, command);
        service.Commit(transactionId);

        Assert.Equal("1,0,0", value);
        Assert.Equal(1, command.ApplyCount);
        Assert.Equal(0, command.RevertCount);
        Assert.True(service.Current.IsDirty);
        Assert.True(service.Current.CanUndo);
        Assert.False(service.Current.CanRedo);
        Assert.Null(service.Current.ActiveTransactionId);
        Assert.Empty(service.Current.Diagnostics);
        Assert.True(changeCount >= 3);
    }

    [Fact]
    public void Rollback_reverts_pending_commands_without_dirtying_document()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var command = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0");
        var transactionId = service.Begin("Move Cube");

        service.AddCommand(transactionId, command);
        service.Rollback(transactionId);

        Assert.Equal("0,0,0", value);
        Assert.Equal(1, command.ApplyCount);
        Assert.Equal(1, command.RevertCount);
        Assert.False(service.Current.IsDirty);
        Assert.False(service.Current.CanUndo);
        Assert.False(service.Current.CanRedo);
        Assert.Null(service.Current.ActiveTransactionId);
    }

    [Fact]
    public void Undo_and_redo_replay_committed_commands()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var command = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0");
        var transactionId = service.Begin("Move Cube");
        service.AddCommand(transactionId, command);
        service.Commit(transactionId);

        service.Undo();

        Assert.Equal("0,0,0", value);
        Assert.False(service.Current.IsDirty);
        Assert.False(service.Current.CanUndo);
        Assert.True(service.Current.CanRedo);

        service.Redo();

        Assert.Equal("1,0,0", value);
        Assert.True(service.Current.IsDirty);
        Assert.True(service.Current.CanUndo);
        Assert.False(service.Current.CanRedo);
        Assert.Equal(2, command.ApplyCount);
        Assert.Equal(1, command.RevertCount);
    }

    [Fact]
    public void Invalid_command_is_rejected_with_diagnostic_without_applying()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var command = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0",
            validation: EditorEditValidationResult.Invalid("Transform position is locked."));
        var transactionId = service.Begin("Move Cube");

        var exception = Assert.Throws<InvalidOperationException>(
            () => service.AddCommand(transactionId, command));

        Assert.Contains("Transform position is locked", exception.Message, StringComparison.Ordinal);
        Assert.Equal("0,0,0", value);
        Assert.Equal(0, command.ApplyCount);
        Assert.False(service.Current.IsDirty);
        Assert.Contains(
            service.Current.Diagnostics,
            diagnostic => diagnostic.Contains("Transform position is locked", StringComparison.Ordinal));
    }

    [Fact]
    public void Invalid_validation_result_requires_diagnostic_message()
    {
        var exception = Assert.Throws<ArgumentException>(
            () => new EditorEditValidationResult(false, " "));

        Assert.Equal("message", exception.ParamName);
    }

    private sealed class CapturingEditCommand(
        Func<string> getValue,
        Action<string> setValue,
        EditorEditCommandDescriptor descriptor) : IEditorEditCommand
    {
        public int ApplyCount { get; private set; }

        public int RevertCount { get; private set; }

        public EditorEditCommandDescriptor Descriptor { get; } = descriptor;

        public static CapturingEditCommand Create(
            Func<string> getValue,
            Action<string> setValue,
            string oldValue,
            string newValue,
            EditorEditValidationResult? validation = null)
        {
            return new CapturingEditCommand(
                getValue,
                setValue,
                new EditorEditCommandDescriptor(
                    targetId: "scene:main/cube",
                    fieldId: "transform.position",
                    oldValue: oldValue,
                    newValue: newValue,
                    displayLabel: "Move Cube",
                    validation: validation ?? EditorEditValidationResult.Valid,
                    mergePolicy: EditorEditMergePolicy.None));
        }

        public void Apply()
        {
            Assert.Equal(Descriptor.OldValue, getValue());
            ApplyCount++;
            setValue(Descriptor.NewValue);
        }

        public void Revert()
        {
            Assert.Equal(Descriptor.NewValue, getValue());
            RevertCount++;
            setValue(Descriptor.OldValue);
        }
    }
}
