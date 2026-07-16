#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <utility>

#include "asharia/host_runtime/process_scope.hpp"

#include "factory_lifecycle_context_access.hpp"
#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

namespace asharia::host_runtime {
    namespace {

        void appendDependencyView(ResolvedProcessFactoryStateV2& factory,
                                  ExactFactoryReferenceViewV1 dependencyFactory,
                                  FactoryInstanceViewV1 dependencyInstance) noexcept {
            try {
                factory.dependencyScratch.push_back({
                    .factory = dependencyFactory,
                    .instance = dependencyInstance,
                });
            } catch (...) {
                // Capacity is reserved during preflight; an exception here indicates
                // corrupted execution bookkeeping rather than recoverable OOM.
                std::terminate();
            }
        }

        [[nodiscard]] bool
        materializeDependencyViews(ProcessScopeExecutorStateV2& state,
                                   ResolvedProcessFactoryStateV2& factory) noexcept {
            factory.dependencyScratch.clear();
            for (const std::size_t dependencyIndex : factory.dependencyIndices) {
                if (dependencyIndex >= state.factories.size()) {
                    return false;
                }
                const ResolvedProcessFactoryStateV2& dependency = state.factories[dependencyIndex];
                if (!dependency.dependencyVisible || !dependency.instance ||
                    !dependency.attribution) {
                    return false;
                }
                appendDependencyView(factory, dependency.attribution->factory.view(),
                                     dependency.instance->view());
            }
            return true;
        }

        [[nodiscard]] ProcessScopeStartResultV2
        operationFailure(ProcessScopeErrorCodeV2 code, ProcessScopeStateV2 state) noexcept {
            return std::unexpected(ProcessScopeStartFailureV2{
                .operation = makeProcessScopeOperationError(code, state),
                .primary = std::nullopt,
                .publication = std::nullopt,
                .cleanupDiagnostics = {},
            });
        }

        [[nodiscard]] ProcessScopeStartResultV2
        rollbackWithoutPrimary(ProcessScopeExecutorStateV2& state,
                               std::span<const StaticFactoryCallbacksV1> callbacks,
                               ProcessScopeErrorCodeV2 code) noexcept {
            state.diagnosticScratch.clear();
            cleanupProcessScopeFactories(state, callbacks);
            state.lifecycleState = ProcessScopeStateV2::StartFailed;
            return std::unexpected(ProcessScopeStartFailureV2{
                .operation = makeProcessScopeOperationError(code, ProcessScopeStateV2::StartFailed),
                .primary = std::nullopt,
                .publication = std::nullopt,
                .cleanupDiagnostics = std::move(state.diagnosticScratch),
            });
        }

        [[nodiscard]] ProcessScopeStartResultV2 rollbackLifecycleFailure(
            ProcessScopeExecutorStateV2& state, std::span<const StaticFactoryCallbacksV1> callbacks,
            const ResolvedProcessFactoryStateV2& failedFactory,
            ProcessScopeLifecycleStageV2 failedStage, ProcessScopeErrorCodeV2 errorCode,
            std::uint32_t providerLocalCode) noexcept {
            ProcessScopeLifecycleDiagnosticV2 primary =
                makeProcessScopeDiagnostic(failedFactory, failedStage, providerLocalCode);
            state.diagnosticScratch.clear();
            cleanupProcessScopeFactories(state, callbacks);
            state.lifecycleState = ProcessScopeStateV2::StartFailed;
            return std::unexpected(ProcessScopeStartFailureV2{
                .operation =
                    makeProcessScopeOperationError(errorCode, ProcessScopeStateV2::StartFailed),
                .primary = std::move(primary),
                .publication = std::nullopt,
                .cleanupDiagnostics = std::move(state.diagnosticScratch),
            });
        }

