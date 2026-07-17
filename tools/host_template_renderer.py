"""Render deterministic CMake and C++ bytes for one fixed Host Template."""

from __future__ import annotations


HOST_TEMPLATE_CMAKE_PATH = "asharia-host-template.cmake"
HOST_TEMPLATE_INTERNAL_HEADER_PATH = "src/host_entry.hpp"
HOST_TEMPLATE_MAIN_PATH = "src/main.cpp"
HOST_TEMPLATE_PROCESS_APPLICATION_PATH = "src/process_application_host.cpp"
HOST_TEMPLATE_REGISTRATION_PATH = "src/registration_verification.cpp"
HOST_TEMPLATE_SUBSYSTEM = "console"
HOST_TEMPLATE_RUNTIME_OUTPUT_DIRECTORY = "asharia-host/bin/$<CONFIG>"
HOST_TEMPLATE_FILE_DESCRIPTORS = (
    (HOST_TEMPLATE_CMAKE_PATH, "cmake-host-template", "text/x-cmake"),
    (
        HOST_TEMPLATE_INTERNAL_HEADER_PATH,
        "internal-host-entry-header",
        "text/x-c++hdr",
    ),
    (HOST_TEMPLATE_MAIN_PATH, "host-main", "text/x-c++src"),
    (
        HOST_TEMPLATE_PROCESS_APPLICATION_PATH,
        "process-application-host",
        "text/x-c++src",
    ),
    (
        HOST_TEMPLATE_REGISTRATION_PATH,
        "registration-verification",
        "text/x-c++src",
    ),
)

_UTF8_BOM = b"\xef\xbb\xbf"


def render_internal_header() -> bytes:
    """Render private entry declarations shared by the generated Host TUs."""

    text = r'''#pragma once

namespace asharia::generated::host_template {

int runRegistrationVerification() noexcept;
int runProcessApplicationHost(int argc, char* const* argv) noexcept;

} // namespace asharia::generated::host_template
'''
    return _UTF8_BOM + text.encode("utf-8")


def render_main() -> bytes:
    """Render the revision-3 mode dispatcher."""

    text = r'''#include <cstdio>
#include <string_view>

#include "host_entry.hpp"

namespace {

constexpr std::string_view kVerificationArgument{"--asharia-verify-static-registration"};

int fail(const char* code, int exitCode) noexcept {
  std::fputs(code, stderr);
  std::fputc('\n', stderr);
  return exitCode;
}

} // namespace

int main(int argc, char** argv) noexcept {
  if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
    return fail("host.invalid-arguments", 64);
  }

  bool verificationRequested = false;
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return fail("host.invalid-arguments", 64);
    }
    verificationRequested = verificationRequested ||
                            std::string_view{argv[index]} == kVerificationArgument;
  }

  if (verificationRequested) {
    if (argc != 2 || std::string_view{argv[1]} != kVerificationArgument) {
      return fail("host-verification.invalid-arguments", 64);
    }
    return asharia::generated::host_template::runRegistrationVerification();
  }

  return asharia::generated::host_template::runProcessApplicationHost(argc, argv);
}
'''
    return _UTF8_BOM + text.encode("utf-8")


def render_registration_verification() -> bytes:
    """Render the restricted registration-only verifier."""

    text = r'''#include <cstdio>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "asharia/generated/static_composition_root.hpp"
#include "asharia/host_runtime/static_factory_callback_table.hpp"
#include "asharia/host_runtime/static_factory_registration_snapshot_json.hpp"

#include "host_entry.hpp"

namespace {

int fail(const char* code, int exitCode) noexcept {
  std::fputs(code, stderr);
  std::fputc('\n', stderr);
  return exitCode;
}

} // namespace

int asharia::generated::host_template::runRegistrationVerification() noexcept {
  auto recorder = asharia::host_runtime::createStaticFactoryRegistrationRecorder(
      asharia::generated::staticFactoryRegistrationCapacity());
  if (!recorder) {
    return fail("host-verification.recorder-create-failed", 70);
  }

  asharia::generated::recordStaticFactoryProviders(*recorder);
  auto table = std::move(*recorder).finish();
  if (!table) {
    return fail("host-verification.registration-failed", 71);
  }

  const auto& snapshot = table->registrationSnapshot();
  auto rendered =
      asharia::host_runtime::renderStaticFactoryRegistrationSnapshotJson(snapshot);
  if (!rendered) {
    return fail("host-verification.snapshot-render-failed", 72);
  }

#if defined(_WIN32)
  // Canonical handoff bytes use LF even when the Windows CRT defaults to text mode.
  if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
    return fail("host-verification.stdout-mode-failed", 73);
  }
#endif

  if (std::fwrite(rendered->data(), 1, rendered->size(), stdout) != rendered->size() ||
      std::fflush(stdout) != 0) {
    return fail("host-verification.stdout-write-failed", 74);
  }
  return 0;
}
'''
    return _UTF8_BOM + text.encode("utf-8")


