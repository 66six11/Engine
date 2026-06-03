#pragma once

namespace asharia {
    struct RenderGraphCompileResult;
    struct RenderGraphDiagnosticsSnapshot;
} // namespace asharia

namespace asharia::rendergraph_internal {

    struct RenderGraphDeclarationView;

    [[nodiscard]] RenderGraphDiagnosticsSnapshot
    makeDiagnosticsSnapshot(const RenderGraphDeclarationView& declarations,
                            const RenderGraphCompileResult& compiled);

} // namespace asharia::rendergraph_internal
