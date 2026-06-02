#include "editor_tool.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] bool emptyPanelContribution(const EditorToolPanelContribution& panel) {
            return panel.panelId.empty();
        }

        [[nodiscard]] bool emptyActionContribution(const EditorToolActionContribution& action) {
            return action.actionId.empty();
        }

        [[nodiscard]] bool emptyActivationViewportId(const std::string& viewportId) {
            return viewportId.empty();
        }

        [[nodiscard]] bool
        emptyViewportOverlayContribution(const EditorToolViewportOverlayContribution& overlay) {
            return overlay.overlayId.empty() || overlay.viewportId.empty();
        }

        [[nodiscard]] std::size_t toolbarActionCount(const EditorToolDesc& tool) {
            return static_cast<std::size_t>(
                std::ranges::count_if(tool.actions, [](const EditorToolActionContribution& action) {
                    return action.toolbarSlot != EditorToolbarSlot::None;
                }));
        }

        [[nodiscard]] bool hasDuplicateActivationViewportIds(const EditorToolDesc& desc) {
            for (auto viewport = desc.activationViewportIds.begin();
                 viewport != desc.activationViewportIds.end(); ++viewport) {
                const auto duplicate = std::ranges::find(
                    std::next(viewport), desc.activationViewportIds.end(), *viewport);
                if (duplicate != desc.activationViewportIds.end()) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool hasViewportActivationPolicy(const EditorToolDesc& desc) {
            return desc.activationPolicy != EditorToolActivationPolicy::None;
        }

    } // namespace

    asharia::VoidResult EditorToolRegistry::registerTool(EditorToolDesc desc) {
        if (desc.id.value.empty()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0, "Editor tool id must not be empty"}};
        }
        if (desc.title.empty()) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor tool title must not be empty"}};
        }
        if (findToolEntry(desc.id.value) != nullptr) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor tool is already registered: " + desc.id.value}};
        }
        if (std::ranges::any_of(desc.panels, emptyPanelContribution)) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor tool panel id must not be empty"}};
        }
        if (std::ranges::any_of(desc.actions, emptyActionContribution)) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor tool action id must not be empty"}};
        }
        if (!hasViewportActivationPolicy(desc) && !desc.activationViewportIds.empty()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor tool viewport activation ids require an activation policy"}};
        }
        if (hasViewportActivationPolicy(desc) && desc.activationViewportIds.empty()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor tool viewport activation policy requires a viewport id"}};
        }
        if (std::ranges::any_of(desc.activationViewportIds, emptyActivationViewportId)) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor tool activation viewport id must not be empty"}};
        }
        if (hasDuplicateActivationViewportIds(desc)) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor tool activation viewport ids must be unique"}};
        }
        if (std::ranges::any_of(desc.viewportOverlays, emptyViewportOverlayContribution)) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::Core, 0,
                "Editor tool viewport overlay id and viewport id must not be empty"}};
        }

        tools_.push_back(std::move(desc));
        return {};
    }

    const EditorToolDesc* EditorToolRegistry::findTool(std::string_view toolId) const {
        return findToolEntry(toolId);
    }

    std::size_t EditorToolRegistry::toolCount() const {
        return tools_.size();
    }

    std::size_t EditorToolRegistry::panelContributionCount() const {
        std::size_t count = 0;
        for (const EditorToolDesc& tool : tools_) {
            count += tool.panels.size();
        }
        return count;
    }

    std::size_t EditorToolRegistry::actionContributionCount() const {
        std::size_t count = 0;
        for (const EditorToolDesc& tool : tools_) {
            count += tool.actions.size();
        }
        return count;
    }

    std::size_t EditorToolRegistry::toolbarActionContributionCount() const {
        std::size_t count = 0;
        for (const EditorToolDesc& tool : tools_) {
            count += toolbarActionCount(tool);
        }
        return count;
    }

    std::size_t EditorToolRegistry::viewportOverlayContributionCount() const {
        std::size_t count = 0;
        for (const EditorToolDesc& tool : tools_) {
            count += tool.viewportOverlays.size();
        }
        return count;
    }

    std::size_t EditorToolRegistry::viewportActivationToolCount() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            tools_, [](const EditorToolDesc& tool) { return hasViewportActivationPolicy(tool); }));
    }

    void EditorToolRegistry::visitTools(const EditorToolVisitor& visitor) const {
        if (!visitor) {
            return;
        }
        for (const EditorToolDesc& tool : tools_) {
            visitor(tool);
        }
    }

    void EditorToolRegistry::visitToolbarActions(EditorToolbarSlot slot,
                                                 const EditorToolActionVisitor& visitor) const {
        if (!visitor || slot == EditorToolbarSlot::None) {
            return;
        }
        for (const EditorToolDesc& tool : tools_) {
            for (const EditorToolActionContribution& action : tool.actions) {
                if (action.toolbarSlot == slot) {
                    visitor(tool, action);
                }
            }
        }
    }

    void EditorToolRegistry::visitViewportOverlays(
        std::string_view viewportId, const EditorToolViewportOverlayVisitor& visitor) const {
        if (!visitor || viewportId.empty()) {
            return;
        }
        for (const EditorToolDesc& tool : tools_) {
            for (const EditorToolViewportOverlayContribution& overlay : tool.viewportOverlays) {
                if (overlay.viewportId == viewportId) {
                    visitor(tool, overlay);
                }
            }
        }
    }

    EditorToolDesc* EditorToolRegistry::findToolEntry(std::string_view toolId) {
        const auto found = std::ranges::find_if(
            tools_, [toolId](const EditorToolDesc& tool) { return tool.id.value == toolId; });
        if (found == tools_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorToolDesc* EditorToolRegistry::findToolEntry(std::string_view toolId) const {
        const auto found = std::ranges::find_if(
            tools_, [toolId](const EditorToolDesc& tool) { return tool.id.value == toolId; });
        if (found == tools_.end()) {
            return nullptr;
        }
        return &(*found);
    }

} // namespace asharia::editor
