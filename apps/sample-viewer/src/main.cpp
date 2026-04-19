#include "vke/core/log.hpp"
#include "vke/core/version.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"
#include "vke/window_glfw/glfw_window.hpp"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool hasArg(std::span<char*> args, std::string_view expected) {
    for (char* arg : args) {
        if (arg != nullptr && arg == expected) {
            return true;
        }
    }

    return false;
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
    window->pollEvents();
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

    const vke::VulkanContextDesc desc{
        .applicationName = "VkEngine Vulkan Smoke",
        .requiredInstanceExtensions = *extensions,
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

} // namespace

int main(int argc, char** argv) {
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

    printVersion();
    std::cout << "Usage: vke-sample-viewer [--version] [--smoke-window] [--smoke-vulkan]\n";
    return EXIT_SUCCESS;
}
