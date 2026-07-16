#include "asharia/host_runtime/static_factory_registration.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

#include "static_factory_callback_table_internal.hpp"
#include "static_factory_registration_state.hpp"

namespace asharia::host_runtime {
    namespace {

        [[nodiscard]] bool isLowerHexSha256(std::string_view value) noexcept {
            if (value.size() != 64) {
                return false;
            }
            return std::ranges::all_of(value, [](const char character) {
                return (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f');
            });
        }

        [[nodiscard]] bool isGenerationId(std::string_view value) noexcept {
            constexpr std::string_view kPrefix{"sha256-"};
            return value.starts_with(kPrefix) && isLowerHexSha256(value.substr(kPrefix.size()));
        }

        [[nodiscard]] bool validateExpectedFactories(
            StaticFactoryRegistrationState& state,
            const StaticFactoryProviderContextV2& context,
            std::size_t& contributionCount) noexcept {
            contributionCount = 0;
            for (std::size_t index = 0; index < context.expectedFactories.size(); ++index) {
                const StaticFactoryExpectationV1& factory = context.expectedFactories[index];
                if (factory.factoryId.empty() ||
                    (index != 0 &&
                     context.expectedFactories[index - 1].factoryId >= factory.factoryId)) {
                    state.fail(StaticFactoryRegistrationErrorCode::ExpectedFactoriesNotCanonical,
                               nullptr, factory.factoryId);
                    return false;
                }
                for (std::size_t contributionIndex = 0;
                     contributionIndex < factory.contributions.size(); ++contributionIndex) {
                    const StaticContributionExpectationV1& contribution =
                        factory.contributions[contributionIndex];
                    if (contribution.contributionId.empty() ||
                        contribution.contributionKind.empty() ||
                        (contributionIndex != 0 &&
                         factory.contributions[contributionIndex - 1].contributionId >=
                             contribution.contributionId)) {
                        state.fail(
                            StaticFactoryRegistrationErrorCode::
                                ExpectedContributionsNotCanonical,
                            nullptr, factory.factoryId, contribution.contributionId);
                        return false;
                    }
                }
                if (factory.contributions.size() >
                    state.capacity.contributionCount - state.expectedContributionCount -
                        contributionCount) {
                    state.fail(StaticFactoryRegistrationErrorCode::ContributionCountMismatch,
                               nullptr, factory.factoryId);
                    return false;
                }
                contributionCount += factory.contributions.size();
            }
            return true;
        }

    } // namespace

    StaticFactoryRegistrationRecorder::StaticFactoryRegistrationRecorder(
        std::unique_ptr<StaticFactoryRegistrationState> state) noexcept
        : state_(std::move(state)) {}

    StaticFactoryRegistrationRecorder::~StaticFactoryRegistrationRecorder() = default;

    StaticFactoryRegistrationRecorder::StaticFactoryRegistrationRecorder(
        StaticFactoryRegistrationRecorder&& other) noexcept = default;

    void StaticFactoryRegistrationRecorder::beginComposition(
        StaticCompositionRegistrationContextV2 context) noexcept {
        if (!state_) {
            return;
        }
        StaticFactoryRegistrationState& state = *state_;
        if (state.failure.has_value()) {
            return;
        }
        if (state.phase == StaticFactoryRegistrationState::Phase::Recording) {
            state.fail(StaticFactoryRegistrationErrorCode::CompositionAlreadyStarted);
            return;
        }
        if (state.phase == StaticFactoryRegistrationState::Phase::Ended ||
            state.phase == StaticFactoryRegistrationState::Phase::Finished) {
            state.fail(StaticFactoryRegistrationErrorCode::CompositionAlreadyEnded);
            return;
        }
        if (context.capacity != state.capacity || !isGenerationId(context.generationId) ||
            !isLowerHexSha256(context.hostActivationBlueprintSha256)) {
            state.fail(StaticFactoryRegistrationErrorCode::InvalidCompositionContext);
            return;
        }

        state.generationId = state.copyText(context.generationId);
        if (state.failure.has_value()) {
            return;
        }
        state.hostActivationBlueprintSha256 = state.copyText(context.hostActivationBlueprintSha256);
        if (!state.failure.has_value()) {
            state.phase = StaticFactoryRegistrationState::Phase::Recording;
        }
    }

