#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <span>

#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"

namespace asharia {

    struct BasicTriangleRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
        BasicMeshKind meshKind{BasicMeshKind::Triangle};
        BasicDrawItem drawItem{basicTriangleDrawItem()};
    };

    struct BasicDescriptorLayoutSmokeDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
    };

    struct BasicFullscreenTextureRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
    };

    struct BasicMrtRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
    };

    struct BasicMesh3DRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
    };

    struct BasicDrawListRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
        std::span<const BasicDrawListItem> drawItems{};
    };

    struct BasicComputeDispatchRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
        bool graphicsQueueSupportsCompute{};
    };

} // namespace asharia