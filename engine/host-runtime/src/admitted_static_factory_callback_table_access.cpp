#include "admitted_static_factory_callback_table_access.hpp"

#include <memory>

#include "activation_eligibility_state.hpp"

namespace asharia::host_runtime {

    std::expected<AdmittedStaticFactoryExecutionViewV1, AdmittedFactoryExecutionAccessErrorV1>
    AdmittedStaticFactoryCallbackTableAccessV1::executionView(
        const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept {
        if (!admittedTable.state_ || !admittedTable.state_->pending ||
            !admittedTable.admission_.valid_) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV1::MovedFrom);
        }

        const PendingActivationFactoryTableStateV1& pending = *admittedTable.state_->pending;
        if (!pending.lineage) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV1::TableInvalid);
        }
        if (!isCurrentControlThread(pending.lineage->controlThreadEpoch)) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV1::WrongControlThread);
        }
        if (!isClaimedCurrentProcessEpoch(pending.lineage->processEpoch)) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV1::ProcessEpochStale);
        }
        if (pending.origin != PendingFactoryTableOriginV1::AdmittedRegistration ||
            pending.expectedTableAddress != std::addressof(pending.table) ||
            pending.expectedTableInstance == nullptr ||
            pending.expectedTableInstance !=
                StaticFactoryCallbackTablePrivateAccessV1::instanceAnchor(pending.table) ||
            pending.table.registrationSnapshot() != pending.lineage->expectedSnapshot) {
            return std::unexpected(AdmittedFactoryExecutionAccessErrorV1::TableInvalid);
        }

        return AdmittedStaticFactoryExecutionViewV1{
            .callbacks = StaticFactoryCallbackTablePrivateAccessV1::callbacks(pending.table),
            .snapshot = std::addressof(pending.table.registrationSnapshot()),
            .processScope = std::addressof(pending.lineage->processScope),
            .engineGenerationId = pending.lineage->host.engineGenerationId,
            .blueprintIntegrity = pending.lineage->blueprintIntegrity,
        };
    }

} // namespace asharia::host_runtime
