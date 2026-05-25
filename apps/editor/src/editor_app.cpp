#include "editor_app.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_action.hpp"
#include "editor_app_registration.hpp"
#include "editor_command.hpp"
#include "editor_context.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_inspected_world.hpp"
#include "editor_panel.hpp"
#include "editor_settings.hpp"
#include "editor_shortcut_router.hpp"
#include "editor_tool.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_coordinator.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "editor_workspace.hpp"
#include "imgui_editor_shell.hpp"
#include "imgui_runtime.hpp"

namespace {

    constexpr asharia::VulkanDebugLabelMode kEditorDebugLabels =
        asharia::VulkanDebugLabelMode::Required;
    constexpr int kSmokeFrameCount = 3;
    constexpr int kResizeSmokeFrameCount = 8;
    constexpr int kFrameDebuggerSmokeFrameCount = 7;
    constexpr int kSmokeAttemptLimit = 120;
    constexpr int kResizeSmokeWindowWidth = 960;
    constexpr int kResizeSmokeWindowHeight = 540;

    bool isSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode != asharia::editor::EditorRunMode::Interactive;
    }

    bool isViewportSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode == asharia::editor::EditorRunMode::SmokeViewport ||
               mode == asharia::editor::EditorRunMode::SmokeViewportResize ||
               mode == asharia::editor::EditorRunMode::SmokeFrameDebugger;
    }

    bool isViewportResizeSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode == asharia::editor::EditorRunMode::SmokeViewportResize;
    }

    bool isFrameDebuggerSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode == asharia::editor::EditorRunMode::SmokeFrameDebugger;
    }

    [[nodiscard]] std::filesystem::path editorSmokeLayoutIniPath() {
        std::error_code error;
        std::filesystem::path basePath = std::filesystem::temp_directory_path(error);
        if (error) {
            basePath = std::filesystem::current_path(error);
        }
        if (basePath.empty()) {
            basePath = ".";
        }
        return basePath / "Asharia" / "EditorSmoke" / "imgui-layout.ini";
    }

    [[nodiscard]] std::filesystem::path editorI18nDirectory() {
#if defined(ASHARIA_EDITOR_I18N_DIR)
        return std::filesystem::path{ASHARIA_EDITOR_I18N_DIR};
#else
        return std::filesystem::path{"resources/i18n"};
#endif
    }

    [[nodiscard]] std::filesystem::path editorLayoutIniPathForRun(bool smokeMode) {
        if (smokeMode) {
            return editorSmokeLayoutIniPath();
        }
        return {};
    }

    [[nodiscard]] std::filesystem::path editorSettingsPathForRun(bool smokeMode) {
        return smokeMode ? asharia::editor::editorSmokeSettingsPath()
                         : asharia::editor::editorUserSettingsPath();
    }

    struct EditorSettingsRunState {
        asharia::editor::EditorSettings settings;
        std::filesystem::path path;
    };

    [[nodiscard]] EditorSettingsRunState
    loadEditorSettingsForRun(bool smokeMode, asharia::editor::EditorLocale fallbackLocale) {
        EditorSettingsRunState state{
            .settings =
                asharia::editor::EditorSettings{
                    .locale = fallbackLocale,
                    .theme = asharia::editor::defaultEditorUiThemeId(),
                },
            .path = editorSettingsPathForRun(smokeMode),
        };
        if (smokeMode) {
            std::error_code removeError;
            std::filesystem::remove(state.path, removeError);
        }

        auto loaded = asharia::editor::loadEditorSettings(state.path, fallbackLocale);
        if (loaded) {
            state.settings = *loaded;
        } else {
            asharia::logError(loaded.error().message);
        }
        return state;
    }

    [[nodiscard]] std::string editorLocaleEnvironmentValue() {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, "ASHARIA_EDITOR_LOCALE") != 0 || value == nullptr) {
            return {};
        }
        const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
        return std::string{ownedValue.get()};
#else
        const char* value = std::getenv("ASHARIA_EDITOR_LOCALE");
        return value == nullptr ? std::string{} : std::string{value};
