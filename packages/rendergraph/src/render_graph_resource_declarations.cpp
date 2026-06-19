#include <cstdint>
#include <utility>

#include "render_graph_internal.hpp"

namespace asharia {

    RenderGraphImageHandle RenderGraph::importImage(RenderGraphImageDesc desc) {
        desc.lifetime = RenderGraphImageLifetime::Imported;
        impl_->images_.push_back(std::move(desc));
        ++impl_->mutationGeneration_;
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
        ++impl_->mutationGeneration_;
        return RenderGraphImageHandle{
            .index = static_cast<std::uint32_t>(impl_->images_.size() - 1),
        };
    }

    RenderGraphBufferHandle RenderGraph::importBuffer(RenderGraphBufferDesc desc) {
        desc.lifetime = RenderGraphBufferLifetime::Imported;
        impl_->buffers_.push_back(std::move(desc));
        ++impl_->mutationGeneration_;
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
        ++impl_->mutationGeneration_;
        return RenderGraphBufferHandle{
            .index = static_cast<std::uint32_t>(impl_->buffers_.size() - 1),
        };
    }

} // namespace asharia
