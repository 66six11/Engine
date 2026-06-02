#include "editor_tool_manager.hpp"

#include <algorithm>
#include <utility>

#include "asharia/core/error.hpp"

#include "editor_tool.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] asharia::Error toolManagerError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        [[nodiscard]] bool isUsableToolState(EditorToolLifecycleState lifecycle) {
            return lifecycle != EditorToolLifecycleState::Unregistered;
        }

    } // namespace

    asharia::VoidResult EditorToolManager::syncTools(const EditorToolRegistry& toolRegistry) {
        for (ToolState& state : tools_) {
            if (toolRegistry.findTool(state.toolId) == nullptr) {
                state.lifecycle = EditorToolLifecycleState::Unregistered;
            }
        }

        toolRegistry.visitTools([&](const EditorToolDesc& tool) {
            ToolState* existing = findToolState(tool.id.value);
            if (existing != nullptr) {
                if (existing->lifecycle == EditorToolLifecycleState::Unregistered) {
                    existing->lifecycle = EditorToolLifecycleState::Available;
                }
                return;
            }
            tools_.push_back(ToolState{
                .toolId = tool.id.value,
                .lifecycle = EditorToolLifecycleState::Available,
            });
        });

        for (ViewportToolState& viewport : viewports_) {
            ToolState* active = findToolState(viewport.activeToolId);
            if (active == nullptr || active->lifecycle == EditorToolLifecycleState::Unregistered) {
                viewport.activeToolId.clear();
            }
        }
        return {};
    }

    asharia::VoidResult
    EditorToolManager::beginActivateToolForViewport(EditorToolViewportBinding binding) {
        const std::string_view viewportId = binding.viewportId;
        const std::string_view toolId = binding.toolId;
        if (auto validated = validateViewportTool(binding); !validated) {
            return validated;
        }
        ViewportToolState* viewport = findViewportState(viewportId);
        if (viewport == nullptr) {
            viewports_.push_back(ViewportToolState{
                .viewportId = std::string{viewportId},
                .activeToolId = {},
            });
            viewport = &viewports_.back();
        }

        ToolState* tool = findToolState(toolId);
        if (tool == nullptr) {
            return std::unexpected{toolManagerError("Editor tool is not registered")};
        }

        const std::string previousToolId = viewport->activeToolId;
        viewport->activeToolId = std::string{toolId};

        if (!previousToolId.empty() && std::string_view{previousToolId} != toolId) {
            if (ToolState* previous = findToolState(previousToolId);
                previous != nullptr &&
                previous->lifecycle != EditorToolLifecycleState::Unregistered &&
                !hasActiveViewportForTool(previous->toolId)) {
                previous->lifecycle = EditorToolLifecycleState::Inactive;
            }
        }
        tool->lifecycle = EditorToolLifecycleState::Activating;
        return {};
    }

    asharia::VoidResult
    EditorToolManager::completeToolActivation(EditorToolViewportBinding binding) {
        const std::string_view toolId = binding.toolId;
        if (auto validated = validateViewportTool(binding); !validated) {
            return validated;
        }
        if (!isToolActiveForViewport(binding)) {
            return std::unexpected{toolManagerError("Editor tool is not activating for viewport")};
        }
        ToolState* tool = findToolState(toolId);
        if (tool == nullptr || tool->lifecycle != EditorToolLifecycleState::Activating) {
            return std::unexpected{toolManagerError("Editor tool activation was not started")};
        }
        tool->lifecycle = EditorToolLifecycleState::Active;
        return {};
    }

    asharia::VoidResult
    EditorToolManager::beginDeactivateToolForViewport(EditorToolViewportBinding binding) {
        const std::string_view toolId = binding.toolId;
        if (auto validated = validateViewportTool(binding); !validated) {
            return validated;
        }
        if (!isToolActiveForViewport(binding)) {
            return std::unexpected{toolManagerError("Editor tool is not active for viewport")};
        }
        ToolState* tool = findToolState(toolId);
        if (tool == nullptr || tool->lifecycle != EditorToolLifecycleState::Active) {
            return std::unexpected{toolManagerError("Editor tool is not active")};
        }
        tool->lifecycle = EditorToolLifecycleState::Suspending;
        return {};
    }

    asharia::VoidResult
    EditorToolManager::completeToolDeactivation(EditorToolViewportBinding binding) {
        const std::string_view viewportId = binding.viewportId;
        const std::string_view toolId = binding.toolId;
        if (auto validated = validateViewportTool(binding); !validated) {
            return validated;
        }
        ViewportToolState* viewport = findViewportState(viewportId);
        if (viewport == nullptr || std::string_view{viewport->activeToolId} != toolId) {
            return std::unexpected{
                toolManagerError("Editor tool is not deactivating for viewport")};
        }
        ToolState* tool = findToolState(toolId);
        if (tool == nullptr || tool->lifecycle != EditorToolLifecycleState::Suspending) {
            return std::unexpected{toolManagerError("Editor tool deactivation was not started")};
        }
        viewport->activeToolId.clear();
        tool->lifecycle = hasActiveViewportForTool(toolId) ? EditorToolLifecycleState::Active
                                                           : EditorToolLifecycleState::Inactive;
        return {};
    }

    asharia::VoidResult
    EditorToolManager::activateToolForViewport(EditorToolViewportBinding binding) {
        if (auto started = beginActivateToolForViewport(binding); !started) {
            return started;
        }
        return completeToolActivation(binding);
    }

    asharia::VoidResult
    EditorToolManager::deactivateToolForViewport(EditorToolViewportBinding binding) {
        if (auto started = beginDeactivateToolForViewport(binding); !started) {
            return started;
        }
        return completeToolDeactivation(binding);
    }

    EditorToolLifecycleState EditorToolManager::lifecycleState(std::string_view toolId) const {
        const ToolState* state = findToolState(toolId);
        if (state == nullptr) {
            return EditorToolLifecycleState::Unregistered;
        }
        return state->lifecycle;
    }

    std::string_view EditorToolManager::activeToolForViewport(std::string_view viewportId) const {
        const ViewportToolState* viewport = findViewportState(viewportId);
        if (viewport == nullptr) {
            return {};
        }
        return viewport->activeToolId;
    }

    bool EditorToolManager::isToolActiveForViewport(EditorToolViewportBinding binding) const {
        const ViewportToolState* viewport = findViewportState(binding.viewportId);
        return viewport != nullptr && std::string_view{viewport->activeToolId} == binding.toolId;
    }

    std::size_t EditorToolManager::trackedToolCount() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            tools_, [](const ToolState& tool) { return isUsableToolState(tool.lifecycle); }));
    }

    std::size_t EditorToolManager::activeViewportCount() const {
        return static_cast<std::size_t>(
            std::ranges::count_if(viewports_, [](const ViewportToolState& viewport) {
                return !viewport.activeToolId.empty();
            }));
    }

    EditorToolManager::ToolState* EditorToolManager::findToolState(std::string_view toolId) {
        const auto found = std::ranges::find_if(
            tools_, [toolId](const ToolState& state) { return state.toolId == toolId; });
        if (found == tools_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorToolManager::ToolState*
    EditorToolManager::findToolState(std::string_view toolId) const {
        const auto found = std::ranges::find_if(
            tools_, [toolId](const ToolState& state) { return state.toolId == toolId; });
        if (found == tools_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    EditorToolManager::ViewportToolState*
    EditorToolManager::findViewportState(std::string_view viewportId) {
        const auto found =
            std::ranges::find_if(viewports_, [viewportId](const ViewportToolState& state) {
                return state.viewportId == viewportId;
            });
        if (found == viewports_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorToolManager::ViewportToolState*
    EditorToolManager::findViewportState(std::string_view viewportId) const {
        const auto found =
            std::ranges::find_if(viewports_, [viewportId](const ViewportToolState& state) {
                return state.viewportId == viewportId;
            });
        if (found == viewports_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    bool EditorToolManager::hasActiveViewportForTool(std::string_view toolId) const {
        return std::ranges::any_of(viewports_, [toolId](const ViewportToolState& viewport) {
            return std::string_view{viewport.activeToolId} == toolId;
        });
    }

    asharia::VoidResult EditorToolManager::validateKnownTool(std::string_view toolId) const {
        if (toolId.empty()) {
            return std::unexpected{toolManagerError("Editor tool id must not be empty")};
        }
        const ToolState* state = findToolState(toolId);
        if (state == nullptr || !isUsableToolState(state->lifecycle)) {
            return std::unexpected{
                toolManagerError("Editor tool is not registered with the tool manager")};
        }
        return {};
    }

    asharia::VoidResult
    EditorToolManager::validateViewportTool(EditorToolViewportBinding binding) const {
        if (binding.viewportId.empty()) {
            return std::unexpected{toolManagerError("Editor tool viewport id must not be empty")};
        }
        return validateKnownTool(binding.toolId);
    }

} // namespace asharia::editor
