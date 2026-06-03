#include "editor_render_runtime.hpp"

#include <filesystem>
#include <memory>
#include <utility>

#include "asharia/renderer_basic_vulkan/fullscreen_texture_renderer.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"

#include "editor_viewport_coordinator.hpp"
#include "imgui_runtime.hpp"

namespace asharia::editor {

    struct EditorRenderRuntime::Impl {
        ImGuiRuntime imgui;
        asharia::BasicFullscreenTextureRenderer renderer;
        EditorViewportCoordinator viewportCoordinator;
    };

    EditorRenderRuntime::EditorRenderRuntime() : impl_(std::make_unique<Impl>()) {}

    EditorRenderRuntime::~EditorRenderRuntime() = default;

    [[nodiscard]] asharia::VoidResult
    EditorRenderRuntime::create(GLFWwindow* window, const asharia::VulkanContext& context,
                                const asharia::VulkanFrameLoop& frameLoop,
                                const ImGuiRuntimeDesc& imguiDesc) {
        if (auto created = impl_->imgui.create(window, context, frameLoop, imguiDesc); !created) {
            return std::unexpected{std::move(created.error())};
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto createdRenderer = asharia::BasicFullscreenTextureRenderer::create(
            asharia::BasicFullscreenTextureRendererDesc{
                .device = context.device(),
                .allocator = context.allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!createdRenderer) {
            return std::unexpected{std::move(createdRenderer.error())};
        }
        impl_->renderer = std::move(*createdRenderer);

        if (auto created = impl_->viewportCoordinator.create(context); !created) {
            return std::unexpected{std::move(created.error())};
        }

        return {};
    }

    ImGuiRuntime& EditorRenderRuntime::imgui() {
        return impl_->imgui;
    }

    const ImGuiRuntime& EditorRenderRuntime::imgui() const {
        return impl_->imgui;
    }

    asharia::BasicFullscreenTextureRenderer& EditorRenderRuntime::renderer() {
        return impl_->renderer;
    }

    EditorViewportCoordinator& EditorRenderRuntime::viewportCoordinator() {
        return impl_->viewportCoordinator;
    }

    const EditorViewportCoordinator& EditorRenderRuntime::viewportCoordinator() const {
        return impl_->viewportCoordinator;
    }

} // namespace asharia::editor
