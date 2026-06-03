#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "asharia/rendergraph/render_graph_pass_context.hpp"

namespace asharia::rendergraph_internal {

    struct Pass {
        std::string name;
        std::string type;
        std::string paramsType;
        std::vector<std::byte> paramsData;
        std::vector<RenderGraphImageSlot> colorWriteSlots;
        std::vector<RenderGraphImageSlot> shaderReadSlots;
        std::vector<RenderGraphImageSlot> depthReadSlots;
        std::vector<RenderGraphImageSlot> depthWriteSlots;
        std::vector<RenderGraphImageSlot> depthSampledReadSlots;
        std::vector<RenderGraphImageSlot> transferReadSlots;
        std::vector<RenderGraphImageSlot> transferWriteSlots;
        std::vector<RenderGraphBufferSlot> bufferReadSlots;
        std::vector<RenderGraphBufferSlot> bufferTransferReadSlots;
        std::vector<RenderGraphBufferSlot> bufferWriteSlots;
        std::vector<RenderGraphBufferSlot> bufferStorageReadWriteSlots;
        std::vector<RenderGraphCommand> commands;
        bool allowCulling{};
        bool hasSideEffects{};
        RenderGraphPassCallback callback;
    };

} // namespace asharia::rendergraph_internal
