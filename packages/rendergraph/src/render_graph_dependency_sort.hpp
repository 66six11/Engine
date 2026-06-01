#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_pass.hpp"

namespace asharia::rendergraph_internal {

    [[nodiscard]] Result<std::vector<std::size_t>>
    sortPassesByDependencies(std::span<const Pass> passes,
                             std::span<const RenderGraphPassDependency> dependencies,
                             const std::vector<bool>& activePasses);

} // namespace asharia::rendergraph_internal
