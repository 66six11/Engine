#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "asharia/asset_artifact/asset_artifact_v1.hpp"
#include "asharia/asset_core/asset_handle.hpp"
#include "asharia/asset_core/asset_product.hpp"
#include "asharia/core/result.hpp"
#include "asharia/mesh_product/mesh_product_v1.hpp"

namespace asharia::resource {

    enum class MeshResourceDiagnosticCode : int {
        InvalidDescriptor = 1,
        WrongOwnerThread = 2,
        InvalidResourceKey = 3,
        InvalidProductKey = 4,
        ProductTypeMismatch = 5,
        InvalidHandle = 6,
        MissingResource = 7,
        SlotGenerationMismatch = 8,
        RequestGenerationMismatch = 9,
        ResourceNotPending = 10,
        InvalidCompletion = 11,
        NoActiveResource = 12,
    };

    enum class MeshResourceFailureReason : std::uint8_t {
        MissingProduct,
        StaleProduct,
        InvalidProductRecord,
        ArtifactReadFailed,
        UnsupportedProduct,
        RuntimeCreationFailed,
    };

    enum class MeshResourceRequestDisposition : std::uint8_t {
        LoadQueued,
        AlreadyReady,
        AlreadyPending,
        FailedNoActive,
        KeptActiveAfterFailure,
    };

    enum class MeshResourceState : std::uint8_t {
        FailedNoActive,
        Pending,
        Ready,
        ReloadPending,
    };

    struct MeshResourceKey {
        asset::AssetGuid guid{};
        asset::AssetTypeId assetType{};

