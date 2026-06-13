#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/material_instance/amat_document.hpp"

namespace asharia::material_instance {

    [[nodiscard]] VoidResult validateAmatDocument(const AmatDocument& document);
    [[nodiscard]] Result<AmatDocument> readAmatText(std::string_view text);
    [[nodiscard]] Result<AmatDocument> readAmatFile(const std::filesystem::path& path);
    [[nodiscard]] Result<std::string> writeAmatText(const AmatDocument& document);
    [[nodiscard]] VoidResult writeAmatFile(const std::filesystem::path& path,
                                           const AmatDocument& document);

} // namespace asharia::material_instance
