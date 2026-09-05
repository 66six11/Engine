#include "asharia/resource_runtime/mesh_resource_store.hpp"

#include <algorithm>
#include <expected>
#include <limits>
#include <string>
#include <utility>

namespace asharia::resource {
    struct MeshResourceStoreIdentity {};

    namespace {

        [[nodiscard]] Error resourceError(MeshResourceDiagnosticCode code, std::string message) {
            return Error{ErrorDomain::Asset, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] std::string keyLabel(MeshResourceKey key) {
            return "guid=\"" + asset::formatAssetGuid(key.guid) +
                   "\" assetType=" + std::to_string(key.assetType.value);
        }

        [[nodiscard]] bool productKeyMatches(MeshResourceKey key,
                                             const asset::AssetProductKey& productKey) noexcept {
            return productKey.guid == key.guid && productKey.assetType == key.assetType;
        }

        template <class T> [[nodiscard]] T nextNonzero(T& value) noexcept {
            ++value;
            if (value == 0U) {
                ++value;
            }
            return value;
        }

        [[nodiscard]] MeshResourceFailure selectionFailure(MeshResourceFailureReason reason,
                                                           MeshResourceKey key,
                                                           std::string message) {
            return MeshResourceFailure{
                .reason = reason,
                .message =
                    "Mesh resource selection for " + keyLabel(key) + " " + std::move(message) + ".",
            };
        }

    } // namespace

    MeshResourceLease::MeshResourceLease(Init init)
        : handle_(init.handle), revision_(init.revision), productHash_(init.productHash),
          payload_(std::move(init.payload)) {}

    MeshResourceLease::operator bool() const noexcept {
        return static_cast<bool>(handle_) && revision_ != 0U && productHash_ != 0U &&
               static_cast<bool>(payload_);
    }

    MeshResourceHandle MeshResourceLease::handle() const noexcept {
        return handle_;
    }

    std::uint64_t MeshResourceLease::revision() const noexcept {
        return revision_;
    }

    std::uint64_t MeshResourceLease::productHash() const noexcept {
        return productHash_;
    }

    const mesh::MeshProductV1& MeshResourceLease::product() const noexcept {
        return *payload_;
    }

