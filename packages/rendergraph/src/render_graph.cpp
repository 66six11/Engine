#include <memory>
#include <utility>

#include "render_graph_internal.hpp"

namespace asharia {
    RenderGraph::RenderGraph() : impl_(std::make_unique<Impl>()) {}

    RenderGraph::~RenderGraph() = default;

    RenderGraph::RenderGraph(const RenderGraph& other)
        : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>()) {}

    RenderGraph& RenderGraph::operator=(const RenderGraph& other) {
        if (this != &other) {
            impl_ = other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>();
        }
        return *this;
    }

    RenderGraph::RenderGraph(RenderGraph&& other) noexcept = default;

    RenderGraph& RenderGraph::operator=(RenderGraph&& other) noexcept = default;

    Result<RenderGraphCompileResult> RenderGraph::compile() const {
        return impl_->compile(nullptr);
    }

    Result<RenderGraphCompileResult>
    RenderGraph::compile(const RenderGraphSchemaRegistry& schemaRegistry) const {
        return impl_->compile(&schemaRegistry);
    }

    Result<void> RenderGraph::execute() const {
        auto compiled = compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        return impl_->execute(*compiled, nullptr);
    }

    Result<void> RenderGraph::execute(const RenderGraphExecutorRegistry& executorRegistry) const {
        auto compiled = compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        return impl_->execute(*compiled, &executorRegistry);
    }

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled) const {
        return impl_->execute(compiled, nullptr);
    }

    Result<void> RenderGraph::execute(const RenderGraphCompileResult& compiled,
                                      const RenderGraphExecutorRegistry& executorRegistry) const {
        return impl_->execute(compiled, &executorRegistry);
    }

    RenderGraphDiagnosticsSnapshot
    RenderGraph::diagnosticsSnapshot(const RenderGraphCompileResult& compiled) const {
        return impl_->diagnosticsSnapshot(compiled);
    }

    std::string RenderGraph::formatDebugTables(const RenderGraphCompileResult& compiled) const {
        return impl_->formatDebugTables(compiled);
    }

} // namespace asharia
