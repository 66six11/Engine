using System;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.EngineBridge.Project;
using Asharia.Studio.EngineBridge.Project.Abi;
using Xunit;

namespace Asharia.Studio.EngineBridge.Tests.Project;

public sealed class ProjectDescriptorBridgeTests
{
    [Fact]
    public async Task Open_forwards_current_abi_and_decodes_caller_owned_utf8()
    {
        var projectId = Guid.NewGuid();
        var api = new StubProjectNativeApi
        {
            ProjectRoot = "C:\\Projects\\Sample",
            ProjectName = "Sample",
            ProjectId = projectId,
        };
        var bridge = new ProjectDescriptorBridge(api);

        var result = await bridge.OpenProjectAsync("C:\\Projects\\Sample");

        Assert.True(result.Succeeded);
        Assert.Equal("C:\\Projects\\Sample", api.LastOpenPath);
        Assert.Equal(ProjectNativeAbi.Version, api.LastOpenRequest.Header.AbiVersion);
        Assert.Equal(ProjectNativeOpenRequest.StructSize, api.LastOpenRequest.Header.StructSize);
        Assert.Equal(api.ProjectRoot, result.Project!.RootPath);
        Assert.Equal(api.ProjectName, result.Project.ProjectName);
        Assert.Equal(projectId, result.Project.ProjectId);
    }

    [Fact]
    public async Task Create_forwards_name_and_stable_project_id()
    {
        var projectId = Guid.NewGuid();
        var api = new StubProjectNativeApi
        {
            ProjectRoot = "C:\\Projects\\Sample",
            ProjectName = "Sample",
            ProjectId = projectId,
        };
        var bridge = new ProjectDescriptorBridge(api);

        var result = await bridge.CreateMinimalProjectAsync(
            "C:\\Projects",
            "Sample",
            projectId);

        Assert.True(result.Succeeded);
        Assert.Equal("C:\\Projects", api.LastCreateParent);
        Assert.Equal("Sample", api.LastCreateName);
        Assert.Equal(projectId.ToString("D"), api.LastCreateId);
        Assert.Equal(
            ProjectNativeCreateRequest.StructSize,
            api.LastCreateRequest.Header.StructSize);
    }

    [Fact]
    public async Task Native_failure_maps_to_typed_failure_without_project_truth()
    {
        var api = new StubProjectNativeApi
        {
            Status = ProjectNativeStatus.InvalidProject,
            Message = "The descriptor is invalid.",
        };
        var bridge = new ProjectDescriptorBridge(api);

        var result = await bridge.OpenProjectAsync("C:\\Broken");

        Assert.False(result.Succeeded);
        Assert.Null(result.Project);
        Assert.Equal(ProjectDescriptorFailureKind.InvalidProject, result.Failure!.Kind);
        Assert.Equal("The descriptor is invalid.", result.Failure.Message);
    }