    Result<MeshResourceStore> MeshResourceStore::create(MeshResourceStoreDesc desc) {
        if (desc.artifactRoot.empty()) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidDescriptor,
                              "Mesh resource store rejected an empty artifact root.")};
        }
        if (desc.artifactLimits.maxBytes == 0U || desc.meshLimits.maxProductBytes == 0U ||
            desc.meshLimits.maxVertices == 0U || desc.meshLimits.maxIndices == 0U ||
            desc.meshLimits.maxSubmeshes == 0U || desc.meshLimits.maxMaterialSlots == 0U) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidDescriptor,
                              "Mesh resource store rejected zero artifact or mesh read limits.")};
        }
        return MeshResourceStore{std::move(desc)};
    }

    MeshResourceStore::MeshResourceStore(MeshResourceStoreDesc desc)
        : desc_(std::move(desc)), identity_(std::make_shared<const MeshResourceStoreIdentity>()),
          ownerThread_(std::this_thread::get_id()) {}

    VoidResult MeshResourceStore::requireOwnerThread() const {
        if (!identity_) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidDescriptor,
                              "Mesh resource operation rejected a moved-from store.")};
        }
        if (std::this_thread::get_id() != ownerThread_) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::WrongOwnerThread,
                              "Mesh resource store mutation rejected a non-owner thread.")};
        }
        return {};
    }

    std::optional<std::size_t>
    MeshResourceStore::findSlotIndex(MeshResourceKey key) const noexcept {
        for (std::size_t index = 0U; index < slots_.size(); ++index) {
            if (slots_[index].occupied && slots_[index].key == key) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::size_t MeshResourceStore::allocateSlot(MeshResourceKey key) {
        std::size_t index{};
        if (freeSlots_.empty()) {
            index = slots_.size();
            slots_.push_back(Slot{});
        } else {
            index = freeSlots_.back();
            freeSlots_.pop_back();
        }

        Slot& slot = slots_[index];
        slot.occupied = true;
        slot.key = key;
        slot.nextRequestGeneration = 0U;
        slot.active.reset();
        slot.candidate.reset();
        slot.lastFailure.reset();
        return index;
    }

    Result<std::size_t>
    MeshResourceStore::resolveSlotIndex(const MeshResourceHandle& handle) const {
        if (!handle) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidHandle,
                              "Mesh resource operation rejected invalid handle.")};
        }
        if (handle.owner != identity_) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::StoreIdentityMismatch,
                              "Mesh resource operation rejected a handle from another store.")};
        }
        const auto index = static_cast<std::size_t>(handle.slot - 1U);
        if (index >= slots_.size() || !slots_[index].occupied) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::MissingResource,
                              "Mesh resource operation could not find handle slot=" +
                                  std::to_string(handle.slot) + ".")};
        }
        if (slots_[index].generation != handle.slotGeneration) {
            return std::unexpected{resourceError(
                MeshResourceDiagnosticCode::SlotGenerationMismatch,
                "Mesh resource operation rejected stale handle slot=" +
                    std::to_string(handle.slot) +
                    " expectedGeneration=" + std::to_string(slots_[index].generation) +
                    " actualGeneration=" + std::to_string(handle.slotGeneration) + ".")};
        }
        return index;
    }

    // The linear branches are the explicit request state machine; splitting them would obscure
    // which failures allocate/invalidate a slot versus return a programming error.
    // NOLINTBEGIN(readability-function-cognitive-complexity)
    Result<MeshResourceRequestResult>
    MeshResourceStore::request(MeshResourceKey key, asset::AssetProductKey expectedProductKey,
                               std::span<const asset::AssetProductRecord> products) {
        if (auto owner = requireOwnerThread(); !owner) {
            return std::unexpected{std::move(owner.error())};
        }
        if (!key) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidResourceKey,
                              "Mesh resource request rejected invalid resource key.")};
        }
        if (!expectedProductKey) {
            return std::unexpected{resourceError(MeshResourceDiagnosticCode::InvalidProductKey,
                                                 "Mesh resource request for " + keyLabel(key) +
                                                     " rejected invalid expected product key.")};
        }
        const asset::AssetTypeId meshType = asset::makeAssetTypeId(mesh::kMeshAssetTypeName);
        if (key.assetType != meshType || expectedProductKey.assetType != meshType) {
            return std::unexpected{resourceError(MeshResourceDiagnosticCode::ProductTypeMismatch,
                                                 "Mesh resource request for " + keyLabel(key) +
                                                     " rejected a non-mesh product type.")};
        }
        if (!productKeyMatches(key, expectedProductKey)) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidProductKey,
                              "Mesh resource request for " + keyLabel(key) +
                                  " rejected a product key with a different guid or asset type.")};
        }

        const std::optional<std::size_t> existingSlot = findSlotIndex(key);
        const std::size_t slotIndex = existingSlot ? *existingSlot : allocateSlot(key);
        Slot& slot = slots_[slotIndex];
        const MeshResourceHandle handle{.slot = static_cast<std::uint32_t>(slotIndex + 1U),
                                        .slotGeneration = slot.generation,
                                        .owner = identity_};

        const asset::AssetProductRecord* exact = nullptr;
        bool foundDuplicateExact = false;
        bool foundSameResource = false;
        for (const asset::AssetProductRecord& product : products) {
            if (product.key.guid == key.guid && product.key.assetType == key.assetType) {
                foundSameResource = true;
            }
            if (product.key == expectedProductKey) {
                if (exact != nullptr) {
                    foundDuplicateExact = true;
                } else {
                    exact = &product;
                }
            }
        }

        auto failSelection = [&](MeshResourceFailure failure) -> MeshResourceRequestResult {
            (void)nextNonzero(slot.nextRequestGeneration);
            slot.candidate.reset();
            slot.lastFailure = failure;
            const MeshResourceRequestDisposition disposition =
                slot.active ? MeshResourceRequestDisposition::KeptActiveAfterFailure
                            : MeshResourceRequestDisposition::FailedNoActive;
            return MeshResourceRequestResult{.handle = handle,
                                             .disposition = disposition,
                                             .loadPlan = std::nullopt,
                                             .failure = std::move(failure)};
        };

        if (exact == nullptr) {
            const MeshResourceFailureReason reason =
                foundSameResource ? MeshResourceFailureReason::StaleProduct
                                  : MeshResourceFailureReason::MissingProduct;
            return failSelection(selectionFailure(reason, key,
                                                  foundSameResource
                                                      ? "found only stale product records"
                                                      : "did not find a product record"));
        }
        if (foundDuplicateExact) {
            return failSelection(selectionFailure(
                MeshResourceFailureReason::InvalidProductRecord, key,
                "found duplicate exact product records in the selection snapshot"));
        }

        auto locator = asset::makeAssetArtifactLocatorV1(*exact);
        if (!locator) {
            return failSelection(selectionFailure(MeshResourceFailureReason::InvalidProductRecord,
                                                  key, locator.error().message));
        }

        const std::uint64_t selectionHash = asset::hashAssetProductKey(expectedProductKey);
        if (slot.candidate && slot.candidate->selectionHash == selectionHash &&
            slot.candidate->productHash == exact->productHash) {
            return MeshResourceRequestResult{
                .handle = handle,
                .disposition = MeshResourceRequestDisposition::AlreadyPending,
                .loadPlan = std::nullopt,
                .failure = std::nullopt,
            };
        }
        if (!slot.candidate && slot.active && slot.active->selectionHash == selectionHash &&
            slot.active->productHash == exact->productHash) {
            return MeshResourceRequestResult{
                .handle = handle,
                .disposition = MeshResourceRequestDisposition::AlreadyReady,
                .loadPlan = std::nullopt,
                .failure = std::nullopt,
            };
        }

        const MeshResourceLoadTicket ticket{
            .handle = handle,
            .requestGeneration = nextNonzero(slot.nextRequestGeneration),
            .expectedProductHash = exact->productHash,
        };
        slot.candidate = Candidate{
            .ticket = ticket, .selectionHash = selectionHash, .productHash = exact->productHash};
        slot.lastFailure.reset();

        return MeshResourceRequestResult{
            .handle = handle,
            .disposition = MeshResourceRequestDisposition::LoadQueued,
            .loadPlan = MeshResourceLoadPlan{.ticket = ticket,
                                             .selectionHash = selectionHash,
                                             .artifact = std::move(*locator),
                                             .artifactRoot = desc_.artifactRoot,
                                             .artifactLimits = desc_.artifactLimits,
                                             .meshLimits = desc_.meshLimits},
            .failure = std::nullopt,
        };
    }
    // NOLINTEND(readability-function-cognitive-complexity)

    MeshResourceSnapshot MeshResourceStore::makeSnapshot(std::size_t slotIndex) const {
        const Slot& slot = slots_[slotIndex];
        MeshResourceState state = MeshResourceState::FailedNoActive;
        if (slot.candidate) {
            state = slot.active ? MeshResourceState::ReloadPending : MeshResourceState::Pending;
        } else if (slot.active) {
            state = MeshResourceState::Ready;
        }

        return MeshResourceSnapshot{
            .handle = MeshResourceHandle{.slot = static_cast<std::uint32_t>(slotIndex + 1U),
                                         .slotGeneration = slot.generation,
                                         .owner = identity_},
            .key = slot.key,
            .state = state,
            .activeRevision = slot.active ? slot.active->revision : 0U,
            .activeProductHash = slot.active ? slot.active->productHash : 0U,
            .pendingRequestGeneration =
                slot.candidate ? slot.candidate->ticket.requestGeneration : 0U,
            .lastFailure = slot.lastFailure,
        };
    }

    Result<MeshResourceSnapshot> MeshResourceStore::publish(MeshResourceLoadCompletion completion) {
        if (auto owner = requireOwnerThread(); !owner) {
            return std::unexpected{std::move(owner.error())};
        }
        auto slotIndex = resolveSlotIndex(completion.ticket.handle);
        if (!slotIndex) {
            return std::unexpected{std::move(slotIndex.error())};
        }
        Slot& slot = slots_[*slotIndex];
        if (!slot.candidate) {
            return std::unexpected{resourceError(
                MeshResourceDiagnosticCode::ResourceNotPending,
                "Mesh resource publish rejected a completion with no pending candidate.")};
        }
        if (completion.ticket.requestGeneration != slot.candidate->ticket.requestGeneration) {
            return std::unexpected{resourceError(
                MeshResourceDiagnosticCode::RequestGenerationMismatch,
                "Mesh resource publish rejected stale request generation expected=" +
                    std::to_string(slot.candidate->ticket.requestGeneration) +
                    " actual=" + std::to_string(completion.ticket.requestGeneration) + ".")};
        }
        if (completion.ticket.expectedProductHash != slot.candidate->productHash ||
            completion.productHash != slot.candidate->productHash ||
            completion.selectionHash != slot.candidate->selectionHash) {
            return std::unexpected{resourceError(
                MeshResourceDiagnosticCode::InvalidCompletion,
                "Mesh resource publish rejected completion selection or product identity drift.")};
        }

        if (auto* failure = std::get_if<MeshResourceFailure>(&completion.outcome)) {
            if (failure->message.empty()) {
                return std::unexpected{
                    resourceError(MeshResourceDiagnosticCode::InvalidCompletion,
                                  "Mesh resource publish rejected an empty failure diagnostic.")};
            }
            slot.candidate.reset();
            slot.lastFailure = std::move(*failure);
            return makeSnapshot(*slotIndex);
        }

        auto& success = std::get<MeshResourceLoadSuccess>(completion.outcome);
        if (!success.payload) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::InvalidCompletion,
                              "Mesh resource publish rejected a null typed mesh payload.")};
        }

        slot.active = ActiveRevision{.revision = nextNonzero(nextRevision_),
                                     .selectionHash = completion.selectionHash,
                                     .productHash = completion.productHash,
                                     .payload = std::move(success.payload)};
        slot.candidate.reset();
        slot.lastFailure.reset();
        return makeSnapshot(*slotIndex);
    }

    Result<MeshResourceLease> MeshResourceStore::acquire(const MeshResourceHandle& handle) const {
        if (auto owner = requireOwnerThread(); !owner) {
            return std::unexpected{std::move(owner.error())};
        }
        auto slotIndex = resolveSlotIndex(handle);
        if (!slotIndex) {
            return std::unexpected{std::move(slotIndex.error())};
        }
        const Slot& slot = slots_[*slotIndex];
        if (!slot.active) {
            return std::unexpected{
                resourceError(MeshResourceDiagnosticCode::NoActiveResource,
                              "Mesh resource acquire found no active typed payload for " +
                                  keyLabel(slot.key) + ".")};
        }
        return MeshResourceLease{MeshResourceLease::Init{.handle = handle,
                                                         .revision = slot.active->revision,
                                                         .productHash = slot.active->productHash,
                                                         .payload = slot.active->payload}};
    }

    Result<MeshResourceSnapshot>
    MeshResourceStore::inspect(const MeshResourceHandle& handle) const {
        if (auto owner = requireOwnerThread(); !owner) {
            return std::unexpected{std::move(owner.error())};
        }
        auto slotIndex = resolveSlotIndex(handle);
        if (!slotIndex) {
            return std::unexpected{std::move(slotIndex.error())};
        }
        return makeSnapshot(*slotIndex);
    }

    VoidResult MeshResourceStore::unload(const MeshResourceHandle& handle) {
        if (auto owner = requireOwnerThread(); !owner) {
            return std::unexpected{std::move(owner.error())};
        }
        auto slotIndex = resolveSlotIndex(handle);
        if (!slotIndex) {
            return std::unexpected{std::move(slotIndex.error())};
        }

        Slot& slot = slots_[*slotIndex];
        slot.occupied = false;
        (void)nextNonzero(slot.generation);
        slot.key = {};
        slot.nextRequestGeneration = 0U;
        slot.active.reset();
        slot.candidate.reset();
        slot.lastFailure.reset();
        freeSlots_.push_back(*slotIndex);
        return {};
    }

    MeshResourceLoadCompletion loadMeshResourceCandidate(const MeshResourceLoadPlan& plan) {
        auto failure = [&](MeshResourceFailureReason reason,
                           std::string message) -> MeshResourceLoadCompletion {
            return MeshResourceLoadCompletion{
                .ticket = plan.ticket,
                .selectionHash = plan.selectionHash,
                .productHash = plan.ticket.expectedProductHash,
                .outcome = MeshResourceFailure{.reason = reason, .message = std::move(message)},
            };
        };

        if (!plan.ticket || !plan.artifact) {
            return failure(MeshResourceFailureReason::RuntimeCreationFailed,
                           "Mesh resource load plan is invalid.");
        }

        auto artifact = asset::readVerifiedAssetArtifactV1(plan.artifactRoot, plan.artifact,
                                                           plan.artifactLimits);
        if (!artifact) {
            return failure(MeshResourceFailureReason::ArtifactReadFailed,
                           std::move(artifact.error().message));
        }

        auto meshProduct = mesh::readMeshProductV1(artifact->bytes, plan.meshLimits);
        if (!meshProduct) {
            return failure(MeshResourceFailureReason::UnsupportedProduct,
                           std::move(meshProduct.error().message));
        }

        auto payload = std::make_shared<const mesh::MeshProductV1>(std::move(*meshProduct));
        return MeshResourceLoadCompletion{
            .ticket = plan.ticket,
            .selectionHash = plan.selectionHash,
            .productHash = plan.ticket.expectedProductHash,
            .outcome = MeshResourceLoadSuccess{.payload = std::move(payload)},
        };
    }

    const char* meshResourceDiagnosticCodeName(MeshResourceDiagnosticCode code) noexcept {
        switch (code) {
        case MeshResourceDiagnosticCode::InvalidDescriptor:
            return "InvalidDescriptor";
        case MeshResourceDiagnosticCode::WrongOwnerThread:
            return "WrongOwnerThread";
        case MeshResourceDiagnosticCode::InvalidResourceKey:
            return "InvalidResourceKey";
        case MeshResourceDiagnosticCode::InvalidProductKey:
            return "InvalidProductKey";
        case MeshResourceDiagnosticCode::ProductTypeMismatch:
            return "ProductTypeMismatch";
        case MeshResourceDiagnosticCode::InvalidHandle:
            return "InvalidHandle";
        case MeshResourceDiagnosticCode::MissingResource:
            return "MissingResource";
        case MeshResourceDiagnosticCode::SlotGenerationMismatch:
            return "SlotGenerationMismatch";
        case MeshResourceDiagnosticCode::RequestGenerationMismatch:
            return "RequestGenerationMismatch";
        case MeshResourceDiagnosticCode::ResourceNotPending:
            return "ResourceNotPending";
        case MeshResourceDiagnosticCode::InvalidCompletion:
            return "InvalidCompletion";
        case MeshResourceDiagnosticCode::StoreIdentityMismatch:
            return "StoreIdentityMismatch";
        case MeshResourceDiagnosticCode::NoActiveResource:
            return "NoActiveResource";
        }
        return "Unknown";
    }

    const char* meshResourceFailureReasonName(MeshResourceFailureReason reason) noexcept {
        switch (reason) {
        case MeshResourceFailureReason::MissingProduct:
            return "MissingProduct";
        case MeshResourceFailureReason::StaleProduct:
            return "StaleProduct";
        case MeshResourceFailureReason::InvalidProductRecord:
            return "InvalidProductRecord";
        case MeshResourceFailureReason::ArtifactReadFailed:
            return "ArtifactReadFailed";
        case MeshResourceFailureReason::UnsupportedProduct:
            return "UnsupportedProduct";
        case MeshResourceFailureReason::RuntimeCreationFailed:
            return "RuntimeCreationFailed";
        }
        return "Unknown";
    }

    const char*
    meshResourceRequestDispositionName(MeshResourceRequestDisposition disposition) noexcept {
        switch (disposition) {
        case MeshResourceRequestDisposition::LoadQueued:
            return "LoadQueued";
        case MeshResourceRequestDisposition::AlreadyReady:
            return "AlreadyReady";
        case MeshResourceRequestDisposition::AlreadyPending:
            return "AlreadyPending";
        case MeshResourceRequestDisposition::FailedNoActive:
            return "FailedNoActive";
        case MeshResourceRequestDisposition::KeptActiveAfterFailure:
            return "KeptActiveAfterFailure";
        }
        return "Unknown";
    }

    const char* meshResourceStateName(MeshResourceState state) noexcept {
        switch (state) {
        case MeshResourceState::FailedNoActive:
            return "FailedNoActive";
        case MeshResourceState::Pending:
            return "Pending";
        case MeshResourceState::Ready:
            return "Ready";
        case MeshResourceState::ReloadPending:
            return "ReloadPending";
        }
        return "Unknown";
    }

} // namespace asharia::resource
