using System.Runtime.InteropServices;

namespace Editor.Core.Interop.Viewports.Api;

[StructLayout(LayoutKind.Sequential)]
internal readonly struct ViewportNativeAbiHeader
{
    private readonly uint abiVersion_;
    private readonly uint structSize_;

    public const uint ExpectedAbiVersion = 1;

    public ViewportNativeAbiHeader(uint structSize)
    {
        abiVersion_ = ExpectedAbiVersion;
        structSize_ = structSize;
    }

    public uint AbiVersion => abiVersion_;

    public uint StructSize => structSize_;
}
