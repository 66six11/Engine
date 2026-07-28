using System.Runtime.InteropServices;

namespace Asharia.Runtime;

[StructLayout(LayoutKind.Explicit, Size = 40)]
public readonly record struct TransformValue(
    [field: FieldOffset(0)] Float3 Position,
    [field: FieldOffset(12)] Quaternion Rotation,
    [field: FieldOffset(28)] Float3 Scale)
{
    public static TransformValue Identity { get; } = new(
        Float3.Zero,
        Quaternion.Identity,
        Float3.One);
}
