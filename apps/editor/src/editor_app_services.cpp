#include "editor_app_services.hpp"

#include <utility>

#include "editor_app_registration.hpp"

namespace asharia::editor {

    EditorAppServices::EditorAppServices(const EditorSettingsRunState& settingsRun)
        : selectionSet(eventQueue), i18n(settingsRun.settings.locale),
          settingsController(settingsRun.settings, settingsRun.path, i18n),
          commandHistory(eventQueue), dirtyState(eventQueue), actionServices{
                                                                  .eventQueue = eventQueue,
                                                                  .panels = panelRegistry,
                                                                  .frameDebugger = frameDebugger,
                                                                  .workspace = workspaceController,
                                                              } {}

    asharia::VoidResult registerEditorAppServices(EditorAppServices& services) {
        auto registered =
            registerEditorAppRegistries(services.panelRegistry, services.actionRegistry,
                                        services.toolRegistry, services.eventQueue);
        if (!registered) {
            return std::unexpected{std::move(registered.error())};
        }
        return services.toolManager.syncTools(services.toolRegistry);
    }

} // namespace asharia::editor
