#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

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

        // Writable storage prevents release-linker identical-COMDAT folding from
        // collapsing distinct contract keys. Host Runtime never mutates the byte.
        template <StaticContributionContractV1 Contract>
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        inline unsigned char kStaticContributionTypeKeyV1{};

    } // namespace detail

    class StaticContributionBindingV1 final {
    public:
        StaticContributionBindingV1(const StaticContributionBindingV1&) noexcept = default;
        StaticContributionBindingV1&
        operator=(const StaticContributionBindingV1&) noexcept = default;

    private:
        constexpr StaticContributionBindingV1(std::string_view contributionId,
                                              std::string_view contributionKind,
                                              StaticContributionCardinalityV1 cardinality,
                                              const void* typeKey) noexcept
            : contributionId_(contributionId), contributionKind_(contributionKind),
              cardinality_(cardinality), typeKey_(typeKey) {}

        std::string_view contributionId_;
        std::string_view contributionKind_;
        StaticContributionCardinalityV1 cardinality_{};
        const void* typeKey_{};

        template <StaticContributionContractV1 Contract>
        friend constexpr StaticContributionBindingV1
        bindStaticContributionV1(std::string_view contributionId) noexcept;

        friend class StaticFactoryRegistrationState;
    };

    template <StaticContributionContractV1 Contract>
    [[nodiscard]] constexpr StaticContributionBindingV1
    bindStaticContributionV1(std::string_view contributionId) noexcept {
        constexpr std::string_view kKind{Contract::kind};
        constexpr StaticContributionCardinalityV1 kCardinality{Contract::cardinality};
        static_assert(!kKind.empty(), "a static contribution contract kind cannot be empty");
        static_assert(kCardinality == StaticContributionCardinalityV1::Single ||
                          kCardinality == StaticContributionCardinalityV1::Multiple,
                      "a static contribution contract must declare a valid cardinality");

        return StaticContributionBindingV1{
            contributionId,
            kKind,
            kCardinality,
            static_cast<const void*>(&detail::kStaticContributionTypeKeyV1<Contract>),
        };
    }

} // namespace asharia::host_runtime
