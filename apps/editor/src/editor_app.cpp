#include "editor_app.hpp"

#include <cstdlib>

#include "asharia/core/log.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_app_config.hpp"
#include "editor_app_run_completion.hpp"
#include "editor_app_services.hpp"
#include "editor_loop_host.hpp"
#include "editor_render_runtime.hpp"
#include "editor_smoke.hpp"
#include "editor_smoke_validation.hpp"
#include "editor_vulkan_host.hpp"
#include "imgui_runtime.hpp"

namespace asharia::editor {

    int runEditor(EditorRunMode mode) {
        const bool smokeMode = asharia::editor::isEditorSmokeMode(mode);

        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Editor"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        auto vulkanContext = createEditorVulkanContext(*extensions, *window);
        if (!vulkanContext) {
            asharia::logError(vulkanContext.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        if (auto waited = waitForRenderableEditorWindow(*window, smokeMode); !waited) {
            asharia::logError(waited.error().message);
            return EXIT_FAILURE;
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = createEditorFrameLoop(*vulkanContext, *window);
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        if (auto loaded = asharia::editor::loadEditorI18nCatalog(editorI18nDirectory()); !loaded) {
            asharia::logError(loaded.error().message);
            return EXIT_FAILURE;
        }

        const asharia::editor::EditorLocale fallbackLocale = editorLocaleFromEnvironment();
        const EditorSettingsRunState settingsRun =
            loadEditorSettingsForRun(smokeMode, fallbackLocale);
        const asharia::editor::EditorLocale editorLocale = settingsRun.settings.locale;

        const ImGuiRuntimeDesc imguiDesc{
            .layoutIniPath = editorLayoutIniPathForRun(smokeMode),
            .theme = settingsRun.settings.theme,
            .enableCjkGlyphs = true,
            .cjkFontPath = {},
            .fontPixelSize = 16.0F,
        };
        EditorRenderRuntime renderRuntime;
        if (auto created =
                renderRuntime.create(window->nativeHandle(), *vulkanContext, *frameLoop, imguiDesc);
            !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }

        EditorAppServices services{settingsRun};
        if (auto registered = registerEditorAppServices(services); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        if (!validateEditorStartupGates(
                mode, renderRuntime.imgui(), editorLocale, settingsRun.settings.theme,
                services.actionRegistry, services.actionServices, services.settingsController,
                services.i18n, services.toolRegistry, services.toolManager)) {
            return EXIT_FAILURE;
        }

        auto runResult = runEditorLoop(
            *window, *frameLoop, renderRuntime.renderer(), renderRuntime.viewportCoordinator(),
            services.frameDebugger, services.actionRegistry, services.actionServices,
            services.eventQueue, services.diagnosticsLog, services.i18n,
            services.settingsController, services.panelRegistry, services.toolRegistry,
            services.toolManager, services.workspaceController, mode);
        if (!runResult) {
            asharia::logError(runResult.error().message);
            return EXIT_FAILURE;
        }
        if (!finishEditorRun(mode, *runResult, *window, renderRuntime.imgui(),
                             renderRuntime.viewportCoordinator(), services.frameDebugger,
                             services.workspaceController)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

} // namespace asharia::editor