    [Fact]
    public async Task Native_binding_failure_is_reported_as_unavailable()
    {
        var api = new StubProjectNativeApi
        {
            Exception = new DllNotFoundException("missing adapter"),
        };
        var bridge = new ProjectDescriptorBridge(api);

        var result = await bridge.OpenProjectAsync("C:\\Projects\\Sample");

        Assert.False(result.Succeeded);
        Assert.Equal(ProjectDescriptorFailureKind.NativeUnavailable, result.Failure!.Kind);
        Assert.Contains("missing adapter", result.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Managed_layout_matches_the_native_v1_contract()
    {
        Assert.Equal(8, Marshal.SizeOf<ProjectNativeAbiHeader>());
        Assert.Equal(16, Marshal.SizeOf<ProjectNativeStringView>());
        Assert.Equal(24, Marshal.SizeOf<ProjectNativeOpenRequest>());
        Assert.Equal(56, Marshal.SizeOf<ProjectNativeCreateRequest>());
        Assert.Equal(16, Marshal.SizeOf<ProjectNativeTextSpan>());
        Assert.Equal(88, Marshal.SizeOf<ProjectNativeResult>());
        Assert.Equal(16, OffsetOf<ProjectNativeResult>(
            nameof(ProjectNativeResult.RequiredByteLength)));
        Assert.Equal(24, OffsetOf<ProjectNativeResult>(
            nameof(ProjectNativeResult.ProjectRootUtf8)));
        Assert.Equal(72, OffsetOf<ProjectNativeResult>(
            nameof(ProjectNativeResult.MessageUtf8)));
    }

    private static int OffsetOf<T>(string propertyName)
    {
        var field = typeof(T).GetField(
            $"<{propertyName}>k__BackingField",
            BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException(
                $"ABI backing field for '{propertyName}' is missing.");
        return Marshal.OffsetOf<T>(field.Name).ToInt32();
    }

    private sealed class StubProjectNativeApi : IProjectNativeApi
    {
        public ProjectNativeStatus Status { get; set; } = ProjectNativeStatus.Success;

        public string ProjectRoot { get; set; } = string.Empty;

        public string ProjectName { get; set; } = string.Empty;

        public Guid ProjectId { get; set; }

        public string Message { get; set; } = string.Empty;

        public Exception? Exception { get; set; }

        public ProjectNativeOpenRequest LastOpenRequest { get; private set; }

        public ProjectNativeCreateRequest LastCreateRequest { get; private set; }

        public string LastOpenPath { get; private set; } = string.Empty;

        public string LastCreateParent { get; private set; } = string.Empty;

        public string LastCreateName { get; private set; } = string.Empty;

        public string LastCreateId { get; private set; } = string.Empty;

        public ProjectNativeStatus Open(
            in ProjectNativeOpenRequest request,
            nint responseUtf8,
            ulong responseCapacity,
            out ProjectNativeResult result,
            ulong resultCapacity)
        {
            ThrowIfConfigured();
            LastOpenRequest = request;
            LastOpenPath = Read(request.ProjectPathUtf8);
            return Write(responseUtf8, responseCapacity, out result);
        }

        public ProjectNativeStatus CreateMinimal(
            in ProjectNativeCreateRequest request,
            nint responseUtf8,
            ulong responseCapacity,
            out ProjectNativeResult result,
            ulong resultCapacity)
        {
            ThrowIfConfigured();
            LastCreateRequest = request;
            LastCreateParent = Read(request.ParentDirectoryUtf8);
            LastCreateName = Read(request.ProjectNameUtf8);
            LastCreateId = Read(request.ProjectIdUtf8);
            return Write(responseUtf8, responseCapacity, out result);
        }

        private ProjectNativeStatus Write(
            nint responseUtf8,
            ulong responseCapacity,
            out ProjectNativeResult result)
        {
            var root = Status == ProjectNativeStatus.Success ? ProjectRoot : string.Empty;
            var name = Status == ProjectNativeStatus.Success ? ProjectName : string.Empty;
            var id = Status == ProjectNativeStatus.Success ? ProjectId.ToString("D") : string.Empty;
            var rootBytes = Encoding.UTF8.GetBytes(root);
            var nameBytes = Encoding.UTF8.GetBytes(name);
            var idBytes = Encoding.UTF8.GetBytes(id);
            var messageBytes = Encoding.UTF8.GetBytes(Message);
            var all = new byte[
                rootBytes.Length + nameBytes.Length + idBytes.Length + messageBytes.Length];
            var cursor = 0;
            var rootSpan = Append(all, ref cursor, rootBytes);
            var nameSpan = Append(all, ref cursor, nameBytes);
            var idSpan = Append(all, ref cursor, idBytes);
            var messageSpan = Append(all, ref cursor, messageBytes);
            if ((ulong)all.Length > responseCapacity)
            {
                result = Result(ProjectNativeStatus.BufferTooSmall, (ulong)all.Length);
                return ProjectNativeStatus.BufferTooSmall;
            }

            if (all.Length != 0)
            {
                Marshal.Copy(all, 0, responseUtf8, all.Length);
            }
            result = new ProjectNativeResult(
                new ProjectNativeAbiHeader(ProjectNativeAbi.Version, ProjectNativeResult.StructSize),
                Status,
                Reserved: 0,
                (ulong)all.Length,
                rootSpan,
                nameSpan,
                idSpan,
                messageSpan);
            return Status;
        }

        private static ProjectNativeResult Result(ProjectNativeStatus status, ulong required) =>
            new(
                new ProjectNativeAbiHeader(ProjectNativeAbi.Version, ProjectNativeResult.StructSize),
                status,
                Reserved: 0,
                required,
                default,
                default,
                default,
                default);

        private static ProjectNativeTextSpan Append(
            byte[] target,
            ref int cursor,
            byte[] value)
        {
            var span = new ProjectNativeTextSpan((ulong)cursor, (ulong)value.Length);
            value.CopyTo(target, cursor);
            cursor += value.Length;
            return span;
        }

        private static string Read(ProjectNativeStringView value)
        {
            var bytes = new byte[checked((int)value.ByteLength)];
            if (bytes.Length != 0)
            {
                Marshal.Copy(value.Data, bytes, 0, bytes.Length);
            }
            return Encoding.UTF8.GetString(bytes);
        }

        private void ThrowIfConfigured()
        {
            if (Exception is not null)
            {
                throw Exception;
            }
        }
    }
}
