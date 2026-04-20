#pragma once

#include "vke/core/result.hpp"

#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace vke {

struct WindowDesc {
    int width{1280};
    int height{720};
    std::string title{"VkEngine"};
    bool visible{true};
};

class GlfwInstance {
public:
    GlfwInstance() = default;
    GlfwInstance(const GlfwInstance&) = delete;
    GlfwInstance& operator=(const GlfwInstance&) = delete;
    GlfwInstance(GlfwInstance&& other) noexcept;
    GlfwInstance& operator=(GlfwInstance&& other) noexcept;
    ~GlfwInstance();

    [[nodiscard]] static Result<GlfwInstance> create();

private:
    explicit GlfwInstance(bool initialized);

    bool initialized_{false};
};

class GlfwWindow {
public:
    GlfwWindow() = default;
    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
    GlfwWindow(GlfwWindow&& other) noexcept;
    GlfwWindow& operator=(GlfwWindow&& other) noexcept;
    ~GlfwWindow();

    [[nodiscard]] static Result<GlfwWindow> create(const GlfwInstance& instance, const WindowDesc& desc);

    [[nodiscard]] bool shouldClose() const;
    void pollEvents() const;
    void requestClose();

    [[nodiscard]] GLFWwindow* nativeHandle() const;

private:
    explicit GlfwWindow(GLFWwindow* window);

    GLFWwindow* window_{nullptr};
};

[[nodiscard]] std::string glfwLastErrorMessage(std::string_view fallback);
[[nodiscard]] Result<std::vector<std::string>> glfwRequiredVulkanInstanceExtensions(const GlfwInstance& instance);

} // namespace vke
