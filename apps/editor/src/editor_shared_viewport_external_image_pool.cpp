#include "editor_shared_viewport_external_image_pool.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool matchesKey(const EditorSharedViewportExternalImageKey& left,
                                      const EditorSharedViewportExternalImageKey& right) {
            return left.imageHandleFamily == right.imageHandleFamily &&
                   left.format == right.format && left.extent.width == right.extent.width &&
                   left.extent.height == right.extent.height && left.usage == right.usage &&
                   left.aspectMask == right.aspectMask;
        }

        [[nodiscard]] EditorSharedViewportExternalImageKey makeKey(
            EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
            const VulkanExternalImageDesc& desc) {
            return EditorSharedViewportExternalImageKey{
                .imageHandleFamily = imageHandleFamily,
                .format = desc.format,
                .extent = desc.extent,
                .usage = desc.usage,
                .aspectMask = desc.aspectMask,
            };
        }

        [[nodiscard]] Result<void> validateAcquireInputs(
            EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
            const VulkanExternalImageDesc& desc) {
            if (imageHandleFamily != EditorSharedViewportExternalImageHandleFamily::VulkanOpaqueNt) {
                return std::unexpected{
                    vulkanError("Shared viewport external image handle family is unsupported")};
            }

            if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr ||
                desc.format == VK_FORMAT_UNDEFINED || desc.extent.width == 0U ||
                desc.extent.height == 0U || desc.usage == 0U || desc.aspectMask == 0U) {
                return std::unexpected{vulkanError(
                    "Cannot acquire a shared viewport external image from incomplete inputs")};
            }

            return {};
        }

    } // namespace

    struct EditorSharedViewportExternalImagePoolState {
        mutable std::mutex mutex;
        std::vector<EditorSharedViewportExternalImagePoolResource> available;
        EditorSharedViewportExternalImagePoolStats stats;
    };

    EditorSharedViewportExternalImageLease::EditorSharedViewportExternalImageLease(
        std::shared_ptr<EditorSharedViewportExternalImagePoolState> state,
        EditorSharedViewportExternalImagePoolResource resource) noexcept
        : state_{std::move(state)}, resource_{std::move(resource)} {}

    EditorSharedViewportExternalImageLease::EditorSharedViewportExternalImageLease(
        EditorSharedViewportExternalImageLease&& other) noexcept {
        *this = std::move(other);
    }

    EditorSharedViewportExternalImageLease&
    EditorSharedViewportExternalImageLease::operator=(
        EditorSharedViewportExternalImageLease&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        release();
        state_ = std::move(other.state_);
        resource_ = std::move(other.resource_);
        other.resource_.reset();
        return *this;
    }

    EditorSharedViewportExternalImageLease::~EditorSharedViewportExternalImageLease() {
        release();
    }

    VulkanExternalImage& EditorSharedViewportExternalImageLease::image() {
        return resource_->image;
    }

    const VulkanExternalImage& EditorSharedViewportExternalImageLease::image() const {
        return resource_->image;
    }

    bool EditorSharedViewportExternalImageLease::hasImage() const {
        return resource_.has_value();
    }

    void EditorSharedViewportExternalImageLease::release() noexcept {
        if (!state_ || !resource_) {
            return;
        }

        std::lock_guard lock{state_->mutex};
        ++state_->stats.released;
        if (state_->stats.leased > 0U) {
            --state_->stats.leased;
        }

        try {
            state_->available.push_back(std::move(*resource_));
            state_->stats.available = static_cast<std::uint64_t>(state_->available.size());
        } catch (const std::bad_alloc&) {
            logError("Shared viewport external image pool dropped an image during release.");
            state_->stats.available = static_cast<std::uint64_t>(state_->available.size());
        }

        resource_.reset();
        state_.reset();
    }

    EditorSharedViewportExternalImagePool::EditorSharedViewportExternalImagePool()
        : state_{std::make_shared<EditorSharedViewportExternalImagePoolState>()} {}

    Result<EditorSharedViewportExternalImageLease>
    EditorSharedViewportExternalImagePool::acquire(
        EditorSharedViewportExternalImageHandleFamily imageHandleFamily,
        const VulkanExternalImageDesc& desc) {
        if (!state_) {
            return std::unexpected{
                vulkanError("Cannot acquire a shared viewport external image from a moved pool")};
        }

        auto validated = validateAcquireInputs(imageHandleFamily, desc);
        if (!validated) {
            return std::unexpected{std::move(validated.error())};
        }

        const EditorSharedViewportExternalImageKey key = makeKey(imageHandleFamily, desc);

        {
            std::lock_guard lock{state_->mutex};
            auto iter =
                std::find_if(state_->available.begin(), state_->available.end(),
                             [&key](const EditorSharedViewportExternalImagePoolResource& resource) {
                                 return matchesKey(resource.key, key);
                             });
            if (iter != state_->available.end()) {
                EditorSharedViewportExternalImagePoolResource resource = std::move(*iter);
                state_->available.erase(iter);
                ++state_->stats.acquired;
                ++state_->stats.reused;
                ++state_->stats.leased;
                state_->stats.available = static_cast<std::uint64_t>(state_->available.size());
                return EditorSharedViewportExternalImageLease{state_, std::move(resource)};
            }
        }

        auto image = VulkanExternalImage::create(desc);
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        EditorSharedViewportExternalImagePoolResource resource{
            .key = key,
            .image = std::move(*image),
        };

        {
            std::lock_guard lock{state_->mutex};
            ++state_->stats.acquired;
            ++state_->stats.created;
            ++state_->stats.leased;
            state_->stats.available = static_cast<std::uint64_t>(state_->available.size());
        }

        return EditorSharedViewportExternalImageLease{state_, std::move(resource)};
    }

    EditorSharedViewportExternalImagePoolStats
    EditorSharedViewportExternalImagePool::stats() const {
        if (!state_) {
            return {};
        }

        std::lock_guard lock{state_->mutex};
        EditorSharedViewportExternalImagePoolStats snapshot = state_->stats;
        snapshot.available = static_cast<std::uint64_t>(state_->available.size());
        return snapshot;
    }

} // namespace asharia::editor
