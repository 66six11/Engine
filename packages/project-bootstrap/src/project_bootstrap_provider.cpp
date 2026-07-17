#include "asharia/project_bootstrap/project_bootstrap_provider.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "asharia/host_runtime/factory_lifecycle_contexts.hpp"
#include "asharia/host_runtime/process_application.hpp"
#include "asharia/host_runtime/static_factory_instance_token_provider_access.hpp"

#include "project_bootstrap_application.hpp"

namespace asharia::project_bootstrap {
    namespace {

        constexpr std::string_view kFactoryId{"project-bootstrap-application"};
        constexpr std::string_view kContributionId{
            "com.asharia.contribution.project-bootstrap-application"};
        constexpr std::uint32_t kAllocationFailureCode = 1;
        constexpr std::uint32_t kInvalidInstanceCode = 2;

        using ApplicationState = detail::ProjectBootstrapApplicationStateV1;

        [[nodiscard]] ApplicationState*
        applicationState(host_runtime::FactoryInstanceViewV1 instance) noexcept {
            return static_cast<ApplicationState*>(
                host_runtime::FactoryInstanceTokenProviderAccessV1::pointer(instance));
        }

        [[nodiscard]] host_runtime::ProcessApplicationV1*
        accessProcessApplication(host_runtime::FactoryInstanceViewV1 instance) noexcept {
            ApplicationState* const state = applicationState(instance);
            return state == nullptr ? nullptr : &state->application();
        }

        constexpr auto kProcessApplicationBinding =
            host_runtime::bindStaticContributionV2<host_runtime::ProcessApplicationV1,
                                                   &accessProcessApplication>(kContributionId);
        constexpr std::array kContributions{kProcessApplicationBinding};

        [[nodiscard]] host_runtime::FactoryCreateResultV1
        createApplication([[maybe_unused]] host_runtime::FactoryCreateContextV1& context) noexcept {
            try {
                auto state = std::make_unique<ApplicationState>();
                return host_runtime::FactoryCreateResultV1::succeeded(
                    host_runtime::FactoryInstanceTokenProviderAccessV1::fromPointer(
                        state.release()));
            } catch (...) {
                return host_runtime::FactoryCreateResultV1::failed(kAllocationFailureCode);
            }
        }

        [[nodiscard]] host_runtime::FactoryCallbackResultV1
        activateApplication([[maybe_unused]] host_runtime::FactoryActivateContextV1& context,
                            host_runtime::FactoryInstanceViewV1 instance) noexcept {
            return applicationState(instance) != nullptr
                       ? host_runtime::FactoryCallbackResultV1::succeeded()
                       : host_runtime::FactoryCallbackResultV1::failed(kInvalidInstanceCode);
        }

        [[nodiscard]] host_runtime::FactoryCallbackResultV1
        quiesceApplication([[maybe_unused]] host_runtime::FactoryQuiesceContextV1& context,
                           host_runtime::FactoryInstanceViewV1 instance) noexcept {
            return applicationState(instance) != nullptr
                       ? host_runtime::FactoryCallbackResultV1::succeeded()
                       : host_runtime::FactoryCallbackResultV1::failed(kInvalidInstanceCode);
        }

        [[nodiscard]] host_runtime::FactoryCallbackResultV1
        deactivateApplication([[maybe_unused]] host_runtime::FactoryDeactivateContextV1& context,
                              host_runtime::FactoryInstanceViewV1 instance) noexcept {
            return applicationState(instance) != nullptr
                       ? host_runtime::FactoryCallbackResultV1::succeeded()
                       : host_runtime::FactoryCallbackResultV1::failed(kInvalidInstanceCode);
        }

        void destroyApplication(host_runtime::FactoryInstanceTokenV1 instance) noexcept {
            std::unique_ptr<ApplicationState>{static_cast<ApplicationState*>(
                host_runtime::FactoryInstanceTokenProviderAccessV1::consume(std::move(instance)))};
        }

        constexpr host_runtime::StaticFactoryCallbacksV1 kCallbacks{
            .create = &createApplication,
            .activate = &activateApplication,
            .quiesce = &quiesceApplication,
            .deactivate = &deactivateApplication,
            .destroy = &destroyApplication,
        };

    } // namespace

    void
    provideProjectBootstrapFactories(host_runtime::StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory(kFactoryId, kCallbacks, kContributions);
    }

} // namespace asharia::project_bootstrap
