#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/host_runtime/static_contribution_contract.hpp"

namespace asharia::host_runtime {

    template <typename Contract>
    concept ProcessContributionContractV1 =
        StaticContributionContractV1<Contract> && std::is_object_v<Contract> &&
        std::same_as<Contract, std::remove_cv_t<Contract>>;

    enum class ProcessContributionLookupErrorCodeV1 : std::uint8_t {
        ExecutorMovedFrom,
        OperationInProgress,
        RegistryExpired,
        WrongControlThread,
        ProcessEpochStale,
        RegistryNotActive,
        RegistryRevoking,
        RegistryRevoked,
        ContractAbsent,
        ContractTypeMismatch,
        ContractCardinalityMismatch,
        OrdinalOutOfRange,
        PayloadUnavailable,
    };

    struct ProcessContributionLookupErrorV1 final {
        ProcessContributionLookupErrorCodeV1 code{
            ProcessContributionLookupErrorCodeV1::RegistryExpired};

        [[nodiscard]] friend bool
        operator==(const ProcessContributionLookupErrorV1&,
                   const ProcessContributionLookupErrorV1&) noexcept = default;
    };

    struct ProcessContributionRegistryGenerationStateV1;
    class ProcessContributionRegistryStateAccessV1;

    template <ProcessContributionContractV1 Contract> class ProcessContributionHandleV1;

    class ProcessContributionHandleCoreV1 final {
    public:
        ProcessContributionHandleCoreV1(const ProcessContributionHandleCoreV1&) noexcept = default;
        ProcessContributionHandleCoreV1&
        operator=(const ProcessContributionHandleCoreV1&) noexcept = default;
        ProcessContributionHandleCoreV1(ProcessContributionHandleCoreV1&&) noexcept = default;
        ProcessContributionHandleCoreV1&
        operator=(ProcessContributionHandleCoreV1&&) noexcept = default;
        ~ProcessContributionHandleCoreV1() = default;

    private:
        ProcessContributionHandleCoreV1(
            std::weak_ptr<const ProcessContributionRegistryGenerationStateV1> generation,
            std::size_t slotIndex) noexcept
            : generation_(std::move(generation)), slotIndex_(slotIndex) {}

        [[nodiscard]] std::expected<void*, ProcessContributionLookupErrorV1>
        tryBorrowErased(std::string_view contributionKind,
                        StaticContributionCardinalityV1 cardinality,
                        const void* typeKey) const noexcept;

        std::weak_ptr<const ProcessContributionRegistryGenerationStateV1> generation_;
        std::size_t slotIndex_{};

        friend class ProcessContributionRegistryViewV1;
        template <ProcessContributionContractV1 Contract> friend class ProcessContributionHandleV1;
    };

    template <ProcessContributionContractV1 Contract> class ProcessContributionHandleV1 final {
    public:
        ProcessContributionHandleV1(const ProcessContributionHandleV1&) noexcept = default;
        ProcessContributionHandleV1&
        operator=(const ProcessContributionHandleV1&) noexcept = default;
        ProcessContributionHandleV1(ProcessContributionHandleV1&&) noexcept = default;
        ProcessContributionHandleV1& operator=(ProcessContributionHandleV1&&) noexcept = default;
        ~ProcessContributionHandleV1() = default;

        // A successful borrow is valid only for synchronous control-thread use
        // before the next ProcessScope lifecycle operation. Do not cache the
        // underlying reference or pointer across stop.
        [[nodiscard]] std::expected<std::reference_wrapper<Contract>,
                                    ProcessContributionLookupErrorV1>
        tryBorrow() const noexcept {
            constexpr std::string_view kContributionKind{Contract::kind};
            constexpr StaticContributionCardinalityV1 kCardinality{Contract::cardinality};
            const auto payload = core_.tryBorrowErased(
                kContributionKind, kCardinality,
                static_cast<const void*>(&detail::kStaticContributionTypeKeyV1<Contract>));
            if (!payload) {
                return std::unexpected(payload.error());
            }
            return std::ref(*static_cast<Contract*>(*payload));
        }

    private:
        explicit ProcessContributionHandleV1(ProcessContributionHandleCoreV1 core) noexcept
            : core_(std::move(core)) {}

        ProcessContributionHandleCoreV1 core_;

        friend class ProcessContributionRegistryViewV1;
    };

