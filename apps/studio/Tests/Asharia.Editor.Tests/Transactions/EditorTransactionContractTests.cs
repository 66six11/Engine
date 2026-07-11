using System;
using Asharia.Editor.Transactions;
using Xunit;

namespace Asharia.Editor.Tests.Transactions;

public sealed class EditorTransactionContractTests
{
    [Fact]
    public void Transaction_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(EditorTransactionId),
            typeof(EditorTransactionServiceSnapshot),
            typeof(IEditorTransactionService),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Transactions", type.Namespace));
    }

    [Fact]
    public void New_transaction_ids_are_non_default_and_unique()
    {
        var first = EditorTransactionId.NewId();
        var second = EditorTransactionId.NewId();

        Assert.NotEqual(default, first);
        Assert.NotEqual(first, second);
    }

    [Fact]
    public void Clean_transaction_snapshot_has_no_pending_state()
    {
        var snapshot = EditorTransactionServiceSnapshot.Clean;

        Assert.Null(snapshot.ActiveTransactionId);
        Assert.False(snapshot.IsDirty);
        Assert.False(snapshot.CanUndo);
        Assert.False(snapshot.CanRedo);
        Assert.Empty(snapshot.Diagnostics);
    }
}
