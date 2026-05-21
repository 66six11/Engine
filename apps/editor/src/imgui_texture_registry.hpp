#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"

#include "editor_viewport.hpp"

namespace asharia::editor {

    inline constexpr std::uint32_t kEditorViewportTextureDescriptorBudget = 32;

    struct ImGuiTextureRegistryStats {
        std::uint64_t descriptorsCreated{};
        std::uint64_t descriptorsRetired{};
        std::uint64_t descriptorsRemoved{};
        std::uint64_t activeDescriptors{};
        std::uint64_t retiredDescriptors{};
        std::uint64_t peakLiveDescriptors{};
    };

    struct ImGuiTextureRegistration {
        std::string_view ownerId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        EditorViewportOverlayFlags overlayFlags;
        asharia::VulkanSampledTextureView texture;
        std::uint64_t frameIndex{};
        std::uint64_t submittedFrameEpoch{};
    };

    class ImGuiTextureRegistry {
    public:
        ImGuiTextureRegistry() = default;
        ImGuiTextureRegistry(const ImGuiTextureRegistry&) = delete;
        ImGuiTextureRegistry& operator=(const ImGuiTextureRegistry&) = delete;
        ImGuiTextureRegistry(ImGuiTextureRegistry&&) = delete;
        ImGuiTextureRegistry& operator=(ImGuiTextureRegistry&&) = delete;
        ~ImGuiTextureRegistry();

        [[nodiscard]] asharia::VoidResult create(VkDevice device);
        void beginFrame(std::uint64_t completedFrameEpoch);
        [[nodiscard]] asharia::Result<EditorViewportResult>
        registerOrUpdate(const ImGuiTextureRegistration& registration);
        [[nodiscard]] std::optional<EditorViewportResult>
        acquireForDraw(std::string_view ownerId, std::uint64_t submittedFrameEpoch);
        void retire(std::string_view ownerId, std::uint64_t submittedFrameEpoch);
        void collectGarbage(std::uint64_t completedFrameEpoch);
        void shutdown();

        [[nodiscard]] ImGuiTextureRegistryStats stats() const;
        [[nodiscard]] bool empty() const;

    private:
        struct Entry {
            std::string ownerId;
            EditorViewportKind kind{EditorViewportKind::Scene};
            EditorExtent2D requestedExtent;
            EditorViewportOverlayFlags overlayFlags;
            VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
            VkImageView imageView{VK_NULL_HANDLE};
            VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
            VkFormat format{VK_FORMAT_UNDEFINED};
            EditorExtent2D extent;
            std::uint64_t frameIndex{};
            std::uint64_t lastUsedFrameEpoch{};
            std::uint64_t retireFrameEpoch{};
        };

        [[nodiscard]] std::vector<Entry>::iterator findActive(std::string_view ownerId);
        [[nodiscard]] std::vector<Entry>::const_iterator findActive(std::string_view ownerId) const;
        [[nodiscard]] static bool matches(const Entry& entry,
                                          const asharia::VulkanSampledTextureView& texture);
        [[nodiscard]] static EditorViewportResult makeResult(const Entry& entry);
        void retireEntry(Entry entry, std::uint64_t submittedFrameEpoch);
        void removeEntry(Entry& entry);
        void refreshLiveCounts();

        VkDevice device_{VK_NULL_HANDLE};
        asharia::VulkanSampler sampler_;
        std::vector<Entry> active_;
        std::vector<Entry> retired_;
        ImGuiTextureRegistryStats stats_;
    };

} // namespace asharia::editor
