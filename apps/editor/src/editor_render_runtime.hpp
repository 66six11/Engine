#pragma once

#include <memory>

#include "asharia/core/result.hpp"

struct GLFWwindow;

namespace asharia {
    class BasicFullscreenTextureRenderer;
    class VulkanContext;
    class VulkanFrameLoop;
} // namespace asharia

namespace asharia::editor {
    class EditorViewportCoordinator;
    class ImGuiRuntime;
    struct ImGuiRuntimeDesc;

    class EditorRenderRuntime {
    public:
        EditorRenderRuntime();
        EditorRenderRuntime(const EditorRenderRuntime&) = delete;
        EditorRenderRuntime& operator=(const EditorRenderRuntime&) = delete;
        EditorRenderRuntime(EditorRenderRuntime&&) = delete;
        EditorRenderRuntime& operator=(EditorRenderRuntime&&) = delete;
        ~EditorRenderRuntime();

        [[nodiscard]] asharia::VoidResult create(GLFWwindow* window,
                                                 const asharia::VulkanContext& context,
                                                 const asharia::VulkanFrameLoop& frameLoop,
                                                 const ImGuiRuntimeDesc& imguiDesc);

        [[nodiscard]] ImGuiRuntime& imgui();
        [[nodiscard]] const ImGuiRuntime& imgui() const;
        [[nodiscard]] asharia::BasicFullscreenTextureRenderer& renderer();
        [[nodiscard]] EditorViewportCoordinator& viewportCoordinator();
        [[nodiscard]] const EditorViewportCoordinator& viewportCoordinator() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace asharia::editor
