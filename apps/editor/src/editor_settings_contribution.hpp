#pragma once

#include <span>
#include <string_view>

namespace asharia::editor {

    class EditorI18n;
    class EditorSettingsController;

    struct EditorSettingsContributionDrawContext {
        EditorSettingsController* settings{};
        const EditorI18n* i18n{};
    };

    using EditorSettingsContributionDrawFn =
        void (*)(EditorSettingsContributionDrawContext& context);

    struct EditorSettingsCategoryContribution {
        std::string_view id;
        std::string_view labelKey;
        std::string_view stableId;
        std::string_view fallback;
        EditorSettingsContributionDrawFn draw{};
    };

    [[nodiscard]] std::span<const EditorSettingsCategoryContribution>
    builtInEditorSettingsCategoryContributions();
    [[nodiscard]] const EditorSettingsCategoryContribution*
    findBuiltInEditorSettingsCategoryContribution(std::string_view categoryId);

} // namespace asharia::editor
