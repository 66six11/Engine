#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_viewport.hpp"

namespace asharia {
    class BasicFullscreenTextureRenderer;
}

namespace asharia::editor {
    class EditorFrameDebugger;
    class EditorViewportCoordinator;

    [[nodiscard]] EditorExtent2D editorExtentFromVk(VkExtent2D extent);
    [[nodiscard]] VoidResult waitForRenderableEditorWindow(GlfwWindow& window, bool smokeMode);
    [[nodiscard]] Result<VulkanContext>
    createEditorVulkanContext(const std::vector<std::string>& extensions, GlfwWindow& window);
    [[nodiscard]] Result<VulkanFrameLoop> createEditorFrameLoop(const VulkanContext& context,
                                                                const GlfwWindow& window);
    [[nodiscard]] Result<bool> prepareEditorFrameLoopExtent(GlfwWindow& window,
                                                            VulkanFrameLoop& frameLoop);
    [[nodiscard]] Result<bool> renderEditorFrame(VulkanFrameLoop& frameLoop,
                                                 BasicFullscreenTextureRenderer& renderer,
                                                 EditorViewportCoordinator& viewportHost,
                                                 EditorFrameDebugger& frameDebugger,
                                                 int frameIndex);

} // namespace asharia::editor
