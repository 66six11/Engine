#pragma once

#include <span>
#include <string>
#include <string_view>

#include "asharia/host_runtime/process_application.hpp"

namespace asharia::project_bootstrap::detail {

    class ProjectBootstrapApplicationStateV1;

    [[nodiscard]] host_runtime::ProcessApplicationRunResultV1
    runProjectBootstrapApplicationV1(ProjectBootstrapApplicationStateV1& state,
                                     std::span<const std::string_view> arguments) noexcept;

    class ProjectBootstrapApplicationStateV1 final {
    public:
        ProjectBootstrapApplicationStateV1() noexcept;

        ProjectBootstrapApplicationStateV1(const ProjectBootstrapApplicationStateV1&) = delete;
        ProjectBootstrapApplicationStateV1&
        operator=(const ProjectBootstrapApplicationStateV1&) = delete;
        ProjectBootstrapApplicationStateV1(ProjectBootstrapApplicationStateV1&&) = delete;
        ProjectBootstrapApplicationStateV1&
        operator=(ProjectBootstrapApplicationStateV1&&) = delete;

        [[nodiscard]] host_runtime::ProcessApplicationV1& application() noexcept {
            return application_;
        }

        void clearDiagnosticMessage() noexcept {
            diagnosticMessage_.clear();
        }

        [[nodiscard]] std::string_view retainDiagnosticMessage(std::string_view message) noexcept;

    private:
        std::string diagnosticMessage_;
        host_runtime::ProcessApplicationV1 application_;
    };

} // namespace asharia::project_bootstrap::detail
