using System;
using System.Linq;
using Asharia.Studio.Application.Actions;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Commands;

internal static class StudioActionShortcutRouter
{
    public static bool TryRoute(
        StudioShellViewModel shell,
        StudioPresentationId topLevelId,
        StudioPresentationId? focusedPanelId,
        IInputElement? focusedElement,
        KeyEventArgs e)
    {
        ArgumentNullException.ThrowIfNull(shell);
        ArgumentNullException.ThrowIfNull(e);

        if (e.Handled || !topLevelId.IsValid || e.Key == Key.None ||
            IsTextInputOwner(focusedElement))
        {
            return false;
        }

        if (!TryNormalizePrimaryModifiers(e.KeyModifiers, out var modifiers))
        {
            return false;
        }

        return shell.TryExecuteShortcut(
            new StudioShortcutChord(e.Key.ToString(), modifiers),
            topLevelId,
            focusedPanelId);
    }

    internal static bool TryNormalizePrimaryModifiers(
        KeyModifiers keyModifiers,
        out StudioShortcutModifiers modifiers)
    {
        var hasControl = keyModifiers.HasFlag(KeyModifiers.Control);
        var hasMeta = keyModifiers.HasFlag(KeyModifiers.Meta);
        if (keyModifiers.HasFlag(KeyModifiers.Alt) || hasControl == hasMeta)
        {
            modifiers = StudioShortcutModifiers.None;
            return false;
        }

        // Registry chords express the platform-primary modifier as Control.
        // Exactly one of Ctrl/Command is required; mixed Ctrl+Command and Alt
        // combinations fail closed instead of accidentally selecting an action.
        modifiers = StudioShortcutModifiers.Control;
        if (keyModifiers.HasFlag(KeyModifiers.Shift))
        {
            modifiers |= StudioShortcutModifiers.Shift;
        }
        return true;
    }

    internal static bool IsTextInputOwner(IInputElement? focusedElement)
    {
        if (focusedElement is TextBox)
        {
            return true;
        }

        // Avalonia's IME client can focus a visual inside the TextBox. Preserve
        // both the text editor's own shortcuts and any active composition.
        return focusedElement is Visual visual &&
               visual.GetVisualAncestors().OfType<TextBox>().Any();
    }
}
