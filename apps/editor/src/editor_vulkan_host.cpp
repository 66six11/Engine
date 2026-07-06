#include "editor_vulkan_host.hpp"

#include <chrono>
#include <thread>
#include <utility>

#include "asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_viewport_coordinator.hpp"
#include "imgui_frame_renderer.hpp"

namespace asharia::editor {
    namespace {

        constexpr VulkanDebugLabelMode kEditorDebugLabels = VulkanDebugLabelMode::Required;
        constexpr int kSmokeAttemptLimit = 120;

        [[nodiscard]] bool isRenderableExtent(WindowFramebufferExtent extent) {
            return extent.width > 0 && extent.height > 0;
        }

        [[nodiscard]] bool extentMatches(VkExtent2D lhs, WindowFramebufferExtent rhs) {
            return lhs.width == rhs.width && lhs.height == rhs.height;
        }

    } // namespace

    [[nodiscard]] EditorExtent2D editorExtentFromVk(VkExtent2D extent) {
        return EditorExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    [[nodiscard]] VoidResult waitForRenderableEditorWindow(GlfwWindow& window, bool smokeMode) {
        int attempts = 0;
        auto framebuffer = window.framebufferExtent();
        while (!window.shouldClose() && !isRenderableExtent(framebuffer)) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{
                    vulkanError("Timed out waiting for a renderable editor framebuffer")};
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            GlfwWindow::pollEvents();
            framebuffer = window.framebufferExtent();
        }

        return {};
    }

    [[nodiscard]] Result<VulkanContext>
    createEditorVulkanContext(const std::vector<std::string>& extensions, GlfwWindow& window) {
        const VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Editor",
            .requiredInstanceExtensions = extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return glfwCreateVulkanSurface(window, instance);
                },
            .debugLabels = kEditorDebugLabels,
            .externalInterop = {},
        };

        return VulkanContext::create(contextDesc);
    }

    [[nodiscard]] Result<VulkanFrameLoop> createEditorFrameLoop(const VulkanContext& context,
                                                                const GlfwWindow& window) {
        const auto framebuffer = window.framebufferExtent();
        return VulkanFrameLoop::create(
            context, VulkanFrameLoopDesc{
                         .width = framebuffer.width,
                         .height = framebuffer.height,
                         .clearColor = VkClearColorValue{{0.015F, 0.018F, 0.022F, 1.0F}},
                     });
    }

    [[nodiscard]] Result<bool> prepareEditorFrameLoopExtent(GlfwWindow& window,
                                                            VulkanFrameLoop& frameLoop) {
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
            if (*recreated == VulkanFrameStatus::OutOfDate) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] Result<bool> renderEditorFrame(VulkanFrameLoop& frameLoop,
                                                 BasicFullscreenTextureRenderer& renderer,
                                                 EditorViewportCoordinator& viewportHost,
                                                 EditorFrameDebugger& frameDebugger,
                                                 int frameIndex) {
        auto status = frameLoop.renderFrame(
            [&renderer, &viewportHost, &frameDebugger, frameIndex](
                const VulkanFrameRecordContext& context) -> Result<VulkanFrameRecordResult> {
                const bool recordRenderViews = frameDebugger.shouldRecordRenderViews();
                EditorViewportRepaintReasons repaintReasons =
                    frameDebugger.consumeRenderViewRepaintReasons();
                if (frameDebugger.isCapturingFrame()) {
                    addEditorViewportRepaintReason(
                        repaintReasons, EditorViewportRepaintReason::FrameDebugEventChanged);
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
                        frameDebugger.captureRecordedView(EditorFrameDebugCaptureDesc{
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

        return *status != VulkanFrameStatus::OutOfDate;
    }

} // namespace asharia::editor