        [[nodiscard]] std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2>
        publicationAttribution(const ProcessScopeExecutorStateV2& state,
                               const ProcessContributionPublicationErrorV1& failure) noexcept {
            if (!state.contributionRegistry || failure.factoryIndex >= state.factories.size() ||
                failure.factoryIndex >= state.contributionRegistry->factorySlots.size()) {
                return {};
            }
            const ProcessContributionFactorySlotRangeStateV1& range =
                state.contributionRegistry->factorySlots[failure.factoryIndex];
            if (failure.slotIndex < range.firstSlot ||
                failure.slotIndex >= range.firstSlot + range.slotCount) {
                return {};
            }
            const std::size_t localIndex = failure.slotIndex - range.firstSlot;
            const auto& attributions =
                state.factories[failure.factoryIndex].contributionAttributions;
            return localIndex < attributions.size() ? attributions[localIndex] : nullptr;
        }

        [[nodiscard]] ProcessScopeStartResultV2
        rollbackPublicationFailure(ProcessScopeExecutorStateV2& state,
                                   std::span<const StaticFactoryCallbacksV1> callbacks,
                                   const ProcessContributionPublicationErrorV1& failure) noexcept {
            ProcessScopeErrorCodeV2 errorCode =
                ProcessScopeErrorCodeV2::ContributionPublicationFailed;
            ProcessScopeContributionPublicationStageV2 stage =
                ProcessScopeContributionPublicationStageV2::LeaseCommit;
            switch (failure.code) {
            case ProcessContributionPublicationErrorCodeV1::WrongControlThread:
                errorCode = ProcessScopeErrorCodeV2::WrongControlThread;
                break;
            case ProcessContributionPublicationErrorCodeV1::ProcessEpochStale:
                errorCode = ProcessScopeErrorCodeV2::ProcessEpochStale;
                break;
            case ProcessContributionPublicationErrorCodeV1::PayloadNull:
                errorCode = ProcessScopeErrorCodeV2::ContributionAccessorReturnedNull;
                stage = ProcessScopeContributionPublicationStageV2::PayloadAccess;
                break;
            case ProcessContributionPublicationErrorCodeV1::PayloadAccessorMissing:
                stage = ProcessScopeContributionPublicationStageV2::PayloadAccess;
                break;
            case ProcessContributionPublicationErrorCodeV1::RegistryNotStaging:
            case ProcessContributionPublicationErrorCodeV1::OwnerFactoryOutOfRange:
            case ProcessContributionPublicationErrorCodeV1::FactoryHasNoContributions:
            case ProcessContributionPublicationErrorCodeV1::FactoryLeaseAlreadyCommitted:
            case ProcessContributionPublicationErrorCodeV1::FactoryInstanceInvalid:
                break;
            }

            std::optional<ProcessScopeContributionPublicationDiagnosticV2> publication;
            if (auto attribution = publicationAttribution(state, failure)) {
                publication = ProcessScopeStateAccessV2::makePublicationDiagnostic(
                    std::move(attribution), stage);
            }
            state.diagnosticScratch.clear();
            cleanupProcessScopeFactories(state, callbacks);
            state.lifecycleState = ProcessScopeStateV2::StartFailed;
            return std::unexpected(ProcessScopeStartFailureV2{
                .operation =
                    makeProcessScopeOperationError(errorCode, ProcessScopeStateV2::StartFailed),
                .primary = std::nullopt,
                .publication = std::move(publication),
                .cleanupDiagnostics = std::move(state.diagnosticScratch),
            });
        }

    } // namespace

