#pragma once

#include <string>

namespace asharia {
    struct RenderGraphCompileResult;
}

namespace asharia::rendergraph_internal {

    struct RenderGraphDeclarationView;

    void appendSlotTable(const RenderGraphDeclarationView& declarations,
                         const RenderGraphCompileResult& compiled, std::string& output);
    void appendCommandTable(const RenderGraphCompileResult& compiled, std::string& output);
    void appendTransitionTable(const RenderGraphDeclarationView& declarations,
                               const RenderGraphCompileResult& compiled, std::string& output);
    void appendTransientTables(const RenderGraphDeclarationView& declarations,
                               const RenderGraphCompileResult& compiled, std::string& output);

} // namespace asharia::rendergraph_internal
