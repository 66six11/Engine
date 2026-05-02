#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "vke/core/log.hpp"
#include "vke/core/version.hpp"
#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"
#include "vke/renderer_basic_vulkan/clear_frame.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"
#include "vke/window_glfw/glfw_window.hpp"

namespace {

    bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return arg != nullptr && arg == expected; });
    }

    void printVersion() {
        std::cout << vke::kEngineName << ' ' << vke::kEngineVersion.major << '.'
                  << vke::kEngineVersion.minor << '.' << vke::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout << "Usage: vke-sample-viewer [--help] [--version] [--smoke-window] "
                     "[--smoke-vulkan] [--smoke-frame] [--smoke-rendergraph] "
                     "[--smoke-dynamic-rendering] [--smoke-resize] [--smoke-triangle] "
                     "[--smoke-descriptor-layout]\n";
    }

    bool isRenderableExtent(vke::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, vke::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    int runSmokeWindow() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        vke::logInfo("GLFW smoke window created.");
        vke::GlfwWindow::pollEvents();
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeVulkan() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Vulkan Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc desc{
            .applicationName = "VkEngine Vulkan Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(desc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        const auto& info = context->deviceInfo();
        std::cout << "Vulkan device: " << info.name << " API "
                  << vke::vulkanVersionString(info.apiVersion) << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeFrame(const vke::VulkanFrameRecordCallback& record, std::string_view title,
                      VkClearColorValue clearColor) {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = std::string{title}});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = std::string{title},
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(*context, vke::VulkanFrameLoopDesc{
                                                                    .width = framebuffer.width,
                                                                    .height = framebuffer.height,
                                                                    .clearColor = clearColor,
                                                                });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(record);
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during frame smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeFrame() {
        return runSmokeFrame(vke::recordBasicClearFrame, "VkEngine Frame Smoke",
                             VkClearColorValue{{0.02F, 0.12F, 0.18F, 1.0F}});
    }

    int runSmokeDynamicRendering() {
        return runSmokeFrame(vke::recordBasicDynamicClearFrame, "VkEngine Dynamic Rendering Smoke",
                             VkClearColorValue{{0.18F, 0.06F, 0.14F, 1.0F}});
    }

    int runSmokeResize() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Resize Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Resize Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.06F, 0.10F, 0.18F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        auto firstFrame = frameLoop->renderFrame(vke::recordBasicDynamicClearFrame);
        if (!firstFrame) {
            vke::logError(firstFrame.error().message);
            return EXIT_FAILURE;
        }
        if (*firstFrame == vke::VulkanFrameStatus::OutOfDate) {
            vke::logError("Initial resize smoke frame was unexpectedly out of date.");
            return EXIT_FAILURE;
        }

        frameLoop->setTargetExtent(0, 0);
        auto zeroExtent = frameLoop->recreate();
        if (!zeroExtent) {
            vke::logError(zeroExtent.error().message);
            return EXIT_FAILURE;
        }
        if (*zeroExtent != vke::VulkanFrameStatus::OutOfDate) {
            vke::logError("Zero-sized resize smoke did not report OutOfDate.");
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto restoredFramebuffer = window->framebufferExtent();
        frameLoop->setTargetExtent(restoredFramebuffer.width, restoredFramebuffer.height);
        auto recreated = frameLoop->recreate();
        if (!recreated) {
            vke::logError(recreated.error().message);
            return EXIT_FAILURE;
        }
        if (*recreated != vke::VulkanFrameStatus::Recreated) {
            vke::logError("Resize smoke did not recreate the swapchain after extent restore.");
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(vke::recordBasicDynamicClearFrame);
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during resize smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Resize smoke frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeTriangle() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Triangle Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Triangle Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto triangleRenderer = vke::BasicTriangleRenderer::create(vke::BasicTriangleRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
        });
        if (!triangleRenderer) {
            vke::logError(triangleRenderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&triangleRenderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during triangle smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered triangle frames: " << extent.width << 'x' << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before triangle pipeline teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeDescriptorLayout() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(
            *glfw, vke::WindowDesc{.title = "VkEngine Descriptor Layout Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Descriptor Layout Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto validated =
            vke::validateBasicDescriptorLayoutSmoke(vke::BasicDescriptorLayoutSmokeDesc{
                .device = context->device(),
                .shaderDirectory = shaderDir,
            });
        if (!validated) {
            vke::logError(validated.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Descriptor layout smoke: set 0 binding 0 constantBuffer fragment\n";
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runInteractiveViewer() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Sample Viewer"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Sample Viewer",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        auto framebuffer = window->framebufferExtent();
        while (!window->shouldClose() && !isRenderableExtent(framebuffer)) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            vke::GlfwWindow::pollEvents();
            framebuffer = window->framebufferExtent();
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto triangleRenderer = vke::BasicTriangleRenderer::create(vke::BasicTriangleRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
        });
        if (!triangleRenderer) {
            vke::logError(triangleRenderer.error().message);
            return EXIT_FAILURE;
        }

        while (!window->shouldClose()) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
            if (!isRenderableExtent(currentFramebuffer)) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                continue;
            }

            if (!extentMatches(frameLoop->extent(), currentFramebuffer)) {
                auto recreated = frameLoop->recreate();
                if (!recreated) {
                    vke::logError(recreated.error().message);
                    return EXIT_FAILURE;
                }
                if (*recreated == vke::VulkanFrameStatus::OutOfDate) {
                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(16ms);
                    continue;
                }
            }

            auto status = frameLoop->renderFrame(
                [&triangleRenderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1ms);
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before viewer shutdown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    int runSmokeRenderGraph() {
        vke::RenderGraph graph;
        const auto backbuffer = graph.importImage(vke::RenderGraphImageDesc{
            .name = "Backbuffer",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::Present,
        });

        int callbackCount = 0;
        graph.addPass("ClearColor", "basic.clear-transfer").writeTransfer(backbuffer);

        auto compiled = graph.compile();
        if (!compiled) {
            vke::logError(compiled.error().message);
            return EXIT_FAILURE;
        }
        if (compiled->finalTransitions.empty()) {
            vke::logError("Render graph did not produce a final transition.");
            return EXIT_FAILURE;
        }

        std::cout << graph.formatDebugTables(*compiled) << '\n';

        const auto vulkanFinalTransition =
            vke::vulkanImageTransition(compiled->finalTransitions.front());
        if (vulkanFinalTransition.oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
            vulkanFinalTransition.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            vke::logError("Render graph Vulkan transition mapping produced unexpected layouts.");
            return EXIT_FAILURE;
        }
        const VkImageMemoryBarrier2 barrier =
            vke::vulkanImageBarrier(vulkanFinalTransition, VK_NULL_HANDLE);
        if (barrier.oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
            barrier.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
            barrier.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            barrier.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT) {
            vke::logError("Render graph Vulkan barrier mapping produced unexpected masks.");
            return EXIT_FAILURE;
        }

        vke::RenderGraphExecutorRegistry executors;
        executors.registerExecutor(
            "basic.clear-transfer",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                if (context.name != "ClearColor" || context.type != "basic.clear-transfer" ||
                    context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
                    context.transferWrites.size() != 1) {
                    return std::unexpected{vke::Error{
                        vke::ErrorDomain::RenderGraph,
                        0,
                        "Render graph executor received unexpected pass context.",
                    }};
                }

                ++callbackCount;
                return {};
            });

        auto executed = graph.execute(*compiled, executors);
        if (!executed) {
            vke::logError(executed.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Render graph passes: " << compiled->passes.size()
                  << ", final transitions: " << compiled->finalTransitions.size()
                  << ", callbacks: " << callbackCount << '\n';
        return compiled->passes.size() == 1 && compiled->finalTransitions.size() == 1 &&
                       callbackCount == 1
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

} // namespace

int main(int argc, char** argv) {
    try {
        std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (args.size() == 1) {
            return runInteractiveViewer();
        }

        if (hasArg(args, "--help")) {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--smoke-window")) {
            return runSmokeWindow();
        }

        if (hasArg(args, "--smoke-vulkan")) {
            return runSmokeVulkan();
        }

        if (hasArg(args, "--smoke-frame")) {
            return runSmokeFrame();
        }

        if (hasArg(args, "--smoke-rendergraph")) {
            return runSmokeRenderGraph();
        }

        if (hasArg(args, "--smoke-dynamic-rendering")) {
            return runSmokeDynamicRendering();
        }

        if (hasArg(args, "--smoke-resize")) {
            return runSmokeResize();
        }

        if (hasArg(args, "--smoke-triangle")) {
            return runSmokeTriangle();
        }

        if (hasArg(args, "--smoke-descriptor-layout")) {
            return runSmokeDescriptorLayout();
        }

        printVersion();
        printUsage();
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        vke::logError(exception.what());
    } catch (...) {
        vke::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
