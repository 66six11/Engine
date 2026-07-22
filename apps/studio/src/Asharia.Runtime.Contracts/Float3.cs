namespace Asharia.Runtime;

public readonly record struct Float3(float X, float Y, float Z)
{
    public static Float3 Zero { get; } = new(0.0f, 0.0f, 0.0f);

    public static Float3 One { get; } = new(1.0f, 1.0f, 1.0f);
}
