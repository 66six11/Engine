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

        struct MaterializedFactory final {
            StaticFactoryRegistrationV1 registration;
            StaticFactoryCallbacksV1 callbacks;
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

    } // namespace

    FactoryInstanceTokenV1::~FactoryInstanceTokenV1() noexcept {
        if (opaque_ != nullptr) {
            std::terminate();
        }
    }

    StaticFactoryCallbackTableV1::StaticFactoryCallbackTableV1(
        std::shared_ptr<const StaticFactoryCallbackTableStorageV1> storage) noexcept
        : storage_(std::move(storage)) {}

    const StaticFactoryRegistrationSnapshotV1&
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
            for (const StaticFactoryRegistrationState::FactoryObservation& factory :
                 observedFactories) {
                factories.push_back({
                    .registration =
                        {
                            .packageId = std::string(factory.packageId),
                            .packageVersion = std::string(factory.packageVersion),
                            .moduleId = std::string(factory.moduleId),
                            .factoryId = std::string(factory.factoryId),
                            .providerEntryPoint = std::string(factory.providerEntryPoint),
                        },
                    .callbacks = factory.callbacks,
                });
            }
            std::ranges::sort(factories, canonicalFactoryLess);

            StaticFactoryRegistrationSnapshotV1 snapshot{
                .generationId = std::string(state.generationId),
                .hostActivationBlueprintSha256 = std::string(state.hostActivationBlueprintSha256),
                .registrations = {},
            };
            snapshot.registrations.reserve(factories.size());
            std::vector<StaticFactoryCallbacksV1> callbacks;
            callbacks.reserve(factories.size());
            for (MaterializedFactory& factory : factories) {
                snapshot.registrations.push_back(std::move(factory.registration));
                callbacks.push_back(factory.callbacks);
            }

            auto storage = std::make_shared<const StaticFactoryCallbackTableStorageV1>(
                std::move(snapshot), std::move(callbacks));
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
