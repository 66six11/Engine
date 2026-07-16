#pragma once

#include <span>
#include <string_view>

#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime {

    class FactoryLifecycleContextAccessV1;

    struct ExactFactoryReferenceViewV1 final {
        std::string_view packageId;
        std::string_view packageVersion;
        std::string_view moduleId;
        std::string_view factoryId;

        [[nodiscard]] friend bool operator==(const ExactFactoryReferenceViewV1&,
                                             const ExactFactoryReferenceViewV1&) noexcept = default;
    };

    struct FactoryAttributionViewV1 final {
        std::string_view engineGenerationId;
        ExactFactoryReferenceViewV1 factory;
    };

    struct FactoryDependencyViewV1 final {
        ExactFactoryReferenceViewV1 factory;
        FactoryInstanceViewV1 instance;
    };

    class FactoryCreateContextV1 final {
    public:
        ~FactoryCreateContextV1() = default;
        FactoryCreateContextV1(const FactoryCreateContextV1&) = delete;
        FactoryCreateContextV1& operator=(const FactoryCreateContextV1&) = delete;
        FactoryCreateContextV1(FactoryCreateContextV1&&) = delete;
        FactoryCreateContextV1& operator=(FactoryCreateContextV1&&) = delete;

        // All returned views are valid only for the dynamic extent of this callback.
        [[nodiscard]] FactoryAttributionViewV1 attribution() const noexcept {
            return attribution_;
        }

        [[nodiscard]] std::span<const FactoryDependencyViewV1> dependencies() const noexcept {
            return dependencies_;
        }

    private:
        FactoryCreateContextV1(FactoryAttributionViewV1 attribution,
                               std::span<const FactoryDependencyViewV1> dependencies) noexcept
            : attribution_(attribution), dependencies_(dependencies) {}

        FactoryAttributionViewV1 attribution_;
        std::span<const FactoryDependencyViewV1> dependencies_;

        friend class FactoryLifecycleContextAccessV1;
    };

    class FactoryActivateContextV1 final {
    public:
        ~FactoryActivateContextV1() = default;
        FactoryActivateContextV1(const FactoryActivateContextV1&) = delete;
        FactoryActivateContextV1& operator=(const FactoryActivateContextV1&) = delete;
        FactoryActivateContextV1(FactoryActivateContextV1&&) = delete;
        FactoryActivateContextV1& operator=(FactoryActivateContextV1&&) = delete;

        // The attribution view is valid only for this callback invocation.
        [[nodiscard]] FactoryAttributionViewV1 attribution() const noexcept {
            return attribution_;
        }

    private:
        explicit FactoryActivateContextV1(FactoryAttributionViewV1 attribution) noexcept
            : attribution_(attribution) {}

        FactoryAttributionViewV1 attribution_;

        friend class FactoryLifecycleContextAccessV1;
    };

    class FactoryQuiesceContextV1 final {
    public:
        ~FactoryQuiesceContextV1() = default;
        FactoryQuiesceContextV1(const FactoryQuiesceContextV1&) = delete;
        FactoryQuiesceContextV1& operator=(const FactoryQuiesceContextV1&) = delete;
        FactoryQuiesceContextV1(FactoryQuiesceContextV1&&) = delete;
        FactoryQuiesceContextV1& operator=(FactoryQuiesceContextV1&&) = delete;

        // The attribution view is valid only for this callback invocation.
        [[nodiscard]] FactoryAttributionViewV1 attribution() const noexcept {
            return attribution_;
        }

    private:
        explicit FactoryQuiesceContextV1(FactoryAttributionViewV1 attribution) noexcept
            : attribution_(attribution) {}

        FactoryAttributionViewV1 attribution_;

        friend class FactoryLifecycleContextAccessV1;
    };

    class FactoryDeactivateContextV1 final {
    public:
        ~FactoryDeactivateContextV1() = default;
        FactoryDeactivateContextV1(const FactoryDeactivateContextV1&) = delete;
        FactoryDeactivateContextV1& operator=(const FactoryDeactivateContextV1&) = delete;
        FactoryDeactivateContextV1(FactoryDeactivateContextV1&&) = delete;
        FactoryDeactivateContextV1& operator=(FactoryDeactivateContextV1&&) = delete;

        // The attribution view is valid only for this callback invocation.
        [[nodiscard]] FactoryAttributionViewV1 attribution() const noexcept {
            return attribution_;
        }

    private:
        explicit FactoryDeactivateContextV1(FactoryAttributionViewV1 attribution) noexcept
            : attribution_(attribution) {}

        FactoryAttributionViewV1 attribution_;

        friend class FactoryLifecycleContextAccessV1;
    };

} // namespace asharia::host_runtime
