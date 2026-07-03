using System;
using System.IO;
using System.Text.Json;

namespace Editor.Shell.Docking.Layout;

public static class EditorDockLayoutStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
    };

    public static string LayoutFilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "VkEngineStudio",
        "dock-layout.json");

    public static EditorDockLayoutSnapshot? TryLoad()
    {
        try
        {
            if (!File.Exists(LayoutFilePath))
            {
                return null;
            }

            var json = File.ReadAllText(LayoutFilePath);
            return JsonSerializer.Deserialize<EditorDockLayoutSnapshot>(json, JsonOptions);
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    public static bool TrySave(EditorDockLayoutSnapshot snapshot)
    {
        try
        {
            var directory = Path.GetDirectoryName(LayoutFilePath);
            if (!string.IsNullOrWhiteSpace(directory))
            {
                Directory.CreateDirectory(directory);
            }

            var json = JsonSerializer.Serialize(snapshot, JsonOptions);
            File.WriteAllText(LayoutFilePath, json);
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    public static bool TryDelete()
    {
        try
        {
            if (File.Exists(LayoutFilePath))
            {
                File.Delete(LayoutFilePath);
            }

            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }
}
