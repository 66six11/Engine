#include <cstddef>
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
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readDepth(std::string slotName,
                                                                  RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].depthReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::writeDepth(std::string slotName,
                                                                   RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].depthWriteSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::readTransfer(std::string slotName,
                                                                     RenderGraphImageHandle image) {
        graph_->impl_->passes_[passIndex_].transferReadSlots.push_back(RenderGraphImageSlot{
            .name = std::move(slotName),
            .image = image,
        });
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::readTransferBuffer(std::string slotName,
                                                 RenderGraphBufferHandle buffer) {
        graph_->impl_->passes_[passIndex_].bufferTransferReadSlots.push_back(RenderGraphBufferSlot{
            .name = std::move(slotName),
            .buffer = buffer,
        });
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
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
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::hasSideEffects(bool hasSideEffects) {
        graph_->impl_->passes_[passIndex_].hasSideEffects = hasSideEffects;
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::setParamsType(std::string paramsType) {
        graph_->impl_->passes_[passIndex_].paramsType = std::move(paramsType);
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::setParamsData(std::vector<std::byte> paramsData) {
        graph_->impl_->passes_[passIndex_].paramsData = std::move(paramsData);
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder& RenderGraph::PassBuilder::execute(RenderGraphPassCallback callback) {
        graph_->impl_->passes_[passIndex_].callback = std::move(callback);
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder&
    RenderGraph::PassBuilder::setCommands(RenderGraphCommandList commands) {
        graph_->impl_->passes_[passIndex_].commands = std::move(commands).takeCommands();
        ++graph_->impl_->mutationGeneration_;
        return *this;
    }

    RenderGraph::PassBuilder::PassBuilder(RenderGraph& graph, std::size_t passIndex)
        : graph_(&graph), passIndex_(passIndex) {}

} // namespace asharia
