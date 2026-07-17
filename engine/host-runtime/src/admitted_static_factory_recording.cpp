#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

#include "activation_eligibility_state.hpp"

namespace asharia::host_runtime {
    namespace {

        [[nodiscard]] ActivationEligibilityErrorV2
        makeError(ActivationEligibilityStageV2 stage, ActivationEligibilityErrorCodeV2 code,
                  ActivationEligibilityFieldV2 field,
                  std::optional<StaticFactoryRegistrationErrorCode> registrationCode =
                      std::nullopt) noexcept {
            return {
                .stage = stage,
                .code = code,
                .field = field,
                .registrationCode = registrationCode,
            };
        }

        [[nodiscard]] ActivationEligibilityErrorV2
        registrationError(const StaticFactoryRegistrationError& error) noexcept {
            return makeError(ActivationEligibilityStageV2::ProviderRecording,
                             ActivationEligibilityErrorCodeV2::RegistrationFailed,
                             ActivationEligibilityFieldV2::Registration, error.code);
        }

    } // namespace

    PendingActivationFactoryTableV2::PendingActivationFactoryTableV2(
        std::unique_ptr<PendingActivationFactoryTableStateV2> state) noexcept
        : state_(std::move(state)) {}

    PendingActivationFactoryTableV2::~PendingActivationFactoryTableV2() = default;
    PendingActivationFactoryTableV2::PendingActivationFactoryTableV2(
        PendingActivationFactoryTableV2&&) noexcept = default;

    ActivationAdmissionV2::ActivationAdmissionV2(ActivationAdmissionV2&& other) noexcept
        : valid_(std::exchange(other.valid_, false)) {}

    AdmittedStaticFactoryCallbackTableV2::AdmittedStaticFactoryCallbackTableV2(
        std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV2> state,
        ActivationAdmissionV2 admission) noexcept
        : state_(std::move(state)), admission_(std::move(admission)) {}

    AdmittedStaticFactoryCallbackTableV2::~AdmittedStaticFactoryCallbackTableV2() = default;

    AdmittedStaticFactoryCallbackTableV2::AdmittedStaticFactoryCallbackTableV2(
        AdmittedStaticFactoryCallbackTableV2&& other) noexcept
        : state_(std::move(other.state_)), admission_(std::move(other.admission_)) {}

    const StaticFactoryRegistrationSnapshotV2&
    AdmittedStaticFactoryCallbackTableV2::registrationSnapshot() const noexcept {
        if (!state_ || !state_->pending || !admission_.valid_) {
            std::terminate();
        }
        return state_->pending->table.registrationSnapshot();
    }

