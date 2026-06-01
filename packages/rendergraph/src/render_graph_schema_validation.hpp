#pragma once

#include "asharia/core/result.hpp"

namespace asharia {
    class RenderGraphSchemaRegistry;
}

namespace asharia::rendergraph_internal {
    struct Pass;

    [[nodiscard]] Result<void> validatePassSchema(const Pass& pass,
                                                  const RenderGraphSchemaRegistry& schemaRegistry);
} // namespace asharia::rendergraph_internal
