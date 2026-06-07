#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::editor {

    struct EditorIconId {
        std::string value;
    };

    struct EditorIconTint {
        float red{0.78F};
        float green{0.80F};
        float blue{0.84F};
        float alpha{1.0F};
    };

    struct EditorIconDescriptor {
        EditorIconId id;
        EditorIconTint tint;
        std::string tooltipKey;
        std::string tooltipFallback;
    };

    enum class EditorAssetIconDiagnosticState {
        None,
        Warning,
        Missing,
        Invalid,
    };

    struct EditorAssetIconQuery {
        bool folder{false};
        std::string assetType;
        std::string importerId;
        std::string extension;
        EditorAssetIconDiagnosticState diagnostic{EditorAssetIconDiagnosticState::None};
        std::string sourcePath;
        std::string displayName;
        std::string guidText;
    };

    using EditorAssetIconResolver =
        std::function<std::optional<EditorIconDescriptor>(const EditorAssetIconQuery&)>;

    class EditorAssetIconRegistry {
    public:
        [[nodiscard]] asharia::VoidResult registerResolver(std::string id,
                                                           EditorAssetIconResolver resolver);
        [[nodiscard]] asharia::VoidResult registerOrReplaceResolver(
            std::string id, EditorAssetIconResolver resolver);
        [[nodiscard]] bool unregisterResolver(std::string_view id);
        [[nodiscard]] EditorIconDescriptor resolveAssetIcon(
            const EditorAssetIconQuery& query) const;
        [[nodiscard]] std::size_t resolverCount() const noexcept;

    private:
        struct ResolverEntry {
            std::string id;
            EditorAssetIconResolver resolver;
        };

        std::vector<ResolverEntry> resolvers_;
    };

    [[nodiscard]] EditorIconTint editorIconTint(float red, float green, float blue,
                                                float alpha = 1.0F) noexcept;
    [[nodiscard]] EditorIconDescriptor makeLucideEditorIconDescriptor(
        std::string_view lucideName, EditorIconTint tint, std::string_view tooltipKey,
        std::string_view tooltipFallback);
    [[nodiscard]] std::string normalizeEditorAssetIconExtension(std::string_view extension);
    [[nodiscard]] std::string normalizeEditorAssetIconToken(std::string_view value);

    void drawEditorIconGlyph(const EditorIconDescriptor& descriptor, float size);
    [[nodiscard]] bool drawEditorIconButton(const EditorIconDescriptor& descriptor,
                                            std::string_view stableId, float size);

    [[nodiscard]] bool validateEditorAssetIconSmoke();

} // namespace asharia::editor
