namespace Asharia.Studio.Application.Projects;

internal interface IRecentProjectStore
{
    string? Read();

    void Write(string projectRoot);
}
