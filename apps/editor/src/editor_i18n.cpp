#include "editor_i18n.hpp"

#include <map>
#include <ranges>
#include <utility>
#include <vector>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/core/error.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] asharia::Error editorI18nError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        [[nodiscard]] std::vector<EditorI18nTextEntry>& mutableCatalog() {
            static std::vector<EditorI18nTextEntry> catalog;
            return catalog;
        }

        [[nodiscard]] const EditorI18nTextEntry* findEntry(std::string_view key) {
            const std::vector<EditorI18nTextEntry>& catalog = mutableCatalog();
            const auto found = std::ranges::find_if(
                catalog, [key](const EditorI18nTextEntry& entry) { return entry.key == key; });
            return found == catalog.end() ? nullptr : &(*found);
        }

        [[nodiscard]] asharia::VoidResult
        readLocaleTexts(const std::filesystem::path& directory, EditorLocale locale,
                        std::map<std::string, EditorI18nTextEntry>& entriesByKey) {
            const std::string localeName{editorLocaleName(locale)};
            const std::filesystem::path path = directory / (localeName + ".json");
            auto archive = asharia::archive::readJsonArchiveFile(path);
            if (!archive) {
                return std::unexpected{editorI18nError("Failed to read editor i18n catalog '" +
                                                       path.string() +
                                                       "': " + archive.error().message)};
            }
            if (archive->kind != asharia::archive::ArchiveValueKind::Object) {
                return std::unexpected{editorI18nError(
                    "Editor i18n catalog root must be an object: " + path.string())};
            }

            const asharia::archive::ArchiveValue* localeValue = archive->findMemberValue("locale");
            if (localeValue == nullptr ||
                localeValue->kind != asharia::archive::ArchiveValueKind::String ||
                localeValue->stringValue != localeName) {
                return std::unexpected{editorI18nError(
                    "Editor i18n catalog has an invalid locale field: " + path.string())};
            }

            const asharia::archive::ArchiveValue* textsValue = archive->findMemberValue("texts");
            if (textsValue == nullptr ||
                textsValue->kind != asharia::archive::ArchiveValueKind::Object) {
                return std::unexpected{editorI18nError(
                    "Editor i18n catalog must contain a texts object: " + path.string())};
            }

            for (const asharia::archive::ArchiveMember& member : textsValue->objectValue) {
                if (member.key.empty()) {
                    return std::unexpected{editorI18nError(
                        "Editor i18n catalog contains an empty key: " + path.string())};
                }
                if (member.value.kind != asharia::archive::ArchiveValueKind::String) {
                    return std::unexpected{
                        editorI18nError("Editor i18n text value must be a string for key '" +
                                        member.key + "': " + path.string())};
                }

                EditorI18nTextEntry& entry = entriesByKey[member.key];
                entry.key = member.key;
                if (locale == EditorLocale::EnUs) {
                    entry.enUs = member.value.stringValue;
                } else {
                    entry.zhHans = member.value.stringValue;
                }
            }

            return {};
        }

    } // namespace

    std::string_view editorLocaleName(EditorLocale locale) {
        switch (locale) {
        case EditorLocale::ZhHans:
            return "zh-Hans";
        case EditorLocale::EnUs:
        default:
            return "en-US";
        }
    }

    std::optional<EditorLocale> editorLocaleFromName(std::string_view name) {
        if (name == "en" || name == "en-US" || name == "en_US") {
            return EditorLocale::EnUs;
        }
        if (name == "zh" || name == "zh-Hans" || name == "zh-CN" || name == "zh_CN") {
            return EditorLocale::ZhHans;
        }
        return std::nullopt;
    }

    asharia::VoidResult loadEditorI18nCatalog(const std::filesystem::path& directory) {
        std::map<std::string, EditorI18nTextEntry> entriesByKey;
        if (auto loaded = readLocaleTexts(directory, EditorLocale::EnUs, entriesByKey); !loaded) {
            return std::unexpected{std::move(loaded.error())};
        }
        if (auto loaded = readLocaleTexts(directory, EditorLocale::ZhHans, entriesByKey); !loaded) {
            return std::unexpected{std::move(loaded.error())};
        }

        std::vector<EditorI18nTextEntry> loadedCatalog;
        loadedCatalog.reserve(entriesByKey.size());
        for (auto& [key, entry] : entriesByKey) {
            static_cast<void>(key);
            if (entry.enUs.empty()) {
                return std::unexpected{editorI18nError(
                    "Editor i18n catalog is missing en-US text for key '" + entry.key + "'.")};
            }
            loadedCatalog.push_back(std::move(entry));
        }

        mutableCatalog() = std::move(loadedCatalog);
        return {};
    }

    std::span<const EditorI18nTextEntry> editorI18nCatalog() {
        return mutableCatalog();
    }

    EditorI18n::EditorI18n(EditorLocale locale) : locale_(locale) {}

    void EditorI18n::setLocale(EditorLocale locale) {
        locale_ = locale;
    }

    EditorLocale EditorI18n::locale() const {
        return locale_;
    }

    std::string_view EditorI18n::text(std::string_view key) const {
        return text(EditorI18nTextQuery{.key = key, .fallback = {}});
    }

    std::string_view EditorI18n::text(const EditorI18nTextQuery& query) const {
        if (query.key.empty()) {
            return query.fallback;
        }

        const EditorI18nTextEntry* entry = findEntry(query.key);
        if (entry == nullptr) {
            return query.fallback.empty() ? query.key : query.fallback;
        }

        if (locale_ == EditorLocale::ZhHans && !entry->zhHans.empty()) {
            return entry->zhHans;
        }
        if (!entry->enUs.empty()) {
            return entry->enUs;
        }
        return query.fallback.empty() ? query.key : query.fallback;
    }

    std::string EditorI18n::label(const EditorI18nLabelDesc& desc) const {
        const std::string_view visible =
            text(EditorI18nTextQuery{.key = desc.key, .fallback = desc.fallback});
        std::string label;
        label.reserve(visible.size() + desc.stableId.size() + 3U);
        label += visible;
        label += "###";
        label += desc.stableId;
        return label;
    }

} // namespace asharia::editor