    ProcessScopeStartResultV2 ProcessScopeExecutorV2::start() noexcept {
        if (!state_) {
            return operationFailure(ProcessScopeErrorCodeV2::ExecutorMovedFrom,
                                    ProcessScopeStateV2::MovedFrom);
        }
        if (state_->operationInProgress) {
            return operationFailure(ProcessScopeErrorCodeV2::OperationInProgress,
                                    state_->lifecycleState);
        }
        if (state_->lifecycleState != ProcessScopeStateV2::Prepared) {
            return operationFailure(ProcessScopeErrorCodeV2::StartRequiresPrepared,
                                    state_->lifecycleState);
        }

        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV1::executionView(state_->admittedTable);
        if (!executionView) {
            const ProcessScopeErrorCodeV2 code = mapExecutionAccessError(executionView.error());
            if (code == ProcessScopeErrorCodeV2::WrongControlThread) {
                return operationFailure(code, state_->lifecycleState);
            }
            state_->lifecycleState = ProcessScopeStateV2::StartFailed;
            return operationFailure(code, state_->lifecycleState);
        }

        const std::span<const StaticFactoryCallbacksV1> callbacks = executionView->callbacks;
        ProcessScopeOperationGuardV2 operationGuard{state_->operationInProgress};
        state_->diagnosticScratch.clear();
        for (std::size_t factoryIndex = 0; factoryIndex < state_->factories.size();
             ++factoryIndex) {
            ResolvedProcessFactoryStateV2& factory = state_->factories[factoryIndex];
            if (!materializeDependencyViews(*state_, factory)) {
                return rollbackWithoutPrimary(*state_, callbacks,
                                              ProcessScopeErrorCodeV2::AdmittedTableInvalid);
            }

            FactoryCreateContextV1 createContext = FactoryLifecycleContextAccessV1::create(
                processFactoryAttribution(factory), factory.dependencyScratch);
            FactoryCreateResultV1 createResult =
                callbacks[factory.descriptorIndex].create(createContext);
            if (!createResult.result().isSucceeded()) {
                return rollbackLifecycleFailure(*state_, callbacks, factory,
                                                ProcessScopeLifecycleStageV2::Create,
                                                ProcessScopeErrorCodeV2::FactoryCreateFailed,
                                                createResult.result().localCode());
            }

            factory.instance.emplace(std::move(createResult).takeInstance());
            FactoryActivateContextV1 activateContext =
                FactoryLifecycleContextAccessV1::activate(processFactoryAttribution(factory));
            const FactoryCallbackResultV1 activateResult =
                callbacks[factory.descriptorIndex].activate(activateContext,
                                                            factory.instance->view());
            if (!activateResult.isSucceeded()) {
                return rollbackLifecycleFailure(
                    *state_, callbacks, factory, ProcessScopeLifecycleStageV2::Activate,
                    ProcessScopeErrorCodeV2::FactoryActivateFailed, activateResult.localCode());
            }
            factory.lifecycleActivated = true;

            const std::size_t contributionCount =
                processFactoryContributionSlotCountV1(state_->contributionRegistry, factoryIndex);
            if (contributionCount != 0) {
                auto lease = publishProcessFactoryContributionsV1(
                    state_->contributionRegistry, factoryIndex, factory.instance->view());
                if (!lease) {
                    return rollbackPublicationFailure(*state_, callbacks, lease.error());
                }
                factory.contributionLease.emplace(std::move(*lease));
            }
            factory.dependencyVisible = true;
        }

        const auto opened = openProcessContributionRegistryV1(state_->contributionRegistry);
        if (!opened) {
            ProcessScopeErrorCodeV2 code = ProcessScopeErrorCodeV2::ContributionPublicationFailed;
            switch (opened.error().code) {
            case ProcessContributionRegistryTransitionErrorCodeV1::WrongControlThread:
                code = ProcessScopeErrorCodeV2::WrongControlThread;
                break;
            case ProcessContributionRegistryTransitionErrorCodeV1::ProcessEpochStale:
                code = ProcessScopeErrorCodeV2::ProcessEpochStale;
                break;
            case ProcessContributionRegistryTransitionErrorCodeV1::RegistryNotStaging:
            case ProcessContributionRegistryTransitionErrorCodeV1::FactoryPublicationIncomplete:
            case ProcessContributionRegistryTransitionErrorCodeV1::SlotPublicationIncomplete:
                break;
            }
            return rollbackWithoutPrimary(*state_, callbacks, code);
        }

        state_->lifecycleState = ProcessScopeStateV2::Active;
        return {};
    }

} // namespace asharia::host_runtime
