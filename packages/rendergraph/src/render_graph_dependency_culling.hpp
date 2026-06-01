#pragma once

#include <span>
#include <vector>

#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_pass.hpp"

namespace asharia {
    class RenderGraphSchemaRegistry;
}

namespace asharia::rendergraph_internal {

    [[nodiscard]] std::vector<bool>
    findActivePasses(std::span<const Pass> passes, std::span<const RenderGraphImageDesc> images,
                     std::span<const RenderGraphBufferDesc> buffers,
                     std::span<const RenderGraphPassDependency> dependencies,
                     const RenderGraphSchemaRegistry* schemaRegistry);

} // namespace asharia::rendergraph_internal
