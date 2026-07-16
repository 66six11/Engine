#include "static_contribution_accessor_tests.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/host_runtime/static_factory_instance_token_provider_access.hpp"
#include "asharia/host_runtime/static_factory_registration.hpp"

#include "static_factory_callback_table_private_access.hpp"
#include "static_factory_callback_table_tests.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::string_view kGenerationId =
            "sha256-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        constexpr std::string_view kBlueprintSha256 =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

        struct LeadingPayloadInterface {
            LeadingPayloadInterface() = default;
            virtual ~LeadingPayloadInterface() = default;
            LeadingPayloadInterface(const LeadingPayloadInterface&) = delete;
            LeadingPayloadInterface& operator=(const LeadingPayloadInterface&) = delete;
            LeadingPayloadInterface(LeadingPayloadInterface&&) = delete;
            LeadingPayloadInterface& operator=(LeadingPayloadInterface&&) = delete;

            [[nodiscard]] virtual std::uint32_t leadingValue() const noexcept = 0;
        };

        struct RuntimeServiceContract {
            static constexpr std::string_view kind{"com.asharia.test.runtime-service"};
            static constexpr StaticContributionCardinalityV1 cardinality{
                StaticContributionCardinalityV1::Single};

            RuntimeServiceContract() = default;
            virtual ~RuntimeServiceContract() = default;
            RuntimeServiceContract(const RuntimeServiceContract&) = delete;
            RuntimeServiceContract& operator=(const RuntimeServiceContract&) = delete;
            RuntimeServiceContract(RuntimeServiceContract&&) = delete;
            RuntimeServiceContract& operator=(RuntimeServiceContract&&) = delete;

            [[nodiscard]] virtual std::uint32_t serviceValue() const noexcept = 0;
        };

        struct OtherRuntimeServiceContract {
            static constexpr std::string_view kind{"com.asharia.test.other-runtime-service"};
            static constexpr StaticContributionCardinalityV1 cardinality{
                StaticContributionCardinalityV1::Multiple};
        };

        struct RuntimeServicePayload final : LeadingPayloadInterface, RuntimeServiceContract {
            [[nodiscard]] std::uint32_t leadingValue() const noexcept override {
                return 17;
            }

            [[nodiscard]] std::uint32_t serviceValue() const noexcept override {
                return 29;
            }
        };

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        std::size_t selectedAccessorInvocationCount{};
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        std::size_t unselectedAccessorInvocationCount{};

        [[nodiscard]] RuntimeServiceContract*
        accessRuntimeService(FactoryInstanceViewV1 instance) noexcept {
            ++selectedAccessorInvocationCount;
            auto* payload = static_cast<RuntimeServicePayload*>(
                FactoryInstanceTokenProviderAccessV1::pointer(instance));
            return static_cast<RuntimeServiceContract*>(payload);
        }

        [[nodiscard]] OtherRuntimeServiceContract*
        accessOtherRuntimeService(FactoryInstanceViewV1 unusedInstance) noexcept {
            ++unselectedAccessorInvocationCount;
            (void)unusedInstance;
            return nullptr;
        }

        [[nodiscard]] RuntimeServiceContract*
        throwingRuntimeServiceAccessor(FactoryInstanceViewV1 unusedInstance) {
            (void)unusedInstance;
            return nullptr;
        }

        [[nodiscard]] OtherRuntimeServiceContract*
        wrongReturnRuntimeServiceAccessor(FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedInstance;
            return nullptr;
        }

        [[nodiscard]] RuntimeServiceContract*
        wrongArgumentRuntimeServiceAccessor(const FactoryInstanceViewV1& unusedInstance) noexcept {
            (void)unusedInstance;
            return nullptr;
        }

        constexpr StaticContributionPayloadAccessorV1<RuntimeServiceContract> kNullAccessor{};

        template <typename Contract, auto Accessor>
        concept CanBindStaticContributionV2 =
            requires { bindStaticContributionV2<Contract, Accessor>("com.asharia.test.runtime"); };

        static_assert(StaticContributionContractV1<RuntimeServiceContract>);
        static_assert(StaticContributionContractV1<const RuntimeServiceContract>);
        static_assert(StaticContributionContractV1<volatile RuntimeServiceContract>);
        static_assert(std::is_abstract_v<RuntimeServiceContract>);
        static_assert(std::is_base_of_v<LeadingPayloadInterface, RuntimeServicePayload>);
        static_assert(std::is_base_of_v<RuntimeServiceContract, RuntimeServicePayload>);
        static_assert(std::same_as<StaticContributionPayloadAccessorV1<RuntimeServiceContract>,
                                   RuntimeServiceContract* (*)(FactoryInstanceViewV1) noexcept>);
        static_assert(
            StaticContributionPayloadAccessorForV1<RuntimeServiceContract, &accessRuntimeService>);
        static_assert(!StaticContributionPayloadAccessorForV1<const RuntimeServiceContract,
                                                              &accessRuntimeService>);
        static_assert(!StaticContributionPayloadAccessorForV1<volatile RuntimeServiceContract,
                                                              &accessRuntimeService>);
        static_assert(!StaticContributionPayloadAccessorForV1<RuntimeServiceContract,
                                                              &throwingRuntimeServiceAccessor>);
        static_assert(!StaticContributionPayloadAccessorForV1<RuntimeServiceContract,
                                                              &wrongReturnRuntimeServiceAccessor>);
        static_assert(!StaticContributionPayloadAccessorForV1<
                      RuntimeServiceContract, &wrongArgumentRuntimeServiceAccessor>);
        static_assert(
            !StaticContributionPayloadAccessorForV1<RuntimeServiceContract, kNullAccessor>);
        static_assert(CanBindStaticContributionV2<RuntimeServiceContract, &accessRuntimeService>);
        static_assert(
            !CanBindStaticContributionV2<RuntimeServiceContract, &throwingRuntimeServiceAccessor>);
        static_assert(!CanBindStaticContributionV2<RuntimeServiceContract,
                                                   &wrongReturnRuntimeServiceAccessor>);
        static_assert(!CanBindStaticContributionV2<RuntimeServiceContract,
                                                   &wrongArgumentRuntimeServiceAccessor>);
        static_assert(
            !CanBindStaticContributionV2<const RuntimeServiceContract, &accessRuntimeService>);
        static_assert(!CanBindStaticContributionV2<RuntimeServiceContract, kNullAccessor>);

        constexpr auto kSelectedBinding =
            bindStaticContributionV2<RuntimeServiceContract, &accessRuntimeService>(
                "com.asharia.test.runtime");
        constexpr auto kUnselectedBinding =
            bindStaticContributionV2<OtherRuntimeServiceContract, &accessOtherRuntimeService>(
                "com.asharia.test.other-runtime");

        void provideRuntimeService(StaticFactoryRegistrar& registrar) noexcept {
            constexpr std::array bindings{kUnselectedBinding, kSelectedBinding};
            registrar.registerFactory("runtime-service", abortingCallbacks(), bindings);
        }

        [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
        collectRuntimeServiceTable() noexcept {
            constexpr StaticFactoryRegistrationCapacityV2 kCapacity{
                .providerCount = 1,
                .factoryCount = 1,
                .contributionCount = 1,
                .textBytes = 512,
                .diagnosticFactoryIdBytes = 128,
                .diagnosticContributionIdBytes = 128,
            };
            auto recorderResult = createStaticFactoryRegistrationRecorder(kCapacity);
            if (!recorderResult) {
                return std::unexpected(std::move(recorderResult.error()));
            }

            auto recorder = std::move(*recorderResult);
            recorder.beginComposition({
                .generationId = kGenerationId,
                .hostActivationBlueprintSha256 = kBlueprintSha256,
                .capacity = kCapacity,
            });
            const std::array expectedContributions{StaticContributionExpectationV1{
                .contributionId = "com.asharia.test.runtime",
                .contributionKind = RuntimeServiceContract::kind,
            }};
            const std::array expectedFactories{StaticFactoryExpectationV1{
                .factoryId = "runtime-service",
                .contributions = expectedContributions,
            }};
            recorder.invokeProvider(
                {
                    .packageId = "com.asharia.test.accessor",
                    .packageVersion = "1.0.0",
                    .moduleId = "runtime",
                    .entryPoint = "asharia::host_runtime::tests::provideRuntimeService",
                    .expectedFactories = expectedFactories,
                },
                &provideRuntimeService);
            recorder.endComposition();
            return std::move(recorder).finish();
        }

        [[nodiscard]] bool registrationOwnsAlignedAccessorWithoutInvokingIt() noexcept {
            selectedAccessorInvocationCount = 0;
            unselectedAccessorInvocationCount = 0;
            auto tableResult = collectRuntimeServiceTable();
            if (!tableResult || selectedAccessorInvocationCount != 0 ||
                unselectedAccessorInvocationCount != 0) {
                return false;
            }

            const auto& registrations = tableResult->registrationSnapshot().registrations;
            const auto runtimeBindings =
                StaticFactoryCallbackTablePrivateAccessV1::contributionRuntimeBindings(
                    *tableResult);
            return registrations.size() == 1 && registrations[0].contributions.size() == 1 &&
                   registrations[0].contributions[0].contributionId == "com.asharia.test.runtime" &&
                   runtimeBindings.size() == 1 && runtimeBindings[0].registrationIndex == 0 &&
                   runtimeBindings[0].contributionIndex == 0 &&
                   runtimeBindings[0].typeKey != nullptr &&
                   runtimeBindings[0].payloadAccessor != nullptr;
        }

        [[nodiscard]] bool erasedAccessorPreservesInterfaceBaseAdjustment() noexcept {
            selectedAccessorInvocationCount = 0;
            unselectedAccessorInvocationCount = 0;
            auto tableResult = collectRuntimeServiceTable();
            if (!tableResult) {
                return false;
            }
            const auto runtimeBindings =
                StaticFactoryCallbackTablePrivateAccessV1::contributionRuntimeBindings(
                    *tableResult);
            if (runtimeBindings.size() != 1 || runtimeBindings[0].payloadAccessor == nullptr ||
                selectedAccessorInvocationCount != 0 || unselectedAccessorInvocationCount != 0) {
                return false;
            }

            RuntimeServicePayload payload;
            FactoryInstanceTokenV1 token =
                FactoryInstanceTokenProviderAccessV1::fromPointer(&payload);
            // This synthetic PRIVATE thunk call is isolated from every registration
            // and eligibility path, which must keep accessors dormant.
            void* const observedPayload = runtimeBindings[0].payloadAccessor(token.view());
            void* const expectedPayload =
                static_cast<void*>(static_cast<RuntimeServiceContract*>(&payload));
            void* const completePayload = static_cast<void*>(&payload);
            static_cast<void>(FactoryInstanceTokenProviderAccessV1::consume(std::move(token)));

            return observedPayload == expectedPayload && observedPayload != completePayload &&
                   selectedAccessorInvocationCount == 1 && unselectedAccessorInvocationCount == 0 &&
                   static_cast<RuntimeServiceContract*>(&payload)->serviceValue() == 29;
        }

    } // namespace

    bool runStaticContributionAccessorTests() noexcept {
        return registrationOwnsAlignedAccessorWithoutInvokingIt() &&
               erasedAccessorPreservesInterfaceBaseAdjustment();
    }

} // namespace asharia::host_runtime::tests
