#include "static_factory_callback_table_tests.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/host_runtime/static_factory_instance_token_provider_access.hpp"
#include "asharia/host_runtime/static_factory_registration.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::string_view kGenerationId =
            "sha256-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
        constexpr std::string_view kBlueprintSha256 =
            "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
        constexpr StaticFactoryRegistrationCapacityV1 kSingleFactoryCapacity{
            .providerCount = 1,
            .factoryCount = 1,
            .textBytes = 512,
            .diagnosticFactoryIdBytes = 256,
        };

        [[noreturn]] FactoryCreateResultV1
        abortCreate(FactoryCreateContextV1& unusedContext) noexcept {
            (void)unusedContext;
            std::abort();
        }

        [[noreturn]] FactoryCreateResultV1
        alternateAbortCreate(FactoryCreateContextV1& unusedContext) noexcept {
            (void)unusedContext;
            std::abort();
        }

        [[noreturn]] FactoryCallbackResultV1
        abortActivate(FactoryActivateContextV1& unusedContext,
                      FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedContext;
            (void)unusedInstance;
            std::abort();
        }

        [[noreturn]] FactoryCallbackResultV1
        abortQuiesce(FactoryQuiesceContextV1& unusedContext,
                     FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedContext;
            (void)unusedInstance;
            std::abort();
        }

        [[noreturn]] FactoryCallbackResultV1
        abortDeactivate(FactoryDeactivateContextV1& unusedContext,
                        FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedContext;
            (void)unusedInstance;
            std::abort();
        }

        [[noreturn]] void abortDestroy(FactoryInstanceTokenV1 unusedInstance) noexcept {
            (void)unusedInstance;
            std::abort();
        }

        [[nodiscard]] StaticCompositionRegistrationContextV1 compositionContext() noexcept {
            return {
                .generationId = kGenerationId,
                .hostActivationBlueprintSha256 = kBlueprintSha256,
                .capacity = kSingleFactoryCapacity,
            };
        }

        [[nodiscard]] StaticFactoryProviderContextV1 providerContext() noexcept {
            static constexpr std::array<std::string_view, 1> kFactories{"service"};
            return {
                .packageId = "com.asharia.test.callbacks",
                .packageVersion = "1.0.0",
                .moduleId = "runtime",
                .entryPoint = "asharia::host_runtime::tests::provideCallbacks",
                .expectedFactoryIds = kFactories,
            };
        }

        void provideCompleteCallbacks(StaticFactoryRegistrar& registrar) noexcept {
            registrar.registerFactory("service", abortingCallbacks());
        }

        void provideAlternateCallbacks(StaticFactoryRegistrar& registrar) noexcept {
            StaticFactoryCallbacksV1 callbacks = abortingCallbacks();
            callbacks.create = &alternateAbortCreate;
            registrar.registerFactory("service", callbacks);
        }

        void provideWithoutCreate(StaticFactoryRegistrar& registrar) noexcept {
            StaticFactoryCallbacksV1 callbacks = abortingCallbacks();
            callbacks.create = nullptr;
            registrar.registerFactory("service", callbacks);
        }

        void provideWithoutActivate(StaticFactoryRegistrar& registrar) noexcept {
            StaticFactoryCallbacksV1 callbacks = abortingCallbacks();
            callbacks.activate = nullptr;
            registrar.registerFactory("service", callbacks);
        }

        void provideWithoutQuiesce(StaticFactoryRegistrar& registrar) noexcept {
            StaticFactoryCallbacksV1 callbacks = abortingCallbacks();
            callbacks.quiesce = nullptr;
            registrar.registerFactory("service", callbacks);
        }

        void provideWithoutDeactivate(StaticFactoryRegistrar& registrar) noexcept {
            StaticFactoryCallbacksV1 callbacks = abortingCallbacks();
            callbacks.deactivate = nullptr;
            registrar.registerFactory("service", callbacks);
        }

        void provideWithoutDestroy(StaticFactoryRegistrar& registrar) noexcept {
            StaticFactoryCallbacksV1 callbacks = abortingCallbacks();
            callbacks.destroy = nullptr;
            registrar.registerFactory("service", callbacks);
        }

        [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
        collectTable(StaticFactoryProviderV2 provider) noexcept {
            auto recorderResult = createStaticFactoryRegistrationRecorder(kSingleFactoryCapacity);
            if (!recorderResult) {
                return std::unexpected(std::move(recorderResult.error()));
            }
            auto recorder = std::move(*recorderResult);
            recorder.beginComposition(compositionContext());
            recorder.invokeProvider(providerContext(), provider);
            recorder.endComposition();
            return std::move(recorder).finish();
        }

        [[nodiscard]] bool tokenAndResultContractsTransferOwnership() noexcept {
            static_assert(!std::is_copy_constructible_v<FactoryInstanceTokenV1>);
            static_assert(!std::is_move_assignable_v<FactoryInstanceTokenV1>);
            static_assert(!std::is_default_constructible_v<FactoryCallbackResultV1>);
            static_assert(!std::is_default_constructible_v<FactoryCreateResultV1>);
            static_assert(std::is_trivially_copyable_v<StaticFactoryCallbacksV1>);
            using ThrowingCreate = FactoryCreateResultV1 (*)(FactoryCreateContextV1&);
            using WrongReturnCreate = FactoryCallbackResultV1 (*)(FactoryCreateContextV1&) noexcept;
            using WrongArgumentCreate =
                FactoryCreateResultV1 (*)(FactoryActivateContextV1&) noexcept;
            static_assert(!std::is_convertible_v<ThrowingCreate, FactoryCreateCallbackV1>);
            static_assert(!std::is_convertible_v<WrongReturnCreate, FactoryCreateCallbackV1>);
            static_assert(!std::is_convertible_v<WrongArgumentCreate, FactoryCreateCallbackV1>);

            int instance = 42;
            FactoryInstanceTokenV1 token =
                FactoryInstanceTokenProviderAccessV1::fromPointer(&instance);
            if (!token.isValid() ||
                FactoryInstanceTokenProviderAccessV1::pointer(token.view()) != &instance) {
                return false;
            }

            FactoryInstanceTokenV1 moved = std::move(token);
            // This assertion intentionally inspects the contract-defined moved-from state.
            // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
            if (token.isValid() || !moved.isValid()) {
                return false;
            }

            FactoryCreateResultV1 created = FactoryCreateResultV1::succeeded(std::move(moved));
            // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
            if (moved.isValid() || !created.result().isSucceeded() ||
                created.result().localCode() != 0 || !created.instanceView().isValid()) {
                return false;
            }

            FactoryInstanceTokenV1 owned = std::move(created).takeInstance();
            void* consumed = FactoryInstanceTokenProviderAccessV1::consume(std::move(owned));
            // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
            if (owned.isValid() || consumed != &instance) {
                return false;
            }

            const FactoryCallbackResultV1 failed = FactoryCallbackResultV1::failed(73);
            const FactoryCreateResultV1 invalidSuccess =
                FactoryCreateResultV1::succeeded(FactoryInstanceTokenV1{});
            return !failed.isSucceeded() && failed.localCode() == 73 &&
                   !invalidSuccess.result().isSucceeded() &&
                   !invalidSuccess.instanceView().isValid();
        }

        [[nodiscard]] bool completeDescriptorProducesIdentityOnlyTable() noexcept {
            auto tableResult = collectTable(&provideCompleteCallbacks);
            if (!tableResult) {
                return false;
            }
            const StaticFactoryCallbackTableV1& table = *tableResult;
            const StaticFactoryRegistrationSnapshotV1& first = table.registrationSnapshot();
            const StaticFactoryRegistrationSnapshotV1& second = table.registrationSnapshot();
            return &first == &second && first.generationId == kGenerationId &&
                   first.hostActivationBlueprintSha256 == kBlueprintSha256 &&
                   first.registrations.size() == 1 && first.registrations[0].factoryId == "service";
        }

        [[nodiscard]] bool missingCallbacksFailByLifecycleSlot() noexcept {
            using Case = std::pair<StaticFactoryProviderV2, StaticFactoryRegistrationErrorCode>;
            constexpr std::array<Case, 5> cases{
                Case{&provideWithoutCreate,
                     StaticFactoryRegistrationErrorCode::FactoryCreateCallbackMissing},
                Case{&provideWithoutActivate,
                     StaticFactoryRegistrationErrorCode::FactoryActivateCallbackMissing},
                Case{&provideWithoutQuiesce,
                     StaticFactoryRegistrationErrorCode::FactoryQuiesceCallbackMissing},
                Case{&provideWithoutDeactivate,
                     StaticFactoryRegistrationErrorCode::FactoryDeactivateCallbackMissing},
                Case{&provideWithoutDestroy,
                     StaticFactoryRegistrationErrorCode::FactoryDestroyCallbackMissing},
            };

            return std::ranges::all_of(cases, [](const Case& testCase) {
                const auto& [provider, expected] = testCase;
                const auto tableResult = collectTable(provider);
                return !tableResult && tableResult.error().code == expected &&
                       tableResult.error().factoryId == "service";
            });
        }

        [[nodiscard]] bool callbackAddressesDoNotChangeIdentityProjection() noexcept {
            const auto first = collectTable(&provideCompleteCallbacks);
            const auto second = collectTable(&provideAlternateCallbacks);
            return first && second &&
                   first->registrationSnapshot() == second->registrationSnapshot();
        }

        [[nodiscard]] bool tableOwnsStackIdentityStorage() noexcept {
            auto recorderResult = createStaticFactoryRegistrationRecorder(kSingleFactoryCapacity);
            if (!recorderResult) {
                return false;
            }
            auto recorder = std::move(*recorderResult);
            {
                const auto generationId = std::to_array(
                    "sha256-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
                const auto blueprintSha256 = std::to_array(
                    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
                recorder.beginComposition({
                    .generationId = std::string_view{generationId.data(), generationId.size() - 1},
                    .hostActivationBlueprintSha256 =
                        std::string_view{blueprintSha256.data(), blueprintSha256.size() - 1},
                    .capacity = kSingleFactoryCapacity,
                });

                const auto packageId = std::to_array("com.asharia.test.callbacks");
                const auto packageVersion = std::to_array("1.0.0");
                const auto moduleId = std::to_array("runtime");
                const auto entryPoint =
                    std::to_array("asharia::host_runtime::tests::provideCallbacks");
                const auto factoryId = std::to_array("service");
                const std::array<std::string_view, 1> factories{
                    std::string_view{factoryId.data(), factoryId.size() - 1}};
                recorder.invokeProvider(
                    {
                        .packageId = std::string_view{packageId.data(), packageId.size() - 1},
                        .packageVersion =
                            std::string_view{packageVersion.data(), packageVersion.size() - 1},
                        .moduleId = std::string_view{moduleId.data(), moduleId.size() - 1},
                        .entryPoint = std::string_view{entryPoint.data(), entryPoint.size() - 1},
                        .expectedFactoryIds = factories,
                    },
                    &provideCompleteCallbacks);
            }
            recorder.endComposition();
            const auto table = std::move(recorder).finish();
            return table && table->registrationSnapshot().generationId == kGenerationId &&
                   table->registrationSnapshot().registrations.front().factoryId == "service";
        }

    } // namespace

    StaticFactoryCallbacksV1 abortingCallbacks() noexcept {
        return {
            .create = &abortCreate,
            .activate = &abortActivate,
            .quiesce = &abortQuiesce,
            .deactivate = &abortDeactivate,
            .destroy = &abortDestroy,
        };
    }

    bool runStaticFactoryCallbackTableTests() noexcept {
        using Test = bool (*)() noexcept;
        constexpr std::array<Test, 5> tests{
            &tokenAndResultContractsTransferOwnership,
            &completeDescriptorProducesIdentityOnlyTable,
            &missingCallbacksFailByLifecycleSlot,
            &callbackAddressesDoNotChangeIdentityProjection,
            &tableOwnsStackIdentityStorage,
        };

        return std::ranges::all_of(tests, [](const Test test) { return test(); });
    }

    [[noreturn]] void runValidTokenDropProbe() noexcept {
        std::set_terminate([]() noexcept { std::_Exit(86); });
        int instance = 42;
        {
            [[maybe_unused]] FactoryInstanceTokenV1 token =
                FactoryInstanceTokenProviderAccessV1::fromPointer(&instance);
        }
        std::_Exit(0);
    }

} // namespace asharia::host_runtime::tests
