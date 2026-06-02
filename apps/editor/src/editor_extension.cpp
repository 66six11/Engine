#include "editor_extension.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] bool hasDuplicateToolIds(const std::vector<EditorToolDesc>& tools) {
            for (auto tool = tools.begin(); tool != tools.end(); ++tool) {
                const auto duplicate = std::ranges::find_if(
                    std::next(tool), tools.end(), [&](const EditorToolDesc& other) {
                        return !tool->id.value.empty() && other.id.value == tool->id.value;
                    });
                if (duplicate != tools.end()) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    asharia::VoidResult
    EditorExtensionRegistry::registerOrReplaceExtension(EditorExtensionManifest manifest) {
        if (manifest.id.value.empty()) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor extension id must not be empty"}};
        }
        if (manifest.displayName.empty()) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::Core, 0, "Editor extension display name must not be empty"}};
        }
        if (std::ranges::any_of(manifest.tools,
                                [](const EditorToolDesc& tool) { return tool.id.value.empty(); })) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor extension tool id must not be empty"}};
        }
        if (hasDuplicateToolIds(manifest.tools)) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor extension manifest contains duplicate tool ids"}};
        }

        if (EditorExtensionManifest* existing = findExtensionEntry(manifest.id.value);
            existing != nullptr) {
            *existing = std::move(manifest);
            return {};
        }
        extensions_.push_back(std::move(manifest));
        return {};
    }

    const EditorExtensionManifest*
    EditorExtensionRegistry::findExtension(std::string_view extensionId) const {
        return findExtensionEntry(extensionId);
    }

    std::size_t EditorExtensionRegistry::extensionCount() const {
        return extensions_.size();
    }

    std::size_t EditorExtensionRegistry::toolContributionCount() const {
        std::size_t count = 0;
        for (const EditorExtensionManifest& extension : extensions_) {
            count += extension.tools.size();
        }
        return count;
    }

    void EditorExtensionRegistry::visitExtensions(const EditorExtensionVisitor& visitor) const {
        if (!visitor) {
            return;
        }
        for (const EditorExtensionManifest& extension : extensions_) {
            visitor(extension);
        }
    }

    EditorExtensionManifest*
    EditorExtensionRegistry::findExtensionEntry(std::string_view extensionId) {
        const auto found = std::ranges::find_if(
            extensions_, [extensionId](const EditorExtensionManifest& extension) {
                return extension.id.value == extensionId;
            });
        if (found == extensions_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorExtensionManifest*
    EditorExtensionRegistry::findExtensionEntry(std::string_view extensionId) const {
        const auto found = std::ranges::find_if(
            extensions_, [extensionId](const EditorExtensionManifest& extension) {
                return extension.id.value == extensionId;
            });
        if (found == extensions_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    asharia::VoidResult
    registerEditorExtensionTools(const EditorExtensionRegistry& extensionRegistry,
                                 EditorToolRegistry& toolRegistry) {
        EditorToolRegistry stagedRegistry;
        asharia::VoidResult result{};
        extensionRegistry.visitExtensions([&](const EditorExtensionManifest& extension) {
            if (!result) {
                return;
            }
            for (const EditorToolDesc& tool : extension.tools) {
                result = stagedRegistry.registerTool(tool);
                if (!result) {
                    return;
                }
            }
        });
        if (!result) {
            return result;
        }
        toolRegistry = std::move(stagedRegistry);
        return result;
    }

} // namespace asharia::editor
