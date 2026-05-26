#pragma once

#include <cstdint>
#include <string_view>

#include "editor_input_router.hpp"

namespace asharia::editor {

    struct EditorActionInvokeContext;
    class EditorActionRegistry;

    struct EditorShortcutRouterStats {
        std::uint64_t evaluatedFrames{};
        std::uint64_t blockedFrames{};
        std::uint64_t shortcutMatches{};
        std::uint64_t shortcutInvocations{};
        std::uint64_t invalidShortcuts{};
    };

    class EditorShortcutRouter {
    public:
        void beginFrame(const EditorInputSnapshot& input);
        [[nodiscard]] bool routeImGuiShortcuts(EditorActionRegistry& actionRegistry,
                                               EditorActionInvokeContext context);
        [[nodiscard]] bool routeShortcut(EditorActionRegistry& actionRegistry,
                                         EditorActionInvokeContext context,
                                         std::string_view actionId, bool pressed);

        [[nodiscard]] EditorShortcutRouterStats stats() const;

    private:
        bool shortcutsEnabled_{};
        EditorShortcutRouterStats stats_;
    };

} // namespace asharia::editor
