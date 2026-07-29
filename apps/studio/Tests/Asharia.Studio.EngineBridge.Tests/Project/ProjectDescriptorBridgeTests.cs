using System;
using System.Runtime.InteropServices;
using System.Text;
using Asharia.Studio.EngineBridge.Project;
using Asharia.Studio.EngineBridge.Project.Abi;
using Xunit;

namespace Asharia.Studio.EngineBridge.Tests.Project;

public sealed class ProjectDescriptorBridgeTests
{
    private static readonly Guid ProjectId =
        Guid.Parse("51e86383-8a06-4c41-9267-ab10b0b67eb9");

    [Fact]
    public void Abi_types_match_native_project_contract()
    {
        Assert.Equal(8, Marshal.SizeOf<ProjectNativeAbiHeader>());
        Assert.Equal(16, Marshal.SizeOf<ProjectNativeStringView>());
        Assert.Equal(24, Marshal.SizeOf<ProjectNativeOpenRequest>());
        Assert.Equal(56, Marshal.SizeOf<ProjectNativeCreateRequest>());
        Assert.Equal(80, Marshal.SizeOf<ProjectNativeResult>());
        Assert.Equal(
            8,
            Marshal.OffsetOf<ProjectNativeOpenRequest>(
                "<ProjectRootUtf8>k__BackingField").ToInt32());
        Assert.Equal(
            24,
            Marshal.OffsetOf<ProjectNativeCreateRequest>(
                "<ProjectNameUtf8>k__BackingField").ToInt32());
        Assert.Equal(
            40,
            Marshal.OffsetOf<ProjectNativeCreateRequest>(
                "<ProjectIdUtf8>k__BackingField").ToInt32());
        Assert.Equal(
            16,
            Marshal.OffsetOf<ProjectNativeResult>(
                "<ProjectRootUtf8>k__BackingField").ToInt32());
        Assert.Equal(
            64,
            Marshal.OffsetOf<ProjectNativeResult>(
                "<MessageUtf8>k__BackingField").ToInt32());
        Assert.Equal(typeof(uint), Enum.GetUnderlyingType(typeof(ProjectNativeStatus)));
        Assert.Equal(0U, (uint)ProjectNativeStatus.Success);
        Assert.Equal(1U, (uint)ProjectNativeStatus.InvalidArgument);
        Assert.Equal(2U, (uint)ProjectNativeStatus.UnsupportedAbi);
        Assert.Equal(3U, (uint)ProjectNativeStatus.InvalidUtf8);
        Assert.Equal(4U, (uint)ProjectNativeStatus.AlreadyExists);
        Assert.Equal(5U, (uint)ProjectNativeStatus.Busy);
        Assert.Equal(6U, (uint)ProjectNativeStatus.InvalidProject);
        Assert.Equal(7U, (uint)ProjectNativeStatus.IoFailure);
        Assert.Equal(8U, (uint)ProjectNativeStatus.InternalError);
    }

