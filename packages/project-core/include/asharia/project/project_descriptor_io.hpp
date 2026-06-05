#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/project/project_descriptor.hpp"

namespace asharia::project {

    [[nodiscard]] Result<std::string>
    writeAshariaProjectDescriptorText(const AshariaProjectDescriptor& descriptor);
    [[nodiscard]] VoidResult
    writeAshariaProjectDescriptorFile(const std::filesystem::path& path,
                                      const AshariaProjectDescriptor& descriptor);

    [[nodiscard]] Result<AshariaProjectDescriptor>
    readAshariaProjectDescriptorText(std::string_view text);
    [[nodiscard]] Result<AshariaProjectDescriptor>
    readAshariaProjectDescriptorFile(const std::filesystem::path& path);

} // namespace asharia::project
