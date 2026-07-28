using System.Runtime.InteropServices;

namespace Asharia.Runtime;

[StructLayout(LayoutKind.Explicit, Size = 12)]
public readonly record struct Float3(
    [field: FieldOffset(0)] float X,
    [field: FieldOffset(4)] float Y,
    [field: FieldOffset(8)] float Z)
{
    public static Float3 Zero { get; } = new(0.0f, 0.0f, 0.0f);

    public static Float3 One { get; } = new(1.0f, 1.0f, 1.0f);
}
