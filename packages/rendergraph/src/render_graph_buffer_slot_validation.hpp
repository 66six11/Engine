#pragma once

#include <span>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia::rendergraph_internal {
    struct Pass;

    [[nodiscard]] Result<void>
    validatePassBufferSlots(std::span<const RenderGraphBufferDesc> buffers, const Pass& pass);
} // namespace asharia::rendergraph_internal
