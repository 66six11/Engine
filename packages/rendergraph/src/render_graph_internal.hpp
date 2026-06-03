#pragma once

#include <vector>

#include "asharia/rendergraph/render_graph_builder.hpp"

#include "render_graph_pass.hpp"

namespace asharia {

    struct RenderGraph::Impl {
        std::vector<RenderGraphImageDesc> images_;
        std::vector<RenderGraphBufferDesc> buffers_;
        std::vector<rendergraph_internal::Pass> passes_;
    };

} // namespace asharia
