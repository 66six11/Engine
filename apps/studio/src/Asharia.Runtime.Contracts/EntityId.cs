using System.Runtime.InteropServices;

namespace Asharia.Runtime;

[StructLayout(LayoutKind.Explicit, Size = 8)]
public readonly record struct EntityId(
    [field: FieldOffset(0)] uint Index,
    [field: FieldOffset(4)] uint Generation)
{
    public static EntityId Invalid { get; } = default;

    public bool IsValid => Index != 0 && Generation != 0;
}
