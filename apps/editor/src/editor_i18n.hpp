#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace asharia::editor {

    enum class EditorLocale {
        EnUs,
        ZhHans,
    };

    struct EditorI18nTextEntry {
        std::string_view key;
        std::string_view enUs;
        std::string_view zhHans;
    };

    struct EditorI18nTextQuery {
        std::string_view key;
        std::string_view fallback;
    };

    struct EditorI18nLabelDesc {
        std::string_view key;
        std::string_view stableId;
        std::string_view fallback;
    };

    [[nodiscard]] std::string_view editorLocaleName(EditorLocale locale);
    [[nodiscard]] std::optional<EditorLocale> editorLocaleFromName(std::string_view name);
    [[nodiscard]] std::span<const EditorI18nTextEntry> editorI18nCatalog();

    class EditorI18n {
    public:
        explicit EditorI18n(EditorLocale locale = EditorLocale::EnUs);

        void setLocale(EditorLocale locale);
        [[nodiscard]] EditorLocale locale() const;
        [[nodiscard]] std::string_view text(std::string_view key) const;
        [[nodiscard]] std::string_view text(const EditorI18nTextQuery& query) const;
        [[nodiscard]] std::string label(const EditorI18nLabelDesc& desc) const;

    private:
        EditorLocale locale_{EditorLocale::EnUs};
    };

} // namespace asharia::editor
