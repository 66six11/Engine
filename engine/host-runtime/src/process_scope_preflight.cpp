#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/host_runtime/process_scope.hpp"

#include "admitted_static_factory_callback_table_access.hpp"
#include "host_activation_blueprint_projection_state.hpp"
#include "process_contribution_registry_state.hpp"
#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"
#include "static_factory_callback_table_state.hpp"

namespace asharia::host_runtime {
    namespace {

        constexpr std::string_view kLifecycleModel{"create-activate-quiesce-deactivate-destroy-v1"};

        [[nodiscard]] bool isComplete(const ExactFactoryReferenceStateV1& reference) noexcept {
            return !reference.packageId.empty() && !reference.packageVersion.empty() &&
                   !reference.moduleId.empty() && !reference.factoryId.empty();
        }

        [[nodiscard]] bool sameFactory(const StaticFactoryRegistrationV2& registration,
                                       const ExactFactoryReferenceStateV1& reference) noexcept {
            return registration.packageId == reference.packageId &&
                   registration.packageVersion == reference.packageVersion &&
                   registration.moduleId == reference.moduleId &&
                   registration.factoryId == reference.factoryId;
        }

        [[nodiscard]] bool sameFactory(const StaticFactoryRegistrationV2& left,
                                       const StaticFactoryRegistrationV2& right) noexcept {
            return left.packageId == right.packageId &&
                   left.packageVersion == right.packageVersion && left.moduleId == right.moduleId &&
                   left.factoryId == right.factoryId;
        }

        [[nodiscard]] ExactFactoryReferenceV2
        ownReference(const ExactFactoryReferenceStateV1& reference) {
            return {
                .packageId = reference.packageId,
                .packageVersion = reference.packageVersion,
                .moduleId = reference.moduleId,
                .factoryId = reference.factoryId,
            };
        }

