using System;
using System.Collections.Generic;
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
    public void New_commit_clears_redo_history()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        CommitSingleCommand(service, () => value, next => value = next, "0,0,0", "1,0,0");
        service.Undo();

        CommitSingleCommand(service, () => value, next => value = next, "0,0,0", "2,0,0");

        Assert.Equal("2,0,0", value);
        Assert.True(service.Current.CanUndo);
        Assert.False(service.Current.CanRedo);
    }

    [Fact]
    public void Empty_commit_closes_transaction_without_dirtying_document()
    {
        var service = new EditorTransactionService();
        var transactionId = service.Begin("No-op");

        service.Commit(transactionId);

        Assert.Null(service.Current.ActiveTransactionId);
        Assert.False(service.Current.IsDirty);
        Assert.False(service.Current.CanUndo);
        Assert.False(service.Current.CanRedo);
    }

    [Fact]
    public void Commit_rejects_inactive_transaction_id_without_closing_active_transaction()
    {
        var service = new EditorTransactionService();
        var transactionId = service.Begin("Move Cube");

        var exception = Assert.Throws<InvalidOperationException>(
            () => service.Commit(EditorTransactionId.NewId()));

        Assert.Contains("is not active", exception.Message, StringComparison.Ordinal);
        Assert.Equal(transactionId, service.Current.ActiveTransactionId);
        Assert.False(service.Current.IsDirty);
    }

    [Fact]
    public void Rollback_rejects_inactive_transaction_id_without_reverting_pending_commands()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var transactionId = service.Begin("Move Cube");
        var command = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0");
        service.AddCommand(transactionId, command);

        var exception = Assert.Throws<InvalidOperationException>(
            () => service.Rollback(EditorTransactionId.NewId()));

        Assert.Contains("is not active", exception.Message, StringComparison.Ordinal);
        Assert.Equal("1,0,0", value);
        Assert.Equal(0, command.RevertCount);
        Assert.Equal(transactionId, service.Current.ActiveTransactionId);
    }

    [Fact]
    public void Add_command_rejects_inactive_transaction_id_without_applying()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var transactionId = service.Begin("Move Cube");
        var command = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0");

        var exception = Assert.Throws<InvalidOperationException>(
            () => service.AddCommand(EditorTransactionId.NewId(), command));

        Assert.Contains("is not active", exception.Message, StringComparison.Ordinal);
        Assert.Equal("0,0,0", value);
        Assert.Equal(0, command.ApplyCount);
        Assert.Equal(transactionId, service.Current.ActiveTransactionId);
    }

    [Fact]
    public void Undo_and_redo_are_rejected_while_transaction_is_active()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        CommitSingleCommand(service, () => value, next => value = next, "0,0,0", "1,0,0");
        service.Begin("Move Again");

        var undoException = Assert.Throws<InvalidOperationException>(service.Undo);
        var redoException = Assert.Throws<InvalidOperationException>(service.Redo);

        Assert.Contains("active", undoException.Message, StringComparison.Ordinal);
        Assert.Contains("active", redoException.Message, StringComparison.Ordinal);
        Assert.Equal("1,0,0", value);
        Assert.True(service.Current.CanUndo);
    }

    [Fact]
    public void Rollback_and_undo_revert_multiple_commands_in_reverse_order()
    {
        var values = new[] { "0", "0" };
        var service = new EditorTransactionService();
        var first = CapturingEditCommand.Create(
            () => values[0],
            next => values[0] = next,
            oldValue: "0",
            newValue: "1");
        var second = CapturingEditCommand.Create(
            () => values[1],
            next => values[1] = next,
            oldValue: "0",
            newValue: "2");
        var rollbackOrder = new List<string>();
        first.Reverted += () => rollbackOrder.Add("first");
        second.Reverted += () => rollbackOrder.Add("second");
        var transactionId = service.Begin("Move Two Objects");
        service.AddCommand(transactionId, first);
        service.AddCommand(transactionId, second);

        service.Rollback(transactionId);

        Assert.Equal(["second", "first"], rollbackOrder);
        Assert.Equal(["0", "0"], values);

        var undoOrder = new List<string>();
        first = CapturingEditCommand.Create(
            () => values[0],
            next => values[0] = next,
            oldValue: "0",
            newValue: "1");
        second = CapturingEditCommand.Create(
            () => values[1],
            next => values[1] = next,
            oldValue: "0",
            newValue: "2");
        first.Reverted += () => undoOrder.Add("first");
        second.Reverted += () => undoOrder.Add("second");
        transactionId = service.Begin("Move Two Objects");
        service.AddCommand(transactionId, first);
        service.AddCommand(transactionId, second);
        service.Commit(transactionId);

        service.Undo();

        Assert.Equal(["second", "first"], undoOrder);
        Assert.Equal(["0", "0"], values);
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
    public void Apply_failure_rolls_back_pending_transaction_and_records_diagnostic()
    {
        var value = "0,0,0";
        var service = new EditorTransactionService();
        var firstCommand = CapturingEditCommand.Create(
            () => value,
            next => value = next,
            oldValue: "0,0,0",
            newValue: "1,0,0");
        var failingCommand = FailingApplyCommand.Create(
            oldValue: "1,0,0",
            newValue: "2,0,0",
            message: "Bridge write failed.");
        var transactionId = service.Begin("Move Cube");
        service.AddCommand(transactionId, firstCommand);

        var exception = Assert.Throws<InvalidOperationException>(
            () => service.AddCommand(transactionId, failingCommand));

        Assert.Contains("Bridge write failed", exception.Message, StringComparison.Ordinal);
        Assert.Equal("0,0,0", value);
        Assert.Equal(1, firstCommand.ApplyCount);
        Assert.Equal(1, firstCommand.RevertCount);
        Assert.Equal(1, failingCommand.ApplyCount);
        Assert.False(service.Current.IsDirty);
        Assert.False(service.Current.CanUndo);
        Assert.False(service.Current.CanRedo);
        Assert.Null(service.Current.ActiveTransactionId);
        Assert.Contains(
            service.Current.Diagnostics,
            diagnostic => diagnostic.Contains("Bridge write failed", StringComparison.Ordinal));
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
        public event Action? Reverted;

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
            Reverted?.Invoke();
        }
    }

    private static void CommitSingleCommand(
        EditorTransactionService service,
        Func<string> getValue,
        Action<string> setValue,
        string oldValue,
        string newValue)
    {
        var transactionId = service.Begin("Move Cube");
        service.AddCommand(
            transactionId,
            CapturingEditCommand.Create(getValue, setValue, oldValue, newValue));
        service.Commit(transactionId);
    }

    private sealed class FailingApplyCommand(EditorEditCommandDescriptor descriptor) : IEditorEditCommand
    {
        public int ApplyCount { get; private set; }

        public EditorEditCommandDescriptor Descriptor { get; } = descriptor;

        public static FailingApplyCommand Create(string oldValue, string newValue, string message)
        {
            return new FailingApplyCommand(
                new EditorEditCommandDescriptor(
                    targetId: "scene:main/cube",
                    fieldId: "transform.position",
                    oldValue: oldValue,
                    newValue: newValue,
                    displayLabel: "Move Cube",
                    validation: EditorEditValidationResult.Valid,
                    mergePolicy: EditorEditMergePolicy.None))
            {
                Message = message,
            };
        }

        public string Message { get; private init; } = string.Empty;

        public void Apply()
        {
            ApplyCount++;
            throw new InvalidOperationException(Message);
        }

        public void Revert()
        {
            throw new InvalidOperationException("Failing apply command should not be reverted.");
        }
    }
}
