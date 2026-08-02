using System;
using System.IO;
using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Observe.Discovery;
using Xunit;

namespace Asharia.Studio.Observe.Tests;

[SupportedOSPlatform("windows")]
public sealed class StudioSessionDiscoveryTests
{
    [Fact]
    public async Task Inherited_discovery_acl_fails_closed_before_reading_files()
    {
        var root = TestRoot();
        Directory.CreateDirectory(root);
        try
        {
            var discovery = new StudioSessionDiscovery(root);

            var result = await discovery.ListAsync(CancellationToken.None);

            var issue = Assert.Single(result.Issues);
            Assert.Empty(result.Sessions);
            Assert.Equal("observation.discovery.invalid-acl", issue.Failure.Code);
            Assert.Equal("security", issue.Failure.Category);
        }
        finally
        {
            SafeDeleteTestRoot(root);
        }
    }

    [Fact]
    public async Task Discovery_stops_enumerating_after_the_fixed_manifest_limit()
    {
        var root = TestRoot();
        CreateProtectedRoot(root);
        for (var index = 0; index <= StudioSessionDiscovery.MaxSessionManifests; index++)
        {
            await File.WriteAllTextAsync(
                Path.Combine(root, $"{index:D3}.json"),
                "{}");
        }

        try
        {
            var discovery = new StudioSessionDiscovery(root);

            var result = await discovery.ListAsync(CancellationToken.None);

            Assert.Empty(result.Sessions);
            Assert.Contains(
                result.Issues,
                issue => issue.Failure.Code == "observation.discovery.too-many");
        }
        finally
        {
            SafeDeleteTestRoot(root);
        }
    }

    [Fact]
    public async Task Missing_manifest_returns_a_typed_stale_resolution()
    {
        var root = TestRoot();
        CreateProtectedRoot(root);
        try
        {
            var discovery = new StudioSessionDiscovery(root);

            var result = await discovery.ResolveAsync(
                new Asharia.Studio.DevelopmentProtocol.StudioInstanceId(Guid.NewGuid()),
                CancellationToken.None);

            Assert.Null(result.Manifest);
            Assert.Equal("observation.discovery.not-found", result.Failure?.Code);
            Assert.Equal("stale", result.Failure?.Category);
        }
        finally
        {
            SafeDeleteTestRoot(root);
        }
    }

    private static string TestRoot() => Path.Combine(
        Path.GetTempPath(),
        "asharia-studio-observe-tests",
        Guid.NewGuid().ToString("N"));

    private static void CreateProtectedRoot(string root)
    {
        using var identity = WindowsIdentity.GetCurrent();
        var currentUser = identity.User!;
        var security = new DirectorySecurity();
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        security.AddAccessRule(new FileSystemAccessRule(
            currentUser,
            FileSystemRights.FullControl,
            InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
            PropagationFlags.None,
            AccessControlType.Allow));
        FileSystemAclExtensions.CreateDirectory(security, root);
    }

    private static void SafeDeleteTestRoot(string root)
    {
        if (!Directory.Exists(root))
        {
            return;
        }

        var fullRoot = Path.GetFullPath(root);
        var testParent = Path.GetFullPath(Path.Combine(
            Path.GetTempPath(),
            "asharia-studio-observe-tests")) + Path.DirectorySeparatorChar;
        if (!fullRoot.StartsWith(testParent, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                "Refusing to recursively delete a path outside the observe test root.");
        }

        Directory.Delete(fullRoot, recursive: true);
    }
}
