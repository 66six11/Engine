namespace Editor.Core.Models;

public sealed record EditorContributionSourceId(string Value)
{
    public override string ToString()
    {
        return Value;
    }
}
