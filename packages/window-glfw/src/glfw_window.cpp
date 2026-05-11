#include "asharia/window_glfw/glfw_window.hpp"

#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia {
    namespace {

        Error glfwError(std::string_view fallback) {
            return Error{ErrorDomain::Platform, 0, glfwLastErrorMessage(fallback)};
        }

    } // namespace

    std::string glfwLastErrorMessage(std::string_view fallback) {
        const char* description = nullptr;
        const int code = glfwGetError(&description);

        if (code == GLFW_NO_ERROR || description == nullptr) {
            return std::string{fallback};
        }

        return std::string{description};
    }

    Result<std::vector<std::string>>
    glfwRequiredVulkanInstanceExtensions([[maybe_unused]] const GlfwInstance& instance) {
        if (glfwVulkanSupported() != GLFW_TRUE) {
            return std::unexpected{glfwError("GLFW reports that Vulkan is not supported.")};
        }

        std::uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        if (extensions == nullptr || count == 0) {
            return std::unexpected{
                glfwError("GLFW did not return required Vulkan instance extensions.")};
        }

        std::vector<std::string> result;
        result.reserve(count);
        for (const char* extension : std::span{extensions, count}) {
            result.emplace_back(extension);
        }

        return result;
    }

    Result<VkSurfaceKHR> glfwCreateVulkanSurface(const GlfwWindow& window, VkInstance instance) {
        if (window.nativeHandle() == nullptr) {
            return std::unexpected{glfwError("Cannot create Vulkan surface for a null window.")};
        }

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult result =
            glfwCreateWindowSurface(instance, window.nativeHandle(), nullptr, &surface);
        if (result != VK_SUCCESS) {
            return std::unexpected{Error{
                ErrorDomain::Vulkan,
                static_cast<int>(result),
                glfwLastErrorMessage("Failed to create Vulkan window surface."),
            }};
        }

        return surface;
    }

    GlfwInstance::GlfwInstance(bool initialized) : initialized_(initialized) {}

    GlfwInstance::GlfwInstance(GlfwInstance&& other) noexcept
        : initialized_(std::exchange(other.initialized_, false)) {}

    GlfwInstance& GlfwInstance::operator=(GlfwInstance&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (initialized_) {
            glfwTerminate();
        }

        initialized_ = std::exchange(other.initialized_, false);
        return *this;
    }

    GlfwInstance::~GlfwInstance() {
        if (initialized_) {
            glfwTerminate();
        }
    }

    Result<GlfwInstance> GlfwInstance::create() {
        if (glfwInit() != GLFW_TRUE) {
            return std::unexpected{glfwError("Failed to initialize GLFW.")};
        }

        return GlfwInstance{true};
    }

    GlfwWindow::GlfwWindow(GLFWwindow* window) : window_(window) {}

    GlfwWindow::GlfwWindow(GlfwWindow&& other) noexcept
        : window_(std::exchange(other.window_, nullptr)) {}

    GlfwWindow& GlfwWindow::operator=(GlfwWindow&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }

        window_ = std::exchange(other.window_, nullptr);
        return *this;
    }

    GlfwWindow::~GlfwWindow() {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
    }

    Result<GlfwWindow> GlfwWindow::create([[maybe_unused]] const GlfwInstance& instance,
                                          const WindowDesc& desc) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);

        GLFWwindow* window =
            glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
        if (window == nullptr) {
            return std::unexpected{glfwError("Failed to create GLFW window.")};
        }

        return GlfwWindow{window};
    }

    bool GlfwWindow::shouldClose() const {
        return window_ == nullptr || glfwWindowShouldClose(window_) == GLFW_TRUE;
    }

    WindowFramebufferExtent GlfwWindow::framebufferExtent() const {
        if (window_ == nullptr) {
            return {};
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        return WindowFramebufferExtent{
            .width = static_cast<std::uint32_t>(std::max(width, 0)),
            .height = static_cast<std::uint32_t>(std::max(height, 0)),
        };
    }

    void GlfwWindow::pollEvents() {
        glfwPollEvents();
    }

    void GlfwWindow::requestClose() {
        if (window_ != nullptr) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }

    GLFWwindow* GlfwWindow::nativeHandle() const {
        return window_;
    }

} // namespace asharia
