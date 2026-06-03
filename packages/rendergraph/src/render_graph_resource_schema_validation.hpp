#pragma once

#include "asharia/core/result.hpp"

namespace asharia {
    struct RenderGraphPassSchema;
}

namespace asharia::rendergraph_internal {
    struct Pass;

    [[nodiscard]] Result<void>
    validateResourceSlotsAgainstSchema(const Pass& pass, const RenderGraphPassSchema& schema);

} // namespace asharia::rendergraph_internal
