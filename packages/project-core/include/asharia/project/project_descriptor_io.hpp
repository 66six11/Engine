#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/project/project_descriptor.hpp"

namespace asharia::project {

    enum class AshariaProjectIoErrorCode : int {
        InvalidProject = 1,
        DescriptorIo = 2,
        AlreadyExists = 3,
        Busy = 4,
        IoFailure = 5,
    };

    struct OpenedAshariaProject {
        std::filesystem::path root;
        AshariaProjectDescriptor descriptor;

        [[nodiscard]] friend bool operator==(const OpenedAshariaProject&,
                                             const OpenedAshariaProject&) = default;
    };

    struct MinimalAshariaProjectCreate {
        std::filesystem::path parentDirectory;
        std::string projectName;
        ProjectId projectId{};
    };

    [[nodiscard]] Result<std::string>
    writeAshariaProjectDescriptorText(const AshariaProjectDescriptor& descriptor);
    [[nodiscard]] VoidResult
    writeAshariaProjectDescriptorFile(const std::filesystem::path& path,
                                      const AshariaProjectDescriptor& descriptor);

    [[nodiscard]] Result<AshariaProjectDescriptor>
    readAshariaProjectDescriptorText(std::string_view text);
    [[nodiscard]] Result<AshariaProjectDescriptor>
    readAshariaProjectDescriptorFile(const std::filesystem::path& path);

    [[nodiscard]] Result<std::filesystem::path>
    resolveContainedProjectPath(const std::filesystem::path& projectRoot,
                                const std::filesystem::path& projectRelativePath,
                                std::string_view context);

    [[nodiscard]] Result<OpenedAshariaProject>
    openAshariaProject(const std::filesystem::path& projectPath);
    [[nodiscard]] Result<OpenedAshariaProject>
    createMinimalAshariaProject(const MinimalAshariaProjectCreate& request);

} // namespace asharia::project
