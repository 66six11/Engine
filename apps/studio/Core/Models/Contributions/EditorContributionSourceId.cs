namespace Editor.Core.Models.Contributions;

public sealed record EditorContributionSourceId(string Value)
{
    public override string ToString()
    {
        return Value;
    }
}
