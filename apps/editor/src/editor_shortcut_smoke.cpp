#include "editor_shortcut_smoke.hpp"

#include <cstdint>

#include "asharia/core/log.hpp"

#include "editor_action.hpp"
#include "editor_event.hpp"
#include "editor_input_router.hpp"
#include "editor_panel.hpp"
#include "editor_shortcut_router.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {
    bool validateShortcutRouterSmoke(EditorActionRegistry& actionRegistry,
                                     EditorActionServices& actionServices) {
        EditorShortcutRouter shortcutRouter;
        actionServices.eventQueue.clear();

        shortcutRouter.beginFrame(EditorInputSnapshot{
            .shortcutsEnabled = false,
        });
        if (shortcutRouter.routeShortcut(
                actionRegistry, makeEditorActionInvokeContext(actionServices), "view.log", true) ||
            actionRegistry.invokeCount("view.log") != 1 || !actionServices.eventQueue.empty()) {
            asharia::logError(
                "Editor shortcut router smoke invoked while shortcuts were disabled.");
            return false;
        }

        shortcutRouter.beginFrame(EditorInputSnapshot{
            .shortcutsEnabled = true,
        });
        if (shortcutRouter.routeShortcut(
                actionRegistry, makeEditorActionInvokeContext(actionServices), "file.open", true) ||
            actionRegistry.invokeCount("file.open") != 0 || !actionServices.eventQueue.empty()) {
            asharia::logError("Editor shortcut router smoke invoked a disabled action.");
            return false;
        }

        if (!actionServices.panels.closePanel("log") ||
            !shortcutRouter.routeShortcut(
                actionRegistry, makeEditorActionInvokeContext(actionServices), "view.log", true) ||
            !actionServices.panels.isOpen("log") || actionRegistry.invokeCount("view.log") != 2) {
            asharia::logError("Editor shortcut router smoke failed to invoke View/Log.");
            return false;
        }

        const EditorShortcutRouterStats stats = shortcutRouter.stats();
        if (stats.evaluatedFrames != 2 || stats.blockedFrames != 1 || stats.shortcutMatches != 2 ||
            stats.shortcutInvocations != 1) {
            asharia::logError("Editor shortcut router smoke detected invalid shortcut stats.");
            return false;
        }
        actionServices.eventQueue.clear();

        return true;
    }

    bool validateShortcutRouterRunSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (runResult.shortcutStats.evaluatedFrames <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor shortcut router smoke did not evaluate every rendered "
                              "frame.");
            return false;
        }
        if (runResult.shortcutStats.invalidShortcuts != 0) {
            asharia::logError("Editor shortcut router smoke found an invalid registered shortcut.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
