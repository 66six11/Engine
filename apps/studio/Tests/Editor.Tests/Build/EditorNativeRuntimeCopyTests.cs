using System;
using System.IO;
using Xunit;

namespace Editor.Tests.Build;

public sealed class EditorNativeRuntimeCopyTests
{
    [Fact]
    public void Studio_output_contains_native_viewport_runtime_dependencies()
    {
        var outputDirectory = Path.Combine(
            FindStudioRoot(),
            "bin",
            FindTestConfiguration(),
            "net10.0");

        AssertRuntimeFileExists(outputDirectory, "editor_native.dll");
        AssertRuntimeFileExists(outputDirectory, "slang.dll");
    }

    private static void AssertRuntimeFileExists(string outputDirectory, string fileName)
    {
        var path = Path.Combine(outputDirectory, fileName);
        var file = new FileInfo(path);

        Assert.True(file.Exists, $"Expected native runtime file at {path}.");
        Assert.True(file.Length > 0, $"Expected native runtime file at {path} to be non-empty.");
    }

    private static string FindTestConfiguration()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (directory.Name is "Debug" or "Release")
            {
                return directory.Name;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException(
            $"Could not infer test configuration from {AppContext.BaseDirectory}.");
    }

    private static string FindStudioRoot()
    {
        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
