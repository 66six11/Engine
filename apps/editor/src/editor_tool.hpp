#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

#include "editor_id.hpp"

namespace asharia::editor {

    enum class EditorToolCategory {
        Core,
        Viewport,
        Diagnostics,
        Styling,
        Settings,
    };

    enum class EditorToolbarSlot {
        None,
        Debug,
        View,
        Utility,
    };

    struct EditorToolPanelContribution {
        std::string panelId;
    };

    struct EditorToolActionContribution {
        std::string actionId;
        EditorToolbarSlot toolbarSlot{EditorToolbarSlot::None};
    };

    struct EditorToolViewportOverlayContribution {
        std::string overlayId;
        std::string viewportId;
    };

    struct EditorToolDesc {
        EditorId id;
        std::string title;
        std::string titleKey;
        EditorToolCategory category{EditorToolCategory::Core};
        std::vector<EditorToolPanelContribution> panels;
        std::vector<EditorToolActionContribution> actions;
        std::vector<EditorToolViewportOverlayContribution> viewportOverlays;
    };

    using EditorToolVisitor = std::function<void(const EditorToolDesc&)>;
    using EditorToolActionVisitor =
        std::function<void(const EditorToolDesc&, const EditorToolActionContribution&)>;
    using EditorToolViewportOverlayVisitor =
        std::function<void(const EditorToolDesc&, const EditorToolViewportOverlayContribution&)>;

    class EditorToolRegistry {
    public:
        [[nodiscard]] asharia::VoidResult registerTool(EditorToolDesc desc);
        [[nodiscard]] const EditorToolDesc* findTool(std::string_view toolId) const;
        [[nodiscard]] std::size_t toolCount() const;
        [[nodiscard]] std::size_t panelContributionCount() const;
        [[nodiscard]] std::size_t actionContributionCount() const;
        [[nodiscard]] std::size_t toolbarActionContributionCount() const;
        [[nodiscard]] std::size_t viewportOverlayContributionCount() const;
        void visitTools(const EditorToolVisitor& visitor) const;
        void visitToolbarActions(EditorToolbarSlot slot,
                                 const EditorToolActionVisitor& visitor) const;
        void visitViewportOverlays(std::string_view viewportId,
                                   const EditorToolViewportOverlayVisitor& visitor) const;

    private:
        [[nodiscard]] EditorToolDesc* findToolEntry(std::string_view toolId);
        [[nodiscard]] const EditorToolDesc* findToolEntry(std::string_view toolId) const;

        std::vector<EditorToolDesc> tools_;
    };

} // namespace asharia::editor
