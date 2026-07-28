#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime {

    class StaticFactoryRegistrationState;

    enum class StaticContributionCardinalityV1 : std::uint8_t {
        Single,
        Multiple,
    };

    template <typename Contract>
    concept StaticContributionContractV1 = requires {
        { Contract::kind } -> std::convertible_to<std::string_view>;
        { Contract::cardinality } -> std::convertible_to<StaticContributionCardinalityV1>;
        requires(!std::string_view{Contract::kind}.empty());
        requires(Contract::cardinality == StaticContributionCardinalityV1::Single ||
                 Contract::cardinality == StaticContributionCardinalityV1::Multiple);
    };

    namespace detail {

        using ErasedStaticContributionPayloadAccessorV1 =
            void* (*)(FactoryInstanceViewV1 instance) noexcept;

        // Writable storage prevents release-linker identical-COMDAT folding from
        // collapsing distinct contract keys. Host Runtime never mutates the byte.
        template <StaticContributionContractV1 Contract>
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        inline unsigned char kStaticContributionTypeKeyV1{};

        template <StaticContributionContractV1 Contract, auto Accessor>
            requires std::is_object_v<Contract> &&
                     std::same_as<Contract, std::remove_cv_t<Contract>> &&
                     std::same_as<decltype(Accessor),
                                  Contract* (*)(FactoryInstanceViewV1) noexcept> &&
                     (Accessor != nullptr)
        [[nodiscard]] void* invokeStaticContributionPayloadAccessorV1(
            FactoryInstanceViewV1 instance) noexcept {
            return static_cast<void*>(Accessor(instance));
        }

    } // namespace detail

    template <typename Contract>
    using StaticContributionPayloadAccessorV1 =
        Contract* (*)(FactoryInstanceViewV1 instance) noexcept;

    template <typename Contract, auto Accessor>
    concept StaticContributionPayloadAccessorForV1 =
        StaticContributionContractV1<Contract> && std::is_object_v<Contract> &&
        std::same_as<Contract, std::remove_cv_t<Contract>> &&
        std::same_as<decltype(Accessor), StaticContributionPayloadAccessorV1<Contract>> &&
        (Accessor != nullptr);

    class StaticContributionBindingV2 final {
    public:
        StaticContributionBindingV2(const StaticContributionBindingV2&) noexcept = default;
        StaticContributionBindingV2&
        operator=(const StaticContributionBindingV2&) noexcept = default;

    private:
        constexpr StaticContributionBindingV2(
            std::string_view contributionId, std::string_view contributionKind,
            StaticContributionCardinalityV1 cardinality, const void* typeKey,
            detail::ErasedStaticContributionPayloadAccessorV1 payloadAccessor) noexcept
            : contributionId_(contributionId), contributionKind_(contributionKind),
              cardinality_(cardinality), typeKey_(typeKey), payloadAccessor_(payloadAccessor) {}

        std::string_view contributionId_;
        std::string_view contributionKind_;
        StaticContributionCardinalityV1 cardinality_{};
        const void* typeKey_{};
        detail::ErasedStaticContributionPayloadAccessorV1 payloadAccessor_{};

        template <StaticContributionContractV1 Contract, auto Accessor>
            requires StaticContributionPayloadAccessorForV1<Contract, Accessor>
        friend constexpr StaticContributionBindingV2
        bindStaticContributionV2(std::string_view contributionId) noexcept;

        friend class StaticFactoryRegistrationState;
    };

    template <StaticContributionContractV1 Contract, auto Accessor>
        requires StaticContributionPayloadAccessorForV1<Contract, Accessor>
    [[nodiscard]] constexpr StaticContributionBindingV2
    bindStaticContributionV2(std::string_view contributionId) noexcept {
        constexpr std::string_view kKind{Contract::kind};
        constexpr StaticContributionCardinalityV1 kCardinality{Contract::cardinality};
        static_assert(!kKind.empty(), "a static contribution contract kind cannot be empty");
        static_assert(kCardinality == StaticContributionCardinalityV1::Single ||
                          kCardinality == StaticContributionCardinalityV1::Multiple,
                      "a static contribution contract must declare a valid cardinality");

        return StaticContributionBindingV2{
            contributionId,
            kKind,
            kCardinality,
            static_cast<const void*>(&detail::kStaticContributionTypeKeyV1<Contract>),
            &detail::invokeStaticContributionPayloadAccessorV1<Contract, Accessor>,
        };
    }

} // namespace asharia::host_runtime
