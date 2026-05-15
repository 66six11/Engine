#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/core/log.hpp"
#include "asharia/core/version.hpp"
#include "asharia/profiling/frame_profiler.hpp"
#include "asharia/reflection/context_view.hpp"
#include "asharia/reflection/type_builder.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/basic_triangle_renderer.hpp"
#include "asharia/renderer_basic_vulkan/clear_frame.hpp"
#include "asharia/rendergraph/render_graph.hpp"
#include "asharia/rhi_vulkan/deferred_deletion_queue.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"
#include "asharia/serialization/migration.hpp"
#include "asharia/serialization/serializer.hpp"
#include "asharia/serialization/storage_attributes.hpp"
#include "asharia/serialization/text_archive.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

namespace {

    constexpr asharia::VulkanDebugLabelMode kSmokeDebugLabels =
        asharia::VulkanDebugLabelMode::Required;

    bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return arg != nullptr && arg == expected; });
    }

    std::optional<std::string_view> argValue(std::span<char*> args, std::string_view option) {
        for (std::size_t index = 1; index + 1 < args.size(); ++index) {
            if (args[index] != nullptr && args[index] == option && args[index + 1] != nullptr) {
                return std::string_view{args[index + 1]};
            }
        }
        return std::nullopt;
    }

    void printVersion() {
        std::cout << asharia::kEngineName << ' ' << asharia::kEngineVersion.major << '.'
                  << asharia::kEngineVersion.minor << '.' << asharia::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout << "Usage: asharia-sample-viewer [--help] [--version] [--smoke-window] "
                     "[--smoke-vulkan] [--smoke-frame] [--smoke-rendergraph] "
                     "[--smoke-transient] [--smoke-dynamic-rendering] [--smoke-resize] "
                     "[--smoke-triangle] [--smoke-depth-triangle] [--smoke-mesh] "
                     "[--smoke-mesh-3d] [--smoke-draw-list] "
                     "[--smoke-descriptor-layout] [--smoke-fullscreen-texture] "
                     "[--smoke-offscreen-viewport] [--smoke-deferred-deletion] "
                     "[--smoke-reflection-registry] [--smoke-reflection-transform] "
                     "[--smoke-reflection-attributes] [--smoke-serialization-roundtrip] "
                     "[--smoke-serialization-json-archive] [--smoke-serialization-migration] "
                     "[--bench-rendergraph --warmup N --frames N --output path]\n";
    }

    bool isRenderableExtent(asharia::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, asharia::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    std::string_view triangleSmokeTitle(bool useDepth, asharia::BasicMeshKind meshKind) {
        if (meshKind == asharia::BasicMeshKind::IndexedQuad) {
            return "Asharia Engine Mesh Smoke";
        }
        if (useDepth) {
            return "Asharia Engine Depth Triangle Smoke";
        }
        return "Asharia Engine Triangle Smoke";
    }

    std::string_view triangleSmokeOutOfDateMessage(bool useDepth, asharia::BasicMeshKind meshKind) {
        if (meshKind == asharia::BasicMeshKind::IndexedQuad) {
            return "Swapchain remained out of date during mesh smoke.";
        }
        if (useDepth) {
            return "Swapchain remained out of date during depth triangle smoke.";
        }
        return "Swapchain remained out of date during triangle smoke.";
    }

    std::string_view triangleSmokeRenderedPrefix(bool useDepth, asharia::BasicMeshKind meshKind) {
        if (meshKind == asharia::BasicMeshKind::IndexedQuad) {
            return "Rendered indexed mesh frames: ";
        }
        if (useDepth) {
            return "Rendered depth triangle frames: ";
        }
        return "Rendered triangle frames: ";
    }

    bool validatePipelineCacheStats(asharia::BasicPipelineCacheStats stats,
                                    std::string_view context) {
        if (stats.created != 1 || stats.reused < 2) {
            asharia::logError(std::string{context} +
                              " did not reuse its renderer pipeline after the first frame.");
            return false;
        }
        return true;
    }

    bool validateOffscreenViewportStats(asharia::BasicOffscreenViewportStats stats,
                                        std::string_view context) {
        if (stats.renderTargetsCreated != 2 || stats.renderTargetsReused < 2 ||
            stats.renderTargetsDeferredForDeletion != 1) {
            asharia::logError(std::string{context} +
                              " did not resize and reuse its offscreen viewport render target.");
            return false;
        }
        return true;
    }

    bool validateOffscreenViewportTarget(asharia::BasicOffscreenViewportTarget target,
                                         VkFormat expectedFormat, VkExtent2D expectedExtent,
                                         std::string_view context) {
        if (target.image == VK_NULL_HANDLE || target.imageView == VK_NULL_HANDLE ||
            target.format != expectedFormat || target.extent.width != expectedExtent.width ||
            target.extent.height != expectedExtent.height ||
            target.sampledLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            asharia::logError(std::string{context} +
                              " did not expose a sampled offscreen viewport target.");
            return false;
        }
        return true;
    }

    constexpr std::string_view kSmokeVec3TypeName = "com.asharia.smoke.Vec3";
    constexpr std::string_view kSmokeQuatTypeName = "com.asharia.smoke.Quat";
    constexpr std::string_view kSmokeTransformTypeName = "com.asharia.smoke.Transform";
    constexpr std::string_view kSmokePropertyComponentTypeName =
        "com.asharia.smoke.PropertyComponent";
    constexpr std::string_view kSmokeMigratedTransformTypeName =
        "com.asharia.smoke.MigratedTransform";
    constexpr std::string_view kSmokeDeferredOwnerTypeName = "com.asharia.smoke.DeferredOwner";
    constexpr std::string_view kSmokeDeferredValueTypeName = "com.asharia.smoke.DeferredValue";
    constexpr std::string_view kEditorVisibleAttribute = "asharia.editor.visible";
    constexpr std::string_view kRuntimeVisibleAttribute = "asharia.runtime.visible";
    constexpr std::string_view kScriptVisibleAttribute = "asharia.script.visible";

    struct ReflectionSmokeVec3 {
        float x{};
        float y{};
        float z{};
    };

    struct ReflectionSmokeQuat {
        float x{};
        float y{};
        float z{};
        float w{1.0F};
    };

    struct ReflectionSmokeTransform {
        ReflectionSmokeVec3 position;
        ReflectionSmokeQuat rotation;
        ReflectionSmokeVec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        std::string debugName;
        float cachedMagnitude{};
        std::int32_t scriptCounter{};
    };

    struct ReflectionSmokeMigratedTransform {
        ReflectionSmokeVec3 position;
        ReflectionSmokeQuat rotation;
        ReflectionSmokeVec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        std::string debugName;
    };

    struct ReflectionSmokeDeferredValue {
        std::int32_t value{};
    };

    struct ReflectionSmokeDeferredOwner {
        ReflectionSmokeDeferredValue deferred;
    };

    struct ReflectionSmokePropertyComponent {
        [[nodiscard]] float exposure() const noexcept {
            return exposure_;
        }

        [[nodiscard]] std::int32_t derivedCounter() const noexcept {
            return counter_ + 1;
        }

        asharia::VoidResult setExposure(const float& exposure) noexcept {
            exposure_ = exposure;
            dirty_ = true;
            return {};
        }

        [[nodiscard]] bool isDirty() const noexcept {
            return dirty_;
        }

    private:
        float exposure_{1.5F};
        std::int32_t counter_{40};
        bool dirty_{};
    };

    asharia::Error smokeSerializationError(std::string message) {
        return asharia::Error{asharia::ErrorDomain::Serialization, 0, std::move(message)};
    }

    bool containsAll(std::string_view text, std::initializer_list<std::string_view> needles) {
        return std::ranges::all_of(needles, [text](std::string_view needle) {
            return text.find(needle) != std::string_view::npos;
        });
    }

    bool removeArchiveMember(asharia::serialization::ArchiveValue& object,
                             std::string_view memberKey) {
        if (object.kind != asharia::serialization::ArchiveValueKind::Object) {
            return false;
        }

        const std::size_t originalSize = object.objectValue.size();
        std::erase_if(object.objectValue,
                      [memberKey](const asharia::serialization::ArchiveMember& member) {
                          return member.key == memberKey;
                      });
        return object.objectValue.size() != originalSize;
    }

    const asharia::reflection::FieldInfo* findField(const asharia::reflection::TypeInfo& type,
                                                    std::string_view name) {
        const auto found =
            std::ranges::find_if(type.fields, [name](const asharia::reflection::FieldInfo& field) {
                return field.name == name;
            });
        return found == type.fields.end() ? nullptr : &*found;
    }

    bool hasContextField(const asharia::reflection::ContextFieldView& view, std::string_view name) {
        return std::ranges::any_of(view.fields,
                                   [name](const asharia::reflection::FieldInfo* field) {
                                       return field != nullptr && field->name == name;
                                   });
    }

    [[nodiscard]] asharia::reflection::AttributeSet storedEditableScriptAttributes() {
        return {
            asharia::serialization::storage_attributes::persistent(),
            asharia::reflection::attributes::boolean(kEditorVisibleAttribute, true),
            asharia::reflection::attributes::boolean(kRuntimeVisibleAttribute, true),
            asharia::reflection::attributes::boolean(kScriptVisibleAttribute, true),
        };
    }

    [[nodiscard]] asharia::reflection::AttributeSet editorOnlyPersistentAttributes() {
        return {
            asharia::serialization::storage_attributes::persistent(),
            asharia::reflection::attributes::boolean(kEditorVisibleAttribute, true),
            asharia::reflection::attributes::boolean("asharia.editor.only", true),
        };
    }

    [[nodiscard]] asharia::reflection::AttributeSet runtimeVisibleAttributes() {
        return {
            asharia::reflection::attributes::boolean(kRuntimeVisibleAttribute, true),
        };
    }

    [[nodiscard]] asharia::reflection::AttributeSet scriptVisibleAttributes() {
        return {
            asharia::reflection::attributes::boolean(kRuntimeVisibleAttribute, true),
            asharia::reflection::attributes::boolean(kScriptVisibleAttribute, true),
        };
    }

    asharia::VoidResult registerReflectionSmokeTypes(asharia::reflection::TypeRegistry& registry) {
        using namespace asharia::reflection;

        auto builtins = registerBuiltinTypes(registry);
        if (!builtins) {
            return builtins;
        }

        const TypeId vec3Type = makeTypeId(kSmokeVec3TypeName);
        const TypeId quatType = makeTypeId(kSmokeQuatTypeName);

        auto vec3Registered =
            TypeBuilder<ReflectionSmokeVec3>(registry, kSmokeVec3TypeName)
                .kind(TypeKind::Struct)
                .field("x", &ReflectionSmokeVec3::x, storedEditableScriptAttributes())
                .field("y", &ReflectionSmokeVec3::y, storedEditableScriptAttributes())
                .field("z", &ReflectionSmokeVec3::z, storedEditableScriptAttributes())
                .commit();
        if (!vec3Registered) {
            return vec3Registered;
        }

        auto quatRegistered =
            TypeBuilder<ReflectionSmokeQuat>(registry, kSmokeQuatTypeName)
                .kind(TypeKind::Struct)
                .field("x", &ReflectionSmokeQuat::x, storedEditableScriptAttributes())
                .field("y", &ReflectionSmokeQuat::y, storedEditableScriptAttributes())
                .field("z", &ReflectionSmokeQuat::z, storedEditableScriptAttributes())
                .field("w", &ReflectionSmokeQuat::w, storedEditableScriptAttributes())
                .commit();
        if (!quatRegistered) {
            return quatRegistered;
        }

        auto transformRegistered =
            TypeBuilder<ReflectionSmokeTransform>(registry, kSmokeTransformTypeName)
                .kind(TypeKind::Component)
                .field("position", &ReflectionSmokeTransform::position, vec3Type,
                       storedEditableScriptAttributes())
                .field("rotation", &ReflectionSmokeTransform::rotation, quatType,
                       storedEditableScriptAttributes())
                .field("scale", &ReflectionSmokeTransform::scale, vec3Type,
                       storedEditableScriptAttributes())
                .defaultValue(ReflectionSmokeVec3{.x = 1.0F, .y = 1.0F, .z = 1.0F})
                .field("debugName", &ReflectionSmokeTransform::debugName,
                       editorOnlyPersistentAttributes())
                .readonlyField("cachedMagnitude", &ReflectionSmokeTransform::cachedMagnitude,
                               runtimeVisibleAttributes())
                .readonlyField("scriptCounter", &ReflectionSmokeTransform::scriptCounter,
                               scriptVisibleAttributes())
                .commit();
        if (!transformRegistered) {
            return transformRegistered;
        }

        auto propertyRegistered =
            TypeBuilder<ReflectionSmokePropertyComponent>(registry, kSmokePropertyComponentTypeName)
                .kind(TypeKind::Component)
                .property<float>(
                    "exposure",
                    [](const ReflectionSmokePropertyComponent& component) {
                        return component.exposure();
                    },
                    [](ReflectionSmokePropertyComponent& component, const float& exposure) {
                        return component.setExposure(exposure);
                    },
                    storedEditableScriptAttributes())
                .readonlyProperty<std::int32_t>(
                    "derivedCounter",
                    [](const ReflectionSmokePropertyComponent& component) {
                        return component.derivedCounter();
                    },
                    runtimeVisibleAttributes())
                .commit();
        if (!propertyRegistered) {
            return propertyRegistered;
        }

        return TypeBuilder<ReflectionSmokeMigratedTransform>(registry,
                                                             kSmokeMigratedTransformTypeName)
            .version(2)
            .kind(TypeKind::Component)
            .field("position", &ReflectionSmokeMigratedTransform::position, vec3Type,
                   storedEditableScriptAttributes())
            .field("rotation", &ReflectionSmokeMigratedTransform::rotation, quatType,
                   storedEditableScriptAttributes())
            .field("scale", &ReflectionSmokeMigratedTransform::scale, vec3Type,
                   storedEditableScriptAttributes())
            .defaultValue(ReflectionSmokeVec3{.x = 1.0F, .y = 1.0F, .z = 1.0F})
            .field("debugName", &ReflectionSmokeMigratedTransform::debugName,
                   editorOnlyPersistentAttributes())
            .commit();
    }

    std::optional<asharia::reflection::TypeRegistry> makeReflectionSmokeRegistry() {
        asharia::reflection::TypeRegistry registry;
        auto registered = registerReflectionSmokeTypes(registry);
        if (!registered) {
            asharia::logError(registered.error().message);
            return std::nullopt;
        }
        auto frozen = registry.freeze();
        if (!frozen) {
            asharia::logError(frozen.error().message);
            return std::nullopt;
        }
        return registry;
    }

    asharia::serialization::ArchiveValue makeVec3Archive(float xValue, float yValue, float zValue) {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        return ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeVec3TypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(1),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object({
                    ArchiveMember{.key = "x", .value = ArchiveValue::floating(xValue)},
                    ArchiveMember{.key = "y", .value = ArchiveValue::floating(yValue)},
                    ArchiveMember{.key = "z", .value = ArchiveValue::floating(zValue)},
                }),
            },
        });
    }

    asharia::serialization::ArchiveValue
    makeQuatArchive(const asharia::serialization::ArchiveValue& zValue) {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        return ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeQuatTypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(1),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object({
                    ArchiveMember{.key = "x", .value = ArchiveValue::floating(0.0)},
                    ArchiveMember{.key = "y", .value = ArchiveValue::floating(0.0)},
                    ArchiveMember{.key = "z", .value = zValue},
                    ArchiveMember{.key = "w", .value = ArchiveValue::floating(1.0)},
                }),
            },
        });
    }

    asharia::serialization::ArchiveValue makeMigratedTransformV1Archive() {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        return ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeMigratedTransformTypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(1),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object({
                    ArchiveMember{
                        .key = "position",
                        .value = makeVec3Archive(1.0F, 2.0F, 3.0F),
                    },
                    ArchiveMember{
                        .key = "eulerRotation",
                        .value = makeVec3Archive(0.0F, 0.0F, 0.25F),
                    },
                    ArchiveMember{
                        .key = "debugName",
                        .value = ArchiveValue::string("migrated"),
                    },
                }),
            },
        });
    }

    asharia::VoidResult
    migrateSmokeTransformV1ToV2(asharia::serialization::MigrationContext& context) {
        using asharia::serialization::ArchiveMember;
        using asharia::serialization::ArchiveValue;

        const ArchiveValue* inputFields =
            context.input == nullptr ? nullptr : context.input->findMemberValue("fields");
        const ArchiveValue* position =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("position");
        const ArchiveValue* eulerRotation =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("eulerRotation");
        const ArchiveValue* debugName =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("debugName");
        const ArchiveValue* eulerFields =
            eulerRotation == nullptr ? nullptr : eulerRotation->findMemberValue("fields");
        const ArchiveValue* eulerZ =
            eulerFields == nullptr ? nullptr : eulerFields->findMemberValue("z");
        if (context.output == nullptr || position == nullptr || eulerZ == nullptr) {
            return std::unexpected{
                smokeSerializationError("Smoke migration is missing position or eulerRotation.z.")};
        }

        std::vector<ArchiveMember> migratedFields{
            ArchiveMember{.key = "position", .value = *position},
            ArchiveMember{.key = "rotation", .value = makeQuatArchive(*eulerZ)},
            ArchiveMember{.key = "scale", .value = makeVec3Archive(1.0F, 1.0F, 1.0F)},
        };
        if (debugName != nullptr) {
            migratedFields.push_back(ArchiveMember{.key = "debugName", .value = *debugName});
        }

        *context.output = ArchiveValue::object({
            ArchiveMember{
                .key = "type",
                .value = ArchiveValue::string(std::string{kSmokeMigratedTransformTypeName}),
            },
            ArchiveMember{
                .key = "version",
                .value = ArchiveValue::integer(context.toVersion),
            },
            ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object(std::move(migratedFields)),
            },
        });
        return {};
    }

    bool smokeDeferredFieldRegistration() {
        using namespace asharia::reflection;

        TypeRegistry registry;
        auto builtins = registerBuiltinTypes(registry);
        if (!builtins) {
            asharia::logError(builtins.error().message);
            return false;
        }

        const TypeId deferredValueType = makeTypeId(kSmokeDeferredValueTypeName);
        auto ownerRegistered =
            TypeBuilder<ReflectionSmokeDeferredOwner>(registry, kSmokeDeferredOwnerTypeName)
                .kind(TypeKind::Struct)
                .field("deferred", &ReflectionSmokeDeferredOwner::deferred, deferredValueType,
                       {asharia::serialization::storage_attributes::persistent()})
                .commit();
        if (!ownerRegistered) {
            asharia::logError(ownerRegistered.error().message);
            return false;
        }

        auto missingFreeze = registry.freeze();
        if (missingFreeze ||
            !containsAll(missingFreeze.error().message, {
                                                            "operation=freeze",
                                                            "type=com.asharia.smoke.DeferredOwner",
                                                            "field=deferred",
                                                            "expected=registered field type",
                                                            "actual=missing",
                                                        })) {
            asharia::logError(
                "Reflection registry smoke did not reject missing field type at freeze.");
            return false;
        }

        auto valueRegistered =
            TypeBuilder<ReflectionSmokeDeferredValue>(registry, kSmokeDeferredValueTypeName)
                .kind(TypeKind::Struct)
                .field("value", &ReflectionSmokeDeferredValue::value,
                       {asharia::serialization::storage_attributes::persistent()})
                .commit();
        if (!valueRegistered) {
            asharia::logError(valueRegistered.error().message);
            return false;
        }

        auto frozen = registry.freeze();
        if (!frozen) {
            asharia::logError(frozen.error().message);
            return false;
        }

        return true;
    }

    int runSmokeReflectionRegistry() {
        if (!smokeDeferredFieldRegistration()) {
            return EXIT_FAILURE;
        }

        asharia::reflection::TypeRegistry registry;
        auto registered = registerReflectionSmokeTypes(registry);
        if (!registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        if (registry.findType(kSmokeTransformTypeName) == nullptr) {
            asharia::logError("Reflection registry smoke could not find the transform type.");
            return EXIT_FAILURE;
        }

        asharia::reflection::TypeInfo duplicate{
            .id = asharia::reflection::makeTypeId(kSmokeTransformTypeName),
            .name = std::string{kSmokeTransformTypeName},
            .version = 1,
            .kind = asharia::reflection::TypeKind::Component,
            .attributes = {},
            .fields = {},
        };
        auto duplicateRegistered = registry.registerType(std::move(duplicate));
        if (duplicateRegistered || !containsAll(duplicateRegistered.error().message,
                                                {
                                                    "operation=register",
                                                    "type=com.asharia.smoke.Transform",
                                                    "expected=unique type name",
                                                    "actual=duplicate",
                                                })) {
            asharia::logError("Reflection registry smoke accepted a duplicate type.");
            return EXIT_FAILURE;
        }

        auto frozen = registry.freeze();
        if (!frozen) {
            asharia::logError(frozen.error().message);
            return EXIT_FAILURE;
        }

        asharia::reflection::TypeInfo lateType{
            .id = asharia::reflection::makeTypeId("com.asharia.smoke.LateType"),
            .name = "com.asharia.smoke.LateType",
            .version = 1,
            .kind = asharia::reflection::TypeKind::Struct,
            .attributes = {},
            .fields = {},
        };
        auto lateRegistered = registry.registerType(std::move(lateType));
        if (lateRegistered ||
            !containsAll(lateRegistered.error().message, {
                                                             "operation=register",
                                                             "type=com.asharia.smoke.LateType",
                                                             "expected=mutable registry",
                                                             "actual=frozen",
                                                         })) {
            asharia::logError("Reflection registry smoke accepted registration after freeze.");
            return EXIT_FAILURE;
        }

        std::cout << "Reflection registry types: " << registry.types().size() << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeReflectionTransform() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return EXIT_FAILURE;
        }

        const asharia::reflection::TypeInfo* transformType =
            registry->findType(kSmokeTransformTypeName);
        if (transformType == nullptr || transformType->fields.size() != 6) {
            asharia::logError("Reflection transform smoke saw an unexpected field count.");
            return EXIT_FAILURE;
        }

        const asharia::reflection::FieldInfo* positionField = findField(*transformType, "position");
        const asharia::reflection::FieldInfo* cachedField =
            findField(*transformType, "cachedMagnitude");
        if (positionField == nullptr || cachedField == nullptr ||
            !asharia::reflection::hasBoolAttribute(
                positionField->attributes,
                asharia::serialization::storage_attributes::kPersistent) ||
            !asharia::reflection::hasBoolAttribute(positionField->attributes,
                                                   kEditorVisibleAttribute) ||
            !asharia::reflection::hasBoolAttribute(positionField->attributes,
                                                   kRuntimeVisibleAttribute) ||
            !asharia::reflection::hasBoolAttribute(positionField->attributes,
                                                   kScriptVisibleAttribute) ||
            cachedField->accessor.writeAddress) {
            asharia::logError("Reflection transform smoke saw unexpected field metadata.");
            return EXIT_FAILURE;
        }

        ReflectionSmokeTransform transform{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
            .debugName = "transform smoke",
            .cachedMagnitude = 3.0F,
            .scriptCounter = 7,
        };

        const auto* readPosition = static_cast<const ReflectionSmokeVec3*>(
            positionField->accessor.readAddress(&transform));
        auto* writePosition =
            static_cast<ReflectionSmokeVec3*>(positionField->accessor.writeAddress(&transform));
        if (readPosition == nullptr || writePosition == nullptr || readPosition->x != 1.0F) {
            asharia::logError(
                "Reflection transform smoke failed to read position through accessor.");
            return EXIT_FAILURE;
        }
        writePosition->x = 4.0F;
        if (transform.position.x != 4.0F) {
            asharia::logError(
                "Reflection transform smoke failed to write position through accessor.");
            return EXIT_FAILURE;
        }

        const asharia::reflection::TypeInfo* propertyType =
            registry->findType(kSmokePropertyComponentTypeName);
        if (propertyType == nullptr || propertyType->fields.size() != 2) {
            asharia::logError("Reflection property smoke saw an unexpected field count.");
            return EXIT_FAILURE;
        }

        const asharia::reflection::FieldInfo* exposureField = findField(*propertyType, "exposure");
        const asharia::reflection::FieldInfo* derivedField =
            findField(*propertyType, "derivedCounter");
        if (exposureField == nullptr || derivedField == nullptr ||
            exposureField->accessor.readAddress || exposureField->accessor.writeAddress ||
            !exposureField->accessor.readValue || !exposureField->accessor.writeValue ||
            derivedField->accessor.writeValue || derivedField->accessor.writeAddress ||
            !derivedField->accessor.readValue) {
            asharia::logError("Reflection property smoke saw unexpected accessor metadata.");
            return EXIT_FAILURE;
        }

        ReflectionSmokePropertyComponent component;
        float exposure{};
        auto readExposure = exposureField->accessor.readValue(&component, &exposure);
        if (!readExposure || exposure != 1.5F) {
            asharia::logError("Reflection property smoke failed to read through getter.");
            return EXIT_FAILURE;
        }

        const float updatedExposure = 2.25F;
        auto wroteExposure = exposureField->accessor.writeValue(&component, &updatedExposure);
        if (!wroteExposure || component.exposure() != updatedExposure || !component.isDirty()) {
            asharia::logError("Reflection property smoke failed to write through setter.");
            return EXIT_FAILURE;
        }

        std::int32_t derivedCounter{};
        auto readDerived = derivedField->accessor.readValue(&component, &derivedCounter);
        if (!readDerived || derivedCounter != 41) {
            asharia::logError("Reflection property smoke failed to read readonly property.");
            return EXIT_FAILURE;
        }

        std::cout << "Reflection transform fields: " << transformType->fields.size() << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeReflectionAttributes() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return EXIT_FAILURE;
        }

        const asharia::reflection::TypeInfo* transformType =
            registry->findType(kSmokeTransformTypeName);
        if (transformType == nullptr) {
            asharia::logError("Reflection attribute smoke could not find transform type.");
            return EXIT_FAILURE;
        }

        const asharia::reflection::ContextFieldView storageView =
            asharia::reflection::makeAttributeContextView(
                *transformType, asharia::serialization::storage_attributes::kPersistent);
        const asharia::reflection::ContextFieldView runtimeView =
            asharia::reflection::makeAttributeContextView(*transformType, kRuntimeVisibleAttribute);
        const asharia::reflection::ContextFieldView scriptView =
            asharia::reflection::makeAttributeContextView(*transformType, kScriptVisibleAttribute);

        if (!hasContextField(storageView, "debugName") ||
            hasContextField(storageView, "cachedMagnitude") ||
            !hasContextField(runtimeView, "cachedMagnitude") ||
            !hasContextField(scriptView, "scriptCounter") ||
            hasContextField(scriptView, "debugName")) {
            asharia::logError("Reflection attribute smoke produced unexpected field projections.");
            return EXIT_FAILURE;
        }

        std::cout << "Reflection attribute fields: storage=" << storageView.fields.size()
                  << ", runtime=" << runtimeView.fields.size()
                  << ", script=" << scriptView.fields.size() << '\n';
        return EXIT_SUCCESS;
    }

    bool smokeSerializationPropertyRoundtrip(const asharia::reflection::TypeRegistry& registry) {
        const asharia::reflection::TypeId propertyType =
            asharia::reflection::makeTypeId(kSmokePropertyComponentTypeName);
        ReflectionSmokePropertyComponent propertySource;
        const float propertyExposure = 2.75F;
        auto sourcePropertyWritten = propertySource.setExposure(propertyExposure);
        if (!sourcePropertyWritten) {
            asharia::logError(sourcePropertyWritten.error().message);
            return false;
        }

        auto propertyArchive =
            asharia::serialization::serializeObject(registry, propertyType, &propertySource);
        if (!propertyArchive) {
            asharia::logError(propertyArchive.error().message);
            return false;
        }

        ReflectionSmokePropertyComponent propertyLoaded;
        auto propertyLoadedResult = asharia::serialization::deserializeObject(
            registry, propertyType, *propertyArchive, &propertyLoaded);
        if (!propertyLoadedResult) {
            asharia::logError(propertyLoadedResult.error().message);
            return false;
        }
        if (propertyLoaded.exposure() != propertyExposure || !propertyLoaded.isDirty()) {
            asharia::logError("Serialization roundtrip smoke did not use property getter/setter.");
            return false;
        }
        return true;
    }

    bool smokeSerializationUnknownFieldPolicies(const asharia::reflection::TypeRegistry& registry,
                                                asharia::reflection::TypeId transformType,
                                                const asharia::serialization::ArchiveValue& archive,
                                                const ReflectionSmokeTransform& source) {
        asharia::serialization::ArchiveValue unknownFieldArchive = archive;
        asharia::serialization::ArchiveValue* unknownFields =
            unknownFieldArchive.findMemberValue("fields");
        if (unknownFields == nullptr ||
            unknownFields->kind != asharia::serialization::ArchiveValueKind::Object) {
            asharia::logError("Serialization roundtrip smoke could not edit unknown fields.");
            return false;
        }
        unknownFields->objectValue.push_back(asharia::serialization::ArchiveMember{
            .key = "unknownFutureField",
            .value = asharia::serialization::ArchiveValue::integer(7),
        });

        ReflectionSmokeTransform rejectedUnknown{};
        auto rejectedUnknownResult = asharia::serialization::deserializeObject(
            registry, transformType, unknownFieldArchive, &rejectedUnknown);
        if (rejectedUnknownResult ||
            !containsAll(rejectedUnknownResult.error().message,
                         {
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.Transform.unknownFutureField",
                             "field=unknownFutureField",
                             "expected=registered serializable field",
                             "actual=unknown field",
                             "policy=error",
                         })) {
            asharia::logError("Serialization roundtrip smoke did not reject an unknown field.");
            return false;
        }

        const asharia::serialization::SerializationPolicy ignoreUnknownPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::serialization::UnknownFieldPolicy::Ignore,
            .missingFields = asharia::serialization::MissingFieldPolicy::UseDefault,
            .migrations = nullptr,
            .archivePath = {},
            .migrationScenario = asharia::serialization::MigrationScenario::Unspecified,
        };
        ReflectionSmokeTransform ignoredUnknown{};
        auto ignoredUnknownResult = asharia::serialization::deserializeObject(
            registry, transformType, unknownFieldArchive, &ignoredUnknown, ignoreUnknownPolicy);
        if (!ignoredUnknownResult || ignoredUnknown.position.x != source.position.x) {
            asharia::logError("Serialization roundtrip smoke did not ignore an unknown field.");
            return false;
        }

        const asharia::serialization::SerializationPolicy preserveUnknownPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::serialization::UnknownFieldPolicy::Preserve,
            .missingFields = asharia::serialization::MissingFieldPolicy::UseDefault,
            .migrations = nullptr,
            .archivePath = {},
            .migrationScenario = asharia::serialization::MigrationScenario::Unspecified,
        };
        ReflectionSmokeTransform preserveUnknown{};
        auto preserveUnknownResult = asharia::serialization::deserializeObject(
            registry, transformType, unknownFieldArchive, &preserveUnknown, preserveUnknownPolicy);
        if (preserveUnknownResult || preserveUnknownResult.error().message.find(
                                         "preserve policy unsupported") == std::string::npos) {
            asharia::logError(
                "Serialization roundtrip smoke did not reject unsupported preserve policy.");
            return false;
        }

        return true;
    }

    bool
    smokeSerializationMissingFieldPolicies(const asharia::reflection::TypeRegistry& registry,
                                           asharia::reflection::TypeId transformType,
                                           const asharia::serialization::ArchiveValue& archive) {
        asharia::serialization::ArchiveValue missingScaleArchive = archive;
        asharia::serialization::ArchiveValue* missingScaleFields =
            missingScaleArchive.findMemberValue("fields");
        if (missingScaleFields == nullptr || !removeArchiveMember(*missingScaleFields, "scale")) {
            asharia::logError("Serialization roundtrip smoke could not remove scale.");
            return false;
        }

        ReflectionSmokeTransform loadedMissingScale{
            .position = {},
            .rotation = {},
            .scale = {.x = 8.0F, .y = 8.0F, .z = 8.0F},
            .debugName = {},
            .cachedMagnitude = 0.0F,
            .scriptCounter = 0,
        };
        auto missingScaleResult = asharia::serialization::deserializeObject(
            registry, transformType, missingScaleArchive, &loadedMissingScale);
        if (!missingScaleResult || loadedMissingScale.scale.x != 1.0F ||
            loadedMissingScale.scale.y != 1.0F || loadedMissingScale.scale.z != 1.0F) {
            asharia::logError("Serialization roundtrip smoke did not apply field default.");
            return false;
        }

        const asharia::serialization::SerializationPolicy keepConstructedPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::serialization::UnknownFieldPolicy::Error,
            .missingFields = asharia::serialization::MissingFieldPolicy::KeepConstructedValue,
            .migrations = nullptr,
            .archivePath = {},
            .migrationScenario = asharia::serialization::MigrationScenario::Unspecified,
        };
        ReflectionSmokeTransform keptConstructed{
            .position = {},
            .rotation = {},
            .scale = {.x = 8.0F, .y = 8.0F, .z = 8.0F},
            .debugName = {},
            .cachedMagnitude = 0.0F,
            .scriptCounter = 0,
        };
        auto keptConstructedResult = asharia::serialization::deserializeObject(
            registry, transformType, missingScaleArchive, &keptConstructed, keepConstructedPolicy);
        if (!keptConstructedResult || keptConstructed.scale.x != 8.0F ||
            keptConstructed.scale.y != 8.0F || keptConstructed.scale.z != 8.0F) {
            asharia::logError("Serialization roundtrip smoke did not keep constructed field.");
            return false;
        }

        const asharia::serialization::SerializationPolicy missingErrorPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::serialization::UnknownFieldPolicy::Error,
            .missingFields = asharia::serialization::MissingFieldPolicy::Error,
            .migrations = nullptr,
            .archivePath = {},
            .migrationScenario = asharia::serialization::MigrationScenario::Unspecified,
        };
        ReflectionSmokeTransform rejectedMissingScale{};
        auto rejectedMissingScaleResult =
            asharia::serialization::deserializeObject(registry, transformType, missingScaleArchive,
                                                      &rejectedMissingScale, missingErrorPolicy);
        if (rejectedMissingScaleResult ||
            !containsAll(rejectedMissingScaleResult.error().message,
                         {
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.Transform.scale",
                             "expected=archive field",
                             "actual=missing",
                             "policy=error",
                         })) {
            asharia::logError("Serialization roundtrip smoke did not reject missing field.");
            return false;
        }

        asharia::serialization::ArchiveValue missingPositionArchive = archive;
        asharia::serialization::ArchiveValue* missingPositionFields =
            missingPositionArchive.findMemberValue("fields");
        if (missingPositionFields == nullptr ||
            !removeArchiveMember(*missingPositionFields, "position")) {
            asharia::logError("Serialization roundtrip smoke could not remove position.");
            return false;
        }

        ReflectionSmokeTransform rejectedMissingDefault{};
        auto rejectedMissingDefaultResult = asharia::serialization::deserializeObject(
            registry, transformType, missingPositionArchive, &rejectedMissingDefault);
        if (rejectedMissingDefaultResult ||
            !containsAll(rejectedMissingDefaultResult.error().message,
                         {
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.Transform.position",
                             "expected=reflected field default value",
                             "actual=missing",
                         })) {
            asharia::logError("Serialization roundtrip smoke accepted missing default metadata.");
            return false;
        }

        return true;
    }

    int runSmokeSerializationRoundtrip() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return EXIT_FAILURE;
        }

        const asharia::reflection::TypeId transformType =
            asharia::reflection::makeTypeId(kSmokeTransformTypeName);
        const ReflectionSmokeTransform source{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
            .debugName = "roundtrip",
            .cachedMagnitude = 99.0F,
            .scriptCounter = 12,
        };

        auto archive = asharia::serialization::serializeObject(*registry, transformType, &source);
        if (!archive) {
            asharia::logError(archive.error().message);
            return EXIT_FAILURE;
        }

        asharia::reflection::TypeRegistry mutableRegistry;
        auto mutableRegistered = registerReflectionSmokeTypes(mutableRegistry);
        if (!mutableRegistered) {
            asharia::logError(mutableRegistered.error().message);
            return EXIT_FAILURE;
        }

        auto rejectedMutableSerialize =
            asharia::serialization::serializeObject(mutableRegistry, transformType, &source);
        if (rejectedMutableSerialize || !containsAll(rejectedMutableSerialize.error().message,
                                                     {
                                                         "operation=serialize",
                                                         "expected=frozen reflection registry",
                                                         "actual=mutable registry",
                                                     })) {
            asharia::logError("Serialization roundtrip smoke serialized with a mutable registry.");
            return EXIT_FAILURE;
        }

        ReflectionSmokeTransform rejectedMutable{};
        auto rejectedMutableDeserialize = asharia::serialization::deserializeObject(
            mutableRegistry, transformType, *archive, &rejectedMutable);
        if (rejectedMutableDeserialize || !containsAll(rejectedMutableDeserialize.error().message,
                                                       {
                                                           "operation=deserialize",
                                                           "expected=frozen reflection registry",
                                                           "actual=mutable registry",
                                                       })) {
            asharia::logError(
                "Serialization roundtrip smoke deserialized with a mutable registry.");
            return EXIT_FAILURE;
        }

        auto firstText = asharia::serialization::writeTextArchive(*archive);
        if (!firstText) {
            asharia::logError(firstText.error().message);
            return EXIT_FAILURE;
        }
        auto secondText = asharia::serialization::writeTextArchive(*archive);
        if (!secondText) {
            asharia::logError(secondText.error().message);
            return EXIT_FAILURE;
        }
        if (*firstText != *secondText) {
            asharia::logError("Serialization roundtrip smoke produced nondeterministic text.");
            return EXIT_FAILURE;
        }

        ReflectionSmokeTransform loaded{};
        auto loadedResult =
            asharia::serialization::deserializeObject(*registry, transformType, *archive, &loaded);
        if (!loadedResult) {
            asharia::logError(loadedResult.error().message);
            return EXIT_FAILURE;
        }

        if (loaded.position.x != source.position.x || loaded.position.y != source.position.y ||
            loaded.position.z != source.position.z || loaded.rotation.w != source.rotation.w ||
            loaded.scale.x != source.scale.x || loaded.debugName != source.debugName ||
            loaded.cachedMagnitude != 0.0F || loaded.scriptCounter != 0) {
            asharia::logError("Serialization roundtrip smoke loaded unexpected values.");
            return EXIT_FAILURE;
        }

        if (!smokeSerializationUnknownFieldPolicies(*registry, transformType, *archive, source)) {
            return EXIT_FAILURE;
        }

        if (!smokeSerializationMissingFieldPolicies(*registry, transformType, *archive)) {
            return EXIT_FAILURE;
        }

        if (!smokeSerializationPropertyRoundtrip(*registry)) {
            return EXIT_FAILURE;
        }

        asharia::serialization::ArchiveValue badArchive = *archive;
        asharia::serialization::ArchiveValue* fields = badArchive.findMemberValue("fields");
        asharia::serialization::ArchiveValue* position =
            fields == nullptr ? nullptr : fields->findMemberValue("position");
        asharia::serialization::ArchiveValue* positionFields =
            position == nullptr ? nullptr : position->findMemberValue("fields");
        asharia::serialization::ArchiveValue* xValue =
            positionFields == nullptr ? nullptr : positionFields->findMemberValue("x");
        if (xValue == nullptr) {
            asharia::logError("Serialization roundtrip smoke could not edit the bad archive.");
            return EXIT_FAILURE;
        }
        *xValue = asharia::serialization::ArchiveValue::string("wrong");

        ReflectionSmokeTransform rejected{};
        auto rejectedResult = asharia::serialization::deserializeObject(*registry, transformType,
                                                                        badArchive, &rejected);
        if (rejectedResult || !containsAll(rejectedResult.error().message,
                                           {
                                               "operation=deserialize",
                                               "objectPath=com.asharia.smoke.Transform.position.x",
                                               "type=com.asharia.core.Float",
                                               "expected=float",
                                               "actual=string",
                                           })) {
            asharia::logError("Serialization roundtrip smoke did not reject a bad field type.");
            return EXIT_FAILURE;
        }

        asharia::serialization::ArchiveValue badVersionArchive = *archive;
        asharia::serialization::ArchiveValue* versionValue =
            badVersionArchive.findMemberValue("version");
        if (versionValue == nullptr) {
            asharia::logError("Serialization roundtrip smoke could not edit the archive version.");
            return EXIT_FAILURE;
        }
        *versionValue = asharia::serialization::ArchiveValue::integer(2);

        ReflectionSmokeTransform rejectedVersion{};
        auto rejectedVersionResult = asharia::serialization::deserializeObject(
            *registry, transformType, badVersionArchive, &rejectedVersion);
        if (rejectedVersionResult || !containsAll(rejectedVersionResult.error().message,
                                                  {
                                                      "operation=deserialize",
                                                      "objectPath=com.asharia.smoke.Transform",
                                                      "type=com.asharia.smoke.Transform",
                                                      "field=version",
                                                      "expected=migration policy",
                                                      "actual=no migration registry",
                                                      "version=2",
                                                  })) {
            asharia::logError("Serialization roundtrip smoke did not reject a bad type version.");
            return EXIT_FAILURE;
        }

        std::cout << "Serialization roundtrip bytes: " << firstText->size() << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeSerializationJsonArchive() {
        std::string escaped = "quote \" slash \\ newline \n carriage \r tab \t";
        escaped.push_back('\b');
        escaped.push_back('\f');

        std::string validUtf8 = "utf8 ";
        validUtf8.push_back(static_cast<char>(0xC3));
        validUtf8.push_back(static_cast<char>(0xA9));

        asharia::serialization::ArchiveValue archive =
            asharia::serialization::ArchiveValue::object({
                asharia::serialization::ArchiveMember{
                    .key = "escaped",
                    .value = asharia::serialization::ArchiveValue::string(escaped),
                },
                asharia::serialization::ArchiveMember{
                    .key = "validUtf8",
                    .value = asharia::serialization::ArchiveValue::string(validUtf8),
                },
                asharia::serialization::ArchiveMember{
                    .key = "array",
                    .value = asharia::serialization::ArchiveValue::array({
                        asharia::serialization::ArchiveValue::integer(7),
                        asharia::serialization::ArchiveValue::floating(0.25),
                        asharia::serialization::ArchiveValue::boolean(true),
                    }),
                },
            });

        auto firstText = asharia::serialization::writeTextArchive(archive);
        if (!firstText) {
            asharia::logError(firstText.error().message);
            return EXIT_FAILURE;
        }
        auto secondText = asharia::serialization::writeTextArchive(archive);
        if (!secondText) {
            asharia::logError(secondText.error().message);
            return EXIT_FAILURE;
        }
        if (*firstText != *secondText) {
            asharia::logError("JSON archive smoke produced nondeterministic output.");
            return EXIT_FAILURE;
        }

        auto parsed = asharia::serialization::readTextArchive(*firstText);
        if (!parsed) {
            asharia::logError(parsed.error().message);
            return EXIT_FAILURE;
        }
        const asharia::serialization::ArchiveValue* parsedEscaped =
            parsed->findMemberValue("escaped");
        const asharia::serialization::ArchiveValue* parsedUtf8 =
            parsed->findMemberValue("validUtf8");
        if (parsedEscaped == nullptr ||
            parsedEscaped->kind != asharia::serialization::ArchiveValueKind::String ||
            parsedEscaped->stringValue != escaped || parsedUtf8 == nullptr ||
            parsedUtf8->kind != asharia::serialization::ArchiveValueKind::String ||
            parsedUtf8->stringValue != validUtf8) {
            asharia::logError("JSON archive smoke failed to round-trip escaped strings.");
            return EXIT_FAILURE;
        }

        const std::filesystem::path archivePath = std::filesystem::temp_directory_path() /
                                                  "asharia-serialization-json-archive-smoke.json";
        auto fileWritten = asharia::serialization::writeTextArchiveFile(archivePath, archive);
        if (!fileWritten) {
            asharia::logError(fileWritten.error().message);
            return EXIT_FAILURE;
        }

        auto fileParsed = asharia::serialization::readTextArchiveFile(archivePath);
        std::error_code removeError;
        std::filesystem::remove(archivePath, removeError);
        if (!fileParsed) {
            asharia::logError(fileParsed.error().message);
            return EXIT_FAILURE;
        }

        const asharia::serialization::ArchiveValue* fileEscaped =
            fileParsed->findMemberValue("escaped");
        if (fileEscaped == nullptr ||
            fileEscaped->kind != asharia::serialization::ArchiveValueKind::String ||
            fileEscaped->stringValue != escaped) {
            asharia::logError("JSON archive smoke failed to round-trip through file IO.");
            return EXIT_FAILURE;
        }

        auto duplicate = asharia::serialization::readTextArchive(R"({"field":1,"field":2})");
        if (duplicate || duplicate.error().message.find("duplicate key") == std::string::npos) {
            asharia::logError("JSON archive smoke did not reject a duplicate object key.");
            return EXIT_FAILURE;
        }

        auto malformed = asharia::serialization::readTextArchive("{");
        if (malformed || malformed.error().message.find("byte") == std::string::npos) {
            asharia::logError(
                "JSON archive smoke did not report a parse byte for malformed input.");
            return EXIT_FAILURE;
        }

        std::string invalidUtf8;
        invalidUtf8.push_back(static_cast<char>(0xFF));
        auto invalidWrite = asharia::serialization::writeTextArchive(
            asharia::serialization::ArchiveValue::string(invalidUtf8));
        if (invalidWrite) {
            asharia::logError("JSON archive smoke accepted invalid UTF-8 output.");
            return EXIT_FAILURE;
        }

        auto duplicateObjectWrite =
            asharia::serialization::writeTextArchive(asharia::serialization::ArchiveValue::object({
                asharia::serialization::ArchiveMember{
                    .key = "field",
                    .value = asharia::serialization::ArchiveValue::integer(1),
                },
                asharia::serialization::ArchiveMember{
                    .key = "field",
                    .value = asharia::serialization::ArchiveValue::integer(2),
                },
            }));
        if (duplicateObjectWrite ||
            duplicateObjectWrite.error().message.find("duplicate key") == std::string::npos) {
            asharia::logError("JSON archive smoke did not reject duplicate ArchiveValue keys.");
            return EXIT_FAILURE;
        }

        auto nonFiniteWrite =
            asharia::serialization::writeTextArchive(asharia::serialization::ArchiveValue::floating(
                std::numeric_limits<double>::infinity()));
        if (nonFiniteWrite ||
            nonFiniteWrite.error().message.find("non-finite") == std::string::npos) {
            asharia::logError("JSON archive smoke did not reject a non-finite float.");
            return EXIT_FAILURE;
        }

        std::cout << "Serialization JSON archive bytes: " << firstText->size() << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeSerializationMigration() {
        auto registry = makeReflectionSmokeRegistry();
        if (!registry) {
            return EXIT_FAILURE;
        }

        const asharia::reflection::TypeId migratedTransformType =
            asharia::reflection::makeTypeId(kSmokeMigratedTransformTypeName);
        asharia::serialization::MigrationRegistry migrations;
        auto migrationRegistered =
            migrations.registerMigration(migratedTransformType, 1, 2, migrateSmokeTransformV1ToV2);
        if (!migrationRegistered) {
            asharia::logError(migrationRegistered.error().message);
            return EXIT_FAILURE;
        }

        auto duplicateMigration =
            migrations.registerMigration(migratedTransformType, 1, 2, migrateSmokeTransformV1ToV2);
        if (duplicateMigration ||
            !containsAll(duplicateMigration.error().message, {
                                                                 "operation=register",
                                                                 "fromVersion=1",
                                                                 "toVersion=2",
                                                                 "expected=unique migration step",
                                                                 "actual=duplicate",
                                                             })) {
            asharia::logError("Serialization migration smoke accepted a duplicate migration.");
            return EXIT_FAILURE;
        }

        const asharia::serialization::ArchiveValue oldArchive = makeMigratedTransformV1Archive();
        ReflectionSmokeMigratedTransform rejectedWithoutMigration{};
        auto missingMigrationResult = asharia::serialization::deserializeObject(
            *registry, migratedTransformType, oldArchive, &rejectedWithoutMigration);
        if (missingMigrationResult ||
            !containsAll(missingMigrationResult.error().message,
                         {
                             "requires migration",
                             "operation=deserialize",
                             "objectPath=com.asharia.smoke.MigratedTransform",
                             "type=com.asharia.smoke.MigratedTransform",
                             "expected=migration policy",
                             "actual=no migration registry",
                             "version=1",
                         })) {
            asharia::logError("Serialization migration smoke did not require a migration policy.");
            return EXIT_FAILURE;
        }

        const asharia::serialization::SerializationPolicy policy{
            .includeTypeHeader = true,
            .unknownFields = asharia::serialization::UnknownFieldPolicy::Error,
            .missingFields = asharia::serialization::MissingFieldPolicy::UseDefault,
            .migrations = &migrations,
            .archivePath = {},
            .migrationScenario = asharia::serialization::MigrationScenario::Unspecified,
        };
        ReflectionSmokeMigratedTransform loaded{};
        auto loadedResult = asharia::serialization::deserializeObject(
            *registry, migratedTransformType, oldArchive, &loaded, policy);
        if (!loadedResult) {
            asharia::logError(loadedResult.error().message);
            return EXIT_FAILURE;
        }

        if (loaded.position.x != 1.0F || loaded.position.y != 2.0F || loaded.position.z != 3.0F ||
            loaded.rotation.x != 0.0F || loaded.rotation.y != 0.0F || loaded.rotation.z != 0.25F ||
            loaded.rotation.w != 1.0F || loaded.scale.x != 1.0F || loaded.scale.y != 1.0F ||
            loaded.scale.z != 1.0F || loaded.debugName != "migrated") {
            asharia::logError("Serialization migration smoke loaded unexpected migrated values.");
            return EXIT_FAILURE;
        }

        asharia::serialization::MigrationRegistry emptyMigrations;
        const asharia::serialization::SerializationPolicy missingPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::serialization::UnknownFieldPolicy::Error,
            .missingFields = asharia::serialization::MissingFieldPolicy::UseDefault,
            .migrations = &emptyMigrations,
            .archivePath = {},
            .migrationScenario = asharia::serialization::MigrationScenario::Unspecified,
        };
        ReflectionSmokeMigratedTransform rejectedMissingRule{};
        auto missingRuleResult = asharia::serialization::deserializeObject(
            *registry, migratedTransformType, oldArchive, &rejectedMissingRule, missingPolicy);
        if (missingRuleResult || !containsAll(missingRuleResult.error().message,
                                              {
                                                  "Missing serialization migration",
                                                  "operation=migrate",
                                                  "objectPath=com.asharia.smoke.MigratedTransform",
                                                  "type=com.asharia.smoke.MigratedTransform",
                                                  "fromVersion=1",
                                                  "toVersion=2",
                                                  "expected=registered migration step",
                                                  "actual=missing",
                                              })) {
            asharia::logError(
                "Serialization migration smoke did not reject a missing migration rule.");
            return EXIT_FAILURE;
        }

        std::cout << "Serialization migration version: 1 -> 2\n";
        return EXIT_SUCCESS;
    }

    bool validateDescriptorAllocatorStats(asharia::VulkanDescriptorAllocatorStats stats,
                                          std::string_view context,
                                          std::uint64_t expectedSets = 1) {
        if (stats.poolsCreated != 1 || stats.allocationCalls != 1 ||
            stats.setsAllocated != expectedSets) {
            asharia::logError(std::string{context} +
                              " did not allocate descriptors through the descriptor allocator.");
            return false;
        }
        return true;
    }

    bool validateBufferUploadStats(asharia::VulkanBufferStats stats, std::uint64_t expectedBuffers,
                                   std::string_view context) {
        if (stats.created != expectedBuffers || stats.hostUploadCreated != expectedBuffers ||
            stats.uploadCalls != expectedBuffers || stats.allocatedBytes == 0 ||
            stats.uploadedBytes == 0 || stats.uploadedBytes > stats.allocatedBytes) {
            asharia::logError(std::string{context} +
                              " did not record the expected buffer upload counters.");
            return false;
        }
        return true;
    }

    bool validateDebugLabelStats(asharia::VulkanDebugLabelStats stats, std::string_view context) {
        if (!stats.available || stats.regionsBegun == 0 ||
            stats.regionsBegun != stats.regionsEnded) {
            asharia::logError(std::string{context} +
                              " did not record balanced Vulkan debug labels.");
            return false;
        }
        return true;
    }

    bool validateTimestampStats(asharia::VulkanTimestampQueryStats stats,
                                std::span<const asharia::VulkanTimestampRegionTiming> timings,
                                std::string_view context) {
        if (!stats.available || stats.framesBegun == 0 || stats.framesResolved == 0 ||
            stats.regionsBegun == 0 || stats.regionsBegun != stats.regionsEnded ||
            stats.regionsResolved == 0 || stats.queryReadbacks == 0 || timings.empty()) {
            asharia::logError(std::string{context} +
                              " did not record delayed Vulkan timestamp query results.");
            return false;
        }

        const bool hasFrameTiming =
            std::ranges::any_of(timings, [](const asharia::VulkanTimestampRegionTiming& timing) {
                return timing.name == "VulkanFrame" && timing.milliseconds >= 0.0;
            });
        if (!hasFrameTiming) {
            asharia::logError(std::string{context} +
                              " did not read back a VulkanFrame timestamp duration.");
            return false;
        }

        return true;
    }

    struct RenderGraphBenchOptions {
        std::size_t warmupFrames{60};
        std::size_t measuredFrames{600};
        std::filesystem::path outputPath{"build/perf/rendergraph.jsonl"};
    };

    struct RenderGraphBenchStats {
        double averageMilliseconds{};
        double p50Milliseconds{};
        double p95Milliseconds{};
        double maxMilliseconds{};
    };

    bool parseSizeOption(std::span<char*> args, std::string_view option, std::size_t& value) {
        const std::optional<std::string_view> text = argValue(args, option);
        if (!text) {
            return true;
        }

        std::size_t parsed{};
        const char* begin = text->data();
        const char* end = text->data() + text->size();
        const auto parsedResult = std::from_chars(begin, end, parsed);
        if (parsedResult.ec != std::errc{} || parsedResult.ptr != end || parsed == 0U) {
            asharia::logError("Invalid positive integer for " + std::string{option} + ".");
            return false;
        }

        value = parsed;
        return true;
    }

    std::optional<RenderGraphBenchOptions> parseRenderGraphBenchOptions(std::span<char*> args) {
        RenderGraphBenchOptions options;
        if (!parseSizeOption(args, "--warmup", options.warmupFrames) ||
            !parseSizeOption(args, "--frames", options.measuredFrames)) {
            return std::nullopt;
        }

        if (const std::optional<std::string_view> output = argValue(args, "--output")) {
            options.outputPath = std::filesystem::path{std::string{*output}};
        }

        return options;
    }

    asharia::RenderGraph createBenchRenderGraph() {
        asharia::RenderGraph graph;
        const auto backbuffer = graph.importImage(asharia::RenderGraphImageDesc{
            .name = "BenchBackbuffer",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        const auto sceneColor = graph.createTransientImage(asharia::RenderGraphImageDesc{
            .name = "BenchSceneColor",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 1280, .height = 720},
        });
        const auto depth = graph.createTransientImage(asharia::RenderGraphImageDesc{
            .name = "BenchDepth",
            .format = asharia::RenderGraphImageFormat::D32Sfloat,
            .extent = asharia::RenderGraphExtent2D{.width = 1280, .height = 720},
        });

        graph.addPass("BenchClearScene", asharia::kBasicDynamicClearPassType)
            .setParams(asharia::kBasicDynamicClearParamsType,
                       asharia::BasicTransferClearParams{.color = {0.02F, 0.08F, 0.10F, 1.0F}})
            .writeColor("target", sceneColor)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.02F, 0.08F, 0.10F, 1.0F});
            });
        graph.addPass("BenchDepthDraw", asharia::kBasicRasterDepthTrianglePassType)
            .setParamsType(asharia::kBasicRasterDepthTriangleParamsType)
            .writeColor("target", sceneColor)
            .writeDepth("depth", depth);
        graph.addPass("BenchComposite", asharia::kBasicRasterFullscreenPassType)
            .setParams(asharia::kBasicRasterFullscreenParamsType,
                       asharia::BasicFullscreenParams{.tint = {1.0F, 1.0F, 1.0F, 1.0F}})
            .readTexture("source", sceneColor, asharia::RenderGraphShaderStage::Fragment)
            .writeColor("target", backbuffer)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/BenchComposite", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setVec4("Tint", std::array{1.0F, 1.0F, 1.0F, 1.0F})
                    .drawFullscreenTriangle();
            });

        return graph;
    }

    [[nodiscard]] std::uint64_t
    renderGraphTransitionCount(const asharia::RenderGraphCompileResult& compiled) {
        auto count = static_cast<std::uint64_t>(compiled.finalTransitions.size());
        count += static_cast<std::uint64_t>(compiled.finalBufferTransitions.size());
        for (const asharia::RenderGraphCompiledPass& pass : compiled.passes) {
            count += static_cast<std::uint64_t>(pass.transitionsBefore.size());
            count += static_cast<std::uint64_t>(pass.bufferTransitionsBefore.size());
        }
        return count;
    }

    void addRenderGraphBenchCounters(asharia::FrameProfiler& profiler,
                                     const asharia::RenderGraphCompileResult& compiled) {
        profiler.addCounter("declaredPassCount",
                            static_cast<std::uint64_t>(compiled.declaredPassCount));
        profiler.addCounter("declaredImageCount",
                            static_cast<std::uint64_t>(compiled.declaredImageCount));
        profiler.addCounter("declaredBufferCount",
                            static_cast<std::uint64_t>(compiled.declaredBufferCount));
        profiler.addCounter("compiledPassCount",
                            static_cast<std::uint64_t>(compiled.passes.size()));
        profiler.addCounter("dependencyEdgeCount",
                            static_cast<std::uint64_t>(compiled.dependencies.size()));
        profiler.addCounter("transitionCount", renderGraphTransitionCount(compiled));
        profiler.addCounter("culledPassCount",
                            static_cast<std::uint64_t>(compiled.culledPasses.size()));
        profiler.addCounter("transientImageCount",
                            static_cast<std::uint64_t>(compiled.transientImages.size()));
        profiler.addCounter("transientBufferCount",
                            static_cast<std::uint64_t>(compiled.transientBuffers.size()));
    }

    [[nodiscard]] std::optional<double> cpuScopeMilliseconds(const asharia::FrameProfile& frame,
                                                             std::string_view scopeName) {
        for (const asharia::CpuScopeSample& scope : frame.cpuScopes) {
            if (scope.name == scopeName && scope.endNanoseconds >= scope.beginNanoseconds) {
                const std::uint64_t elapsed = scope.endNanoseconds - scope.beginNanoseconds;
                return static_cast<double>(elapsed) / 1'000'000.0;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderGraphBenchStats makeBenchStats(std::vector<double> values) {
        if (values.empty()) {
            return {};
        }

        const double average =
            std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        std::ranges::sort(values);
        const auto percentile = [&values](double value) {
            const auto index =
                static_cast<std::size_t>((static_cast<double>(values.size() - 1U) * value));
            return values[index];
        };

        return RenderGraphBenchStats{
            .averageMilliseconds = average,
            .p50Milliseconds = percentile(0.50),
            .p95Milliseconds = percentile(0.95),
            .maxMilliseconds = values.back(),
        };
    }

    void writeBenchSummary(std::ostream& output, const RenderGraphBenchOptions& options,
                           const RenderGraphBenchStats& recordStats,
                           const RenderGraphBenchStats& compileStats,
                           const RenderGraphBenchStats& totalStats,
                           const asharia::FrameProfile& lastFrame) {
        output << R"({"type":"summary","warmupFrames":)" << options.warmupFrames
               << R"(,"measuredFrames":)" << options.measuredFrames << R"(,"outputPath":)";
        asharia::writeJsonString(output, options.outputPath.generic_string());
        output << R"(,"recordGraph":{"averageMilliseconds":)" << recordStats.averageMilliseconds
               << R"(,"p50Milliseconds":)" << recordStats.p50Milliseconds
               << R"(,"p95Milliseconds":)" << recordStats.p95Milliseconds
               << R"(,"maxMilliseconds":)" << recordStats.maxMilliseconds << '}';
        output << R"(,"compileGraph":{"averageMilliseconds":)" << compileStats.averageMilliseconds
               << R"(,"p50Milliseconds":)" << compileStats.p50Milliseconds
               << R"(,"p95Milliseconds":)" << compileStats.p95Milliseconds
               << R"(,"maxMilliseconds":)" << compileStats.maxMilliseconds << '}';
        output << R"(,"total":{"averageMilliseconds":)" << totalStats.averageMilliseconds
               << R"(,"p50Milliseconds":)" << totalStats.p50Milliseconds << R"(,"p95Milliseconds":)"
               << totalStats.p95Milliseconds << R"(,"maxMilliseconds":)"
               << totalStats.maxMilliseconds << '}';
        output << R"(,"lastCounters":{)";
        for (std::size_t index = 0; index < lastFrame.counters.size(); ++index) {
            const asharia::CounterSample& counter = lastFrame.counters[index];
            if (index > 0) {
                output << ',';
            }
            asharia::writeJsonString(output, counter.name);
            output << ':' << counter.value;
        }
        output << "}}\n";
    }

    int runBenchRenderGraph(std::span<char*> args) {
        const std::optional<RenderGraphBenchOptions> parsedOptions =
            parseRenderGraphBenchOptions(args);
        if (!parsedOptions) {
            return EXIT_FAILURE;
        }

        const RenderGraphBenchOptions& options = *parsedOptions;
        const asharia::RenderGraphSchemaRegistry schemas =
            asharia::basicRenderGraphSchemaRegistry();
        for (std::size_t frame = 0; frame < options.warmupFrames; ++frame) {
            asharia::RenderGraph graph = createBenchRenderGraph();
            auto compiled = graph.compile(schemas);
            if (!compiled) {
                asharia::logError(compiled.error().message);
                return EXIT_FAILURE;
            }
        }

        asharia::FrameProfiler profiler{options.measuredFrames};
        std::vector<double> recordMilliseconds;
        std::vector<double> compileMilliseconds;
        std::vector<double> totalMilliseconds;
        recordMilliseconds.reserve(options.measuredFrames);
        compileMilliseconds.reserve(options.measuredFrames);
        totalMilliseconds.reserve(options.measuredFrames);

        for (std::size_t frame = 0; frame < options.measuredFrames; ++frame) {
            profiler.beginFrame(asharia::FrameProfileInfo{
                .frameIndex = static_cast<std::uint64_t>(frame),
                .target = asharia::ProfileTarget::Bench,
                .viewName = "RenderGraphBench",
            });

            asharia::RenderGraph graph;
            const asharia::CpuScopeHandle recordScope = profiler.beginCpuScope("RecordGraph");
            graph = createBenchRenderGraph();
            profiler.endCpuScope(recordScope);

            const asharia::CpuScopeHandle compileScope = profiler.beginCpuScope("CompileGraph");
            auto compiled = graph.compile(schemas);
            profiler.endCpuScope(compileScope);
            if (!compiled) {
                asharia::logError(compiled.error().message);
                return EXIT_FAILURE;
            }

            addRenderGraphBenchCounters(profiler, *compiled);
            profiler.endFrame();

            const asharia::FrameProfile& profile = profiler.lastFrame();
            const double recordMs = cpuScopeMilliseconds(profile, "RecordGraph").value_or(0.0);
            const double compileMs = cpuScopeMilliseconds(profile, "CompileGraph").value_or(0.0);
            recordMilliseconds.push_back(recordMs);
            compileMilliseconds.push_back(compileMs);
            totalMilliseconds.push_back(recordMs + compileMs);
        }

        const std::filesystem::path parentPath = options.outputPath.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath);
        }

        std::ofstream output{options.outputPath};
        if (!output) {
            asharia::logError("Failed to open render graph benchmark output file.");
            return EXIT_FAILURE;
        }

        const RenderGraphBenchStats recordStats = makeBenchStats(recordMilliseconds);
        const RenderGraphBenchStats compileStats = makeBenchStats(compileMilliseconds);
        const RenderGraphBenchStats totalStats = makeBenchStats(totalMilliseconds);
        writeBenchSummary(output, options, recordStats, compileStats, totalStats,
                          profiler.lastFrame());
        asharia::writeFrameProfileJsonl(output, profiler.frames());

        std::cout << "RenderGraph bench frames: " << options.measuredFrames
                  << ", record avg ms: " << recordStats.averageMilliseconds
                  << ", compile avg ms: " << compileStats.averageMilliseconds
                  << ", total p95 ms: " << totalStats.p95Milliseconds
                  << ", output: " << options.outputPath.generic_string() << '\n';

        return EXIT_SUCCESS;
    }

    int runSmokeDeferredDeletion() {
        asharia::VulkanDeferredDeletionQueue queue;
        std::vector<int> retired;

        if (queue.enqueue(0, {})) {
            asharia::logError("Deferred deletion queue accepted an empty callback.");
            return EXIT_FAILURE;
        }

        const bool enqueued = queue.enqueue(2, [&retired]() { retired.push_back(2); }) &&
                              queue.enqueue(1, [&retired]() { retired.push_back(1); }) &&
                              queue.enqueue(3, [&retired]() { retired.push_back(3); });
        if (!enqueued) {
            asharia::logError("Deferred deletion queue rejected a valid callback.");
            return EXIT_FAILURE;
        }

        asharia::VulkanDeferredDeletionStats stats = queue.stats();
        if (stats.pending != 3 || stats.enqueued != 3 || stats.retired != 0 || stats.flushed != 0 ||
            queue.pendingCount() != 3 || queue.empty()) {
            asharia::logError("Deferred deletion queue reported unexpected initial counters.");
            return EXIT_FAILURE;
        }

        if (queue.retireCompleted(1) != 1 || retired != std::vector<int>{1}) {
            asharia::logError("Deferred deletion queue retired the wrong epoch 1 callbacks.");
            return EXIT_FAILURE;
        }

        if (!queue.enqueue(2, [&retired]() { retired.push_back(22); })) {
            asharia::logError("Deferred deletion queue rejected a valid late callback.");
            return EXIT_FAILURE;
        }

        if (queue.retireCompleted(2) != 2 || retired != std::vector<int>{1, 2, 22}) {
            asharia::logError("Deferred deletion queue retired the wrong epoch 2 callbacks.");
            return EXIT_FAILURE;
        }

        stats = queue.stats();
        if (stats.pending != 1 || stats.enqueued != 4 || stats.retired != 3 || stats.flushed != 0) {
            asharia::logError("Deferred deletion queue reported unexpected post-retire counters.");
            return EXIT_FAILURE;
        }

        if (queue.flush() != 1 || retired != std::vector<int>{1, 2, 22, 3}) {
            asharia::logError("Deferred deletion queue flush retired the wrong callbacks.");
            return EXIT_FAILURE;
        }

        stats = queue.stats();
        if (stats.pending != 0 || stats.enqueued != 4 || stats.retired != 4 || stats.flushed != 1 ||
            !queue.empty()) {
            asharia::logError("Deferred deletion queue reported unexpected final counters.");
            return EXIT_FAILURE;
        }

        std::cout << "Deferred deletion queue enqueued: " << stats.enqueued
                  << ", retired: " << stats.retired << ", flushed: " << stats.flushed << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeWindow() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        asharia::logInfo("GLFW smoke window created.");
        asharia::GlfwWindow::pollEvents();
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeVulkan() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Vulkan Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc desc{
            .applicationName = "Asharia Engine Vulkan Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .enableValidation = false,
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(desc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        const auto& info = context->deviceInfo();
        std::cout << "Vulkan device: " << info.name << " API "
                  << asharia::vulkanVersionString(info.apiVersion) << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeFrame(const asharia::VulkanFrameRecordCallback& record, std::string_view title,
                      VkClearColorValue clearColor) {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            asharia::GlfwWindow::create(*glfw, asharia::WindowDesc{.title = std::string{title}});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = std::string{title},
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop =
            asharia::VulkanFrameLoop::create(*context, asharia::VulkanFrameLoopDesc{
                                                           .width = framebuffer.width,
                                                           .height = framebuffer.height,
                                                           .clearColor = clearColor,
                                                       });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(record);
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError("Swapchain remained out of date during frame smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), title)) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), title)) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeFrame() {
        return runSmokeFrame(asharia::recordBasicClearFrame, "Asharia Engine Frame Smoke",
                             VkClearColorValue{{0.02F, 0.12F, 0.18F, 1.0F}});
    }

    int runSmokeDynamicRendering() {
        return runSmokeFrame(asharia::recordBasicDynamicClearFrame,
                             "Asharia Engine Dynamic Rendering Smoke",
                             VkClearColorValue{{0.18F, 0.06F, 0.14F, 1.0F}});
    }

    int runSmokeTransient() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Transient Image Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Transient Image Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::BasicTransientFrameRecorder recorder{context->device(), context->allocator()};

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.08F, 0.14F, 0.22F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanFrameRecordCallback record =
            [&recorder](const asharia::VulkanFrameRecordContext& frame) {
                return recorder.record(frame);
            };

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(record);
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError("Swapchain remained out of date during transient smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const asharia::VulkanDeferredDeletionStats deletionStats =
            frameLoop->deferredDeletionStats();
        if (deletionStats.enqueued == 0 || deletionStats.retired == 0) {
            asharia::logError(
                "Transient smoke did not retire deferred Vulkan resource destruction.");
            return EXIT_FAILURE;
        }
        const asharia::VulkanTransientImagePoolStats transientPoolStats =
            recorder.transientPoolStats();
        if (transientPoolStats.created == 0 || transientPoolStats.reused == 0 ||
            transientPoolStats.retired == 0) {
            asharia::logError("Transient smoke did not reuse a retired transient Vulkan image.");
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Transient smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "Transient smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered transient frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeResize() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Resize Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Resize Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.06F, 0.10F, 0.18F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        auto firstFrame = frameLoop->renderFrame(asharia::recordBasicDynamicClearFrame);
        if (!firstFrame) {
            asharia::logError(firstFrame.error().message);
            return EXIT_FAILURE;
        }
        if (*firstFrame == asharia::VulkanFrameStatus::OutOfDate) {
            asharia::logError("Initial resize smoke frame was unexpectedly out of date.");
            return EXIT_FAILURE;
        }

        frameLoop->setTargetExtent(0, 0);
        auto zeroExtent = frameLoop->recreate();
        if (!zeroExtent) {
            asharia::logError(zeroExtent.error().message);
            return EXIT_FAILURE;
        }
        if (*zeroExtent != asharia::VulkanFrameStatus::OutOfDate) {
            asharia::logError("Zero-sized resize smoke did not report OutOfDate.");
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto restoredFramebuffer = window->framebufferExtent();
        frameLoop->setTargetExtent(restoredFramebuffer.width, restoredFramebuffer.height);
        auto recreated = frameLoop->recreate();
        if (!recreated) {
            asharia::logError(recreated.error().message);
            return EXIT_FAILURE;
        }
        if (*recreated != asharia::VulkanFrameStatus::Recreated) {
            asharia::logError("Resize smoke did not recreate the swapchain after extent restore.");
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(asharia::recordBasicDynamicClearFrame);
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError("Swapchain remained out of date during resize smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Resize smoke frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeTriangle(bool useDepth = false,
                         asharia::BasicMeshKind meshKind = asharia::BasicMeshKind::Triangle) {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        const std::string_view title = triangleSmokeTitle(useDepth, meshKind);
        auto window =
            asharia::GlfwWindow::create(*glfw, asharia::WindowDesc{.title = std::string{title}});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = std::string{title},
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto triangleRenderer =
            asharia::BasicTriangleRenderer::create(asharia::BasicTriangleRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
                .meshKind = meshKind,
            });
        if (!triangleRenderer) {
            asharia::logError(triangleRenderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status =
                frameLoop->renderFrame([&triangleRenderer, useDepth](
                                           const asharia::VulkanFrameRecordContext& recordContext) {
                    if (useDepth) {
                        return triangleRenderer->recordFrameWithDepth(recordContext);
                    }
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError(std::string{triangleSmokeOutOfDateMessage(useDepth, meshKind)});
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(triangleRenderer->pipelineCacheStats(), title)) {
            return EXIT_FAILURE;
        }
        const std::uint64_t expectedBuffers =
            meshKind == asharia::BasicMeshKind::IndexedQuad ? 2ULL : 1ULL;
        if (!validateBufferUploadStats(triangleRenderer->bufferStats(), expectedBuffers, title)) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), title)) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), title)) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << triangleSmokeRenderedPrefix(useDepth, meshKind) << extent.width << 'x'
                  << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError(
                "Failed to wait for Vulkan queue before triangle pipeline teardown: " +
                asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeDescriptorLayout() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Descriptor Layout Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Descriptor Layout Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto validated =
            asharia::validateBasicDescriptorLayoutSmoke(asharia::BasicDescriptorLayoutSmokeDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!validated) {
            asharia::logError(validated.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Descriptor layout smoke: set 0 bindings 0-2 buffer/image/sampler allocated\n";
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeMesh3D() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Mesh 3D Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Mesh 3D Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto meshRenderer = asharia::BasicMesh3DRenderer::create(asharia::BasicMesh3DRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
        });
        if (!meshRenderer) {
            asharia::logError(meshRenderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&meshRenderer](const asharia::VulkanFrameRecordContext& recordContext) {
                    return meshRenderer->recordFrame(recordContext);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError("Swapchain remained out of date during mesh 3D smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(meshRenderer->pipelineCacheStats(), "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(meshRenderer->bufferStats(), 2, "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered mesh 3D frames: " << extent.width << 'x' << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before mesh 3D teardown: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeDrawList() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Draw List Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Draw List Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.010F, 0.012F, 0.018F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        constexpr auto drawItems = asharia::basicDrawListSmokeItems();
        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = asharia::BasicDrawListRenderer::create(asharia::BasicDrawListRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
            .drawItems = drawItems,
        });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&renderer](const asharia::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordFrame(recordContext);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError("Swapchain remained out of date during draw list smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(), "Draw list smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 2, "Draw list smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Draw list smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "Draw list smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered draw list frames: " << extent.width << 'x' << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before draw list teardown: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeFullscreenTexture() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Fullscreen Texture Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Fullscreen Texture Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = asharia::BasicFullscreenTextureRenderer::create(
            asharia::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&renderer](const asharia::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordFrame(recordContext);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError(
                    "Swapchain remained out of date during fullscreen texture smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(),
                                        "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDescriptorAllocatorStats(renderer->descriptorAllocatorStats(),
                                              "Fullscreen texture smoke", 2)) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 1, "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(),
                                    "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered fullscreen texture frames: " << extent.width << 'x' << extent.height
                  << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError(
                "Failed to wait for Vulkan queue before fullscreen texture teardown: " +
                asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeOffscreenViewport() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Offscreen Viewport Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Offscreen Viewport Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = asharia::BasicFullscreenTextureRenderer::create(
            asharia::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        VkExtent2D lastViewportExtent{};
        for (int frame = 0; frame < 4; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
            VkExtent2D viewportExtent{
                .width = currentFramebuffer.width,
                .height = currentFramebuffer.height,
            };
            if (frame >= 2) {
                viewportExtent.width = std::max(1U, currentFramebuffer.width / 2U);
                viewportExtent.height = std::max(1U, currentFramebuffer.height / 2U);
            }
            lastViewportExtent = viewportExtent;

            auto status =
                frameLoop->renderFrame([&renderer, viewportExtent](
                                           const asharia::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordOffscreenViewportFrame(recordContext, viewportExtent);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError(
                    "Swapchain remained out of date during offscreen viewport smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(),
                                        "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateOffscreenViewportStats(renderer->offscreenViewportStats(),
                                            "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateOffscreenViewportTarget(renderer->offscreenViewportTarget(),
                                             frameLoop->format(), lastViewportExtent,
                                             "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        const asharia::VulkanDeferredDeletionStats deletionStats =
            frameLoop->deferredDeletionStats();
        if (deletionStats.enqueued < 2 || deletionStats.retired < 2) {
            asharia::logError(
                "Offscreen viewport smoke did not retire resized viewport resources.");
            return EXIT_FAILURE;
        }
        if (!validateDescriptorAllocatorStats(renderer->descriptorAllocatorStats(),
                                              "Offscreen viewport smoke", 2)) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 1, "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(),
                                    "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D swapchainExtent = frameLoop->extent();
        std::cout << "Rendered offscreen viewport frames: " << lastViewportExtent.width << 'x'
                  << lastViewportExtent.height << " inside swapchain " << swapchainExtent.width
                  << 'x' << swapchainExtent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError(
                "Failed to wait for Vulkan queue before offscreen viewport teardown: " +
                asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runInteractiveViewer() {
        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Sample Viewer"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Sample Viewer",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = asharia::VulkanContext::create(contextDesc);
        if (!context) {
            asharia::logError(context.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        auto framebuffer = window->framebufferExtent();
        while (!window->shouldClose() && !isRenderableExtent(framebuffer)) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            asharia::GlfwWindow::pollEvents();
            framebuffer = window->framebufferExtent();
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto triangleRenderer =
            asharia::BasicTriangleRenderer::create(asharia::BasicTriangleRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!triangleRenderer) {
            asharia::logError(triangleRenderer.error().message);
            return EXIT_FAILURE;
        }

        while (!window->shouldClose()) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
            if (!isRenderableExtent(currentFramebuffer)) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                continue;
            }

            if (!extentMatches(frameLoop->extent(), currentFramebuffer)) {
                auto recreated = frameLoop->recreate();
                if (!recreated) {
                    asharia::logError(recreated.error().message);
                    return EXIT_FAILURE;
                }
                if (*recreated == asharia::VulkanFrameStatus::OutOfDate) {
                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(16ms);
                    continue;
                }
            }

            auto status = frameLoop->renderFrame(
                [&triangleRenderer](const asharia::VulkanFrameRecordContext& recordContext) {
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1ms);
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before viewer shutdown: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    bool validateSmokeRenderGraphVulkanMappings(const asharia::RenderGraphCompileResult& compiled) {
        const auto vulkanFinalTransition =
            asharia::vulkanImageTransition(compiled.finalTransitions.front());
        if (vulkanFinalTransition.oldLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanFinalTransition.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            asharia::logError(
                "Render graph Vulkan transition mapping produced unexpected layouts.");
            return false;
        }

        const VkImageMemoryBarrier2 barrier =
            asharia::vulkanImageBarrier(vulkanFinalTransition, VK_NULL_HANDLE);
        if (barrier.oldLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            barrier.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
            barrier.srcStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            barrier.srcAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            asharia::logError("Render graph Vulkan barrier mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanShaderReadTransition =
            asharia::vulkanImageTransition(compiled.passes[1].transitionsBefore.front());
        if (vulkanShaderReadTransition.oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
            vulkanShaderReadTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanShaderReadTransition.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            vulkanShaderReadTransition.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
            vulkanShaderReadTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanShaderReadTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            asharia::logError("Render graph Vulkan shader-read mapping produced unexpected masks.");
            return false;
        }

        const VkPipelineStageFlags2 depthTestsStages =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        const auto vulkanDepthWriteTransition =
            asharia::vulkanImageTransition(compiled.passes[2].transitionsBefore.front());
        if (vulkanDepthWriteTransition.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
            vulkanDepthWriteTransition.newLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
            vulkanDepthWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanDepthWriteTransition.srcAccessMask != 0 ||
            vulkanDepthWriteTransition.dstStageMask != depthTestsStages ||
            vulkanDepthWriteTransition.dstAccessMask !=
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
            asharia::logError("Render graph Vulkan depth-write mapping produced unexpected masks.");
            return false;
        }

        const VkImageMemoryBarrier2 depthBarrier = asharia::vulkanImageBarrier(
            vulkanDepthWriteTransition, VK_NULL_HANDLE, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (depthBarrier.subresourceRange.aspectMask != VK_IMAGE_ASPECT_DEPTH_BIT) {
            asharia::logError("Render graph Vulkan depth barrier used an unexpected aspect mask.");
            return false;
        }

        const auto vulkanDepthSampledTransition =
            asharia::vulkanImageTransition(compiled.passes[3].transitionsBefore.front());
        if (vulkanDepthSampledTransition.oldLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
            vulkanDepthSampledTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanDepthSampledTransition.srcStageMask != depthTestsStages ||
            vulkanDepthSampledTransition.srcAccessMask !=
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ||
            vulkanDepthSampledTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanDepthSampledTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            asharia::logError(
                "Render graph Vulkan depth sampled-read mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanTransientWriteTransition =
            asharia::vulkanImageTransition(compiled.passes[4].transitionsBefore.front());
        if (vulkanTransientWriteTransition.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
            vulkanTransientWriteTransition.newLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
            vulkanTransientWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanTransientWriteTransition.srcAccessMask != 0 ||
            vulkanTransientWriteTransition.dstStageMask !=
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT ||
            vulkanTransientWriteTransition.dstAccessMask !=
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
            asharia::logError(
                "Render graph Vulkan transient write mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanTransientSampleTransition =
            asharia::vulkanImageTransition(compiled.passes[5].transitionsBefore.front());
        if (vulkanTransientSampleTransition.oldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
            vulkanTransientSampleTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanTransientSampleTransition.srcStageMask !=
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT ||
            vulkanTransientSampleTransition.srcAccessMask !=
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT ||
            vulkanTransientSampleTransition.dstStageMask !=
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanTransientSampleTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            asharia::logError(
                "Render graph Vulkan transient sampled-read mapping produced unexpected masks.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphTransientPlan(const asharia::RenderGraphCompileResult& compiled) {
        if (compiled.transientImages.size() != 1) {
            asharia::logError(
                "Render graph did not produce the expected transient allocation plan.");
            return false;
        }

        const asharia::RenderGraphTransientImageAllocation& transient =
            compiled.transientImages.front();
        if (transient.image.index != 2 || transient.imageName != "TransientColor" ||
            transient.format != asharia::RenderGraphImageFormat::B8G8R8A8Srgb ||
            transient.extent.width != 640 || transient.extent.height != 360 ||
            transient.firstPassIndex != 4 || transient.lastPassIndex != 5 ||
            transient.finalState != asharia::RenderGraphImageState::ShaderRead ||
            transient.finalShaderStage != asharia::RenderGraphShaderStage::Fragment) {
            asharia::logError(
                "Render graph transient allocation plan contained unexpected fields.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphCommands(const asharia::RenderGraphCompileResult& compiled) {
        if (compiled.passes.size() != 6) {
            asharia::logError("Render graph command smoke received an unexpected pass count.");
            return false;
        }

        const auto& clearCommands = compiled.passes[0].commands;
        if (clearCommands.size() != 1 ||
            clearCommands.front().kind != asharia::RenderGraphCommandKind::ClearColor ||
            clearCommands.front().name != "target") {
            asharia::logError("Render graph clear command summary contained unexpected fields.");
            return false;
        }

        const auto& sampleCommands = compiled.passes[1].commands;
        if (sampleCommands.size() != 4 ||
            sampleCommands[0].kind != asharia::RenderGraphCommandKind::SetShader ||
            sampleCommands[0].name != "Hidden/SmokeSample" ||
            sampleCommands[1].kind != asharia::RenderGraphCommandKind::SetTexture ||
            sampleCommands[1].secondaryName != "source" ||
            sampleCommands[2].kind != asharia::RenderGraphCommandKind::SetFloat ||
            sampleCommands[3].kind != asharia::RenderGraphCommandKind::DrawFullscreenTriangle) {
            asharia::logError("Render graph sample command summary contained unexpected fields.");
            return false;
        }

        const auto& transientCommands = compiled.passes[5].commands;
        if (transientCommands.size() != 4 ||
            transientCommands[0].kind != asharia::RenderGraphCommandKind::SetShader ||
            transientCommands[1].kind != asharia::RenderGraphCommandKind::SetTexture ||
            transientCommands[2].kind != asharia::RenderGraphCommandKind::SetVec4 ||
            transientCommands[3].kind != asharia::RenderGraphCommandKind::DrawFullscreenTriangle) {
            asharia::logError(
                "Render graph transient command summary contained unexpected fields.");
            return false;
        }

        return true;
    }

    bool hasNoDepthSlots(asharia::RenderGraphPassContext context) {
        return context.depthReads.empty() && context.depthWrites.empty() &&
               context.depthSampledReads.empty() && context.depthReadSlots.empty() &&
               context.depthWriteSlots.empty() && context.depthSampledReadSlots.empty();
    }

    asharia::Result<void> validateClearTransferContext(asharia::RenderGraphPassContext context) {
        if (context.name != "ClearColor" || context.type != "basic.clear-transfer" ||
            context.paramsType != "basic.clear-transfer.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !hasNoDepthSlots(context) ||
            context.transferWrites.size() != 1 || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || context.transferWriteSlots.size() != 1 ||
            context.transferWriteSlots.front().name != "target") {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Render graph executor received unexpected pass context.",
            }};
        }

        return {};
    }

    asharia::Result<void> validateSampleFragmentContext(asharia::RenderGraphPassContext context) {
        if (context.name != "SampleColor" || context.type != "basic.sample-fragment" ||
            context.paramsType != "basic.sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            context.shaderReads.size() != 1 || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            context.shaderReadSlots.size() != 1 || !context.transferWriteSlots.empty() ||
            context.shaderReadSlots.front().name != "source" ||
            context.shaderReadSlots.front().shaderStage !=
                asharia::RenderGraphShaderStage::Fragment) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Render graph shader-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    asharia::Result<void> validateDepthWriteContext(asharia::RenderGraphPassContext context) {
        if (context.name != "WriteDepth" || context.type != "basic.depth-write" ||
            context.paramsType != "basic.depth-write.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !context.depthReads.empty() ||
            context.depthWrites.size() != 1 || !context.depthSampledReads.empty() ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || !context.depthReadSlots.empty() ||
            context.depthWriteSlots.size() != 1 || !context.depthSampledReadSlots.empty() ||
            !context.transferWriteSlots.empty() ||
            context.depthWriteSlots.front().name != "depth") {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Render graph depth-write executor received unexpected pass context.",
            }};
        }

        return {};
    }

    asharia::Result<void> validateDepthSampleContext(asharia::RenderGraphPassContext context) {
        if (context.name != "SampleDepth" || context.type != "basic.depth-sample-fragment" ||
            context.paramsType != "basic.depth-sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !context.depthReads.empty() ||
            !context.depthWrites.empty() || context.depthSampledReads.size() != 1 ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || !context.depthReadSlots.empty() ||
            !context.depthWriteSlots.empty() || context.depthSampledReadSlots.size() != 1 ||
            !context.transferWriteSlots.empty() ||
            context.depthSampledReadSlots.front().name != "depth" ||
            context.depthSampledReadSlots.front().shaderStage !=
                asharia::RenderGraphShaderStage::Fragment) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Render graph depth sampled-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    asharia::Result<void> validateTransientWriteContext(asharia::RenderGraphPassContext context) {
        if (context.name != "WriteTransientColor" || context.type != "basic.transient-color" ||
            context.paramsType != "basic.transient-color.params" ||
            context.transitionsBefore.size() != 1 || context.colorWrites.size() != 1 ||
            !context.shaderReads.empty() || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || context.colorWriteSlots.size() != 1 ||
            !context.shaderReadSlots.empty() || !context.transferWriteSlots.empty() ||
            context.colorWriteSlots.front().name != "target") {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Render graph transient write executor received unexpected pass context.",
            }};
        }

        return {};
    }

    asharia::Result<void> validateTransientSampleContext(asharia::RenderGraphPassContext context) {
        if (context.name != "SampleTransientColor" ||
            context.type != "basic.transient-sample-fragment" ||
            context.paramsType != "basic.transient-sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            context.shaderReads.size() != 1 || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            context.shaderReadSlots.size() != 1 || !context.transferWriteSlots.empty() ||
            context.shaderReadSlots.front().name != "source" ||
            context.shaderReadSlots.front().shaderStage !=
                asharia::RenderGraphShaderStage::Fragment) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Render graph transient sampled-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    void registerSmokeRenderGraphExecutors(asharia::RenderGraphExecutorRegistry& executors,
                                           int& callbackCount) {
        executors.registerExecutor(
            "basic.clear-transfer",
            [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                auto validated = validateClearTransferContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.sample-fragment",
            [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                auto validated = validateSampleFragmentContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.depth-write",
            [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                auto validated = validateDepthWriteContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.depth-sample-fragment",
            [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                auto validated = validateDepthSampleContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.transient-color",
            [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                auto validated = validateTransientWriteContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.transient-sample-fragment",
            [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                auto validated = validateTransientSampleContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
    }

    struct ExpectedRenderGraphCompileFailure {
        std::string_view message;
        std::string_view context;
    };

    bool expectRenderGraphCompileFailure(
        const asharia::Result<asharia::RenderGraphCompileResult>& compiled,
        ExpectedRenderGraphCompileFailure expected) {
        if (compiled) {
            asharia::logError("Render graph accepted invalid graph: " +
                              std::string{expected.context});
            return false;
        }
        if (compiled.error().message.find(expected.message) == std::string::npos) {
            asharia::logError("Render graph produced an unexpected error for " +
                              std::string{expected.context} + ": " + compiled.error().message);
            return false;
        }

        return true;
    }

    enum class BuiltinSchemaSmokePass : std::uint8_t {
        TransferClear,
        DynamicClear,
        TransientPresent,
        RasterTriangle,
        RasterDepthTriangle,
        RasterMesh3D,
        RasterFullscreen,
        RasterDrawList,
    };

    struct BuiltinSchemaSmokeCase {
        BuiltinSchemaSmokePass pass;
        std::string_view type;
        std::string_view paramsType;
        std::string_view missingSlot;
        std::string_view context;
    };

    struct BuiltinSchemaSmokeImages {
        asharia::RenderGraphImageHandle colorTarget{};
        asharia::RenderGraphImageHandle colorSource{};
        asharia::RenderGraphImageHandle depthTarget{};
        asharia::RenderGraphImageHandle unexpectedTarget{};
    };

    struct BuiltinSchemaSmokeCompileOptions {
        std::string_view paramsType;
        std::string_view omittedSlot;
        bool addUnexpectedSlot{};
    };

    BuiltinSchemaSmokeImages createBuiltinSchemaSmokeImages(asharia::RenderGraph& graph) {
        return BuiltinSchemaSmokeImages{
            .colorTarget = graph.importImage(asharia::RenderGraphImageDesc{
                .name = "BuiltinSchemaColorTarget",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::Present,
            }),
            .colorSource = graph.importImage(asharia::RenderGraphImageDesc{
                .name = "BuiltinSchemaColorSource",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::ShaderRead,
                .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
                .finalState = asharia::RenderGraphImageState::ShaderRead,
                .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
            }),
            .depthTarget = graph.importImage(asharia::RenderGraphImageDesc{
                .name = "BuiltinSchemaDepthTarget",
                .format = asharia::RenderGraphImageFormat::D32Sfloat,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::DepthAttachmentWrite,
            }),
            .unexpectedTarget = graph.importImage(asharia::RenderGraphImageDesc{
                .name = "BuiltinSchemaUnexpectedTarget",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::Present,
            }),
        };
    }

    void addBuiltinSchemaSmokeSlots(BuiltinSchemaSmokePass passKind,
                                    asharia::RenderGraph::PassBuilder& pass,
                                    BuiltinSchemaSmokeImages images,
                                    std::string_view omittedSlot = {}) {
        switch (passKind) {
        case BuiltinSchemaSmokePass::TransferClear:
            if (omittedSlot != "target") {
                pass.writeTransfer("target", images.colorTarget);
            }
            break;
        case BuiltinSchemaSmokePass::DynamicClear:
        case BuiltinSchemaSmokePass::RasterTriangle:
            if (omittedSlot != "target") {
                pass.writeColor("target", images.colorTarget);
            }
            break;
        case BuiltinSchemaSmokePass::TransientPresent:
            if (omittedSlot != "source") {
                pass.readTexture("source", images.colorSource,
                                 asharia::RenderGraphShaderStage::Fragment);
            }
            if (omittedSlot != "target") {
                pass.writeTransfer("target", images.colorTarget);
            }
            break;
        case BuiltinSchemaSmokePass::RasterDepthTriangle:
        case BuiltinSchemaSmokePass::RasterMesh3D:
        case BuiltinSchemaSmokePass::RasterDrawList:
            if (omittedSlot != "target") {
                pass.writeColor("target", images.colorTarget);
            }
            if (omittedSlot != "depth") {
                pass.writeDepth("depth", images.depthTarget);
            }
            break;
        case BuiltinSchemaSmokePass::RasterFullscreen:
            if (omittedSlot != "source") {
                pass.readTexture("source", images.colorSource,
                                 asharia::RenderGraphShaderStage::Fragment);
            }
            if (omittedSlot != "target") {
                pass.writeColor("target", images.colorTarget);
            }
            break;
        }
    }

    asharia::Result<asharia::RenderGraphCompileResult>
    compileBuiltinSchemaSmokePass(const BuiltinSchemaSmokeCase& testCase,
                                  const asharia::RenderGraphSchemaRegistry& schemas,
                                  BuiltinSchemaSmokeCompileOptions options) {
        asharia::RenderGraph graph;
        const BuiltinSchemaSmokeImages images = createBuiltinSchemaSmokeImages(graph);
        auto pass = graph.addPass(std::string{testCase.context}, std::string{testCase.type});
        pass.setParamsType(std::string{options.paramsType});
        addBuiltinSchemaSmokeSlots(testCase.pass, pass, images, options.omittedSlot);
        if (options.addUnexpectedSlot) {
            pass.writeTransfer("unexpected", images.unexpectedTarget);
        }

        return graph.compile(schemas);
    }

    bool validateBuiltinSchemaSmokeCase(const asharia::RenderGraphSchemaRegistry& builtinSchemas,
                                        const BuiltinSchemaSmokeCase& testCase) {
        if (!expectRenderGraphCompileFailure(
                compileBuiltinSchemaSmokePass(testCase, builtinSchemas,
                                              BuiltinSchemaSmokeCompileOptions{
                                                  .paramsType = testCase.paramsType,
                                                  .omittedSlot = testCase.missingSlot,
                                                  .addUnexpectedSlot = false,
                                              }),
                ExpectedRenderGraphCompileFailure{
                    .message = "is missing required slot",
                    .context = testCase.context,
                })) {
            return false;
        }
        if (!expectRenderGraphCompileFailure(
                compileBuiltinSchemaSmokePass(testCase, builtinSchemas,
                                              BuiltinSchemaSmokeCompileOptions{
                                                  .paramsType = testCase.paramsType,
                                                  .omittedSlot = {},
                                                  .addUnexpectedSlot = true,
                                              }),
                ExpectedRenderGraphCompileFailure{
                    .message = "that is not allowed by schema",
                    .context = testCase.context,
                })) {
            return false;
        }
        if (!expectRenderGraphCompileFailure(
                compileBuiltinSchemaSmokePass(testCase, builtinSchemas,
                                              BuiltinSchemaSmokeCompileOptions{
                                                  .paramsType = "builtin.invalid-params",
                                                  .omittedSlot = {},
                                                  .addUnexpectedSlot = false,
                                              }),
                ExpectedRenderGraphCompileFailure{
                    .message = "expected params type",
                    .context = testCase.context,
                })) {
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphBuiltinSchemaFailures() {
        const asharia::RenderGraphSchemaRegistry builtinSchemas =
            asharia::basicRenderGraphSchemaRegistry();
        const std::array builtinCases{
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::TransferClear,
                .type = asharia::kBasicTransferClearPassType,
                .paramsType = asharia::kBasicTransferClearParamsType,
                .missingSlot = "target",
                .context = "builtin transfer clear",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::DynamicClear,
                .type = asharia::kBasicDynamicClearPassType,
                .paramsType = asharia::kBasicDynamicClearParamsType,
                .missingSlot = "target",
                .context = "builtin dynamic clear",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::TransientPresent,
                .type = asharia::kBasicTransientPresentPassType,
                .paramsType = asharia::kBasicTransientPresentParamsType,
                .missingSlot = "source",
                .context = "builtin transient present",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterTriangle,
                .type = asharia::kBasicRasterTrianglePassType,
                .paramsType = asharia::kBasicRasterTriangleParamsType,
                .missingSlot = "target",
                .context = "builtin raster triangle",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterDepthTriangle,
                .type = asharia::kBasicRasterDepthTrianglePassType,
                .paramsType = asharia::kBasicRasterDepthTriangleParamsType,
                .missingSlot = "depth",
                .context = "builtin raster depth triangle",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterMesh3D,
                .type = asharia::kBasicRasterMesh3DPassType,
                .paramsType = asharia::kBasicRasterMesh3DParamsType,
                .missingSlot = "depth",
                .context = "builtin raster mesh3D",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterFullscreen,
                .type = asharia::kBasicRasterFullscreenPassType,
                .paramsType = asharia::kBasicRasterFullscreenParamsType,
                .missingSlot = "source",
                .context = "builtin raster fullscreen",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterDrawList,
                .type = asharia::kBasicRasterDrawListPassType,
                .paramsType = asharia::kBasicRasterDrawListParamsType,
                .missingSlot = "depth",
                .context = "builtin raster draw list",
            },
        };

        return std::ranges::all_of(
            builtinCases, [&builtinSchemas](const BuiltinSchemaSmokeCase& testCase) {
                return validateBuiltinSchemaSmokeCase(builtinSchemas, testCase);
            });
    }

    bool
    validateSmokeRenderGraphNegativeCompiles(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph missingProducerGraph;
        const auto orphanColor =
            missingProducerGraph.createTransientImage(asharia::RenderGraphImageDesc{
                .name = "OrphanTransientColor",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
            });
        int missingProducerCallbackCount = 0;
        missingProducerGraph.addPass("SampleOrphanTransient", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", orphanColor, asharia::RenderGraphShaderStage::Fragment)
            .execute([&missingProducerCallbackCount](asharia::RenderGraphPassContext) {
                ++missingProducerCallbackCount;
                return asharia::Result<void>{};
            });

        auto missingProducerCompiled = missingProducerGraph.compile(schemas);
        if (missingProducerCompiled) {
            asharia::logError("Render graph accepted a transient read without a producer.");
            return false;
        }
        if (missingProducerCompiled.error().message.find("before any pass writes it") ==
            std::string::npos) {
            asharia::logError("Render graph produced an unexpected missing-producer error: " +
                              missingProducerCompiled.error().message);
            return false;
        }

        auto missingProducerExecuted = missingProducerGraph.execute();
        if (missingProducerExecuted) {
            asharia::logError("Render graph executed a transient read without a producer.");
            return false;
        }
        if (missingProducerCallbackCount != 0) {
            asharia::logError(
                "Render graph invoked a callback after missing-producer compile failure.");
            return false;
        }

        asharia::RenderGraph missingSchemaGraph;
        const auto schemaBackbuffer = missingSchemaGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "UnknownSchemaBackbuffer",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        int missingSchemaCallbackCount = 0;
        missingSchemaGraph.addPass("UnknownTypedPass", "basic.unknown-pass")
            .setParamsType("basic.unknown-pass.params")
            .writeTransfer("target", schemaBackbuffer)
            .execute([&missingSchemaCallbackCount](asharia::RenderGraphPassContext) {
                ++missingSchemaCallbackCount;
                return asharia::Result<void>{};
            });

        auto missingSchemaCompiled = missingSchemaGraph.compile(schemas);
        if (missingSchemaCompiled) {
            asharia::logError("Render graph accepted a pass type without a registered schema.");
            return false;
        }
        if (missingSchemaCompiled.error().message.find("has no registered schema") ==
            std::string::npos) {
            asharia::logError("Render graph produced an unexpected missing-schema error: " +
                              missingSchemaCompiled.error().message);
            return false;
        }
        if (missingSchemaCallbackCount != 0) {
            asharia::logError("Render graph invoked a callback during missing-schema compile.");
            return false;
        }

        asharia::RenderGraph mixedReadWriteGraph;
        const auto mixedReadWriteImage =
            mixedReadWriteGraph.importImage(asharia::RenderGraphImageDesc{
                .name = "MixedReadWriteImage",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
                .initialState = asharia::RenderGraphImageState::ShaderRead,
                .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
                .finalState = asharia::RenderGraphImageState::Present,
            });
        int mixedReadWriteCallbackCount = 0;
        mixedReadWriteGraph.addPass("MixedReadWritePass", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", mixedReadWriteImage, asharia::RenderGraphShaderStage::Fragment)
            .writeColor("target", mixedReadWriteImage)
            .execute([&mixedReadWriteCallbackCount](asharia::RenderGraphPassContext) {
                ++mixedReadWriteCallbackCount;
                return asharia::Result<void>{};
            });
        if (!expectRenderGraphCompileFailure(
                mixedReadWriteGraph.compile(schemas),
                ExpectedRenderGraphCompileFailure{
                    .message = "more than once",
                    .context = "same-image shader read and color write",
                })) {
            return false;
        }
        auto mixedReadWriteExecuted = mixedReadWriteGraph.execute();
        if (mixedReadWriteExecuted || mixedReadWriteCallbackCount != 0) {
            asharia::logError(
                "Render graph executed a same-image shader read and color write pass.");
            return false;
        }

        asharia::RenderGraph mixedColorTransferGraph;
        const auto mixedColorTransferImage =
            mixedColorTransferGraph.importImage(asharia::RenderGraphImageDesc{
                .name = "MixedColorTransferImage",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::Present,
            });
        mixedColorTransferGraph.addPass("MixedColorTransferPass", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("clearTarget", mixedColorTransferImage)
            .writeColor("colorTarget", mixedColorTransferImage);
        if (!expectRenderGraphCompileFailure(mixedColorTransferGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "more than once",
                                                 .context = "same-image transfer and color write",
                                             })) {
            return false;
        }

        asharia::RenderGraph mixedDepthGraph;
        const auto mixedDepthImage = mixedDepthGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "MixedDepthImage",
            .format = asharia::RenderGraphImageFormat::D32Sfloat,
            .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::DepthSampledRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        mixedDepthGraph.addPass("MixedDepthPass", "basic.depth-write")
            .setParamsType("basic.depth-write.params")
            .writeDepth("depthWrite", mixedDepthImage)
            .readDepthTexture("depthSample", mixedDepthImage,
                              asharia::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(
                mixedDepthGraph.compile(schemas),
                ExpectedRenderGraphCompileFailure{
                    .message = "more than once",
                    .context = "same-image depth write and sampled read",
                })) {
            return false;
        }

        asharia::RenderGraph ambiguousProducerGraph;
        const auto ambiguousProducerImage =
            ambiguousProducerGraph.createTransientImage(asharia::RenderGraphImageDesc{
                .name = "AmbiguousProducerImage",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
            });
        ambiguousProducerGraph.addPass("ReadBeforeTwoWriters", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", ambiguousProducerImage,
                         asharia::RenderGraphShaderStage::Fragment);
        ambiguousProducerGraph.addPass("FutureWriterA", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", ambiguousProducerImage);
        ambiguousProducerGraph.addPass("FutureWriterB", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", ambiguousProducerImage);
        auto ambiguousProducerCompiled = ambiguousProducerGraph.compile(schemas);
        if (!expectRenderGraphCompileFailure(ambiguousProducerCompiled,
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "Candidate writers",
                                                 .context = "ambiguous producer diagnostics",
                                             })) {
            return false;
        }
        const std::string& ambiguousProducerError = ambiguousProducerCompiled.error().message;
        if (ambiguousProducerError.find("ReadBeforeTwoWriters") == std::string::npos ||
            ambiguousProducerError.find("AmbiguousProducerImage") == std::string::npos ||
            ambiguousProducerError.find("FutureWriterA") == std::string::npos ||
            ambiguousProducerError.find("FutureWriterB") == std::string::npos) {
            asharia::logError("Render graph ambiguous producer diagnostic omitted context: " +
                              ambiguousProducerError);
            return false;
        }

        asharia::RenderGraph missingFinalStateGraph;
        const auto missingFinalImage =
            missingFinalStateGraph.importImage(asharia::RenderGraphImageDesc{
                .name = "ImportedTextureWithoutFinalState",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
                .initialState = asharia::RenderGraphImageState::ShaderRead,
                .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            });
        missingFinalStateGraph.addPass("SampleImportedWithoutFinal", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", missingFinalImage, asharia::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(missingFinalStateGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "must declare an explicit final state",
                                                 .context = "imported image without final state",
                                             })) {
            return false;
        }

        asharia::RenderGraph explicitFinalStateGraph;
        const auto explicitFinalImage =
            explicitFinalStateGraph.importImage(asharia::RenderGraphImageDesc{
                .name = "ExplicitFinalImportedTexture",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
                .initialState = asharia::RenderGraphImageState::ShaderRead,
                .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
                .finalState = asharia::RenderGraphImageState::ShaderRead,
                .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
            });
        explicitFinalStateGraph.addPass("SampleExplicitFinalImported", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", explicitFinalImage, asharia::RenderGraphShaderStage::Fragment);
        auto explicitFinalCompiled = explicitFinalStateGraph.compile(schemas);
        if (!explicitFinalCompiled) {
            asharia::logError(
                "Render graph rejected an imported image with explicit final state: " +
                explicitFinalCompiled.error().message);
            return false;
        }
        if (!explicitFinalCompiled->finalTransitions.empty()) {
            asharia::logError(
                "Render graph produced a final transition for an already shader-readable import.");
            return false;
        }

        asharia::RenderGraph cycleGraph;
        const auto cycleImageA = cycleGraph.createTransientImage(asharia::RenderGraphImageDesc{
            .name = "CycleImageA",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
        });
        const auto cycleImageB = cycleGraph.createTransientImage(asharia::RenderGraphImageDesc{
            .name = "CycleImageB",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
        });
        cycleGraph.addPass("CycleFirst", "basic.cycle-read-write")
            .setParamsType("basic.cycle-read-write.params")
            .readTexture("source", cycleImageA, asharia::RenderGraphShaderStage::Fragment)
            .writeColor("target", cycleImageB);
        cycleGraph.addPass("CycleSecond", "basic.cycle-read-write")
            .setParamsType("basic.cycle-read-write.params")
            .readTexture("source", cycleImageB, asharia::RenderGraphShaderStage::Fragment)
            .writeColor("target", cycleImageA);
        auto cycleCompiled = cycleGraph.compile(schemas);
        if (!expectRenderGraphCompileFailure(cycleCompiled,
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "dependency cycle",
                                                 .context = "cyclic dependency diagnostics",
                                             })) {
            return false;
        }
        const std::string& cycleError = cycleCompiled.error().message;
        if (cycleError.find("CycleFirst") == std::string::npos ||
            cycleError.find("CycleSecond") == std::string::npos ||
            cycleError.find("CycleImageA") == std::string::npos ||
            cycleError.find("Cycle edge") == std::string::npos) {
            asharia::logError(
                "Render graph cycle diagnostic omitted pass, image, or edge context: " +
                cycleError);
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphCulling(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph cullingGraph;
        const auto cullingBackbuffer = cullingGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "CullingBackbuffer",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        const auto unusedTransient =
            cullingGraph.createTransientImage(asharia::RenderGraphImageDesc{
                .name = "UnusedTransient",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 32, .height = 32},
            });

        int visibleCallbackCount = 0;
        int culledCallbackCount = 0;
        int sideEffectCallbackCount = 0;
        cullingGraph.addPass("VisibleClear", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", cullingBackbuffer)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.0F, 0.0F, 0.0F, 1.0F});
            })
            .execute([&visibleCallbackCount](asharia::RenderGraphPassContext) {
                ++visibleCallbackCount;
                return asharia::Result<void>{};
            });
        cullingGraph.addPass("WriteUnusedTransient", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", unusedTransient)
            .allowCulling()
            .execute([&culledCallbackCount](asharia::RenderGraphPassContext) {
                ++culledCallbackCount;
                return asharia::Result<void>{};
            });
        cullingGraph.addPass("SideEffectMarker", "basic.side-effect")
            .setParamsType("basic.side-effect.params")
            .execute([&sideEffectCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (!context.allowCulling || !context.hasSideEffects) {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph side-effect context did not preserve culling flags.",
                    }};
                }
                ++sideEffectCallbackCount;
                return asharia::Result<void>{};
            });

        auto compiled = cullingGraph.compile(schemas);
        if (!compiled) {
            asharia::logError(compiled.error().message);
            return false;
        }
        if (compiled->passes.size() != 2 || compiled->culledPasses.size() != 1 ||
            !compiled->transientImages.empty()) {
            asharia::logError("Render graph culling smoke produced an unexpected compile plan.");
            return false;
        }
        if (compiled->culledPasses.front().name != "WriteUnusedTransient" ||
            compiled->culledPasses.front().declarationIndex != 1) {
            asharia::logError("Render graph culling smoke culled the wrong pass.");
            return false;
        }
        if (compiled->passes[0].name != "VisibleClear" ||
            compiled->passes[1].name != "SideEffectMarker" || !compiled->passes[1].hasSideEffects) {
            asharia::logError("Render graph culling smoke kept the wrong active passes.");
            return false;
        }

        auto executed = cullingGraph.execute(*compiled);
        if (!executed) {
            asharia::logError(executed.error().message);
            return false;
        }
        if (visibleCallbackCount != 1 || sideEffectCallbackCount != 1 || culledCallbackCount != 0) {
            asharia::logError("Render graph culling smoke invoked unexpected callbacks.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphBufferVulkanMappings(
        const asharia::RenderGraphCompileResult& compiled) {
        const auto vulkanBufferWriteTransition =
            asharia::vulkanBufferTransition(compiled.passes[0].bufferTransitionsBefore.front());
        if (vulkanBufferWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanBufferWriteTransition.srcAccessMask != 0 ||
            vulkanBufferWriteTransition.dstStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            vulkanBufferWriteTransition.dstAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT) {
            asharia::logError("Render graph Vulkan buffer transfer-write mapping was unexpected.");
            return false;
        }

        constexpr VkAccessFlags2 kExpectedShaderBufferReadAccess =
            VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        const auto vulkanBufferReadTransition =
            asharia::vulkanBufferTransition(compiled.passes[1].bufferTransitionsBefore.front());
        if (vulkanBufferReadTransition.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            vulkanBufferReadTransition.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
            vulkanBufferReadTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanBufferReadTransition.dstAccessMask != kExpectedShaderBufferReadAccess) {
            asharia::logError("Render graph Vulkan buffer shader-read mapping was unexpected.");
            return false;
        }

        const VkBufferMemoryBarrier2 bufferBarrier =
            asharia::vulkanBufferBarrier(vulkanBufferReadTransition, VK_NULL_HANDLE, 16, 64);
        if (bufferBarrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED ||
            bufferBarrier.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED ||
            bufferBarrier.offset != 16 || bufferBarrier.size != 64 ||
            bufferBarrier.buffer != VK_NULL_HANDLE) {
            asharia::logError("Render graph Vulkan buffer barrier fields were unexpected.");
            return false;
        }

        const asharia::VulkanRenderGraphBufferUsage computeReadUsage = asharia::vulkanBufferUsage(
            asharia::RenderGraphBufferState::ShaderRead, asharia::RenderGraphShaderStage::Compute);
        if (computeReadUsage.stageMask != VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ||
            computeReadUsage.accessMask != kExpectedShaderBufferReadAccess) {
            asharia::logError("Render graph Vulkan compute buffer read mapping was unexpected.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphBuffers(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph bufferGraph;
        const auto transientBuffer =
            bufferGraph.createTransientBuffer(asharia::RenderGraphBufferDesc{
                .name = "TransientUploadBuffer",
                .byteSize = 1024,
            });

        int writeCallbackCount = 0;
        int readCallbackCount = 0;
        bufferGraph.addPass("ReadTransientBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", transientBuffer, asharia::RenderGraphShaderStage::Fragment)
            .execute([&readCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (context.name != "ReadTransientBuffer" ||
                    context.type != "basic.buffer-read-fragment" ||
                    context.paramsType != "basic.buffer-read-fragment.params" ||
                    !context.bufferWrites.empty() || context.bufferReads.size() != 1 ||
                    context.bufferReadSlots.size() != 1 || !context.bufferWriteSlots.empty() ||
                    context.bufferReadSlots.front().name != "source" ||
                    context.bufferReadSlots.front().shaderStage !=
                        asharia::RenderGraphShaderStage::Fragment ||
                    context.bufferTransitionsBefore.size() != 1 ||
                    context.bufferTransitionsBefore.front().oldState !=
                        asharia::RenderGraphBufferState::TransferWrite ||
                    context.bufferTransitionsBefore.front().newState !=
                        asharia::RenderGraphBufferState::ShaderRead ||
                    context.bufferTransitionsBefore.front().newShaderStage !=
                        asharia::RenderGraphShaderStage::Fragment) {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph buffer read executor received unexpected pass context.",
                    }};
                }
                ++readCallbackCount;
                return {};
            });
        bufferGraph.addPass("WriteTransientBuffer", "basic.buffer-transfer-write")
            .setParamsType("basic.buffer-transfer-write.params")
            .writeBuffer("target", transientBuffer)
            .execute([&writeCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (context.name != "WriteTransientBuffer" ||
                    context.type != "basic.buffer-transfer-write" ||
                    context.paramsType != "basic.buffer-transfer-write.params" ||
                    !context.bufferReads.empty() || context.bufferWrites.size() != 1 ||
                    !context.bufferReadSlots.empty() || context.bufferWriteSlots.size() != 1 ||
                    context.bufferWriteSlots.front().name != "target" ||
                    context.bufferTransitionsBefore.size() != 1 ||
                    context.bufferTransitionsBefore.front().oldState !=
                        asharia::RenderGraphBufferState::Undefined ||
                    context.bufferTransitionsBefore.front().newState !=
                        asharia::RenderGraphBufferState::TransferWrite) {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph buffer write executor received unexpected pass context.",
                    }};
                }
                ++writeCallbackCount;
                return {};
            });

        auto compiled = bufferGraph.compile(schemas);
        if (!compiled) {
            asharia::logError(compiled.error().message);
            return false;
        }
        if (compiled->declaredBufferCount != 1 || compiled->passes.size() != 2 ||
            compiled->passes[0].name != "WriteTransientBuffer" ||
            compiled->passes[1].name != "ReadTransientBuffer" ||
            compiled->dependencies.size() != 1 || compiled->transientBuffers.size() != 1 ||
            !compiled->finalBufferTransitions.empty()) {
            asharia::logError("Render graph buffer smoke produced an unexpected compile plan.");
            return false;
        }
        if (!validateSmokeRenderGraphBufferVulkanMappings(*compiled)) {
            return false;
        }

        auto executed = bufferGraph.execute(*compiled);
        if (!executed) {
            asharia::logError(executed.error().message);
            return false;
        }
        if (writeCallbackCount != 1 || readCallbackCount != 1) {
            asharia::logError("Render graph buffer smoke invoked unexpected callbacks.");
            return false;
        }

        asharia::RenderGraph importedReadGraph;
        const auto importedBuffer = importedReadGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "ImportedReadBuffer",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::ShaderRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            .finalState = asharia::RenderGraphBufferState::ShaderRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        importedReadGraph.addPass("ReadImportedBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", importedBuffer, asharia::RenderGraphShaderStage::Fragment);
        auto importedReadCompiled = importedReadGraph.compile(schemas);
        if (!importedReadCompiled) {
            asharia::logError("Render graph rejected a shader-readable imported buffer: " +
                              importedReadCompiled.error().message);
            return false;
        }
        if (!importedReadCompiled->finalBufferTransitions.empty() ||
            !importedReadCompiled->transientBuffers.empty() ||
            !importedReadCompiled->passes.front().bufferTransitionsBefore.empty()) {
            asharia::logError(
                "Render graph produced unexpected transitions for imported buffer read.");
            return false;
        }

        asharia::RenderGraph missingFinalStateGraph;
        const auto missingFinalBuffer =
            missingFinalStateGraph.importBuffer(asharia::RenderGraphBufferDesc{
                .name = "ImportedBufferWithoutFinalState",
                .byteSize = 64,
                .initialState = asharia::RenderGraphBufferState::ShaderRead,
                .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            });
        missingFinalStateGraph.addPass("ReadImportedWithoutFinal", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", missingFinalBuffer, asharia::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(missingFinalStateGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "must declare an explicit final state",
                                                 .context = "imported buffer without final state",
                                             })) {
            return false;
        }

        asharia::RenderGraph missingProducerGraph;
        const auto orphanBuffer = missingProducerGraph.createTransientBuffer(
            asharia::RenderGraphBufferDesc{.name = "OrphanTransientBuffer", .byteSize = 64});
        missingProducerGraph.addPass("ReadOrphanBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", orphanBuffer, asharia::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(missingProducerGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "reads buffer",
                                                 .context = "transient buffer without producer",
                                             })) {
            return false;
        }

        asharia::RenderGraph missingStageGraph;
        const auto stageBuffer = missingStageGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "BufferMissingShaderStage",
            .byteSize = 64,
            .initialState = asharia::RenderGraphBufferState::ShaderRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            .finalState = asharia::RenderGraphBufferState::ShaderRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        missingStageGraph.addPass("ReadBufferWithoutStage", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", stageBuffer, asharia::RenderGraphShaderStage::None);
        if (!expectRenderGraphCompileFailure(missingStageGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "without a shader stage",
                                                 .context = "buffer read without shader stage",
                                             })) {
            return false;
        }

        asharia::RenderGraph zeroSizeGraph;
        const auto zeroSizeBuffer = zeroSizeGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "ZeroSizeBuffer",
            .byteSize = 0,
            .initialState = asharia::RenderGraphBufferState::ShaderRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            .finalState = asharia::RenderGraphBufferState::ShaderRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        zeroSizeGraph.addPass("ReadZeroSizeBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", zeroSizeBuffer, asharia::RenderGraphShaderStage::Fragment);
        return expectRenderGraphCompileFailure(zeroSizeGraph.compile(schemas),
                                               ExpectedRenderGraphCompileFailure{
                                                   .message = "non-zero byte size",
                                                   .context = "zero-size render graph buffer",
                                               });
    }

    int runSmokeRenderGraph() {
        asharia::RenderGraph graph;
        const auto backbuffer = graph.importImage(asharia::RenderGraphImageDesc{
            .name = "Backbuffer",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        const auto depthBuffer = graph.importImage(asharia::RenderGraphImageDesc{
            .name = "DepthBuffer",
            .format = asharia::RenderGraphImageFormat::D32Sfloat,
            .extent = asharia::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::DepthSampledRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        const auto transientColor = graph.createTransientImage(asharia::RenderGraphImageDesc{
            .name = "TransientColor",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 640, .height = 360},
        });

        int callbackCount = 0;
        graph.addPass("ClearColor", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", backbuffer)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.02F, 0.12F, 0.18F, 1.0F});
            });
        graph.addPass("SampleColor", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", backbuffer, asharia::RenderGraphShaderStage::Fragment)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/SmokeSample", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setFloat("Exposure", 1.0F)
                    .drawFullscreenTriangle();
            });
        graph.addPass("WriteDepth", "basic.depth-write")
            .setParamsType("basic.depth-write.params")
            .writeDepth("depth", depthBuffer);
        graph.addPass("SampleDepth", "basic.depth-sample-fragment")
            .setParamsType("basic.depth-sample-fragment.params")
            .readDepthTexture("depth", depthBuffer, asharia::RenderGraphShaderStage::Fragment);
        graph.addPass("SampleTransientColor", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", transientColor, asharia::RenderGraphShaderStage::Fragment)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/TransientSample", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setVec4("Tint", std::array{1.0F, 0.85F, 0.65F, 1.0F})
                    .drawFullscreenTriangle();
            });
        graph.addPass("WriteTransientColor", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", transientColor);

        asharia::RenderGraphSchemaRegistry schemas;
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.clear-transfer",
            .paramsType = "basic.clear-transfer.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::ClearColor},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.sample-fragment",
            .paramsType = "basic.sample-fragment.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    asharia::RenderGraphCommandKind::SetShader,
                    asharia::RenderGraphCommandKind::SetTexture,
                    asharia::RenderGraphCommandKind::SetFloat,
                    asharia::RenderGraphCommandKind::DrawFullscreenTriangle,
                },
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.depth-write",
            .paramsType = "basic.depth-write.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = asharia::RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.depth-sample-fragment",
            .paramsType = "basic.depth-sample-fragment.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = asharia::RenderGraphSlotAccess::DepthSampledRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.transient-color",
            .paramsType = "basic.transient-color.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.transient-sample-fragment",
            .paramsType = "basic.transient-sample-fragment.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    asharia::RenderGraphCommandKind::SetShader,
                    asharia::RenderGraphCommandKind::SetTexture,
                    asharia::RenderGraphCommandKind::SetVec4,
                    asharia::RenderGraphCommandKind::DrawFullscreenTriangle,
                },
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.cycle-read-write",
            .paramsType = "basic.cycle-read-write.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.side-effect",
            .paramsType = "basic.side-effect.params",
            .resourceSlots = {},
            .allowedCommands = {},
            .allowCulling = true,
            .hasSideEffects = true,
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.buffer-transfer-write",
            .paramsType = "basic.buffer-transfer-write.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.buffer-read-fragment",
            .paramsType = "basic.buffer-read-fragment.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            asharia::logError(compiled.error().message);
            return EXIT_FAILURE;
        }
        if (compiled->finalTransitions.empty()) {
            asharia::logError("Render graph did not produce a final transition.");
            return EXIT_FAILURE;
        }
        if (compiled->passes.size() != 6 || compiled->passes[1].transitionsBefore.empty() ||
            compiled->passes[2].transitionsBefore.empty() ||
            compiled->passes[3].transitionsBefore.empty() ||
            compiled->passes[4].transitionsBefore.empty() ||
            compiled->passes[5].transitionsBefore.empty() ||
            compiled->transientImages.size() != 1) {
            asharia::logError(
                "Render graph did not produce the expected shader-read pass transition.");
            return EXIT_FAILURE;
        }
        if (compiled->passes[4].name != "WriteTransientColor" ||
            compiled->passes[4].declarationIndex != 5 ||
            compiled->passes[5].name != "SampleTransientColor" ||
            compiled->passes[5].declarationIndex != 4) {
            asharia::logError(
                "Render graph compiler did not reorder transient producer before reader.");
            return EXIT_FAILURE;
        }
        if (compiled->dependencies.size() != 3) {
            asharia::logError("Render graph compiler produced an unexpected dependency count.");
            return EXIT_FAILURE;
        }
        if (!compiled->culledPasses.empty()) {
            asharia::logError("Render graph compiler culled an unexpected smoke pass.");
            return EXIT_FAILURE;
        }

        std::cout << graph.formatDebugTables(*compiled) << '\n';

        if (!validateSmokeRenderGraphTransientPlan(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphCommands(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphVulkanMappings(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphNegativeCompiles(schemas)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphBuiltinSchemaFailures()) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphCulling(schemas)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphBuffers(schemas)) {
            return EXIT_FAILURE;
        }

        asharia::RenderGraph invalidCommandGraph;
        const auto invalidBackbuffer =
            invalidCommandGraph.importImage(asharia::RenderGraphImageDesc{
                .name = "InvalidBackbuffer",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::Present,
            });
        invalidCommandGraph.addPass("InvalidClearCommand", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", invalidBackbuffer)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.drawFullscreenTriangle();
            });
        auto invalidCompiled = invalidCommandGraph.compile(schemas);
        if (invalidCompiled) {
            asharia::logError("Render graph schema accepted an invalid command kind.");
            return EXIT_FAILURE;
        }

        asharia::RenderGraphExecutorRegistry executors;
        registerSmokeRenderGraphExecutors(executors, callbackCount);

        auto executed = graph.execute(*compiled, executors);
        if (!executed) {
            asharia::logError(executed.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Render graph passes: " << compiled->passes.size()
                  << ", final transitions: " << compiled->finalTransitions.size()
                  << ", dependencies: " << compiled->dependencies.size()
                  << ", culled: " << compiled->culledPasses.size()
                  << ", callbacks: " << callbackCount << '\n';
        return compiled->passes.size() == 6 && compiled->transientImages.size() == 1 &&
                       compiled->finalTransitions.size() == 1 &&
                       compiled->dependencies.size() == 3 && compiled->culledPasses.empty() &&
                       callbackCount == 6
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

    int runSmokeDepthTriangle() {
        return runSmokeTriangle(true);
    }

    int runSmokeTriangleDefault() {
        return runSmokeTriangle();
    }

    int runSmokeMesh() {
        return runSmokeTriangle(false, asharia::BasicMeshKind::IndexedQuad);
    }

    std::optional<int> runRequestedCommand(std::span<char*> args) {
        if (hasArg(args, "--bench-rendergraph")) {
            return runBenchRenderGraph(args);
        }

        struct SmokeCommand {
            std::string_view option;
            int (*run)();
        };

        const std::array commands{
            SmokeCommand{.option = "--smoke-window", .run = runSmokeWindow},
            SmokeCommand{.option = "--smoke-vulkan", .run = runSmokeVulkan},
            SmokeCommand{.option = "--smoke-frame", .run = runSmokeFrame},
            SmokeCommand{.option = "--smoke-rendergraph", .run = runSmokeRenderGraph},
            SmokeCommand{.option = "--smoke-transient", .run = runSmokeTransient},
            SmokeCommand{.option = "--smoke-dynamic-rendering", .run = runSmokeDynamicRendering},
            SmokeCommand{.option = "--smoke-resize", .run = runSmokeResize},
            SmokeCommand{.option = "--smoke-triangle", .run = runSmokeTriangleDefault},
            SmokeCommand{.option = "--smoke-depth-triangle", .run = runSmokeDepthTriangle},
            SmokeCommand{.option = "--smoke-mesh", .run = runSmokeMesh},
            SmokeCommand{.option = "--smoke-mesh-3d", .run = runSmokeMesh3D},
            SmokeCommand{.option = "--smoke-draw-list", .run = runSmokeDrawList},
            SmokeCommand{.option = "--smoke-descriptor-layout", .run = runSmokeDescriptorLayout},
            SmokeCommand{.option = "--smoke-fullscreen-texture", .run = runSmokeFullscreenTexture},
            SmokeCommand{.option = "--smoke-offscreen-viewport", .run = runSmokeOffscreenViewport},
            SmokeCommand{.option = "--smoke-deferred-deletion", .run = runSmokeDeferredDeletion},
            SmokeCommand{.option = "--smoke-reflection-registry",
                         .run = runSmokeReflectionRegistry},
            SmokeCommand{.option = "--smoke-reflection-transform",
                         .run = runSmokeReflectionTransform},
            SmokeCommand{.option = "--smoke-reflection-attributes",
                         .run = runSmokeReflectionAttributes},
            SmokeCommand{.option = "--smoke-serialization-roundtrip",
                         .run = runSmokeSerializationRoundtrip},
            SmokeCommand{.option = "--smoke-serialization-json-archive",
                         .run = runSmokeSerializationJsonArchive},
            SmokeCommand{.option = "--smoke-serialization-migration",
                         .run = runSmokeSerializationMigration},
        };

        for (const SmokeCommand& command : commands) {
            if (hasArg(args, command.option)) {
                return command.run();
            }
        }

        return std::nullopt;
    }

} // namespace

int main(int argc, char** argv) {
    try {
        std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (args.size() == 1) {
            return runInteractiveViewer();
        }

        if (hasArg(args, "--help")) {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        }

        if (const std::optional<int> commandResult = runRequestedCommand(args)) {
            return *commandResult;
        }

        printVersion();
        printUsage();
        return EXIT_FAILURE;
    } catch (const std::exception& exception) {
        asharia::logError(exception.what());
    } catch (...) {
        asharia::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
