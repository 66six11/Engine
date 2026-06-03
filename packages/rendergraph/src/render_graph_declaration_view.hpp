#pragma once

#include <span>

#include "asharia/rendergraph/render_graph_types.hpp"

#include "render_graph_pass.hpp"

namespace asharia::rendergraph_internal {
    struct RenderGraphDeclarationView {
        std::span<const RenderGraphImageDesc> images;
        std::span<const RenderGraphBufferDesc> buffers;
        std::span<const Pass> passes;
    };

    [[nodiscard]] RenderGraphDeclarationView
    makeRenderGraphDeclarationView(std::span<const RenderGraphImageDesc> images,
                                   std::span<const RenderGraphBufferDesc> buffers,
                                   std::span<const Pass> passes);

} // namespace asharia::rendergraph_internal
