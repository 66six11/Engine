#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "asharia/archive/json_archive.hpp"

namespace {

    [[nodiscard]] std::filesystem::path createUniqueTestDirectory() {
        constexpr std::uint32_t kMaximumCreateAttempts = 128U;
        const auto temporaryRoot = std::filesystem::temp_directory_path();

        for (std::uint32_t attempt = 0U; attempt < kMaximumCreateAttempts; ++attempt) {
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate =
                temporaryRoot / ("asharia-archive-smoke-tests-" + std::to_string(timestamp) + "." +
                                 std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                return candidate;
            }
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error{"Could not create archive test directory",
                                                        candidate, error};
            }
        }

        throw std::runtime_error{"Could not allocate a unique archive test directory."};
    }

    class ScopedTestDirectory final {
    public:
        ScopedTestDirectory() : path_(createUniqueTestDirectory()) {}

        ~ScopedTestDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScopedTestDirectory(const ScopedTestDirectory&) = delete;
        ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;
        ScopedTestDirectory(ScopedTestDirectory&&) = delete;
        ScopedTestDirectory& operator=(ScopedTestDirectory&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

} // namespace

// Unexpected test exceptions are reported as a failing process by the test runner.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    std::string escaped = "quote \" slash \\ newline \n carriage \r tab \t";
    escaped.push_back('\b');
    escaped.push_back('\f');

    std::string validUtf8 = "utf8 ";
    validUtf8.push_back(static_cast<char>(0xC3));
    validUtf8.push_back(static_cast<char>(0xA9));

    asharia::archive::ArchiveValue archive = asharia::archive::ArchiveValue::object({
        asharia::archive::ArchiveMember{
            .key = "escaped",
            .value = asharia::archive::ArchiveValue::string(escaped),
        },
        asharia::archive::ArchiveMember{
            .key = "validUtf8",
            .value = asharia::archive::ArchiveValue::string(validUtf8),
        },
        asharia::archive::ArchiveMember{
            .key = "array",
            .value = asharia::archive::ArchiveValue::array({
                asharia::archive::ArchiveValue::integer(7),
                asharia::archive::ArchiveValue::floating(0.25),
                asharia::archive::ArchiveValue::boolean(true),
            }),
        },
    });

    auto firstText = asharia::archive::writeJsonArchive(archive);
    auto secondText = asharia::archive::writeJsonArchive(archive);
    if (!firstText || !secondText || *firstText != *secondText) {
        std::cerr << "Archive JSON output was not deterministic.\n";
        return EXIT_FAILURE;
    }

    auto parsed = asharia::archive::readJsonArchive(*firstText);
    if (!parsed) {
        std::cerr << parsed.error().message << '\n';
        return EXIT_FAILURE;
    }
    const asharia::archive::ArchiveValue* parsedEscaped = parsed->findMemberValue("escaped");
    const asharia::archive::ArchiveValue* parsedUtf8 = parsed->findMemberValue("validUtf8");
    if (parsedEscaped == nullptr ||
        parsedEscaped->kind != asharia::archive::ArchiveValueKind::String ||
        parsedEscaped->stringValue != escaped || parsedUtf8 == nullptr ||
        parsedUtf8->kind != asharia::archive::ArchiveValueKind::String ||
        parsedUtf8->stringValue != validUtf8) {
        std::cerr << "Archive JSON did not round-trip strings.\n";
        return EXIT_FAILURE;
    }

    const ScopedTestDirectory testDirectory;
    const std::filesystem::path archivePath = testDirectory.path() / "archive.json";
    {
        std::ofstream stale{archivePath, std::ios::binary | std::ios::trunc};
        stale << "stale archive content that must be replaced";
    }
    if (auto written = asharia::archive::writeJsonArchiveFile(archivePath, archive); !written) {
        std::cerr << written.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto exactLimitParsed = asharia::archive::readJsonArchiveFile(
        archivePath, {.maxBytes = static_cast<std::uint64_t>(firstText->size())});
    if (!exactLimitParsed) {
        std::cerr << "Archive JSON rejected a file at the exact byte limit.\n";
        return EXIT_FAILURE;
    }
    auto oversized = asharia::archive::readJsonArchiveFile(
        archivePath, {.maxBytes = static_cast<std::uint64_t>(firstText->size() - 1U)});
    if (oversized ||
        !contains(oversized.error().message,
                  "observedBytes=" + std::to_string(firstText->size())) ||
        !contains(oversized.error().message,
                  "maxBytes=" + std::to_string(firstText->size() - 1U))) {
        std::cerr << "Archive JSON accepted a file one byte above the byte limit.\n";
        return EXIT_FAILURE;
    }
    auto fileParsed = asharia::archive::readJsonArchiveFile(archivePath);
    for (const auto& entry : std::filesystem::directory_iterator{testDirectory.path()}) {
        const std::string filename = entry.path().filename().string();
        if (filename.starts_with(archivePath.filename().string() + ".tmp.")) {
            std::cerr << "Archive JSON left an atomic-write temporary file behind.\n";
            return EXIT_FAILURE;
        }
    }
    if (!fileParsed) {
        std::cerr << fileParsed.error().message << '\n';
        return EXIT_FAILURE;
    }

    auto duplicate = asharia::archive::readJsonArchive(R"({"field":1,"field":2})");
    if (duplicate || !contains(duplicate.error().message, "duplicate key")) {
        std::cerr << "Archive JSON accepted duplicate object keys.\n";
        return EXIT_FAILURE;
    }

    auto malformed = asharia::archive::readJsonArchive("{");
    if (malformed || !contains(malformed.error().message, "byte")) {
        std::cerr << "Archive JSON parse error did not include byte context.\n";
        return EXIT_FAILURE;
    }

    std::string invalidUtf8;
    invalidUtf8.push_back(static_cast<char>(0xFF));
    auto invalidWrite =
        asharia::archive::writeJsonArchive(asharia::archive::ArchiveValue::string(invalidUtf8));
    if (invalidWrite) {
        std::cerr << "Archive JSON accepted invalid UTF-8 output.\n";
        return EXIT_FAILURE;
    }

    auto duplicateObjectWrite =
        asharia::archive::writeJsonArchive(asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{
                .key = "field",
                .value = asharia::archive::ArchiveValue::integer(1),
            },
            asharia::archive::ArchiveMember{
                .key = "field",
                .value = asharia::archive::ArchiveValue::integer(2),
            },
        }));
    if (duplicateObjectWrite || !contains(duplicateObjectWrite.error().message, "duplicate key")) {
        std::cerr << "Archive JSON accepted duplicate ArchiveValue keys.\n";
        return EXIT_FAILURE;
    }

    auto nonFiniteWrite = asharia::archive::writeJsonArchive(
        asharia::archive::ArchiveValue::floating(std::numeric_limits<double>::infinity()));
    if (nonFiniteWrite || !contains(nonFiniteWrite.error().message, "non-finite")) {
        std::cerr << "Archive JSON accepted non-finite float output.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Archive JSON bytes: " << firstText->size() << '\n';
    return EXIT_SUCCESS;
}
