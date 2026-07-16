#include "process_scope_synthetic_provider.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "asharia/host_runtime/factory_lifecycle_contexts.hpp"
#include "asharia/host_runtime/static_factory_instance_token_provider_access.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        struct FailureSlot final {
            std::uint32_t localCode{};
            bool enabled{};
        };

        struct FactoryState final {
            std::array<std::size_t, kSyntheticLifecyclePhaseCount> invocationCounts{};
            std::uint8_t observedDependencyMask{};
            std::size_t tokenIssueCount{};
            std::size_t destroyCount{};
            bool attributionMismatch{};
            bool generationMismatch{};
            bool dependencyMismatch{};
            bool instanceMismatch{};
            bool duplicateTokenIssue{};
            bool duplicateDestroy{};
            bool tokenOutstanding{};
            bool active{};
        };

        struct SyntheticProviderFixture final {
            std::array<FactoryState, kSyntheticFactoryCount> factories{};
            std::array<std::array<FailureSlot, kSyntheticLifecyclePhaseCount>,
                       kSyntheticFactoryCount>
                failures{};
            std::array<SyntheticLifecycleEventV1, kSyntheticTraceCapacity> trace{};
            std::size_t traceSize{};
            SyntheticLifecycleHookV1 lifecycleHook{};
            bool lifecycleHookInvoked{};
            bool lifecycleHookInProgress{};
            bool traceOverflow{};
        };

        // The fixture is single-control-thread test state. Keeping it fixed-size
        // makes every provider callback noexcept and allocation-free.
        SyntheticProviderFixture
            fixture; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        [[nodiscard]] constexpr std::size_t factoryIndex(SyntheticFactoryV1 factory) noexcept {
            return static_cast<std::size_t>(factory);
        }

        [[nodiscard]] constexpr std::size_t phaseIndex(SyntheticLifecyclePhaseV1 phase) noexcept {
            return static_cast<std::size_t>(phase);
        }

        [[nodiscard]] constexpr bool isFactoryValid(SyntheticFactoryV1 factory) noexcept {
            return factoryIndex(factory) < kSyntheticFactoryCount;
        }

        [[nodiscard]] constexpr bool isPhaseValid(SyntheticLifecyclePhaseV1 phase) noexcept {
            return phaseIndex(phase) < kSyntheticLifecyclePhaseCount;
        }

        [[nodiscard]] constexpr std::uint8_t factoryMask(SyntheticFactoryV1 factory) noexcept {
            return std::uint8_t{1} << static_cast<std::uint8_t>(factory);
        }

        [[nodiscard]] FactoryState& stateFor(SyntheticFactoryV1 factory) noexcept {
            return fixture.factories.at(factoryIndex(factory));
        }

        [[nodiscard]] bool isProjectOnly(SyntheticFactoryV1 factory) noexcept {
            return factory == SyntheticFactoryV1::ProjectOnly;
        }

        void recordInvocation(SyntheticFactoryV1 factory,
                              SyntheticLifecyclePhaseV1 phase) noexcept {
            FactoryState& state = stateFor(factory);
            ++state.invocationCounts.at(phaseIndex(phase));
            if (fixture.traceSize < fixture.trace.size()) {
                fixture.trace.at(fixture.traceSize) = {
                    .factory = factory,
                    .phase = phase,
                };
                ++fixture.traceSize;
            } else {
                fixture.traceOverflow = true;
            }

            if (fixture.lifecycleHook == nullptr || fixture.lifecycleHookInvoked ||
                fixture.lifecycleHookInProgress) {
                return;
            }
            const SyntheticLifecycleHookV1 hook = fixture.lifecycleHook;
            fixture.lifecycleHookInvoked = true;
            fixture.lifecycleHookInProgress = true;
            hook(factory, phase);
            fixture.lifecycleHookInProgress = false;
        }

        [[nodiscard]] bool sameFactory(ExactFactoryReferenceViewV1 reference,
                                       SyntheticFactoryV1 factory) noexcept {
            return reference.packageId == kSyntheticPackageId &&
                   reference.packageVersion == kSyntheticPackageVersion &&
                   reference.moduleId == kSyntheticModuleId &&
                   reference.factoryId == syntheticFactoryId(factory);
        }

        void validateAttribution(SyntheticFactoryV1 factory,
                                 FactoryAttributionViewV1 attribution) noexcept {
            FactoryState& state = stateFor(factory);
            state.generationMismatch |=
                attribution.engineGenerationId != kSyntheticEngineGenerationId;
            state.attributionMismatch |= !sameFactory(attribution.factory, factory);
        }

        [[nodiscard]] std::optional<SyntheticFactoryV1>
        identifyFactory(ExactFactoryReferenceViewV1 reference) noexcept {
            constexpr std::array<SyntheticFactoryV1, kSyntheticFactoryCount> factories{
                SyntheticFactoryV1::Middle,
                SyntheticFactoryV1::ProjectOnly,
                SyntheticFactoryV1::Leaf,
                SyntheticFactoryV1::Root,
            };
            for (const SyntheticFactoryV1 factory : factories) {
                if (sameFactory(reference, factory)) {
                    return factory;
                }
            }
            return std::nullopt;
        }

        void validateDependencies(SyntheticFactoryV1 factory,
                                  std::span<const FactoryDependencyViewV1> dependencies) noexcept {
            FactoryState& state = stateFor(factory);
            std::uint8_t observedMask = 0;
            for (const FactoryDependencyViewV1& dependency : dependencies) {
                const std::optional<SyntheticFactoryV1> dependencyFactory =
                    identifyFactory(dependency.factory);
                if (!dependencyFactory || *dependencyFactory == SyntheticFactoryV1::ProjectOnly) {
                    state.dependencyMismatch = true;
                    continue;
                }

                const std::uint8_t bit = factoryMask(*dependencyFactory);
                if ((observedMask & bit) != 0) {
                    state.dependencyMismatch = true;
                }
                observedMask |= bit;

                FactoryState& dependencyState = stateFor(*dependencyFactory);
                void* const expectedInstance = &dependencyState;
                if (!dependency.instance.isValid() || !dependencyState.active ||
                    !dependencyState.tokenOutstanding ||
                    FactoryInstanceTokenProviderAccessV1::pointer(dependency.instance) !=
                        expectedInstance) {
                    state.instanceMismatch = true;
                }
            }
            state.observedDependencyMask = observedMask;
            state.dependencyMismatch |= observedMask != syntheticExpectedDependencyMask(factory);
        }

        void validateInstance(SyntheticFactoryV1 factory, FactoryInstanceViewV1 instance,
                              bool requireActive) noexcept {
            FactoryState& state = stateFor(factory);
            void* const expectedInstance = &state;
            const bool activeMismatch = requireActive ? !state.active : state.active;
            if (!instance.isValid() || !state.tokenOutstanding || activeMismatch ||
                FactoryInstanceTokenProviderAccessV1::pointer(instance) != expectedInstance) {
                state.instanceMismatch = true;
            }
        }

        [[nodiscard]] std::optional<std::uint32_t>
        injectedFailure(SyntheticFactoryV1 factory, SyntheticLifecyclePhaseV1 phase) noexcept {
            const FailureSlot& slot =
                fixture.failures.at(factoryIndex(factory)).at(phaseIndex(phase));
            if (!slot.enabled) {
                return std::nullopt;
            }
            return slot.localCode;
        }

        template <SyntheticFactoryV1 Factory>
        [[nodiscard]] FactoryCreateResultV1 create(FactoryCreateContextV1& context) noexcept {
            recordInvocation(Factory, SyntheticLifecyclePhaseV1::Create);
            validateAttribution(Factory, context.attribution());
            validateDependencies(Factory, context.dependencies());

            if (const auto failure = injectedFailure(Factory, SyntheticLifecyclePhaseV1::Create)) {
                return FactoryCreateResultV1::failed(*failure);
            }

            FactoryState& state = stateFor(Factory);
            if (state.tokenOutstanding || state.tokenIssueCount != state.destroyCount) {
                state.duplicateTokenIssue = true;
            }
            ++state.tokenIssueCount;
            state.tokenOutstanding = true;
            state.active = false;
            return FactoryCreateResultV1::succeeded(
                FactoryInstanceTokenProviderAccessV1::fromPointer(&state));
        }

        template <SyntheticFactoryV1 Factory>
        [[nodiscard]] FactoryCallbackResultV1 activate(FactoryActivateContextV1& context,
                                                       FactoryInstanceViewV1 instance) noexcept {
            recordInvocation(Factory, SyntheticLifecyclePhaseV1::Activate);
            validateAttribution(Factory, context.attribution());
            validateInstance(Factory, instance, false);

            if (const auto failure =
                    injectedFailure(Factory, SyntheticLifecyclePhaseV1::Activate)) {
                return FactoryCallbackResultV1::failed(*failure);
            }
            stateFor(Factory).active = true;
            return FactoryCallbackResultV1::succeeded();
        }

        template <SyntheticFactoryV1 Factory>
        [[nodiscard]] FactoryCallbackResultV1 quiesce(FactoryQuiesceContextV1& context,
                                                      FactoryInstanceViewV1 instance) noexcept {
            recordInvocation(Factory, SyntheticLifecyclePhaseV1::Quiesce);
            validateAttribution(Factory, context.attribution());
            validateInstance(Factory, instance, true);

            if (const auto failure = injectedFailure(Factory, SyntheticLifecyclePhaseV1::Quiesce)) {
                return FactoryCallbackResultV1::failed(*failure);
            }
            return FactoryCallbackResultV1::succeeded();
        }

        template <SyntheticFactoryV1 Factory>
        [[nodiscard]] FactoryCallbackResultV1 deactivate(FactoryDeactivateContextV1& context,
                                                         FactoryInstanceViewV1 instance) noexcept {
            recordInvocation(Factory, SyntheticLifecyclePhaseV1::Deactivate);
            validateAttribution(Factory, context.attribution());
            validateInstance(Factory, instance, true);
            stateFor(Factory).active = false;

            if (const auto failure =
                    injectedFailure(Factory, SyntheticLifecyclePhaseV1::Deactivate)) {
                return FactoryCallbackResultV1::failed(*failure);
            }
            return FactoryCallbackResultV1::succeeded();
        }

        template <SyntheticFactoryV1 Factory>
        void destroy(FactoryInstanceTokenV1 instance) noexcept {
            recordInvocation(Factory, SyntheticLifecyclePhaseV1::Destroy);
            FactoryState& state = stateFor(Factory);
            ++state.destroyCount;
            void* const pointer =
                FactoryInstanceTokenProviderAccessV1::consume(std::move(instance));
            if (pointer != &state) {
                state.instanceMismatch = true;
            }
            if (!state.tokenOutstanding || state.destroyCount > state.tokenIssueCount) {
                state.duplicateDestroy = true;
            }
            if (state.active) {
                state.instanceMismatch = true;
            }
            state.tokenOutstanding = false;
        }

        template <SyntheticFactoryV1 Factory>
        [[nodiscard]] StaticFactoryCallbacksV1 callbacks() noexcept {
            return {
                .create = &create<Factory>,
                .activate = &activate<Factory>,
                .quiesce = &quiesce<Factory>,
                .deactivate = &deactivate<Factory>,
                .destroy = &destroy<Factory>,
            };
        }

        void provideSyntheticProcessScopeFactories(StaticFactoryRegistrar& registrar) noexcept {
            // Registration is canonical (a, b, m, z), deliberately unlike the
            // sealed ProcessScope plan (z, a, m).
            registrar.registerFactory(kSyntheticMiddleFactoryId,
                                      callbacks<SyntheticFactoryV1::Middle>());
            registrar.registerFactory(kSyntheticProjectOnlyFactoryId,
                                      callbacks<SyntheticFactoryV1::ProjectOnly>());
            registrar.registerFactory(kSyntheticLeafFactoryId,
                                      callbacks<SyntheticFactoryV1::Leaf>());
            registrar.registerFactory(kSyntheticRootFactoryId,
                                      callbacks<SyntheticFactoryV1::Root>());
        }

        [[nodiscard]] bool stateValid(SyntheticFactoryV1 factory,
                                      const FactoryState& state) noexcept {
            if (state.attributionMismatch || state.generationMismatch || state.dependencyMismatch ||
                state.instanceMismatch || state.duplicateTokenIssue || state.duplicateDestroy ||
                state.tokenIssueCount < state.destroyCount) {
                return false;
            }
            if (isProjectOnly(factory)) {
                for (const std::size_t count : state.invocationCounts) {
                    if (count != 0) {
                        return false;
                    }
                }
            }
            return true;
        }

    } // namespace

    std::string_view syntheticFactoryId(SyntheticFactoryV1 factory) noexcept {
        switch (factory) {
        case SyntheticFactoryV1::Middle:
            return kSyntheticMiddleFactoryId;
        case SyntheticFactoryV1::ProjectOnly:
            return kSyntheticProjectOnlyFactoryId;
        case SyntheticFactoryV1::Leaf:
            return kSyntheticLeafFactoryId;
        case SyntheticFactoryV1::Root:
            return kSyntheticRootFactoryId;
        case SyntheticFactoryV1::Count:
            break;
        }
        return {};
    }

    std::uint8_t syntheticExpectedDependencyMask(SyntheticFactoryV1 factory) noexcept {
        switch (factory) {
        case SyntheticFactoryV1::Middle:
            return kSyntheticMiddleDependencyMask;
        case SyntheticFactoryV1::Leaf:
            return kSyntheticLeafDependencyMask;
        case SyntheticFactoryV1::ProjectOnly:
        case SyntheticFactoryV1::Root:
        case SyntheticFactoryV1::Count:
            return 0;
        }
        return 0;
    }

    StaticFactoryRegistrationCapacityV1 syntheticRegistrationCapacity() noexcept {
        return {
            .providerCount = 1,
            .factoryCount = kSyntheticFactoryCount,
            .textBytes = 1024,
            .diagnosticFactoryIdBytes = 256,
        };
    }

    void recordSyntheticFactoryProviders(StaticFactoryRegistrationRecorder& recorder) noexcept {
        recorder.beginComposition({
            .generationId = kSyntheticCompositionGenerationId,
            .hostActivationBlueprintSha256 = kSyntheticBlueprintSha256,
            .capacity = syntheticRegistrationCapacity(),
        });
        constexpr std::array<std::string_view, kSyntheticFactoryCount> expectedFactories{
            kSyntheticMiddleFactoryId,
            kSyntheticProjectOnlyFactoryId,
            kSyntheticLeafFactoryId,
            kSyntheticRootFactoryId,
        };
        recorder.invokeProvider(
            {
                .packageId = kSyntheticPackageId,
                .packageVersion = kSyntheticPackageVersion,
                .moduleId = kSyntheticModuleId,
                .entryPoint = kSyntheticProviderEntryPoint,
                .expectedFactoryIds = expectedFactories,
            },
            &provideSyntheticProcessScopeFactories);
        recorder.endComposition();
    }

    void resetSyntheticProviderFixture() noexcept {
        fixture = {};
    }

    void clearSyntheticProviderFailures() noexcept {
        fixture.failures = {};
    }

    void armSyntheticProviderLifecycleHook(SyntheticLifecycleHookV1 hook) noexcept {
        fixture.lifecycleHook = hook;
        fixture.lifecycleHookInvoked = false;
        fixture.lifecycleHookInProgress = false;
    }

    bool injectSyntheticProviderFailure(SyntheticFactoryV1 factory, SyntheticLifecyclePhaseV1 phase,
                                        std::uint32_t localCode) noexcept {
        if (!isFactoryValid(factory) || !isPhaseValid(phase) ||
            phase == SyntheticLifecyclePhaseV1::Destroy) {
            return false;
        }
        fixture.failures.at(factoryIndex(factory)).at(phaseIndex(phase)) = {
            .localCode = localCode,
            .enabled = true,
        };
        return true;
    }

    std::span<const SyntheticLifecycleEventV1> syntheticProviderTrace() noexcept {
        return {fixture.trace.data(), fixture.traceSize};
    }

    std::size_t syntheticProviderInvocationCount(SyntheticFactoryV1 factory,
                                                 SyntheticLifecyclePhaseV1 phase) noexcept {
        if (!isFactoryValid(factory) || !isPhaseValid(phase)) {
            return 0;
        }
        return stateFor(factory).invocationCounts.at(phaseIndex(phase));
    }

    SyntheticFactoryObservationV1
    syntheticProviderObservation(SyntheticFactoryV1 factory) noexcept {
        if (!isFactoryValid(factory)) {
            return {};
        }
        const FactoryState& state = stateFor(factory);
        return {
            .invocationCounts = state.invocationCounts,
            .expectedDependencyMask = syntheticExpectedDependencyMask(factory),
            .observedDependencyMask = state.observedDependencyMask,
            .tokenIssueCount = state.tokenIssueCount,
            .destroyCount = state.destroyCount,
            .attributionMismatch = state.attributionMismatch,
            .generationMismatch = state.generationMismatch,
            .dependencyMismatch = state.dependencyMismatch,
            .instanceMismatch = state.instanceMismatch,
            .duplicateTokenIssue = state.duplicateTokenIssue,
            .duplicateDestroy = state.duplicateDestroy,
            .tokenOutstanding = state.tokenOutstanding,
        };
    }

    std::size_t syntheticProjectOnlyInvocationCount() noexcept {
        const FactoryState& state = stateFor(SyntheticFactoryV1::ProjectOnly);
        std::size_t count = 0;
        for (const std::size_t phaseCount : state.invocationCounts) {
            count += phaseCount;
        }
        return count;
    }

    bool syntheticProviderTraceOverflowed() noexcept {
        return fixture.traceOverflow;
    }

    bool syntheticProviderFixtureValid() noexcept {
        if (fixture.traceOverflow) {
            return false;
        }
        constexpr std::array<SyntheticFactoryV1, kSyntheticFactoryCount> factories{
            SyntheticFactoryV1::Middle,
            SyntheticFactoryV1::ProjectOnly,
            SyntheticFactoryV1::Leaf,
            SyntheticFactoryV1::Root,
        };
        return std::ranges::all_of(factories, [](SyntheticFactoryV1 factory) {
            return stateValid(factory, stateFor(factory));
        });
    }

} // namespace asharia::host_runtime::tests