def render_process_application_host() -> bytes:
    """Render current-image admission and one synchronous ProcessApplication run."""

    text = r'''#include <cstdio>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/generated/static_composition_root.hpp"
#include "asharia/host_runtime/admitted_static_factory_recording.hpp"
#include "asharia/host_runtime/process_application.hpp"
#include "asharia/host_runtime/process_scope.hpp"

#include "host_entry.hpp"

namespace {

struct ApplicationInvocationResult final {
  bool invoked{};
  std::string_view hostDiagnosticCode;
  asharia::host_runtime::ProcessApplicationRunResultV1 applicationResult;
};

int fail(std::string_view code, std::string_view message, int exitCode) noexcept {
  if (!code.empty()) {
    (void)std::fwrite(code.data(), 1, code.size(), stderr);
  }
  if (!message.empty()) {
    constexpr std::string_view separator{": "};
    (void)std::fwrite(separator.data(), 1, separator.size(), stderr);
    (void)std::fwrite(message.data(), 1, message.size(), stderr);
  }
  std::fputc('\n', stderr);
  return exitCode;
}

int stopAfterFailure(asharia::host_runtime::ProcessScopeExecutorV2& executor,
                     int reportedExitCode) noexcept {
  auto stopped = executor.stop();
  if (!stopped) {
    return fail("host.process-scope.stop-failed", {}, 78);
  }
  if (!stopped->callbacksSucceeded()) {
    return fail("host.process-scope.cleanup-failed", {}, 79);
  }
  return reportedExitCode;
}

[[nodiscard]] ApplicationInvocationResult invokeProcessApplication(
    asharia::host_runtime::ProcessScopeExecutorV2& executor,
    std::span<const std::string_view> arguments) noexcept {
  auto registry = executor.contributions();
  if (!registry) {
    return {.hostDiagnosticCode = "host.process-registry-unavailable"};
  }
  auto applicationHandle =
      registry->single<asharia::host_runtime::ProcessApplicationV1>();
  if (!applicationHandle) {
    return {.hostDiagnosticCode = "host.process-application-unavailable"};
  }
  auto application = applicationHandle->tryBorrow();
  if (!application) {
    return {.hostDiagnosticCode = "host.process-application-borrow-failed"};
  }

  return {
      .invoked = true,
      .hostDiagnosticCode = {},
      .applicationResult = application->get().run(arguments),
  };
}

} // namespace

int asharia::generated::host_template::runProcessApplicationHost(
    int argc, char* const* argv) noexcept {
  std::vector<std::string_view> arguments;
  try {
    arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
      arguments.emplace_back(argv[index]);
    }
  } catch (...) {
    return fail("host.arguments-allocation-failed", {}, 70);
  }

  auto admission = asharia::generated::admitCurrentImagePreRegistration();
  if (!admission) {
    return fail("host.current-image-admission-failed", {}, 71);
  }
  auto pending = asharia::host_runtime::recordAdmittedStaticFactoryProviders(
      std::move(*admission));
  if (!pending) {
    return fail("host.provider-recording-failed", {}, 72);
  }
  auto admitted = asharia::host_runtime::admitStaticFactoryActivation(
      std::move(*pending));
  if (!admitted) {
    return fail("host.static-factory-admission-failed", {}, 73);
  }
  auto executor = asharia::host_runtime::prepareProcessScopeExecutorV2(
      std::move(*admitted));
  if (!executor) {
    return fail("host.process-scope-prepare-failed", {}, 74);
  }
  auto started = executor->start();
  if (!started) {
    return fail("host.process-scope-start-failed", {}, 75);
  }

  // The helper returns only after the synchronous contribution borrow is
  // released.
  const auto invocation = invokeProcessApplication(
      *executor, std::span<const std::string_view>{arguments});
  if (!invocation.invoked) {
    const int reportedExitCode = fail(invocation.hostDiagnosticCode, {}, 76);
    return stopAfterFailure(*executor, reportedExitCode);
  }

  const auto& result = invocation.applicationResult;
  if (!result.succeeded()) {
    const std::string_view code = result.diagnosticCode.empty()
                                      ? std::string_view{"host.process-application-failed"}
                                      : result.diagnosticCode;
    const int exitCode = result.exitCode == 0 ? 1 : result.exitCode;
    // The diagnostic message may borrow provider state. Report it before stop
    // destroys that state.
    const int reportedExitCode = fail(code, result.diagnosticMessage, exitCode);
    return stopAfterFailure(*executor, reportedExitCode);
  }

  const int applicationExitCode = result.exitCode;
  auto stopped = executor->stop();
  if (!stopped) {
    return fail("host.process-scope.stop-failed", {}, 78);
  }
  if (!stopped->callbacksSucceeded()) {
    return fail("host.process-scope.cleanup-failed", {}, 79);
  }
  return applicationExitCode;
}
'''
    return _UTF8_BOM + text.encode("utf-8")


