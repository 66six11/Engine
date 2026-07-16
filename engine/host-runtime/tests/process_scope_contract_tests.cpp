#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

#if __has_include("asharia/host_runtime/static_factory_instance_token_provider_access.hpp")
#error "provider-only token bridge leaked into ProcessScope consumer tests"
#endif

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] std::optional<ProcessScopeExecutorV1>
        preparedExecutor(ProcessPlanMutationV1 mutation = ProcessPlanMutationV1::None) {
            auto admitted = makeAdmittedSyntheticProcessScope(mutation);
            if (!admitted) {
                return std::nullopt;
            }
            auto prepared = prepareProcessScopeExecutor(std::move(*admitted));
            if (!prepared) {
                return std::nullopt;
            }
            return std::move(*prepared);
        }

        [[nodiscard]] bool publicContractsAreNarrowAndLinear() {
            static_assert(!std::is_default_constructible_v<FactoryCreateContextV1>);
            static_assert(!std::is_copy_constructible_v<FactoryCreateContextV1>);
            static_assert(!std::is_move_constructible_v<FactoryCreateContextV1>);
            static_assert(!std::is_default_constructible_v<FactoryActivateContextV1>);
            static_assert(!std::is_default_constructible_v<FactoryQuiesceContextV1>);
            static_assert(!std::is_default_constructible_v<FactoryDeactivateContextV1>);

            static_assert(!std::is_default_constructible_v<ProcessScopeExecutorV1>);
            static_assert(!std::is_copy_constructible_v<ProcessScopeExecutorV1>);
            static_assert(std::is_move_constructible_v<ProcessScopeExecutorV1>);
            static_assert(!std::is_move_assignable_v<ProcessScopeExecutorV1>);

            using PrepareFunction =
                ProcessScopePreparationResultV1 (*)(AdmittedStaticFactoryCallbackTableV1) noexcept;
            static_assert(std::is_same_v<decltype(&prepareProcessScopeExecutor), PrepareFunction>);
            static_assert(!std::is_invocable_v<PrepareFunction, StaticFactoryCallbackTableV1>);
            static_assert(!std::is_invocable_v<PrepareFunction, PendingActivationFactoryTableV1>);
            return true;
        }

        [[nodiscard]] bool emptyScopeStartsAndStopsWithoutCallbacks() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor(ProcessPlanMutationV1::Empty);
            if (!executor || executor->state() != ProcessScopeStateV1::Prepared) {
                return false;
            }
            const auto started = executor->start();
            if (!started || executor->state() != ProcessScopeStateV1::Active ||
                !syntheticProviderTrace().empty()) {
                return false;
            }
            const auto stopped = executor->stop();
            return stopped && stopped->callbacksSucceeded() &&
                   executor->state() == ProcessScopeStateV1::Stopped &&
                   syntheticProviderTrace().empty() && syntheticProjectOnlyInvocationCount() == 0;
        }

        [[nodiscard]] bool stateMisuseDoesNotReplayCallbacks() {
            resetSyntheticProviderFixture();
            auto executor = preparedExecutor();
            if (!executor) {
                return false;
            }
            const auto stopBeforeStart = executor->stop();
            if (stopBeforeStart ||
                stopBeforeStart.error().code != ProcessScopeErrorCodeV1::StopRequiresActive ||
                !syntheticProviderTrace().empty()) {
                return false;
            }
            if (!executor->start()) {
                return false;
            }
            const std::size_t startedTraceSize = syntheticProviderTrace().size();
            const auto doubleStart = executor->start();
            if (doubleStart ||
                doubleStart.error().operation.code !=
                    ProcessScopeErrorCodeV1::StartRequiresPrepared ||
                syntheticProviderTrace().size() != startedTraceSize) {
                return false;
            }
            const auto stopped = executor->stop();
            if (!stopped) {
                return false;
            }
            const std::size_t stoppedTraceSize = syntheticProviderTrace().size();
            const auto doubleStop = executor->stop();
            const auto restart = executor->start();
            return !doubleStop &&
                   doubleStop.error().code == ProcessScopeErrorCodeV1::StopRequiresActive &&
                   !restart &&
                   restart.error().operation.code ==
                       ProcessScopeErrorCodeV1::StartRequiresPrepared &&
                   syntheticProviderTrace().size() == stoppedTraceSize;
        }

        [[nodiscard]] bool movedFromAndWrongThreadOperationsFailClosed() {
            resetSyntheticProviderFixture();
            auto source = preparedExecutor();
            if (!source) {
                return false;
            }
            ProcessScopeExecutorV1 destination = std::move(*source);
            const auto movedFromStart = source->start();
            if (source->state() != ProcessScopeStateV1::MovedFrom || movedFromStart ||
                movedFromStart.error().operation.code !=
                    ProcessScopeErrorCodeV1::ExecutorMovedFrom) {
                return false;
            }

            bool wrongStartRejected = false;
            ProcessScopeErrorCodeV1 wrongStartCode{};
            std::jthread startWorker([&]() {
                const auto result = destination.start();
                wrongStartRejected = !result;
                if (!result) {
                    wrongStartCode = result.error().operation.code;
                }
            });
            startWorker.join();
            if (!wrongStartRejected ||
                wrongStartCode != ProcessScopeErrorCodeV1::WrongControlThread ||
                destination.state() != ProcessScopeStateV1::Prepared ||
                !syntheticProviderTrace().empty() || !destination.start()) {
                return false;
            }

            const std::size_t activeTraceSize = syntheticProviderTrace().size();
            bool wrongStopRejected = false;
            ProcessScopeErrorCodeV1 wrongStopCode{};
            std::jthread stopWorker([&]() {
                const auto result = destination.stop();
                wrongStopRejected = !result;
                if (!result) {
                    wrongStopCode = result.error().code;
                }
            });
            stopWorker.join();
            return wrongStopRejected &&
                   wrongStopCode == ProcessScopeErrorCodeV1::WrongControlThread &&
                   destination.state() == ProcessScopeStateV1::Active &&
                   syntheticProviderTrace().size() == activeTraceSize &&
                   destination.stop().has_value();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopeContractTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "public contracts are narrow and linear",
                .function = &publicContractsAreNarrowAndLinear,
            },
            NamedProcessScopeTestV1{
                .name = "empty scope starts and stops without callbacks",
                .function = &emptyScopeStartsAndStopsWithoutCallbacks,
            },
            NamedProcessScopeTestV1{
                .name = "state misuse does not replay callbacks",
                .function = &stateMisuseDoesNotReplayCallbacks,
            },
            NamedProcessScopeTestV1{
                .name = "moved-from and wrong-thread operations fail closed",
                .function = &movedFromAndWrongThreadOperationsFailClosed,
            },
        };
        return tests;
    }

    [[noreturn]] void runActiveProcessScopeDropProbe() {
        resetSyntheticProviderFixture();
        auto executor = preparedExecutor();
        if (!executor || !executor->start()) {
            std::_Exit(2);
        }
        std::set_terminate([]() noexcept {
            constexpr std::string_view marker{"asharia-process-scope-active-drop-terminated\n"};
            (void)std::fwrite(marker.data(), 1, marker.size(), stderr);
            (void)std::fflush(stderr);
            std::_Exit(87);
        });
        executor.reset();
        std::_Exit(0);
    }

} // namespace asharia::host_runtime::tests
