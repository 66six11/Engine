#include "imgui_texture_registry.hpp"

#include <algorithm>
#include <imgui_impl_vulkan.h>
#include <iterator>
#include <utility>

#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace {

    std::uintptr_t textureId(VkDescriptorSet descriptorSet) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<std::uintptr_t>(descriptorSet);
    }

    asharia::editor::EditorExtent2D editorExtentFromVk(VkExtent2D extent) {
        return asharia::editor::EditorExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    bool isCompleteTexture(const asharia::VulkanSampledTextureView& texture) {
        return texture.imageView != VK_NULL_HANDLE &&
               texture.sampledLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
               texture.format != VK_FORMAT_UNDEFINED && texture.extent.width > 0 &&
               texture.extent.height > 0;
    }

} // namespace

namespace asharia::editor {

    ImGuiTextureRegistry::~ImGuiTextureRegistry() {
        shutdown();
    }

    asharia::VoidResult ImGuiTextureRegistry::create(VkDevice device) {
        if (device == VK_NULL_HANDLE) {
            return std::unexpected{
                asharia::vulkanError("Cannot create an ImGui texture registry without a device")};
        }

        device_ = device;
        auto sampler = asharia::VulkanSampler::create(asharia::VulkanSamplerDesc{
            .device = device_,
        });
        if (!sampler) {
            return std::unexpected{std::move(sampler.error())};
        }
        sampler_ = std::move(*sampler);
        return {};
    }

    void ImGuiTextureRegistry::beginFrame(std::uint64_t completedFrameEpoch) {
        collectGarbage(completedFrameEpoch);
    }

    asharia::Result<EditorViewportResult>
    ImGuiTextureRegistry::registerOrUpdate(const ImGuiTextureRegistration& registration) {
        if (registration.ownerId.empty()) {
            return std::unexpected{
                asharia::vulkanError("Cannot register an ImGui texture without an owner id")};
        }
        if (!isRenderableEditorExtent(registration.requestedExtent) ||
            !isCompleteTexture(registration.texture)) {
            return std::unexpected{
                asharia::vulkanError("Cannot register an incomplete ImGui texture")};
        }
        if (sampler_.handle() == VK_NULL_HANDLE) {
            return std::unexpected{
                asharia::vulkanError("Cannot register an ImGui texture without a sampler")};
        }

        auto active = findActive(registration.ownerId);
        if (active != active_.end() && matches(*active, registration.texture)) {
            active->kind = registration.kind;
            active->requestedExtent = registration.requestedExtent;
            active->overlayFlags =
                effectiveEditorViewportOverlayFlags(registration.kind, registration.overlayFlags);
            return makeResult(*active);
        }

        VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
            sampler_.handle(), registration.texture.imageView, registration.texture.sampledLayout);
        if (descriptorSet == VK_NULL_HANDLE) {
            return std::unexpected{
                asharia::vulkanError("Failed to register editor texture with ImGui")};
        }

        Entry replacement{
            .ownerId = std::string{registration.ownerId},
            .kind = registration.kind,
            .requestedExtent = registration.requestedExtent,
            .overlayFlags =
                effectiveEditorViewportOverlayFlags(registration.kind, registration.overlayFlags),
            .descriptorSet = descriptorSet,
            .imageView = registration.texture.imageView,
            .layout = registration.texture.sampledLayout,
            .format = registration.texture.format,
            .extent = editorExtentFromVk(registration.texture.extent),
        };
        ++stats_.descriptorsCreated;

