#include "native_bridge/project_native_smoke.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/core/file_io.hpp"
#include "asharia/core/log.hpp"
#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"

#include "native_bridge/project_native_api.hpp"

namespace asharia::editor {
    namespace {

        constexpr std::string_view kProjectName = "Minimal Project";
        constexpr std::string_view kProjectId = "51e86383-8a06-4c41-9267-ab10b0b67eb9";

        struct SmokeWorkspace {
            std::filesystem::path root;

            SmokeWorkspace() = default;
            explicit SmokeWorkspace(std::filesystem::path rootPath) : root(std::move(rootPath)) {}

            SmokeWorkspace(const SmokeWorkspace&) = delete;
            SmokeWorkspace& operator=(const SmokeWorkspace&) = delete;
            SmokeWorkspace(SmokeWorkspace&&) = delete;
            SmokeWorkspace& operator=(SmokeWorkspace&&) = delete;

            ~SmokeWorkspace() {
                if (root.empty()) {
                    return;
                }
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        };

        [[nodiscard]] std::optional<SmokeWorkspace> makeSmokeWorkspace() {
            std::error_code tempError;
            const std::filesystem::path temp = std::filesystem::temp_directory_path(tempError);
            if (tempError || temp.empty()) {
                return std::nullopt;
            }

            for (std::uint64_t attempt = 0U; attempt < 32U; ++attempt) {
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                const std::filesystem::path candidate =
                    temp / ("asharia-editor-project-native-smoke-" + std::to_string(stamp) + "-" +
                            std::to_string(attempt));
                std::error_code createError;
                if (std::filesystem::create_directory(candidate, createError)) {
                    return std::optional<SmokeWorkspace>{std::in_place, candidate};
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string pathText(const std::filesystem::path& path) {
            const std::u8string text = path.u8string();
            return std::string{text.begin(), text.end()};
        }

        [[nodiscard]] EditorProjectNativeStringView stringView(std::string_view text) {
            return EditorProjectNativeStringView{
                .data = text.data(),
                .byteLength = text.size(),
            };
        }

        [[nodiscard]] EditorProjectNativeOpenRequest openRequest(std::string_view root) {
            return EditorProjectNativeOpenRequest{
                .header =
                    EditorProjectNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize =
                            static_cast<std::uint32_t>(sizeof(EditorProjectNativeOpenRequest)),
                    },
                .projectRootUtf8 = stringView(root),
            };
        }

        [[nodiscard]] EditorProjectNativeCreateRequest createRequest(std::string_view root) {
            return EditorProjectNativeCreateRequest{
                .header =
                    EditorProjectNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize =
                            static_cast<std::uint32_t>(sizeof(EditorProjectNativeCreateRequest)),
                    },
                .projectRootUtf8 = stringView(root),
                .projectNameUtf8 = stringView(kProjectName),
                .projectIdUtf8 = stringView(kProjectId),
            };
        }

        [[nodiscard]] std::string_view resultText(const void* data, std::uint64_t byteLength) {
            if (data == nullptr || byteLength == 0U) {
                return {};
            }
            return std::string_view{static_cast<const char*>(data),
                                    static_cast<std::size_t>(byteLength)};
        }

        void logResultMessage(const EditorProjectNativeResult& result) {
            const std::string_view message =
                resultText(result.messageUtf8, result.messageByteLength);
            if (!message.empty()) {
                logError(message);
            }
        }

        [[nodiscard]] bool hasExpectedProject(const EditorProjectNativeResult& result) {
            return result.status == EditorProjectNativeStatus_Success &&
                   result.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
                   result.header.structSize == sizeof(EditorProjectNativeResult) &&
                   resultText(result.projectNameUtf8, result.projectNameByteLength) ==
                       kProjectName &&
                   resultText(result.projectIdUtf8, result.projectIdByteLength) == kProjectId &&
                   !resultText(result.projectRootUtf8, result.projectRootByteLength).empty();
        }

        [[nodiscard]] bool rejectsInvalidBoundary(std::string_view missingRoot) {
            EditorProjectNativeResult result{};
            if (editor_project_open(nullptr, &result) !=
                    EditorProjectNativeStatus_InvalidArgument ||
                result.status != EditorProjectNativeStatus_InvalidArgument) {
                editor_project_release_result(result);
                logError("Project native bridge smoke did not reject a null open request.");
                return false;
            }
            editor_project_release_result(result);

            EditorProjectNativeOpenRequest undersized = openRequest(missingRoot);
            undersized.header.structSize = sizeof(EditorProjectNativeAbiHeader);
            result = {};
            if (editor_project_open(&undersized, &result) !=
                    EditorProjectNativeStatus_UnsupportedAbi ||
                result.status != EditorProjectNativeStatus_UnsupportedAbi) {
                editor_project_release_result(result);
                logError("Project native bridge smoke did not reject an undersized open request.");
                return false;
            }
            editor_project_release_result(result);

            EditorProjectNativeOpenRequest missing = openRequest(missingRoot);
            result = {};
            if (editor_project_open(&missing, &result) !=
                    EditorProjectNativeStatus_InvalidProject ||
                result.status != EditorProjectNativeStatus_InvalidProject) {
                logResultMessage(result);
                editor_project_release_result(result);
                logError("Project native bridge smoke accepted a missing project.");
                return false;
            }
            editor_project_release_result(result);
            return true;
        }

        [[nodiscard]] bool createsAndReopensProject(const std::filesystem::path& projectRoot) {
            const std::string rootText = pathText(projectRoot);
            EditorProjectNativeCreateRequest create = createRequest(rootText);
            EditorProjectNativeResult result{};
            const std::uint32_t createStatus = editor_project_create_minimal(&create, &result);
            if (createStatus != EditorProjectNativeStatus_Success || !hasExpectedProject(result)) {
                logResultMessage(result);
                editor_project_release_result(result);
                logError("Project native bridge smoke could not create a minimal project.");
                return false;
            }
            editor_project_release_result(result);

            const std::filesystem::path descriptorPath =
                projectRoot / std::string{asharia::project::kDefaultAshariaProjectFileName};
            auto descriptor = asharia::project::readAshariaProjectDescriptorFile(descriptorPath);
            if (!descriptor || descriptor->projectName != kProjectName ||
                asharia::project::formatProjectId(descriptor->projectId) != kProjectId ||
                !std::filesystem::is_directory(projectRoot / "Assets") ||
                !std::filesystem::is_directory(projectRoot / ".asharia" / "cache" / "assets")) {
                logError("Project native bridge smoke created an incomplete project layout.");
                return false;
            }
            auto descriptorTextBefore = asharia::core::readFileText(
                descriptorPath, asharia::core::FileReadLimits{.maxBytes = 1024ULL * 1024ULL});
            if (!descriptorTextBefore) {
                logError("Project native bridge smoke could not read its created descriptor.");
                return false;
            }

            result = {};
            const std::uint32_t duplicateStatus = editor_project_create_minimal(&create, &result);
            if (duplicateStatus != EditorProjectNativeStatus_AlreadyExists ||
                result.status != EditorProjectNativeStatus_AlreadyExists) {
                logResultMessage(result);
                editor_project_release_result(result);
                logError("Project native bridge smoke overwrote or accepted a duplicate project.");
                return false;
            }
            editor_project_release_result(result);
            auto descriptorTextAfter = asharia::core::readFileText(
                descriptorPath, asharia::core::FileReadLimits{.maxBytes = 1024ULL * 1024ULL});
            if (!descriptorTextAfter || *descriptorTextAfter != *descriptorTextBefore) {
                logError("Project native bridge smoke observed a duplicate-create overwrite.");
                return false;
            }

            EditorProjectNativeOpenRequest open = openRequest(rootText);
            result = {};
            const std::uint32_t openStatus = editor_project_open(&open, &result);
            if (openStatus != EditorProjectNativeStatus_Success || !hasExpectedProject(result)) {
                logResultMessage(result);
                editor_project_release_result(result);
                logError("Project native bridge smoke could not reopen the minimal project.");
                return false;
            }
            editor_project_release_result(result);
            return true;
        }

        [[nodiscard]] bool rejectsCorruptProject(const std::filesystem::path& corruptRoot) {
            std::error_code directoryError;
            std::filesystem::create_directories(corruptRoot, directoryError);
            if (directoryError) {
                return false;
            }
            const std::filesystem::path descriptorPath =
                corruptRoot / std::string{asharia::project::kDefaultAshariaProjectFileName};
            auto written = asharia::core::writeFileTextAtomically(descriptorPath, "{");
            if (!written) {
                return false;
            }

            const std::string rootText = pathText(corruptRoot);
            EditorProjectNativeOpenRequest open = openRequest(rootText);
            EditorProjectNativeResult result{};
            const std::uint32_t status = editor_project_open(&open, &result);
            const bool rejected = status == EditorProjectNativeStatus_InvalidProject &&
                                  result.status == EditorProjectNativeStatus_InvalidProject;
            if (!rejected) {
                logResultMessage(result);
                logError("Project native bridge smoke accepted a corrupt project descriptor.");
            }
            editor_project_release_result(result);
            return rejected;
        }

    } // namespace

    bool runProjectNativeBridgeSmoke() {
        auto workspace = makeSmokeWorkspace();
        if (!workspace) {
            logError("Project native bridge smoke could not create its temporary workspace.");
            return false;
        }

        const std::filesystem::path projectRoot = workspace->root / "MinimalProject";
        const std::string missingRootText = pathText(workspace->root / "MissingProject");
        if (!rejectsInvalidBoundary(missingRootText) || !createsAndReopensProject(projectRoot) ||
            !rejectsCorruptProject(workspace->root / "CorruptProject")) {
            return false;
        }

        logInfo("Project native bridge smoke passed.");
        return true;
    }

} // namespace asharia::editor
