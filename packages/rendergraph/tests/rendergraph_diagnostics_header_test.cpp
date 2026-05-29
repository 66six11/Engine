#include "asharia/rendergraph/render_graph_diagnostics.hpp"

namespace asharia::rendergraph_header_tests {

    void touchDiagnosticsHeader() {
        RenderGraphDiagnosticsSnapshot snapshot;
        snapshot.declaredPassCount = 1;
        snapshot.passes.push_back(RenderGraphDiagnosticsPassNode{
            .passIndex = 0,
            .declarationIndex = 0,
            .name = "DiagnosticsHeaderPass",
        });
        snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
            .phase = RenderGraphDiagnosticsTransitionPhase::Final,
            .resourceKind = RenderGraphResourceKind::Image,
            .resourceIndex = 0,
            .oldImageAccess =
                RenderGraphImageAccess{
                    .state = RenderGraphImageState::ColorAttachment,
                },
            .newImageAccess =
                RenderGraphImageAccess{
                    .state = RenderGraphImageState::Present,
                },
        });
    }

} // namespace asharia::rendergraph_header_tests