        if (active != active_.end()) {
            retireEntry(std::move(*active), registration.submittedFrameEpoch);
            *active = std::move(replacement);
        } else {
            active_.push_back(std::move(replacement));
            active = std::prev(active_.end());
        }
        refreshLiveCounts();
        return makeResult(*active);
    }

    std::optional<EditorViewportResult>
    ImGuiTextureRegistry::acquireForDraw(std::string_view ownerId,
                                         std::uint64_t submittedFrameEpoch) {
        auto active = findActive(ownerId);
        if (active == active_.end()) {
            return std::nullopt;
        }

        active->lastUsedFrameEpoch = std::max(active->lastUsedFrameEpoch, submittedFrameEpoch);
        return makeResult(*active);
    }

    void ImGuiTextureRegistry::retire(std::string_view ownerId, std::uint64_t submittedFrameEpoch) {
        auto active = findActive(ownerId);
        if (active == active_.end()) {
            return;
        }

        retireEntry(std::move(*active), submittedFrameEpoch);
        active_.erase(active);
        refreshLiveCounts();
    }

    void ImGuiTextureRegistry::collectGarbage(std::uint64_t completedFrameEpoch) {
        auto removed = std::ranges::remove_if(retired_, [&](Entry& entry) {
            if (entry.retireFrameEpoch > completedFrameEpoch) {
                return false;
            }
            removeEntry(entry);
            return true;
        });
        retired_.erase(removed.begin(), removed.end());
        refreshLiveCounts();
    }

    void ImGuiTextureRegistry::shutdown() {
        for (Entry& entry : active_) {
            removeEntry(entry);
        }
        for (Entry& entry : retired_) {
            removeEntry(entry);
        }
        active_.clear();
        retired_.clear();
        sampler_ = {};
        device_ = VK_NULL_HANDLE;
        refreshLiveCounts();
    }

    ImGuiTextureRegistryStats ImGuiTextureRegistry::stats() const {
        return stats_;
    }

    bool ImGuiTextureRegistry::empty() const {
        return active_.empty() && retired_.empty();
    }

    std::vector<ImGuiTextureRegistry::Entry>::iterator
    ImGuiTextureRegistry::findActive(std::string_view ownerId) {
        return std::ranges::find_if(
            active_, [ownerId](const Entry& entry) { return entry.ownerId == ownerId; });
    }

    std::vector<ImGuiTextureRegistry::Entry>::const_iterator
    ImGuiTextureRegistry::findActive(std::string_view ownerId) const {
        return std::ranges::find_if(
            active_, [ownerId](const Entry& entry) { return entry.ownerId == ownerId; });
    }

    bool ImGuiTextureRegistry::matches(const Entry& entry,
                                       const asharia::VulkanSampledTextureView& texture) {
        return entry.descriptorSet != VK_NULL_HANDLE && entry.imageView == texture.imageView &&
               entry.layout == texture.sampledLayout && entry.format == texture.format &&
               entry.extent.width == texture.extent.width &&
               entry.extent.height == texture.extent.height;
    }

    EditorViewportResult ImGuiTextureRegistry::makeResult(const Entry& entry) {
        return EditorViewportResult{
            .panelId = EditorId{.value = entry.ownerId},
            .kind = entry.kind,
            .requestedExtent = entry.requestedExtent,
            .texture =
                EditorViewportTexture{
                    .textureId = textureId(entry.descriptorSet),
                    .extent = entry.extent,
                },
            .overlayFlags = entry.overlayFlags,
        };
    }

    void ImGuiTextureRegistry::retireEntry(Entry entry, std::uint64_t submittedFrameEpoch) {
        if (entry.descriptorSet == VK_NULL_HANDLE) {
            return;
        }

        entry.retireFrameEpoch = std::max(entry.lastUsedFrameEpoch, submittedFrameEpoch);
        retired_.push_back(std::move(entry));
        ++stats_.descriptorsRetired;
        refreshLiveCounts();
    }

    void ImGuiTextureRegistry::removeEntry(Entry& entry) {
        if (entry.descriptorSet == VK_NULL_HANDLE) {
            return;
        }

        ImGui_ImplVulkan_RemoveTexture(entry.descriptorSet);
        entry.descriptorSet = VK_NULL_HANDLE;
        ++stats_.descriptorsRemoved;
    }

    void ImGuiTextureRegistry::refreshLiveCounts() {
        stats_.activeDescriptors = active_.size();
        stats_.retiredDescriptors = retired_.size();
        stats_.peakLiveDescriptors = std::max(stats_.peakLiveDescriptors,
                                              stats_.activeDescriptors + stats_.retiredDescriptors);
    }

} // namespace asharia::editor
