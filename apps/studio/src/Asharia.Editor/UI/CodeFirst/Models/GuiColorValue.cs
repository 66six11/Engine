
namespace Asharia.Editor.UI.CodeFirst.Models;

public readonly record struct GuiColorValue(
    byte Red,
    byte Green,
    byte Blue,
    byte Alpha = byte.MaxValue);
