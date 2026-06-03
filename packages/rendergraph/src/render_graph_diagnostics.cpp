#include <span>
#include <string>

#include "render_graph_debug_tables.hpp"
#include "render_graph_declaration_view.hpp"
#include "render_graph_diagnostics_snapshot.hpp"
#include "render_graph_internal.hpp"

namespace asharia {

    RenderGraphDiagnosticsSnapshot
    RenderGraph::diagnosticsSnapshot(const RenderGraphCompileResult& compiled) const {
        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_});
        return rendergraph_internal::makeDiagnosticsSnapshot(declarations, compiled);
    }

    std::string RenderGraph::formatDebugTables(const RenderGraphCompileResult& compiled) const {
        const rendergraph_internal::RenderGraphDeclarationView declarations =
            rendergraph_internal::makeRenderGraphDeclarationView(
                std::span<const RenderGraphImageDesc>{impl_->images_},
                std::span<const RenderGraphBufferDesc>{impl_->buffers_},
                std::span<const rendergraph_internal::Pass>{impl_->passes_});
        return rendergraph_internal::formatDebugTables(declarations, compiled);
    }

} // namespace asharia
