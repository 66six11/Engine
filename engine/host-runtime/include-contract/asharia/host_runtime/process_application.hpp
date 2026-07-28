#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

#include "asharia/host_runtime/static_contribution_contract.hpp"

namespace asharia::host_runtime {

    enum class ProcessApplicationRunStatusV1 : std::uint8_t {
        Succeeded,
        Failed,
    };

    struct ProcessApplicationRunResultV1 final {
        ProcessApplicationRunStatusV1 status{ProcessApplicationRunStatusV1::Failed};
        int exitCode{1};
        // Diagnostic views remain valid until the next run or provider deactivation.
        std::string_view diagnosticCode;
        std::string_view diagnosticMessage;

        [[nodiscard]] bool succeeded() const noexcept {
            return status == ProcessApplicationRunStatusV1::Succeeded;
        }
    };

    template <typename Context>
    using ProcessApplicationRunFunctionV1 = ProcessApplicationRunResultV1 (*)(
        Context& context, std::span<const std::string_view> arguments) noexcept;

    class ProcessApplicationV1;

    template <typename Context, ProcessApplicationRunFunctionV1<Context> Run>
    [[nodiscard]] ProcessApplicationV1 bindProcessApplicationV1(Context& context) noexcept;

    class ProcessApplicationV1 final {
    public:
        static constexpr std::string_view kind{"com.asharia.host.process-application"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Single};

        ProcessApplicationV1(const ProcessApplicationV1&) = delete;
        ProcessApplicationV1& operator=(const ProcessApplicationV1&) = delete;
        ProcessApplicationV1(ProcessApplicationV1&&) = delete;
        ProcessApplicationV1& operator=(ProcessApplicationV1&&) = delete;

        [[nodiscard]] ProcessApplicationRunResultV1
        run(std::span<const std::string_view> arguments) noexcept {
            return run_(context_, arguments);
        }

    private:
        using ErasedRunFunctionV1 = ProcessApplicationRunResultV1 (*)(
            void* context, std::span<const std::string_view> arguments) noexcept;

        ProcessApplicationV1(void* context, ErasedRunFunctionV1 run) noexcept
            : context_(context), run_(run) {}

        template <typename Context, ProcessApplicationRunFunctionV1<Context> Run>
        [[nodiscard]] static ProcessApplicationRunResultV1
        invoke(void* context, std::span<const std::string_view> arguments) noexcept {
            return Run(*static_cast<Context*>(context), arguments);
        }

        void* context_{};
        ErasedRunFunctionV1 run_{};

        template <typename Context, ProcessApplicationRunFunctionV1<Context> Run>
        friend ProcessApplicationV1 bindProcessApplicationV1(Context& context) noexcept;
    };

    template <typename Context, ProcessApplicationRunFunctionV1<Context> Run>
    ProcessApplicationV1 bindProcessApplicationV1(Context& context) noexcept {
        static_assert(std::is_object_v<Context>, "a ProcessApplication context must be an object");
        static_assert(!std::is_const_v<Context>, "a ProcessApplication context must be mutable");
        static_assert(Run != nullptr, "a ProcessApplication run function cannot be null");

        return ProcessApplicationV1{
            static_cast<void*>(std::addressof(context)),
            &ProcessApplicationV1::invoke<Context, Run>,
        };
    }

} // namespace asharia::host_runtime
