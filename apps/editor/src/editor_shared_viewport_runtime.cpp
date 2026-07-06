#include "editor_shared_viewport_runtime.hpp"

#include <expected>
#include <mutex>
#include <utility>

namespace asharia::editor {

    EditorSharedViewportRuntime& EditorSharedViewportRuntime::instance() {
        static EditorSharedViewportRuntime runtime;
        return runtime;
    }

    asharia::Result<const asharia::VulkanContext*> EditorSharedViewportRuntime::ensureContext() {
        std::lock_guard lock{mutex_};
        if (!context_) {
            auto context = asharia::VulkanContext::create(asharia::VulkanContextDesc{
                .applicationName = "Asharia Studio Shared Viewport",
                .requiredInstanceExtensions = {},
                .createSurface = {},
                .enableValidation = true,
                .debugLabels = asharia::VulkanDebugLabelMode::Optional,
                .requireVulkan14 = true,
            });
            if (!context) {
                return std::unexpected{std::move(context.error())};
            }

            context_.emplace(std::move(*context));
        }

        return &*context_;
    }

    void EditorSharedViewportRuntime::shutdown() {
        std::lock_guard lock{mutex_};
        context_.reset();
    }

} // namespace asharia::editor
