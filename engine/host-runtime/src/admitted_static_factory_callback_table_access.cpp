#include "admitted_static_factory_callback_table_access.hpp"

#include <memory>

#include "activation_eligibility_state.hpp"

namespace asharia::host_runtime {

    std::expected<AdmittedStaticFactoryExecutionViewV2, AdmittedFactoryExecutionAccessErrorV2>
    AdmittedStaticFactoryCallbackTableAccessV2::executionView(
        const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept {
        if (!admittedTable.state_ || !admittedTable.state_->pending ||
            !admittedTable.admission_.valid_) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV2::MovedFrom);
        }

        const PendingActivationFactoryTableStateV2& pending = *admittedTable.state_->pending;
        if (!pending.lineage) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV2::TableInvalid);
        }
        if (!isCurrentControlThread(pending.lineage->controlThreadEpoch)) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV2::WrongControlThread);
        }
        if (!isClaimedCurrentProcessEpoch(pending.lineage->processEpoch)) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV2::ProcessEpochStale);
        }
        if (pending.origin != PendingFactoryTableOriginV2::AdmittedRegistration ||
            pending.expectedTableAddress != std::addressof(pending.table) ||
            pending.expectedTableInstance == nullptr ||
            pending.expectedTableInstance !=
                StaticFactoryCallbackTablePrivateAccessV1::instanceAnchor(pending.table) ||
            pending.table.registrationSnapshot().generationId !=
                pending.lineage->staticCompositionGenerationId ||
            pending.table.registrationSnapshot().hostActivationBlueprintSha256 !=
                pending.lineage->blueprintIntegrity) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV2::TableInvalid);
        }

        return AdmittedStaticFactoryExecutionViewV2{
            .callbacks = StaticFactoryCallbackTablePrivateAccessV1::callbacks(pending.table),
            .contributionRuntimeBindings =
                StaticFactoryCallbackTablePrivateAccessV1::contributionRuntimeBindings(
                    pending.table),
            .snapshot = std::addressof(pending.table.registrationSnapshot()),
            .processScope = std::addressof(pending.lineage->processScope),
            .processEpoch = pending.lineage->processEpoch,
            .controlThreadEpoch = pending.lineage->controlThreadEpoch,
            .engineGenerationId = pending.lineage->host.engineGenerationId,
            .blueprintIntegrity = pending.lineage->blueprintIntegrity,
        };
    }

} // namespace asharia::host_runtime