    void
    StaticFactoryRegistrationRecorder::invokeProvider(StaticFactoryProviderContextV2 context,
                                                      StaticFactoryProviderV3 provider) noexcept {
        if (!state_) {
            return;
        }
        StaticFactoryRegistrationState& state = *state_;
        if (state.failure.has_value()) {
            return;
        }
        if (state.phase != StaticFactoryRegistrationState::Phase::Recording) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderOutsideComposition);
            return;
        }
        if (state.providerActive) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderNested);
            return;
        }
        if (provider == nullptr) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderMissing);
            return;
        }
        if (context.packageId.empty() || context.packageVersion.empty() ||
            context.moduleId.empty() || context.entryPoint.empty() ||
            context.expectedFactories.empty()) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderContextInvalid);
            return;
        }
        std::size_t contextContributionCount{};
        if (!validateExpectedFactories(state, context, contextContributionCount)) {
            return;
        }
        const std::span<const StaticFactoryRegistrationState::ProviderObservation>
            observedProviders =
                std::span<const StaticFactoryRegistrationState::ProviderObservation>{
                    state.providers}
                    .first(state.observedProviderCount);
        const auto duplicate = std::ranges::find_if(
            observedProviders,
            [&context](const StaticFactoryRegistrationState::ProviderObservation& existing) {
                return existing.packageId == context.packageId &&
                       existing.packageVersion == context.packageVersion &&
                       existing.moduleId == context.moduleId &&
                       existing.entryPoint == context.entryPoint;
            });
        if (duplicate != observedProviders.end()) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderDuplicate,
                       std::addressof(*duplicate));
            return;
        }
        if (state.observedProviderCount >= state.capacity.providerCount ||
            context.expectedFactories.size() >
                state.capacity.factoryCount - state.expectedFactoryCount) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderCountMismatch);
            return;
        }

        StaticFactoryRegistrationState::ProviderObservation observed{
            .packageId = state.copyText(context.packageId),
            .packageVersion = state.copyText(context.packageVersion),
            .moduleId = state.copyText(context.moduleId),
            .entryPoint = state.copyText(context.entryPoint),
        };
        if (state.failure.has_value()) {
            return;
        }

        state.providers[state.observedProviderCount] = observed;
        state.activeProviderIndex = state.observedProviderCount;
        ++state.observedProviderCount;
        state.expectedFactoryCount += context.expectedFactories.size();
        state.expectedContributionCount += contextContributionCount;
        state.activeExpectedFactories = context.expectedFactories;
        state.activeFactoryBegin = state.observedFactoryCount;
        state.providerActive = true;

        provider(state.registrar);

        state.providerActive = false;
        if (state.failure.has_value()) {
            return;
        }

        const StaticFactoryRegistrationState::ProviderObservation& activeProvider =
            state.providers[state.activeProviderIndex];
        const std::span<const StaticFactoryRegistrationState::FactoryObservation>
            providerFactories =
                std::span<const StaticFactoryRegistrationState::FactoryObservation>{state.factories}
                    .subspan(state.activeFactoryBegin,
                             state.observedFactoryCount - state.activeFactoryBegin);
        for (const StaticFactoryExpectationV1& expected : context.expectedFactories) {
            const auto registered = std::ranges::find_if(
                providerFactories,
                [&expected](const StaticFactoryRegistrationState::FactoryObservation& factory) {
                    return factory.factoryId == expected.factoryId;
                });
            if (registered == providerFactories.end()) {
                state.fail(StaticFactoryRegistrationErrorCode::FactoryMissing, &activeProvider,
                           expected.factoryId);
                return;
            }
        }
    }

    void StaticFactoryRegistrationRecorder::endComposition() noexcept {
        if (!state_) {
            return;
        }
        StaticFactoryRegistrationState& state = *state_;
        if (state.failure.has_value()) {
            return;
        }
        if (state.phase == StaticFactoryRegistrationState::Phase::NotStarted) {
            state.fail(StaticFactoryRegistrationErrorCode::CompositionNotStarted);
            return;
        }
        if (state.phase == StaticFactoryRegistrationState::Phase::Ended ||
            state.phase == StaticFactoryRegistrationState::Phase::Finished) {
            state.fail(StaticFactoryRegistrationErrorCode::CompositionAlreadyEnded);
            return;
        }
        if (state.providerActive) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderNested);
            return;
        }

        state.phase = StaticFactoryRegistrationState::Phase::Ended;
        if (state.observedProviderCount != state.capacity.providerCount) {
            state.fail(StaticFactoryRegistrationErrorCode::ProviderCountMismatch);
        } else if (state.expectedFactoryCount != state.capacity.factoryCount ||
                   state.observedFactoryCount != state.capacity.factoryCount) {
            state.fail(StaticFactoryRegistrationErrorCode::FactoryCountMismatch);
        } else if (state.expectedContributionCount != state.capacity.contributionCount ||
                   state.observedContributionCount != state.capacity.contributionCount) {
            state.fail(StaticFactoryRegistrationErrorCode::ContributionCountMismatch);
        } else {
            state.validateContributionContracts();
        }
    }

    StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    StaticFactoryRegistrationRecorder::finish() && noexcept {
        if (!state_) {
            return std::unexpected(makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::RecorderMovedFrom));
        }
        StaticFactoryRegistrationState& state = *state_;
        if (!state.failure.has_value()) {
            if (state.phase == StaticFactoryRegistrationState::Phase::NotStarted) {
                state.fail(StaticFactoryRegistrationErrorCode::CompositionNotStarted);
            } else if (state.phase == StaticFactoryRegistrationState::Phase::Recording) {
                state.fail(StaticFactoryRegistrationErrorCode::CompositionNotEnded);
            } else if (state.phase == StaticFactoryRegistrationState::Phase::Finished) {
                state.fail(StaticFactoryRegistrationErrorCode::RecorderAlreadyFinished);
            }
        }
        if (state.failure.has_value()) {
            return std::unexpected(state.owningFailure());
        }

        state.phase = StaticFactoryRegistrationState::Phase::Finished;
        return StaticFactoryCallbackTableBuilder::build(state);
    }

    void StaticFactoryRegistrar::registerFactory(
        std::string_view localFactoryId, StaticFactoryCallbacksV1 callbacks,
        std::span<const StaticContributionBindingV1> availableContributions) noexcept {
        state_->registerFactory(localFactoryId, callbacks, availableContributions);
    }

    StaticFactoryRegistrationResult<StaticFactoryRegistrationRecorder>
    createStaticFactoryRegistrationRecorder(StaticFactoryRegistrationCapacityV2 capacity) noexcept {
        if ((capacity.providerCount == 0) != (capacity.factoryCount == 0) ||
            capacity.providerCount > capacity.factoryCount || capacity.textBytes == 0 ||
            capacity.diagnosticFactoryIdBytes == 0 || capacity.diagnosticContributionIdBytes == 0 ||
            (capacity.factoryCount == 0 && capacity.contributionCount != 0)) {
            return std::unexpected(makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::InvalidCapacity));
        }
        try {
            return StaticFactoryRegistrationRecorder(
                std::make_unique<StaticFactoryRegistrationState>(capacity));
        } catch (const std::bad_alloc&) {
            return std::unexpected(makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed));
        } catch (const std::length_error&) {
            return std::unexpected(makeStaticFactoryRegistrationError(
                StaticFactoryRegistrationErrorCode::AllocationFailed));
        }
    }

} // namespace asharia::host_runtime