        [[nodiscard]] friend bool operator==(MeshResourceKey, MeshResourceKey) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(guid) && static_cast<bool>(assetType);
        }
    };

    template <class T>
    [[nodiscard]] MeshResourceKey makeMeshResourceKey(asset::AssetHandle<T> handle) noexcept {
        return MeshResourceKey{.guid = handle.guid,
                               .assetType = asset::makeAssetTypeId(mesh::kMeshAssetTypeName)};
    }

    struct MeshResourceHandle {
        std::uint32_t slot{};
        std::uint32_t slotGeneration{};

        [[nodiscard]] friend bool operator==(MeshResourceHandle, MeshResourceHandle) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return slot != 0U && slotGeneration != 0U;
        }
    };

    struct MeshResourceLoadTicket {
        MeshResourceHandle handle{};
        std::uint64_t requestGeneration{};
        std::uint64_t expectedProductHash{};

        [[nodiscard]] friend bool operator==(MeshResourceLoadTicket,
                                             MeshResourceLoadTicket) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(handle) && requestGeneration != 0U &&
                   expectedProductHash != 0U;
        }
    };

    struct MeshResourceFailure {
        MeshResourceFailureReason reason{MeshResourceFailureReason::MissingProduct};
        std::string message;

        [[nodiscard]] friend bool operator==(const MeshResourceFailure&,
                                             const MeshResourceFailure&) = default;
    };

    struct MeshResourceLoadPlan {
        MeshResourceLoadTicket ticket{};
        std::uint64_t selectionHash{};
        asset::AssetArtifactLocatorV1 artifact;
        std::filesystem::path artifactRoot;
        asset::AssetArtifactReadLimits artifactLimits;
        mesh::MeshProductReadLimits meshLimits;
    };

    struct MeshResourceLoadSuccess {
        std::shared_ptr<const mesh::MeshProductV1> payload;
    };

    struct MeshResourceLoadCompletion {
        MeshResourceLoadTicket ticket{};
        std::uint64_t selectionHash{};
        std::uint64_t productHash{};
        std::variant<MeshResourceLoadSuccess, MeshResourceFailure> outcome;
    };

    struct MeshResourceRequestResult {
        MeshResourceHandle handle{};
        MeshResourceRequestDisposition disposition{MeshResourceRequestDisposition::FailedNoActive};
        std::optional<MeshResourceLoadPlan> loadPlan;
        std::optional<MeshResourceFailure> failure;
    };

    struct MeshResourceSnapshot {
        MeshResourceHandle handle{};
        MeshResourceKey key{};
        MeshResourceState state{MeshResourceState::FailedNoActive};
        std::uint64_t activeRevision{};
        std::uint64_t activeProductHash{};
        std::uint64_t pendingRequestGeneration{};
        std::optional<MeshResourceFailure> lastFailure;
    };

    class MeshResourceLease final {
    public:
        MeshResourceLease(const MeshResourceLease&) = default;
        MeshResourceLease& operator=(const MeshResourceLease&) = default;
        MeshResourceLease(MeshResourceLease&&) noexcept = default;
        MeshResourceLease& operator=(MeshResourceLease&&) noexcept = default;
        ~MeshResourceLease() = default;

        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] MeshResourceHandle handle() const noexcept;
        [[nodiscard]] std::uint64_t revision() const noexcept;
        [[nodiscard]] std::uint64_t productHash() const noexcept;
        [[nodiscard]] const mesh::MeshProductV1& product() const noexcept;

    private:
        friend class MeshResourceStore;

        struct Init {
            MeshResourceHandle handle{};
            std::uint64_t revision{};
            std::uint64_t productHash{};
            std::shared_ptr<const mesh::MeshProductV1> payload;
        };

        explicit MeshResourceLease(Init init);

        MeshResourceHandle handle_{};
        std::uint64_t revision_{};
        std::uint64_t productHash_{};
        std::shared_ptr<const mesh::MeshProductV1> payload_;
    };

    struct MeshResourceStoreDesc {
        std::filesystem::path artifactRoot;
        asset::AssetArtifactReadLimits artifactLimits;
        mesh::MeshProductReadLimits meshLimits;
    };

    class MeshResourceStore final {
    public:
        [[nodiscard]] static Result<MeshResourceStore> create(MeshResourceStoreDesc desc);

        MeshResourceStore(const MeshResourceStore&) = delete;
        MeshResourceStore& operator=(const MeshResourceStore&) = delete;
        MeshResourceStore(MeshResourceStore&&) noexcept = default;
        MeshResourceStore& operator=(MeshResourceStore&&) noexcept = default;
        ~MeshResourceStore() = default;

        [[nodiscard]] Result<MeshResourceRequestResult>
        request(MeshResourceKey key, asset::AssetProductKey expectedProductKey,
                std::span<const asset::AssetProductRecord> products);

        [[nodiscard]] Result<MeshResourceSnapshot> publish(MeshResourceLoadCompletion completion);

        [[nodiscard]] Result<MeshResourceLease> acquire(MeshResourceHandle handle) const;
        [[nodiscard]] Result<MeshResourceSnapshot> inspect(MeshResourceHandle handle) const;
        [[nodiscard]] VoidResult unload(MeshResourceHandle handle);

    private:
        struct ActiveRevision {
            std::uint64_t revision{};
            std::uint64_t selectionHash{};
            std::uint64_t productHash{};
            std::shared_ptr<const mesh::MeshProductV1> payload;
        };

        struct Candidate {
            MeshResourceLoadTicket ticket{};
            std::uint64_t selectionHash{};
            std::uint64_t productHash{};
        };

        struct Slot {
            bool occupied{};
            std::uint32_t generation{1U};
            MeshResourceKey key{};
            std::uint64_t nextRequestGeneration{};
            std::optional<ActiveRevision> active;
            std::optional<Candidate> candidate;
            std::optional<MeshResourceFailure> lastFailure;
        };

        explicit MeshResourceStore(MeshResourceStoreDesc desc);

        [[nodiscard]] VoidResult requireOwnerThread() const;
        [[nodiscard]] Result<std::size_t> resolveSlotIndex(MeshResourceHandle handle) const;
        [[nodiscard]] std::optional<std::size_t> findSlotIndex(MeshResourceKey key) const noexcept;
        [[nodiscard]] std::size_t allocateSlot(MeshResourceKey key);
        [[nodiscard]] MeshResourceSnapshot makeSnapshot(std::size_t slotIndex) const;

        MeshResourceStoreDesc desc_;
        std::thread::id ownerThread_;
        std::vector<Slot> slots_;
        std::vector<std::size_t> freeSlots_;
        std::uint64_t nextRevision_{};
    };

    [[nodiscard]] MeshResourceLoadCompletion
    loadMeshResourceCandidate(const MeshResourceLoadPlan& plan);

    [[nodiscard]] const char*
    meshResourceDiagnosticCodeName(MeshResourceDiagnosticCode code) noexcept;
    [[nodiscard]] const char*
    meshResourceFailureReasonName(MeshResourceFailureReason reason) noexcept;
    [[nodiscard]] const char*
    meshResourceRequestDispositionName(MeshResourceRequestDisposition disposition) noexcept;
    [[nodiscard]] const char* meshResourceStateName(MeshResourceState state) noexcept;

} // namespace asharia::resource