    ActivationEligibilityResultV2<PendingActivationFactoryTableV2>
    recordAdmittedStaticFactoryProviders(PreRegistrationAdmissionV2 admission) noexcept {
        auto lineage = ActivationEligibilityStateAccessV2::take(std::move(admission));
        if (!lineage) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::ProviderRecording,
                                             ActivationEligibilityErrorCodeV2::AdmissionMovedFrom,
                                             ActivationEligibilityFieldV2::Admission));
        }
        if (!isCurrentControlThread(lineage->controlThreadEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::ProviderRecording,
                                             ActivationEligibilityErrorCodeV2::WrongControlThread,
                                             ActivationEligibilityFieldV2::ControlThread));
        }
        if (!isClaimedCurrentProcessEpoch(lineage->processEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::ProviderRecording,
                                             ActivationEligibilityErrorCodeV2::ProcessEpochStale,
                                             ActivationEligibilityFieldV2::CurrentProcess));
        }
        if (lineage->registrationCapacity == nullptr || lineage->recordProviders == nullptr) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::ProviderRecording,
                          ActivationEligibilityErrorCodeV2::RecordingFunctionMissing,
                          ActivationEligibilityFieldV2::RecordingFunction));
        }

        const StaticFactoryRegistrationCapacityV2 capacity = lineage->registrationCapacity();
        auto recorderResult = createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return std::unexpected(registrationError(recorderResult.error()));
        }

        auto recorder = std::move(*recorderResult);
        lineage->recordProviders(recorder);
        auto tableResult = std::move(recorder).finish();
        if (!tableResult) {
            return std::unexpected(registrationError(tableResult.error()));
        }

        const StaticFactoryRegistrationSnapshotV2& snapshot = tableResult->registrationSnapshot();
        if (snapshot.generationId != lineage->staticCompositionGenerationId ||
            snapshot.hostActivationBlueprintSha256 != lineage->blueprintIntegrity) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::ProviderRecording,
                          ActivationEligibilityErrorCodeV2::TableSnapshotMismatch,
                          ActivationEligibilityFieldV2::TableSnapshot));
        }

        try {
            auto state = std::make_unique<PendingActivationFactoryTableStateV2>(
                std::move(lineage), std::move(*tableResult),
                PendingFactoryTableOriginV2::AdmittedRegistration);
            return ActivationEligibilityStateAccessV2::makePendingTable(std::move(state));
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::ProviderRecording,
                                             ActivationEligibilityErrorCodeV2::AllocationFailed,
                                             ActivationEligibilityFieldV2::Registration));
        } catch (const std::length_error&) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::ProviderRecording,
                                             ActivationEligibilityErrorCodeV2::AllocationFailed,
                                             ActivationEligibilityFieldV2::Registration));
        }
    }

    ActivationEligibilityResultV2<AdmittedStaticFactoryCallbackTableV2>
    admitStaticFactoryActivation(PendingActivationFactoryTableV2 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV2::take(std::move(pendingTable));
        if (!state) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::Activation,
                          ActivationEligibilityErrorCodeV2::PendingTableMovedFrom,
                          ActivationEligibilityFieldV2::Admission));
        }
        if (!state->lineage || !state->lineage->processEpoch) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::Activation,
                          ActivationEligibilityErrorCodeV2::PendingTableMovedFrom,
                          ActivationEligibilityFieldV2::Admission));
        }
        if (!isCurrentControlThread(state->lineage->controlThreadEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::Activation,
                                             ActivationEligibilityErrorCodeV2::WrongControlThread,
                                             ActivationEligibilityFieldV2::ControlThread));
        }
        if (!isClaimedCurrentProcessEpoch(state->lineage->processEpoch)) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::Activation,
                                             ActivationEligibilityErrorCodeV2::ProcessEpochStale,
                                             ActivationEligibilityFieldV2::CurrentProcess));
        }
        if (state->origin != PendingFactoryTableOriginV2::AdmittedRegistration) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::Activation,
                                             ActivationEligibilityErrorCodeV2::TableOriginInvalid,
                                             ActivationEligibilityFieldV2::TableOrigin));
        }
        if (state->expectedTableAddress != std::addressof(state->table)) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::Activation,
                          ActivationEligibilityErrorCodeV2::TableInstanceMismatch,
                          ActivationEligibilityFieldV2::TableInstance));
        }
        if (state->expectedTableInstance == nullptr ||
            state->expectedTableInstance !=
                StaticFactoryCallbackTablePrivateAccessV1::instanceAnchor(state->table)) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::Activation,
                          ActivationEligibilityErrorCodeV2::TableInstanceMismatch,
                          ActivationEligibilityFieldV2::TableInstance));
        }
        const StaticFactoryRegistrationSnapshotV2& snapshot = state->table.registrationSnapshot();
        if (snapshot.generationId != state->lineage->staticCompositionGenerationId ||
            snapshot.hostActivationBlueprintSha256 != state->lineage->blueprintIntegrity) {
            return std::unexpected(
                makeError(ActivationEligibilityStageV2::Activation,
                          ActivationEligibilityErrorCodeV2::TableSnapshotMismatch,
                          ActivationEligibilityFieldV2::TableSnapshot));
        }

        try {
            auto admittedState =
                std::make_unique<AdmittedStaticFactoryCallbackTableStateV2>(std::move(state));
            return ActivationEligibilityStateAccessV2::makeAdmittedTable(
                std::move(admittedState),
                ActivationEligibilityStateAccessV2::makeActivationAdmission());
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(ActivationEligibilityStageV2::Activation,
                                             ActivationEligibilityErrorCodeV2::AllocationFailed,
                                             ActivationEligibilityFieldV2::Admission));
        }
    }

} // namespace asharia::host_runtime
