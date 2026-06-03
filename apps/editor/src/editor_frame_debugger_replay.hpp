#pragma once

#include <cstddef>
#include <optional>

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

namespace asharia::editor::frame_debugger_replay {

    [[nodiscard]] std::optional<std::size_t>
    defaultReplayPassIndex(const asharia::RenderGraphDiagnosticsSnapshot& snapshot);

} // namespace asharia::editor::frame_debugger_replay
