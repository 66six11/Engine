#include "editor_app_services.hpp"

#include "editor_app_registration.hpp"

namespace asharia::editor {

    EditorAppServices::EditorAppServices(const EditorSettingsRunState& settingsRun)
        : i18n(settingsRun.settings.locale),
          settingsController(settingsRun.settings, settingsRun.path, i18n),
          actionServices{
              .eventQueue = eventQueue,
              .panels = panelRegistry,
              .frameDebugger = frameDebugger,
              .workspace = workspaceController,
          } {}

    asharia::VoidResult registerEditorAppServices(EditorAppServices& services) {
        return registerEditorAppRegistries(services.panelRegistry, services.actionRegistry,
                                           services.toolRegistry, services.eventQueue);
    }

} // namespace asharia::editor
