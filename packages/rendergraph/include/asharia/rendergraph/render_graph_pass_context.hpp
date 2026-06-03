#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {

    struct RenderGraphPassContext {
        std::size_t passIndex{};
        std::size_t declarationIndex{};
        std::string_view name;
        std::string_view type;
        std::string_view paramsType;
        bool allowCulling{};
        bool hasSideEffects{};
        std::span<const std::byte> paramsData;
        std::span<const RenderGraphCommand> commands;
        std::span<const RenderGraphImageTransition> transitionsBefore;
        std::span<const RenderGraphImageHandle> colorWrites;
        std::span<const RenderGraphImageHandle> shaderReads;
        std::span<const RenderGraphImageHandle> depthReads;
        std::span<const RenderGraphImageHandle> depthWrites;
        std::span<const RenderGraphImageHandle> depthSampledReads;
        std::span<const RenderGraphImageHandle> transferReads;
        std::span<const RenderGraphImageHandle> transferWrites;
        std::span<const RenderGraphBufferHandle> bufferReads;
        std::span<const RenderGraphBufferHandle> bufferTransferReads;
        std::span<const RenderGraphBufferHandle> bufferWrites;
        std::span<const RenderGraphBufferHandle> bufferStorageReadWrites;
        std::span<const RenderGraphImageSlot> colorWriteSlots;
        std::span<const RenderGraphImageSlot> shaderReadSlots;
        std::span<const RenderGraphImageSlot> depthReadSlots;
        std::span<const RenderGraphImageSlot> depthWriteSlots;
        std::span<const RenderGraphImageSlot> depthSampledReadSlots;
        std::span<const RenderGraphImageSlot> transferReadSlots;
        std::span<const RenderGraphImageSlot> transferWriteSlots;
        std::span<const RenderGraphBufferSlot> bufferReadSlots;
        std::span<const RenderGraphBufferSlot> bufferTransferReadSlots;
        std::span<const RenderGraphBufferSlot> bufferWriteSlots;
        std::span<const RenderGraphBufferSlot> bufferStorageReadWriteSlots;
        std::span<const RenderGraphBufferTransition> bufferTransitionsBefore;
    };

    using RenderGraphPassCallback = std::function<Result<void>(RenderGraphPassContext)>;

} // namespace asharia
