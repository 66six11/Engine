using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiTreeValidator
{
    public GuiTreeValidationResult Validate(GuiTreeSnapshot tree)
    {
        ArgumentNullException.ThrowIfNull(tree);

        var errors = new List<GuiTreeValidationError>();
        ValidateNode(tree.Root, errors, isInsideScroll: false);

        return errors.Count == 0
            ? GuiTreeValidationResult.Success
            : new GuiTreeValidationResult(errors);
    }

    private static void ValidateNode(
        GuiNode node,
        ICollection<GuiTreeValidationError> errors,
        bool isInsideScroll)
    {
        if (isInsideScroll && IsVirtualizedContent(node.Kind))
        {
            errors.Add(new GuiTreeValidationError(
                GuiTreeValidationErrorCode.VirtualizedContentInsideScroll,
                node.Id.FullKeyPath,
                $"Virtualized GUI node '{node.Id.KeyPath}' must not be placed inside a scroll container."));
        }

        foreach (var duplicate in node.Children
                     .GroupBy(child => child.Id.KeyPath, StringComparer.Ordinal)
                     .Where(group => group.Count() > 1))
        {
            var duplicateNode = duplicate.First();
            errors.Add(new GuiTreeValidationError(
                GuiTreeValidationErrorCode.DuplicateKey,
                duplicateNode.Id.FullKeyPath,
                $"Duplicate GUI key '{duplicateNode.Id.KeyPath}'."));
        }

        var childIsInsideScroll = isInsideScroll || node.Kind == GuiNodeKind.Scroll;
        foreach (var child in node.Children)
        {
            ValidateNode(child, errors, childIsInsideScroll);
        }
    }

    private static bool IsVirtualizedContent(GuiNodeKind kind)
    {
        return kind == GuiNodeKind.List;
    }
}
