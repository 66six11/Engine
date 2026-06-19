#include "render_graph_declaration_view.hpp"

#include "render_graph_pass.hpp"

namespace asharia::rendergraph_internal {

    RenderGraphDeclarationView
    makeRenderGraphDeclarationView(std::span<const RenderGraphImageDesc> images,
                                   std::span<const RenderGraphBufferDesc> buffers,
                                   std::span<const Pass> passes,
                                   std::size_t mutationGeneration) {
        return RenderGraphDeclarationView{
            .images = images,
            .buffers = buffers,
            .passes = passes,
            .mutationGeneration = mutationGeneration,
        };
    }

} // namespace asharia::rendergraph_internal
