#pragma once

#include <cstddef>
#include <span>

#include "asharia/rendergraph/render_graph_types.hpp"

#include "render_graph_pass.hpp"

namespace asharia::rendergraph_internal {
    struct RenderGraphDeclarationView {
        std::span<const RenderGraphImageDesc> images;
        std::span<const RenderGraphBufferDesc> buffers;
        std::span<const Pass> passes;
        std::size_t mutationGeneration{};
    };

    [[nodiscard]] RenderGraphDeclarationView
    makeRenderGraphDeclarationView(std::span<const RenderGraphImageDesc> images,
                                   std::span<const RenderGraphBufferDesc> buffers,
                                   std::span<const Pass> passes,
                                   std::size_t mutationGeneration);

} // namespace asharia::rendergraph_internal
