#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"

namespace asharia {
    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeColor(RenderGraphImageHandle image) {
        return writeColor("target", image);
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeColor(std::string slotName,
                                                                   RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].colorWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readTexture(std::string slotName, RenderGraphImageHandle image,
                                          RenderGraphShaderStage shaderStage) {
        graph_->impl_->passes_[passIndex_].shaderReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readDepth(std::string slotName,
                                                                  RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].depthReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeDepth(std::string slotName,
                                                                   RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].depthWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readDepthTexture(std::string slotName, RenderGraphImageHandle image,
                                               RenderGraphShaderStage shaderStage) {
        graph_->impl_->passes_[passIndex_].depthSampledReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readTransfer(std::string slotName,
                                                                     RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].transferReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readTransfer(RenderGraphImageHandle image) {
        return readTransfer("source", image);
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeTransfer(RenderGraphImageHandle image) {
        return writeTransfer("target", image);
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeTransfer(std::string slotName, RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].transferWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readBuffer(std::string slotName, RenderGraphBufferHandle buffer,
                                         RenderGraphShaderStage shaderStage) {
        graph_->impl_->passes_[passIndex_].bufferReadSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
            .shaderStage = shaderStage,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readTransferBuffer(std::string slotName,
                                                 RenderGraphBufferHandle buffer) {
        graph_->impl_->passes_[passIndex_].bufferTransferReadSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
        });
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeBuffer(RenderGraphBufferHandle buffer) {
        return writeBuffer("target", buffer);
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::writeBuffer(std::string slotName, RenderGraphBufferHandle buffer) {
        graph_->impl_->passes_[passIndex_].bufferWriteSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
        });
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readWriteStorageBuffer(
        std::string slotName, RenderGraphBufferHandle buffer, RenderGraphShaderStage shaderStage) {
        graph_->impl_->passes_[passIndex_].bufferStorageReadWriteSlots.push_back(
            RenderGraphBufferSlot{
                .name = std::move(slotName),
                .buffer = buffer,
                .shaderStage = shaderStage,
            });
        return *this;
    }

    std::string_view RenderGraph::PassBuilder::name() const {
        return graph_->impl_->passes_[passIndex_].name;
    }

    std::string_view RenderGraph::PassBuilder::type() const {
        return graph_->impl_->passes_[passIndex_].type;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::allowCulling(bool allow) {
        graph_->impl_->passes_[passIndex_].allowCulling = allow;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::hasSideEffects(bool hasSideEffects) {
        graph_->impl_->passes_[passIndex_].hasSideEffects = hasSideEffects;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::setParamsType(std::string paramsType) {
        graph_->impl_->passes_[passIndex_].paramsType = std::move(paramsType);
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::setParamsData(std::vector<std::byte> paramsData) {
        graph_->impl_->passes_[passIndex_].paramsData = std::move(paramsData);
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::execute(RenderGraphPassCallback callback) {
        graph_->impl_->passes_[passIndex_].callback = std::move(callback);
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::setCommands(RenderGraphCommandList commands) {
        graph_->impl_->passes_[passIndex_].commands = std::move(commands).takeCommands();
        return *this;
    }

    RenderGraph::PassBuilder::PassBuilder(RenderGraph& graph, std::size_t passIndex)
        : graph_(&graph), passIndex_(passIndex) {}

    RenderGraphImageHandle RenderGraph::importImage(RenderGraphImageDesc desc) {
        desc.lifetime = RenderGraphImageLifetime::Imported;
        impl_->images_.push_back(std::move(desc));
        return RenderGraphImageHandle{
            .index = static_cast<std::uint32_t>(impl_->images_.size() - 1),
        };
    }

    RenderGraphImageHandle RenderGraph::createTransientImage(RenderGraphImageDesc desc) {
        desc.lifetime = RenderGraphImageLifetime::Transient;
        desc.initialState = RenderGraphImageState::Undefined;
        desc.initialShaderStage = RenderGraphShaderStage::None;
        desc.finalState = RenderGraphImageState::Undefined;
        desc.finalShaderStage = RenderGraphShaderStage::None;
        impl_->images_.push_back(std::move(desc));
        return RenderGraphImageHandle{
            .index = static_cast<std::uint32_t>(impl_->images_.size() - 1),
        };
    }

    RenderGraphBufferHandle RenderGraph::importBuffer(RenderGraphBufferDesc desc) {
        desc.lifetime = RenderGraphBufferLifetime::Imported;
        impl_->buffers_.push_back(std::move(desc));
        return RenderGraphBufferHandle{
            .index = static_cast<std::uint32_t>(impl_->buffers_.size() - 1),
        };
    }

    RenderGraphBufferHandle RenderGraph::createTransientBuffer(RenderGraphBufferDesc desc) {
        desc.lifetime = RenderGraphBufferLifetime::Transient;
        desc.initialState = RenderGraphBufferState::Undefined;
        desc.initialShaderStage = RenderGraphShaderStage::None;
        desc.finalState = RenderGraphBufferState::Undefined;
        desc.finalShaderStage = RenderGraphShaderStage::None;
        impl_->buffers_.push_back(std::move(desc));
        return RenderGraphBufferHandle{
            .index = static_cast<std::uint32_t>(impl_->buffers_.size() - 1),
        };
    }

    RenderGraph::PassBuilder RenderGraph::addPass(std::string name) {
        Impl::Pass pass{
            .name = std::move(name),
            .type = {},
            .paramsType = {},
            .paramsData = {},
            .colorWriteSlots = {},
            .shaderReadSlots = {},
            .depthReadSlots = {},
            .depthWriteSlots = {},
            .depthSampledReadSlots = {},
            .transferReadSlots = {},
            .transferWriteSlots = {},
            .bufferReadSlots = {},
            .bufferTransferReadSlots = {},
            .bufferWriteSlots = {},
            .bufferStorageReadWriteSlots = {},
            .commands = {},
            .allowCulling = {},
            .hasSideEffects = {},
            .callback = {},
        };
        impl_->passes_.push_back(std::move(pass));
        return PassBuilder{*this, impl_->passes_.size() - 1};
    }

    RenderGraph::PassBuilder RenderGraph::addPass(std::string name, std::string type) {
        Impl::Pass pass{
            .name = std::move(name),
            .type = std::move(type),
            .paramsType = {},
            .paramsData = {},
            .colorWriteSlots = {},
            .shaderReadSlots = {},
            .depthReadSlots = {},
            .depthWriteSlots = {},
            .depthSampledReadSlots = {},
            .transferReadSlots = {},
            .transferWriteSlots = {},
            .bufferReadSlots = {},
            .bufferTransferReadSlots = {},
            .bufferWriteSlots = {},
            .bufferStorageReadWriteSlots = {},
            .commands = {},
            .allowCulling = {},
            .hasSideEffects = {},
            .callback = {},
        };
        impl_->passes_.push_back(std::move(pass));
        return PassBuilder{*this, impl_->passes_.size() - 1};
    }

} // namespace asharia
