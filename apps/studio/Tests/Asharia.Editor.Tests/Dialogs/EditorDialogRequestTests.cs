using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Dialogs;
using Xunit;

namespace Asharia.Editor.Tests.Dialogs;

public sealed class EditorDialogRequestTests
{
    [Fact]
    public void Request_preserves_valid_values_and_defensively_freezes_actions()
    {
        var source = new List<EditorDialogActionDescriptor>
        {
            Action("save", EditorDialogActionRole.Primary, isDefault: true),
            Action("close", EditorDialogActionRole.Dismiss),
        };

        var request = new EditorDialogRequest(
            EditorDialogSeverity.Warning,
            title: null,
            "Unsaved changes",
            allowSystemDismiss: false,
            source);
        source.Clear();

        Assert.Equal(EditorDialogSeverity.Warning, request.Severity);
        Assert.Null(request.Title);
        Assert.Equal("Unsaved changes", request.Message);
        Assert.False(request.AllowSystemDismiss);
        Assert.Equal(["save", "close"], request.Actions.Select(action => action.Id.Value));
        var collection = Assert.IsAssignableFrom<ICollection<EditorDialogActionDescriptor>>(
            request.Actions);
        Assert.True(collection.IsReadOnly);
        Assert.Throws<NotSupportedException>(() => collection.Add(
            Action("other", EditorDialogActionRole.Secondary)));
    }

    [Fact]
    public void Request_accepts_one_two_or_three_actions()
    {
        Assert.Single(CreateRequest([Action("close", EditorDialogActionRole.Dismiss)]).Actions);
        Assert.Equal(2, CreateRequest(
            [
                Action("save", EditorDialogActionRole.Primary),
                Action("close", EditorDialogActionRole.Dismiss),
            ]).Actions.Count);
        Assert.Equal(3, CreateRequest(
            [
                Action("save", EditorDialogActionRole.Primary),
                Action("dont-save", EditorDialogActionRole.Secondary),
                Action("cancel", EditorDialogActionRole.Dismiss),
            ]).Actions.Count);
    }

    [Fact]
    public void Request_rejects_invalid_scalar_fields()
    {
        var actions = new[] { Action("close", EditorDialogActionRole.Dismiss) };

        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorDialogRequest(
            (EditorDialogSeverity)42,
            "Title",
            "Message",
            true,
            actions));
        Assert.Throws<ArgumentException>(() => new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "   ",
            "Message",
            true,
            actions));
        Assert.Throws<ArgumentException>(() => new EditorDialogRequest(
            EditorDialogSeverity.Information,
            null,
            "   ",
            true,
            actions));
    }

    [Fact]
    public void Request_rejects_invalid_action_collection_shape()
    {
        Assert.Throws<ArgumentNullException>(() => new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "Title",
            "Message",
            true,
            null!));
        Assert.Throws<ArgumentException>(() => CreateRequest([]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("one", EditorDialogActionRole.Primary),
                Action("two", EditorDialogActionRole.Secondary),
                Action("close", EditorDialogActionRole.Dismiss),
                Action("four", EditorDialogActionRole.Primary),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [null!, Action("close", EditorDialogActionRole.Dismiss)]));
    }

    [Fact]
    public void Request_rejects_duplicate_ids_roles_and_missing_dismiss()
    {
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("same", EditorDialogActionRole.Primary),
                Action("same", EditorDialogActionRole.Dismiss),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("one", EditorDialogActionRole.Primary),
                Action("two", EditorDialogActionRole.Primary),
                Action("close", EditorDialogActionRole.Dismiss),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [Action("save", EditorDialogActionRole.Primary)]));
    }

    [Fact]
    public void Request_rejects_invalid_emphasis_and_destructive_dismiss()
    {
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("save", EditorDialogActionRole.Primary, isDefault: true),
                Action("close", EditorDialogActionRole.Dismiss, isDefault: true),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("delete", EditorDialogActionRole.Primary, isDestructive: true),
                Action("close", EditorDialogActionRole.Dismiss, isDestructive: true),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [
                Action("delete", EditorDialogActionRole.Primary, isDestructive: true),
                Action("close", EditorDialogActionRole.Dismiss, isDefault: true),
            ]));
        Assert.Throws<ArgumentException>(() => CreateRequest(
            [Action("close", EditorDialogActionRole.Dismiss, isDestructive: true)]));
    }

    private static EditorDialogRequest CreateRequest(
        IReadOnlyList<EditorDialogActionDescriptor> actions)
    {
        return new EditorDialogRequest(
            EditorDialogSeverity.Information,
            "Title",
            "Message",
            allowSystemDismiss: true,
            actions);
    }

    private static EditorDialogActionDescriptor Action(
        string id,
        EditorDialogActionRole role,
        bool isDefault = false,
        bool isDestructive = false)
    {
        return new EditorDialogActionDescriptor(
            EditorDialogActionId.Create(id),
            id,
            role,
            isDefault,
            isDestructive);
    }
}
