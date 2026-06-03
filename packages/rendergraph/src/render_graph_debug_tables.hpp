#pragma once

#include <string>

namespace asharia {
    struct RenderGraphCompileResult;
}

namespace asharia::rendergraph_internal {

    struct RenderGraphDeclarationView;

    [[nodiscard]] std::string formatDebugTables(const RenderGraphDeclarationView& declarations,
                                                const RenderGraphCompileResult& compiled);

} // namespace asharia::rendergraph_internal