        [[nodiscard]] ExactFactoryReferenceV2
        ownReference(const StaticFactoryRegistrationV2& registration) {
            return {
                .packageId = registration.packageId,
                .packageVersion = registration.packageVersion,
                .moduleId = registration.moduleId,
                .factoryId = registration.factoryId,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV2
        simpleError(ProcessScopeErrorCodeV2 code) noexcept {
            return {
                .code = code,
                .factory = std::nullopt,
                .requirement = std::nullopt,
                .contributionId = std::nullopt,
                .contributionKind = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV2
        factoryError(ProcessScopeErrorCodeV2 code, const ExactFactoryReferenceStateV1& factory) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = std::nullopt,
                .contributionId = std::nullopt,
                .contributionKind = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV2
        factoryError(ProcessScopeErrorCodeV2 code, const StaticFactoryRegistrationV2& factory) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = std::nullopt,
                .contributionId = std::nullopt,
                .contributionKind = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV2
        requirementError(ProcessScopeErrorCodeV2 code, const ExactFactoryReferenceStateV1& factory,
                         const ExactFactoryReferenceStateV1& requirement) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = ownReference(requirement),
                .contributionId = std::nullopt,
                .contributionKind = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV2
        contributionError(ProcessScopeErrorCodeV2 code, const StaticFactoryRegistrationV2& factory,
                          const StaticContributionRegistrationV2& contribution) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = std::nullopt,
                .contributionId = contribution.contributionId,
                .contributionKind = contribution.contributionKind,
            };
        }

        [[nodiscard]] bool callbacksComplete(const StaticFactoryCallbacksV1& callbacks) noexcept {
            return callbacks.create != nullptr && callbacks.activate != nullptr &&
                   callbacks.quiesce != nullptr && callbacks.deactivate != nullptr &&
                   callbacks.destroy != nullptr;
        }

        [[nodiscard]] std::optional<std::size_t>
        findPlanFactory(const ProcessScopeBlueprintProjectionStateV1& projection,
                        const ExactFactoryReferenceStateV1& reference) noexcept {
            for (std::size_t index = 0; index < projection.factories.size(); ++index) {
                if (projection.factories[index].reference == reference) {
                    return index;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::size_t>
        findDescriptor(const StaticFactoryRegistrationSnapshotV2& snapshot,
                       const ExactFactoryReferenceStateV1& reference) noexcept {
            for (std::size_t index = 0; index < snapshot.registrations.size(); ++index) {
                if (sameFactory(snapshot.registrations[index], reference)) {
                    return index;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV2>
        validateProjectionHeader(const AdmittedStaticFactoryExecutionViewV2& executionView,
                                 const StaticFactoryRegistrationSnapshotV2& snapshot,
                                 const ProcessScopeBlueprintProjectionStateV1& projection) {
            if (projection.scope != HostScopeKindStateV1::Process) {
                return simpleError(ProcessScopeErrorCodeV2::ProcessScopeExpected);
            }
            if (projection.parentScope.has_value()) {
                return simpleError(ProcessScopeErrorCodeV2::ParentScopeInvalid);
            }
            if (projection.engineGenerationId != executionView.engineGenerationId) {
                return simpleError(ProcessScopeErrorCodeV2::EngineGenerationMismatch);
            }
            if (projection.blueprintIntegrity != executionView.blueprintIntegrity ||
                projection.blueprintIntegrity != snapshot.hostActivationBlueprintSha256) {
                return simpleError(ProcessScopeErrorCodeV2::BlueprintMismatch);
            }
            if (projection.lifecycleModel != kLifecycleModel) {
                return simpleError(ProcessScopeErrorCodeV2::LifecycleModelMismatch);
            }
            if (executionView.callbacks.size() != snapshot.registrations.size()) {
                return simpleError(ProcessScopeErrorCodeV2::DescriptorCountMismatch);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV2>
        validateDescriptorIdentities(const StaticFactoryRegistrationSnapshotV2& snapshot) {
            for (std::size_t index = 0; index < snapshot.registrations.size(); ++index) {
                const StaticFactoryRegistrationV2& registration = snapshot.registrations[index];
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (sameFactory(snapshot.registrations[previous], registration)) {
                        return factoryError(ProcessScopeErrorCodeV2::DescriptorDuplicate,
                                            registration);
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::size_t
        descriptorMatchCount(const StaticFactoryRegistrationSnapshotV2& snapshot,
                             const ExactFactoryReferenceStateV1& reference) noexcept {
            std::size_t matches = 0;
            for (const StaticFactoryRegistrationV2& registration : snapshot.registrations) {
                if (sameFactory(registration, reference)) {
                    ++matches;
                }
            }
            return matches;
        }

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV2>
        validateRequirements(const ProcessScopeBlueprintProjectionStateV1& projection,
                             const ProcessFactoryProjectionStateV1& factory,
                             std::size_t factoryIndex) {
            for (std::size_t index = 0; index < factory.requirements.size(); ++index) {
                const ExactFactoryReferenceStateV1& requirement = factory.requirements[index];
                if (!isComplete(requirement)) {
                    return requirementError(ProcessScopeErrorCodeV2::ProcessProjectionInvalid,
                                            factory.reference, requirement);
                }
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (factory.requirements[previous] == requirement) {
                        return requirementError(ProcessScopeErrorCodeV2::RequirementDuplicate,
                                                factory.reference, requirement);
                    }
                }
                const std::optional<std::size_t> target = findPlanFactory(projection, requirement);
                if (!target) {
                    return requirementError(ProcessScopeErrorCodeV2::RequirementMissing,
                                            factory.reference, requirement);
                }
                if (*target >= factoryIndex) {
                    return requirementError(ProcessScopeErrorCodeV2::RequirementOrderInvalid,
                                            factory.reference, requirement);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV2>
        validateProcessFactories(const ProcessScopeBlueprintProjectionStateV1& projection,
                                 const StaticFactoryRegistrationSnapshotV2& snapshot) {
            for (std::size_t index = 0; index < projection.factories.size(); ++index) {
                const ProcessFactoryProjectionStateV1& factory = projection.factories[index];
                if (!isComplete(factory.reference)) {
                    return factoryError(ProcessScopeErrorCodeV2::ProcessProjectionInvalid,
                                        factory.reference);
                }
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (projection.factories[previous].reference == factory.reference) {
                        return factoryError(ProcessScopeErrorCodeV2::FactoryDuplicate,
                                            factory.reference);
                    }
                }

                const std::size_t matches = descriptorMatchCount(snapshot, factory.reference);
                if (matches == 0) {
                    return factoryError(ProcessScopeErrorCodeV2::DescriptorMissing,
                                        factory.reference);
                }
                if (matches != 1) {
                    return factoryError(ProcessScopeErrorCodeV2::DescriptorDuplicate,
                                        factory.reference);
                }
                if (auto failure = validateRequirements(projection, factory, index)) {
                    return failure;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] const StaticContributionRuntimeBindingV1*
        findRuntimeBinding(std::span<const StaticContributionRuntimeBindingV1> runtimeBindings,
                           std::size_t registrationIndex, std::size_t contributionIndex,
                           std::size_t& matchCount) noexcept {
            const StaticContributionRuntimeBindingV1* matched = nullptr;
            matchCount = 0;
            for (const StaticContributionRuntimeBindingV1& binding : runtimeBindings) {
                if (binding.registrationIndex != registrationIndex ||
                    binding.contributionIndex != contributionIndex) {
                    continue;
                }
                ++matchCount;
                if (matched == nullptr) {
                    matched = std::addressof(binding);
                }
            }
            return matched;
        }

        using RegistrySlotPlanResultV1 =
            std::expected<std::vector<ProcessContributionRegistrySlotPlanV1>,
                          ProcessScopePreparationErrorV2>;

        [[nodiscard]] RegistrySlotPlanResultV1
        materializeRegistrySlotPlans(const AdmittedStaticFactoryExecutionViewV2& executionView,
                                     const StaticFactoryRegistrationSnapshotV2& snapshot,
                                     const ProcessScopeBlueprintProjectionStateV1& projection) {
            std::size_t slotCount = 0;
            for (const ProcessFactoryProjectionStateV1& factory : projection.factories) {
                const std::size_t descriptorIndex = *findDescriptor(snapshot, factory.reference);
                const std::size_t contributionCount =
                    snapshot.registrations[descriptorIndex].contributions.size();
                if (slotCount > std::numeric_limits<std::size_t>::max() - contributionCount) {
                    return std::unexpected(simpleError(ProcessScopeErrorCodeV2::AllocationFailed));
                }
                slotCount += contributionCount;
            }

            std::vector<ProcessContributionRegistrySlotPlanV1> slotPlans;
            slotPlans.reserve(slotCount);
            for (std::size_t factoryIndex = 0; factoryIndex < projection.factories.size();
                 ++factoryIndex) {
                const ProcessFactoryProjectionStateV1& factory = projection.factories[factoryIndex];
                const std::size_t descriptorIndex = *findDescriptor(snapshot, factory.reference);
                const StaticFactoryRegistrationV2& registration =
                    snapshot.registrations[descriptorIndex];

                for (std::size_t contributionIndex = 0;
                     contributionIndex < registration.contributions.size(); ++contributionIndex) {
                    const StaticContributionRegistrationV2& contribution =
                        registration.contributions[contributionIndex];
                    std::size_t matchCount = 0;
                    const StaticContributionRuntimeBindingV1* binding =
                        findRuntimeBinding(executionView.contributionRuntimeBindings,
                                           descriptorIndex, contributionIndex, matchCount);
                    if (matchCount != 1 || binding == nullptr || binding->typeKey == nullptr ||
                        binding->payloadAccessor == nullptr) {
                        return std::unexpected(contributionError(
                            ProcessScopeErrorCodeV2::ContributionRuntimeBindingInvalid,
                            registration, contribution));
                    }
                    slotPlans.push_back({
                        .ownerFactoryIndex = factoryIndex,
                        .contributionId = contribution.contributionId,
                        .contributionKind = contribution.contributionKind,
                        .cardinality = contribution.cardinality,
                        .typeKey = binding->typeKey,
                        .payloadAccessor = binding->payloadAccessor,
                    });
                }

                for (const StaticContributionRuntimeBindingV1& binding :
                     executionView.contributionRuntimeBindings) {
                    if (binding.registrationIndex == descriptorIndex &&
                        binding.contributionIndex >= registration.contributions.size()) {
                        return std::unexpected(
                            factoryError(ProcessScopeErrorCodeV2::ContributionRuntimeBindingInvalid,
                                         registration));
                    }
                }
            }
            return slotPlans;
        }

        [[nodiscard]] ProcessScopePreparationErrorV2
        registryPreparationError(const ProcessContributionRegistryPreparationErrorV1& error,
                                 std::span<const ProcessContributionRegistrySlotPlanV1> slotPlans,
                                 const ProcessScopeBlueprintProjectionStateV1& projection) {
            ProcessScopeErrorCodeV2 code =
                ProcessScopeErrorCodeV2::ContributionRuntimeBindingInvalid;
            switch (error.code) {
            case ProcessContributionRegistryPreparationErrorCodeV1::WrongControlThread:
                code = ProcessScopeErrorCodeV2::WrongControlThread;
                break;
            case ProcessContributionRegistryPreparationErrorCodeV1::ProcessEpochStale:
                code = ProcessScopeErrorCodeV2::ProcessEpochStale;
                break;
            case ProcessContributionRegistryPreparationErrorCodeV1::SingleContractConflict:
                code = ProcessScopeErrorCodeV2::ContributionSingleConflict;
                break;
            case ProcessContributionRegistryPreparationErrorCodeV1::AllocationFailed:
                code = ProcessScopeErrorCodeV2::AllocationFailed;
                break;
            case ProcessContributionRegistryPreparationErrorCodeV1::SlotInvalid:
            case ProcessContributionRegistryPreparationErrorCodeV1::OwnerFactoryOutOfRange:
            case ProcessContributionRegistryPreparationErrorCodeV1::OwnerFactoryOrderInvalid:
            case ProcessContributionRegistryPreparationErrorCodeV1::ContractCardinalityConflict:
            case ProcessContributionRegistryPreparationErrorCodeV1::ContractTypeConflict:
                break;
            }

            const std::size_t attributedSlot = error.slotIndex;
            if (attributedSlot >= slotPlans.size()) {
                return simpleError(code);
            }
            const ProcessContributionRegistrySlotPlanV1& plan = slotPlans[attributedSlot];
            if (plan.ownerFactoryIndex >= projection.factories.size()) {
                return simpleError(code);
            }
            return {
                .code = code,
                .factory = ownReference(projection.factories[plan.ownerFactoryIndex].reference),
                .requirement = std::nullopt,
                .contributionId = std::string(plan.contributionId),
                .contributionKind = std::string(plan.contributionKind),
            };
        }

        using ResolvedFactoryResultV2 = std::expected<std::vector<ResolvedProcessFactoryStateV2>,
                                                      ProcessScopePreparationErrorV2>;

        [[nodiscard]] ResolvedFactoryResultV2
        materializeFactories(const AdmittedStaticFactoryExecutionViewV2& executionView,
                             const StaticFactoryRegistrationSnapshotV2& snapshot,
                             const ProcessScopeBlueprintProjectionStateV1& projection) {
            std::vector<ResolvedProcessFactoryStateV2> factories;
            factories.reserve(projection.factories.size());
            for (const ProcessFactoryProjectionStateV1& factory : projection.factories) {
                const std::size_t descriptorIndex = *findDescriptor(snapshot, factory.reference);
                if (!callbacksComplete(executionView.callbacks[descriptorIndex])) {
                    return std::unexpected(
                        factoryError(ProcessScopeErrorCodeV2::CallbackMissing, factory.reference));
                }

                std::vector<std::size_t> dependencyIndices;
                dependencyIndices.reserve(factory.requirements.size());
                for (const ExactFactoryReferenceStateV1& requirement : factory.requirements) {
                    dependencyIndices.push_back(*findPlanFactory(projection, requirement));
                }

                auto attribution = std::make_shared<const ProcessScopeDiagnosticAttributionStateV2>(
                    ProcessScopeDiagnosticAttributionStateV2{
                        .engineGenerationId = std::string(executionView.engineGenerationId),
                        .factory = ownReference(factory.reference),
                    });
                std::vector<
                    std::shared_ptr<const ProcessScopeContributionDiagnosticAttributionStateV2>>
                    contributionAttributions;
                const StaticFactoryRegistrationV2& registration =
                    snapshot.registrations[descriptorIndex];
                contributionAttributions.reserve(registration.contributions.size());
                for (const StaticContributionRegistrationV2& contribution :
                     registration.contributions) {
                    contributionAttributions.push_back(
                        std::make_shared<
                            const ProcessScopeContributionDiagnosticAttributionStateV2>(
                            ProcessScopeContributionDiagnosticAttributionStateV2{
                                .engineGenerationId = std::string(executionView.engineGenerationId),
                                .factory = ownReference(factory.reference),
                                .contributionId = contribution.contributionId,
                                .contributionKind = contribution.contributionKind,
                            }));
                }

                ResolvedProcessFactoryStateV2 resolved{
                    .attribution = std::move(attribution),
                    .descriptorIndex = descriptorIndex,
                    .dependencyIndices = std::move(dependencyIndices),
                    .dependencyScratch = {},
                    .contributionAttributions = std::move(contributionAttributions),
                    .instance = std::nullopt,
                    .contributionLease = std::nullopt,
                    .lifecycleActivated = false,
                    .dependencyVisible = false,
                };
                resolved.dependencyScratch.reserve(factory.requirements.size());
                factories.push_back(std::move(resolved));
            }
            return factories;
        }

    } // namespace

    ProcessScopePreparationResultV2
    prepareProcessScopeExecutorV2(AdmittedStaticFactoryCallbackTableV2 admittedTable) noexcept {
        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV2::executionView(admittedTable);
        if (!executionView) {
            return std::unexpected(ProcessScopePreparationErrorV2{
                .code = mapExecutionAccessError(executionView.error()),
                .factory = std::nullopt,
                .requirement = std::nullopt,
                .contributionId = std::nullopt,
                .contributionKind = std::nullopt,
            });
        }

        try {
            if (executionView->snapshot == nullptr || executionView->processScope == nullptr) {
                return std::unexpected(
                    simpleError(ProcessScopeErrorCodeV2::ProcessProjectionInvalid));
            }
            const StaticFactoryRegistrationSnapshotV2& snapshot = *executionView->snapshot;
            const ProcessScopeBlueprintProjectionStateV1& projection = *executionView->processScope;

            if (auto failure = validateProjectionHeader(*executionView, snapshot, projection)) {
                return std::unexpected(std::move(*failure));
            }
            if (auto failure = validateDescriptorIdentities(snapshot)) {
                return std::unexpected(std::move(*failure));
            }
            if (auto failure = validateProcessFactories(projection, snapshot)) {
                return std::unexpected(std::move(*failure));
            }
            if (projection.factories.size() > (std::numeric_limits<std::size_t>::max() - 1U) / 2U) {
                return std::unexpected(simpleError(ProcessScopeErrorCodeV2::AllocationFailed));
            }

            auto slotPlans = materializeRegistrySlotPlans(*executionView, snapshot, projection);
            if (!slotPlans) {
                return std::unexpected(std::move(slotPlans.error()));
            }
            auto contributionRegistry = prepareProcessContributionRegistryV1(
                projection.factories.size(), *slotPlans, executionView->processEpoch,
                executionView->controlThreadEpoch);
            if (!contributionRegistry) {
                return std::unexpected(
                    registryPreparationError(contributionRegistry.error(), *slotPlans, projection));
            }

            auto factories = materializeFactories(*executionView, snapshot, projection);
            if (!factories) {
                return std::unexpected(std::move(factories.error()));
            }
            std::vector<ProcessScopeLifecycleDiagnosticV2> diagnosticScratch;
            diagnosticScratch.reserve((projection.factories.size() * 2U) + 1U);

            auto state = std::make_unique<ProcessScopeExecutorStateV2>(
                std::move(admittedTable), std::move(*contributionRegistry), std::move(*factories),
                std::move(diagnosticScratch));
            return ProcessScopeStateAccessV2::makeExecutor(std::move(state));
        } catch (const std::bad_alloc&) {
            return std::unexpected(simpleError(ProcessScopeErrorCodeV2::AllocationFailed));
        } catch (const std::length_error&) {
            return std::unexpected(simpleError(ProcessScopeErrorCodeV2::AllocationFailed));
        }
    }

} // namespace asharia::host_runtime
