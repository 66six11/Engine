#include "static_factory_registration_state.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>
#include <tuple>

namespace asharia::host_runtime {

    StaticFactoryRegistrationError
    makeStaticFactoryRegistrationError(StaticFactoryRegistrationErrorCode code) noexcept {
        StaticFactoryRegistrationError error;
        error.code = code;
        return error;
    }

    StaticFactoryRegistrationState::StaticFactoryRegistrationState(
        StaticFactoryRegistrationCapacityV2 requestedCapacity)
        : capacity(requestedCapacity), textStorage(requestedCapacity.textBytes),
          failureFactoryIdStorage(requestedCapacity.diagnosticFactoryIdBytes),
          failureContributionIdStorage(requestedCapacity.diagnosticContributionIdBytes),
          // Fixed-size slots make provider invocation a zero-growth operation.
          providers(requestedCapacity.providerCount), factories(requestedCapacity.factoryCount),
          contributions(requestedCapacity.contributionCount), registrar(*this) {}

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
                                              std::string_view factoryId,
                                              std::string_view contributionId) noexcept {
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
        pending.observedContributionIdBytes = contributionId.size();
        if (factoryId.size() > failureFactoryIdStorage.size()) {
            pending.code = StaticFactoryRegistrationErrorCode::DiagnosticFactoryIdCapacityExceeded;
            failure = pending;
            return;
        }
        pending.factoryIdSize = factoryId.size();
        if (pending.factoryIdSize != 0) {
            std::memcpy(failureFactoryIdStorage.data(), factoryId.data(), pending.factoryIdSize);
        }
        if (contributionId.size() > failureContributionIdStorage.size()) {
            pending.code =
                StaticFactoryRegistrationErrorCode::DiagnosticContributionIdCapacityExceeded;
            failure = pending;
            return;
        }
        pending.contributionIdSize = contributionId.size();
        if (pending.contributionIdSize != 0) {
            std::memcpy(failureContributionIdStorage.data(), contributionId.data(),
                        pending.contributionIdSize);
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
                .contributionId =
                    std::string(failureContributionIdStorage.data(), pending.contributionIdSize),
                .observedFactoryIdBytes = pending.observedFactoryIdBytes,
                .observedContributionIdBytes = pending.observedContributionIdBytes,
            };
        } catch (const std::bad_alloc&) {
            return makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed);
        } catch (const std::length_error&) {
            return makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed);
        }
    }

    bool StaticFactoryRegistrationState::validateFactoryCallbacks(
        const ProviderObservation& provider, std::string_view localFactoryId,
        StaticFactoryCallbacksV1 callbacks) noexcept {
        if (callbacks.create == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryCreateCallbackMissing, &provider,
                 localFactoryId);
            return false;
        }
        if (callbacks.activate == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryActivateCallbackMissing, &provider,
                 localFactoryId);
            return false;
        }
        if (callbacks.quiesce == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryQuiesceCallbackMissing, &provider,
                 localFactoryId);
            return false;
        }
        if (callbacks.deactivate == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryDeactivateCallbackMissing, &provider,
                 localFactoryId);
            return false;
        }
        if (callbacks.destroy == nullptr) {
            fail(StaticFactoryRegistrationErrorCode::FactoryDestroyCallbackMissing, &provider,
                 localFactoryId);
            return false;
        }
        return true;
    }

    bool StaticFactoryRegistrationState::validateAvailableContributionBindings(
        const ProviderObservation& provider, std::string_view localFactoryId,
        std::span<const StaticContributionBindingV2> availableContributions) noexcept {
        for (std::size_t index = 0; index < availableContributions.size(); ++index) {
            const StaticContributionBindingV2& binding = availableContributions[index];
            if (binding.contributionId_.empty() || binding.contributionKind_.empty() ||
                binding.typeKey_ == nullptr || binding.payloadAccessor_ == nullptr ||
                (binding.cardinality_ != StaticContributionCardinalityV1::Single &&
                 binding.cardinality_ != StaticContributionCardinalityV1::Multiple)) {
                fail(StaticFactoryRegistrationErrorCode::ContributionBindingInvalid, &provider,
                     localFactoryId, binding.contributionId_);
                return false;
            }
            if (std::ranges::any_of(availableContributions.first(index),
                                    [&binding](const StaticContributionBindingV2& previous) {
                                        return previous.contributionId_ == binding.contributionId_;
                                    })) {
                fail(StaticFactoryRegistrationErrorCode::ContributionBindingDuplicate, &provider,
                     localFactoryId, binding.contributionId_);
                return false;
            }
        }
        return true;
    }

    bool StaticFactoryRegistrationState::validateSelectedContributionCapacity(
        const ProviderObservation& provider, std::string_view localFactoryId,
        const StaticFactoryExpectationV1& expected) noexcept {
        if (expected.contributions.size() >
            capacity.contributionCount - observedContributionCount) {
            fail(StaticFactoryRegistrationErrorCode::ContributionCountMismatch, &provider,
                 localFactoryId);
            return false;
        }

        std::size_t remainingTextBytes = textStorage.size() - textBytesUsed;
        for (const StaticContributionExpectationV1& contribution : expected.contributions) {
            if (contribution.contributionId.size() > remainingTextBytes) {
                fail(StaticFactoryRegistrationErrorCode::TextCapacityExceeded, &provider,
                     localFactoryId, contribution.contributionId);
                return false;
            }
            remainingTextBytes -= contribution.contributionId.size();
            if (contribution.contributionKind.size() > remainingTextBytes) {
                fail(StaticFactoryRegistrationErrorCode::TextCapacityExceeded, &provider,
                     localFactoryId, contribution.contributionId);
                return false;
            }
            remainingTextBytes -= contribution.contributionKind.size();
        }
        if (localFactoryId.size() > remainingTextBytes) {
            fail(StaticFactoryRegistrationErrorCode::TextCapacityExceeded, &provider,
                 localFactoryId);
            return false;
        }
        return true;
    }

    bool StaticFactoryRegistrationState::recordSelectedContributions(
        std::size_t factoryIndex, const ProviderObservation& provider,
        std::string_view localFactoryId, const StaticFactoryExpectationV1& expected,
        std::span<const StaticContributionBindingV2> availableContributions) noexcept {
        for (const StaticContributionExpectationV1& expectedContribution :
             expected.contributions) {
            const auto binding = std::ranges::find_if(
                availableContributions,
                [&expectedContribution](const StaticContributionBindingV2& value) {
                    return value.contributionId_ == expectedContribution.contributionId;
                });
            if (binding == availableContributions.end()) {
                fail(StaticFactoryRegistrationErrorCode::ContributionMissing, &provider,
                     localFactoryId, expectedContribution.contributionId);
                return false;
            }
            if (binding->contributionKind_ != expectedContribution.contributionKind) {
                fail(StaticFactoryRegistrationErrorCode::ContributionKindMismatch, &provider,
                     localFactoryId, expectedContribution.contributionId);
                return false;
            }
        }

        for (const StaticContributionExpectationV1& expectedContribution :
             expected.contributions) {
            const auto binding = std::ranges::find_if(
                availableContributions,
                [&expectedContribution](const StaticContributionBindingV2& value) {
                    return value.contributionId_ == expectedContribution.contributionId;
                });
            const std::string_view ownedContributionId =
                copyText(expectedContribution.contributionId);
            const std::string_view ownedContributionKind =
                copyText(expectedContribution.contributionKind);
            if (failure.has_value()) {
                return false;
            }
            contributions[observedContributionCount] = {
                .factoryIndex = factoryIndex,
                .contributionId = ownedContributionId,
                .contributionKind = ownedContributionKind,
                .cardinality = binding->cardinality_,
                .typeKey = binding->typeKey_,
                .payloadAccessor = binding->payloadAccessor_,
            };
            ++observedContributionCount;
        }
        return true;
    }

    void StaticFactoryRegistrationState::registerFactory(
        std::string_view localFactoryId, StaticFactoryCallbacksV1 callbacks,
        std::span<const StaticContributionBindingV2> availableContributions) noexcept {
        if (failure.has_value()) {
            return;
        }
        if (!providerActive) {
            fail(StaticFactoryRegistrationErrorCode::FactoryOutsideProvider);
            return;
        }

        const ProviderObservation& provider = providers[activeProviderIndex];
        const auto expected = std::ranges::find_if(
            activeExpectedFactories, [localFactoryId](const StaticFactoryExpectationV1& value) {
                return value.factoryId == localFactoryId;
            });
        if (expected == activeExpectedFactories.end()) {
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
        if (!validateFactoryCallbacks(provider, localFactoryId, callbacks) ||
            !validateAvailableContributionBindings(provider, localFactoryId,
                                                   availableContributions) ||
            !validateSelectedContributionCapacity(provider, localFactoryId, *expected)) {
            return;
        }

        const std::size_t factoryIndex = observedFactoryCount;
        if (!recordSelectedContributions(factoryIndex, provider, localFactoryId, *expected,
                                         availableContributions)) {
            return;
        }

        const std::string_view ownedFactoryId = copyText(expected->factoryId);
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

    void StaticFactoryRegistrationState::validateContributionContracts() noexcept {
        if (failure.has_value()) {
            return;
        }

        const std::span<const ContributionObservation> observed =
            std::span<const ContributionObservation>{contributions}.first(
                observedContributionCount);
        const auto stableKey = [this](const ContributionObservation& contribution) noexcept {
            const FactoryObservation& factory = factories[contribution.factoryIndex];
            return std::tie(contribution.contributionKind, factory.packageId,
                            factory.packageVersion, factory.moduleId, factory.factoryId,
                            contribution.contributionId);
        };
        const auto failCanonicalConflict = [this, observed,
                                            &stableKey](StaticFactoryRegistrationErrorCode code,
                                                        auto conflicts) noexcept {
            const ContributionObservation* selected = nullptr;
            for (std::size_t left = 0; left < observed.size(); ++left) {
                for (std::size_t right = left + 1; right < observed.size(); ++right) {
                    if (observed[left].contributionKind != observed[right].contributionKind ||
                        !conflicts(observed[left], observed[right])) {
                        continue;
                    }
                    const ContributionObservation& attributed =
                        stableKey(observed[left]) < stableKey(observed[right]) ? observed[right]
                                                                               : observed[left];
                    if (selected == nullptr || stableKey(attributed) < stableKey(*selected)) {
                        selected = &attributed;
                    }
                }
            }
            if (selected == nullptr) {
                return false;
            }
            const FactoryObservation& factory = factories[selected->factoryIndex];
            const ProviderObservation provider{
                .packageId = factory.packageId,
                .packageVersion = factory.packageVersion,
                .moduleId = factory.moduleId,
                .entryPoint = factory.providerEntryPoint,
            };
            fail(code, &provider, factory.factoryId, selected->contributionId);
            return true;
        };

        if (failCanonicalConflict(
                StaticFactoryRegistrationErrorCode::ContributionContractCardinalityConflict,
                [](const ContributionObservation& left,
                   const ContributionObservation& right) noexcept {
                    return left.cardinality != right.cardinality;
                })) {
            return;
        }
        static_cast<void>(failCanonicalConflict(
            StaticFactoryRegistrationErrorCode::ContributionContractTypeConflict,
            [](const ContributionObservation& left, const ContributionObservation& right) noexcept {
                return left.typeKey != right.typeKey;
            }));
    }

} // namespace asharia::host_runtime
