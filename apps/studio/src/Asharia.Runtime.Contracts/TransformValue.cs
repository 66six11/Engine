namespace Asharia.Runtime;

public readonly record struct TransformValue(
    Float3 Position,
    Quaternion Rotation,
    Float3 Scale)
{
    public static TransformValue Identity { get; } = new(
        Float3.Zero,
        Quaternion.Identity,
        Float3.One);
}
