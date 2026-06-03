#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

#include "editor_id.hpp"
#include "editor_tool.hpp"

namespace asharia::editor {

    struct EditorExtensionManifest {
        EditorId id;
        std::string displayName;
        std::vector<EditorToolDesc> tools;
    };

    using EditorExtensionVisitor = std::function<void(const EditorExtensionManifest&)>;

    class EditorExtensionRegistry {
    public:
        [[nodiscard]] asharia::VoidResult
        registerOrReplaceExtension(EditorExtensionManifest manifest);
        [[nodiscard]] const EditorExtensionManifest*
        findExtension(std::string_view extensionId) const;
        [[nodiscard]] std::size_t extensionCount() const;
        [[nodiscard]] std::size_t toolContributionCount() const;
        void visitExtensions(const EditorExtensionVisitor& visitor) const;

    private:
        [[nodiscard]] EditorExtensionManifest* findExtensionEntry(std::string_view extensionId);
        [[nodiscard]] const EditorExtensionManifest*
        findExtensionEntry(std::string_view extensionId) const;

        std::vector<EditorExtensionManifest> extensions_;
    };

    [[nodiscard]] asharia::VoidResult
    registerEditorExtensionTools(const EditorExtensionRegistry& extensionRegistry,
                                 EditorToolRegistry& toolRegistry);

} // namespace asharia::editor
