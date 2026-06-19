#pragma once

#include <cstddef>
#include <vector>

#include "asharia/rendergraph/render_graph_builder.hpp"

#include "render_graph_pass.hpp"

namespace asharia {

    struct RenderGraph::Impl {
        std::vector<RenderGraphImageDesc> images_;
        std::vector<RenderGraphBufferDesc> buffers_;
        std::vector<rendergraph_internal::Pass> passes_;
        std::size_t mutationGeneration_{};
    };

} // namespace asharia
