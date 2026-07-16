#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/host_runtime/process_scope.hpp"

#include "admitted_static_factory_callback_table_access.hpp"
#include "host_activation_blueprint_projection_state.hpp"
#include "process_scope_internal.hpp"
#include "process_scope_state.hpp"

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

        [[nodiscard]] ExactFactoryReferenceV1
        ownReference(const ExactFactoryReferenceStateV1& reference) {
            return {
                .packageId = reference.packageId,
                .packageVersion = reference.packageVersion,
                .moduleId = reference.moduleId,
                .factoryId = reference.factoryId,
            };
        }

        [[nodiscard]] ExactFactoryReferenceV1
        ownReference(const StaticFactoryRegistrationV2& registration) {
            return {
                .packageId = registration.packageId,
                .packageVersion = registration.packageVersion,
                .moduleId = registration.moduleId,
                .factoryId = registration.factoryId,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV1
        simpleError(ProcessScopeErrorCodeV1 code) noexcept {
            return {
                .code = code,
                .factory = std::nullopt,
                .requirement = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV1
        factoryError(ProcessScopeErrorCodeV1 code, const ExactFactoryReferenceStateV1& factory) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV1
        factoryError(ProcessScopeErrorCodeV1 code, const StaticFactoryRegistrationV2& factory) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = std::nullopt,
            };
        }

        [[nodiscard]] ProcessScopePreparationErrorV1
        requirementError(ProcessScopeErrorCodeV1 code, const ExactFactoryReferenceStateV1& factory,
                         const ExactFactoryReferenceStateV1& requirement) {
            return {
                .code = code,
                .factory = ownReference(factory),
                .requirement = ownReference(requirement),
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

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV1>
        validateProjectionHeader(const AdmittedStaticFactoryExecutionViewV1& executionView,
                                 const StaticFactoryRegistrationSnapshotV2& snapshot,
                                 const ProcessScopeBlueprintProjectionStateV1& projection) {
            if (projection.scope != HostScopeKindStateV1::Process) {
                return simpleError(ProcessScopeErrorCodeV1::ProcessScopeExpected);
            }
            if (projection.parentScope.has_value()) {
                return simpleError(ProcessScopeErrorCodeV1::ParentScopeInvalid);
            }
            if (projection.engineGenerationId != executionView.engineGenerationId) {
                return simpleError(ProcessScopeErrorCodeV1::EngineGenerationMismatch);
            }
            if (projection.blueprintIntegrity != executionView.blueprintIntegrity ||
                projection.blueprintIntegrity != snapshot.hostActivationBlueprintSha256) {
                return simpleError(ProcessScopeErrorCodeV1::BlueprintMismatch);
            }
            if (projection.lifecycleModel != kLifecycleModel) {
                return simpleError(ProcessScopeErrorCodeV1::LifecycleModelMismatch);
            }
            if (executionView.callbacks.size() != snapshot.registrations.size()) {
                return simpleError(ProcessScopeErrorCodeV1::DescriptorCountMismatch);
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV1>
        validateDescriptorIdentities(const StaticFactoryRegistrationSnapshotV2& snapshot) {
            for (std::size_t index = 0; index < snapshot.registrations.size(); ++index) {
                const StaticFactoryRegistrationV2& registration = snapshot.registrations[index];
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (sameFactory(snapshot.registrations[previous], registration)) {
                        return factoryError(ProcessScopeErrorCodeV1::DescriptorDuplicate,
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

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV1>
        validateRequirements(const ProcessScopeBlueprintProjectionStateV1& projection,
                             const ProcessFactoryProjectionStateV1& factory,
                             std::size_t factoryIndex) {
            for (std::size_t index = 0; index < factory.requirements.size(); ++index) {
                const ExactFactoryReferenceStateV1& requirement = factory.requirements[index];
                if (!isComplete(requirement)) {
                    return requirementError(ProcessScopeErrorCodeV1::ProcessProjectionInvalid,
                                            factory.reference, requirement);
                }
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (factory.requirements[previous] == requirement) {
                        return requirementError(ProcessScopeErrorCodeV1::RequirementDuplicate,
                                                factory.reference, requirement);
                    }
                }
                const std::optional<std::size_t> target = findPlanFactory(projection, requirement);
                if (!target) {
                    return requirementError(ProcessScopeErrorCodeV1::RequirementMissing,
                                            factory.reference, requirement);
                }
                if (*target >= factoryIndex) {
                    return requirementError(ProcessScopeErrorCodeV1::RequirementOrderInvalid,
                                            factory.reference, requirement);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProcessScopePreparationErrorV1>
        validateProcessFactories(const ProcessScopeBlueprintProjectionStateV1& projection,
                                 const StaticFactoryRegistrationSnapshotV2& snapshot) {
            for (std::size_t index = 0; index < projection.factories.size(); ++index) {
                const ProcessFactoryProjectionStateV1& factory = projection.factories[index];
                if (!isComplete(factory.reference)) {
                    return factoryError(ProcessScopeErrorCodeV1::ProcessProjectionInvalid,
                                        factory.reference);
                }
                for (std::size_t previous = 0; previous < index; ++previous) {
                    if (projection.factories[previous].reference == factory.reference) {
                        return factoryError(ProcessScopeErrorCodeV1::FactoryDuplicate,
                                            factory.reference);
                    }
                }

                const std::size_t matches = descriptorMatchCount(snapshot, factory.reference);
                if (matches == 0) {
                    return factoryError(ProcessScopeErrorCodeV1::DescriptorMissing,
                                        factory.reference);
                }
                if (matches != 1) {
                    return factoryError(ProcessScopeErrorCodeV1::DescriptorDuplicate,
                                        factory.reference);
                }
                if (auto failure = validateRequirements(projection, factory, index)) {
                    return failure;
                }
            }
            return std::nullopt;
        }

        using ResolvedFactoryResultV1 = std::expected<std::vector<ResolvedProcessFactoryStateV1>,
                                                      ProcessScopePreparationErrorV1>;

        [[nodiscard]] ResolvedFactoryResultV1
        materializeFactories(const AdmittedStaticFactoryExecutionViewV1& executionView,
                             const StaticFactoryRegistrationSnapshotV2& snapshot,
                             const ProcessScopeBlueprintProjectionStateV1& projection) {
            std::vector<ResolvedProcessFactoryStateV1> factories;
            factories.reserve(projection.factories.size());
            for (const ProcessFactoryProjectionStateV1& factory : projection.factories) {
                const std::size_t descriptorIndex = *findDescriptor(snapshot, factory.reference);
                if (!callbacksComplete(executionView.callbacks[descriptorIndex])) {
                    return std::unexpected(
                        factoryError(ProcessScopeErrorCodeV1::CallbackMissing, factory.reference));
                }

                std::vector<std::size_t> dependencyIndices;
                dependencyIndices.reserve(factory.requirements.size());
                for (const ExactFactoryReferenceStateV1& requirement : factory.requirements) {
                    dependencyIndices.push_back(*findPlanFactory(projection, requirement));
                }

                auto attribution = std::make_shared<const ProcessScopeDiagnosticAttributionStateV1>(
                    ProcessScopeDiagnosticAttributionStateV1{
                        .engineGenerationId = std::string(executionView.engineGenerationId),
                        .factory = ownReference(factory.reference),
                    });
                ResolvedProcessFactoryStateV1 resolved{
                    .attribution = std::move(attribution),
                    .descriptorIndex = descriptorIndex,
                    .dependencyIndices = std::move(dependencyIndices),
                    .dependencyScratch = {},
                    .instance = std::nullopt,
                    .active = false,
                };
                resolved.dependencyScratch.reserve(factory.requirements.size());
                factories.push_back(std::move(resolved));
            }
            return factories;
        }

    } // namespace

    ProcessScopePreparationResultV1
    prepareProcessScopeExecutor(AdmittedStaticFactoryCallbackTableV1 admittedTable) noexcept {
        const auto executionView =
            AdmittedStaticFactoryCallbackTableAccessV1::executionView(admittedTable);
        if (!executionView) {
            return std::unexpected(ProcessScopePreparationErrorV1{
                .code = mapExecutionAccessError(executionView.error()),
                .factory = std::nullopt,
                .requirement = std::nullopt,
            });
        }

        try {
            if (executionView->snapshot == nullptr || executionView->processScope == nullptr) {
                return std::unexpected(
                    simpleError(ProcessScopeErrorCodeV1::ProcessProjectionInvalid));
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
                return std::unexpected(simpleError(ProcessScopeErrorCodeV1::AllocationFailed));
            }

            auto factories = materializeFactories(*executionView, snapshot, projection);
            if (!factories) {
                return std::unexpected(std::move(factories.error()));
            }
            std::vector<ProcessScopeLifecycleDiagnosticV1> diagnosticScratch;
            diagnosticScratch.reserve((projection.factories.size() * 2U) + 1U);

            auto state = std::make_unique<ProcessScopeExecutorStateV1>(
                std::move(admittedTable), std::move(*factories), std::move(diagnosticScratch));
            return ProcessScopeStateAccessV1::makeExecutor(std::move(state));
        } catch (const std::bad_alloc&) {
            return std::unexpected(simpleError(ProcessScopeErrorCodeV1::AllocationFailed));
        } catch (const std::length_error&) {
            return std::unexpected(simpleError(ProcessScopeErrorCodeV1::AllocationFailed));
        }
    }

} // namespace asharia::host_runtime
