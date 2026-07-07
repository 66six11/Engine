#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"

#include "editor_shared_viewport_external_image_handle_family.hpp"

namespace asharia::editor {

    struct EditorSharedViewportExternalImageKey {
        EditorSharedViewportExternalImageHandleFamily imageHandleFamily{
            EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    struct EditorSharedViewportExternalImagePoolResource {
        EditorSharedViewportExternalImageKey key;
        VulkanExternalImage image;
    };

    struct EditorSharedViewportExternalImagePoolState;

    struct EditorSharedViewportExternalImagePoolStats {
        std::uint64_t acquired{};
        std::uint64_t created{};
        std::uint64_t reused{};
        std::uint64_t released{};
        std::uint64_t available{};
        std::uint64_t leased{};
    };

    class EditorSharedViewportExternalImagePool;

    class EditorSharedViewportExternalImageLease final {
    public:
        EditorSharedViewportExternalImageLease() = default;
        EditorSharedViewportExternalImageLease(
            const EditorSharedViewportExternalImageLease&) = delete;
        EditorSharedViewportExternalImageLease&
        operator=(const EditorSharedViewportExternalImageLease&) = delete;
        EditorSharedViewportExternalImageLease(
            EditorSharedViewportExternalImageLease&& other) noexcept;
        EditorSharedViewportExternalImageLease&
        operator=(EditorSharedViewportExternalImageLease&& other) noexcept;
        ~EditorSharedViewportExternalImageLease();

        [[nodiscard]] VulkanExternalImage& image();
        [[nodiscard]] const VulkanExternalImage& image() const;
        [[nodiscard]] bool hasImage() const;

    private:
        friend class EditorSharedViewportExternalImagePool;

        EditorSharedViewportExternalImageLease(
            std::shared_ptr<EditorSharedViewportExternalImagePoolState> state,
            EditorSharedViewportExternalImagePoolResource resource) noexcept;

        void release() noexcept;

        std::shared_ptr<EditorSharedViewportExternalImagePoolState> state_;
        std::optional<EditorSharedViewportExternalImagePoolResource> resource_;
    };

    class EditorSharedViewportExternalImagePool final {
    public:
        EditorSharedViewportExternalImagePool();
        EditorSharedViewportExternalImagePool(
            const EditorSharedViewportExternalImagePool&) = delete;
        EditorSharedViewportExternalImagePool&
        operator=(const EditorSharedViewportExternalImagePool&) = delete;
        EditorSharedViewportExternalImagePool(
            EditorSharedViewportExternalImagePool&&) noexcept = default;
        EditorSharedViewportExternalImagePool&
        operator=(EditorSharedViewportExternalImagePool&&) noexcept = default;
        ~EditorSharedViewportExternalImagePool() = default;

        [[nodiscard]] Result<EditorSharedViewportExternalImageLease>
        acquire(EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
                const VulkanExternalImageDesc& desc);

        [[nodiscard]] EditorSharedViewportExternalImagePoolStats stats() const;

    private:
        std::shared_ptr<EditorSharedViewportExternalImagePoolState> state_;
    };

} // namespace asharia::editor
