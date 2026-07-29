using System;
using System.IO;
using System.Text;

namespace Asharia.Studio.Application.Projects;

internal sealed class RecentProjectStore : IRecentProjectStore
{
    private const long MaxRecentProjectBytes = 64 * 1024;
    private static readonly UTF8Encoding StrictUtf8Encoding = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    private readonly string path_;

    public RecentProjectStore(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException(
                "Recent-project path must not be null or whitespace.",
                nameof(path));
        }
        path_ = Path.GetFullPath(path);
    }

    public static RecentProjectStore CreateDefault()
    {
        var localApplicationData = Environment.GetFolderPath(
            Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(localApplicationData))
        {
            throw new InvalidOperationException(
                "The local application-data directory is unavailable.");
        }

        return new RecentProjectStore(Path.Combine(
            localApplicationData,
            "Asharia",
            "Studio",
            "recent-project.txt"));
    }

    public string? Read()
    {
        if (!File.Exists(path_))
        {
            return null;
        }

        var file = new FileInfo(path_);
        if (file.Length > MaxRecentProjectBytes)
        {
            throw new InvalidDataException(
                "The recent-project preference exceeds its size limit.");
        }

        var value = File.ReadAllText(path_, StrictUtf8Encoding);
        return string.IsNullOrWhiteSpace(value)
            ? null
            : value;
    }

    public void Write(string projectRoot)
    {
        if (string.IsNullOrWhiteSpace(projectRoot))
        {
            throw new ArgumentException(
                "Project root must not be null or whitespace.",
                nameof(projectRoot));
        }

        var directory = Path.GetDirectoryName(path_)
            ?? throw new InvalidOperationException(
                "Recent-project preference has no parent directory.");
        Directory.CreateDirectory(directory);

        var temporaryPath = path_ + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.WriteAllText(
                temporaryPath,
                projectRoot,
                StrictUtf8Encoding);
            File.Move(
                temporaryPath,
                path_,
                overwrite: true);
        }
        finally
        {
            TryDeleteTemporaryFile(temporaryPath);
        }
    }

    private static void TryDeleteTemporaryFile(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception exception) when (
            exception is IOException
            or UnauthorizedAccessException)
        {
        }
    }
}
