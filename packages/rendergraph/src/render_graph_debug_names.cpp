#include "render_graph_debug_names.hpp"

#include <string>
#include <string_view>

namespace asharia::rendergraph_internal {

    std::string_view imageFormatName(RenderGraphImageFormat format) {
        switch (format) {
        case RenderGraphImageFormat::B8G8R8A8Srgb:
            return "B8G8R8A8Srgb";
        case RenderGraphImageFormat::D32Sfloat:
            return "D32Sfloat";
        case RenderGraphImageFormat::Undefined:
        default:
            return "Undefined";
        }
    }

    std::string_view imageStateName(RenderGraphImageState state) {
        switch (state) {
        case RenderGraphImageState::ColorAttachment:
            return "ColorAttachment";
        case RenderGraphImageState::ShaderRead:
            return "ShaderRead";
        case RenderGraphImageState::DepthAttachmentRead:
            return "DepthAttachmentRead";
        case RenderGraphImageState::DepthAttachmentWrite:
            return "DepthAttachmentWrite";
        case RenderGraphImageState::DepthSampledRead:
            return "DepthSampledRead";
        case RenderGraphImageState::TransferSrc:
            return "TransferSrc";
        case RenderGraphImageState::TransferDst:
            return "TransferDst";
        case RenderGraphImageState::Present:
            return "Present";
        case RenderGraphImageState::Undefined:
        default:
            return "Undefined";
        }
    }

    std::string_view bufferStateName(RenderGraphBufferState state) {
        switch (state) {
        case RenderGraphBufferState::TransferRead:
            return "TransferRead";
        case RenderGraphBufferState::TransferWrite:
            return "TransferWrite";
        case RenderGraphBufferState::HostRead:
            return "HostRead";
        case RenderGraphBufferState::ShaderRead:
            return "ShaderRead";
        case RenderGraphBufferState::StorageReadWrite:
            return "StorageReadWrite";
        case RenderGraphBufferState::Undefined:
        default:
            return "Undefined";
        }
    }

    std::string_view imageLifetimeName(RenderGraphImageLifetime lifetime) {
        switch (lifetime) {
        case RenderGraphImageLifetime::Transient:
            return "Transient";
        case RenderGraphImageLifetime::Imported:
        default:
            return "Imported";
        }
    }

    std::string_view bufferLifetimeName(RenderGraphBufferLifetime lifetime) {
        switch (lifetime) {
        case RenderGraphBufferLifetime::Transient:
            return "Transient";
        case RenderGraphBufferLifetime::Imported:
        default:
            return "Imported";
        }
    }

    std::string_view shaderStageName(RenderGraphShaderStage stage) {
        switch (stage) {
        case RenderGraphShaderStage::Fragment:
            return "fragment";
        case RenderGraphShaderStage::Compute:
            return "compute";
        case RenderGraphShaderStage::None:
        default:
            return "";
        }
    }

    std::string_view commandKindName(RenderGraphCommandKind kind) {
        switch (kind) {
        case RenderGraphCommandKind::SetShader:
            return "SetShader";
        case RenderGraphCommandKind::SetTexture:
            return "SetTexture";
        case RenderGraphCommandKind::SetFloat:
            return "SetFloat";
        case RenderGraphCommandKind::SetInt:
            return "SetInt";
        case RenderGraphCommandKind::SetVec4:
            return "SetVec4";
        case RenderGraphCommandKind::DrawFullscreenTriangle:
            return "DrawFullscreenTriangle";
        case RenderGraphCommandKind::ClearColor:
            return "ClearColor";
        case RenderGraphCommandKind::FillBuffer:
            return "FillBuffer";
        case RenderGraphCommandKind::CopyImage:
            return "CopyImage";
        case RenderGraphCommandKind::CopyBuffer:
            return "CopyBuffer";
        case RenderGraphCommandKind::CopyBufferToImage:
            return "CopyBufferToImage";
        case RenderGraphCommandKind::CopyImageToBuffer:
            return "CopyImageToBuffer";
        case RenderGraphCommandKind::Dispatch:
            return "Dispatch";
        }
        return "";
    }

    std::string commandDetail(const RenderGraphCommand& command) {
        switch (command.kind) {
        case RenderGraphCommandKind::SetShader:
        case RenderGraphCommandKind::SetTexture:
            return command.name + " -> " + command.secondaryName;
        case RenderGraphCommandKind::SetFloat:
            return command.name + " = " + std::to_string(command.floatValues[0]);
        case RenderGraphCommandKind::SetInt:
            return command.name + " = " + std::to_string(command.intValue);
        case RenderGraphCommandKind::SetVec4:
        case RenderGraphCommandKind::ClearColor:
            return command.name + " = (" + std::to_string(command.floatValues[0]) + ", " +
                   std::to_string(command.floatValues[1]) + ", " +
                   std::to_string(command.floatValues[2]) + ", " +
                   std::to_string(command.floatValues[3]) + ")";
        case RenderGraphCommandKind::FillBuffer:
            return command.name + " = " + std::to_string(command.uintValues[0]);
        case RenderGraphCommandKind::CopyImage:
        case RenderGraphCommandKind::CopyBuffer:
        case RenderGraphCommandKind::CopyBufferToImage:
        case RenderGraphCommandKind::CopyImageToBuffer:
            return command.name + " -> " + command.secondaryName;
        case RenderGraphCommandKind::DrawFullscreenTriangle:
            return "-";
        case RenderGraphCommandKind::Dispatch:
            return std::to_string(command.uintValues[0]) + " x " +
                   std::to_string(command.uintValues[1]) + " x " +
                   std::to_string(command.uintValues[2]);
        }
        return "-";
    }

    std::string imageAccessName(RenderGraphImageState state, RenderGraphShaderStage shaderStage) {
        std::string name{imageStateName(state)};
        if ((state == RenderGraphImageState::ShaderRead ||
             state == RenderGraphImageState::DepthSampledRead) &&
            shaderStage != RenderGraphShaderStage::None) {
            name += "(";
            name += shaderStageName(shaderStage);
            name += ")";
        }
        return name;
    }

    std::string bufferAccessName(RenderGraphBufferState state, RenderGraphShaderStage shaderStage) {
        std::string name{bufferStateName(state)};
        if (state == RenderGraphBufferState::ShaderRead &&
            shaderStage != RenderGraphShaderStage::None) {
            name += "(";
            name += shaderStageName(shaderStage);
            name += ")";
        }
        if (state == RenderGraphBufferState::StorageReadWrite &&
            shaderStage != RenderGraphShaderStage::None) {
            name += "(";
            name += shaderStageName(shaderStage);
            name += ")";
        }
        return name;
    }

} // namespace asharia::rendergraph_internal
