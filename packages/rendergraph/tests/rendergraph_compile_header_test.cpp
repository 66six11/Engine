#include "asharia/rendergraph/render_graph_compile.hpp"

namespace asharia::rendergraph_header_tests {

    void touchCompileHeader() {
        RenderGraphCompileResult result;
        result.declaredPassCount = 1;
        result.declaredImageCount = 1;
        result.dependencies.push_back(RenderGraphPassDependency{
            .fromDeclarationIndex = 0,
            .toDeclarationIndex = 0,
            .resourceKind = RenderGraphResourceKind::Image,
            .image = RenderGraphImageHandle{.index = 0},
            .reason = "header test",
        });
        result.culledPasses.push_back(RenderGraphCulledPass{
            .declarationIndex = 0,
            .name = "CulledHeaderPass",
            .reason = "header test",
        });
    }

} // namespace asharia::rendergraph_header_tests