#endif
    }

    [[nodiscard]] asharia::editor::EditorLocale editorLocaleFromEnvironment() {
        const std::string value = editorLocaleEnvironmentValue();
        if (value.empty()) {
            return asharia::editor::EditorLocale::EnUs;
        }
        const std::optional<asharia::editor::EditorLocale> locale =
            asharia::editor::editorLocaleFromName(value);
        if (locale) {
            return *locale;
        }

        asharia::logError("Unsupported ASHARIA_EDITOR_LOCALE value: " + value);
        return asharia::editor::EditorLocale::EnUs;
    }

    int smokeFrameCount(asharia::editor::EditorRunMode mode) {
        if (isViewportResizeSmokeMode(mode)) {
            return kResizeSmokeFrameCount;
        }
        if (isFrameDebuggerSmokeMode(mode)) {
            return kFrameDebuggerSmokeFrameCount;
        }
        return kSmokeFrameCount;
    }

    bool isRenderableExtent(asharia::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, asharia::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool isRenderableExtent(VkExtent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool differs(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width != rhs.width || lhs.height != rhs.height;
    }

    bool closeFloat(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.0001F;
    }

    float editorVec3Distance(std::array<float, 3> lhs, std::array<float, 3> rhs) {
        const float deltaX = lhs[0] - rhs[0];
        const float deltaY = lhs[1] - rhs[1];
        const float deltaZ = lhs[2] - rhs[2];
        return std::sqrt((deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ));
    }

    bool sameViewportOverlayFlags(asharia::editor::EditorViewportOverlayFlags lhs,
                                  asharia::editor::EditorViewportOverlayFlags rhs) {
        return lhs.gridVisible == rhs.gridVisible && lhs.gizmoVisible == rhs.gizmoVisible &&
               lhs.wireVisible == rhs.wireVisible &&
               lhs.selectionOutlineVisible == rhs.selectionOutlineVisible &&
               lhs.debugOverlayVisible == rhs.debugOverlayVisible &&
               lhs.debugGizmoVisible == rhs.debugGizmoVisible;
    }

    [[nodiscard]] std::uint64_t
    renderGraphPassTypeCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                             std::string_view passType) {
        return static_cast<std::uint64_t>(std::ranges::count_if(
            snapshot.passes, [passType](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                return pass.type == passType;
            }));
    }

    [[nodiscard]] std::uint64_t
    renderGraphOverlayCommandCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        std::uint64_t commandCount = 0;
        for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
            if (pass.type == asharia::kBasicRenderViewOverlayPassType) {
                commandCount += static_cast<std::uint64_t>(pass.commandCount);
            }
        }
        return commandCount;
    }

    [[nodiscard]] bool
    validateRenderViewCameraSmoke(const asharia::BasicRenderViewDiagnostics& diagnostics) {
        if (!closeFloat(diagnostics.camera.position[0], 0.0F) ||
            !closeFloat(diagnostics.camera.position[1], 2.0F) ||
            !closeFloat(diagnostics.camera.position[2], -6.0F) ||
            !closeFloat(diagnostics.camera.nearPlane, 0.1F) ||
            !closeFloat(diagnostics.camera.farPlane, 1000.0F) ||
            diagnostics.camera.projection[0] <= 0.0F || diagnostics.camera.projection[5] <= 1.0F ||
            diagnostics.camera.viewProjection[10] == 0.0F) {
            asharia::logError(
                "Editor viewport smoke recorded invalid RenderView camera diagnostics.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateEditorViewportCameraSmoke() {
        const asharia::editor::EditorExtent2D extent{.width = 320, .height = 240};
        const asharia::editor::EditorViewportCamera camera =
            asharia::editor::defaultEditorSceneViewCamera(extent);
        const std::optional<asharia::editor::EditorViewportWorldRay> ray =
            asharia::editor::unprojectEditorViewportPoint(
                camera, extent, asharia::editor::EditorViewportPoint{.x = 160.0F, .y = 120.0F});
        if (!ray || !closeFloat(ray->origin[0], ray->nearPoint[0]) ||
            !closeFloat(ray->origin[1], ray->nearPoint[1]) ||
            !closeFloat(ray->origin[2], ray->nearPoint[2]) ||
            !closeFloat(ray->nearPoint[0], 0.0F) || !closeFloat(ray->nearPoint[1], 1.96837723F) ||
            !closeFloat(ray->nearPoint[2], -5.90513182F) ||
            !closeFloat(editorVec3Distance(camera.position, ray->nearPoint), camera.nearPlane) ||
            !closeFloat(ray->direction[0], 0.0F) || !closeFloat(ray->direction[1], -0.31622776F) ||
            !closeFloat(ray->direction[2], 0.94868332F)) {
            asharia::logError("Editor viewport smoke calculated an invalid center unproject ray.");
            return false;
        }

        const std::optional<asharia::editor::EditorViewportWorldRay> topLeftRay =
            asharia::editor::unprojectEditorViewportPoint(
                camera, extent, asharia::editor::EditorViewportPoint{.x = 0.0F, .y = 0.0F});
        const std::optional<asharia::editor::EditorViewportWorldRay> bottomRightRay =
            asharia::editor::unprojectEditorViewportPoint(
                camera, extent,
                asharia::editor::EditorViewportPoint{
                    .x = static_cast<float>(extent.width),
                    .y = static_cast<float>(extent.height),
                });
        if (!topLeftRay || !bottomRightRay || topLeftRay->direction[0] >= 0.0F ||
            topLeftRay->direction[1] <= 0.0F || bottomRightRay->direction[0] <= 0.0F ||
            bottomRightRay->direction[1] >= 0.0F) {
            asharia::logError("Editor viewport smoke found invalid viewport point orientation.");
            return false;
        }

        const float rayLength = std::sqrt((ray->direction[0] * ray->direction[0]) +
                                          (ray->direction[1] * ray->direction[1]) +
                                          (ray->direction[2] * ray->direction[2]));
        const asharia::editor::EditorViewportCamera resizedCamera =
            asharia::editor::editorViewportCameraForExtent(
                camera, asharia::editor::EditorExtent2D{.width = 640, .height = 320});
        const std::optional<asharia::editor::EditorViewportWorldRay> resizedTopLeftRay =
            asharia::editor::unprojectEditorViewportPoint(
                resizedCamera, asharia::editor::EditorExtent2D{.width = 640, .height = 320},
                asharia::editor::EditorViewportPoint{.x = 0.0F, .y = 0.0F});
        asharia::editor::EditorViewportCamera invalidCamera = camera;
        invalidCamera.viewProjection = {};
        if (!closeFloat(rayLength, 1.0F) || !closeFloat(resizedCamera.aspectRatio, 2.0F) ||
            !closeFloat(resizedCamera.position[0], camera.position[0]) ||
            !closeFloat(resizedCamera.position[1], camera.position[1]) ||
            !closeFloat(resizedCamera.position[2], camera.position[2]) ||
            resizedCamera.projection[0] >= resizedCamera.projection[5] || !resizedTopLeftRay ||
            std::fabs(resizedTopLeftRay->direction[0]) <= std::fabs(topLeftRay->direction[0]) ||
            asharia::editor::unprojectEditorViewportPoint(
                camera, asharia::editor::EditorExtent2D{.width = 0, .height = 240},
                asharia::editor::EditorViewportPoint{}) != std::nullopt ||
            asharia::editor::unprojectEditorViewportPoint(
                invalidCamera, extent, asharia::editor::EditorViewportPoint{}) != std::nullopt) {
            asharia::logError("Editor viewport smoke found invalid camera extent handling.");
            return false;
        }
        return true;
    }

    std::uint64_t extentArea(VkExtent2D extent) {
        return static_cast<std::uint64_t>(extent.width) * static_cast<std::uint64_t>(extent.height);
    }

    asharia::editor::EditorExtent2D editorExtentFromVk(VkExtent2D extent) {
        return asharia::editor::EditorExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    struct ImageBarrierDesc {
        VkImage image{VK_NULL_HANDLE};
        VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkPipelineStageFlags2 srcStageMask{};
        VkAccessFlags2 srcAccessMask{};
        VkPipelineStageFlags2 dstStageMask{};
        VkAccessFlags2 dstAccessMask{};
    };

    struct EditorSmokeRunResult {
        int renderedFrames{};
        bool resizeRequested{};
        bool resizedViewportPresented{};
        bool frameDebugCaptureRequested{};
        bool frameDebugReplayPassRequested{};
        bool frameDebugPreviewRequested{};
        bool frameDebugPreviewVisible{};
        bool frameDebugResumeRequested{};
        bool frameDebugRenderedAfterResume{};
        VkExtent2D viewportExtentBeforeResize{};
        VkExtent2D viewportExtentAfterResize{};
        std::uint64_t textureFramesBeforeResize{};
        std::uint64_t viewportFramesAtFrameDebugPause{};
        std::uint64_t viewportFramesAtFrameDebugPreview{};
        std::uint64_t viewportFramesAfterFrameDebugResume{};
        std::uint64_t inspectedWorldFramesAtFrameDebugPause{};
        std::uint64_t inspectedWorldFramesAtFrameDebugPreview{};
        std::uint64_t inspectedWorldFramesAfterFrameDebugResume{};
        asharia::editor::EditorInspectedWorldSchedulerStats inspectedWorldStats;
        asharia::editor::EditorInputRouterStats inputStats;
        asharia::editor::EditorShortcutRouterStats shortcutStats;
    };

    struct EditorViewportResizeSmokeState {
        bool requested{};
        bool presentedAfterResize{};
        VkExtent2D extentBeforeResize{};
        VkExtent2D extentAfterResize{};
        std::uint64_t textureFramesBeforeResize{};
    };

    struct EditorFrameDebuggerSmokeState {
        bool captureRequested{};
        bool replayPassRequested{};
        bool previewRequested{};
        bool previewVisible{};
        bool resumeRequested{};
        bool renderedAfterResume{};
        std::uint64_t viewportFramesAtPause{};
        std::uint64_t viewportFramesAtPreview{};
        std::uint64_t viewportFramesAfterResume{};
        std::uint64_t inspectedWorldFramesAtPause{};
        std::uint64_t inspectedWorldFramesAtPreview{};
        std::uint64_t inspectedWorldFramesAfterResume{};
    };

    void requestSyntheticMultiViewSmoke(asharia::editor::EditorRunMode mode,
                                        asharia::editor::EditorViewportCoordinator& viewportHost) {
        if (mode != asharia::editor::EditorRunMode::SmokeViewport ||
            viewportHost.stats().multiViewFramesRecorded > 0) {
            return;
        }

        const asharia::editor::EditorViewportOverlayFlags allFlags{
            .gridVisible = true,
            .gizmoVisible = true,
            .wireVisible = true,
            .selectionOutlineVisible = true,
            .debugOverlayVisible = true,
            .debugGizmoVisible = true,
        };
        const asharia::editor::EditorExtent2D gameExtent{.width = 192, .height = 128};
        const asharia::editor::EditorExtent2D previewExtent{.width = 96, .height = 96};
        viewportHost.requestViewport(asharia::editor::EditorViewportRequest{
            .panelId = asharia::editor::EditorId{.value = "editor-smoke-game-view"},
            .kind = asharia::editor::EditorViewportKind::Game,
            .extent = gameExtent,
            .camera = asharia::editor::defaultEditorSceneViewCamera(gameExtent),
            .overlayFlags = allFlags,
            .refresh =
                asharia::editor::EditorViewportRefreshRequest{
                    .policy = asharia::editor::EditorViewportRefreshPolicy::OnDemand,
                },
        });
        viewportHost.requestViewport(asharia::editor::EditorViewportRequest{
            .panelId = asharia::editor::EditorId{.value = "editor-smoke-preview-view"},
            .kind = asharia::editor::EditorViewportKind::Preview,
            .extent = previewExtent,
            .camera = asharia::editor::defaultEditorSceneViewCamera(previewExtent),
            .overlayFlags = allFlags,
            .refresh =
                asharia::editor::EditorViewportRefreshRequest{
                    .policy = asharia::editor::EditorViewportRefreshPolicy::OnDemand,
                },
        });
    }

    void updateViewportResizeSmoke(asharia::GlfwWindow& window,
                                   const asharia::editor::EditorViewportCoordinator& viewportHost,
                                   EditorViewportResizeSmokeState& state) {
        if (!state.requested && viewportHost.hasPresentedViewportTexture()) {
            state.extentBeforeResize = viewportHost.descriptorExtent();
            state.textureFramesBeforeResize = viewportHost.textureFramesSubmitted();
            window.setSize(kResizeSmokeWindowWidth, kResizeSmokeWindowHeight);
            state.requested = true;
        }

        const VkExtent2D currentViewportExtent = viewportHost.descriptorExtent();
        if (state.requested && !state.presentedAfterResize &&
            isRenderableExtent(currentViewportExtent) &&
            differs(currentViewportExtent, state.extentBeforeResize) &&
            extentArea(currentViewportExtent) < extentArea(state.extentBeforeResize) &&
            viewportHost.textureFramesSubmitted() > state.textureFramesBeforeResize) {
            state.extentAfterResize = currentViewportExtent;
            state.presentedAfterResize = true;
        }
    }

    std::optional<std::size_t>
    chooseFrameDebugReplayPass(const asharia::editor::EditorFrameDebugger& frameDebugger) {
        const std::optional<asharia::editor::EditorFrameDebugCapture>& capture =
            frameDebugger.pausedCapture();
        if (!capture) {
            return std::nullopt;
        }

        std::optional<std::size_t> fallback;
        for (const asharia::RenderGraphDiagnosticsPassNode& pass :
             capture->diagnostics.renderGraph.passes) {
            if (!fallback) {
                fallback = pass.passIndex;
            }
            if (pass.name == "ClearFullscreenSource") {
                return pass.passIndex;
            }
        }
        return fallback;
    }

    std::optional<asharia::BasicRenderViewExecutionEventId>
    chooseFrameDebugReplayEvent(const asharia::editor::EditorFrameDebugger& frameDebugger) {
        const std::optional<asharia::editor::EditorFrameDebugCapture>& capture =
            frameDebugger.pausedCapture();
        if (!capture) {
            return std::nullopt;
        }

        std::optional<asharia::BasicRenderViewExecutionEventId> fallback;
        for (const asharia::BasicRenderViewExecutionEvent& event :
             capture->diagnostics.executionEvents) {
            if (event.kind == asharia::BasicRenderViewExecutionEventKind::BeginPass ||
                event.kind == asharia::BasicRenderViewExecutionEventKind::EndPass) {
                continue;
            }
            if (!fallback) {
                fallback = event.id;
            }
            if (event.passName == "ClearFullscreenSource" &&
                event.kind == asharia::BasicRenderViewExecutionEventKind::ClearColor) {
                return event.id;
            }
        }
        return fallback;
    }

    void updateFrameDebuggerSmoke(
        asharia::editor::EditorFrameDebugger& frameDebugger,
        asharia::editor::EditorActionRegistry& actionRegistry,
        asharia::editor::EditorContext& editorContext,
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        const asharia::editor::EditorInspectedWorldScheduler& inspectedWorldScheduler,
        EditorFrameDebuggerSmokeState& state) {
        if (!state.captureRequested) {
            state.captureRequested = actionRegistry.invoke("debug.capture-frame", editorContext);
            return;
        }

        if (!state.resumeRequested &&
            frameDebugger.state() == asharia::editor::EditorFrameDebuggerState::PausedFrameDebug) {
            if (!state.replayPassRequested) {
                state.viewportFramesAtPause = viewportHost.viewportFramesRendered();
                state.inspectedWorldFramesAtPause =
                    inspectedWorldScheduler.stats().frameAdvanceSafePoints;
                const std::optional<asharia::BasicRenderViewExecutionEventId> replayEvent =
                    chooseFrameDebugReplayEvent(frameDebugger);
                if (replayEvent) {
                    state.replayPassRequested = frameDebugger.selectReplayEvent(*replayEvent);
                    state.previewRequested = state.replayPassRequested;
                } else {
                    const std::optional<std::size_t> replayPass =
                        chooseFrameDebugReplayPass(frameDebugger);
                    if (replayPass) {
                        state.replayPassRequested = frameDebugger.selectReplayPass(*replayPass);
                        state.previewRequested = state.replayPassRequested;
                    }
                }
                return;
            }

            if (!state.previewRequested) {
                return;
            }

            const asharia::editor::EditorFrameDebugPreview& preview = frameDebugger.preview();
            const asharia::editor::EditorFrameDebuggerStats stats = frameDebugger.stats();
            if (!state.previewVisible) {
                if (preview.status == asharia::editor::EditorFrameDebugPreviewStatus::Available &&
                    asharia::editor::hasEditorViewportTexture(preview.texture) &&
                    stats.previewTextureFramesDrawn > 0) {
                    state.viewportFramesAtPreview = viewportHost.viewportFramesRendered();
                    state.inspectedWorldFramesAtPreview =
                        inspectedWorldScheduler.stats().frameAdvanceSafePoints;
                    state.previewVisible = true;
                }
                return;
            }

            state.resumeRequested = actionRegistry.invoke("debug.resume-frame", editorContext);
            return;
        }

        if (state.resumeRequested && !state.renderedAfterResume &&
            frameDebugger.state() == asharia::editor::EditorFrameDebuggerState::Running &&
            viewportHost.viewportFramesRendered() > state.viewportFramesAtPause) {
            state.viewportFramesAfterResume = viewportHost.viewportFramesRendered();
            state.inspectedWorldFramesAfterResume =
                inspectedWorldScheduler.stats().frameAdvanceSafePoints;
            state.renderedAfterResume = true;
        }
    }

    [[nodiscard]] bool validateViewportSmokePresentation(
        asharia::editor::EditorRunMode mode, const EditorSmokeRunResult& runResult,
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        const asharia::editor::ImGuiTextureRegistryStats& textureRegistryStats) {
        if (!isViewportSmokeMode(mode)) {
            return true;
        }
        if (!viewportHost.hasPresentedViewportTexture()) {
            asharia::logError("Editor viewport smoke did not present a sampled viewport texture.");
            return false;
        }
        if (viewportHost.textureFramesSubmitted() + 1U <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor viewport smoke dropped sampled texture presentation during "
                              "resize.");
            return false;
        }
        if (textureRegistryStats.peakLiveDescriptors >
            asharia::editor::kEditorViewportTextureDescriptorBudget) {
            asharia::logError("Editor viewport smoke exceeded ImGui texture descriptor budget.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateIdleSceneViewReuseSmoke(
        asharia::editor::EditorRunMode mode, const EditorSmokeRunResult& runResult,
        const asharia::editor::EditorViewportCoordinatorStats& viewportStats) {
        if (mode != asharia::editor::EditorRunMode::SmokeViewport) {
            return true;
        }
        if (viewportStats.idleSceneViewFramesSkipped == 0) {
            asharia::logError("Editor viewport smoke did not skip idle Scene View recording.");
            return false;
        }
        if (viewportStats.sceneViewDiagnosticsFramesRecorded >=
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor viewport smoke did not reuse the idle Scene View texture.");
            return false;
        }
        if (viewportStats.repaintReasonFramesRecorded == 0) {
            asharia::logError("Editor viewport smoke did not record a repaint-reason RenderView.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateSceneRenderViewDiagnosticsSmoke(
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        asharia::BasicRenderViewDiagnostics& scene) {
        const std::optional<asharia::editor::EditorRecordedRenderViewDiagnostics> sceneDiagnostics =
            viewportHost.latestRecordedRenderViewDiagnosticsForView(
                "scene-view", asharia::editor::EditorViewportKind::Scene);
        if (!sceneDiagnostics) {
            asharia::logError("Editor viewport smoke missed a keyed RenderView diagnostics "
                              "snapshot.");
            return false;
        }

        scene = sceneDiagnostics->diagnostics;
        if (scene.renderGraph.passes.size() != 3U || scene.renderGraph.resources.size() != 2U ||
            scene.renderGraph.accessEdges.size() != 4U ||
            scene.renderGraph.dependencyEdges.size() != 2U ||
            scene.renderGraph.transitions.size() != 4U || scene.executionEvents.empty()) {
            asharia::logError(
                "Editor viewport smoke recorded unexpected render view diagnostics counts: "
                "passes " +
                std::to_string(scene.renderGraph.passes.size()) + ", resources " +
                std::to_string(scene.renderGraph.resources.size()) + ", access edges " +
                std::to_string(scene.renderGraph.accessEdges.size()) + ", dependency edges " +
                std::to_string(scene.renderGraph.dependencyEdges.size()) + ", transitions " +
                std::to_string(scene.renderGraph.transitions.size()) + ", execution events " +
                std::to_string(scene.executionEvents.size()) + ".");
            return false;
        }
        if (renderGraphPassTypeCount(scene.renderGraph, asharia::kBasicRenderViewOverlayPassType) !=
                1U ||
            renderGraphOverlayCommandCount(scene.renderGraph) != 4U) {
            asharia::logError(
                "Editor viewport smoke did not record graph-visible RenderView overlay inputs.");
            return false;
        }
        if (scene.viewKind != asharia::BasicRenderViewKind::Scene ||
            scene.frameParams.frameIndex == 0 || !scene.overlay.enabled ||
            scene.overlay.debugWorldLineCount == 0) {
            asharia::logError(
                "Editor viewport smoke recorded invalid RenderView overlay prerequisites.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateSyntheticMultiViewDiagnosticsSmoke(
        asharia::editor::EditorRunMode mode,
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        const asharia::editor::EditorViewportCoordinatorStats& viewportStats) {
        if (mode != asharia::editor::EditorRunMode::SmokeViewport) {
            return true;
        }
        if (viewportStats.multiViewFramesRecorded == 0 ||
            viewportStats.viewportRequestsRecorded < 3 ||
            viewportStats.gameViewDiagnosticsFramesRecorded == 0 ||
            viewportStats.previewViewDiagnosticsFramesRecorded == 0) {
            asharia::logError(
                "Editor viewport smoke did not record multiple keyed RenderViews in one frame.");
            return false;
        }

        const std::optional<asharia::editor::EditorRecordedRenderViewDiagnostics> gameDiagnostics =
            viewportHost.latestRecordedRenderViewDiagnosticsForView(
                "editor-smoke-game-view", asharia::editor::EditorViewportKind::Game);
        const std::optional<asharia::editor::EditorRecordedRenderViewDiagnostics>
            previewDiagnostics = viewportHost.latestRecordedRenderViewDiagnosticsForView(
                "editor-smoke-preview-view", asharia::editor::EditorViewportKind::Preview);
        if (!gameDiagnostics || !previewDiagnostics) {
            asharia::logError(
                "Editor viewport smoke missed a keyed multi-view diagnostics snapshot.");
            return false;
        }

        const asharia::BasicRenderViewDiagnostics& game = gameDiagnostics->diagnostics;
        if (game.viewKind != asharia::BasicRenderViewKind::Game || !game.overlay.enabled ||
            game.overlay.debugWorldLineCount != 0 ||
            renderGraphPassTypeCount(game.renderGraph, asharia::kBasicRenderViewOverlayPassType) !=
                1U) {
            asharia::logError("Editor viewport smoke recorded invalid Game View diagnostics.");
            return false;
        }

        const asharia::BasicRenderViewDiagnostics& preview = previewDiagnostics->diagnostics;
        if (preview.viewKind != asharia::BasicRenderViewKind::Preview || preview.overlay.enabled ||
            renderGraphPassTypeCount(preview.renderGraph,
                                     asharia::kBasicRenderViewOverlayPassType) != 0U) {
            asharia::logError("Editor viewport smoke leaked overlay inputs into Preview View.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateViewportFlagsSmoke(
        asharia::editor::EditorRunMode mode, const EditorSmokeRunResult& runResult,
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        const asharia::editor::EditorViewportCoordinatorStats& viewportStats) {
        if (!isViewportSmokeMode(mode)) {
            return true;
        }

        const asharia::editor::EditorViewportOverlayFlags defaults =
            asharia::editor::defaultEditorSceneViewOverlayFlags();
        if (!defaults.gridVisible || !defaults.gizmoVisible || defaults.wireVisible ||
            !defaults.selectionOutlineVisible || defaults.debugOverlayVisible ||
            defaults.debugGizmoVisible) {
            asharia::logError("Editor viewport smoke found invalid default overlay flags.");
            return false;
        }

        const asharia::editor::EditorViewportOverlayFlags allFlags{
            .gridVisible = true,
            .gizmoVisible = true,
            .wireVisible = true,
            .selectionOutlineVisible = true,
            .debugOverlayVisible = true,
            .debugGizmoVisible = true,
        };
        if (!sameViewportOverlayFlags(asharia::editor::effectiveEditorViewportOverlayFlags(
                                          asharia::editor::EditorViewportKind::Scene, allFlags),
                                      allFlags)) {
            asharia::logError("Editor viewport smoke dropped Scene View overlay flags.");
            return false;
        }
        const asharia::editor::EditorViewportOverlayFlags gameFlags =
            asharia::editor::effectiveEditorViewportOverlayFlags(
                asharia::editor::EditorViewportKind::Game, allFlags);
        if (gameFlags.gridVisible || gameFlags.gizmoVisible || gameFlags.wireVisible ||
            gameFlags.selectionOutlineVisible || !gameFlags.debugOverlayVisible ||
            !gameFlags.debugGizmoVisible) {
            asharia::logError(
                "Editor viewport smoke filtered Game View overlay flags incorrectly.");
            return false;
        }
        if (asharia::editor::anyEditorViewportOverlayFlagEnabled(
                asharia::editor::effectiveEditorViewportOverlayFlags(
                    asharia::editor::EditorViewportKind::Preview, allFlags))) {
            asharia::logError("Editor viewport smoke leaked overlay flags into Preview views.");
            return false;
        }
        const asharia::editor::EditorViewportOverlayPacketList scenePackets =
            asharia::editor::collectBuiltInEditorViewportOverlayPackets(
                asharia::editor::EditorViewportOverlayProviderContext{
                    .viewportKind = asharia::editor::EditorViewportKind::Scene,
                    .camera = asharia::editor::defaultEditorSceneViewCamera(
                        asharia::editor::EditorExtent2D{.width = 320, .height = 240}),
                    .overlayFlags = defaults,
                });
        const asharia::editor::EditorViewportOverlayPacketList gamePackets =
            asharia::editor::collectBuiltInEditorViewportOverlayPackets(
                asharia::editor::EditorViewportOverlayProviderContext{
                    .viewportKind = asharia::editor::EditorViewportKind::Game,
                    .camera = asharia::editor::defaultEditorSceneViewCamera(
                        asharia::editor::EditorExtent2D{.width = 320, .height = 240}),
                    .overlayFlags = gameFlags,
                });
        if (scenePackets.packets.size() != 1U ||
            scenePackets.packets.front().overlayId != "scene.grid" ||
            scenePackets.packets.front().viewportKind !=
                asharia::editor::EditorViewportKind::Scene ||
            scenePackets.debugWorldLineCount() == 0 || !gamePackets.packets.empty()) {
            asharia::logError("Editor viewport smoke detected invalid overlay provider packets.");
            return false;
        }
        if (viewportStats.overlayFlagFramesRendered == 0) {
            asharia::logError("Editor viewport smoke did not render a Scene View flagged frame.");
            return false;
        }
        if (viewportStats.overlayFlagTextureFramesAcquired == 0) {
            asharia::logError(
                "Editor viewport smoke did not acquire a Scene View flagged texture.");
            return false;
        }
        if (mode == asharia::editor::EditorRunMode::SmokeViewport) {
            if (viewportStats.sceneViewOnlyFlagRequestsDiscarded == 0) {
                asharia::logError("Editor viewport smoke did not exercise Game View overlay "
                                  "filtering through the coordinator.");
                return false;
            }
        } else if (viewportStats.sceneViewOnlyFlagRequestsDiscarded != 0) {
            asharia::logError(
                "Editor viewport smoke discarded Scene View-only flags unexpectedly.");
            return false;
        }
        if (viewportStats.renderViewDiagnosticsFramesRecorded == 0) {
            asharia::logError("Editor viewport smoke did not record render view diagnostics.");
            return false;
        }
        if (viewportStats.liveRenderGraphSnapshotFrames == 0) {
            asharia::logError("Editor viewport smoke did not draw a live RG View snapshot.");
            return false;
        }
        if (!validateIdleSceneViewReuseSmoke(mode, runResult, viewportStats)) {
            return false;
        }

        asharia::BasicRenderViewDiagnostics scene;
        if (!validateSceneRenderViewDiagnosticsSmoke(viewportHost, scene) ||
            !validateSyntheticMultiViewDiagnosticsSmoke(mode, viewportHost, viewportStats)) {
            return false;
        }
        if (!validateEditorViewportCameraSmoke()) {
            return false;
        }
        return validateRenderViewCameraSmoke(scene);
    }

    [[nodiscard]] bool validateViewportResizeSmoke(
        asharia::editor::EditorRunMode mode, const EditorSmokeRunResult& runResult,
        const asharia::editor::EditorViewportCoordinatorStats& viewportStats,
        const asharia::editor::ImGuiTextureRegistryStats& textureRegistryStats) {
        if (!isViewportResizeSmokeMode(mode)) {
            return true;
        }
        if (!runResult.resizeRequested || !runResult.resizedViewportPresented) {
            asharia::logError(
                "Editor viewport resize smoke did not present a resized viewport texture.");
            return false;
        }
        if (textureRegistryStats.descriptorsCreated < 2 ||
            textureRegistryStats.descriptorsRetired == 0) {
            asharia::logError(
                "Editor viewport resize smoke did not retire an ImGui texture descriptor.");
            return false;
        }
        if (viewportStats.renderTargetsRetired == 0 || viewportStats.renderTargetsDeferred == 0) {
            asharia::logError("Editor viewport resize smoke did not defer retired viewport "
                              "render target destruction.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool
    validateFrameDebuggerSmoke(asharia::editor::EditorRunMode mode,
                               const EditorSmokeRunResult& runResult,
                               const asharia::editor::EditorFrameDebugger& frameDebugger) {
        if (!isFrameDebuggerSmokeMode(mode)) {
            return true;
        }

        if (!runResult.frameDebugCaptureRequested || !runResult.frameDebugReplayPassRequested ||
            !runResult.frameDebugPreviewRequested || !runResult.frameDebugPreviewVisible ||
            !runResult.frameDebugResumeRequested || !runResult.frameDebugRenderedAfterResume) {
            asharia::logError(
                "Editor frame debugger smoke did not complete capture/preview/resume flow.");
            return false;
        }
        if (frameDebugger.state() != asharia::editor::EditorFrameDebuggerState::Running) {
            asharia::logError("Editor frame debugger smoke did not return to Running state.");
            return false;
        }

        const asharia::editor::EditorFrameDebuggerStats stats = frameDebugger.stats();
        if (stats.captureRequests != 1 || stats.framesCaptured != 1 ||
            stats.completedCaptures != 1 || stats.resumeRequests != 1 || stats.framesResumed != 1 ||
            stats.renderViewFramesSkipped == 0 || stats.previewRequests == 0 ||
            stats.previewFramesRecorded == 0 || stats.previewTextureFramesPublished == 0 ||
            stats.previewTextureFramesDrawn == 0 || stats.replayPassRequests == 0 ||
            stats.replayPassSelections == 0 || stats.replayEventRequests == 0 ||
            stats.replayEventSelections == 0 || stats.frameDebugRenderGraphViewFrames == 0 ||
            stats.frameDebugRenderGraphSnapshotFrames == 0) {
            asharia::logError("Editor frame debugger smoke recorded unexpected state counts.");
            return false;
        }
        if (runResult.viewportFramesAtFrameDebugPreview !=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke recorded a normal RenderView while previewing.");
            return false;
        }
        if (runResult.inspectedWorldFramesAtFrameDebugPreview !=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke advanced inspected-world safe points "
                              "while previewing.");
            return false;
        }
        if (runResult.viewportFramesAfterFrameDebugResume <=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke did not resume RenderView recording.");
            return false;
        }
        if (runResult.inspectedWorldFramesAfterFrameDebugResume <=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke did not resume inspected-world safe points.");
            return false;
        }
        const asharia::editor::EditorInspectedWorldSchedulerStats& inspectedWorldStats =
            runResult.inspectedWorldStats;
        if (inspectedWorldStats.frameAdvanceSafePoints == 0 ||
            inspectedWorldStats.gameUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.scriptUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.skippedFrameAdvanceSafePoints == 0 ||
            inspectedWorldStats.skippedGameUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints ||
            inspectedWorldStats.skippedScriptUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints) {
            asharia::logError(
                "Editor frame debugger smoke recorded invalid inspected-world safe-point counts.");
            return false;
        }
        const std::optional<asharia::editor::EditorFrameDebugCapture>& capture =
            frameDebugger.latestCapture();
        if (!capture) {
            asharia::logError("Editor frame debugger smoke did not keep a captured snapshot.");
            return false;
        }
        if (capture->diagnostics.renderGraph.passes.size() != 3 ||
            capture->diagnostics.renderGraph.resources.size() != 2 ||
            capture->diagnostics.renderGraph.accessEdges.size() != 4 ||
            capture->diagnostics.renderGraph.dependencyEdges.size() != 2 ||
            capture->diagnostics.renderGraph.transitions.size() != 4) {
            asharia::logError(
                "Editor frame debugger smoke captured unexpected RenderGraph diagnostics: passes " +
                std::to_string(capture->diagnostics.renderGraph.passes.size()) + ", resources " +
                std::to_string(capture->diagnostics.renderGraph.resources.size()) +
                ", access edges " +
                std::to_string(capture->diagnostics.renderGraph.accessEdges.size()) +
                ", dependency edges " +
                std::to_string(capture->diagnostics.renderGraph.dependencyEdges.size()) +
                ", transitions " +
                std::to_string(capture->diagnostics.renderGraph.transitions.size()) + ".");
            return false;
        }
        if (capture->diagnostics.executionEvents.empty()) {
            asharia::logError("Editor frame debugger smoke captured no renderer execution events.");
            return false;
        }
        if (frameDebugger.pausedCapture()) {
            asharia::logError("Editor frame debugger smoke kept a paused capture after resume.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateInputRouterSmoke(asharia::editor::EditorRunMode mode,
                                                const EditorSmokeRunResult& runResult) {
        if (!isSmokeMode(mode)) {
            return true;
        }
        if (runResult.inputStats.capturedFrames <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor input router smoke did not capture every rendered frame.");
            return false;
        }
        if (runResult.inputStats.sceneViewReports == 0) {
            asharia::logError("Editor input router smoke did not receive Scene View input state.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateShortcutRouterRunSmoke(asharia::editor::EditorRunMode mode,
                                                      const EditorSmokeRunResult& runResult) {
        if (!isSmokeMode(mode)) {
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

    [[nodiscard]] bool
    validateImguiLayoutPersistenceSmoke(asharia::editor::EditorRunMode mode,
                                        const asharia::editor::ImGuiRuntime& imgui) {
        if (!isSmokeMode(mode)) {
            return true;
        }
        if (!imgui.layoutPersistenceEnabled()) {
            asharia::logError("Editor ImGui layout persistence is disabled.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateImguiLayoutSavedSmoke(asharia::editor::EditorRunMode mode,
                                                     const asharia::editor::ImGuiRuntime& imgui) {
        if (!isSmokeMode(mode)) {
            return true;
        }
        if (!std::filesystem::exists(imgui.layoutIniPath())) {
            asharia::logError("Editor ImGui layout smoke did not write the layout ini file.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateI18nSmoke(asharia::editor::EditorRunMode mode) {
        if (!isSmokeMode(mode)) {
            return true;
        }

        if (asharia::editor::editorI18nCatalog().empty()) {
            asharia::logError("Editor i18n catalog is empty.");
            return false;
        }

        const asharia::editor::EditorI18n enUs{asharia::editor::EditorLocale::EnUs};
        const asharia::editor::EditorI18n zhHans{asharia::editor::EditorLocale::ZhHans};
        const std::string_view enFile = enUs.text("menu.file");
        const std::string_view zhFile = zhHans.text("menu.file");
        if (enFile != "File" || zhFile.empty() || zhFile == enFile) {
            asharia::logError("Editor i18n smoke failed locale text lookup.");
            return false;
        }
        if (zhHans.text(asharia::editor::EditorI18nTextQuery{
                .key = "missing.editor.key",
                .fallback = "Fallback",
            }) != "Fallback") {
            asharia::logError("Editor i18n smoke failed missing-key fallback.");
            return false;
        }

        const std::string captureLabel = zhHans.label(asharia::editor::EditorI18nLabelDesc{
            .key = "action.debug.captureFrame",
            .stableId = "debug.capture-frame",
            .fallback = "Capture Frame",
        });
        if (!captureLabel.ends_with("###debug.capture-frame")) {
            asharia::logError("Editor i18n smoke failed stable ImGui label id generation.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validateEditorFontSmoke(asharia::editor::EditorRunMode mode,
                                               const asharia::editor::ImGuiRuntime& imgui,
                                               asharia::editor::EditorLocale locale) {
        if (!isSmokeMode(mode) || locale != asharia::editor::EditorLocale::ZhHans) {
            return true;
        }

        const asharia::editor::ImGuiRuntimeFontStatus& status = imgui.fontStatus();
        if (!status.cjkRequested) {
            asharia::logError("Editor zh-Hans smoke did not request CJK glyph coverage.");
            return false;
        }
        if (status.cjkFontPathExplicit && !status.cjkLoaded) {
            asharia::logError("Editor zh-Hans smoke failed to load the explicit CJK font path.");
            return false;
        }
        if (status.cjkCandidateFound && !status.cjkLoaded) {
            asharia::logError(
                "Editor zh-Hans smoke found a CJK font candidate but did not load it.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validateEditorThemeSmoke(asharia::editor::EditorRunMode mode,
                                                asharia::editor::EditorUiThemeId expectedTheme) {
        if (!isSmokeMode(mode)) {
            return true;
        }

        const std::span<const asharia::editor::EditorUiTheme> themes =
            asharia::editor::editorUiThemes();
        if (themes.size() != 7U) {
            asharia::logError("Editor theme smoke found an unexpected theme count.");
            return false;
        }
        if (asharia::editor::defaultEditorUiThemeId() !=
                asharia::editor::EditorUiThemeId::ClassicBlueGray ||
            asharia::editor::editorUiThemeName(asharia::editor::defaultEditorUiThemeId()) !=
                "classic-blue-gray-2") {
            asharia::logError("Editor theme smoke found an invalid default theme.");
            return false;
        }
        const asharia::editor::EditorUiTheme& classicTheme =
            asharia::editor::editorUiTheme(asharia::editor::EditorUiThemeId::ClassicBlueGray);
        const bool classicAppBgMatches =
            classicTheme.appBackground.r == 0x17U && classicTheme.appBackground.g == 0x1DU &&
            classicTheme.appBackground.b == 0x24U && classicTheme.appBackground.a == 0xFFU;
        const bool classicAccentMatches =
            classicTheme.accent.r == 0x72U && classicTheme.accent.g == 0xB7U &&
            classicTheme.accent.b == 0xE8U && classicTheme.accent.a == 0xFFU;
        if (!classicAppBgMatches || !classicAccentMatches ||
            asharia::editor::toImGuiEncodedSrgbU32(classicTheme.appBackground) !=
                IM_COL32(0x17U, 0x1DU, 0x24U, 0xFFU)) {
            asharia::logError("Editor theme smoke found an invalid encoded sRGB theme byte.");
            return false;
        }
        for (std::size_t index = 0; index < themes.size(); ++index) {
            if (themes[index].storageName.empty() || themes[index].name.empty()) {
                asharia::logError("Editor theme smoke found an unnamed theme.");
                return false;
            }
            for (std::size_t otherIndex = index + 1U; otherIndex < themes.size(); ++otherIndex) {
                if (themes[index].storageName == themes[otherIndex].storageName) {
                    asharia::logError("Editor theme smoke found duplicate theme storage names.");
                    return false;
                }
            }
        }
        const std::optional<asharia::editor::EditorUiThemeId> defaultTheme =
            asharia::editor::editorUiThemeIdFromName("classic-blue-gray-2");
        if (!defaultTheme || *defaultTheme != asharia::editor::EditorUiThemeId::ClassicBlueGray) {
            asharia::logError("Editor theme smoke could not resolve the default theme name.");
            return false;
        }
        const std::optional<asharia::editor::EditorUiThemeId> legacyDefaultTheme =
            asharia::editor::editorUiThemeIdFromName("classic-blue-gray");
        if (!legacyDefaultTheme ||
            *legacyDefaultTheme != asharia::editor::EditorUiThemeId::ClassicBlueGray) {
            asharia::logError(
                "Editor theme smoke could not resolve the legacy default theme name.");
            return false;
        }
        if (asharia::editor::currentEditorUiThemeId() != expectedTheme) {
            asharia::logError("Editor theme smoke did not apply the startup theme.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateEditorStartupSmoke(asharia::editor::EditorRunMode mode,
                                                  const asharia::editor::ImGuiRuntime& imgui,
                                                  asharia::editor::EditorLocale locale,
                                                  asharia::editor::EditorUiThemeId theme) {
        return validateImguiLayoutPersistenceSmoke(mode, imgui) && validateI18nSmoke(mode) &&
               validateEditorFontSmoke(mode, imgui, locale) &&
               validateEditorThemeSmoke(mode, theme);
    }

    [[nodiscard]] asharia::editor::EditorLocale
    alternateLocale(asharia::editor::EditorLocale locale) {
        return locale == asharia::editor::EditorLocale::ZhHans
                   ? asharia::editor::EditorLocale::EnUs
                   : asharia::editor::EditorLocale::ZhHans;
    }

    [[nodiscard]] asharia::editor::EditorUiThemeId
    alternateTheme(asharia::editor::EditorUiThemeId theme) {
        return theme == asharia::editor::EditorUiThemeId::WarmGraphiteAmber
                   ? asharia::editor::EditorUiThemeId::ClassicBlueGray
                   : asharia::editor::EditorUiThemeId::WarmGraphiteAmber;
    }

    [[nodiscard]] bool validateEditorSettingsSmoke(asharia::editor::EditorRunMode mode,
                                                   asharia::editor::EditorContext& editorContext) {
        if (!isSmokeMode(mode)) {
            return true;
        }

        asharia::editor::EditorSettingsController& settings = editorContext.settings();
        const asharia::editor::EditorLocale initialLocale = settings.settings().locale;
        const asharia::editor::EditorUiThemeId initialTheme = settings.settings().theme;
        const asharia::editor::EditorLocale changedLocale = alternateLocale(initialLocale);
        if (auto changed = settings.setLocale(changedLocale); !changed) {
            asharia::logError(changed.error().message);
            return false;
        }
        if (settings.settings().locale != changedLocale ||
            editorContext.i18n().locale() != changedLocale) {
            asharia::logError("Editor settings smoke did not apply the selected locale.");
            return false;
        }

        auto loaded = asharia::editor::loadEditorSettings(settings.settingsPath(), initialLocale);
        if (!loaded) {
            asharia::logError(loaded.error().message);
            return false;
        }
        if (loaded->locale != changedLocale) {
            asharia::logError("Editor settings smoke did not persist the selected locale.");
            return false;
        }

        const asharia::editor::EditorUiThemeId changedTheme = alternateTheme(initialTheme);
        if (auto changed = settings.setTheme(changedTheme); !changed) {
            asharia::logError(changed.error().message);
            return false;
        }
        if (settings.settings().theme != changedTheme ||
            asharia::editor::currentEditorUiThemeId() != changedTheme) {
            asharia::logError("Editor settings smoke did not apply the selected theme.");
            return false;
        }

        loaded = asharia::editor::loadEditorSettings(settings.settingsPath(), initialLocale);
        if (!loaded) {
            asharia::logError(loaded.error().message);
            return false;
        }
        if (loaded->theme != changedTheme) {
            asharia::logError("Editor settings smoke did not persist the selected theme.");
            return false;
        }

        if (auto restored = settings.setLocale(initialLocale); !restored) {
            asharia::logError(restored.error().message);
            return false;
        }
        if (settings.settings().locale != initialLocale ||
            editorContext.i18n().locale() != initialLocale) {
            asharia::logError("Editor settings smoke did not restore the initial locale.");
            return false;
        }
        if (auto restored = settings.setTheme(initialTheme); !restored) {
            asharia::logError(restored.error().message);
            return false;
        }
        if (settings.settings().theme != initialTheme ||
            asharia::editor::currentEditorUiThemeId() != initialTheme) {
            asharia::logError("Editor settings smoke did not restore the initial theme.");
            return false;
        }

        return true;
    }

    VkImageMemoryBarrier2 imageBarrier(const ImageBarrierDesc& desc) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = desc.srcStageMask;
        barrier.srcAccessMask = desc.srcAccessMask;
        barrier.dstStageMask = desc.dstStageMask;
        barrier.dstAccessMask = desc.dstAccessMask;
        barrier.oldLayout = desc.oldLayout;
        barrier.newLayout = desc.newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = desc.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

    void cmdPipelineBarrier(VkCommandBuffer commandBuffer, const VkImageMemoryBarrier2& barrier) {
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void buildEditorShell(asharia::editor::EditorActionRegistry& actionRegistry,
                          asharia::editor::EditorContext& editorContext,
                          asharia::editor::EditorPanelRegistry& panelRegistry,
                          asharia::editor::EditorFrameContext& frameContext) {
        asharia::editor::drawEditorMainMenu(actionRegistry, editorContext);
        asharia::editor::drawEditorCommandBar(actionRegistry, editorContext);
        asharia::editor::drawEditorStatusBar(frameContext, editorContext);
        asharia::editor::drawEditorDockspace(editorContext);
        panelRegistry.drawPanels(frameContext);
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordEditorImguiFrame(const asharia::VulkanFrameRecordContext& context) {
        const VkImageMemoryBarrier2 colorBarrier = imageBarrier(ImageBarrierDesc{
            .image = context.image,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        });
        cmdPipelineBarrier(context.commandBuffer, colorBarrier);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = context.imageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = context.clearColor;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = context.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(context.commandBuffer, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), context.commandBuffer);
        vkCmdEndRendering(context.commandBuffer);

        const VkImageMemoryBarrier2 presentBarrier = imageBarrier(ImageBarrierDesc{
            .image = context.image,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = 0,
        });
        cmdPipelineBarrier(context.commandBuffer, presentBarrier);

        return asharia::VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    }

    [[nodiscard]] asharia::VoidResult waitForRenderableWindow(asharia::GlfwWindow& window,
                                                              bool smokeMode) {
        int attempts = 0;
        auto framebuffer = window.framebufferExtent();
        while (!window.shouldClose() && !isRenderableExtent(framebuffer)) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{
                    asharia::vulkanError("Timed out waiting for a renderable editor framebuffer")};
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            asharia::GlfwWindow::pollEvents();
            framebuffer = window.framebufferExtent();
        }

        return {};
    }

    [[nodiscard]] asharia::Result<asharia::VulkanContext>
    createEditorContext(const std::vector<std::string>& extensions, asharia::GlfwWindow& window) {
        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Editor",
            .requiredInstanceExtensions = extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(window, instance);
                },
            .debugLabels = kEditorDebugLabels,
        };

        return asharia::VulkanContext::create(contextDesc);
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameLoop>
    createEditorFrameLoop(const asharia::VulkanContext& context,
                          const asharia::GlfwWindow& window) {
        const auto framebuffer = window.framebufferExtent();
        return asharia::VulkanFrameLoop::create(
            context, asharia::VulkanFrameLoopDesc{
                         .width = framebuffer.width,
                         .height = framebuffer.height,
                         .clearColor = VkClearColorValue{{0.015F, 0.018F, 0.022F, 1.0F}},
                     });
    }

    [[nodiscard]] asharia::Result<bool>
    prepareFrameLoopExtent(asharia::GlfwWindow& window, asharia::VulkanFrameLoop& frameLoop) {
        const auto currentFramebuffer = window.framebufferExtent();
        frameLoop.setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
        if (!isRenderableExtent(currentFramebuffer)) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            return false;
        }

        if (!extentMatches(frameLoop.extent(), currentFramebuffer)) {
            auto recreated = frameLoop.recreate();
            if (!recreated) {
                return std::unexpected{std::move(recreated.error())};
            }
            if (*recreated == asharia::VulkanFrameStatus::OutOfDate) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] asharia::Result<bool>
    renderEditorFrame(asharia::VulkanFrameLoop& frameLoop,
                      asharia::BasicFullscreenTextureRenderer& renderer,
                      asharia::editor::EditorViewportCoordinator& viewportHost,
                      asharia::editor::EditorFrameDebugger& frameDebugger, int frameIndex) {
        auto status =
            frameLoop.renderFrame([&renderer, &viewportHost, &frameDebugger,
                                   frameIndex](const asharia::VulkanFrameRecordContext& context)
                                      -> asharia::Result<asharia::VulkanFrameRecordResult> {
                const bool recordRenderViews = frameDebugger.shouldRecordRenderViews();
                asharia::editor::EditorViewportRepaintReasons repaintReasons =
                    frameDebugger.consumeRenderViewRepaintReasons();
                if (frameDebugger.isCapturingFrame()) {
                    asharia::editor::addEditorViewportRepaintReason(
                        repaintReasons,
                        asharia::editor::EditorViewportRepaintReason::FrameDebugEventChanged);
                }
                auto viewport = viewportHost.recordRequestedViews(
                    context, renderer, recordRenderViews, repaintReasons);
                if (!viewport) {
                    return std::unexpected{std::move(viewport.error())};
                }
                auto frameDebugPreview =
                    viewportHost.recordFrameDebugPreview(context, renderer, frameDebugger);
                if (!frameDebugPreview) {
                    return std::unexpected{std::move(frameDebugPreview.error())};
                }
                if (!recordRenderViews) {
                    frameDebugger.notifyRenderViewSkipped();
                } else if (frameDebugger.isCapturingFrame()) {
                    const auto& diagnostics = viewportHost.latestRecordedRenderViewDiagnostics();
                    if (diagnostics) {
                        frameDebugger.captureRecordedView(
                            asharia::editor::EditorFrameDebugCaptureDesc{
                                .frameIndex = frameIndex,
                                .submittedFrameEpoch = diagnostics->submittedFrameEpoch,
                                .viewKind = diagnostics->kind,
                                .requestedExtent = diagnostics->requestedExtent,
                                .diagnostics = diagnostics->diagnostics,
                            });
                    }
                }

                return recordEditorImguiFrame(context);
            });
        if (!status) {
            return std::unexpected{std::move(status.error())};
        }

        return *status != asharia::VulkanFrameStatus::OutOfDate;
    }

    [[nodiscard]] asharia::Result<EditorSmokeRunResult>
    runEditorLoop(asharia::GlfwWindow& window, asharia::VulkanFrameLoop& frameLoop,
                  asharia::BasicFullscreenTextureRenderer& renderer,
                  asharia::editor::EditorViewportCoordinator& viewportHost,
                  asharia::editor::EditorFrameDebugger& frameDebugger,
                  asharia::editor::EditorActionRegistry& actionRegistry,
                  asharia::editor::EditorContext& editorContext,
                  asharia::editor::EditorPanelRegistry& panelRegistry,
                  asharia::editor::EditorRunMode mode) {
        const bool smokeMode = isSmokeMode(mode);
        EditorViewportResizeSmokeState resizeSmoke;
        EditorFrameDebuggerSmokeState frameDebugSmoke;
        asharia::editor::EditorInspectedWorldScheduler inspectedWorldScheduler;
        asharia::editor::EditorInputRouter inputRouter;
        asharia::editor::EditorShortcutRouter shortcutRouter;
        int renderedFrames = 0;
        int attempts = 0;
        while (!window.shouldClose()) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{asharia::vulkanError(
                    "Editor shell smoke timed out before rendering enough frames")};
            }

            asharia::GlfwWindow::pollEvents();
            auto extentReady = prepareFrameLoopExtent(window, frameLoop);
            if (!extentReady) {
                return std::unexpected{std::move(extentReady.error())};
            }
            if (!*extentReady) {
                continue;
            }

            if (isFrameDebuggerSmokeMode(mode)) {
                updateFrameDebuggerSmoke(frameDebugger, actionRegistry, editorContext, viewportHost,
                                         inspectedWorldScheduler, frameDebugSmoke);
            }
            frameDebugger.beginFrame(renderedFrames);
            inspectedWorldScheduler.runFrameSafePoints(
                frameDebugger.shouldRunInspectedWorldSafePoints());
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const ImGuiIO& imguiIo = ImGui::GetIO();
            inputRouter.beginFrame(asharia::editor::EditorInputCapture{
                .imguiWantsMouse = imguiIo.WantCaptureMouse,
                .imguiWantsKeyboard = imguiIo.WantCaptureKeyboard,
                .imguiWantsTextInput = imguiIo.WantTextInput,
            });
            viewportHost.beginImguiFrame(asharia::editor::editorViewportFrameEpochs(frameLoop));
            panelRegistry.clearLifecycleEvents();
            editorContext.eventQueue().clear();
            asharia::editor::EditorFrameContext frameContext{
                .frameIndex = renderedFrames,
                .swapchainExtent = editorExtentFromVk(frameLoop.extent()),
                .smokeMode = smokeMode,
                .eventQueue = editorContext.eventQueue(),
                .diagnosticsLog = editorContext.diagnosticsLog(),
                .frameDebugger = frameDebugger,
                .i18n = editorContext.i18n(),
                .settings = editorContext.settings(),
                .tools = editorContext.tools(),
                .inputRouter = inputRouter,
                .renderGraphSnapshots = viewportHost,
                .viewportHost = viewportHost,
            };
            buildEditorShell(actionRegistry, editorContext, panelRegistry, frameContext);
            requestSyntheticMultiViewSmoke(mode, viewportHost);
            inputRouter.finalizeFrame();
            shortcutRouter.beginFrame(inputRouter.snapshot());
            static_cast<void>(shortcutRouter.routeImGuiShortcuts(actionRegistry, editorContext));
            ImGui::Render();

            auto rendered =
                renderEditorFrame(frameLoop, renderer, viewportHost, frameDebugger, renderedFrames);
            if (!rendered) {
                return std::unexpected{std::move(rendered.error())};
            }
            if (*rendered) {
                ++renderedFrames;
            }
            frameDebugger.endSubmittedFrame(frameLoop.completedFrameEpoch());
            if (isViewportResizeSmokeMode(mode)) {
                updateViewportResizeSmoke(window, viewportHost, resizeSmoke);
            }
            if (isFrameDebuggerSmokeMode(mode)) {
                updateFrameDebuggerSmoke(frameDebugger, actionRegistry, editorContext, viewportHost,
                                         inspectedWorldScheduler, frameDebugSmoke);
            }
            editorContext.diagnosticsLog().appendEvents(editorContext.eventQueue().events());
            editorContext.eventQueue().clear();
            panelRegistry.clearLifecycleEvents();

            if (smokeMode && renderedFrames >= smokeFrameCount(mode)) {
                break;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(smokeMode ? 16ms : 1ms);
        }

        return EditorSmokeRunResult{
            .renderedFrames = renderedFrames,
            .resizeRequested = resizeSmoke.requested,
            .resizedViewportPresented = resizeSmoke.presentedAfterResize,
            .frameDebugCaptureRequested = frameDebugSmoke.captureRequested,
            .frameDebugReplayPassRequested = frameDebugSmoke.replayPassRequested,
            .frameDebugPreviewRequested = frameDebugSmoke.previewRequested,
            .frameDebugPreviewVisible = frameDebugSmoke.previewVisible,
            .frameDebugResumeRequested = frameDebugSmoke.resumeRequested,
            .frameDebugRenderedAfterResume = frameDebugSmoke.renderedAfterResume,
            .viewportExtentBeforeResize = resizeSmoke.extentBeforeResize,
            .viewportExtentAfterResize = resizeSmoke.extentAfterResize,
            .textureFramesBeforeResize = resizeSmoke.textureFramesBeforeResize,
            .viewportFramesAtFrameDebugPause = frameDebugSmoke.viewportFramesAtPause,
            .viewportFramesAtFrameDebugPreview = frameDebugSmoke.viewportFramesAtPreview,
            .viewportFramesAfterFrameDebugResume = frameDebugSmoke.viewportFramesAfterResume,
            .inspectedWorldFramesAtFrameDebugPause = frameDebugSmoke.inspectedWorldFramesAtPause,
            .inspectedWorldFramesAtFrameDebugPreview =
                frameDebugSmoke.inspectedWorldFramesAtPreview,
            .inspectedWorldFramesAfterFrameDebugResume =
                frameDebugSmoke.inspectedWorldFramesAfterResume,
            .inspectedWorldStats = inspectedWorldScheduler.stats(),
            .inputStats = inputRouter.stats(),
            .shortcutStats = shortcutRouter.stats(),
        };
    }

    [[nodiscard]] bool
    validatePanelRegistrySmoke(asharia::editor::EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedPanelCount = 6;
        constexpr std::size_t kExpectedOpenPanelCount = 4;

        if (!panelRegistry.closePanel("log") || !panelRegistry.focusPanel("log")) {
            asharia::logError("Editor panel registry smoke could not close and reopen Log panel.");
            return false;
        }
        if (panelRegistry.panelCount() != kExpectedPanelCount ||
            panelRegistry.openPanelCount() != kExpectedOpenPanelCount ||
            !panelRegistry.isOpen("log") || panelRegistry.isOpen("ui-style-preview") ||
            panelRegistry.isOpen("editor-settings")) {
            asharia::logError(
                "Editor panel registry smoke detected invalid singleton panel state.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    validateActionRegistrySmoke(asharia::editor::EditorActionRegistry& actionRegistry,
                                asharia::editor::EditorContext& editorContext,
                                asharia::editor::EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedActionCount = 12;
        constexpr std::size_t kExpectedEnabledActionCount = 9;

        if (actionRegistry.actionCount() != kExpectedActionCount ||
            actionRegistry.enabledActionCount() != kExpectedEnabledActionCount) {
            asharia::logError("Editor action registry smoke detected invalid action counts.");
            return false;
        }
        editorContext.eventQueue().clear();
        if (actionRegistry.invoke("file.open", editorContext) ||
            actionRegistry.invokeCount("file.open") != 0 || !editorContext.eventQueue().empty()) {
            asharia::logError("Editor action registry smoke invoked a disabled action.");
            return false;
        }
        if (!panelRegistry.closePanel("log") || !actionRegistry.invoke("view.log", editorContext) ||
            !panelRegistry.isOpen("log") || actionRegistry.invokeCount("view.log") != 1) {
            asharia::logError("Editor action registry smoke failed to route View/Log action.");
            return false;
        }
        const std::span<const asharia::editor::EditorEvent> events =
            editorContext.eventQueue().events();
        const bool closedLog =
            std::ranges::any_of(events, [](const asharia::editor::EditorEvent& event) {
                return event.kind == asharia::editor::EditorEventKind::PanelClosed &&
                       event.sourceId.value == "log";
            });
        const bool invokedLogAction =
            std::ranges::any_of(events, [](const asharia::editor::EditorEvent& event) {
                return event.kind == asharia::editor::EditorEventKind::ActionInvoked &&
                       event.sourceId.value == "view.log";
            });
        const bool openedLog =
            std::ranges::any_of(events, [](const asharia::editor::EditorEvent& event) {
                return event.kind == asharia::editor::EditorEventKind::PanelOpened &&
                       event.sourceId.value == "log";
            });
        if (!closedLog || !invokedLogAction || !openedLog) {
            asharia::logError("Editor event queue smoke missed action or panel lifecycle events.");
            return false;
        }
        editorContext.eventQueue().clear();

        if (!actionRegistry.invoke("view.ui-style-preview", editorContext) ||
            !panelRegistry.isOpen("ui-style-preview") ||
            actionRegistry.invokeCount("view.ui-style-preview") != 1 ||
            !panelRegistry.closePanel("ui-style-preview")) {
            asharia::logError("Editor action registry smoke failed to route UI Style Preview.");
            return false;
        }
        editorContext.eventQueue().clear();

        if (!actionRegistry.invoke("view.editor-settings", editorContext) ||
            !panelRegistry.isOpen("editor-settings") ||
            actionRegistry.invokeCount("view.editor-settings") != 1 ||
            !panelRegistry.closePanel("editor-settings")) {
            asharia::logError("Editor action registry smoke failed to route Editor Settings.");
            return false;
        }
        editorContext.eventQueue().clear();

        if (!actionRegistry.invoke("view.reset-layout", editorContext) ||
            actionRegistry.invokeCount("view.reset-layout") != 1 ||
            editorContext.workspace().layoutResetRequestCount() != 1) {
            asharia::logError("Editor action registry smoke failed to request layout reset.");
            return false;
        }
        editorContext.eventQueue().clear();

        return true;
    }

    [[nodiscard]] bool
    validateToolRegistrySmoke(const asharia::editor::EditorToolRegistry& toolRegistry,
                              const asharia::editor::EditorActionRegistry& actionRegistry,
                              const asharia::editor::EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedToolCount = 7;
        constexpr std::size_t kExpectedPanelContributions = 6;
        constexpr std::size_t kExpectedActionContributions = 9;
        constexpr std::size_t kExpectedToolbarActionContributions = 8;
        constexpr std::size_t kExpectedViewportOverlayContributions = 3;

        if (toolRegistry.toolCount() != kExpectedToolCount ||
            toolRegistry.panelContributionCount() != kExpectedPanelContributions ||
            toolRegistry.actionContributionCount() != kExpectedActionContributions ||
            toolRegistry.toolbarActionContributionCount() != kExpectedToolbarActionContributions ||
            toolRegistry.viewportOverlayContributionCount() !=
                kExpectedViewportOverlayContributions) {
            asharia::logError("Editor tool registry smoke detected invalid contribution counts.");
            return false;
        }

        bool referencesValid = true;
        toolRegistry.visitTools([&](const asharia::editor::EditorToolDesc& tool) {
            for (const asharia::editor::EditorToolPanelContribution& panel : tool.panels) {
                referencesValid =
                    referencesValid && panelRegistry.findPanelDesc(panel.panelId) != nullptr;
            }
            for (const asharia::editor::EditorToolActionContribution& action : tool.actions) {
                referencesValid =
                    referencesValid && actionRegistry.findAction(action.actionId) != nullptr;
            }
            for (const asharia::editor::EditorToolViewportOverlayContribution& overlay :
                 tool.viewportOverlays) {
                referencesValid = referencesValid && overlay.viewportId == "scene-view";
            }
        });
        if (!referencesValid) {
            asharia::logError("Editor tool registry smoke found an invalid contribution target.");
            return false;
        }

        bool sawDebugToolbarAction = false;
        bool sawViewToolbarAction = false;
        bool sawUtilityToolbarAction = false;
        toolRegistry.visitToolbarActions(
            asharia::editor::EditorToolbarSlot::Debug,
            [&](const asharia::editor::EditorToolDesc& tool,
                const asharia::editor::EditorToolActionContribution& action) {
                static_cast<void>(tool);
                sawDebugToolbarAction =
                    sawDebugToolbarAction || action.actionId == "debug.capture-frame";
            });
        toolRegistry.visitToolbarActions(
            asharia::editor::EditorToolbarSlot::View,
            [&](const asharia::editor::EditorToolDesc& tool,
                const asharia::editor::EditorToolActionContribution& action) {
                static_cast<void>(tool);
                sawViewToolbarAction = sawViewToolbarAction || action.actionId == "view.scene-view";
            });
        toolRegistry.visitToolbarActions(
            asharia::editor::EditorToolbarSlot::Utility,
            [&](const asharia::editor::EditorToolDesc& tool,
                const asharia::editor::EditorToolActionContribution& action) {
                static_cast<void>(tool);
                sawUtilityToolbarAction =
                    sawUtilityToolbarAction || action.actionId == "view.editor-settings";
            });
        if (!sawDebugToolbarAction || !sawViewToolbarAction || !sawUtilityToolbarAction) {
            asharia::logError("Editor tool registry smoke missed a toolbar contribution slot.");
            return false;
        }

        std::size_t sceneOverlayCount = 0;
        bool sawSceneGrid = false;
        bool sawSceneGizmo = false;
        bool sawSceneSelectionOutline = false;
        toolRegistry.visitViewportOverlays(
            "scene-view",
            [&](const asharia::editor::EditorToolDesc& tool,
                const asharia::editor::EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                ++sceneOverlayCount;
                sawSceneGrid = sawSceneGrid || overlay.overlayId == "scene.grid";
                sawSceneGizmo = sawSceneGizmo || overlay.overlayId == "scene.transform-gizmo";
                sawSceneSelectionOutline =
                    sawSceneSelectionOutline || overlay.overlayId == "scene.selection-outline";
            });
        std::size_t gameOverlayCount = 0;
        toolRegistry.visitViewportOverlays(
            "game-view",
            [&](const asharia::editor::EditorToolDesc& tool,
                const asharia::editor::EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                static_cast<void>(overlay);
                ++gameOverlayCount;
            });
        if (sceneOverlayCount != kExpectedViewportOverlayContributions || gameOverlayCount != 0 ||
            !sawSceneGrid || !sawSceneGizmo || !sawSceneSelectionOutline) {
            asharia::logError("Editor tool registry smoke missed a viewport overlay query.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    validateShortcutRouterSmoke(asharia::editor::EditorActionRegistry& actionRegistry,
                                asharia::editor::EditorContext& editorContext,
                                asharia::editor::EditorPanelRegistry& panelRegistry) {
        asharia::editor::EditorShortcutRouter shortcutRouter;
        editorContext.eventQueue().clear();

        shortcutRouter.beginFrame(asharia::editor::EditorInputSnapshot{
            .shortcutsEnabled = false,
        });
        if (shortcutRouter.routeShortcut(actionRegistry, editorContext, "view.log", true) ||
            actionRegistry.invokeCount("view.log") != 1 || !editorContext.eventQueue().empty()) {
            asharia::logError("Editor shortcut router smoke invoked while shortcuts were "
                              "disabled.");
            return false;
        }

        shortcutRouter.beginFrame(asharia::editor::EditorInputSnapshot{
            .shortcutsEnabled = true,
        });
        if (shortcutRouter.routeShortcut(actionRegistry, editorContext, "file.open", true) ||
            actionRegistry.invokeCount("file.open") != 0 || !editorContext.eventQueue().empty()) {
            asharia::logError("Editor shortcut router smoke invoked a disabled action.");
            return false;
        }

        if (!panelRegistry.closePanel("log") ||
            !shortcutRouter.routeShortcut(actionRegistry, editorContext, "view.log", true) ||
            !panelRegistry.isOpen("log") || actionRegistry.invokeCount("view.log") != 2) {
            asharia::logError("Editor shortcut router smoke failed to invoke View/Log.");
            return false;
        }

        const asharia::editor::EditorShortcutRouterStats stats = shortcutRouter.stats();
        if (stats.evaluatedFrames != 2 || stats.blockedFrames != 1 || stats.shortcutMatches != 2 ||
            stats.shortcutInvocations != 1) {
            asharia::logError("Editor shortcut router smoke detected invalid shortcut stats.");
            return false;
        }
        editorContext.eventQueue().clear();

        return true;
    }

    [[nodiscard]] bool
    validateEditorRegistrationSmoke(asharia::editor::EditorRunMode mode,
                                    asharia::editor::EditorActionRegistry& actionRegistry,
                                    asharia::editor::EditorContext& editorContext,
                                    asharia::editor::EditorPanelRegistry& panelRegistry,
                                    const asharia::editor::EditorToolRegistry& toolRegistry) {
        if (!isSmokeMode(mode)) {
            return true;
        }

        return validatePanelRegistrySmoke(panelRegistry) &&
               validateActionRegistrySmoke(actionRegistry, editorContext, panelRegistry) &&
               validateToolRegistrySmoke(toolRegistry, actionRegistry, panelRegistry) &&
               validateEditorSettingsSmoke(mode, editorContext) &&
                validateShortcutRouterSmoke(actionRegistry, editorContext, panelRegistry);
    }

    class TestSetIntCommand final : public asharia::editor::EditorCommand {
    public:
        TestSetIntCommand(int& target, int newValue)
            : target_(&target), newValue_(newValue), oldValue_(target) {}

        [[nodiscard]] std::string description() const override {
            return "SetInt " + std::to_string(oldValue_) + " -> " + std::to_string(newValue_);
        }

        [[nodiscard]] asharia::Result<void> execute() override {
            *target_ = newValue_;
            return {};
        }

        [[nodiscard]] asharia::Result<void> undo() override {
            *target_ = oldValue_;
            return {};
        }

    private:
        int* target_{};
        int newValue_{};
        int oldValue_{};
    };

    [[nodiscard]] bool
    validateEditorCommandSmoke(asharia::editor::EditorRunMode mode) {
        if (!isSmokeMode(mode)) {
            return true;
        }

        int testValue = 0;
        constexpr int kNewValue = 42;
        asharia::editor::EditorCommandHistory history;
        {
            auto transaction = asharia::editor::EditorTransaction{};
            transaction.addCommand(std::make_unique<TestSetIntCommand>(testValue, kNewValue));
            history.push(std::move(transaction));
        }
        if (history.undoDepth() != 1 || history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: invalid depth after push.");
            return false;
        }
        auto undoResult = history.undo();
        if (!undoResult || testValue != 0 || history.undoDepth() != 0 ||
            history.redoDepth() != 1) {
            asharia::logError("Editor command smoke: undo did not restore value.");
            return false;
        }
        auto redoResult = history.redo();
        if (!redoResult || testValue != kNewValue || history.undoDepth() != 1 ||
            history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: redo did not reapply value.");
            return false;
        }
        auto emptyUndo = history.undo();
        auto doubleUndo = history.undo();
        if (doubleUndo) {
            asharia::logError("Editor command smoke: double undo should have failed.");
            return false;
        }
        static_cast<void>(emptyUndo);
        return true;
    }

} // namespace

namespace asharia::editor {

    int runEditor(EditorRunMode mode) {
        const bool smokeMode = isSmokeMode(mode);

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

        auto context = createEditorContext(*extensions, *window);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        if (auto waited = waitForRenderableWindow(*window, smokeMode); !waited) {
            asharia::logError(waited.error().message);
            return EXIT_FAILURE;
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = createEditorFrameLoop(*context, *window);
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

        ImGuiRuntime imgui;
        const ImGuiRuntimeDesc imguiDesc{
            .layoutIniPath = editorLayoutIniPathForRun(smokeMode),
            .theme = settingsRun.settings.theme,
            .enableCjkGlyphs = true,
            .cjkFontPath = {},
            .fontPixelSize = 16.0F,
        };
        if (auto created = imgui.create(window->nativeHandle(), *context, *frameLoop, imguiDesc);
            !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }
        if (!validateEditorStartupSmoke(mode, imgui, editorLocale, settingsRun.settings.theme)) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = asharia::BasicFullscreenTextureRenderer::create(
            asharia::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        EditorViewportCoordinator viewportHost;
        if (auto created = viewportHost.create(*context); !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorEventQueue eventQueue;
        asharia::editor::EditorDiagnosticsLog diagnosticsLog;
        asharia::editor::EditorFrameDebugger frameDebugger;
        asharia::editor::EditorI18n editorI18n{settingsRun.settings.locale};
        asharia::editor::EditorSettingsController settingsController{settingsRun.settings,
                                                                     settingsRun.path, editorI18n};
        asharia::editor::EditorWorkspaceController workspaceController;
        asharia::editor::EditorPanelRegistry panelRegistry;
        panelRegistry.setEventQueue(&eventQueue);
        if (auto registered = registerEditorPanels(panelRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorActionRegistry actionRegistry;
        if (auto registered = registerEditorActions(actionRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorToolRegistry toolRegistry;
        if (auto registered = registerEditorTools(toolRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorContext editorContext{
            panelRegistry, eventQueue,         diagnosticsLog,      frameDebugger,
            editorI18n,    settingsController, workspaceController, toolRegistry};
        if (!validateEditorRegistrationSmoke(mode, actionRegistry, editorContext, panelRegistry,
                                             toolRegistry) ||
            !validateEditorCommandSmoke(mode)) {
            return EXIT_FAILURE;
        }

        auto runResult = runEditorLoop(*window, *frameLoop, *renderer, viewportHost, frameDebugger,
                                       actionRegistry, editorContext, panelRegistry, mode);
        if (!runResult) {
            asharia::logError(runResult.error().message);
            return EXIT_FAILURE;
        }
        if (smokeMode && workspaceController.layoutApplyCount() == 0) {
            asharia::logError("Editor workspace smoke did not apply a dock layout preset.");
            return EXIT_FAILURE;
        }
        const asharia::editor::EditorViewportCoordinatorStats viewportStats = viewportHost.stats();
        const asharia::editor::ImGuiTextureRegistryStats textureRegistryStats =
            viewportHost.textureRegistryStats();
        if (!validateViewportSmokePresentation(mode, *runResult, viewportHost,
                                               textureRegistryStats) ||
            !validateViewportFlagsSmoke(mode, *runResult, viewportHost, viewportStats) ||
            !validateViewportResizeSmoke(mode, *runResult, viewportStats, textureRegistryStats) ||
            !validateFrameDebuggerSmoke(mode, *runResult, frameDebugger) ||
            !validateInputRouterSmoke(mode, *runResult) ||
            !validateShortcutRouterRunSmoke(mode, *runResult)) {
            return EXIT_FAILURE;
        }
        imgui.saveLayoutNow();
        if (!validateImguiLayoutSavedSmoke(mode, imgui)) {
            return EXIT_FAILURE;
        }

        const VkExtent2D viewportExtent = viewportHost.descriptorExtent();
        viewportHost.shutdown();
        imgui.shutdown();
        window->requestClose();

        std::cout << "Editor shell frames: " << runResult->renderedFrames
                  << ", viewport frames: " << viewportHost.viewportFramesRendered()
                  << ", texture frames: " << viewportHost.textureFramesSubmitted()
                  << ", input frames: " << runResult->inputStats.capturedFrames
                  << ", scene input reports: " << runResult->inputStats.sceneViewReports
                  << ", shortcut frames: " << runResult->shortcutStats.evaluatedFrames
                  << ", shortcut invocations: " << runResult->shortcutStats.shortcutInvocations
                  << ", overlay texture frames: " << viewportStats.overlayFlagTextureFramesAcquired
                  << ", render view diagnostics frames: "
                  << viewportStats.renderViewDiagnosticsFramesRecorded
                  << ", idle scene skips: " << viewportStats.idleSceneViewFramesSkipped
                  << ", frame debugger: " << frameDebugger.stateName()
                  << ", live texture descriptors peak: " << textureRegistryStats.peakLiveDescriptors
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        if (isViewportResizeSmokeMode(mode)) {
            std::cout << "Editor viewport resize: " << runResult->viewportExtentBeforeResize.width
                      << 'x' << runResult->viewportExtentBeforeResize.height << " -> "
                      << runResult->viewportExtentAfterResize.width << 'x'
                      << runResult->viewportExtentAfterResize.height
                      << ", retired render targets: " << viewportStats.renderTargetsRetired
                      << ", deferred render targets: " << viewportStats.renderTargetsDeferred
                      << ", retired texture descriptors: "
                      << textureRegistryStats.descriptorsRetired << '\n';
        }
        return EXIT_SUCCESS;
    }

} // namespace asharia::editor
