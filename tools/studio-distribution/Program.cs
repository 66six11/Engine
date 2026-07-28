using System.Text.Json;

namespace Asharia.Studio.Distribution;

internal static class Program
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    public static int Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "stage-editor-host-profile")
        {
            return RunEditorHostProfile(args);
        }

        if (!TryParse(args, out var request, out var parseError))
        {
            Console.Error.WriteLine(parseError);
            Console.Error.WriteLine(Usage);
            return 2;
        }

        var result = StudioEditorImageProducer.Produce(request!);
        if (!result.Succeeded)
        {
            Console.Error.WriteLine(JsonSerializer.Serialize(
                new { diagnostics = result.Diagnostics },
                JsonOptions));
            return 1;
        }

        Console.Out.WriteLine(JsonSerializer.Serialize(result.Receipt, JsonOptions));
        return 0;
    }

    private static int RunEditorHostProfile(IReadOnlyList<string> args)
    {
        if (!TryParseEditorHostProfile(args, out var request, out var parseError))
        {
            Console.Error.WriteLine(parseError);
            Console.Error.WriteLine(Usage);
            return 2;
        }

        var result = StudioEditorHostProfileProducer.Produce(request!);
        if (!result.Succeeded)
        {
            Console.Error.WriteLine(JsonSerializer.Serialize(
                new { diagnostics = result.Diagnostics },
                JsonOptions));
            return 1;
        }

        Console.Out.WriteLine(JsonSerializer.Serialize(result.Receipt, JsonOptions));
        return 0;
    }

    private static bool TryParseEditorHostProfile(
        IReadOnlyList<string> args,
        out StudioEditorHostProfileProductionRequest? request,
        out string error)
    {
        request = null;
        error = string.Empty;
        if (args.Count != 3 || args[1] != "--output-root")
        {
            error = "The stage-editor-host-profile command requires exactly --output-root <path>.";
            return false;
        }

        try
        {
            request = new StudioEditorHostProfileProductionRequest(
                new DirectoryInfo(args[2]));
            return true;
        }
        catch (Exception pathError) when (
            pathError is ArgumentException or NotSupportedException)
        {
            error = $"Invalid path option: {pathError.Message}";
            return false;
        }
    }

    private static bool TryParse(
        IReadOnlyList<string> args,
        out StudioEditorImageProductionRequest? request,
        out string error)
    {
        request = null;
        error = string.Empty;
        if (args.Count == 0 || args[0] != "stage-editor-image")
        {
            error = "Expected the stage-editor-image command.";
            return false;
        }

        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 1; index < args.Count; index += 2)
        {
            if (index + 1 >= args.Count || !args[index].StartsWith("--", StringComparison.Ordinal))
            {
                error = "Every option must use --name value form.";
                return false;
            }

            if (!values.TryAdd(args[index], args[index + 1]))
            {
                error = $"Duplicate option '{args[index]}'.";
                return false;
            }
        }

        var known = new HashSet<string>(RequiredOptions, StringComparer.Ordinal)
        {
            "--environment-id",
            "--target-framework",
        };
        var unknown = values.Keys.FirstOrDefault(key => !known.Contains(key));
        if (unknown is not null)
        {
            error = $"Unknown option '{unknown}'.";
            return false;
        }

        var missing = RequiredOptions.FirstOrDefault(option => !values.ContainsKey(option));
        if (missing is not null)
        {
            error = $"Missing required option '{missing}'.";
            return false;
        }

        try
        {
            request = new StudioEditorImageProductionRequest(
                new DirectoryInfo(values["--publish-root"]),
                values["--entry-point"],
                new DirectoryInfo(values["--dotnet-root"]),
                values["--sdk-version"],
                values["--hostfxr-version"],
                values["--host-runtime-version"],
                values["--reference-pack-version"],
                new FileInfo(values["--runtime-contract"]),
                new FileInfo(values["--editor-contract"]),
                new DirectoryInfo(values["--output-root"]),
                values.GetValueOrDefault("--environment-id", "project-code-net10"),
                values.GetValueOrDefault("--target-framework", "net10.0"));
            return true;
        }
        catch (Exception pathError) when (
            pathError is ArgumentException or NotSupportedException)
        {
            error = $"Invalid path option: {pathError.Message}";
            return false;
        }
    }

    private static readonly string[] RequiredOptions =
    [
        "--publish-root",
        "--entry-point",
        "--dotnet-root",
        "--sdk-version",
        "--hostfxr-version",
        "--host-runtime-version",
        "--reference-pack-version",
        "--runtime-contract",
        "--editor-contract",
        "--output-root",
    ];

    private const string Usage = """
        Usage:
          Asharia.Studio.Distribution stage-editor-host-profile
            --output-root <fresh absolute path>

          Asharia.Studio.Distribution stage-editor-image
            --publish-root <absolute path>
            --entry-point <publish-relative path>
            --dotnet-root <absolute path>
            --sdk-version <x.y.z>
            --hostfxr-version <x.y.z>
            --host-runtime-version <x.y.z>
            --reference-pack-version <x.y.z>
            --runtime-contract <absolute path inside publish root>
            --editor-contract <absolute path inside publish root>
            --output-root <fresh absolute path>
            [--environment-id project-code-net10]
            [--target-framework net10.0]
        """;
}
