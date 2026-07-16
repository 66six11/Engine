#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

#include <exception>
#include <memory>
#include <new>
#include <utility>

#include "activation_eligibility_state.hpp"

namespace asharia::host_runtime {
    namespace {

        [[nodiscard]] ActivationEligibilityErrorV1 makeError(
            ActivationEligibilityStageV1 stage,
            ActivationEligibilityErrorCodeV1 code,
            ActivationEligibilityFieldV1 field,
            std::optional<StaticFactoryRegistrationErrorCode> registrationCode =
                std::nullopt) noexcept {
            return {
                .stage = stage,
                .code = code,
                .field = field,
                .registrationCode = registrationCode,
            };
        }

        [[nodiscard]] ActivationEligibilityErrorV1 registrationError(
            const StaticFactoryRegistrationError& error) noexcept {
            return makeError(ActivationEligibilityStageV1::ProviderRecording,
                             ActivationEligibilityErrorCodeV1::RegistrationFailed,
                             ActivationEligibilityFieldV1::Registration, error.code);
        }

    } // namespace

    PendingActivationFactoryTableV1::PendingActivationFactoryTableV1(
        std::unique_ptr<PendingActivationFactoryTableStateV1> state) noexcept
        : state_(std::move(state)) {}

    PendingActivationFactoryTableV1::~PendingActivationFactoryTableV1() = default;
    PendingActivationFactoryTableV1::PendingActivationFactoryTableV1(
        PendingActivationFactoryTableV1&&) noexcept = default;

    ActivationAdmissionV1::ActivationAdmissionV1(ActivationAdmissionV1&& other) noexcept
        : valid_(std::exchange(other.valid_, false)) {}

    AdmittedStaticFactoryCallbackTableV1::AdmittedStaticFactoryCallbackTableV1(
        std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV1> state,
        ActivationAdmissionV1 admission) noexcept
        : state_(std::move(state)), admission_(std::move(admission)) {}

    AdmittedStaticFactoryCallbackTableV1::~AdmittedStaticFactoryCallbackTableV1() = default;

    AdmittedStaticFactoryCallbackTableV1::AdmittedStaticFactoryCallbackTableV1(
        AdmittedStaticFactoryCallbackTableV1&& other) noexcept
        : state_(std::move(other.state_)), admission_(std::move(other.admission_)) {}

    const StaticFactoryRegistrationSnapshotV1&
    AdmittedStaticFactoryCallbackTableV1::registrationSnapshot() const noexcept {
        if (!state_ || !state_->pending || !admission_.valid_) {
            std::terminate();
        }
        return state_->pending->table.registrationSnapshot();
    }

    ActivationEligibilityResultV1<PendingActivationFactoryTableV1>
    recordAdmittedStaticFactoryProviders(PreRegistrationAdmissionV1 admission) noexcept {
        auto lineage = ActivationEligibilityStateAccessV1::take(std::move(admission));
        if (!lineage) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::ProviderRecording,
                ActivationEligibilityErrorCodeV1::AdmissionMovedFrom,
                ActivationEligibilityFieldV1::Admission));
        }
        if (!isCurrentControlThread(lineage->controlThreadEpoch)) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::ProviderRecording,
                ActivationEligibilityErrorCodeV1::WrongControlThread,
                ActivationEligibilityFieldV1::ControlThread));
        }
        if (!isClaimedCurrentProcessEpoch(lineage->processEpoch)) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::ProviderRecording,
                ActivationEligibilityErrorCodeV1::ProcessEpochStale,
                ActivationEligibilityFieldV1::CurrentProcess));
        }
        if (lineage->registrationCapacity == nullptr || lineage->recordProviders == nullptr) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::ProviderRecording,
                ActivationEligibilityErrorCodeV1::RecordingFunctionMissing,
                ActivationEligibilityFieldV1::RecordingFunction));
        }

        const StaticFactoryRegistrationCapacityV1 capacity =
            lineage->registrationCapacity();
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

        try {
            auto state = std::make_unique<PendingActivationFactoryTableStateV1>(
                std::move(lineage), std::move(*tableResult),
                PendingFactoryTableOriginV1::AdmittedRegistration);
            return ActivationEligibilityStateAccessV1::makePendingTable(std::move(state));
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::ProviderRecording,
                ActivationEligibilityErrorCodeV1::AllocationFailed,
                ActivationEligibilityFieldV1::Registration));
        }
    }

    ActivationEligibilityResultV1<AdmittedStaticFactoryCallbackTableV1>
    admitStaticFactoryActivation(PendingActivationFactoryTableV1 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV1::take(std::move(pendingTable));
        if (!state) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::PendingTableMovedFrom,
                ActivationEligibilityFieldV1::Admission));
        }
        if (!state->lineage || !state->lineage->processEpoch) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::PendingTableMovedFrom,
                ActivationEligibilityFieldV1::Admission));
        }
        if (!isCurrentControlThread(state->lineage->controlThreadEpoch)) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::WrongControlThread,
                ActivationEligibilityFieldV1::ControlThread));
        }
        if (!isClaimedCurrentProcessEpoch(state->lineage->processEpoch)) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::ProcessEpochStale,
                ActivationEligibilityFieldV1::CurrentProcess));
        }
        if (state->origin != PendingFactoryTableOriginV1::AdmittedRegistration) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::TableOriginInvalid,
                ActivationEligibilityFieldV1::TableOrigin));
        }
        if (state->expectedTableAddress != std::addressof(state->table)) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::TableInstanceMismatch,
                ActivationEligibilityFieldV1::TableInstance));
        }
        if (state->expectedTableInstance == nullptr ||
            state->expectedTableInstance !=
                StaticFactoryCallbackTablePrivateAccessV1::instanceAnchor(state->table)) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::TableInstanceMismatch,
                ActivationEligibilityFieldV1::TableInstance));
        }
        if (state->table.registrationSnapshot() != state->lineage->expectedSnapshot) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::TableSnapshotMismatch,
                ActivationEligibilityFieldV1::TableSnapshot));
        }

        try {
            auto admittedState =
                std::make_unique<AdmittedStaticFactoryCallbackTableStateV1>(
                    std::move(state));
            return ActivationEligibilityStateAccessV1::makeAdmittedTable(
                std::move(admittedState),
                ActivationEligibilityStateAccessV1::makeActivationAdmission());
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeError(
                ActivationEligibilityStageV1::Activation,
                ActivationEligibilityErrorCodeV1::AllocationFailed,
                ActivationEligibilityFieldV1::Admission));
        }
    }

} // namespace asharia::host_runtime
