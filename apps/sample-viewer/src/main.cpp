#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "vke/core/log.hpp"
#include "vke/core/version.hpp"
#include "vke/renderer_basic_vulkan/clear_frame.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_render_graph.hpp"
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

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = std::string{title}});
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
        return runSmokeFrame(vke::recordBasicDynamicClearFrame,
                             "VkEngine Dynamic Rendering Smoke",
                             VkClearColorValue{{0.18F, 0.06F, 0.14F, 1.0F}});
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
        graph.addPass("ClearColor")
            .writeTransfer(backbuffer)
            .execute([&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                if (context.name != "ClearColor" || context.transitionsBefore.size() != 1 ||
                    !context.colorWrites.empty() || context.transferWrites.size() != 1) {
                    return std::unexpected{vke::Error{
                        vke::ErrorDomain::RenderGraph,
                        0,
                        "Render graph execute callback received unexpected pass context.",
                    }};
                }

                ++callbackCount;
                return {};
            });

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

        auto executed = graph.execute();
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

        printVersion();
        std::cout << "Usage: vke-sample-viewer [--version] [--smoke-window] [--smoke-vulkan] "
                     "[--smoke-frame] [--smoke-rendergraph] [--smoke-dynamic-rendering]\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        vke::logError(exception.what());
    } catch (...) {
        vke::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
