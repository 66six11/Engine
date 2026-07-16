#include "asharia/host_runtime/static_factory_callback_table.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "static_factory_callback_table_internal.hpp"
#include "static_factory_callback_table_state.hpp"
#include "static_factory_registration_state.hpp"

namespace asharia::host_runtime {

    namespace {

        struct MaterializedContribution final {
            StaticContributionRegistrationV2 registration;
            const void* typeKey{};
            detail::ErasedStaticContributionPayloadAccessorV1 payloadAccessor{};
        };

        struct MaterializedFactory final {
            StaticFactoryRegistrationV2 registration;
            StaticFactoryCallbacksV1 callbacks;
            std::vector<MaterializedContribution> contributions;
        };

        [[nodiscard]] bool canonicalFactoryLess(const MaterializedFactory& left,
                                                const MaterializedFactory& right) noexcept {
            return std::tie(left.registration.packageId, left.registration.packageVersion,
                            left.registration.moduleId, left.registration.factoryId,
                            left.registration.providerEntryPoint) <
                   std::tie(right.registration.packageId, right.registration.packageVersion,
                            right.registration.moduleId, right.registration.factoryId,
                            right.registration.providerEntryPoint);
        }

        [[nodiscard]] bool
        canonicalContributionLess(const MaterializedContribution& left,
                                  const MaterializedContribution& right) noexcept {
            return std::tie(left.registration.contributionId, left.registration.contributionKind,
                            left.registration.cardinality) <
                   std::tie(right.registration.contributionId, right.registration.contributionKind,
                            right.registration.cardinality);
        }

    } // namespace

    FactoryInstanceTokenV1::~FactoryInstanceTokenV1() noexcept {
        if (opaque_ != nullptr) {
            std::terminate();
        }
    }

    StaticFactoryCallbackTableV1::StaticFactoryCallbackTableV1(
        std::shared_ptr<const StaticFactoryCallbackTableStorageV1> storage) noexcept
        : storage_(std::move(storage)) {}

    const StaticFactoryRegistrationSnapshotV2&
    StaticFactoryCallbackTableV1::registrationSnapshot() const noexcept {
        if (!storage_) {
            std::terminate();
        }
        return storage_->snapshot;
    }

    StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    StaticFactoryCallbackTableBuilder::build(const StaticFactoryRegistrationState& state) noexcept {
        try {
            std::vector<MaterializedFactory> factories;
            factories.reserve(state.observedFactoryCount);

            const std::span<const StaticFactoryRegistrationState::FactoryObservation>
                observedFactories =
                    std::span<const StaticFactoryRegistrationState::FactoryObservation>{
                        state.factories}
                        .first(state.observedFactoryCount);
            for (std::size_t factoryIndex = 0; factoryIndex < observedFactories.size();
                 ++factoryIndex) {
                const StaticFactoryRegistrationState::FactoryObservation& factory =
                    observedFactories[factoryIndex];
                std::vector<MaterializedContribution> contributions;
                const std::span<const StaticFactoryRegistrationState::ContributionObservation>
                    observedContributions =
                        std::span<const StaticFactoryRegistrationState::ContributionObservation>{
                            state.contributions}
                            .first(state.observedContributionCount);
                for (const StaticFactoryRegistrationState::ContributionObservation& contribution :
                     observedContributions) {
                    if (contribution.factoryIndex != factoryIndex) {
                        continue;
                    }
                    contributions.push_back({
                        .registration =
                            StaticContributionRegistrationV2{
                                .contributionId = std::string(contribution.contributionId),
                                .contributionKind = std::string(contribution.contributionKind),
                                .cardinality = contribution.cardinality,
                            },
                        .typeKey = contribution.typeKey,
                        .payloadAccessor = contribution.payloadAccessor,
                    });
                }
                std::ranges::sort(contributions, canonicalContributionLess);
                factories.push_back({
                    .registration =
                        {
                            .packageId = std::string(factory.packageId),
                            .packageVersion = std::string(factory.packageVersion),
                            .moduleId = std::string(factory.moduleId),
                            .factoryId = std::string(factory.factoryId),
                            .providerEntryPoint = std::string(factory.providerEntryPoint),
                            .contributions = {},
                        },
                    .callbacks = factory.callbacks,
                    .contributions = std::move(contributions),
                });
            }
            std::ranges::sort(factories, canonicalFactoryLess);

            StaticFactoryRegistrationSnapshotV2 snapshot{
                .generationId = std::string(state.generationId),
                .hostActivationBlueprintSha256 = std::string(state.hostActivationBlueprintSha256),
                .registrations = {},
            };
            snapshot.registrations.reserve(factories.size());
            std::vector<StaticFactoryCallbacksV1> callbacks;
            callbacks.reserve(factories.size());
            std::vector<StaticContributionRuntimeBindingV1> contributionRuntimeBindings;
            contributionRuntimeBindings.reserve(state.observedContributionCount);
            for (std::size_t registrationIndex = 0; registrationIndex < factories.size();
                 ++registrationIndex) {
                MaterializedFactory& factory = factories[registrationIndex];
                factory.registration.contributions.reserve(factory.contributions.size());
                for (std::size_t contributionIndex = 0;
                     contributionIndex < factory.contributions.size(); ++contributionIndex) {
                    MaterializedContribution& contribution =
                        factory.contributions[contributionIndex];
                    factory.registration.contributions.push_back(
                        std::move(contribution.registration));
                    contributionRuntimeBindings.push_back({
                        .registrationIndex = registrationIndex,
                        .contributionIndex = contributionIndex,
                        .typeKey = contribution.typeKey,
                        .payloadAccessor = contribution.payloadAccessor,
                    });
                }
                snapshot.registrations.push_back(std::move(factory.registration));
                callbacks.push_back(factory.callbacks);
            }

            auto storage = std::make_shared<const StaticFactoryCallbackTableStorageV1>(
                std::move(snapshot), std::move(callbacks), std::move(contributionRuntimeBindings));
            return StaticFactoryCallbackTableV1{std::move(storage)};
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed));
        } catch (const std::length_error&) {
            return std::unexpected(makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed));
        }
    }

} // namespace asharia::host_runtime
