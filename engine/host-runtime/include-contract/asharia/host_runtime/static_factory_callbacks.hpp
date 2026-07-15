#pragma once

#include <cstdint>

namespace asharia::host_runtime {

    class FactoryCreateContextV1;
    class FactoryActivateContextV1;
    class FactoryQuiesceContextV1;
    class FactoryDeactivateContextV1;
    class FactoryInstanceTokenProviderAccessV1;
    class FactoryInstanceViewV1;

    class FactoryInstanceTokenV1 final {
    public:
        FactoryInstanceTokenV1() noexcept = default;
        ~FactoryInstanceTokenV1() noexcept;

        FactoryInstanceTokenV1(FactoryInstanceTokenV1&& other) noexcept : opaque_(other.opaque_) {
            other.opaque_ = nullptr;
        }

        FactoryInstanceTokenV1& operator=(FactoryInstanceTokenV1&&) = delete;
        FactoryInstanceTokenV1(const FactoryInstanceTokenV1&) = delete;
        FactoryInstanceTokenV1& operator=(const FactoryInstanceTokenV1&) = delete;

        [[nodiscard]] bool isValid() const noexcept {
            return opaque_ != nullptr;
        }

        [[nodiscard]] FactoryInstanceViewV1 view() const noexcept;

    private:
        explicit FactoryInstanceTokenV1(void* opaque) noexcept : opaque_(opaque) {}

        void* opaque_{};

        friend class FactoryInstanceTokenProviderAccessV1;
    };

    class FactoryInstanceViewV1 final {
    public:
        FactoryInstanceViewV1(const FactoryInstanceViewV1&) noexcept = default;
        FactoryInstanceViewV1& operator=(const FactoryInstanceViewV1&) noexcept = default;

        [[nodiscard]] bool isValid() const noexcept {
            return opaque_ != nullptr;
        }

    private:
        explicit FactoryInstanceViewV1(void* opaque) noexcept : opaque_(opaque) {}

        void* opaque_{};

        friend class FactoryInstanceTokenV1;
        friend class FactoryInstanceTokenProviderAccessV1;
    };

    inline FactoryInstanceViewV1 FactoryInstanceTokenV1::view() const noexcept {
        return FactoryInstanceViewV1{opaque_};
    }

    enum class FactoryCallbackStatusV1 : std::uint8_t {
        Failed = 0,
        Succeeded = 1,
    };

    class FactoryCallbackResultV1 final {
    public:
        [[nodiscard]] static FactoryCallbackResultV1 succeeded() noexcept {
            return FactoryCallbackResultV1{FactoryCallbackStatusV1::Succeeded, 0};
        }

        [[nodiscard]] static FactoryCallbackResultV1 failed(std::uint32_t localCode) noexcept {
            return FactoryCallbackResultV1{FactoryCallbackStatusV1::Failed, localCode};
        }

        [[nodiscard]] bool isSucceeded() const noexcept {
            return status_ == FactoryCallbackStatusV1::Succeeded;
        }

        [[nodiscard]] FactoryCallbackStatusV1 status() const noexcept {
            return status_;
        }

        [[nodiscard]] std::uint32_t localCode() const noexcept {
            return localCode_;
        }

    private:
        FactoryCallbackResultV1(FactoryCallbackStatusV1 status, std::uint32_t localCode) noexcept
            : status_(status), localCode_(localCode) {}

        FactoryCallbackStatusV1 status_;
        std::uint32_t localCode_;
    };

    class FactoryCreateResultV1 final {
    public:
        [[nodiscard]] static FactoryCreateResultV1
        succeeded(FactoryInstanceTokenV1 instance) noexcept {
            if (!instance.isValid()) {
                return failed(0);
            }
            return FactoryCreateResultV1{
                FactoryCallbackResultV1::succeeded(),
                static_cast<FactoryInstanceTokenV1&&>(instance),
            };
        }

        [[nodiscard]] static FactoryCreateResultV1 failed(std::uint32_t localCode) noexcept {
            return FactoryCreateResultV1{
                FactoryCallbackResultV1::failed(localCode),
                FactoryInstanceTokenV1{},
            };
        }

        [[nodiscard]] const FactoryCallbackResultV1& result() const noexcept {
            return result_;
        }

        [[nodiscard]] FactoryInstanceViewV1 instanceView() const noexcept {
            return instance_.view();
        }

        [[nodiscard]] FactoryInstanceTokenV1 takeInstance() && noexcept {
            return static_cast<FactoryInstanceTokenV1&&>(instance_);
        }

    private:
        FactoryCreateResultV1(FactoryCallbackResultV1 result,
                              FactoryInstanceTokenV1 instance) noexcept
            : result_(result), instance_(static_cast<FactoryInstanceTokenV1&&>(instance)) {}

        FactoryCallbackResultV1 result_;
        FactoryInstanceTokenV1 instance_;
    };

    using FactoryCreateCallbackV1 = FactoryCreateResultV1 (*)(FactoryCreateContextV1&) noexcept;
    using FactoryActivateCallbackV1 = FactoryCallbackResultV1 (*)(FactoryActivateContextV1&,
                                                                  FactoryInstanceViewV1) noexcept;
    using FactoryQuiesceCallbackV1 = FactoryCallbackResultV1 (*)(FactoryQuiesceContextV1&,
                                                                 FactoryInstanceViewV1) noexcept;
    using FactoryDeactivateCallbackV1 = FactoryCallbackResultV1 (*)(FactoryDeactivateContextV1&,
                                                                    FactoryInstanceViewV1) noexcept;
    using FactoryDestroyCallbackV1 = void (*)(FactoryInstanceTokenV1 instance) noexcept;

    struct StaticFactoryCallbacksV1 final {
        FactoryCreateCallbackV1 create{};
        FactoryActivateCallbackV1 activate{};
        FactoryQuiesceCallbackV1 quiesce{};
        FactoryDeactivateCallbackV1 deactivate{};
        FactoryDestroyCallbackV1 destroy{};
    };

} // namespace asharia::host_runtime
