#include <algorithm>
#include <array>
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
                     "[--smoke-transient] [--smoke-dynamic-rendering] [--smoke-resize] "
                     "[--smoke-triangle] [--smoke-depth-triangle] "
                     "[--smoke-descriptor-layout] [--smoke-fullscreen-texture]\n";
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

    int runSmokeTransient() {
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
            *glfw, vke::WindowDesc{.title = "VkEngine Transient Image Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Transient Image Smoke",
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

        vke::BasicTransientFrameRecorder recorder{context->device(), context->allocator()};

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.08F, 0.14F, 0.22F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanFrameRecordCallback record =
            [&recorder](const vke::VulkanFrameRecordContext& frame) {
                return recorder.record(frame);
            };

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
                vke::logError("Swapchain remained out of date during transient smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered transient frames: " << extent.width << 'x' << extent.height
                  << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
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

    int runSmokeTriangle(bool useDepth = false) {
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

        const std::string_view title =
            useDepth ? "VkEngine Depth Triangle Smoke" : "VkEngine Triangle Smoke";
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
                [&triangleRenderer, useDepth](
                    const vke::VulkanFrameRecordContext& recordContext) {
                    if (useDepth) {
                        return triangleRenderer->recordFrameWithDepth(recordContext);
                    }
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError(useDepth ? "Swapchain remained out of date during depth triangle "
                                         "smoke."
                                       : "Swapchain remained out of date during triangle smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << (useDepth ? "Rendered depth triangle frames: " : "Rendered triangle frames: ")
                  << extent.width << 'x' << extent.height << '\n';
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
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!validated) {
            vke::logError(validated.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Descriptor layout smoke: set 0 bindings 0-2 buffer/image/sampler allocated\n";
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeFullscreenTexture() {
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
            *glfw, vke::WindowDesc{.title = "VkEngine Fullscreen Texture Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Fullscreen Texture Smoke",
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
                          .clearColor = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer =
            vke::BasicFullscreenTextureRenderer::create(vke::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            vke::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&renderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during fullscreen texture smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered fullscreen texture frames: " << extent.width << 'x' << extent.height
                  << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before fullscreen texture teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

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

    bool validateSmokeRenderGraphVulkanMappings(const vke::RenderGraphCompileResult& compiled) {
        const auto vulkanFinalTransition =
            vke::vulkanImageTransition(compiled.finalTransitions.front());
        if (vulkanFinalTransition.oldLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanFinalTransition.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            vke::logError("Render graph Vulkan transition mapping produced unexpected layouts.");
            return false;
        }

        const VkImageMemoryBarrier2 barrier =
            vke::vulkanImageBarrier(vulkanFinalTransition, VK_NULL_HANDLE);
        if (barrier.oldLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            barrier.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
            barrier.srcStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            barrier.srcAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError("Render graph Vulkan barrier mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanShaderReadTransition =
            vke::vulkanImageTransition(compiled.passes[1].transitionsBefore.front());
        if (vulkanShaderReadTransition.oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
            vulkanShaderReadTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanShaderReadTransition.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            vulkanShaderReadTransition.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
            vulkanShaderReadTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanShaderReadTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError("Render graph Vulkan shader-read mapping produced unexpected masks.");
            return false;
        }

        const VkPipelineStageFlags2 depthTestsStages =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        const auto vulkanDepthWriteTransition =
            vke::vulkanImageTransition(compiled.passes[2].transitionsBefore.front());
        if (vulkanDepthWriteTransition.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
            vulkanDepthWriteTransition.newLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
            vulkanDepthWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanDepthWriteTransition.srcAccessMask != 0 ||
            vulkanDepthWriteTransition.dstStageMask != depthTestsStages ||
            vulkanDepthWriteTransition.dstAccessMask !=
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
            vke::logError("Render graph Vulkan depth-write mapping produced unexpected masks.");
            return false;
        }

        const VkImageMemoryBarrier2 depthBarrier = vke::vulkanImageBarrier(
            vulkanDepthWriteTransition, VK_NULL_HANDLE, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (depthBarrier.subresourceRange.aspectMask != VK_IMAGE_ASPECT_DEPTH_BIT) {
            vke::logError("Render graph Vulkan depth barrier used an unexpected aspect mask.");
            return false;
        }

        const auto vulkanDepthSampledTransition =
            vke::vulkanImageTransition(compiled.passes[3].transitionsBefore.front());
        if (vulkanDepthSampledTransition.oldLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
            vulkanDepthSampledTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanDepthSampledTransition.srcStageMask != depthTestsStages ||
            vulkanDepthSampledTransition.srcAccessMask !=
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ||
            vulkanDepthSampledTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanDepthSampledTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError(
                "Render graph Vulkan depth sampled-read mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanTransientWriteTransition =
            vke::vulkanImageTransition(compiled.passes[4].transitionsBefore.front());
        if (vulkanTransientWriteTransition.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
            vulkanTransientWriteTransition.newLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
            vulkanTransientWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanTransientWriteTransition.srcAccessMask != 0 ||
            vulkanTransientWriteTransition.dstStageMask !=
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT ||
            vulkanTransientWriteTransition.dstAccessMask !=
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
            vke::logError("Render graph Vulkan transient write mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanTransientSampleTransition =
            vke::vulkanImageTransition(compiled.passes[5].transitionsBefore.front());
        if (vulkanTransientSampleTransition.oldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
            vulkanTransientSampleTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanTransientSampleTransition.srcStageMask !=
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT ||
            vulkanTransientSampleTransition.srcAccessMask !=
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT ||
            vulkanTransientSampleTransition.dstStageMask !=
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanTransientSampleTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError(
                "Render graph Vulkan transient sampled-read mapping produced unexpected masks.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphTransientPlan(const vke::RenderGraphCompileResult& compiled) {
        if (compiled.transientImages.size() != 1) {
            vke::logError("Render graph did not produce the expected transient allocation plan.");
            return false;
        }

        const vke::RenderGraphTransientImageAllocation& transient =
            compiled.transientImages.front();
        if (transient.image.index != 2 || transient.imageName != "TransientColor" ||
            transient.format != vke::RenderGraphImageFormat::B8G8R8A8Srgb ||
            transient.extent.width != 640 || transient.extent.height != 360 ||
            transient.firstPassIndex != 4 || transient.lastPassIndex != 5 ||
            transient.finalState != vke::RenderGraphImageState::ShaderRead ||
            transient.finalShaderStage != vke::RenderGraphShaderStage::Fragment) {
            vke::logError("Render graph transient allocation plan contained unexpected fields.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphCommands(const vke::RenderGraphCompileResult& compiled) {
        if (compiled.passes.size() != 6) {
            vke::logError("Render graph command smoke received an unexpected pass count.");
            return false;
        }

        const auto& clearCommands = compiled.passes[0].commands;
        if (clearCommands.size() != 1 ||
            clearCommands.front().kind != vke::RenderGraphCommandKind::ClearColor ||
            clearCommands.front().name != "target") {
            vke::logError("Render graph clear command summary contained unexpected fields.");
            return false;
        }

        const auto& sampleCommands = compiled.passes[1].commands;
        if (sampleCommands.size() != 4 ||
            sampleCommands[0].kind != vke::RenderGraphCommandKind::SetShader ||
            sampleCommands[0].name != "Hidden/SmokeSample" ||
            sampleCommands[1].kind != vke::RenderGraphCommandKind::SetTexture ||
            sampleCommands[1].secondaryName != "source" ||
            sampleCommands[2].kind != vke::RenderGraphCommandKind::SetFloat ||
            sampleCommands[3].kind != vke::RenderGraphCommandKind::DrawFullscreenTriangle) {
            vke::logError("Render graph sample command summary contained unexpected fields.");
            return false;
        }

        const auto& transientCommands = compiled.passes[5].commands;
        if (transientCommands.size() != 4 ||
            transientCommands[0].kind != vke::RenderGraphCommandKind::SetShader ||
            transientCommands[1].kind != vke::RenderGraphCommandKind::SetTexture ||
            transientCommands[2].kind != vke::RenderGraphCommandKind::SetVec4 ||
            transientCommands[3].kind != vke::RenderGraphCommandKind::DrawFullscreenTriangle) {
            vke::logError("Render graph transient command summary contained unexpected fields.");
            return false;
        }

        return true;
    }

    bool hasNoDepthSlots(vke::RenderGraphPassContext context) {
        return context.depthReads.empty() && context.depthWrites.empty() &&
               context.depthSampledReads.empty() && context.depthReadSlots.empty() &&
               context.depthWriteSlots.empty() && context.depthSampledReadSlots.empty();
    }

    vke::Result<void> validateClearTransferContext(vke::RenderGraphPassContext context) {
        if (context.name != "ClearColor" || context.type != "basic.clear-transfer" ||
            context.paramsType != "basic.clear-transfer.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !hasNoDepthSlots(context) ||
            context.transferWrites.size() != 1 || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || context.transferWriteSlots.size() != 1 ||
            context.transferWriteSlots.front().name != "target") {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateSampleFragmentContext(vke::RenderGraphPassContext context) {
        if (context.name != "SampleColor" || context.type != "basic.sample-fragment" ||
            context.paramsType != "basic.sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            context.shaderReads.size() != 1 || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            context.shaderReadSlots.size() != 1 || !context.transferWriteSlots.empty() ||
            context.shaderReadSlots.front().name != "source" ||
            context.shaderReadSlots.front().shaderStage != vke::RenderGraphShaderStage::Fragment) {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph shader-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateDepthWriteContext(vke::RenderGraphPassContext context) {
        if (context.name != "WriteDepth" || context.type != "basic.depth-write" ||
            context.paramsType != "basic.depth-write.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !context.depthReads.empty() ||
            context.depthWrites.size() != 1 || !context.depthSampledReads.empty() ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || !context.depthReadSlots.empty() ||
            context.depthWriteSlots.size() != 1 || !context.depthSampledReadSlots.empty() ||
            !context.transferWriteSlots.empty() ||
            context.depthWriteSlots.front().name != "depth") {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph depth-write executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateDepthSampleContext(vke::RenderGraphPassContext context) {
        if (context.name != "SampleDepth" || context.type != "basic.depth-sample-fragment" ||
            context.paramsType != "basic.depth-sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !context.depthReads.empty() ||
            !context.depthWrites.empty() || context.depthSampledReads.size() != 1 ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || !context.depthReadSlots.empty() ||
            !context.depthWriteSlots.empty() || context.depthSampledReadSlots.size() != 1 ||
            !context.transferWriteSlots.empty() ||
            context.depthSampledReadSlots.front().name != "depth" ||
            context.depthSampledReadSlots.front().shaderStage !=
                vke::RenderGraphShaderStage::Fragment) {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph depth sampled-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateTransientWriteContext(vke::RenderGraphPassContext context) {
        if (context.name != "WriteTransientColor" || context.type != "basic.transient-color" ||
            context.paramsType != "basic.transient-color.params" ||
            context.transitionsBefore.size() != 1 || context.colorWrites.size() != 1 ||
            !context.shaderReads.empty() || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || context.colorWriteSlots.size() != 1 ||
            !context.shaderReadSlots.empty() || !context.transferWriteSlots.empty() ||
            context.colorWriteSlots.front().name != "target") {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph transient write executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateTransientSampleContext(vke::RenderGraphPassContext context) {
        if (context.name != "SampleTransientColor" ||
            context.type != "basic.transient-sample-fragment" ||
            context.paramsType != "basic.transient-sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            context.shaderReads.size() != 1 || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            context.shaderReadSlots.size() != 1 || !context.transferWriteSlots.empty() ||
            context.shaderReadSlots.front().name != "source" ||
            context.shaderReadSlots.front().shaderStage != vke::RenderGraphShaderStage::Fragment) {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph transient sampled-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    void registerSmokeRenderGraphExecutors(vke::RenderGraphExecutorRegistry& executors,
                                           int& callbackCount) {
        executors.registerExecutor(
            "basic.clear-transfer",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateClearTransferContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.sample-fragment",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateSampleFragmentContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.depth-write",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateDepthWriteContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.depth-sample-fragment",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateDepthSampleContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.transient-color",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateTransientWriteContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.transient-sample-fragment",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateTransientSampleContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
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
        const auto depthBuffer = graph.importImage(vke::RenderGraphImageDesc{
            .name = "DepthBuffer",
            .format = vke::RenderGraphImageFormat::D32Sfloat,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::DepthSampledRead,
            .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        const auto transientColor = graph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "TransientColor",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 640, .height = 360},
        });

        int callbackCount = 0;
        graph.addPass("ClearColor", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", backbuffer)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.02F, 0.12F, 0.18F, 1.0F});
            });
        graph.addPass("SampleColor", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", backbuffer, vke::RenderGraphShaderStage::Fragment)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/SmokeSample", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setFloat("Exposure", 1.0F)
                    .drawFullscreenTriangle();
            });
        graph.addPass("WriteDepth", "basic.depth-write")
            .setParamsType("basic.depth-write.params")
            .writeDepth("depth", depthBuffer);
        graph.addPass("SampleDepth", "basic.depth-sample-fragment")
            .setParamsType("basic.depth-sample-fragment.params")
            .readDepthTexture("depth", depthBuffer, vke::RenderGraphShaderStage::Fragment);
        graph.addPass("WriteTransientColor", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", transientColor);
        graph.addPass("SampleTransientColor", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", transientColor, vke::RenderGraphShaderStage::Fragment)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/TransientSample", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setVec4("Tint", std::array{1.0F, 0.85F, 0.65F, 1.0F})
                    .drawFullscreenTriangle();
            });

        vke::RenderGraphSchemaRegistry schemas;
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.clear-transfer",
            .paramsType = "basic.clear-transfer.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = vke::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.sample-fragment",
            .paramsType = "basic.sample-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = vke::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.depth-write",
            .paramsType = "basic.depth-write.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = vke::RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.depth-sample-fragment",
            .paramsType = "basic.depth-sample-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = vke::RenderGraphSlotAccess::DepthSampledRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.transient-color",
            .paramsType = "basic.transient-color.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = vke::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.transient-sample-fragment",
            .paramsType = "basic.transient-sample-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = vke::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
        });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            vke::logError(compiled.error().message);
            return EXIT_FAILURE;
        }
        if (compiled->finalTransitions.empty()) {
            vke::logError("Render graph did not produce a final transition.");
            return EXIT_FAILURE;
        }
        if (compiled->passes.size() != 6 || compiled->passes[1].transitionsBefore.empty() ||
            compiled->passes[2].transitionsBefore.empty() ||
            compiled->passes[3].transitionsBefore.empty() ||
            compiled->passes[4].transitionsBefore.empty() ||
            compiled->passes[5].transitionsBefore.empty() ||
            compiled->transientImages.size() != 1) {
            vke::logError("Render graph did not produce the expected shader-read pass transition.");
            return EXIT_FAILURE;
        }

        std::cout << graph.formatDebugTables(*compiled) << '\n';

        if (!validateSmokeRenderGraphTransientPlan(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphCommands(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphVulkanMappings(*compiled)) {
            return EXIT_FAILURE;
        }

        vke::RenderGraphExecutorRegistry executors;
        registerSmokeRenderGraphExecutors(executors, callbackCount);

        auto executed = graph.execute(*compiled, executors);
        if (!executed) {
            vke::logError(executed.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Render graph passes: " << compiled->passes.size()
                  << ", final transitions: " << compiled->finalTransitions.size()
                  << ", callbacks: " << callbackCount << '\n';
        return compiled->passes.size() == 6 && compiled->transientImages.size() == 1 &&
                       compiled->finalTransitions.size() == 1 && callbackCount == 6
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

        if (hasArg(args, "--smoke-transient")) {
            return runSmokeTransient();
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

        if (hasArg(args, "--smoke-depth-triangle")) {
            return runSmokeTriangle(true);
        }

        if (hasArg(args, "--smoke-descriptor-layout")) {
            return runSmokeDescriptorLayout();
        }

        if (hasArg(args, "--smoke-fullscreen-texture")) {
            return runSmokeFullscreenTexture();
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
