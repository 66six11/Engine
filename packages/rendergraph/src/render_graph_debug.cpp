#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "render_graph_internal.hpp"

namespace asharia {

    std::string_view RenderGraph::Impl::imageFormatName(RenderGraphImageFormat format) {
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

    std::string_view RenderGraph::Impl::imageStateName(RenderGraphImageState state) {
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

    std::string_view RenderGraph::Impl::bufferStateName(RenderGraphBufferState state) {
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

    std::string_view RenderGraph::Impl::imageLifetimeName(RenderGraphImageLifetime lifetime) {
        switch (lifetime) {
        case RenderGraphImageLifetime::Transient:
            return "Transient";
        case RenderGraphImageLifetime::Imported:
        default:
            return "Imported";
        }
    }

    std::string_view RenderGraph::Impl::bufferLifetimeName(RenderGraphBufferLifetime lifetime) {
        switch (lifetime) {
        case RenderGraphBufferLifetime::Transient:
            return "Transient";
        case RenderGraphBufferLifetime::Imported:
        default:
            return "Imported";
        }
    }

    std::string RenderGraph::Impl::missingCallbackMessage(const RenderGraphCompiledPass& pass) {
        std::string message = "Render graph pass '";
        message += pass.name;
        message += "'";
        if (!pass.type.empty()) {
            message += " of type '";
            message += pass.type;
            message += "'";
        }
        message += " is missing an execute callback.";
        return message;
    }

    std::string RenderGraph::Impl::duplicateSlotMessage(const Pass& pass,
                                                        std::string_view slotName) {
        std::string message = "Render graph pass '";
        message += pass.name;
        message += "' declares duplicate resource slot '";
        message += slotName;
        message += "'.";
        return message;
    }

    std::string RenderGraph::Impl::imageAccessConflictMessage(const Pass& pass,
                                                              const RenderGraphImageSlot& slot,
                                                              std::string_view access,
                                                              const RenderGraphImageSlot& otherSlot,
                                                              std::string_view otherAccess) const {
        std::string message = "Render graph pass '";
        message += pass.name;
        message += "' declares image '";
        message += imageHandleLabel(slot.image);
        message += "' more than once in slots '";
        message += slot.name;
        message += "' (";
        message += access;
        message += ") and '";
        message += otherSlot.name;
        message += "' (";
        message += otherAccess;
        message += "). Split the operation into separate passes or add an explicit combined "
                   "access state.";
        return message;
    }

    std::string RenderGraph::Impl::bufferAccessConflictMessage(
        const Pass& pass, const RenderGraphBufferSlot& slot, std::string_view access,
        const RenderGraphBufferSlot& otherSlot, std::string_view otherAccess) const {
        std::string message = "Render graph pass '";
        message += pass.name;
        message += "' declares buffer '";
        message += bufferHandleLabel(slot.buffer);
        message += "' more than once in slots '";
        message += slot.name;
        message += "' (";
        message += access;
        message += ") and '";
        message += otherSlot.name;
        message += "' (";
        message += otherAccess;
        message += "). Split the operation into separate passes or add an explicit combined "
                   "access state.";
        return message;
    }

    std::string RenderGraph::Impl::imageHandleLabel(RenderGraphImageHandle image) const {
        std::string label = "#";
        label += std::to_string(image.index);
        if (image.index < images_.size() && !images_[image.index].name.empty()) {
            label += " ";
            label += images_[image.index].name;
        }
        return label;
    }

    std::string RenderGraph::Impl::bufferHandleLabel(RenderGraphBufferHandle buffer) const {
        std::string label = "#";
        label += std::to_string(buffer.index);
        if (buffer.index < buffers_.size() && !buffers_[buffer.index].name.empty()) {
            label += " ";
            label += buffers_[buffer.index].name;
        }
        return label;
    }

    std::string
    RenderGraph::Impl::dependencyResourceLabel(const RenderGraphPassDependency& dependency) const {
        if (dependency.resourceKind == RenderGraphResourceKind::Buffer) {
            return bufferHandleLabel(dependency.buffer);
        }
        return imageHandleLabel(dependency.image);
    }

    std::string RenderGraph::Impl::passDeclarationLabel(std::size_t passIndex) const {
        std::string label = "#";
        label += std::to_string(passIndex);
        if (passIndex < passes_.size() && !passes_[passIndex].name.empty()) {
            label += " ";
            label += passes_[passIndex].name;
        }
        return label;
    }

    std::string
    RenderGraph::Impl::passDeclarationList(std::span<const std::size_t> passIndices) const {
        if (passIndices.empty()) {
            return "-";
        }

        std::string labels;
        for (std::size_t index = 0; index < passIndices.size(); ++index) {
            if (index > 0) {
                labels += ", ";
            }
            labels += passDeclarationLabel(passIndices[index]);
        }
        return labels;
    }

    std::string
    RenderGraph::Impl::imageHandleList(std::span<const RenderGraphImageHandle> images) const {
        if (images.empty()) {
            return "-";
        }

        std::string labels;
        for (std::size_t index = 0; index < images.size(); ++index) {
            if (index > 0) {
                labels += ", ";
            }
            labels += imageHandleLabel(images[index]);
        }
        return labels;
    }

    std::string_view RenderGraph::Impl::shaderStageName(RenderGraphShaderStage stage) {
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

    std::string_view RenderGraph::Impl::commandKindName(RenderGraphCommandKind kind) {
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
        case RenderGraphCommandKind::Dispatch:
            return "Dispatch";
        }
        return "";
    }

    std::string RenderGraph::Impl::commandDetail(const RenderGraphCommand& command) {
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

    std::string RenderGraph::Impl::imageAccessName(RenderGraphImageState state,
                                                   RenderGraphShaderStage shaderStage) {
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

    std::string RenderGraph::Impl::bufferAccessName(RenderGraphBufferState state,
                                                    RenderGraphShaderStage shaderStage) {
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

    std::string RenderGraph::Impl::slotImageLabel(const RenderGraphImageSlot& slot) const {
        std::string label = slot.name;
        if (slot.shaderStage != RenderGraphShaderStage::None) {
            label += "(";
            label += shaderStageName(slot.shaderStage);
            label += ")";
        }
        label += "=";
        label += imageHandleLabel(slot.image);
        return label;
    }

    std::string RenderGraph::Impl::slotBufferLabel(const RenderGraphBufferSlot& slot) const {
        std::string label = slot.name;
        if (slot.shaderStage != RenderGraphShaderStage::None) {
            label += "(";
            label += shaderStageName(slot.shaderStage);
            label += ")";
        }
        label += "=";
        label += bufferHandleLabel(slot.buffer);
        return label;
    }

    std::string
    RenderGraph::Impl::imageSlotList(std::span<const RenderGraphImageSlot> slots) const {
        if (slots.empty()) {
            return "-";
        }

        std::string labels;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (index > 0) {
                labels += ", ";
            }
            labels += slotImageLabel(slots[index]);
        }
        return labels;
    }

    std::string
    RenderGraph::Impl::bufferSlotList(std::span<const RenderGraphBufferSlot> slots) const {
        if (slots.empty()) {
            return "-";
        }

        std::string labels;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (index > 0) {
                labels += ", ";
            }
            labels += slotBufferLabel(slots[index]);
        }
        return labels;
    }

    void RenderGraph::Impl::appendSlotRows(std::string& output, std::string_view passName,
                                           std::string_view access,
                                           std::span<const RenderGraphImageSlot> slots) const {
        if (slots.empty()) {
            return;
        }

        for (const RenderGraphImageSlot& slot : slots) {
            output += "| ";
            output += passName;
            output += " | ";
            output += access;
            output += " | ";
            output += slot.name;
            if (slot.shaderStage != RenderGraphShaderStage::None) {
                output += "(";
                output += shaderStageName(slot.shaderStage);
                output += ")";
            }
            output += " | ";
            output += imageHandleLabel(slot.image);
            output += " |\n";
        }
    }

    void
    RenderGraph::Impl::appendBufferSlotRows(std::string& output, std::string_view passName,
                                            std::string_view access,
                                            std::span<const RenderGraphBufferSlot> slots) const {
        if (slots.empty()) {
            return;
        }

        for (const RenderGraphBufferSlot& slot : slots) {
            output += "| ";
            output += passName;
            output += " | ";
            output += access;
            output += " | ";
            output += slot.name;
            if (slot.shaderStage != RenderGraphShaderStage::None) {
                output += "(";
                output += shaderStageName(slot.shaderStage);
                output += ")";
            }
            output += " | ";
            output += bufferHandleLabel(slot.buffer);
            output += " |\n";
        }
    }

    void RenderGraph::Impl::appendCommandRows(std::string& output,
                                              const RenderGraphCompiledPass& pass) {
        for (std::size_t index = 0; index < pass.commands.size(); ++index) {
            const RenderGraphCommand& command = pass.commands[index];
            output += "| ";
            output += pass.name;
            output += " | ";
            output += std::to_string(index);
            output += " | ";
            output += commandKindName(command.kind);
            output += " | ";
            output += commandDetail(command);
            output += " |\n";
        }
    }

    void
    RenderGraph::Impl::appendTransitionRow(std::string& output, std::string_view phase,
                                           std::string_view passName,
                                           const RenderGraphImageTransition& transition) const {
        output += "| ";
        output += phase;
        output += " | ";
        output += passName;
        output += " | ";
        output += imageHandleLabel(transition.image);
        output += " | ";
        output += imageAccessName(transition.oldState, transition.oldShaderStage);
        output += " | ";
        output += imageAccessName(transition.newState, transition.newShaderStage);
        output += " |\n";
    }

    void
    RenderGraph::Impl::appendTransitionRow(std::string& output, std::string_view phase,
                                           std::string_view passName,
                                           const RenderGraphBufferTransition& transition) const {
        output += "| ";
        output += phase;
        output += " | ";
        output += passName;
        output += " | ";
        output += bufferHandleLabel(transition.buffer);
        output += " | ";
        output += bufferAccessName(transition.oldState, transition.oldShaderStage);
        output += " | ";
        output += bufferAccessName(transition.newState, transition.newShaderStage);
        output += " |\n";
    }

} // namespace asharia
