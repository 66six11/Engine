using System.Runtime.InteropServices;

namespace Asharia.Runtime;

[StructLayout(LayoutKind.Explicit, Size = 16)]
public readonly record struct Quaternion(
    [field: FieldOffset(0)] float X,
    [field: FieldOffset(4)] float Y,
    [field: FieldOffset(8)] float Z,
    [field: FieldOffset(12)] float W)
{
    public static Quaternion Identity { get; } = new(0.0f, 0.0f, 0.0f, 1.0f);
}
