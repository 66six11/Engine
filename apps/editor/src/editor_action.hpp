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

    class EditorEventQueue;
    class EditorFrameDebugger;
    class EditorPanelRegistry;
    class EditorWorkspaceController;

    struct EditorActionContext {
        EditorPanelRegistry& panels;
        EditorFrameDebugger& frameDebugger;
        EditorWorkspaceController& workspace;
    };

    struct EditorActionInvokeContext {
        EditorEventQueue& eventQueue;
        EditorActionContext actions;
    };

    struct EditorActionServices {
        EditorEventQueue& eventQueue;
        EditorPanelRegistry& panels;
        EditorFrameDebugger& frameDebugger;
        EditorWorkspaceController& workspace;
    };

    [[nodiscard]] EditorActionInvokeContext
    makeEditorActionInvokeContext(EditorActionServices& services);

    struct EditorActionDesc {
        EditorId id;
        std::string menuPath;
        std::string label;
        std::string labelKey;
        std::string shortcut;
        bool enabled{true};
    };

    using EditorActionCallback = std::function<void(EditorActionContext&)>;
    using EditorActionVisitor = std::function<void(const EditorActionDesc&)>;

    class EditorActionRegistry {
    public:
        [[nodiscard]] asharia::VoidResult registerAction(EditorActionDesc desc,
                                                         EditorActionCallback callback = {});
        [[nodiscard]] bool invoke(std::string_view actionId, EditorActionInvokeContext context);
        [[nodiscard]] const EditorActionDesc* findAction(std::string_view actionId) const;
        [[nodiscard]] bool isEnabled(std::string_view actionId) const;
        [[nodiscard]] std::size_t actionCount() const;
        [[nodiscard]] std::size_t enabledActionCount() const;
        [[nodiscard]] std::uint64_t invokeCount(std::string_view actionId) const;
        void visitActions(const EditorActionVisitor& visitor) const;

    private:
        struct ActionEntry {
            EditorActionDesc desc;
            EditorActionCallback callback;
            std::uint64_t invokeCount{0};
        };

        [[nodiscard]] ActionEntry* findActionEntry(std::string_view actionId);
        [[nodiscard]] const ActionEntry* findActionEntry(std::string_view actionId) const;

        std::vector<ActionEntry> actions_;
    };

} // namespace asharia::editor
