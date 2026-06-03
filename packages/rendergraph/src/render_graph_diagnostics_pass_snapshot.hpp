#pragma once

namespace asharia {
    struct RenderGraphCompileResult;
    struct RenderGraphDiagnosticsSnapshot;
} // namespace asharia

namespace asharia::rendergraph_internal {

    struct RenderGraphDeclarationView;

    void appendDiagnosticsPassNodes(const RenderGraphDeclarationView& declarations,
                                    const RenderGraphCompileResult& compiled,
                                    RenderGraphDiagnosticsSnapshot& snapshot);

} // namespace asharia::rendergraph_internal
