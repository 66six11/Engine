#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"
#include "asharia/project/project_native_api.h"

namespace {

    constexpr std::string_view kProjectName = "NativeSmokeProject";
    constexpr std::string_view kProjectId = "51e86383-8a06-4c41-9267-ab10b0b67eb9";
    constexpr std::size_t kResponseBufferBytes = std::size_t{128U} * 1024U;

    struct SmokeWorkspace {
        std::filesystem::path root;

        SmokeWorkspace() = default;
        explicit SmokeWorkspace(std::filesystem::path value) : root(std::move(value)) {}
        SmokeWorkspace(const SmokeWorkspace&) = delete;
        SmokeWorkspace& operator=(const SmokeWorkspace&) = delete;
        SmokeWorkspace(SmokeWorkspace&&) = delete;
        SmokeWorkspace& operator=(SmokeWorkspace&&) = delete;

        ~SmokeWorkspace() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    [[nodiscard]] std::optional<SmokeWorkspace> makeWorkspace() {
        const auto temp = std::filesystem::temp_directory_path();
        for (std::uint64_t attempt = 0U; attempt < 32U; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = temp / ("asharia-project-native-smoke-" +
                                            std::to_string(stamp) + "-" +
                                            std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                return std::optional<SmokeWorkspace>{std::in_place, candidate};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
        const std::u8string text = path.u8string();
        return std::string{text.begin(), text.end()};
    }

    [[nodiscard]] AshariaProjectNativeStringView nativeString(std::string_view text) {
        return AshariaProjectNativeStringView{
            .data = text.data(),
            .byteLength = text.size(),
        };
    }

    [[nodiscard]] std::string_view resultText(std::span<const char> buffer,
                                              AshariaProjectNativeTextSpan span) {
        if (span.byteOffset > buffer.size() || span.byteLength > buffer.size() - span.byteOffset) {
            return {};
        }
        const auto text = buffer.subspan(static_cast<std::size_t>(span.byteOffset),
                                         static_cast<std::size_t>(span.byteLength));
        return std::string_view{text.data(), text.size()};
    }

    [[nodiscard]] bool runSmoke() {
        auto workspace = makeWorkspace();
        if (!workspace) {
            return false;
        }

        const std::string parent = pathText(workspace->root);
        const AshariaProjectNativeCreateRequest create{
            .header =
                AshariaProjectNativeAbiHeader{
                    .abiVersion = ASHARIA_PROJECT_NATIVE_ABI_VERSION,
                    .structSize = sizeof(AshariaProjectNativeCreateRequest),
                },
            .parentDirectoryUtf8 = nativeString(parent),
            .projectNameUtf8 = nativeString(kProjectName),
            .projectIdUtf8 = nativeString(kProjectId),
        };
        std::array<char, kResponseBufferBytes> response{};
        AshariaProjectNativeResult result{};
        if (asharia_project_create_minimal(&create, response.data(), response.size(), &result,
                                           sizeof(result)) !=
                AshariaProjectNativeStatus_Success ||
            result.status != AshariaProjectNativeStatus_Success ||
            resultText(response, result.projectNameUtf8) != kProjectName ||
            resultText(response, result.projectIdUtf8) != kProjectId) {
            return false;
        }

        const auto projectRoot = workspace->root / std::string{kProjectName};
        const auto descriptorPath =
            projectRoot / std::string{asharia::project::kDefaultAshariaProjectFileName};
        auto descriptor = asharia::project::readAshariaProjectDescriptorFile(descriptorPath);
        if (!descriptor || descriptor->projectName != kProjectName ||
            !std::filesystem::is_directory(projectRoot / "Assets") ||
            !std::filesystem::is_directory(projectRoot / ".asharia" / "cache" / "assets")) {
            return false;
        }

        result = {};
        if (asharia_project_create_minimal(&create, response.data(), response.size(), &result,
                                           sizeof(result)) !=
                AshariaProjectNativeStatus_AlreadyExists ||
            result.status != AshariaProjectNativeStatus_AlreadyExists) {
            return false;
        }

        for (const std::filesystem::path& openPath : {projectRoot, descriptorPath}) {
            const std::string path = pathText(openPath);
            const AshariaProjectNativeOpenRequest open{
                .header =
                    AshariaProjectNativeAbiHeader{
                        .abiVersion = ASHARIA_PROJECT_NATIVE_ABI_VERSION,
                        .structSize = sizeof(AshariaProjectNativeOpenRequest),
                    },
                .projectPathUtf8 = nativeString(path),
            };
            result = {};
            if (asharia_project_open(&open, response.data(), response.size(), &result,
                                     sizeof(result)) != AshariaProjectNativeStatus_Success ||
                resultText(response, result.projectNameUtf8) != kProjectName) {
                return false;
            }

            std::array<char, 2> canary{'x', 'y'};
            result = {};
            if (asharia_project_open(&open, canary.data(), 1U, &result, sizeof(result)) !=
                    AshariaProjectNativeStatus_BufferTooSmall ||
                result.requiredByteLength <= 1U || canary[1] != 'y') {
                return false;
            }
        }

        AshariaProjectNativeOpenRequest unsupported{};
        unsupported.header.abiVersion = ASHARIA_PROJECT_NATIVE_ABI_VERSION + 1U;
        unsupported.header.structSize = sizeof(unsupported);
        unsupported.projectPathUtf8 = nativeString(parent);
        result = {};
        return asharia_project_open(&unsupported, response.data(), response.size(), &result,
                                    sizeof(result)) ==
                   AshariaProjectNativeStatus_UnsupportedAbi &&
               result.status == AshariaProjectNativeStatus_UnsupportedAbi;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        if (!runSmoke()) {
            std::cerr << "Asharia project native smoke failed.\n";
            return 1;
        }
        std::cout << "Asharia project native smoke passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asharia project native smoke threw: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Asharia project native smoke threw an unknown exception.\n";
        return 1;
    }
}
