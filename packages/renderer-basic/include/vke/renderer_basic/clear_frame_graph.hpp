#pragma once

#include "vke/rendergraph/render_graph.hpp"

namespace vke {

    [[nodiscard]] inline RenderGraphImageDesc backbufferDesc(
        RenderGraphImageFormat format, RenderGraphExtent2D extent,
        RenderGraphImageState initialState = RenderGraphImageState::Undefined,
        RenderGraphImageState finalState = RenderGraphImageState::Present) {
        return RenderGraphImageDesc{
            .name = "Backbuffer",
            .format = format,
            .extent = extent,
            .initialState = initialState,
            .finalState = finalState,
        };
    }

} // namespace vke
