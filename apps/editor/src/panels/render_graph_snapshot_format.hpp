#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "asharia/rendergraph/render_graph_diagnostics.hpp"

#include "editor_viewport.hpp"

namespace asharia::editor::render_graph_snapshot_format {

    struct AccessCell {
        bool read{};
        bool write{};
        std::uint32_t accessCount{};
        std::string detail;
    };

    [[nodiscard]] const char* boolName(bool value);
    [[nodiscard]] std::string fallbackText(std::string_view value);
    [[nodiscard]] const char* viewportKindName(EditorViewportKind kind);
    [[nodiscard]] const char* resourceKindName(asharia::RenderGraphResourceKind kind);
    [[nodiscard]] const char* accessRoleName(asharia::RenderGraphSlotAccess access);
    [[nodiscard]] const char* accessDirectionName(asharia::RenderGraphSlotAccess access);
    [[nodiscard]] std::string resourceLabel(asharia::RenderGraphResourceKind kind,
                                            std::uint32_t index, std::string_view name);
    [[nodiscard]] std::string passHeaderLabel(const asharia::RenderGraphDiagnosticsPassNode& pass);
    [[nodiscard]] AccessCell
    accessCellFor(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                  const asharia::RenderGraphDiagnosticsResourceNode& resource,
                  const asharia::RenderGraphDiagnosticsPassNode& pass);
    [[nodiscard]] std::string accessCellLabel(const AccessCell& cell);
    [[nodiscard]] std::string
    resourceShape(const asharia::RenderGraphDiagnosticsResourceNode& resource);
    [[nodiscard]] std::string
    resourceLifetime(const asharia::RenderGraphDiagnosticsResourceNode& resource);
    [[nodiscard]] std::string
    resourceAccessRange(const asharia::RenderGraphDiagnosticsResourceNode& resource);
    [[nodiscard]] std::string
    unityResourcePrefix(const asharia::RenderGraphDiagnosticsResourceNode& resource);

} // namespace asharia::editor::render_graph_snapshot_format
