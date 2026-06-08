#include "editor_app.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/core/log.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_app_config.hpp"
#include "editor_app_run_completion.hpp"
#include "editor_app_services.hpp"
#include "editor_asset_catalog.hpp"
#include "editor_asset_catalog_smoke.hpp"
#include "editor_asset_import_settings_command.hpp"
#include "editor_loop_host.hpp"
#include "editor_render_runtime.hpp"
#include "editor_smoke.hpp"
#include "editor_smoke_validation.hpp"
#include "editor_vulkan_host.hpp"
#include "imgui_runtime.hpp"

namespace {

    [[nodiscard]] std::string editorEnvironmentValue(const char* name) {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
            return {};
        }
        const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
        return std::string{ownedValue.get()};
#else
        const char* value = std::getenv(name);
        return value == nullptr ? std::string{} : std::string{value};
#endif
    }

    [[nodiscard]] std::optional<asharia::editor::EditorAssetCatalogSnapshotRequest>
    editorAssetCatalogRequestFromConfig(
        const asharia::editor::EditorAssetCatalogRunConfig& config) {
        if (!config) {
            return std::nullopt;
        }

        std::string targetProfile = config.targetProfile;
        if (targetProfile.empty()) {
            targetProfile = "editor-preview";
        }

        return asharia::editor::EditorAssetCatalogSnapshotRequest{
            .projectFile = config.projectFile,
            .productManifestFile = config.productManifestFile,
            .targetProfile = std::move(targetProfile),
        };
    }

    [[nodiscard]] std::optional<asharia::editor::EditorAssetCatalogSnapshotRequest>
    editorAssetCatalogRequestFromEnvironment() {
        const std::string project = editorEnvironmentValue("ASHARIA_EDITOR_PROJECT");
        if (project.empty()) {
            return std::nullopt;
        }

        std::string targetProfile = editorEnvironmentValue("ASHARIA_EDITOR_ASSET_TARGET_PROFILE");
        if (targetProfile.empty()) {
            targetProfile = "editor-preview";
        }

        return asharia::editor::EditorAssetCatalogSnapshotRequest{
            .projectFile = std::filesystem::path{project},
            .productManifestFile =
                std::filesystem::path{editorEnvironmentValue("ASHARIA_EDITOR_PRODUCT_MANIFEST")},
            .targetProfile = std::move(targetProfile),
        };
    }

    void loadEditorAssetCatalogForRun(asharia::editor::EditorAssetCatalogStore& store,
                                      const asharia::editor::EditorRunConfig& config) {
        const asharia::editor::EditorRunMode mode = config.mode;
        if (asharia::editor::isEditorAssetBrowserSmokeMode(mode)) {
            asharia::editor::EditorAssetCatalogSnapshot snapshot =
                asharia::editor::loadEditorAssetCatalogSmokeSnapshot();
            const bool succeeded = snapshot.succeeded();
            const std::size_t rowCount = snapshot.catalogView.entries.size();
            const std::size_t diagnosticCount = snapshot.diagnostics.size();
            store.useSnapshot(std::move(snapshot));
            if (!succeeded || rowCount == 0U) {
                asharia::logError("Editor asset browser smoke could not load project catalog "
                                  "snapshot rows=" +
                                  std::to_string(rowCount) +
                                  " diagnostics=" + std::to_string(diagnosticCount));
            }
            return;
        }

        if (asharia::editor::isEditorSmokeMode(mode)) {
            store.useFixtureCatalog();
            return;
        }

        const std::optional<asharia::editor::EditorAssetCatalogSnapshotRequest> request =
            editorAssetCatalogRequestFromConfig(config.assetCatalog)
                .or_else(editorAssetCatalogRequestFromEnvironment);
        if (!request) {
            store.useFixtureCatalog();
            return;
        }

        asharia::editor::EditorAssetCatalogSnapshot snapshot =
            asharia::editor::loadEditorAssetCatalogSnapshot(*request);
        const bool succeeded = snapshot.succeeded();
        const std::size_t rowCount = snapshot.catalogView.entries.size();
        const std::size_t diagnosticCount = snapshot.diagnostics.size();
        store.useSnapshot(std::move(snapshot));
        if (succeeded) {
            asharia::logInfo("Loaded editor asset catalog snapshot rows=" +
                             std::to_string(rowCount));
        } else {
            asharia::logWarning("Loaded editor asset catalog snapshot with diagnostics rows=" +
                                std::to_string(rowCount) +
                                " diagnostics=" + std::to_string(diagnosticCount));
        }
    }

    void seedAssetBrowserSmokePendingReimport(
        const asharia::editor::EditorAssetCatalogStore& store,
        asharia::editor::EditorAssetReimportRequestLog& reimportRequests,
        asharia::editor::EditorAssetReimportPendingState& pendingReimports) {
        const asharia::editor::EditorAssetCatalogSnapshot* snapshot = store.snapshot();
        if (snapshot == nullptr || pendingReimports.size() != 0U) {
            return;
        }

        const auto row =
            std::ranges::find_if(store.catalogView().entries,
                                 [](const asharia::asset::AssetCatalogViewEntry& candidate) {
                                     return candidate.guid &&
                                            !candidate.importProfileName.empty() &&
                                            !candidate.sourcePath.empty();
                                 });
        if (row == store.catalogView().entries.end()) {
            return;
        }

        std::string targetProfile = snapshot->targetProfile;
        if (targetProfile.empty()) {
            targetProfile = "editor-preview";
        }
        asharia::editor::EditorAssetReimportRequest request{
            .guid = row->guid,
            .sourcePath = row->sourcePath,
            .metadataFile = asharia::editor::resolveEditorAssetCatalogMetadataFilePath(
                *snapshot, row->sourcePath),
            .targetProfile = targetProfile,
            .changedSettingKeys = std::vector<std::string>{std::string{
                asharia::asset::kTextureImportProfileSettingKey}},
        };
        reimportRequests.record(request);
        pendingReimports.record(std::move(request));
    }

} // namespace

namespace asharia::editor {

    int runEditor(EditorRunMode mode) {
        return runEditor(EditorRunConfig{.mode = mode, .assetCatalog = {}});
    }

    int runEditor(const EditorRunConfig& config) {
        const EditorRunMode mode = config.mode;
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
        loadEditorAssetCatalogForRun(services.assetCatalogStore, config);
        if (isEditorAssetBrowserSmokeMode(mode)) {
            seedAssetBrowserSmokePendingReimport(services.assetCatalogStore,
                                                 services.assetReimportRequests,
                                                 services.assetPendingReimports);
        }
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
            services.eventQueue, services.diagnosticsLog, services.i18n, services.selectionSet,
            services.settingsController, services.panelRegistry, services.toolRegistry,
            services.toolManager, services.workspaceController, services.assetCatalogStore,
            services.assetIconRegistry, services.commandHistory, services.dirtyState,
            services.assetReimportRequests, services.assetPendingReimports, mode);
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
