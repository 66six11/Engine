#pragma once

#include <span>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {
    class RenderGraphSchemaRegistry;
}

namespace asharia::rendergraph_internal {

    struct Pass;

    [[nodiscard]] Result<void> validatePass(std::span<const RenderGraphImageDesc> images,
                                            std::span<const RenderGraphBufferDesc> buffers,
                                            const Pass& pass,
                                            const RenderGraphSchemaRegistry* schemaRegistry);

    [[nodiscard]] Result<void> validateImageHandle(std::span<const RenderGraphImageDesc> images,
                                                   RenderGraphImageHandle image);

    [[nodiscard]] Result<void> validateBufferHandle(std::span<const RenderGraphBufferDesc> buffers,
                                                    RenderGraphBufferHandle buffer);

    [[nodiscard]] Result<void> validateImages(std::span<const RenderGraphImageDesc> images);

    [[nodiscard]] Result<void> validateBuffers(std::span<const RenderGraphBufferDesc> buffers);

} // namespace asharia::rendergraph_internal
