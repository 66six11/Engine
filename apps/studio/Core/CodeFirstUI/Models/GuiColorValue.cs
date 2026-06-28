namespace Editor.Core.CodeFirstUI;

public readonly record struct GuiColorValue(
    byte Red,
    byte Green,
    byte Blue,
    byte Alpha = byte.MaxValue);
