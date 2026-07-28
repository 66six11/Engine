#include "project_bootstrap_application.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "asharia/project_bootstrap/project_bootstrap_reader.hpp"

namespace asharia::project_bootstrap::detail {
    namespace {

        constexpr std::string_view kProjectRootArgument{"--asharia-project-root"};
        constexpr std::string_view kInvalidArgumentsCode{"project-bootstrap.invalid-arguments"};
        constexpr std::string_view kProjectReadFailedCode{"project-bootstrap.project-read-failed"};
        constexpr std::string_view kSummaryRenderFailedCode{
            "project-bootstrap.summary-render-failed"};
        constexpr std::string_view kStdoutModeFailedCode{"project-bootstrap.stdout-mode-failed"};
        constexpr std::string_view kStdoutWriteFailedCode{"project-bootstrap.stdout-write-failed"};
        constexpr std::string_view kInternalFailureCode{"project-bootstrap.internal-failure"};

        constexpr int kInvalidArgumentsExitCode = 64;
        constexpr int kProjectReadFailedExitCode = 65;
        constexpr int kInternalFailureExitCode = 70;
        constexpr int kOutputFailureExitCode = 74;

        [[nodiscard]] host_runtime::ProcessApplicationRunResultV1 succeeded() noexcept {
            return {
                .status = host_runtime::ProcessApplicationRunStatusV1::Succeeded,
                .exitCode = 0,
                .diagnosticCode = {},
                .diagnosticMessage = {},
            };
        }

        [[nodiscard]] host_runtime::ProcessApplicationRunResultV1
        failed(ProjectBootstrapApplicationStateV1& state, int exitCode,
               std::string_view diagnosticCode, std::string_view message) noexcept {
            return {
                .status = host_runtime::ProcessApplicationRunStatusV1::Failed,
                .exitCode = exitCode,
                .diagnosticCode = diagnosticCode,
                .diagnosticMessage = state.retainDiagnosticMessage(message),
            };
        }

        [[nodiscard]] bool setStdoutBinaryMode() noexcept {
#if defined(_WIN32)
            return _setmode(_fileno(stdout), _O_BINARY) != -1;
#else
            return true;
#endif
        }

    } // namespace

    ProjectBootstrapApplicationStateV1::ProjectBootstrapApplicationStateV1() noexcept
        : application_(
              host_runtime::bindProcessApplicationV1<ProjectBootstrapApplicationStateV1,
                                                     &runProjectBootstrapApplicationV1>(*this)) {}

    std::string_view
    ProjectBootstrapApplicationStateV1::retainDiagnosticMessage(std::string_view message) noexcept {
        try {
            diagnosticMessage_ = message;
            return diagnosticMessage_;
        } catch (...) {
            diagnosticMessage_.clear();
            return "Project Bootstrap could not retain diagnostic text.";
        }
    }

    host_runtime::ProcessApplicationRunResultV1
    runProjectBootstrapApplicationV1(ProjectBootstrapApplicationStateV1& state,
                                     std::span<const std::string_view> arguments) noexcept {
        state.clearDiagnosticMessage();
        if (arguments.size() != 2 || arguments[0] != kProjectRootArgument || arguments[1].empty()) {
            return failed(state, kInvalidArgumentsExitCode, kInvalidArgumentsCode,
                          "Expected --asharia-project-root followed by one project directory.");
        }

        try {
            auto summary =
                readProjectBootstrapSummaryV1(std::filesystem::path{std::string{arguments[1]}});
            if (!summary) {
                return failed(state, kProjectReadFailedExitCode, kProjectReadFailedCode,
                              summary.error().message);
            }

            auto rendered = renderProjectBootstrapSummaryJsonV1(*summary);
            if (!rendered) {
                return failed(state, kInternalFailureExitCode, kSummaryRenderFailedCode,
                              rendered.error().message);
            }

            if (!setStdoutBinaryMode()) {
                return failed(state, kOutputFailureExitCode, kStdoutModeFailedCode,
                              "Failed to switch stdout to deterministic binary mode.");
            }
            if (std::fwrite(rendered->data(), 1, rendered->size(), stdout) != rendered->size() ||
                std::fflush(stdout) != 0) {
                return failed(state, kOutputFailureExitCode, kStdoutWriteFailedCode,
                              "Failed to write the Project Bootstrap summary to stdout.");
            }
            return succeeded();
        } catch (const std::exception& exception) {
            return failed(state, kInternalFailureExitCode, kInternalFailureCode, exception.what());
        } catch (...) {
            return failed(state, kInternalFailureExitCode, kInternalFailureCode,
                          "Project Bootstrap caught an unknown failure.");
        }
    }

} // namespace asharia::project_bootstrap::detail
