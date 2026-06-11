#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace asharia {

    struct RenderGraphImageHandle {
        std::uint32_t index{};

        [[nodiscard]] friend bool operator==(RenderGraphImageHandle,
                                             RenderGraphImageHandle) = default;
    };

    struct RenderGraphBufferHandle {
        std::uint32_t index{};

        [[nodiscard]] friend bool operator==(RenderGraphBufferHandle,
                                             RenderGraphBufferHandle) = default;
    };

    struct RenderGraphExtent2D {
        std::uint32_t width{};
        std::uint32_t height{};
    };

    enum class RenderGraphImageFormat {
        Undefined,
        B8G8R8A8Srgb,
        D32Sfloat,
    };

    enum class RenderGraphImageState {
        Undefined,
        ColorAttachment,
        ShaderRead,
        DepthAttachmentRead,
        DepthAttachmentWrite,
        DepthSampledRead,
        TransferSrc,
        TransferDst,
        Present,
    };

    enum class RenderGraphBufferState {
        Undefined,
        TransferRead,
        TransferWrite,
        HostRead,
        ShaderRead,
        StorageReadWrite,
    };

    enum class RenderGraphShaderStage {
        None,
        Fragment,
        Compute,
    };

    enum class RenderGraphImageLifetime {
        Imported,
        Transient,
    };

    enum class RenderGraphBufferLifetime {
        Imported,
        Transient,
    };

    struct RenderGraphImageAccess {
        RenderGraphImageState state{RenderGraphImageState::Undefined};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};

        [[nodiscard]] friend bool operator==(RenderGraphImageAccess,
                                             RenderGraphImageAccess) = default;
    };

    struct RenderGraphBufferAccess {
        RenderGraphBufferState state{RenderGraphBufferState::Undefined};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};

        [[nodiscard]] friend bool operator==(RenderGraphBufferAccess,
                                             RenderGraphBufferAccess) = default;
    };

    struct RenderGraphImageDesc {
        std::string name;
        RenderGraphImageFormat format{RenderGraphImageFormat::Undefined};
        RenderGraphExtent2D extent{};
        RenderGraphImageState initialState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage initialShaderStage{RenderGraphShaderStage::None};
        RenderGraphImageState finalState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage finalShaderStage{RenderGraphShaderStage::None};
        RenderGraphImageLifetime lifetime{RenderGraphImageLifetime::Imported};
    };

    struct RenderGraphBufferDesc {
        std::string name;
        std::uint64_t byteSize{};
        RenderGraphBufferState initialState{RenderGraphBufferState::Undefined};
        RenderGraphShaderStage initialShaderStage{RenderGraphShaderStage::None};
        RenderGraphBufferState finalState{RenderGraphBufferState::Undefined};
        RenderGraphShaderStage finalShaderStage{RenderGraphShaderStage::None};
        RenderGraphBufferLifetime lifetime{RenderGraphBufferLifetime::Imported};
    };

    struct RenderGraphImageTransition {
        RenderGraphImageHandle image{};
        std::string imageName;
        RenderGraphImageState oldState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage oldShaderStage{RenderGraphShaderStage::None};
        RenderGraphImageState newState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage newShaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphBufferTransition {
        RenderGraphBufferHandle buffer{};
        std::string bufferName;
        RenderGraphBufferState oldState{RenderGraphBufferState::Undefined};
        RenderGraphShaderStage oldShaderStage{RenderGraphShaderStage::None};
        RenderGraphBufferState newState{RenderGraphBufferState::Undefined};
        RenderGraphShaderStage newShaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphImageSlot {
        std::string name;
        RenderGraphImageHandle image{};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphBufferSlot {
        std::string name;
        RenderGraphBufferHandle buffer{};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
    };

    enum class RenderGraphSlotAccess {
        ColorWrite,
        ShaderRead,
        DepthAttachmentRead,
        DepthAttachmentWrite,
        DepthSampledRead,
        TransferRead,
        TransferWrite,
        BufferShaderRead,
        BufferTransferRead,
        BufferTransferWrite,
        BufferStorageReadWrite,
    };

    struct RenderGraphResourceSlotSchema {
        std::string name;
        RenderGraphSlotAccess access{RenderGraphSlotAccess::ColorWrite};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
        bool optional{};
    };

    enum class RenderGraphCommandKind {
        SetShader,
        SetTexture,
        SetFloat,
        SetInt,
        SetVec4,
        DrawFullscreenTriangle,
        ClearColor,
        FillBuffer,
        CopyImage,
        CopyBuffer,
        CopyBufferToImage,
        CopyImageToBuffer,
        Dispatch,
    };

    struct RenderGraphPassSchema {
        std::string type;
        std::string paramsType;
        std::vector<RenderGraphResourceSlotSchema> resourceSlots;
        std::vector<RenderGraphCommandKind> allowedCommands;
        bool allowCulling{};
        bool hasSideEffects{};
    };

    struct RenderGraphCommand {
        RenderGraphCommandKind kind{RenderGraphCommandKind::SetShader};
        std::string name;
        std::string secondaryName;
        std::array<float, 4> floatValues{};
        int intValue{};
        std::array<std::uint32_t, 3> uintValues{};
    };

} // namespace asharia
