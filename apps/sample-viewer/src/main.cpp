#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "asharia/asset_pipeline/asset_product_blob.hpp"
#include "asharia/asset_pipeline/asset_product_execution.hpp"
#include "asharia/asset_pipeline/asset_texture_import.hpp"
#include "asharia/asset_pipeline/asset_texture_import_profile.hpp"
#include "asharia/core/error.hpp"
#include "asharia/core/log.hpp"
#include "asharia/core/version.hpp"
#include "asharia/profiling/frame_profiler.hpp"
#include "asharia/reflection/context_view.hpp"
#include "asharia/reflection/type_builder.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"
#include "asharia/renderer_basic_vulkan/clear_frame.hpp"
#include "asharia/renderer_basic_vulkan/frame_graph_vulkan.hpp"
#include "asharia/rendergraph/render_graph.hpp"
#include "asharia/rhi_vulkan/deferred_deletion_queue.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"
#include "asharia/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"
#include "asharia/scene/world.hpp"
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
                     "[--smoke-mrt [--frames N] [--hold]] "
                     "[--smoke-descriptor-layout] [--smoke-material-binding] "
                     "[--smoke-fullscreen-texture] "
                     "[--smoke-scene-draw-packet] "
                     "[--smoke-render-view-grid-readback] "
                     "[--smoke-offscreen-viewport] [--smoke-compute-dispatch] "
                     "[--smoke-buffer-upload] [--smoke-texture-upload] "
                     "[--smoke-renderer-format-contract] [--smoke-deferred-deletion] "
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

    asharia::Error smokeRenderGraphError(std::string message) {
        return asharia::Error{asharia::ErrorDomain::RenderGraph, 0, std::move(message)};
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

    bool validateComputeBufferStats(asharia::VulkanBufferStats stats, std::string_view context) {
        if (stats.created != 2 || stats.deviceLocalCreated != 1 || stats.hostReadbackCreated != 1 ||
            stats.allocatedBytes == 0) {
            asharia::logError(std::string{context} +
                              " did not create the expected storage and readback buffers.");
            return false;
        }
        return true;
    }

    bool validateComputeDispatchStats(asharia::BasicComputeDispatchStats stats,
                                      std::string_view context) {
        if (stats.bufferFillsRecorded != 1 || stats.dispatchesRecorded != 1) {
            asharia::logError(std::string{context} +
                              " did not record exactly one buffer fill and compute dispatch.");
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

    struct SmokeMrtOptions {
        std::size_t frameCount{3};
        bool hold{};
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

    std::optional<SmokeMrtOptions> parseSmokeMrtOptions(std::span<char*> args) {
        SmokeMrtOptions options{
            .frameCount = 3,
            .hold = hasArg(args, "--hold"),
        };
        if (!parseSizeOption(args, "--frames", options.frameCount)) {
            return std::nullopt;
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

    int runSmokeRendererFormatContract() {
        auto supported = asharia::basicRenderGraphImageFormat(VK_FORMAT_B8G8R8A8_SRGB,
                                                              "Renderer format contract smoke");
        if (!supported || *supported != asharia::RenderGraphImageFormat::B8G8R8A8Srgb) {
            asharia::logError("Renderer format contract smoke did not accept B8G8R8A8 SRGB.");
            return EXIT_FAILURE;
        }

        auto unsupported = asharia::basicRenderGraphImageFormat(VK_FORMAT_R8G8B8A8_UNORM,
                                                                "Renderer format contract smoke");
        if (unsupported) {
            asharia::logError("Renderer format contract smoke accepted an unsupported format.");
            return EXIT_FAILURE;
        }
        if (unsupported.error().message.find("VK_FORMAT_R8G8B8A8_UNORM") == std::string::npos) {
            asharia::logError("Renderer format contract smoke lost unsupported format context.");
            return EXIT_FAILURE;
        }

        const asharia::VulkanFrameRecordContext unsupportedFrame{
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = VkExtent2D{.width = 16, .height = 16},
        };
        auto unsupportedBackbuffer = asharia::basicBackbufferDesc(unsupportedFrame);
        if (unsupportedBackbuffer) {
            asharia::logError("Renderer format contract smoke imported an unsupported backbuffer.");
            return EXIT_FAILURE;
        }

        const asharia::VulkanFrameRecordContext supportedFrame{
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .extent = VkExtent2D{.width = 16, .height = 16},
        };
        auto supportedBackbuffer = asharia::basicBackbufferDesc(supportedFrame);
        if (!supportedBackbuffer ||
            supportedBackbuffer->format != asharia::RenderGraphImageFormat::B8G8R8A8Srgb) {
            asharia::logError("Renderer format contract smoke did not create a supported desc.");
            return EXIT_FAILURE;
        }

        std::cout << "Renderer format contract smoke rejected unsupported Vulkan formats.\n";
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

    int runSmokeMrt(std::span<char*> args) {
        const std::optional<SmokeMrtOptions> options = parseSmokeMrtOptions(args);
        if (!options) {
            return EXIT_FAILURE;
        }

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
            *glfw, asharia::WindowDesc{.title = "Asharia Engine MRT Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine MRT Smoke",
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

        auto renderer = asharia::BasicMrtRenderer::create(asharia::BasicMrtRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
        });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.04F, 0.07F, 0.11F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanFrameRecordCallback record =
            [&renderer](const asharia::VulkanFrameRecordContext& frame) {
                return renderer->recordFrame(frame);
            };

        std::size_t renderedFrames = 0;
        while (options->hold ? !window->shouldClose() : renderedFrames < options->frameCount) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(record);
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError("Swapchain remained out of date during MRT smoke.");
                return EXIT_FAILURE;
            }

            ++renderedFrames;
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }
        if (renderedFrames == 0) {
            asharia::logError("MRT smoke did not render any frames.");
            return EXIT_FAILURE;
        }

        const asharia::VulkanTransientImagePoolStats transientPoolStats =
            renderer->transientPoolStats();
        if (renderedFrames >= 3 &&
            (transientPoolStats.created < 2 || transientPoolStats.reused < 2 ||
             transientPoolStats.retired < 2)) {
            asharia::logError("MRT smoke did not reuse both transient color attachments.");
            return EXIT_FAILURE;
        }
        const asharia::VulkanDebugLabelStats debugLabelStats = frameLoop->debugLabelStats();
        if (!validateDebugLabelStats(debugLabelStats, "MRT smoke")) {
            return EXIT_FAILURE;
        }
        if (debugLabelStats.objectsNamed == 0) {
            asharia::logError("MRT smoke did not name Vulkan resources for capture tools.");
            return EXIT_FAILURE;
        }
        if (renderedFrames >= 3 &&
            !validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "MRT smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered MRT frames: " << extent.width << 'x' << extent.height << '\n';
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

    int runSmokeMaterialBinding() {
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
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Material Binding Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Material Binding Smoke",
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
            asharia::validateBasicMaterialBindingSmoke(asharia::BasicMaterialBindingSmokeDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!validated) {
            asharia::logError(validated.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Material binding smoke: signature drove set 0 bindings 0-2 "
                     "buffer/image/sampler\n";
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeComputeDispatch() {
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
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Compute Dispatch Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Compute Dispatch Smoke",
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
                          .clearColor = VkClearColorValue{{0.03F, 0.03F, 0.04F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer =
            asharia::BasicComputeDispatchRenderer::create(asharia::BasicComputeDispatchRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
                .graphicsQueueSupportsCompute = context->deviceInfo().graphicsQueueSupportsCompute,
            });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

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
            asharia::logError("Swapchain remained out of date during compute dispatch smoke.");
            return EXIT_FAILURE;
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before compute readback: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        auto values = renderer->readbackValuesAfterGpuComplete();
        if (!values) {
            asharia::logError(values.error().message);
            return EXIT_FAILURE;
        }

        constexpr std::array<std::uint32_t, 4> kExpectedValues{
            0xA5000000U,
            0xA5000001U,
            0xA5000002U,
            0xA5000003U,
        };
        if (*values != kExpectedValues) {
            asharia::logError("Compute dispatch smoke read back unexpected storage buffer data.");
            return EXIT_FAILURE;
        }
        const asharia::BasicPipelineCacheStats pipelineStats = renderer->pipelineCacheStats();
        if (pipelineStats.created != 1) {
            asharia::logError("Compute dispatch smoke did not create exactly one pipeline.");
            return EXIT_FAILURE;
        }
        if (!validateDescriptorAllocatorStats(renderer->descriptorAllocatorStats(),
                                              "Compute dispatch smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateComputeBufferStats(renderer->bufferStats(), "Compute dispatch smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateComputeDispatchStats(renderer->computeStats(), "Compute dispatch smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Compute dispatch smoke")) {
            return EXIT_FAILURE;
        }

        std::cout << "Compute dispatch storage values: " << (*values)[0] << ", " << (*values)[1]
                  << ", " << (*values)[2] << ", " << (*values)[3] << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    constexpr std::size_t kSmokeBufferUploadByteCount = 256;

    [[nodiscard]] std::array<std::byte, kSmokeBufferUploadByteCount> smokeBufferUploadPayload() {
        std::array<std::byte, kSmokeBufferUploadByteCount> bytes{};
        std::size_t index = 0;
        for (std::byte& byte : bytes) {
            byte = std::byte{static_cast<unsigned char>((index * 37U + 11U) & 0xFFU)};
            ++index;
        }
        return bytes;
    }

    [[nodiscard]] asharia::Result<void> recordSmokeBufferCopyPass(
        const asharia::VulkanFrameRecordContext& frame, asharia::RenderGraphPassContext pass,
        std::span<const asharia::VulkanRenderGraphBufferBinding> bufferBindings) {
        [[maybe_unused]] const auto timestamp =
            asharia::VulkanTimestampScope::begin(frame, pass.name);
        [[maybe_unused]] const auto debugLabel = asharia::VulkanDebugLabelScope::begin(
            frame, asharia::renderGraphPassDebugLabel(pass, {}, bufferBindings));

        auto transitions = asharia::recordRenderGraphBufferTransitions(
            frame, pass.bufferTransitionsBefore, bufferBindings);
        if (!transitions) {
            return std::unexpected{std::move(transitions.error())};
        }
        if (pass.type != asharia::kBasicTransferCopyBufferPassType || pass.commands.size() != 1) {
            return std::unexpected{
                smokeRenderGraphError("Buffer upload copy pass received an invalid context.")};
        }

        const asharia::RenderGraphCommand& copy = pass.commands.front();
        if (copy.kind != asharia::RenderGraphCommandKind::CopyBuffer || copy.name != "source" ||
            copy.secondaryName != "target") {
            return std::unexpected{
                smokeRenderGraphError("Buffer upload copy command does not match its slots.")};
        }

        auto source =
            asharia::findVulkanRenderGraphBufferTransferRead(pass, copy.name, bufferBindings);
        if (!source) {
            return std::unexpected{std::move(source.error())};
        }
        auto target = asharia::findVulkanRenderGraphBufferTransferWrite(pass, copy.secondaryName,
                                                                        bufferBindings);
        if (!target) {
            return std::unexpected{std::move(target.error())};
        }
        if (source->size == VK_WHOLE_SIZE || target->size == VK_WHOLE_SIZE ||
            source->size > target->size) {
            return std::unexpected{smokeRenderGraphError(
                "Buffer upload copy bindings have invalid source or target sizes.")};
        }

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = source->offset;
        copyRegion.dstOffset = target->offset;
        copyRegion.size = source->size;
        vkCmdCopyBuffer(frame.commandBuffer, source->vulkanBuffer, target->vulkanBuffer, 1,
                        &copyRegion);
        return {};
    }

    [[nodiscard]] bool
    validateSmokeBufferUploadPlan(const asharia::RenderGraph& graph,
                                  const asharia::RenderGraphCompileResult& compiled) {
        if (compiled.passes.size() != 2 || compiled.passes[0].name != "UploadToDeviceLocal" ||
            compiled.passes[1].name != "ReadbackDeviceLocal" ||
            compiled.passes[0].commands.size() != 1 || compiled.passes[1].commands.size() != 1 ||
            compiled.passes[0].commands.front().kind !=
                asharia::RenderGraphCommandKind::CopyBuffer ||
            compiled.passes[1].commands.front().kind !=
                asharia::RenderGraphCommandKind::CopyBuffer ||
            compiled.dependencies.size() != 1 || compiled.finalBufferTransitions.size() != 1 ||
            compiled.finalBufferTransitions.front().newState !=
                asharia::RenderGraphBufferState::HostRead) {
            asharia::logError("Buffer upload smoke produced an unexpected RenderGraph plan.");
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot diagnostics =
            graph.diagnosticsSnapshot(compiled);
        const auto copyCommandCount = std::ranges::count_if(
            diagnostics.commands, [](const asharia::RenderGraphDiagnosticsCommandNode& command) {
                return command.kind == asharia::RenderGraphCommandKind::CopyBuffer &&
                       command.detail == "source -> target";
            });
        if (copyCommandCount != 2) {
            asharia::logError(
                "Buffer upload smoke diagnostics did not expose both CopyBuffer commands.");
            return false;
        }

        return true;
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult> recordSmokeBufferUploadFrame(
        const asharia::VulkanFrameRecordContext& frame, asharia::VulkanBuffer& stagingBuffer,
        asharia::VulkanBuffer& deviceBuffer, asharia::VulkanBuffer& readbackBuffer) {
        auto clear = asharia::recordBasicClearFrame(frame);
        if (!clear) {
            return std::unexpected{std::move(clear.error())};
        }

        asharia::RenderGraph graph;
        const auto staging = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "SmokeUploadStagingBuffer",
            .byteSize = stagingBuffer.size(),
            .initialState = asharia::RenderGraphBufferState::TransferRead,
            .finalState = asharia::RenderGraphBufferState::TransferRead,
        });
        const auto device = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "SmokeDeviceLocalBuffer",
            .byteSize = deviceBuffer.size(),
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::TransferRead,
        });
        const auto readback = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "SmokeUploadReadbackBuffer",
            .byteSize = readbackBuffer.size(),
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::HostRead,
        });

        const std::array bufferBindings{
            asharia::VulkanRenderGraphBufferBinding{
                .buffer = staging,
                .vulkanBuffer = stagingBuffer.handle(),
                .offset = 0,
                .size = stagingBuffer.size(),
                .debugName = "SmokeUploadStagingBuffer",
            },
            asharia::VulkanRenderGraphBufferBinding{
                .buffer = device,
                .vulkanBuffer = deviceBuffer.handle(),
                .offset = 0,
                .size = deviceBuffer.size(),
                .debugName = "SmokeDeviceLocalBuffer",
            },
            asharia::VulkanRenderGraphBufferBinding{
                .buffer = readback,
                .vulkanBuffer = readbackBuffer.handle(),
                .offset = 0,
                .size = readbackBuffer.size(),
                .debugName = "SmokeUploadReadbackBuffer",
            },
        };
        const std::span<const asharia::VulkanRenderGraphBufferBinding> bufferBindingSpan{
            bufferBindings.data(),
            bufferBindings.size(),
        };

        int copyCallbackCount = 0;
        graph.addPass("UploadToDeviceLocal", asharia::kBasicTransferCopyBufferPassType)
            .readTransferBuffer("source", staging)
            .writeBuffer("target", device)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBuffer("source", "target");
            })
            .execute([&frame, bufferBindingSpan,
                      &copyCallbackCount](asharia::RenderGraphPassContext pass) {
                ++copyCallbackCount;
                return recordSmokeBufferCopyPass(frame, pass, bufferBindingSpan);
            });
        graph.addPass("ReadbackDeviceLocal", asharia::kBasicTransferCopyBufferPassType)
            .readTransferBuffer("source", device)
            .writeBuffer("target", readback)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBuffer("source", "target");
            })
            .execute([&frame, bufferBindingSpan,
                      &copyCallbackCount](asharia::RenderGraphPassContext pass) {
                ++copyCallbackCount;
                return recordSmokeBufferCopyPass(frame, pass, bufferBindingSpan);
            });

        const asharia::RenderGraphSchemaRegistry schemas =
            asharia::basicRenderGraphSchemaRegistry();
        auto compiled = graph.compile(schemas);
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }
        if (!validateSmokeBufferUploadPlan(graph, *compiled)) {
            return std::unexpected{
                smokeRenderGraphError("Buffer upload smoke failed RenderGraph plan validation.")};
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            return std::unexpected{std::move(executed.error())};
        }
        if (copyCallbackCount != 2) {
            return std::unexpected{
                smokeRenderGraphError("Buffer upload smoke did not execute both copy passes.")};
        }

        auto finalTransitions = asharia::recordRenderGraphBufferTransitions(
            frame, compiled->finalBufferTransitions, bufferBindingSpan);
        if (!finalTransitions) {
            return std::unexpected{std::move(finalTransitions.error())};
        }

        return asharia::VulkanFrameRecordResult{
            .waitStageMask = clear->waitStageMask | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

    struct SmokeBufferUploadStats {
        asharia::VulkanBufferStats staging;
        asharia::VulkanBufferStats device;
        asharia::VulkanBufferStats readback;
        VkDeviceSize byteCount{};
    };

    [[nodiscard]] bool validateSmokeBufferUploadStats(SmokeBufferUploadStats stats) {
        if (stats.staging.hostUploadCreated != 1 || stats.staging.uploadCalls != 1 ||
            stats.staging.uploadedBytes != stats.byteCount) {
            asharia::logError("Buffer upload smoke staging buffer stats were unexpected.");
            return false;
        }
        if (stats.device.deviceLocalCreated != 1 ||
            stats.device.allocatedBytes != stats.byteCount) {
            asharia::logError("Buffer upload smoke device-local buffer stats were unexpected.");
            return false;
        }
        if (stats.readback.hostReadbackCreated != 1 ||
            stats.readback.allocatedBytes != stats.byteCount) {
            asharia::logError("Buffer upload smoke readback buffer stats were unexpected.");
            return false;
        }
        return true;
    }

    int runSmokeBufferUpload() {
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
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Buffer Upload Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Buffer Upload Smoke",
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

        const std::array<std::byte, kSmokeBufferUploadByteCount> uploadBytes =
            smokeBufferUploadPayload();
        constexpr auto kUploadByteCount = static_cast<VkDeviceSize>(kSmokeBufferUploadByteCount);
        auto stagingBuffer = asharia::VulkanBuffer::create(asharia::VulkanBufferDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .size = kUploadByteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .memoryUsage = asharia::VulkanBufferMemoryUsage::HostUpload,
        });
        if (!stagingBuffer) {
            asharia::logError(stagingBuffer.error().message);
            return EXIT_FAILURE;
        }
        auto deviceBuffer = asharia::VulkanBuffer::create(asharia::VulkanBufferDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .size = kUploadByteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .memoryUsage = asharia::VulkanBufferMemoryUsage::DeviceLocal,
        });
        if (!deviceBuffer) {
            asharia::logError(deviceBuffer.error().message);
            return EXIT_FAILURE;
        }
        auto readbackBuffer = asharia::VulkanBuffer::create(asharia::VulkanBufferDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .size = kUploadByteCount,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .memoryUsage = asharia::VulkanBufferMemoryUsage::HostReadback,
        });
        if (!readbackBuffer) {
            asharia::logError(readbackBuffer.error().message);
            return EXIT_FAILURE;
        }

        auto uploaded = stagingBuffer->upload(
            std::span<const std::byte>{uploadBytes.data(), uploadBytes.size()});
        if (!uploaded) {
            asharia::logError(uploaded.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.02F, 0.025F, 0.03F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const auto currentFramebuffer = window->framebufferExtent();
        frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
        auto status =
            frameLoop->renderFrame([&stagingBuffer, &deviceBuffer, &readbackBuffer](
                                       const asharia::VulkanFrameRecordContext& recordContext) {
                return recordSmokeBufferUploadFrame(recordContext, *stagingBuffer, *deviceBuffer,
                                                    *readbackBuffer);
            });
        if (!status) {
            asharia::logError(status.error().message);
            return EXIT_FAILURE;
        }
        if (*status == asharia::VulkanFrameStatus::OutOfDate) {
            asharia::logError("Swapchain remained out of date during buffer upload smoke.");
            return EXIT_FAILURE;
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before buffer upload readback: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        std::array<std::byte, kSmokeBufferUploadByteCount> readbackBytes{};
        auto readback =
            readbackBuffer->read(std::span<std::byte>{readbackBytes.data(), readbackBytes.size()});
        if (!readback) {
            asharia::logError(readback.error().message);
            return EXIT_FAILURE;
        }
        if (readbackBytes != uploadBytes) {
            asharia::logError(
                "Buffer upload smoke read back data that does not match the upload payload.");
            return EXIT_FAILURE;
        }
        if (!validateSmokeBufferUploadStats(SmokeBufferUploadStats{
                .staging = stagingBuffer->stats(),
                .device = deviceBuffer->stats(),
                .readback = readbackBuffer->stats(),
                .byteCount = kUploadByteCount,
            })) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Buffer upload smoke")) {
            return EXIT_FAILURE;
        }

        std::cout << "Buffer upload bytes copied through RenderGraph: " << kUploadByteCount << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    constexpr VkExtent2D kSmokeTextureUploadExtent{.width = 1, .height = 1};
    constexpr VkFormat kSmokeTextureUploadFormat = VK_FORMAT_R8G8B8A8_SRGB;
    constexpr std::size_t kSmokeTextureUploadPixelBytes =
        static_cast<std::size_t>(kSmokeTextureUploadExtent.width) *
        static_cast<std::size_t>(kSmokeTextureUploadExtent.height) * 4U;
    constexpr std::string_view kSmokeTextureCopyBufferToImagePassType =
        "smoke.transfer-copy-buffer-to-image";
    constexpr std::string_view kSmokeTextureCopyImageToBufferPassType =
        "smoke.transfer-copy-image-to-buffer";

    struct SmokeTextureProduct {
        asharia::asset::SourceAssetRecord source;
        asharia::asset::AssetProductRecord product;
        std::vector<std::uint8_t> pixelBytes;
    };

    [[nodiscard]] std::uint64_t smokeHashBytes(std::span<const std::uint8_t> bytes) noexcept {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const std::uint8_t byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] std::vector<std::uint8_t> smokeTexturePngSourceBytes() {
        return {
            0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x0DU,
            0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U,
            0x08U, 0x06U, 0x00U, 0x00U, 0x00U, 0x1FU, 0x15U, 0xC4U, 0x89U, 0x00U, 0x00U, 0x00U,
            0x0DU, 0x49U, 0x44U, 0x41U, 0x54U, 0x78U, 0xDAU, 0x63U, 0x10U, 0x50U, 0x30U, 0xF8U,
            0x0FU, 0x00U, 0x02U, 0x04U, 0x01U, 0x60U, 0x52U, 0xE2U, 0xA9U, 0x61U, 0x00U, 0x00U,
            0x00U, 0x00U, 0x49U, 0x45U, 0x4EU, 0x44U, 0xAEU, 0x42U, 0x60U, 0x82U,
        };
    }

    [[nodiscard]] std::vector<std::uint8_t> smokeTexturePixels() {
        return {0x10U, 0x20U, 0x30U, 0xFFU};
    }

    [[nodiscard]] asharia::Result<SmokeTextureProduct> createSmokeTextureProduct() {
        auto guid = asharia::asset::parseAssetGuid("10000000-2000-3000-4000-500000000001");
        if (!guid) {
            return std::unexpected{std::move(guid.error())};
        }

        std::vector<std::uint8_t> pngSourceBytes = smokeTexturePngSourceBytes();
        const std::vector<asharia::asset::AssetImportSetting> settings{
            {
                .key = std::string{asharia::asset::kTextureImportProfileSettingKey},
                .value = std::string{asharia::asset::kTextureImportProfileTexture2D},
            },
            {
                .key = std::string{asharia::asset::kTextureImportSettingsVersionSettingKey},
                .value = std::to_string(asharia::asset::kTextureImportContractSettingsVersion),
            },
            {
                .key = std::string{asharia::asset::kTextureImportFormatSettingKey},
                .value = std::string{asharia::asset::kTextureImportFormatRgba8Srgb},
            },
        };
        const asharia::asset::AssetTextureImporterDescriptor importer =
            asharia::asset::makePngTextureImporterDescriptor();
        const asharia::asset::SourceAssetRecord source{
            .guid = *guid,
            .assetType = asharia::asset::makeAssetTypeId(asharia::asset::kTextureRoleTexture2D),
            .assetTypeName = std::string{asharia::asset::kTextureRoleTexture2D},
            .sourcePath = "textures/smoke-texture.png",
            .importerId = asharia::asset::makeImporterId(importer.importerName),
            .importerName = importer.importerName,
            .importerVersion = importer.importerVersion,
            .sourceHash = smokeHashBytes(
                std::span<const std::uint8_t>{pngSourceBytes.data(), pngSourceBytes.size()}),
            .settingsHash = asharia::asset::hashAssetImportSettings(
                std::span<const asharia::asset::AssetImportSetting>{settings.data(),
                                                                    settings.size()}),
        };
        if (auto valid = asharia::asset::validateSourceAssetRecord(source); !valid) {
            return std::unexpected{std::move(valid.error())};
        }

        constexpr std::string_view kTargetProfile = "vulkan-smoke";
        const std::uint64_t targetProfileHash =
            asharia::asset::makeAssetTargetProfileHash(kTargetProfile);
        const std::uint64_t dependencyHash = asharia::asset::hashAssetDependencies(
            std::span<const asharia::asset::AssetDependency>{});
        const asharia::asset::AssetProductKey productKey =
            asharia::asset::makeAssetProductKey(source, dependencyHash, targetProfileHash);
        const std::string productPath =
            asharia::asset::makeAssetImportProductPath(productKey, kTargetProfile);

        asharia::asset::AssetImportPlanResult plan{
            .targetProfile = std::string{kTargetProfile},
            .targetProfileHash = targetProfileHash,
            .requests =
                {
                    asharia::asset::AssetImportRequest{
                        .source = source,
                        .settings = settings,
                        .dependencies = {},
                        .productKey = productKey,
                        .relativeProductPath = productPath,
                        .reason = asharia::asset::AssetImportRequestReason::MissingProduct,
                    },
                },
            .cacheHits = {},
            .diagnostics = {},
        };

        const std::filesystem::path productRoot =
            std::filesystem::temp_directory_path() /
            ("asharia-texture-upload-smoke-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = std::move(plan),
                .existingManifest = {},
                .sourceBytes =
                    {
                        asharia::asset::AssetProductSourceBytes{
                            .sourcePath = source.sourcePath,
                            .bytes = pngSourceBytes,
                        },
                    },
                .productOutputRoot = productRoot,
                .productManifestOutputPath = productRoot / "asset-products.json",
            });
        if (!execution.succeeded() || execution.writtenProducts.size() != 1U ||
            !execution.manifestWritten) {
            std::string message = "Texture upload smoke product execution failed.";
            if (!execution.diagnostics.empty()) {
                message += " ";
                message += execution.diagnostics.front().message;
            }
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Asset, 0, std::move(message)}};
        }

        const asharia::asset::AssetProductWrite& writtenProduct = execution.writtenProducts.front();
        auto payload =
            asharia::asset::readTexture2DProductPayload(asharia::asset::AssetProductBlobReadRequest{
                .productFilePath = writtenProduct.productFilePath,
                .relativeProductPath = writtenProduct.product.relativeProductPath,
            });
        if (!payload) {
            return std::unexpected{std::move(payload.error())};
        }
        const std::vector<std::uint8_t> expectedPixels = smokeTexturePixels();
        if (payload->sourcePath != source.sourcePath ||
            payload->productTypeName != asharia::asset::kTextureRoleTexture2D ||
            payload->format != asharia::asset::AssetTextureImportFormat::Rgba8Srgb ||
            payload->width != kSmokeTextureUploadExtent.width ||
            payload->height != kSmokeTextureUploadExtent.height ||
            payload->payload != expectedPixels) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Asset, 0,
                               "Texture upload smoke product payload changed during execution."}};
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(productRoot, cleanupError);

        return SmokeTextureProduct{
            .source = source,
            .product = writtenProduct.product,
            .pixelBytes = std::move(payload->payload),
        };
    }

    [[nodiscard]] asharia::RenderGraphSchemaRegistry smokeTextureUploadSchemas() {
        asharia::RenderGraphSchemaRegistry schemas = asharia::basicRenderGraphSchemaRegistry();
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kSmokeTextureCopyBufferToImagePassType},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyBufferToImage},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kSmokeTextureCopyImageToBufferPassType},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::TransferRead,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferWrite,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyImageToBuffer},
        });
        return schemas;
    }

    [[nodiscard]] asharia::Result<void> recordSmokeTextureCopyPass(
        const asharia::VulkanFrameRecordContext& frame, asharia::RenderGraphPassContext pass,
        std::span<const asharia::VulkanRenderGraphImageBinding> imageBindings,
        std::span<const asharia::VulkanRenderGraphBufferBinding> bufferBindings,
        VkExtent2D extent) {
        [[maybe_unused]] const auto timestamp =
            asharia::VulkanTimestampScope::begin(frame, pass.name);
        [[maybe_unused]] const auto debugLabel = asharia::VulkanDebugLabelScope::begin(
            frame, asharia::renderGraphPassDebugLabel(pass, imageBindings, bufferBindings));

        auto imageTransitions =
            asharia::recordRenderGraphTransitions(frame, pass.transitionsBefore, imageBindings);
        if (!imageTransitions) {
            return std::unexpected{std::move(imageTransitions.error())};
        }
        auto bufferTransitions = asharia::recordRenderGraphBufferTransitions(
            frame, pass.bufferTransitionsBefore, bufferBindings);
        if (!bufferTransitions) {
            return std::unexpected{std::move(bufferTransitions.error())};
        }
        if (pass.commands.size() != 1) {
            return std::unexpected{
                smokeRenderGraphError("Texture upload copy pass expected one command.")};
        }

        const asharia::RenderGraphCommand& command = pass.commands.front();
        if (command.kind == asharia::RenderGraphCommandKind::CopyBufferToImage) {
            auto source = asharia::findVulkanRenderGraphBufferTransferRead(pass, command.name,
                                                                           bufferBindings);
            if (!source) {
                return std::unexpected{std::move(source.error())};
            }
            auto target = asharia::findVulkanRenderGraphTransferWrite(pass, command.secondaryName,
                                                                      imageBindings);
            if (!target) {
                return std::unexpected{std::move(target.error())};
            }
            if (source->size < static_cast<VkDeviceSize>(kSmokeTextureUploadPixelBytes)) {
                return std::unexpected{
                    smokeRenderGraphError("Texture upload staging buffer is too small.")};
            }

            VkBufferImageCopy copy{};
            copy.bufferOffset = source->offset;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = target->aspectMask;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = VkExtent3D{
                .width = extent.width,
                .height = extent.height,
                .depth = 1,
            };
            vkCmdCopyBufferToImage(frame.commandBuffer, source->vulkanBuffer, target->vulkanImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            return {};
        }

        if (command.kind == asharia::RenderGraphCommandKind::CopyImageToBuffer) {
            auto source =
                asharia::findVulkanRenderGraphTransferRead(pass, command.name, imageBindings);
            if (!source) {
                return std::unexpected{std::move(source.error())};
            }
            auto target = asharia::findVulkanRenderGraphBufferTransferWrite(
                pass, command.secondaryName, bufferBindings);
            if (!target) {
                return std::unexpected{std::move(target.error())};
            }
            if (target->size < static_cast<VkDeviceSize>(kSmokeTextureUploadPixelBytes)) {
                return std::unexpected{
                    smokeRenderGraphError("Texture upload readback buffer is too small.")};
            }

            VkBufferImageCopy copy{};
            copy.bufferOffset = target->offset;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = source->aspectMask;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = VkExtent3D{
                .width = extent.width,
                .height = extent.height,
                .depth = 1,
            };
            vkCmdCopyImageToBuffer(frame.commandBuffer, source->vulkanImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target->vulkanBuffer, 1,
                                   &copy);
            return {};
        }

        return std::unexpected{
            smokeRenderGraphError("Texture upload copy pass received an unexpected command.")};
    }

    [[nodiscard]] bool
    validateSmokeTextureUploadPlan(const asharia::RenderGraph& graph,
                                   const asharia::RenderGraphCompileResult& compiled) {
        if (compiled.passes.size() != 2 ||
            std::string_view{compiled.passes[0].type} != kSmokeTextureCopyBufferToImagePassType ||
            std::string_view{compiled.passes[1].type} != kSmokeTextureCopyImageToBufferPassType ||
            compiled.passes[0].commands.size() != 1 || compiled.passes[1].commands.size() != 1 ||
            compiled.passes[0].commands.front().kind !=
                asharia::RenderGraphCommandKind::CopyBufferToImage ||
            compiled.passes[1].commands.front().kind !=
                asharia::RenderGraphCommandKind::CopyImageToBuffer ||
            compiled.dependencies.size() != 1 || compiled.finalTransitions.size() != 1 ||
            compiled.finalBufferTransitions.size() != 1 ||
            compiled.finalTransitions.front().newState !=
                asharia::RenderGraphImageState::ShaderRead ||
            compiled.finalTransitions.front().newShaderStage !=
                asharia::RenderGraphShaderStage::Fragment ||
            compiled.finalBufferTransitions.front().newState !=
                asharia::RenderGraphBufferState::HostRead) {
            asharia::logError("Texture upload smoke produced an unexpected RenderGraph plan.");
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot diagnostics =
            graph.diagnosticsSnapshot(compiled);
        const bool sawUpload = std::ranges::any_of(
            diagnostics.commands, [](const asharia::RenderGraphDiagnosticsCommandNode& command) {
                return command.kind == asharia::RenderGraphCommandKind::CopyBufferToImage &&
                       command.detail == "source -> target";
            });
        const bool sawReadback = std::ranges::any_of(
            diagnostics.commands, [](const asharia::RenderGraphDiagnosticsCommandNode& command) {
                return command.kind == asharia::RenderGraphCommandKind::CopyImageToBuffer &&
                       command.detail == "source -> target";
            });
        if (!sawUpload || !sawReadback) {
            asharia::logError(
                "Texture upload smoke diagnostics did not expose both texture copy commands.");
            return false;
        }
        return true;
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult> recordSmokeTextureUploadFrame(
        const asharia::VulkanFrameRecordContext& frame, asharia::VulkanBuffer& stagingBuffer,
        asharia::VulkanImage& textureImage, asharia::VulkanImageView& textureImageView,
        asharia::VulkanBuffer& readbackBuffer) {
        auto clear = asharia::recordBasicClearFrame(frame);
        if (!clear) {
            return std::unexpected{std::move(clear.error())};
        }

        asharia::RenderGraph graph;
        const auto staging = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "SmokeTextureProductStaging",
            .byteSize = stagingBuffer.size(),
            .initialState = asharia::RenderGraphBufferState::TransferRead,
            .finalState = asharia::RenderGraphBufferState::TransferRead,
        });
        const auto texture = graph.importImage(asharia::RenderGraphImageDesc{
            .name = "SmokeTextureProductImage",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent =
                asharia::RenderGraphExtent2D{
                    .width = kSmokeTextureUploadExtent.width,
                    .height = kSmokeTextureUploadExtent.height,
                },
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::ShaderRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        const auto readback = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "SmokeTextureProductReadback",
            .byteSize = readbackBuffer.size(),
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::HostRead,
        });

        const std::array imageBindings{
            asharia::VulkanRenderGraphImageBinding{
                .image = texture,
                .vulkanImage = textureImage.handle(),
                .vulkanImageView = textureImageView.handle(),
                .aspectMask = textureImage.aspectMask(),
                .debugName = "SmokeTextureProductImage",
            },
        };
        const std::array bufferBindings{
            asharia::VulkanRenderGraphBufferBinding{
                .buffer = staging,
                .vulkanBuffer = stagingBuffer.handle(),
                .offset = 0,
                .size = stagingBuffer.size(),
                .debugName = "SmokeTextureProductStaging",
            },
            asharia::VulkanRenderGraphBufferBinding{
                .buffer = readback,
                .vulkanBuffer = readbackBuffer.handle(),
                .offset = 0,
                .size = readbackBuffer.size(),
                .debugName = "SmokeTextureProductReadback",
            },
        };
        const std::span<const asharia::VulkanRenderGraphImageBinding> imageBindingSpan{
            imageBindings.data(),
            imageBindings.size(),
        };
        const std::span<const asharia::VulkanRenderGraphBufferBinding> bufferBindingSpan{
            bufferBindings.data(),
            bufferBindings.size(),
        };

        int copyCallbackCount = 0;
        graph.addPass("UploadTextureProduct", std::string{kSmokeTextureCopyBufferToImagePassType})
            .readTransferBuffer("source", staging)
            .writeTransfer("target", texture)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBufferToImage("source", "target");
            })
            .execute([&frame, imageBindingSpan, bufferBindingSpan,
                      &copyCallbackCount](asharia::RenderGraphPassContext pass) {
                ++copyCallbackCount;
                return recordSmokeTextureCopyPass(frame, pass, imageBindingSpan, bufferBindingSpan,
                                                  kSmokeTextureUploadExtent);
            });
        graph.addPass("ReadbackTextureProduct", std::string{kSmokeTextureCopyImageToBufferPassType})
            .readTransfer("source", texture)
            .writeBuffer("target", readback)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImageToBuffer("source", "target");
            })
            .execute([&frame, imageBindingSpan, bufferBindingSpan,
                      &copyCallbackCount](asharia::RenderGraphPassContext pass) {
                ++copyCallbackCount;
                return recordSmokeTextureCopyPass(frame, pass, imageBindingSpan, bufferBindingSpan,
                                                  kSmokeTextureUploadExtent);
            });

        const asharia::RenderGraphSchemaRegistry schemas = smokeTextureUploadSchemas();
        auto compiled = graph.compile(schemas);
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }
        if (!validateSmokeTextureUploadPlan(graph, *compiled)) {
            return std::unexpected{
                smokeRenderGraphError("Texture upload smoke failed RenderGraph plan validation.")};
        }

        auto namedImage = asharia::setVulkanRenderGraphImageDebugNames(frame, imageBindings[0]);
        if (!namedImage) {
            return std::unexpected{std::move(namedImage.error())};
        }
        for (const asharia::VulkanRenderGraphBufferBinding& binding : bufferBindings) {
            auto namedBuffer = asharia::setVulkanRenderGraphBufferDebugName(frame, binding);
            if (!namedBuffer) {
                return std::unexpected{std::move(namedBuffer.error())};
            }
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            return std::unexpected{std::move(executed.error())};
        }
        if (copyCallbackCount != 2) {
            return std::unexpected{
                smokeRenderGraphError("Texture upload smoke did not execute both copy passes.")};
        }

        auto finalImageTransitions = asharia::recordRenderGraphTransitions(
            frame, compiled->finalTransitions, imageBindingSpan);
        if (!finalImageTransitions) {
            return std::unexpected{std::move(finalImageTransitions.error())};
        }
        auto finalBufferTransitions = asharia::recordRenderGraphBufferTransitions(
            frame, compiled->finalBufferTransitions, bufferBindingSpan);
        if (!finalBufferTransitions) {
            return std::unexpected{std::move(finalBufferTransitions.error())};
        }

        return asharia::VulkanFrameRecordResult{
            .waitStageMask = clear->waitStageMask | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

    [[nodiscard]] bool
    validateSmokeTextureUploadReadback(std::span<const std::byte> readbackBytes,
                                       std::span<const std::uint8_t> productBytes) {
        if (readbackBytes.size() != productBytes.size()) {
            asharia::logError("Texture upload smoke readback byte count changed.");
            return false;
        }
        for (std::size_t index = 0; index < productBytes.size(); ++index) {
            if (readbackBytes[index] != std::byte{productBytes[index]}) {
                asharia::logError("Texture upload smoke readback does not match product bytes.");
                return false;
            }
        }
        return true;
    }

    int runSmokeTextureUpload() {
        auto product = createSmokeTextureProduct();
        if (!product) {
            asharia::logError(product.error().message);
            return EXIT_FAILURE;
        }

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
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Texture Upload Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Texture Upload Smoke",
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

        auto stagingBuffer = asharia::VulkanBuffer::create(asharia::VulkanBufferDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .size = static_cast<VkDeviceSize>(product->pixelBytes.size()),
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .memoryUsage = asharia::VulkanBufferMemoryUsage::HostUpload,
        });
        if (!stagingBuffer) {
            asharia::logError(stagingBuffer.error().message);
            return EXIT_FAILURE;
        }
        const std::span<const std::uint8_t> productPixelBytes{product->pixelBytes.data(),
                                                              product->pixelBytes.size()};
        auto uploaded = stagingBuffer->upload(std::as_bytes(productPixelBytes));
        if (!uploaded) {
            asharia::logError(uploaded.error().message);
            return EXIT_FAILURE;
        }

        auto textureImage = asharia::VulkanImage::create(asharia::VulkanImageDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .format = kSmokeTextureUploadFormat,
            .extent = kSmokeTextureUploadExtent,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        });
        if (!textureImage) {
            asharia::logError(textureImage.error().message);
            return EXIT_FAILURE;
        }
        auto textureImageView = asharia::VulkanImageView::create(asharia::VulkanImageViewDesc{
            .device = context->device(),
            .image = textureImage->handle(),
            .format = textureImage->format(),
            .aspectMask = textureImage->aspectMask(),
        });
        if (!textureImageView) {
            asharia::logError(textureImageView.error().message);
            return EXIT_FAILURE;
        }
        const asharia::VulkanSampledTextureView sampledTexture{
            .image = textureImage->handle(),
            .imageView = textureImageView->handle(),
            .format = textureImage->format(),
            .extent = textureImage->extent(),
            .aspectMask = textureImage->aspectMask(),
            .sampledLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        if (sampledTexture.image == VK_NULL_HANDLE || sampledTexture.imageView == VK_NULL_HANDLE ||
            sampledTexture.format != kSmokeTextureUploadFormat ||
            sampledTexture.extent.width != kSmokeTextureUploadExtent.width ||
            sampledTexture.extent.height != kSmokeTextureUploadExtent.height) {
            asharia::logError("Texture upload smoke did not create a sampled texture view.");
            return EXIT_FAILURE;
        }

        auto readbackBuffer = asharia::VulkanBuffer::create(asharia::VulkanBufferDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .size = static_cast<VkDeviceSize>(product->pixelBytes.size()),
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .memoryUsage = asharia::VulkanBufferMemoryUsage::HostReadback,
        });
        if (!readbackBuffer) {
            asharia::logError(readbackBuffer.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = asharia::VulkanFrameLoop::create(
            *context, asharia::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.012F, 0.018F, 0.024F, 1.0F}},
                      });
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const auto currentFramebuffer = window->framebufferExtent();
        frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
        auto status = frameLoop->renderFrame(
            [&stagingBuffer, &textureImage, &textureImageView,
             &readbackBuffer](const asharia::VulkanFrameRecordContext& recordContext) {
                return recordSmokeTextureUploadFrame(recordContext, *stagingBuffer, *textureImage,
                                                     *textureImageView, *readbackBuffer);
            });
        if (!status) {
            asharia::logError(status.error().message);
            return EXIT_FAILURE;
        }
        if (*status == asharia::VulkanFrameStatus::OutOfDate) {
            asharia::logError("Swapchain remained out of date during texture upload smoke.");
            return EXIT_FAILURE;
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before texture upload readback: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        std::vector<std::byte> readbackBytes(product->pixelBytes.size());
        auto readback =
            readbackBuffer->read(std::span<std::byte>{readbackBytes.data(), readbackBytes.size()});
        if (!readback) {
            asharia::logError(readback.error().message);
            return EXIT_FAILURE;
        }
        if (!validateSmokeTextureUploadReadback(readbackBytes, product->pixelBytes)) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Texture upload smoke")) {
            return EXIT_FAILURE;
        }

        std::cout << "Texture upload product bytes copied through RenderGraph: "
                  << product->pixelBytes.size() << " from " << product->product.relativeProductPath
                  << '\n';
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

    constexpr asharia::BasicDrawResourceKey kSmokeSceneMeshA{.value = 0xA501U};
    constexpr asharia::BasicDrawResourceKey kSmokeSceneMeshB{.value = 0xA502U};
    constexpr asharia::BasicDrawResourceKey kSmokeSceneMaterial{.value = 0xB501U};

    [[nodiscard]] constexpr asharia::BasicTransformMatrix3D
    smokeSceneModelMatrix(const asharia::TransformComponent& transform) {
        return asharia::BasicTransformMatrix3D{
            transform.scale.x,
            0.0F,
            0.0F,
            transform.position.x,
            0.0F,
            transform.scale.y,
            0.0F,
            transform.position.y,
            0.0F,
            0.0F,
            transform.scale.z,
            transform.position.z,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
        };
    }

    [[nodiscard]] constexpr asharia::BasicDrawSourceId
    smokeSceneSourceId(asharia::EntityId entity) {
        return asharia::BasicDrawSourceId{
            .index = entity.index,
            .generation = entity.generation,
        };
    }

    [[nodiscard]] asharia::Result<std::array<asharia::BasicDrawListItem, 2>>
    smokeSceneDrawPackets() {
        asharia::World world;
        auto left = world.createEntity("SceneDrawPacketLeft");
        if (!left) {
            return std::unexpected{std::move(left.error())};
        }
        auto right = world.createEntity("SceneDrawPacketRight");
        if (!right) {
            return std::unexpected{std::move(right.error())};
        }

        auto leftTransform = world.setTransform(
            *left, asharia::TransformComponent{.position = {.x = -0.72F, .y = 0.0F, .z = 3.0F}});
        if (!leftTransform) {
            return std::unexpected{std::move(leftTransform.error())};
        }
        auto rightTransform = world.setTransform(
            *right, asharia::TransformComponent{.position = {.x = 0.72F, .y = 0.0F, .z = 3.0F}});
        if (!rightTransform) {
            return std::unexpected{std::move(rightTransform.error())};
        }

        const asharia::TransformComponent* leftStored = world.tryGetTransform(*left);
        const asharia::TransformComponent* rightStored = world.tryGetTransform(*right);
        if (leftStored == nullptr || rightStored == nullptr) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::Scene,
                0,
                "Scene draw packet smoke failed to read entity transforms",
            }};
        }

        return std::array{
            asharia::BasicDrawListItem{
                .drawItem = asharia::basicIndexedCubeDrawItem(),
                .modelMatrix = smokeSceneModelMatrix(*leftStored),
                .context =
                    asharia::BasicDrawPacketContext{
                        .sourceObject = smokeSceneSourceId(*left),
                        .meshResource = kSmokeSceneMeshA,
                        .materialResource = kSmokeSceneMaterial,
                    },
            },
            asharia::BasicDrawListItem{
                .drawItem = asharia::basicIndexedCubeDrawItem(),
                .modelMatrix = smokeSceneModelMatrix(*rightStored),
                .context =
                    asharia::BasicDrawPacketContext{
                        .sourceObject = smokeSceneSourceId(*right),
                        .meshResource = kSmokeSceneMeshB,
                        .materialResource = kSmokeSceneMaterial,
                    },
            },
        };
    }

    [[nodiscard]] bool validateFullscreenTextureOverlayDiagnostics(
        const asharia::BasicRenderViewDiagnostics& diagnostics, bool diagnosticsRecorded) {
        if (!diagnosticsRecorded) {
            asharia::logError("Fullscreen texture smoke did not record overlay diagnostics.");
            return false;
        }
        if (diagnostics.scene.drawItemCount != 2U) {
            asharia::logError("Fullscreen texture smoke did not record scene input diagnostics.");
            return false;
        }
        if (diagnostics.scene.drawPacketContexts.size() != 2U ||
            diagnostics.scene.drawPacketContexts[0].sourceObject.index != 1U ||
            diagnostics.scene.drawPacketContexts[0].sourceObject.generation != 1U ||
            diagnostics.scene.drawPacketContexts[0].meshResource != kSmokeSceneMeshA ||
            diagnostics.scene.drawPacketContexts[0].materialResource != kSmokeSceneMaterial ||
            diagnostics.scene.drawPacketContexts[1].sourceObject.index != 2U ||
            diagnostics.scene.drawPacketContexts[1].sourceObject.generation != 1U ||
            diagnostics.scene.drawPacketContexts[1].meshResource != kSmokeSceneMeshB ||
            diagnostics.scene.drawPacketContexts[1].materialResource != kSmokeSceneMaterial) {
            asharia::logError(
                "Fullscreen texture smoke did not preserve scene draw packet diagnostics.");
            return false;
        }
        const auto sceneInputPass = std::ranges::find_if(
            diagnostics.renderGraph.passes,
            [](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                return pass.type == asharia::kBasicRenderViewSceneInputsPassType;
            });
        if (sceneInputPass == diagnostics.renderGraph.passes.end() ||
            !sceneInputPass->hasSideEffects || sceneInputPass->commandCount != 1U) {
            asharia::logError("Fullscreen texture smoke did not record a scene input marker pass.");
            return false;
        }
        const auto sceneInputCommand = std::ranges::find_if(
            diagnostics.renderGraph.commands,
            [sceneInputPass](const asharia::RenderGraphDiagnosticsCommandNode& command) {
                return command.passName == sceneInputPass->name &&
                       command.kind == asharia::RenderGraphCommandKind::SetInt &&
                       command.detail == "SceneDrawItemCount = 2";
            });
        if (sceneInputCommand == diagnostics.renderGraph.commands.end()) {
            asharia::logError(
                "Fullscreen texture smoke did not record scene input command diagnostics.");
            return false;
        }
        const auto sceneInputEvent = std::ranges::find_if(
            diagnostics.executionEvents, [](const asharia::BasicRenderViewExecutionEvent& event) {
                return event.kind == asharia::BasicRenderViewExecutionEventKind::RenderViewInput &&
                       event.label == "BindRenderViewSceneInputs";
            });
        if (sceneInputEvent == diagnostics.executionEvents.end()) {
            asharia::logError("Fullscreen texture smoke did not record a scene input event.");
            return false;
        }
        const auto overlayPass =
            std::ranges::find_if(diagnostics.renderGraph.passes,
                                 [](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                                     return pass.type == asharia::kBasicRenderViewOverlayPassType;
                                 });
        if (overlayPass == diagnostics.renderGraph.passes.end()) {
            asharia::logError("Fullscreen texture smoke did not record a debug-line overlay pass.");
            return false;
        }
        const auto drawLines = std::ranges::find_if(
            diagnostics.executionEvents, [](const asharia::BasicRenderViewExecutionEvent& event) {
                return event.kind == asharia::BasicRenderViewExecutionEventKind::Draw &&
                       event.label == "DrawDebugWorldLines";
            });
        if (drawLines == diagnostics.executionEvents.end() || drawLines->draw.vertexCount != 2U) {
            asharia::logError("Fullscreen texture smoke did not record a debug-line draw event.");
            return false;
        }
        return true;
    }

    constexpr std::string_view kInvalidSceneInputSmokeExpectedError =
        "RenderView scene input item must declare vertices or indices and a non-zero instance "
        "count";

    struct SmokeFullscreenTextureState {
        asharia::BasicRenderViewDiagnostics overlayDiagnostics;
        bool overlayDiagnosticsRecorded{};
        bool invalidSceneInputRejected{};
        bool invalidSceneInputContextReported{};
    };

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordSmokeFullscreenTextureOverlayFrame(const asharia::VulkanFrameRecordContext& recordContext,
                                             asharia::BasicFullscreenTextureRenderer& renderer,
                                             SmokeFullscreenTextureState& state, int frame) {
        auto drawItems = smokeSceneDrawPackets();
        if (!drawItems) {
            return std::unexpected{std::move(drawItems.error())};
        }
        const std::array debugLines{
            asharia::BasicDebugWorldLine{
                .start = {-0.65F, 0.0F, 0.0F},
                .end = {0.65F, 0.0F, 0.0F},
                .color = {0.18F, 0.78F, 0.95F, 0.65F},
            },
        };
        auto recorded = renderer.recordViewFrame(
            recordContext,
            asharia::BasicRenderViewDesc{
                .target =
                    asharia::BasicRenderViewTarget{
                        .image = recordContext.image,
                        .imageView = recordContext.imageView,
                        .format = recordContext.format,
                        .extent = recordContext.extent,
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .finalUsage = asharia::BasicRenderViewTargetFinalUsage::Present,
                    },
                .viewKind = asharia::BasicRenderViewKind::Game,
                .camera = asharia::BasicRenderViewCamera{},
                .frameParams =
                    asharia::BasicRenderViewFrameParams{
                        .frameIndex = static_cast<std::uint64_t>(frame + 1),
                    },
                .scene =
                    asharia::BasicRenderViewSceneDesc{
                        .drawItems = std::span<const asharia::BasicDrawListItem>{drawItems->data(),
                                                                                 drawItems->size()},
                    },
                .overlay =
                    asharia::BasicRenderViewOverlayDesc{
                        .enabled = true,
                        .blendMode = asharia::BasicRenderViewOverlayBlendMode::Additive,
                        .worldGrid = {},
                        .debugWorldLines = debugLines,
                    },
                .viewName = "FullscreenTextureAdditiveOverlaySmoke",
                .diagnostics = &state.overlayDiagnostics,
            });
        if (recorded) {
            state.overlayDiagnosticsRecorded = true;
        }
        return recorded;
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordSmokeFullscreenTextureInvalidSceneFrame(
        const asharia::VulkanFrameRecordContext& recordContext,
        asharia::BasicFullscreenTextureRenderer& renderer, SmokeFullscreenTextureState& state,
        int frame) {
        constexpr std::array invalidDrawItems{
            asharia::BasicDrawListItem{
                .drawItem =
                    asharia::BasicDrawItem{
                        .vertexCount = 0,
                        .indexCount = 0,
                        .instanceCount = 1,
                    },
                .modelMatrix = asharia::basicIdentityTransform3D(),
                .context =
                    asharia::BasicDrawPacketContext{
                        .sourceObject = asharia::BasicDrawSourceId{.index = 1U, .generation = 1U},
                        .meshResource = kSmokeSceneMeshA,
                        .materialResource = kSmokeSceneMaterial,
                    },
            },
        };
        auto rejected = renderer.recordViewFrame(
            recordContext,
            asharia::BasicRenderViewDesc{
                .target =
                    asharia::BasicRenderViewTarget{
                        .image = recordContext.image,
                        .imageView = recordContext.imageView,
                        .format = recordContext.format,
                        .extent = recordContext.extent,
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .finalUsage = asharia::BasicRenderViewTargetFinalUsage::Present,
                    },
                .viewKind = asharia::BasicRenderViewKind::Game,
                .camera = asharia::BasicRenderViewCamera{},
                .frameParams =
                    asharia::BasicRenderViewFrameParams{
                        .frameIndex = static_cast<std::uint64_t>(frame + 1),
                    },
                .scene =
                    asharia::BasicRenderViewSceneDesc{
                        .drawItems = invalidDrawItems,
                    },
                .overlay = {},
                .viewName = "FullscreenTextureInvalidSceneInputSmoke",
            });
        if (rejected) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Fullscreen texture smoke accepted invalid scene input",
            }};
        }

        state.invalidSceneInputRejected =
            rejected.error().message.find(kInvalidSceneInputSmokeExpectedError) !=
            std::string::npos;
        state.invalidSceneInputContextReported =
            rejected.error().message.find("source object 1:1") != std::string::npos &&
            rejected.error().message.find("mesh resource 42241") != std::string::npos &&
            rejected.error().message.find("material resource 46337") != std::string::npos;
        if (!state.invalidSceneInputRejected) {
            return std::unexpected{std::move(rejected.error())};
        }
        if (!state.invalidSceneInputContextReported) {
            return std::unexpected{asharia::Error{
                asharia::ErrorDomain::RenderGraph,
                0,
                "Fullscreen texture smoke did not preserve invalid scene input context: " +
                    rejected.error().message,
            }};
        }
        return renderer.recordFrame(recordContext);
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordSmokeFullscreenTextureFrame(const asharia::VulkanFrameRecordContext& recordContext,
                                      asharia::BasicFullscreenTextureRenderer& renderer,
                                      SmokeFullscreenTextureState& state, int frame) {
        if (frame == 1) {
            return recordSmokeFullscreenTextureOverlayFrame(recordContext, renderer, state, frame);
        }
        if (frame == 2) {
            return recordSmokeFullscreenTextureInvalidSceneFrame(recordContext, renderer, state,
                                                                 frame);
        }
        return renderer.recordFrame(recordContext);
    }

    using SmokeGridVec3 = std::array<float, 3>;
    using SmokeGridMat4 = std::array<float, 16>;

    struct SmokeGridLookAtDesc {
        SmokeGridVec3 position{};
        SmokeGridVec3 target{};
        SmokeGridVec3 up{0.0F, 1.0F, 0.0F};
    };

    struct SmokeGridPerspectiveDesc {
        float verticalFovRadians{};
        float aspectRatio{1.0F};
        float nearPlane{0.1F};
        float farPlane{1000.0F};
    };

    struct SmokeRenderViewGridReadbackProbe {
        asharia::VulkanRenderTarget target;
        asharia::VulkanBuffer readback;
        std::vector<std::byte> pixels;
        asharia::BasicRenderViewDiagnostics diagnostics;
        bool diagnosticsRecorded{};
    };

    constexpr VkExtent2D kSmokeGridReadbackExtent{.width = 192, .height = 128};
    constexpr VkFormat kSmokeGridReadbackFormat = VK_FORMAT_B8G8R8A8_SRGB;
    constexpr std::uint64_t kSmokeGridReadbackBytesPerPixel = 4;
    constexpr std::size_t kSmokeGridReadbackProbeCount = 4;
    constexpr std::size_t kSmokeGridHighViewProbeIndex = 2;
    constexpr std::size_t kSmokeGridLowFarViewProbeIndex = 3;
    constexpr std::uint64_t kMinimumVisibleGridSpread = 20000;
    constexpr std::uint64_t kMinimumCameraDifference = 40000;

    [[nodiscard]] constexpr bool smokeGridProbeUsesStableBaseLod(std::size_t probeIndex) {
        return probeIndex != kSmokeGridHighViewProbeIndex;
    }

    [[nodiscard]] constexpr VkDeviceSize smokeGridReadbackByteCount() {
        return static_cast<VkDeviceSize>(kSmokeGridReadbackExtent.width) *
               kSmokeGridReadbackExtent.height * kSmokeGridReadbackBytesPerPixel;
    }

    [[nodiscard]] constexpr SmokeGridVec3 smokeGridSubtract(SmokeGridVec3 lhs, SmokeGridVec3 rhs) {
        return SmokeGridVec3{
            lhs[0] - rhs[0],
            lhs[1] - rhs[1],
            lhs[2] - rhs[2],
        };
    }

    [[nodiscard]] constexpr float smokeGridDot(SmokeGridVec3 lhs, SmokeGridVec3 rhs) {
        return (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]);
    }

    [[nodiscard]] constexpr SmokeGridVec3 smokeGridCross(SmokeGridVec3 lhs, SmokeGridVec3 rhs) {
        return SmokeGridVec3{
            (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]),
            (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]),
            (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]),
        };
    }

    [[nodiscard]] SmokeGridVec3 smokeGridNormalize(SmokeGridVec3 value) {
        const float length = std::sqrt(smokeGridDot(value, value));
        if (length <= 0.0F) {
            return SmokeGridVec3{};
        }
        return SmokeGridVec3{value[0] / length, value[1] / length, value[2] / length};
    }

    [[nodiscard]] constexpr float smokeGridMat4At(const SmokeGridMat4& matrix, std::size_t row,
                                                  std::size_t column) {
        return matrix.at((row * 4U) + column);
    }

    [[nodiscard]] SmokeGridMat4 smokeGridMultiply(SmokeGridMat4 lhs, SmokeGridMat4 rhs) {
        SmokeGridMat4 result{};
        for (std::size_t row = 0; row < 4U; ++row) {
            for (std::size_t column = 0; column < 4U; ++column) {
                float value = 0.0F;
                for (std::size_t index = 0; index < 4U; ++index) {
                    value += smokeGridMat4At(lhs, row, index) * smokeGridMat4At(rhs, index, column);
                }
                result.at((row * 4U) + column) = value;
            }
        }
        return result;
    }

    [[nodiscard]] SmokeGridMat4 smokeGridLookAt(const SmokeGridLookAtDesc& desc) {
        const SmokeGridVec3 forward =
            smokeGridNormalize(smokeGridSubtract(desc.target, desc.position));
        const SmokeGridVec3 right = smokeGridNormalize(smokeGridCross(desc.up, forward));
        const SmokeGridVec3 cameraUp = smokeGridCross(forward, right);

        return SmokeGridMat4{
            right[0],    right[1],    right[2],    -smokeGridDot(right, desc.position),
            cameraUp[0], cameraUp[1], cameraUp[2], -smokeGridDot(cameraUp, desc.position),
            forward[0],  forward[1],  forward[2],  -smokeGridDot(forward, desc.position),
            0.0F,        0.0F,        0.0F,        1.0F,
        };
    }

    [[nodiscard]] SmokeGridMat4 smokeGridPerspective(const SmokeGridPerspectiveDesc& desc) {
        const float focalLength = 1.0F / std::tan(desc.verticalFovRadians * 0.5F);
        return SmokeGridMat4{
            focalLength / desc.aspectRatio,
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            focalLength,
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            desc.farPlane / (desc.farPlane - desc.nearPlane),
            (-desc.nearPlane * desc.farPlane) / (desc.farPlane - desc.nearPlane),
            0.0F,
            0.0F,
            1.0F,
            0.0F,
        };
    }

    [[nodiscard]] asharia::BasicRenderViewCamera
    smokeGridCamera(SmokeGridVec3 position, SmokeGridVec3 target, VkExtent2D extent) {
        constexpr float kVerticalFovRadians = 60.0F * std::numbers::pi_v<float> / 180.0F;
        const float aspectRatio = static_cast<float>(std::max(extent.width, 1U)) /
                                  static_cast<float>(std::max(extent.height, 1U));
        const SmokeGridMat4 view = smokeGridLookAt(SmokeGridLookAtDesc{
            .position = position,
            .target = target,
            .up = {0.0F, 1.0F, 0.0F},
        });
        const SmokeGridMat4 projection = smokeGridPerspective(SmokeGridPerspectiveDesc{
            .verticalFovRadians = kVerticalFovRadians,
            .aspectRatio = aspectRatio,
            .nearPlane = 0.1F,
            .farPlane = 1000.0F,
        });
        return asharia::BasicRenderViewCamera{
            .view = view,
            .projection = projection,
            .viewProjection = smokeGridMultiply(projection, view),
            .position = position,
            .nearPlane = 0.1F,
            .farPlane = 1000.0F,
        };
    }

    [[nodiscard]] asharia::Result<void>
    recordSmokeGridReadbackCopy(const asharia::VulkanFrameRecordContext& frame, VkImage image,
                                VkBuffer buffer, VkExtent2D extent) {
        if (image == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE || extent.width == 0 ||
            extent.height == 0) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Vulkan, 0,
                               "Cannot record grid readback copy with incomplete resources"}};
        }

        VkImageMemoryBarrier2 imageBarrier{};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = image;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.baseMipLevel = 0;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseArrayLayer = 0;
        imageBarrier.subresourceRange.layerCount = 1;

        VkDependencyInfo imageDependency{};
        imageDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        imageDependency.imageMemoryBarrierCount = 1;
        imageDependency.pImageMemoryBarriers = &imageBarrier;
        vkCmdPipelineBarrier2(frame.commandBuffer, &imageDependency);

        VkBufferImageCopy copy{};
        copy.bufferOffset = 0;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = VkExtent3D{
            .width = extent.width,
            .height = extent.height,
            .depth = 1,
        };
        vkCmdCopyImageToBuffer(frame.commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               buffer, 1, &copy);

        VkBufferMemoryBarrier2 bufferBarrier{};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = buffer;
        bufferBarrier.offset = 0;
        bufferBarrier.size = VK_WHOLE_SIZE;

        VkDependencyInfo bufferDependency{};
        bufferDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        bufferDependency.bufferMemoryBarrierCount = 1;
        bufferDependency.pBufferMemoryBarriers = &bufferBarrier;
        vkCmdPipelineBarrier2(frame.commandBuffer, &bufferDependency);
        return {};
    }

    [[nodiscard]] std::uint64_t smokePixelSpread(std::span<const std::byte> pixels) {
        if (pixels.size() < 4U) {
            return 0;
        }
        std::uint64_t spread = 0;
        for (std::size_t index = 4; index + 3 < pixels.size(); index += 4) {
            spread += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(std::to_integer<unsigned char>(pixels[index])) -
                         static_cast<int>(std::to_integer<unsigned char>(pixels[0]))));
            spread += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(std::to_integer<unsigned char>(pixels[index + 1])) -
                         static_cast<int>(std::to_integer<unsigned char>(pixels[1]))));
            spread += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(std::to_integer<unsigned char>(pixels[index + 2])) -
                         static_cast<int>(std::to_integer<unsigned char>(pixels[2]))));
        }
        return spread;
    }

    [[nodiscard]] std::uint64_t smokePixelDifference(std::span<const std::byte> lhs,
                                                     std::span<const std::byte> rhs) {
        const std::size_t count = std::min(lhs.size(), rhs.size());
        std::uint64_t difference = 0;
        for (std::size_t index = 0; index + 3 < count; index += 4) {
            difference += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(std::to_integer<unsigned char>(lhs[index])) -
                         static_cast<int>(std::to_integer<unsigned char>(rhs[index]))));
            difference += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(std::to_integer<unsigned char>(lhs[index + 1])) -
                         static_cast<int>(std::to_integer<unsigned char>(rhs[index + 1]))));
            difference += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(std::to_integer<unsigned char>(lhs[index + 2])) -
                         static_cast<int>(std::to_integer<unsigned char>(rhs[index + 2]))));
        }
        return difference;
    }

    [[nodiscard]] bool validateRenderViewGridReadbackDiagnostics(
        const asharia::BasicRenderViewDiagnostics& diagnostics, bool diagnosticsRecorded,
        std::size_t probeIndex) {
        if (!diagnosticsRecorded || diagnostics.viewKind != asharia::BasicRenderViewKind::Scene ||
            !diagnostics.overlay.enabled || !diagnostics.overlay.worldGridEnabled) {
            asharia::logError("RenderView grid readback smoke missed Scene View grid diagnostics.");
            return false;
        }
        const asharia::BasicRenderViewWorldGridDesc& worldGrid = diagnostics.overlay.worldGrid;
        if (!worldGrid.enabled || worldGrid.planeY != 0.0F || worldGrid.minorSpacing != 1.0F ||
            worldGrid.majorSpacing != 10.0F || worldGrid.fadeStart != 0.0F ||
            worldGrid.fadeEnd != 0.0F || worldGrid.opacity != 1.0F) {
            asharia::logError(
                "RenderView grid readback smoke captured invalid world-grid diagnostics.");
            return false;
        }

        const auto gridPass =
            std::ranges::find_if(diagnostics.renderGraph.passes,
                                 [](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                                     return pass.type == asharia::kBasicRenderViewWorldGridPassType;
                                 });
        if (gridPass == diagnostics.renderGraph.passes.end()) {
            asharia::logError("RenderView grid readback smoke did not record the world-grid pass.");
            return false;
        }

        const auto gridDraw = std::ranges::find_if(
            diagnostics.executionEvents, [](const asharia::BasicRenderViewExecutionEvent& event) {
                return event.kind ==
                           asharia::BasicRenderViewExecutionEventKind::DrawFullscreenTriangle &&
                       event.label == "DrawWorldGrid";
            });
        if (gridDraw == diagnostics.executionEvents.end() || gridDraw->draw.vertexCount != 3U) {
            asharia::logError(
                "RenderView grid readback smoke did not record the world-grid draw event.");
            return false;
        }

        const auto lodCommand = std::ranges::find_if(
            diagnostics.renderGraph.commands,
            [](const asharia::RenderGraphDiagnosticsCommandNode& command) {
                return command.kind == asharia::RenderGraphCommandKind::SetVec4 &&
                       command.detail.starts_with("GridLodSettings = ");
            });
        if (lodCommand == diagnostics.renderGraph.commands.end()) {
            asharia::logError(
                "RenderView grid readback smoke did not record world-grid LOD settings.");
            return false;
        }

        constexpr std::string_view kCloseGridLodSettings{
            "GridLodSettings = (1.000000, 1.000000, 0.000000, 10.000000)"};
        const bool stableBaseGridLod = lodCommand->detail == kCloseGridLodSettings;
        if (smokeGridProbeUsesStableBaseLod(probeIndex) && !stableBaseGridLod) {
            asharia::logError(
                "RenderView grid readback smoke expected low-height cameras to use stable base "
                "LOD but found '" +
                lodCommand->detail + "'.");
            return false;
        }
        if (!smokeGridProbeUsesStableBaseLod(probeIndex) && stableBaseGridLod) {
            asharia::logError(
                "RenderView grid readback smoke expected high camera to leave stable base LOD.");
            return false;
        }
        return true;
    }

    [[nodiscard]] asharia::Result<SmokeRenderViewGridReadbackProbe>
    createSmokeRenderViewGridReadbackProbe(const asharia::VulkanContext& context) {
        auto readbackBuffer = asharia::VulkanBuffer::create(asharia::VulkanBufferDesc{
            .device = context.device(),
            .allocator = context.allocator(),
            .size = smokeGridReadbackByteCount(),
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .memoryUsage = asharia::VulkanBufferMemoryUsage::HostReadback,
        });
        if (!readbackBuffer) {
            return std::unexpected{std::move(readbackBuffer.error())};
        }

        SmokeRenderViewGridReadbackProbe probe;
        probe.readback = std::move(*readbackBuffer);
        probe.pixels.resize(static_cast<std::size_t>(smokeGridReadbackByteCount()));
        return probe;
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordSmokeRenderViewGridReadbackFrame(const asharia::VulkanFrameRecordContext& recordContext,
                                           const asharia::VulkanContext& context,
                                           asharia::BasicFullscreenTextureRenderer& renderer,
                                           SmokeRenderViewGridReadbackProbe& probe,
                                           const asharia::BasicRenderViewCamera& camera,
                                           std::uint64_t frameIndex) {
        auto targetReady =
            probe.target.ensure(recordContext, asharia::VulkanRenderTargetDesc{
                                                   .device = context.device(),
                                                   .allocator = context.allocator(),
                                                   .format = kSmokeGridReadbackFormat,
                                                   .extent = kSmokeGridReadbackExtent,
                                                   .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                            VK_IMAGE_USAGE_SAMPLED_BIT |
                                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                                   .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                               });
        if (!targetReady) {
            return std::unexpected{std::move(targetReady.error())};
        }

        const asharia::VulkanSampledTextureView sampled = probe.target.sampledTextureView();
        auto recorded = renderer.recordViewFrame(
            recordContext,
            asharia::BasicRenderViewDesc{
                .target =
                    asharia::BasicRenderViewTarget{
                        .image = sampled.image,
                        .imageView = sampled.imageView,
                        .format = sampled.format,
                        .extent = sampled.extent,
                        .aspectMask = sampled.aspectMask,
                        .finalUsage = asharia::BasicRenderViewTargetFinalUsage::SampledTexture,
                    },
                .viewKind = asharia::BasicRenderViewKind::Scene,
                .camera = camera,
                .frameParams =
                    asharia::BasicRenderViewFrameParams{
                        .frameIndex = frameIndex,
                    },
                .scene = {},
                .overlay =
                    asharia::BasicRenderViewOverlayDesc{
                        .enabled = true,
                        .worldGrid =
                            asharia::BasicRenderViewWorldGridDesc{
                                .enabled = true,
                                .planeY = 0.0F,
                                .minorSpacing = 1.0F,
                                .majorSpacing = 10.0F,
                                .fadeStart = 0.0F,
                                .fadeEnd = 0.0F,
                                .opacity = 1.0F,
                            },
                    },
                .viewName = "RenderViewGridReadbackSmoke",
                .diagnostics = &probe.diagnostics,
            });
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }
        probe.diagnosticsRecorded = true;

        auto copied = recordSmokeGridReadbackCopy(
            recordContext, sampled.image, probe.readback.handle(), kSmokeGridReadbackExtent);
        if (!copied) {
            return std::unexpected{std::move(copied.error())};
        }
        auto presentReady = asharia::recordBasicClearFrame(recordContext);
        if (!presentReady) {
            return std::unexpected{std::move(presentReady.error())};
        }
        return *presentReady;
    }

    [[nodiscard]] bool
    readSmokeRenderViewGridReadbackProbes(std::span<SmokeRenderViewGridReadbackProbe> probes) {
        std::size_t probeIndex = 0;
        for (SmokeRenderViewGridReadbackProbe& probe : probes) {
            if (!validateRenderViewGridReadbackDiagnostics(probe.diagnostics,
                                                           probe.diagnosticsRecorded, probeIndex)) {
                return false;
            }
            auto read = probe.readback.read(std::span<std::byte>{probe.pixels});
            if (!read) {
                asharia::logError(read.error().message);
                return false;
            }
            ++probeIndex;
        }
        return true;
    }

    [[nodiscard]] bool validateSmokeRenderViewGridPixels(
        std::span<const SmokeRenderViewGridReadbackProbe, kSmokeGridReadbackProbeCount> probes) {
        const std::uint64_t firstSpread = smokePixelSpread(probes.front().pixels);
        const std::uint64_t secondSpread = smokePixelSpread(probes[1].pixels);
        const SmokeRenderViewGridReadbackProbe& highViewProbe =
            probes[kSmokeGridHighViewProbeIndex];
        const SmokeRenderViewGridReadbackProbe& lowFarViewProbe =
            probes[kSmokeGridLowFarViewProbeIndex];
        const std::uint64_t highViewSpread = smokePixelSpread(highViewProbe.pixels);
        const std::uint64_t lowFarViewSpread = smokePixelSpread(lowFarViewProbe.pixels);
        const std::uint64_t cameraDifference =
            smokePixelDifference(probes.front().pixels, probes[1].pixels);
        const std::uint64_t highViewDifference =
            smokePixelDifference(probes.front().pixels, highViewProbe.pixels);
        if (firstSpread < kMinimumVisibleGridSpread || secondSpread < kMinimumVisibleGridSpread ||
            highViewSpread < kMinimumVisibleGridSpread ||
            lowFarViewSpread < kMinimumVisibleGridSpread ||
            cameraDifference < kMinimumCameraDifference ||
            highViewDifference < kMinimumCameraDifference) {
            asharia::logError(
                "RenderView grid readback smoke did not observe enough grid/camera pixel "
                "difference: spread A " +
                std::to_string(firstSpread) + ", spread B " + std::to_string(secondSpread) +
                ", high-view spread " + std::to_string(highViewSpread) + ", low-far-view spread " +
                std::to_string(lowFarViewSpread) + ", camera difference " +
                std::to_string(cameraDifference) + ", high-view difference " +
                std::to_string(highViewDifference) + ".");
            return false;
        }

        std::cout << "RenderView grid readback: " << kSmokeGridReadbackExtent.width << 'x'
                  << kSmokeGridReadbackExtent.height << ", spread A " << firstSpread
                  << ", spread B " << secondSpread << ", high-view spread " << highViewSpread
                  << ", low-far-view spread " << lowFarViewSpread << ", camera difference "
                  << cameraDifference << ", high-view difference " << highViewDifference << '\n';
        return true;
    }

    int runSmokeRenderViewGridReadback() {
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
            *glfw, asharia::WindowDesc{.title = "Asharia Engine RenderView Grid Readback Smoke"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine RenderView Grid Readback Smoke",
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

        std::array<SmokeRenderViewGridReadbackProbe, kSmokeGridReadbackProbeCount> probes;
        for (SmokeRenderViewGridReadbackProbe& probe : probes) {
            auto createdProbe = createSmokeRenderViewGridReadbackProbe(*context);
            if (!createdProbe) {
                asharia::logError(createdProbe.error().message);
                return EXIT_FAILURE;
            }
            probe = std::move(*createdProbe);
        }

        const std::array cameras{
            smokeGridCamera({0.0F, 2.0F, -6.0F}, {0.0F, 0.0F, 0.0F}, kSmokeGridReadbackExtent),
            smokeGridCamera({2.8F, 2.4F, -4.2F}, {0.0F, 0.0F, 0.0F}, kSmokeGridReadbackExtent),
            smokeGridCamera({0.0F, 180.0F, -180.0F}, {0.0F, 0.0F, 0.0F}, kSmokeGridReadbackExtent),
            smokeGridCamera({120.0F, 2.2F, -120.0F}, {0.0F, 0.0F, 0.0F}, kSmokeGridReadbackExtent),
        };

        std::uint64_t frameIndex = 1;
        auto camera = cameras.begin();
        for (SmokeRenderViewGridReadbackProbe& probe : probes) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status =
                frameLoop->renderFrame([&context, &renderer, &probe, &camera, frameIndex](
                                           const asharia::VulkanFrameRecordContext& recordContext)
                                           -> asharia::Result<asharia::VulkanFrameRecordResult> {
                    return recordSmokeRenderViewGridReadbackFrame(
                        recordContext, *context, *renderer, probe, *camera, frameIndex);
                });
            if (!status) {
                asharia::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == asharia::VulkanFrameStatus::OutOfDate) {
                asharia::logError(
                    "Swapchain remained out of date during RenderView grid readback smoke.");
                return EXIT_FAILURE;
            }
            ++camera;
            ++frameIndex;
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            asharia::logError("Failed to wait for Vulkan queue before RenderView grid readback: " +
                              asharia::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        if (!readSmokeRenderViewGridReadbackProbes(probes)) {
            return EXIT_FAILURE;
        }
        if (!validateSmokeRenderViewGridPixels(std::span{probes})) {
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

        SmokeFullscreenTextureState smokeState;
        for (int frame = 0; frame < 3; ++frame) {
            asharia::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status =
                frameLoop->renderFrame([&renderer, &smokeState, frame](
                                           const asharia::VulkanFrameRecordContext& recordContext)
                                           -> asharia::Result<asharia::VulkanFrameRecordResult> {
                    return recordSmokeFullscreenTextureFrame(recordContext, *renderer, smokeState,
                                                             frame);
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

        if (!validateFullscreenTextureOverlayDiagnostics(smokeState.overlayDiagnostics,
                                                         smokeState.overlayDiagnosticsRecorded)) {
            return EXIT_FAILURE;
        }
        if (!smokeState.invalidSceneInputRejected) {
            asharia::logError("Fullscreen texture smoke did not reject invalid scene input.");
            return EXIT_FAILURE;
        }
        if (!smokeState.invalidSceneInputContextReported) {
            asharia::logError(
                "Fullscreen texture smoke did not report invalid scene input context.");
            return EXIT_FAILURE;
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(),
                                        "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDescriptorAllocatorStats(renderer->descriptorAllocatorStats(),
                                              "Fullscreen texture smoke", 32)) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 2, "Fullscreen texture smoke")) {
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

    int runSmokeSceneDrawPacket() {
        const int result = runSmokeFullscreenTexture();
        if (result == EXIT_SUCCESS) {
            std::cout << "Validated scene draw packet smoke.\n";
        }
        return result;
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
                                              "Offscreen viewport smoke", 32)) {
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

    bool validateSmokeRenderGraphDiagnostics(const asharia::RenderGraph& graph,
                                             const asharia::RenderGraphCompileResult& compiled) {
        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(compiled);
        if (snapshot.declaredPassCount != 6 || snapshot.declaredImageCount != 3 ||
            snapshot.declaredBufferCount != 0 || snapshot.passes.size() != 6 ||
            snapshot.commands.size() != 9 || snapshot.resources.size() != 3 ||
            snapshot.accessEdges.size() != 6 || snapshot.dependencyEdges.size() != 3 ||
            snapshot.transitions.size() != 7 || snapshot.transientImages.size() != 1 ||
            !snapshot.transientBuffers.empty() || !snapshot.culledPasses.empty()) {
            asharia::logError("Render graph diagnostics snapshot produced unexpected counts.");
            return false;
        }
        if (snapshot.passes[4].name != "WriteTransientColor" ||
            snapshot.passes[4].declarationIndex != 5 ||
            snapshot.passes[5].name != "SampleTransientColor" ||
            snapshot.passes[5].declarationIndex != 4) {
            asharia::logError(
                "Render graph diagnostics snapshot did not preserve compiled pass order.");
            return false;
        }

        const asharia::RenderGraphDiagnosticsCommandNode& clearCommand = snapshot.commands.front();
        if (clearCommand.passIndex != 0 || clearCommand.declarationIndex != 0 ||
            clearCommand.commandIndex != 0 || clearCommand.passName != "ClearColor" ||
            clearCommand.kind != asharia::RenderGraphCommandKind::ClearColor ||
            clearCommand.detail.find("target") == std::string::npos) {
            asharia::logError("Render graph diagnostics snapshot missed the clear command node.");
            return false;
        }

        const asharia::RenderGraphDiagnosticsCommandNode& sampleShaderCommand =
            snapshot.commands[1];
        if (sampleShaderCommand.passIndex != 1 || sampleShaderCommand.declarationIndex != 1 ||
            sampleShaderCommand.commandIndex != 0 ||
            sampleShaderCommand.passName != "SampleColor" ||
            sampleShaderCommand.kind != asharia::RenderGraphCommandKind::SetShader ||
            sampleShaderCommand.detail != "Hidden/SmokeSample -> Fragment") {
            asharia::logError(
                "Render graph diagnostics snapshot missed the sample shader command node.");
            return false;
        }

        const asharia::RenderGraphDiagnosticsCommandNode& transientDrawCommand =
            snapshot.commands.back();
        if (transientDrawCommand.passIndex != 5 || transientDrawCommand.declarationIndex != 4 ||
            transientDrawCommand.commandIndex != 3 ||
            transientDrawCommand.passName != "SampleTransientColor" ||
            transientDrawCommand.kind != asharia::RenderGraphCommandKind::DrawFullscreenTriangle ||
            transientDrawCommand.detail != "-") {
            asharia::logError(
                "Render graph diagnostics snapshot missed the transient draw command node.");
            return false;
        }

        bool foundTransientReadEdge = false;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passName == "SampleTransientColor" && edge.resourceName == "TransientColor" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::ShaderRead &&
                edge.shaderStage == asharia::RenderGraphShaderStage::Fragment) {
                foundTransientReadEdge = true;
            }
        }
        if (!foundTransientReadEdge) {
            asharia::logError("Render graph diagnostics snapshot missed a transient read edge.");
            return false;
        }

        bool foundTransientDependency = false;
        for (const asharia::RenderGraphDiagnosticsDependencyEdge& edge : snapshot.dependencyEdges) {
            if (edge.fromPassIndex == 4 && edge.toPassIndex == 5 &&
                edge.fromDeclarationIndex == 5 && edge.toDeclarationIndex == 4 &&
                edge.resourceName == "TransientColor" && edge.reason == "producer read") {
                foundTransientDependency = true;
            }
        }
        if (!foundTransientDependency) {
            asharia::logError(
                "Render graph diagnostics snapshot missed the transient dependency edge.");
            return false;
        }

        bool foundFinalBackbufferTransition = false;
        for (const asharia::RenderGraphDiagnosticsTransition& transition : snapshot.transitions) {
            if (transition.phase == asharia::RenderGraphDiagnosticsTransitionPhase::Final &&
                transition.resourceName == "Backbuffer" &&
                transition.oldImageAccess.state == asharia::RenderGraphImageState::ShaderRead &&
                transition.oldImageAccess.shaderStage ==
                    asharia::RenderGraphShaderStage::Fragment &&
                transition.newImageAccess.state == asharia::RenderGraphImageState::Present) {
                foundFinalBackbufferTransition = true;
            }
        }
        if (!foundFinalBackbufferTransition) {
            asharia::logError(
                "Render graph diagnostics snapshot missed the final backbuffer transition.");
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
        RasterMrt,
        RasterFullscreen,
        RenderViewWorldGrid,
        RenderViewSceneInputs,
        RenderViewOverlay,
        RasterDrawList,
        ComputeDispatch,
        ComputeReadback,
        TransferFillBuffer,
        TransferCopyBuffer,
        DebugImageCopy,
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
        asharia::RenderGraphImageHandle colorTarget1{};
        asharia::RenderGraphImageHandle colorSource{};
        asharia::RenderGraphImageHandle depthTarget{};
        asharia::RenderGraphImageHandle unexpectedTarget{};
        asharia::RenderGraphBufferHandle storageTarget{};
        asharia::RenderGraphBufferHandle readbackTarget{};
    };

    struct BuiltinSchemaSmokeCompileOptions {
        std::string_view paramsType;
        std::string_view omittedSlot;
        bool addUnexpectedSlot{};
    };

    void addBuiltinComputeReadbackSmokeSlots(asharia::RenderGraph::PassBuilder& pass,
                                             BuiltinSchemaSmokeImages images,
                                             std::string_view omittedSlot) {
        if (omittedSlot != "source") {
            pass.readTransferBuffer("source", images.storageTarget);
        }
        if (omittedSlot != "target") {
            pass.writeBuffer("target", images.readbackTarget);
        }
    }

    void addBuiltinTransferCopyBufferSmokeSlots(asharia::RenderGraph::PassBuilder& pass,
                                                BuiltinSchemaSmokeImages images,
                                                std::string_view omittedSlot) {
        if (omittedSlot != "source") {
            pass.readTransferBuffer("source", images.storageTarget);
        }
        if (omittedSlot != "target") {
            pass.writeBuffer("target", images.readbackTarget);
        }
    }

    BuiltinSchemaSmokeImages createBuiltinSchemaSmokeImages(asharia::RenderGraph& graph) {
        return BuiltinSchemaSmokeImages{
            .colorTarget = graph.importImage(asharia::RenderGraphImageDesc{
                .name = "BuiltinSchemaColorTarget",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::Present,
            }),
            .colorTarget1 = graph.importImage(asharia::RenderGraphImageDesc{
                .name = "BuiltinSchemaColorTarget1",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = asharia::RenderGraphImageState::Undefined,
                .finalState = asharia::RenderGraphImageState::ColorAttachment,
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
            .storageTarget = graph.importBuffer(asharia::RenderGraphBufferDesc{
                .name = "BuiltinSchemaStorageBuffer",
                .byteSize = 256,
                .initialState = asharia::RenderGraphBufferState::StorageReadWrite,
                .initialShaderStage = asharia::RenderGraphShaderStage::Compute,
                .finalState = asharia::RenderGraphBufferState::StorageReadWrite,
                .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
            }),
            .readbackTarget = graph.importBuffer(asharia::RenderGraphBufferDesc{
                .name = "BuiltinSchemaReadbackBuffer",
                .byteSize = 256,
                .initialState = asharia::RenderGraphBufferState::Undefined,
                .finalState = asharia::RenderGraphBufferState::HostRead,
            }),
        };
    }

    void writeTransferSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                        std::string_view omittedSlot, std::string_view slot,
                                        asharia::RenderGraphImageHandle image) {
        if (omittedSlot != slot) {
            pass.writeTransfer(std::string{slot}, image);
        }
    }

    void readTransferSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                       std::string_view omittedSlot, std::string_view slot,
                                       asharia::RenderGraphImageHandle image) {
        if (omittedSlot != slot) {
            pass.readTransfer(std::string{slot}, image);
        }
    }

    void writeColorSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                     std::string_view omittedSlot, std::string_view slot,
                                     asharia::RenderGraphImageHandle image) {
        if (omittedSlot != slot) {
            pass.writeColor(std::string{slot}, image);
        }
    }

    void readTextureSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                      std::string_view omittedSlot, std::string_view slot,
                                      asharia::RenderGraphImageHandle image) {
        if (omittedSlot != slot) {
            pass.readTexture(std::string{slot}, image, asharia::RenderGraphShaderStage::Fragment);
        }
    }

    void writeDepthSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                     std::string_view omittedSlot, std::string_view slot,
                                     asharia::RenderGraphImageHandle image) {
        if (omittedSlot != slot) {
            pass.writeDepth(std::string{slot}, image);
        }
    }

    void readWriteStorageBufferSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                                 std::string_view omittedSlot,
                                                 std::string_view slot,
                                                 asharia::RenderGraphBufferHandle buffer) {
        if (omittedSlot != slot) {
            pass.readWriteStorageBuffer(std::string{slot}, buffer,
                                        asharia::RenderGraphShaderStage::Compute);
        }
    }

    void writeBufferSlotUnlessOmitted(asharia::RenderGraph::PassBuilder& pass,
                                      std::string_view omittedSlot, std::string_view slot,
                                      asharia::RenderGraphBufferHandle buffer) {
        if (omittedSlot != slot) {
            pass.writeBuffer(std::string{slot}, buffer);
        }
    }

    void addBuiltinSchemaSmokeSlots(BuiltinSchemaSmokePass passKind,
                                    asharia::RenderGraph::PassBuilder& pass,
                                    BuiltinSchemaSmokeImages images,
                                    std::string_view omittedSlot = {}) {
        switch (passKind) {
        case BuiltinSchemaSmokePass::TransferClear:
            writeTransferSlotUnlessOmitted(pass, omittedSlot, "target", images.colorTarget);
            break;
        case BuiltinSchemaSmokePass::DynamicClear:
        case BuiltinSchemaSmokePass::RasterTriangle:
        case BuiltinSchemaSmokePass::RenderViewWorldGrid:
        case BuiltinSchemaSmokePass::RenderViewOverlay:
            writeColorSlotUnlessOmitted(pass, omittedSlot, "target", images.colorTarget);
            break;
        case BuiltinSchemaSmokePass::RenderViewSceneInputs:
            break;
        case BuiltinSchemaSmokePass::TransientPresent:
            readTextureSlotUnlessOmitted(pass, omittedSlot, "source", images.colorSource);
            writeTransferSlotUnlessOmitted(pass, omittedSlot, "target", images.colorTarget);
            break;
        case BuiltinSchemaSmokePass::RasterDepthTriangle:
        case BuiltinSchemaSmokePass::RasterMesh3D:
        case BuiltinSchemaSmokePass::RasterDrawList:
            writeColorSlotUnlessOmitted(pass, omittedSlot, "target", images.colorTarget);
            writeDepthSlotUnlessOmitted(pass, omittedSlot, "depth", images.depthTarget);
            break;
        case BuiltinSchemaSmokePass::RasterMrt:
            writeColorSlotUnlessOmitted(pass, omittedSlot, "color0", images.colorTarget);
            writeColorSlotUnlessOmitted(pass, omittedSlot, "color1", images.colorTarget1);
            break;
        case BuiltinSchemaSmokePass::RasterFullscreen:
            readTextureSlotUnlessOmitted(pass, omittedSlot, "source", images.colorSource);
            writeColorSlotUnlessOmitted(pass, omittedSlot, "target", images.colorTarget);
            break;
        case BuiltinSchemaSmokePass::ComputeDispatch:
            readWriteStorageBufferSlotUnlessOmitted(pass, omittedSlot, "target",
                                                    images.storageTarget);
            break;
        case BuiltinSchemaSmokePass::ComputeReadback:
            addBuiltinComputeReadbackSmokeSlots(pass, images, omittedSlot);
            break;
        case BuiltinSchemaSmokePass::TransferFillBuffer:
            writeBufferSlotUnlessOmitted(pass, omittedSlot, "target", images.storageTarget);
            break;
        case BuiltinSchemaSmokePass::TransferCopyBuffer:
            addBuiltinTransferCopyBufferSmokeSlots(pass, images, omittedSlot);
            break;
        case BuiltinSchemaSmokePass::DebugImageCopy:
            readTransferSlotUnlessOmitted(pass, omittedSlot, "source", images.colorSource);
            writeTransferSlotUnlessOmitted(pass, omittedSlot, "target", images.colorTarget);
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
        if (!testCase.missingSlot.empty()) {
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
                .pass = BuiltinSchemaSmokePass::RasterMrt,
                .type = asharia::kBasicRasterMrtPassType,
                .paramsType = asharia::kBasicRasterMrtParamsType,
                .missingSlot = "color1",
                .context = "builtin raster MRT",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterFullscreen,
                .type = asharia::kBasicRasterFullscreenPassType,
                .paramsType = asharia::kBasicRasterFullscreenParamsType,
                .missingSlot = "source",
                .context = "builtin raster fullscreen",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RenderViewWorldGrid,
                .type = asharia::kBasicRenderViewWorldGridPassType,
                .paramsType = asharia::kBasicRenderViewWorldGridParamsType,
                .missingSlot = "target",
                .context = "builtin render view world grid",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RenderViewSceneInputs,
                .type = asharia::kBasicRenderViewSceneInputsPassType,
                .paramsType = asharia::kBasicRenderViewSceneInputsParamsType,
                .missingSlot = {},
                .context = "builtin render view scene inputs",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RenderViewOverlay,
                .type = asharia::kBasicRenderViewOverlayPassType,
                .paramsType = asharia::kBasicRenderViewOverlayParamsType,
                .missingSlot = "target",
                .context = "builtin render view overlay",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterDrawList,
                .type = asharia::kBasicRasterDrawListPassType,
                .paramsType = asharia::kBasicRasterDrawListParamsType,
                .missingSlot = "depth",
                .context = "builtin raster draw list",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::ComputeDispatch,
                .type = asharia::kBasicComputeDispatchPassType,
                .paramsType = asharia::kBasicComputeDispatchParamsType,
                .missingSlot = "target",
                .context = "builtin compute dispatch",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::ComputeReadback,
                .type = asharia::kBasicComputeReadbackPassType,
                .paramsType = {},
                .missingSlot = "source",
                .context = "builtin compute readback",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::TransferFillBuffer,
                .type = asharia::kBasicTransferFillBufferPassType,
                .paramsType = asharia::kBasicTransferFillBufferParamsType,
                .missingSlot = "target",
                .context = "builtin transfer fill buffer",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::TransferCopyBuffer,
                .type = asharia::kBasicTransferCopyBufferPassType,
                .paramsType = {},
                .missingSlot = "source",
                .context = "builtin transfer copy buffer",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::DebugImageCopy,
                .type = asharia::kBasicDebugImageCopyPassType,
                .paramsType = {},
                .missingSlot = "source",
                .context = "builtin debug image copy",
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
        const auto importedStorage = cullingGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "ImportedStorageSideEffect",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::StorageReadWrite,
            .initialShaderStage = asharia::RenderGraphShaderStage::Compute,
            .finalState = asharia::RenderGraphBufferState::StorageReadWrite,
            .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
        });

        int visibleCallbackCount = 0;
        int culledCallbackCount = 0;
        int importedStorageCallbackCount = 0;
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
        cullingGraph.addPass("ImportedStorageWrite", "basic.compute-dispatch")
            .setParamsType("basic.compute-dispatch.params")
            .readWriteStorageBuffer("target", importedStorage,
                                    asharia::RenderGraphShaderStage::Compute)
            .allowCulling()
            .execute([&importedStorageCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (!context.allowCulling || context.name != "ImportedStorageWrite" ||
                    context.type != "basic.compute-dispatch" ||
                    context.paramsType != "basic.compute-dispatch.params" ||
                    context.bufferStorageReadWrites.size() != 1 ||
                    context.bufferStorageReadWriteSlots.size() != 1 ||
                    context.bufferStorageReadWriteSlots.front().name != "target" ||
                    context.bufferStorageReadWriteSlots.front().shaderStage !=
                        asharia::RenderGraphShaderStage::Compute) {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph imported storage write culling context was unexpected.",
                    }};
                }
                ++importedStorageCallbackCount;
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
        if (compiled->passes.size() != 3 || compiled->culledPasses.size() != 1 ||
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
            compiled->passes[1].name != "ImportedStorageWrite" ||
            compiled->passes[2].name != "SideEffectMarker" || !compiled->passes[2].hasSideEffects) {
            asharia::logError("Render graph culling smoke kept the wrong active passes.");
            return false;
        }

        auto executed = cullingGraph.execute(*compiled);
        if (!executed) {
            asharia::logError(executed.error().message);
            return false;
        }
        if (visibleCallbackCount != 1 || importedStorageCallbackCount != 1 ||
            sideEffectCallbackCount != 1 || culledCallbackCount != 0) {
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

    bool validateSmokeRenderGraphCompute(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph computeGraph;
        const auto storageBuffer =
            computeGraph.createTransientBuffer(asharia::RenderGraphBufferDesc{
                .name = "ComputeStorageBuffer",
                .byteSize = 4096,
            });
        const auto readbackBuffer = computeGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "ComputeReadbackBuffer",
            .byteSize = 4096,
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::HostRead,
        });

        int seedCallbackCount = 0;
        int dispatchCallbackCount = 0;
        int readbackCallbackCount = 0;
        computeGraph.addPass("SeedComputeStorage", "basic.buffer-transfer-write")
            .setParamsType("basic.buffer-transfer-write.params")
            .writeBuffer("target", storageBuffer)
            .recordCommands(
                [](asharia::RenderGraphCommandList& commands) { commands.fillBuffer("target", 0); })
            .execute([&seedCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (context.name != "SeedComputeStorage" || context.bufferWrites.size() != 1 ||
                    !context.bufferTransferReads.empty() ||
                    !context.bufferStorageReadWrites.empty() ||
                    context.bufferTransitionsBefore.size() != 1 ||
                    context.bufferTransitionsBefore.front().newState !=
                        asharia::RenderGraphBufferState::TransferWrite ||
                    context.commands.size() != 1 ||
                    context.commands.front().kind != asharia::RenderGraphCommandKind::FillBuffer ||
                    context.commands.front().name != "target" ||
                    context.commands.front().uintValues[0] != 0) {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph compute seed executor received unexpected context.",
                    }};
                }
                ++seedCallbackCount;
                return {};
            });
        computeGraph.addPass("ComputeDispatch", "basic.compute-dispatch")
            .setParamsType("basic.compute-dispatch.params")
            .readWriteStorageBuffer("target", storageBuffer,
                                    asharia::RenderGraphShaderStage::Compute)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/ComputeSmoke", "Main").dispatch(4, 2, 1);
            })
            .execute([&dispatchCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (context.name != "ComputeDispatch" || context.type != "basic.compute-dispatch" ||
                    context.paramsType != "basic.compute-dispatch.params" ||
                    !context.bufferReads.empty() || !context.bufferTransferReads.empty() ||
                    !context.bufferWrites.empty() || context.bufferStorageReadWrites.size() != 1 ||
                    context.bufferStorageReadWriteSlots.size() != 1 ||
                    context.bufferStorageReadWriteSlots.front().name != "target" ||
                    context.bufferStorageReadWriteSlots.front().shaderStage !=
                        asharia::RenderGraphShaderStage::Compute ||
                    context.bufferTransitionsBefore.size() != 1 ||
                    context.bufferTransitionsBefore.front().oldState !=
                        asharia::RenderGraphBufferState::TransferWrite ||
                    context.bufferTransitionsBefore.front().newState !=
                        asharia::RenderGraphBufferState::StorageReadWrite ||
                    context.bufferTransitionsBefore.front().newShaderStage !=
                        asharia::RenderGraphShaderStage::Compute ||
                    context.commands.size() != 2 ||
                    context.commands[0].kind != asharia::RenderGraphCommandKind::SetShader ||
                    context.commands[0].name != "Hidden/ComputeSmoke" ||
                    context.commands[1].kind != asharia::RenderGraphCommandKind::Dispatch ||
                    context.commands[1].uintValues != std::array<std::uint32_t, 3>{4, 2, 1}) {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph compute dispatch executor received unexpected context.",
                    }};
                }
                ++dispatchCallbackCount;
                return {};
            });
        computeGraph.addPass("ComputeReadbackCopy", asharia::kBasicComputeReadbackPassType)
            .readTransferBuffer("source", storageBuffer)
            .writeBuffer("target", readbackBuffer)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBuffer("source", "target");
            })
            .execute([&readbackCallbackCount](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                if (context.name != "ComputeReadbackCopy" ||
                    context.type != asharia::kBasicComputeReadbackPassType ||
                    !context.bufferReads.empty() || context.bufferTransferReads.size() != 1 ||
                    context.bufferTransferReadSlots.size() != 1 ||
                    context.bufferTransferReadSlots.front().name != "source" ||
                    context.bufferWrites.size() != 1 || context.bufferWriteSlots.size() != 1 ||
                    context.bufferWriteSlots.front().name != "target" ||
                    !context.bufferStorageReadWrites.empty() ||
                    context.bufferTransitionsBefore.size() != 2 || context.commands.size() != 1 ||
                    context.commands.front().kind != asharia::RenderGraphCommandKind::CopyBuffer ||
                    context.commands.front().name != "source" ||
                    context.commands.front().secondaryName != "target") {
                    return std::unexpected{asharia::Error{
                        asharia::ErrorDomain::RenderGraph,
                        0,
                        "Render graph compute readback executor received unexpected context.",
                    }};
                }
                ++readbackCallbackCount;
                return {};
            });

        auto compiled = computeGraph.compile(schemas);
        if (!compiled) {
            asharia::logError(compiled.error().message);
            return false;
        }
        if (compiled->passes.size() != 3 || compiled->passes[0].name != "SeedComputeStorage" ||
            compiled->passes[0].commands.size() != 1 ||
            compiled->passes[0].commands.front().kind !=
                asharia::RenderGraphCommandKind::FillBuffer ||
            compiled->passes[1].name != "ComputeDispatch" ||
            compiled->passes[2].name != "ComputeReadbackCopy" ||
            compiled->passes[2].commands.size() != 1 ||
            compiled->passes[2].commands.front().kind !=
                asharia::RenderGraphCommandKind::CopyBuffer ||
            compiled->dependencies.size() != 2 || compiled->transientBuffers.size() != 1 ||
            compiled->transientBuffers.front().finalState !=
                asharia::RenderGraphBufferState::TransferRead ||
            compiled->transientBuffers.front().finalShaderStage !=
                asharia::RenderGraphShaderStage::None ||
            compiled->finalBufferTransitions.size() != 1 ||
            compiled->finalBufferTransitions.front().newState !=
                asharia::RenderGraphBufferState::HostRead) {
            asharia::logError("Render graph compute dispatch smoke produced an unexpected plan.");
            return false;
        }
        const auto computeTransition =
            asharia::vulkanBufferTransition(compiled->passes[1].bufferTransitionsBefore.front());
        if (computeTransition.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            computeTransition.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
            computeTransition.dstStageMask != VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ||
            computeTransition.dstAccessMask !=
                (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
            asharia::logError("Render graph Vulkan compute storage mapping was unexpected.");
            return false;
        }
        const auto copySourceTransition =
            asharia::vulkanBufferTransition(compiled->passes[2].bufferTransitionsBefore[1]);
        if (copySourceTransition.srcStageMask != VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ||
            copySourceTransition.srcAccessMask !=
                (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) ||
            copySourceTransition.dstStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            copySourceTransition.dstAccessMask != VK_ACCESS_2_TRANSFER_READ_BIT) {
            asharia::logError("Render graph Vulkan compute transfer-read mapping was unexpected.");
            return false;
        }
        const auto readbackFinalTransition =
            asharia::vulkanBufferTransition(compiled->finalBufferTransitions.front());
        if (readbackFinalTransition.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            readbackFinalTransition.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
            readbackFinalTransition.dstStageMask != VK_PIPELINE_STAGE_2_HOST_BIT ||
            readbackFinalTransition.dstAccessMask != VK_ACCESS_2_HOST_READ_BIT) {
            asharia::logError("Render graph Vulkan host-read mapping was unexpected.");
            return false;
        }

        auto executed = computeGraph.execute(*compiled);
        if (!executed) {
            asharia::logError(executed.error().message);
            return false;
        }
        if (seedCallbackCount != 1 || dispatchCallbackCount != 1 || readbackCallbackCount != 1) {
            asharia::logError("Render graph compute dispatch smoke invoked unexpected callbacks.");
            return false;
        }

        asharia::RenderGraph missingStageGraph;
        const auto missingStageBuffer =
            missingStageGraph.importBuffer(asharia::RenderGraphBufferDesc{
                .name = "ComputeStorageMissingStage",
                .byteSize = 64,
                .initialState = asharia::RenderGraphBufferState::StorageReadWrite,
                .initialShaderStage = asharia::RenderGraphShaderStage::Compute,
                .finalState = asharia::RenderGraphBufferState::StorageReadWrite,
                .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
            });
        missingStageGraph.addPass("StorageWithoutStage", "basic.compute-dispatch")
            .setParamsType("basic.compute-dispatch.params")
            .readWriteStorageBuffer("target", missingStageBuffer,
                                    asharia::RenderGraphShaderStage::None)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/ComputeSmoke", "Main").dispatch(1, 1, 1);
            });
        if (!expectRenderGraphCompileFailure(missingStageGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "without a shader stage",
                                                 .context = "storage buffer without shader stage",
                                             })) {
            return false;
        }

        asharia::RenderGraph invalidCommandGraph;
        const auto invalidCommandBuffer =
            invalidCommandGraph.importBuffer(asharia::RenderGraphBufferDesc{
                .name = "ComputeStorageInvalidCommand",
                .byteSize = 64,
                .initialState = asharia::RenderGraphBufferState::StorageReadWrite,
                .initialShaderStage = asharia::RenderGraphShaderStage::Compute,
                .finalState = asharia::RenderGraphBufferState::StorageReadWrite,
                .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
            });
        invalidCommandGraph.addPass("ComputeWithDrawCommand", "basic.compute-dispatch")
            .setParamsType("basic.compute-dispatch.params")
            .readWriteStorageBuffer("target", invalidCommandBuffer,
                                    asharia::RenderGraphShaderStage::Compute)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.drawFullscreenTriangle();
            });
        return expectRenderGraphCompileFailure(invalidCommandGraph.compile(schemas),
                                               ExpectedRenderGraphCompileFailure{
                                                   .message = "is not allowed by schema",
                                                   .context = "compute pass invalid command",
                                               });
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

    bool validateSmokeRenderGraphImageCopy(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto copySource = graph.createTransientImage(asharia::RenderGraphImageDesc{
            .name = "CopySource",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
        });
        const auto copyTarget = graph.importImage(asharia::RenderGraphImageDesc{
            .name = "CopyTarget",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });

        graph.addPass("CopyImage", "basic.image-copy")
            .readTransfer("source", copySource)
            .writeTransfer("target", copyTarget)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImage("source", "target");
            });
        graph.addPass("WriteCopySource", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", copySource);

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            asharia::logError(compiled.error().message);
            return false;
        }
        if (compiled->passes.size() != 2 || compiled->passes[0].name != "WriteCopySource" ||
            compiled->passes[1].name != "CopyImage" || compiled->dependencies.size() != 1) {
            asharia::logError("Render graph image copy smoke produced an unexpected pass plan.");
            return false;
        }

        const asharia::RenderGraphCompiledPass& copyPass = compiled->passes[1];
        if (copyPass.commands.size() != 1 ||
            copyPass.commands.front().kind != asharia::RenderGraphCommandKind::CopyImage ||
            copyPass.commands.front().name != "source" ||
            copyPass.commands.front().secondaryName != "target" ||
            copyPass.transferReadSlots.size() != 1 || copyPass.transferWriteSlots.size() != 1) {
            asharia::logError("Render graph image copy smoke lost command or slot metadata.");
            return false;
        }

        bool foundTransferSrcTransition = false;
        for (const asharia::RenderGraphImageTransition& transition : copyPass.transitionsBefore) {
            if (transition.image == copySource &&
                transition.oldState == asharia::RenderGraphImageState::ColorAttachment &&
                transition.newState == asharia::RenderGraphImageState::TransferSrc) {
                foundTransferSrcTransition = true;
            }
        }
        if (!foundTransferSrcTransition) {
            asharia::logError("Render graph image copy smoke missed the TransferSrc transition.");
            return false;
        }

        const auto vulkanCopySourceTransition =
            asharia::vulkanImageTransition(copyPass.transitionsBefore.front());
        if (vulkanCopySourceTransition.newLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ||
            vulkanCopySourceTransition.dstStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            vulkanCopySourceTransition.dstAccessMask != VK_ACCESS_2_TRANSFER_READ_BIT) {
            asharia::logError("Render graph image copy Vulkan mapping was unexpected.");
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        bool foundTransferReadEdge = false;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passName == "CopyImage" && edge.resourceName == "CopySource" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::TransferRead) {
                foundTransferReadEdge = true;
            }
        }
        if (!foundTransferReadEdge) {
            asharia::logError("Render graph image copy diagnostics missed TransferRead.");
            return false;
        }

        asharia::RenderGraph missingSlotGraph;
        const auto missingSource =
            missingSlotGraph.createTransientImage(asharia::RenderGraphImageDesc{
                .name = "MissingCopySource",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
            });
        const auto missingTarget = missingSlotGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "MissingCopyTarget",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        missingSlotGraph.addPass("WriteMissingCopySource", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", missingSource);
        missingSlotGraph.addPass("CopyMissingSource", "basic.image-copy")
            .writeTransfer("target", missingTarget)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImage("source", "target");
            });
        if (!expectRenderGraphCompileFailure(missingSlotGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "is missing required slot",
                                                 .context = "image copy missing source slot",
                                             })) {
            return false;
        }

        asharia::RenderGraph invalidSlotGraph;
        const auto invalidSource = invalidSlotGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "InvalidCopySource",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
            .initialState = asharia::RenderGraphImageState::TransferSrc,
            .finalState = asharia::RenderGraphImageState::TransferSrc,
        });
        const auto invalidTarget = invalidSlotGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "InvalidCopyTarget",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        invalidSlotGraph.addPass("CopyInvalidSlot", "basic.image-copy")
            .readTransfer("unexpected", invalidSource)
            .writeTransfer("target", invalidTarget)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImage("source", "target");
            });
        if (!expectRenderGraphCompileFailure(invalidSlotGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "that is not allowed by schema",
                                                 .context = "image copy invalid slot",
                                             })) {
            return false;
        }

        asharia::RenderGraph invalidCommandGraph;
        const auto commandSource = invalidCommandGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "InvalidCommandSource",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
            .initialState = asharia::RenderGraphImageState::TransferSrc,
            .finalState = asharia::RenderGraphImageState::TransferSrc,
        });
        const auto commandTarget = invalidCommandGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "InvalidCommandTarget",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 16, .height = 16},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        invalidCommandGraph.addPass("CopyInvalidCommand", "basic.image-copy")
            .readTransfer("source", commandSource)
            .writeTransfer("target", commandTarget)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.0F, 0.0F, 0.0F, 1.0F});
            });
        return expectRenderGraphCompileFailure(invalidCommandGraph.compile(schemas),
                                               ExpectedRenderGraphCompileFailure{
                                                   .message = "is not allowed by schema",
                                                   .context = "image copy invalid command",
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
            .type = "basic.image-copy",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::TransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyImage},
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
            .allowedCommands = {asharia::RenderGraphCommandKind::FillBuffer},
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
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "basic.compute-dispatch",
            .paramsType = "basic.compute-dispatch.params",
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferStorageReadWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::Compute,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    asharia::RenderGraphCommandKind::SetShader,
                    asharia::RenderGraphCommandKind::Dispatch,
                },
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = asharia::kBasicComputeReadbackPassType,
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyBuffer},
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

        if (!validateSmokeRenderGraphDiagnostics(graph, *compiled)) {
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

        if (!validateSmokeRenderGraphImageCopy(schemas)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphCompute(schemas)) {
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
        if (hasArg(args, "--smoke-mrt")) {
            return runSmokeMrt(args);
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
            SmokeCommand{.option = "--smoke-material-binding", .run = runSmokeMaterialBinding},
            SmokeCommand{.option = "--smoke-fullscreen-texture", .run = runSmokeFullscreenTexture},
            SmokeCommand{.option = "--smoke-scene-draw-packet", .run = runSmokeSceneDrawPacket},
            SmokeCommand{.option = "--smoke-render-view-grid-readback",
                         .run = runSmokeRenderViewGridReadback},
            SmokeCommand{.option = "--smoke-offscreen-viewport", .run = runSmokeOffscreenViewport},
            SmokeCommand{.option = "--smoke-compute-dispatch", .run = runSmokeComputeDispatch},
            SmokeCommand{.option = "--smoke-buffer-upload", .run = runSmokeBufferUpload},
            SmokeCommand{.option = "--smoke-texture-upload", .run = runSmokeTextureUpload},
            SmokeCommand{.option = "--smoke-renderer-format-contract",
                         .run = runSmokeRendererFormatContract},
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
