#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/material_instance/mat_document.hpp"

namespace asharia::material_instance {

    [[nodiscard]] VoidResult validateMatDocument(const MatDocument& document);
    [[nodiscard]] Result<MatDocument> readMatText(std::string_view text);
    [[nodiscard]] Result<MatDocument> readMatFile(const std::filesystem::path& path);
    [[nodiscard]] Result<std::string> writeMatText(const MatDocument& document);
    [[nodiscard]] VoidResult writeMatFile(const std::filesystem::path& path,
                                          const MatDocument& document);

} // namespace asharia::material_instance
