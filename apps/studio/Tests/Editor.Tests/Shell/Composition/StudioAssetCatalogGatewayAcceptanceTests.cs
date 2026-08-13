using System;
using System.IO;
using System.Threading.Tasks;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.EngineBridge.Assets;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioAssetCatalogGatewayAcceptanceTests
{
    [Fact]
    public async Task Real_native_catalog_queries_an_unloaded_project_without_runtime_resources()
    {
        var workspace = Path.Combine(
            Path.GetTempPath(),
            $"asharia-studio-catalog-{Guid.NewGuid():N}");
        Directory.CreateDirectory(Path.Combine(workspace, "Assets", "Models"));
        try
        {
            var projectFile = Path.Combine(workspace, "asharia.project.json");
            await File.WriteAllTextAsync(
                projectFile,
                $$"""
                {
                  "schema": "com.asharia.project",
                  "schemaVersion": 1,
                  "projectName": "CatalogAcceptance",
                  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
                  "assetSourceRoots": [
                    {
                      "rootName": "project-assets",
                      "directory": "Assets",
                      "sourcePathPrefix": "Assets"
                    }
                  ],
                  "assetCacheRoot": ".asharia/cache/assets",
                  "assetDiscovery": {
                    "ignoredDirectories": [".git", ".asharia"]
                  }
                }
                """);
            await File.WriteAllTextAsync(
                Path.Combine(workspace, "Assets", "Models", "untracked.glb"),
                "source bytes are catalog facts, not a runtime resource");
            var scope = new AssetCatalogQueryScope(
                ProjectSessionId.CreateNew(),
                Guid.Parse("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21"),
                workspace,
                projectFile,
                "editor-preview");

            var result = await new AssetCatalogGateway().QueryAsync(scope);

            Assert.True(result.Succeeded, result.Failure?.Message);
            var snapshot = result.Snapshot!;
            Assert.NotEqual(AssetCatalogSnapshotState.Failed, snapshot.State);
            var row = Assert.Single(snapshot.Entries);
            Assert.Null(row.AssetGuid);
            Assert.Equal("Assets/Models/untracked.glb", row.SourcePath);
            Assert.Equal(AssetCatalogProductState.NotTracked, row.ProductState);
            Assert.Contains(
                snapshot.Navigation,
                node => node.Kind == AssetCatalogNavigationKind.Folder
                    && node.ScopePath == "Assets/Models");
        }
        finally
        {
            if (Directory.Exists(workspace))
            {
                Directory.Delete(workspace, recursive: true);
            }
        }
    }
}
