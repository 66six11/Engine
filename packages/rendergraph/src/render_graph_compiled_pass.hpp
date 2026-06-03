#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"

namespace asharia {
    class RenderGraphSchemaRegistry;
}

namespace asharia::rendergraph_internal {
    struct Pass;

    [[nodiscard]] RenderGraphCompiledPass
    makeCompiledPass(const Pass& pass, std::size_t declarationIndex,
                     const RenderGraphSchemaRegistry* schemaRegistry);

    [[nodiscard]] Result<void>
    appendCompiledPassTransitions(std::span<const RenderGraphImageDesc> images,
                                  std::span<const RenderGraphBufferDesc> buffers,
                                  std::vector<RenderGraphImageAccess>& currentAccesses,
                                  std::vector<RenderGraphBufferAccess>& currentBufferAccesses,
                                  RenderGraphCompiledPass& compiledPass);
} // namespace asharia::rendergraph_internal
