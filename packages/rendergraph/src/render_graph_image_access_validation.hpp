#pragma once

#include <span>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia::rendergraph_internal {
    struct Pass;

    [[nodiscard]] Result<void>
    validateUniquePassImageAccesses(std::span<const RenderGraphImageDesc> images, const Pass& pass);
} // namespace asharia::rendergraph_internal
