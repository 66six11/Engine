#pragma once

#include <string>

namespace asharia {
    struct RenderGraphCompileResult;
}

namespace asharia::rendergraph_internal {

    struct RenderGraphDeclarationView;

    void appendResourceTables(const RenderGraphDeclarationView& declarations, std::string& output);
    void appendPassTable(const RenderGraphDeclarationView& declarations,
                         const RenderGraphCompileResult& compiled, std::string& output);
    void appendDependencyTable(const RenderGraphDeclarationView& declarations,
                               const RenderGraphCompileResult& compiled, std::string& output);
    void appendCulledPassTable(const RenderGraphCompileResult& compiled, std::string& output);

} // namespace asharia::rendergraph_internal