    [Fact]
    public void Open_copies_native_result_and_releases_it()
    {
        using var api = StubProjectNativeApi.Success(
            @"D:\Projects\Example",
            "Example",
            ProjectId);
        var bridge = new ProjectDescriptorBridge(api);

        var result = bridge.OpenProject(@"D:\Projects\Example");

        Assert.Equal(@"D:\Projects\Example", api.LastOpenRoot);
        Assert.Equal(ProjectNativeAbi.Version, api.LastOpenRequest.Header.AbiVersion);
        Assert.Equal(ProjectNativeOpenRequest.StructSize, api.LastOpenRequest.Header.StructSize);
        Assert.Equal(@"D:\Projects\Example", result.RootPath);
        Assert.Equal("Example", result.ProjectName);
        Assert.Equal(ProjectId, result.ProjectId);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Create_forwards_exact_utf8_and_canonical_project_id()
    {
        using var api = StubProjectNativeApi.Success(
            @"D:\项目\示例",
            "示例 项目",
            ProjectId);
        var bridge = new ProjectDescriptorBridge(api);

        var result = bridge.CreateMinimalProject(
            @"D:\项目\示例",
            "示例 项目",
            ProjectId);

        Assert.Equal(@"D:\项目\示例", api.LastCreateRoot);
        Assert.Equal("示例 项目", api.LastCreateName);
        Assert.Equal(ProjectId.ToString("D"), api.LastCreateId);
        Assert.Equal(
            ProjectNativeCreateRequest.StructSize,
            api.LastCreateRequest.Header.StructSize);
        Assert.Equal(ProjectId, result.ProjectId);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Native_rejection_preserves_status_message_and_releases_result()
    {
        using var api = StubProjectNativeApi.Failure(
            ProjectNativeStatus.AlreadyExists,
            "A project descriptor already exists.");
        var bridge = new ProjectDescriptorBridge(api);

        var exception = Assert.Throws<ProjectNativeCallException>(() =>
            bridge.CreateMinimalProject(
                @"D:\Projects\Example",
                "Example",
                ProjectId));

        Assert.Equal("project.create-minimal", exception.Operation);
        Assert.Equal(ProjectNativeStatus.AlreadyExists, exception.Status);
        Assert.Equal(
            "A project descriptor already exists.",
            exception.Message);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Binding_failure_does_not_release_an_unowned_result()
    {
        using var api = StubProjectNativeApi.Success(
            @"D:\Projects\Example",
            "Example",
            ProjectId);
        api.OpenException = new DllNotFoundException("missing editor native");
        var bridge = new ProjectDescriptorBridge(api);

        var exception = Assert.Throws<ProjectNativeCallException>(() =>
            bridge.OpenProject(@"D:\Projects\Example"));

        Assert.Equal("project.open", exception.Operation);
        Assert.Null(exception.Status);
        Assert.IsType<DllNotFoundException>(exception.InnerException);
        Assert.Equal(0, api.ReleaseCalls);
    }

    [Fact]
    public void Invalid_success_result_is_rejected_after_release()
    {
        using var api = StubProjectNativeApi.Success(
            @"D:\Projects\Example",
            "Example",
            ProjectId);
        api.ResultHeader = new ProjectNativeAbiHeader(
            ProjectNativeAbi.Version,
            ProjectNativeResult.StructSize - 1);
        var bridge = new ProjectDescriptorBridge(api);

        var exception = Assert.Throws<ProjectNativeCallException>(() =>
            bridge.OpenProject(@"D:\Projects\Example"));

        Assert.Equal(ProjectNativeStatus.Success, exception.Status);
        Assert.Contains(
            "unsupported result ABI",
            exception.Message,
            StringComparison.Ordinal);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Empty_project_id_is_rejected_before_native_call()
    {
        using var api = StubProjectNativeApi.Success(
            @"D:\Projects\Example",
            "Example",
            ProjectId);
        var bridge = new ProjectDescriptorBridge(api);

        _ = Assert.Throws<ArgumentException>(() =>
            bridge.CreateMinimalProject(
                @"D:\Projects\Example",
                "Example",
                Guid.Empty));

        Assert.Equal(0, api.CreateCalls);
        Assert.Equal(0, api.ReleaseCalls);
    }

    private sealed class StubProjectNativeApi : IProjectNativeApi, IDisposable
    {
        private readonly string root_;
        private readonly string name_;
        private readonly Guid projectId_;
        private readonly string message_;
        private bool isDisposed_;

        private StubProjectNativeApi(
            ProjectNativeStatus status,
            string root,
            string name,
            Guid projectId,
            string message)
        {
            Status = status;
            root_ = root;
            name_ = name;
            projectId_ = projectId;
            message_ = message;
        }

        public static StubProjectNativeApi Success(
            string root,
            string name,
            Guid projectId)
        {
            return new StubProjectNativeApi(
                ProjectNativeStatus.Success,
                root,
                name,
                projectId,
                string.Empty);
        }

        public static StubProjectNativeApi Failure(
            ProjectNativeStatus status,
            string message)
        {
            return new StubProjectNativeApi(
                status,
                string.Empty,
                string.Empty,
                Guid.Empty,
                message);
        }

        public int OpenCalls { get; private set; }

        public int CreateCalls { get; private set; }

        public int ReleaseCalls { get; private set; }

        public ProjectNativeStatus Status { get; }

        public Exception? OpenException { get; set; }

        public ProjectNativeAbiHeader ResultHeader { get; set; } =
            new(ProjectNativeAbi.Version, ProjectNativeResult.StructSize);

        public string LastOpenRoot { get; private set; } = string.Empty;

        public string LastCreateRoot { get; private set; } = string.Empty;

        public string LastCreateName { get; private set; } = string.Empty;

        public string LastCreateId { get; private set; } = string.Empty;

        public ProjectNativeOpenRequest LastOpenRequest { get; private set; }

        public ProjectNativeCreateRequest LastCreateRequest { get; private set; }

        public ProjectNativeStatus Open(
            in ProjectNativeOpenRequest request,
            out ProjectNativeResult result)
        {
            OpenCalls++;
            LastOpenRequest = request;
            result = default;
            if (OpenException is not null)
            {
                throw OpenException;
            }

            LastOpenRoot = Decode(request.ProjectRootUtf8);
            result = CreateResult();
            return Status;
        }

        public ProjectNativeStatus CreateMinimal(
            in ProjectNativeCreateRequest request,
            out ProjectNativeResult result)
        {
            CreateCalls++;
            LastCreateRequest = request;
            LastCreateRoot = Decode(request.ProjectRootUtf8);
            LastCreateName = Decode(request.ProjectNameUtf8);
            LastCreateId = Decode(request.ProjectIdUtf8);
            result = CreateResult();
            return Status;
        }

        public void Release(ProjectNativeResult result)
        {
            ReleaseCalls++;
            Free(result.ProjectRootUtf8);
            Free(result.ProjectNameUtf8);
            Free(result.ProjectIdUtf8);
            Free(result.MessageUtf8);
        }

        public void Dispose()
        {
            isDisposed_ = true;
        }

        private ProjectNativeResult CreateResult()
        {
            ObjectDisposedException.ThrowIf(isDisposed_, this);
            var root = Allocate(root_);
            var name = Allocate(name_);
            var projectId = Allocate(
                projectId_ == Guid.Empty
                    ? string.Empty
                    : projectId_.ToString("D"));
            var message = Allocate(message_);
            return new ProjectNativeResult(
                ResultHeader,
                Status,
                root,
                ByteLength(root_),
                name,
                ByteLength(name_),
                projectId,
                ByteLength(
                    projectId_ == Guid.Empty
                        ? string.Empty
                        : projectId_.ToString("D")),
                message,
                ByteLength(message_));
        }

        private static nint Allocate(string value)
        {
            return string.IsNullOrEmpty(value)
                ? 0
                : Marshal.StringToCoTaskMemUTF8(value);
        }

        private static void Free(nint value)
        {
            if (value != 0)
            {
                Marshal.FreeCoTaskMem(value);
            }
        }

        private static ulong ByteLength(string value)
        {
            return (ulong)Encoding.UTF8.GetByteCount(value);
        }

        private static string Decode(ProjectNativeStringView value)
        {
            if (value.Data == 0 || value.ByteLength == 0)
            {
                return string.Empty;
            }
            var bytes = new byte[checked((int)value.ByteLength)];
            Marshal.Copy(value.Data, bytes, 0, bytes.Length);
            return Encoding.UTF8.GetString(bytes);
        }
    }
}
