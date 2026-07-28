#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

namespace asharia::host_runtime {

    struct CurrentImageExactFactoryReferenceProviderV2 final {
        std::string_view packageId;
        std::string_view packageVersion;
        std::string_view moduleId;
        std::string_view factoryId;
    };

    struct CurrentImageProcessFactoryProviderV2 final {
        CurrentImageExactFactoryReferenceProviderV2 reference;
        std::span<const CurrentImageExactFactoryReferenceProviderV2> requirements;
    };

    struct CurrentImageActivationDescriptorProviderV2 final {
        std::string_view engineGenerationId;
        std::string_view hostKind;
        std::string_view targetPlatform;
        std::string_view effectiveSessionIntegrity;
        std::string_view staticCompositionGenerationId;
        std::string_view hostActivationBlueprintSha256;
        std::uint32_t templateRendererRevision{};
        std::uint32_t compositionRendererRevision{};
        std::string_view providerApi;
        std::uint32_t registrationSnapshotSchemaVersion{};
        std::string_view lifecycleModel;
        std::span<const CurrentImageProcessFactoryProviderV2> processFactories;
        StaticFactoryRegistrationCapacityFunctionV2 registrationCapacity{};
        StaticFactoryRecordingFunctionV2 recordProviders{};
    };

    namespace detail {

        [[nodiscard]] ActivationEligibilityResultV2<CurrentImageActivationDescriptorV2>
        issueCurrentImageActivationDescriptorV2(
            const CurrentImageActivationDescriptorProviderV2& provider) noexcept;

    } // namespace detail

    class CurrentImageActivationDescriptorProviderAccessV2 final {
    public:
        template <const CurrentImageActivationDescriptorProviderV2& Provider>
        [[nodiscard]] static ActivationEligibilityResultV2<CurrentImageActivationDescriptorV2>
        seal() noexcept {
            return detail::issueCurrentImageActivationDescriptorV2(Provider);
        }
    };

} // namespace asharia::host_runtime
