#include "asharia/rendergraph/render_graph_diagnostics.hpp"

namespace asharia::rendergraph_header_tests {

    void touchDiagnosticsHeader() {
        RenderGraphDiagnosticsSnapshot snapshot;
        snapshot.declaredPassCount = 1;
        snapshot.passes.push_back(RenderGraphDiagnosticsPassNode{
            .passIndex = 0,
            .declarationIndex = 0,
            .name = "DiagnosticsHeaderPass",
            .type = {},
            .paramsType = {},
            .allowCulling = false,
            .hasSideEffects = false,
            .commandCount = 0,
            .imageTransitionCount = 0,
            .bufferTransitionCount = 0,
        });
        snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
            .phase = RenderGraphDiagnosticsTransitionPhase::Final,
            .passIndex = 0,
            .declarationIndex = 0,
            .passName = {},
            .resourceKind = RenderGraphResourceKind::Image,
            .resourceIndex = 0,
            .resourceName = {},
            .oldImageAccess =
                RenderGraphImageAccess{
                    .state = RenderGraphImageState::ColorAttachment,
                },
            .newImageAccess =
                RenderGraphImageAccess{
                    .state = RenderGraphImageState::Present,
                },
            .oldBufferAccess = {},
            .newBufferAccess = {},
        });
    }

} // namespace asharia::rendergraph_header_tests
