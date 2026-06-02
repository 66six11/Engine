#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::editor {

    class EditorToolRegistry;

    enum class EditorToolLifecycleState {
        Available,
        Activating,
        Active,
        Suspending,
        Inactive,
        Unregistered,
    };

    struct EditorToolViewportBinding {
        std::string_view viewportId{};
        std::string_view toolId{};
    };

    class EditorToolManager {
    public:
        [[nodiscard]] asharia::VoidResult syncTools(const EditorToolRegistry& toolRegistry);

        [[nodiscard]] asharia::VoidResult
        beginActivateToolForViewport(EditorToolViewportBinding binding);
        [[nodiscard]] asharia::VoidResult completeToolActivation(EditorToolViewportBinding binding);
        [[nodiscard]] asharia::VoidResult
        beginDeactivateToolForViewport(EditorToolViewportBinding binding);
        [[nodiscard]] asharia::VoidResult
        completeToolDeactivation(EditorToolViewportBinding binding);

        [[nodiscard]] asharia::VoidResult
        activateToolForViewport(EditorToolViewportBinding binding);
        [[nodiscard]] asharia::VoidResult
        deactivateToolForViewport(EditorToolViewportBinding binding);

        [[nodiscard]] EditorToolLifecycleState lifecycleState(std::string_view toolId) const;
        [[nodiscard]] std::string_view activeToolForViewport(std::string_view viewportId) const;
        [[nodiscard]] bool isToolActiveForViewport(EditorToolViewportBinding binding) const;
        [[nodiscard]] std::size_t trackedToolCount() const;
        [[nodiscard]] std::size_t activeViewportCount() const;

    private:
        struct ToolState {
            std::string toolId;
            EditorToolLifecycleState lifecycle{EditorToolLifecycleState::Available};
        };

        struct ViewportToolState {
            std::string viewportId;
            std::string activeToolId;
        };

        [[nodiscard]] ToolState* findToolState(std::string_view toolId);
        [[nodiscard]] const ToolState* findToolState(std::string_view toolId) const;
        [[nodiscard]] ViewportToolState* findViewportState(std::string_view viewportId);
        [[nodiscard]] const ViewportToolState* findViewportState(std::string_view viewportId) const;
        [[nodiscard]] bool hasActiveViewportForTool(std::string_view toolId) const;
        [[nodiscard]] asharia::VoidResult validateKnownTool(std::string_view toolId) const;
        [[nodiscard]] asharia::VoidResult
        validateViewportTool(EditorToolViewportBinding binding) const;

        std::vector<ToolState> tools_;
        std::vector<ViewportToolState> viewports_;
    };

} // namespace asharia::editor
