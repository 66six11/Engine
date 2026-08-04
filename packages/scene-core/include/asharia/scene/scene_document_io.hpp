#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/scene/scene_document.hpp"

namespace asharia::scene {

    [[nodiscard]] Result<std::string> writeSceneDocumentText(const SceneDocumentData& data);
    [[nodiscard]] VoidResult writeSceneDocumentFile(const std::filesystem::path& path,
                                                    const SceneDocumentData& data);
    [[nodiscard]] Result<SceneDocumentData> readSceneDocumentText(std::string_view text);
    [[nodiscard]] Result<SceneDocumentData>
    readSceneDocumentFile(const std::filesystem::path& path);

} // namespace asharia::scene