def render_windows_development_cmake(
    target_name: str,
    template_generation_id: str,
    static_composition_generation_id: str,
) -> bytes:
    """Render the CMake adapter that owns the final executable target."""

    lines = [
        "# Generated by tools/host_executable_template.py. Do not edit.",
        "if(NOT WIN32)",
        '  message(FATAL_ERROR "windows-development-v1 requires Windows")',
        "endif()",
        "if(NOT DEFINED ASHARIA_STATIC_COMPOSITION_ROOT)",
        '  message(FATAL_ERROR "ASHARIA_STATIC_COMPOSITION_ROOT must be explicit")',
        "endif()",
        "if(NOT COMMAND asharia_configure_target)",
        '  message(FATAL_ERROR "asharia_configure_target must exist before the Host Template")',
        "endif()",
        f"if(TARGET {target_name})",
        f'  message(FATAL_ERROR "Host target \'{target_name}\' already exists")',
        "endif()",
        "get_filename_component(_asharia_static_composition_root",
        '    "${ASHARIA_STATIC_COMPOSITION_ROOT}" ABSOLUTE)',
        'include("${_asharia_static_composition_root}/asharia-static-composition.cmake")',
        f"add_executable({target_name}",
        '    "${CMAKE_CURRENT_LIST_DIR}/src/main.cpp"',
        '    "${CMAKE_CURRENT_LIST_DIR}/src/process_application_host.cpp"',
        '    "${CMAKE_CURRENT_LIST_DIR}/src/registration_verification.cpp")',
        f"asharia_configure_target({target_name})",
        f"set_target_properties({target_name} PROPERTIES",
        "    WIN32_EXECUTABLE FALSE",
        f'    OUTPUT_NAME "{target_name}"',
        '    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/asharia-host/bin/$<CONFIG>"',
        f'    ASHARIA_HOST_TEMPLATE_GENERATION_ID "{template_generation_id}"',
        ")",
        f"asharia_attach_static_composition({target_name})",
        f"get_target_property(_asharia_attached_generation {target_name}",
        "    ASHARIA_STATIC_COMPOSITION_GENERATION_ID)",
        f'if(NOT _asharia_attached_generation STREQUAL "{static_composition_generation_id}")',
        '  message(FATAL_ERROR "Host Template static-composition generation mismatch")',
        "endif()",
        "",
    ]
    return "\n".join(lines).encode("utf-8")