    class ProcessContributionRegistryViewV1 final {
    public:
        ProcessContributionRegistryViewV1() noexcept = default;
        ProcessContributionRegistryViewV1(const ProcessContributionRegistryViewV1&) noexcept =
            default;
        ProcessContributionRegistryViewV1&
        operator=(const ProcessContributionRegistryViewV1&) noexcept = default;
        ProcessContributionRegistryViewV1(ProcessContributionRegistryViewV1&&) noexcept = default;
        ProcessContributionRegistryViewV1&
        operator=(ProcessContributionRegistryViewV1&&) noexcept = default;
        ~ProcessContributionRegistryViewV1() = default;

        template <ProcessContributionContractV1 Contract>
        [[nodiscard]] std::expected<std::size_t, ProcessContributionLookupErrorV1>
        size() const noexcept {
            constexpr std::string_view kContributionKind{Contract::kind};
            constexpr StaticContributionCardinalityV1 kCardinality{Contract::cardinality};
            return sizeErased(
                kContributionKind, kCardinality,
                static_cast<const void*>(&detail::kStaticContributionTypeKeyV1<Contract>));
        }

        template <ProcessContributionContractV1 Contract>
            requires(Contract::cardinality == StaticContributionCardinalityV1::Multiple)
        [[nodiscard]] std::expected<ProcessContributionHandleV1<Contract>,
                                    ProcessContributionLookupErrorV1>
        at(std::size_t ordinal) const noexcept {
            constexpr std::string_view kContributionKind{Contract::kind};
            constexpr StaticContributionCardinalityV1 kCardinality{Contract::cardinality};
            auto result = atErased(
                kContributionKind, kCardinality,
                static_cast<const void*>(&detail::kStaticContributionTypeKeyV1<Contract>), ordinal);
            if (!result) {
                return std::unexpected(result.error());
            }
            return ProcessContributionHandleV1<Contract>{std::move(*result)};
        }

        template <ProcessContributionContractV1 Contract>
            requires(Contract::cardinality == StaticContributionCardinalityV1::Single)
        [[nodiscard]] std::expected<ProcessContributionHandleV1<Contract>,
                                    ProcessContributionLookupErrorV1>
        single() const noexcept {
            constexpr std::string_view kContributionKind{Contract::kind};
            constexpr StaticContributionCardinalityV1 kCardinality{Contract::cardinality};
            auto result = singleErased(
                kContributionKind, kCardinality,
                static_cast<const void*>(&detail::kStaticContributionTypeKeyV1<Contract>));
            if (!result) {
                return std::unexpected(result.error());
            }
            return ProcessContributionHandleV1<Contract>{std::move(*result)};
        }

    private:
        ProcessContributionRegistryViewV1(
            std::weak_ptr<const ProcessContributionRegistryGenerationStateV1> generation) noexcept
            : generation_(std::move(generation)) {}

        [[nodiscard]] std::expected<std::size_t, ProcessContributionLookupErrorV1>
        sizeErased(std::string_view contributionKind, StaticContributionCardinalityV1 cardinality,
                   const void* typeKey) const noexcept;

        [[nodiscard]] std::expected<ProcessContributionHandleCoreV1,
                                    ProcessContributionLookupErrorV1>
        atErased(std::string_view contributionKind, StaticContributionCardinalityV1 cardinality,
                 const void* typeKey, std::size_t ordinal) const noexcept;

        [[nodiscard]] std::expected<ProcessContributionHandleCoreV1,
                                    ProcessContributionLookupErrorV1>
        singleErased(std::string_view contributionKind, StaticContributionCardinalityV1 cardinality,
                     const void* typeKey) const noexcept;

        std::weak_ptr<const ProcessContributionRegistryGenerationStateV1> generation_;

        friend class ProcessContributionRegistryStateAccessV1;
    };

} // namespace asharia::host_runtime
