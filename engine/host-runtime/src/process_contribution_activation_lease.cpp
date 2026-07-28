#include "process_contribution_activation_lease.hpp"

#include <exception>
#include <utility>

#include "activation_eligibility_state.hpp"
#include "process_contribution_registry_state.hpp"

namespace asharia::host_runtime {
    namespace {

        [[nodiscard]] ProcessContributionPublicationErrorV1
        publicationError(ProcessContributionPublicationErrorCodeV1 code, std::size_t factoryIndex,
                         std::size_t slotIndex = 0) noexcept {
            return {
                .code = code,
                .factoryIndex = factoryIndex,
                .slotIndex = slotIndex,
            };
        }

        void clearStagedPayloads(ProcessContributionRegistryGenerationStateV1& generation,
                                 const ProcessContributionFactorySlotRangeStateV1& range) noexcept {
            for (std::size_t offset = 0; offset < range.slotCount; ++offset) {
                ProcessContributionRegistrySlotStateV1& slot =
                    generation.slots[range.firstSlot + offset];
                slot.payload = nullptr;
                slot.committed = false;
            }
        }

    } // namespace

    ProcessContributionActivationLeaseV1::~ProcessContributionActivationLeaseV1() noexcept {
        if (requiresRevoke_) {
            std::terminate();
        }
    }

    ProcessContributionActivationLeaseV1::ProcessContributionActivationLeaseV1(
        ProcessContributionActivationLeaseV1&& other) noexcept
        : generation_(std::move(other.generation_)), factoryIndex_(other.factoryIndex_),
          requiresRevoke_(other.requiresRevoke_) {
        other.factoryIndex_ = 0;
        other.requiresRevoke_ = false;
    }

    void ProcessContributionActivationLeaseV1::revoke() noexcept {
        if (!requiresRevoke_ || !generation_ ||
            !isCurrentControlThread(generation_->controlThreadEpoch) ||
            !isClaimedCurrentProcessEpoch(generation_->processEpoch) ||
            generation_->phase != ProcessContributionRegistryPhaseV1::Revoking ||
            factoryIndex_ >= generation_->factorySlots.size()) {
            std::terminate();
        }

        ProcessContributionFactorySlotRangeStateV1& range =
            generation_->factorySlots[factoryIndex_];
        if (!range.leaseCommitted || range.leaseRevoked ||
            range.firstSlot > generation_->slots.size() ||
            range.slotCount > generation_->slots.size() - range.firstSlot) {
            std::terminate();
        }
        clearStagedPayloads(*generation_, range);
        range.leaseRevoked = true;

        requiresRevoke_ = false;
        generation_.reset();
    }

    std::expected<ProcessContributionActivationLeaseV1, ProcessContributionPublicationErrorV1>
    publishProcessFactoryContributionsV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation,
        std::size_t factoryIndex, FactoryInstanceViewV1 instance) noexcept {
        if (!generation || !isCurrentControlThread(generation->controlThreadEpoch)) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::WrongControlThread, factoryIndex));
        }
        if (!isClaimedCurrentProcessEpoch(generation->processEpoch)) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::ProcessEpochStale, factoryIndex));
        }
        if (generation->phase != ProcessContributionRegistryPhaseV1::Staging) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::RegistryNotStaging, factoryIndex));
        }
        if (factoryIndex >= generation->factorySlots.size()) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::OwnerFactoryOutOfRange, factoryIndex));
        }
        if (!instance.isValid()) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::FactoryInstanceInvalid, factoryIndex));
        }

        ProcessContributionFactorySlotRangeStateV1& range = generation->factorySlots[factoryIndex];
        if (range.slotCount == 0) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::FactoryHasNoContributions,
                factoryIndex));
        }
        if (range.leaseCommitted || range.leaseRevoked ||
            range.firstSlot > generation->slots.size() ||
            range.slotCount > generation->slots.size() - range.firstSlot) {
            return std::unexpected(publicationError(
                ProcessContributionPublicationErrorCodeV1::FactoryLeaseAlreadyCommitted,
                factoryIndex));
        }

        for (std::size_t offset = 0; offset < range.slotCount; ++offset) {
            const std::size_t slotIndex = range.firstSlot + offset;
            ProcessContributionRegistrySlotStateV1& slot = generation->slots[slotIndex];
            if (slot.ownerFactoryIndex != factoryIndex || slot.payloadAccessor == nullptr ||
                slot.payload != nullptr || slot.committed) {
                clearStagedPayloads(*generation, range);
                return std::unexpected(publicationError(
                    ProcessContributionPublicationErrorCodeV1::PayloadAccessorMissing, factoryIndex,
                    slotIndex));
            }

            slot.payload = slot.payloadAccessor(instance);
            if (slot.payload == nullptr) {
                clearStagedPayloads(*generation, range);
                return std::unexpected(
                    publicationError(ProcessContributionPublicationErrorCodeV1::PayloadNull,
                                     factoryIndex, slotIndex));
            }
        }

        // All payloads remain hidden while the generation is Staging. Mark the
        // fixed range only after every accessor succeeds, then return its sole lease.
        for (std::size_t offset = 0; offset < range.slotCount; ++offset) {
            generation->slots[range.firstSlot + offset].committed = true;
        }
        range.leaseCommitted = true;
        return ProcessContributionActivationLeaseV1{generation, factoryIndex};
    }

} // namespace asharia::host_runtime
