#pragma once

#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/rendergraph/render_graph_diagnostics.hpp"
#include "asharia/resource_runtime/mesh_resource_store.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"

namespace asharia {
    enum class BasicGpuMeshError : int {
        InvalidInput = 1,
        WrongThread,
        StaleRevision,
        BudgetExceeded,
        NotReady,
        InvalidSubmission,
        IncompatibleDraw,
    };

    // Immutable after publication. References must be released before device/allocator teardown.
    class BasicGpuMesh final {
    public:
        [[nodiscard]] BasicDrawResourceKey key() const noexcept {
            return key_;
        }
        [[nodiscard]] std::uint64_t revision() const noexcept {
            return lease_.revision();
        }
        [[nodiscard]] std::uint64_t productHash() const noexcept {
            return lease_.productHash();
        }
        [[nodiscard]] const mesh::MeshProductV1& product() const noexcept {
            return lease_.product();
        }
        [[nodiscard]] VoidResult validate(std::span<const BasicDrawListItem> items) const;

    private:
        friend class BasicGpuMeshOwner;
        friend class BasicFullscreenTextureRenderer;
        BasicGpuMesh(resource::MeshResourceLease lease, BasicDrawResourceKey key);
        resource::MeshResourceLease lease_;
        BasicDrawResourceKey key_{};
        VkDevice device_{VK_NULL_HANDLE};
        VulkanBuffer vertices_;
        VulkanBuffer indices_;
    };

    struct BasicGpuMeshOwnerDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        BasicDrawResourceKey key{};
        std::uint64_t maxResidentBytes{64ULL * 1024ULL * 1024ULL};
        std::size_t maxResidentVersions{4U};
    };

    struct BasicGpuMeshStats {
        std::size_t residentVersions{};
        std::uint64_t residentBytes{};
        std::uint64_t uploadsRecorded{};
        std::uint64_t published{};
        bool pending{};
    };

    // One logical mesh, one candidate, render-thread-only. Host owns submission and completion.
    class BasicGpuMeshOwner final {
    public:
        BasicGpuMeshOwner(const BasicGpuMeshOwner&) = delete;
        BasicGpuMeshOwner& operator=(const BasicGpuMeshOwner&) = delete;
        BasicGpuMeshOwner(BasicGpuMeshOwner&&) noexcept = default;
        BasicGpuMeshOwner& operator=(BasicGpuMeshOwner&&) noexcept = default;
        [[nodiscard]] static Result<BasicGpuMeshOwner> create(BasicGpuMeshOwnerDesc desc);
        [[nodiscard]] VoidResult queue(resource::MeshResourceLease lease);
        [[nodiscard]] VoidResult
        recordUpload(const VulkanFrameRecordContext& frame,
                     RenderGraphDiagnosticsSnapshot* diagnostics = nullptr);
        // Call immediately after a successful renderFrame containing recordUpload, before another
        // frame.
        [[nodiscard]] VoidResult confirmUploadSubmission(const VulkanFrameLoop& frameLoop);
        [[nodiscard]] VoidResult publishCompleted(const VulkanFrameLoop& frameLoop);
        [[nodiscard]] Result<std::shared_ptr<const BasicGpuMesh>> acquire() const;
        [[nodiscard]] VoidResult cancelUpload();
        [[nodiscard]] VoidResult clear();
        [[nodiscard]] BasicGpuMeshStats stats() const;

    private:
        struct Upload {
            std::shared_ptr<BasicGpuMesh> mesh;
            VulkanBuffer vertexStaging;
            VulkanBuffer indexStaging;
            const VulkanFrameLoop* frameLoop{};
            std::uint64_t epoch{};
            bool recorded{};
            bool recordingSucceeded{};
            bool confirmed{};
        };
        struct Resident {
            std::weak_ptr<const BasicGpuMesh> mesh;
            std::weak_ptr<Upload> upload;
            std::uint64_t bytes{};
        };
        explicit BasicGpuMeshOwner(BasicGpuMeshOwnerDesc desc);
        [[nodiscard]] VoidResult requireOwnerThread() const;
        BasicGpuMeshOwnerDesc desc_;
        std::thread::id thread_;
        std::optional<resource::MeshResourceHandle> handle_;
        std::uint64_t highestRevision_{};
        std::shared_ptr<const BasicGpuMesh> active_;
        std::shared_ptr<Upload> pending_;
        std::vector<Resident> residents_;
        std::uint64_t uploadsRecorded_{};
        std::uint64_t published_{};
    };
} // namespace asharia
