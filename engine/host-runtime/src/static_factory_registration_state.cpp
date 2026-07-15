#include "static_factory_registration_state.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>

namespace asharia::host_runtime {

    StaticFactoryRegistrationError
    makeStaticFactoryRegistrationError(StaticFactoryRegistrationErrorCode code) noexcept {
        StaticFactoryRegistrationError error;
        error.code = code;
        return error;
    }

    StaticFactoryRegistrationState::StaticFactoryRegistrationState(
        StaticFactoryRegistrationCapacityV1 requestedCapacity)
        : capacity(requestedCapacity), textStorage(requestedCapacity.textBytes),
          failureFactoryIdStorage(requestedCapacity.diagnosticFactoryIdBytes),
          // Fixed-size slots make provider invocation a zero-growth operation.
          providers(requestedCapacity.providerCount), factories(requestedCapacity.factoryCount),
          registrar(*this) {}

    std::string_view StaticFactoryRegistrationState::copyText(std::string_view value) noexcept {
        if (value.size() > textStorage.size() - textBytesUsed) {
            fail(StaticFactoryRegistrationErrorCode::TextCapacityExceeded);
            return {};
        }
        const std::span<char> destination =
            std::span<char>{textStorage}.subspan(textBytesUsed, value.size());
        if (!value.empty()) {
            std::memcpy(destination.data(), value.data(), value.size());
        }
        textBytesUsed += value.size();
        return {destination.data(), destination.size()};
    }

    void StaticFactoryRegistrationState::fail(StaticFactoryRegistrationErrorCode code,
                                              const ProviderObservation* provider,
                                              std::string_view factoryId) noexcept {
        // Preserve the first deterministic failure; later calls cannot rewrite attribution.
        if (failure.has_value()) {
            return;
        }

        PendingFailure pending;
        pending.code = code;
        if (provider != nullptr) {
            pending.provider = *provider;
        }
        pending.observedFactoryIdBytes = factoryId.size();
        if (factoryId.size() > failureFactoryIdStorage.size()) {
            pending.code = StaticFactoryRegistrationErrorCode::DiagnosticFactoryIdCapacityExceeded;
            failure = pending;
            return;
        }
        pending.factoryIdSize = factoryId.size();
        if (pending.factoryIdSize != 0) {
            std::memcpy(failureFactoryIdStorage.data(), factoryId.data(), pending.factoryIdSize);
        }
        failure = pending;
    }

    StaticFactoryRegistrationError StaticFactoryRegistrationState::owningFailure() const noexcept {
        if (!failure.has_value()) {
            return makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::InvalidCapacity);
        }

        try {
            const PendingFailure& pending = *failure;
            return {
                .code = pending.code,
                .packageId = std::string(pending.provider.packageId),
                .packageVersion = std::string(pending.provider.packageVersion),
                .moduleId = std::string(pending.provider.moduleId),
                .entryPoint = std::string(pending.provider.entryPoint),
                .factoryId = std::string(failureFactoryIdStorage.data(), pending.factoryIdSize),
                .observedFactoryIdBytes = pending.observedFactoryIdBytes,
            };
        } catch (const std::bad_alloc&) {
            return makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed);
        } catch (const std::length_error&) {
            return makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed);
        }
    }

    void
    StaticFactoryRegistrationState::registerFactory(std::string_view localFactoryId,
                                                    StaticFactoryCallbacksV1 callbacks) noexcept {
        if (failure.has_value()) {
            return;
        }
        if (!providerActive) {
            fail(StaticFactoryRegistrationErrorCode::FactoryOutsideProvider);
            return;
        }

        const ProviderObservation& provider = providers[activeProviderIndex];
        const auto expected = std::ranges::find(activeExpectedFactoryIds, localFactoryId);
        if (expected == activeExpectedFactoryIds.end()) {
            fail(StaticFactoryRegistrationErrorCode::FactoryUnknown, &provider, localFactoryId);
            return;
        }

        const std::span<const FactoryObservation> observedFactories =
            std::span<const FactoryObservation>{factories}.first(observedFactoryCount);
        const auto duplicate = std::ranges::find_if(
            observedFactories, [&provider, localFactoryId](const FactoryObservation& factory) {
                return factory.packageId == provider.packageId &&
                       factory.packageVersion == provider.packageVersion &&
                       factory.moduleId == provider.moduleId && factory.factoryId == localFactoryId;
            });
        if (duplicate != observedFactories.end()) {
            fail(StaticFactoryRegistrationErrorCode::FactoryDuplicate, &provider, localFactoryId);
            return;
        }
        if (observedFactoryCount >= capacity.factoryCount) {
            fail(StaticFactoryRegistrationErrorCode::FactoryCountMismatch, &provider,
                 localFactoryId);
            return;
        }
        if (callbacks.create == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryCreateCallbackMissing, &provider,
                 localFactoryId);
            return;
        }
        if (callbacks.activate == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryActivateCallbackMissing, &provider,
                 localFactoryId);
            return;
        }
        if (callbacks.quiesce == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryQuiesceCallbackMissing, &provider,
                 localFactoryId);
            return;
        }
        if (callbacks.deactivate == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryDeactivateCallbackMissing, &provider,
                 localFactoryId);
            return;
        }
        if (callbacks.destroy == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryDestroyCallbackMissing, &provider,
                 localFactoryId);
            return;
        }

        const std::string_view ownedFactoryId = copyText(*expected);
        if (failure.has_value()) {
            return;
        }
        factories[observedFactoryCount] = {
            .packageId = provider.packageId,
            .packageVersion = provider.packageVersion,
            .moduleId = provider.moduleId,
            .factoryId = ownedFactoryId,
            .providerEntryPoint = provider.entryPoint,
            .callbacks = callbacks,
        };
        ++observedFactoryCount;
    }

} // namespace asharia::host_runtime
