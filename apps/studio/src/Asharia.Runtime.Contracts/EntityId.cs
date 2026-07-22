namespace Asharia.Runtime;

public readonly record struct EntityId(uint Index, uint Generation)
{
    public static EntityId Invalid { get; } = default;

    public bool IsValid => Index != 0 && Generation != 0;
}
