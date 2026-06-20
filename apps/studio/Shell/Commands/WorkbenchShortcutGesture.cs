using System;
using Avalonia.Input;

namespace Editor.Shell.Commands;

internal sealed record WorkbenchShortcutGesture(Key Key, KeyModifiers Modifiers)
{
    public static bool TryParse(string? shortcutText, out WorkbenchShortcutGesture gesture)
    {
        gesture = default!;
        if (string.IsNullOrWhiteSpace(shortcutText))
        {
            return false;
        }

        var key = Key.None;
        var modifiers = KeyModifiers.None;
        var parts = shortcutText.Split(
            '+',
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (parts.Length == 0)
        {
            return false;
        }

        foreach (var part in parts)
        {
            if (TryParseModifier(part, out var modifier))
            {
                modifiers |= modifier;
                continue;
            }

            if (key != Key.None || !Enum.TryParse<Key>(part, ignoreCase: true, out key))
            {
                return false;
            }
        }

        if (key == Key.None)
        {
            return false;
        }

        gesture = new WorkbenchShortcutGesture(key, modifiers);
        return true;
    }

    public bool Matches(Key key, KeyModifiers modifiers)
    {
        return Key == key && Modifiers == modifiers;
    }

    private static bool TryParseModifier(string text, out KeyModifiers modifier)
    {
        modifier = text switch
        {
            "Ctrl" or "Control" => KeyModifiers.Control,
            "Shift" => KeyModifiers.Shift,
            "Alt" => KeyModifiers.Alt,
            "Meta" or "Command" or "Cmd" => KeyModifiers.Meta,
            _ => KeyModifiers.None,
        };
        return modifier != KeyModifiers.None;
    }
}
