#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vke/core/error.hpp"
#include "vke/core/result.hpp"

namespace vke {

    struct RenderGraphImageHandle {
        std::uint32_t index{};

        [[nodiscard]] friend bool operator==(RenderGraphImageHandle,
                                             RenderGraphImageHandle) = default;
    };

    struct RenderGraphExtent2D {
        std::uint32_t width{};
        std::uint32_t height{};
    };

    enum class RenderGraphImageFormat {
        Undefined,
        B8G8R8A8Srgb,
    };

    enum class RenderGraphImageState {
        Undefined,
        ColorAttachment,
        ShaderRead,
        TransferDst,
        Present,
    };

    enum class RenderGraphShaderStage {
        None,
        Fragment,
        Compute,
    };

    struct RenderGraphImageAccess {
        RenderGraphImageState state{RenderGraphImageState::Undefined};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};

        [[nodiscard]] friend bool operator==(RenderGraphImageAccess,
                                             RenderGraphImageAccess) = default;
    };

    struct RenderGraphImageDesc {
        std::string name;
        RenderGraphImageFormat format{RenderGraphImageFormat::Undefined};
        RenderGraphExtent2D extent{};
        RenderGraphImageState initialState{RenderGraphImageState::Undefined};
        RenderGraphImageState finalState{RenderGraphImageState::Present};
    };

    struct RenderGraphImageTransition {
        RenderGraphImageHandle image{};
        std::string imageName;
        RenderGraphImageState oldState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage oldShaderStage{RenderGraphShaderStage::None};
        RenderGraphImageState newState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage newShaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphImageSlot {
        std::string name;
        RenderGraphImageHandle image{};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
    };

    enum class RenderGraphSlotAccess {
        ColorWrite,
        ShaderRead,
        TransferWrite,
    };

    struct RenderGraphResourceSlotSchema {
        std::string name;
        RenderGraphSlotAccess access{RenderGraphSlotAccess::ColorWrite};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
        bool optional{};
    };

    struct RenderGraphPassSchema {
        std::string type;
        std::string paramsType;
        std::vector<RenderGraphResourceSlotSchema> resourceSlots;
    };

    struct RenderGraphCompiledPass {
        std::string name;
        std::string type;
        std::string paramsType;
        std::vector<RenderGraphImageTransition> transitionsBefore;
        std::vector<RenderGraphImageHandle> colorWrites;
        std::vector<RenderGraphImageHandle> shaderReads;
        std::vector<RenderGraphImageHandle> transferWrites;
        std::vector<RenderGraphImageSlot> colorWriteSlots;
        std::vector<RenderGraphImageSlot> shaderReadSlots;
        std::vector<RenderGraphImageSlot> transferWriteSlots;
    };

    struct RenderGraphCompileResult {
        std::vector<RenderGraphCompiledPass> passes;
        std::vector<RenderGraphImageTransition> finalTransitions;
    };

    struct RenderGraphPassContext {
        std::string_view name;
        std::string_view type;
        std::string_view paramsType;
        std::span<const RenderGraphImageTransition> transitionsBefore;
        std::span<const RenderGraphImageHandle> colorWrites;
        std::span<const RenderGraphImageHandle> shaderReads;
        std::span<const RenderGraphImageHandle> transferWrites;
        std::span<const RenderGraphImageSlot> colorWriteSlots;
        std::span<const RenderGraphImageSlot> shaderReadSlots;
        std::span<const RenderGraphImageSlot> transferWriteSlots;
    };

    using RenderGraphPassCallback = std::function<Result<void>(RenderGraphPassContext)>;

    class RenderGraphSchemaRegistry {
    public:
        RenderGraphSchemaRegistry& registerSchema(RenderGraphPassSchema schema) {
            for (RenderGraphPassSchema& registered : schemas_) {
                if (registered.type == schema.type) {
                    registered = std::move(schema);
                    return *this;
                }
            }

            schemas_.push_back(std::move(schema));
            return *this;
        }

        [[nodiscard]] const RenderGraphPassSchema* find(std::string_view type) const {
            for (const RenderGraphPassSchema& schema : schemas_) {
                if (schema.type == type) {
                    return &schema;
                }
            }

            return nullptr;
        }

    private:
        std::vector<RenderGraphPassSchema> schemas_;
    };

    class RenderGraphExecutorRegistry {
    public:
        RenderGraphExecutorRegistry& registerExecutor(std::string type,
                                                      RenderGraphPassCallback callback) {
            for (Executor& executor : executors_) {
                if (executor.type == type) {
                    executor.callback = std::move(callback);
                    return *this;
                }
            }

            executors_.push_back(Executor{
                .type = std::move(type),
                .callback = std::move(callback),
            });
            return *this;
        }

        [[nodiscard]] const RenderGraphPassCallback* find(std::string_view type) const {
            for (const Executor& executor : executors_) {
                if (executor.type == type) {
                    return &executor.callback;
                }
            }

            return nullptr;
        }

    private:
        struct Executor {
            std::string type;
            RenderGraphPassCallback callback;
        };

        std::vector<Executor> executors_;
    };

    class RenderGraph {
    private:
        struct Pass {
            std::string name;
            std::string type;
            std::string paramsType;
            std::vector<RenderGraphImageSlot> colorWriteSlots;
            std::vector<RenderGraphImageSlot> shaderReadSlots;
            std::vector<RenderGraphImageSlot> transferWriteSlots;
            RenderGraphPassCallback callback;
        };

    public:
        class PassBuilder {
        public:
            PassBuilder& writeColor(RenderGraphImageHandle image) {
                return writeColor("target", image);
            }

            PassBuilder& writeColor(std::string slotName, RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].colorWriteSlots.push_back(RenderGraphImageSlot{
                    .name = std::move(slotName),
                    .image = image,
                });
                return *this;
            }

            PassBuilder& readTexture(std::string slotName, RenderGraphImageHandle image,
                                     RenderGraphShaderStage shaderStage) {
                graph_->passes_[passIndex_].shaderReadSlots.push_back(RenderGraphImageSlot{
                    .name = std::move(slotName),
                    .image = image,
                    .shaderStage = shaderStage,
                });
                return *this;
            }

            PassBuilder& writeTransfer(RenderGraphImageHandle image) {
                return writeTransfer("target", image);
            }

            PassBuilder& writeTransfer(std::string slotName, RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].transferWriteSlots.push_back(RenderGraphImageSlot{
                    .name = std::move(slotName),
                    .image = image,
                });
                return *this;
            }

            [[nodiscard]] std::string_view name() const {
                return graph_->passes_[passIndex_].name;
            }

            [[nodiscard]] std::string_view type() const {
                return graph_->passes_[passIndex_].type;
            }

            PassBuilder& setParamsType(std::string paramsType) {
                graph_->passes_[passIndex_].paramsType = std::move(paramsType);
                return *this;
            }

            PassBuilder& execute(RenderGraphPassCallback callback) {
                graph_->passes_[passIndex_].callback = std::move(callback);
                return *this;
            }

        private:
            friend class RenderGraph;

            PassBuilder(RenderGraph& graph, std::size_t passIndex)
                : graph_(&graph), passIndex_(passIndex) {}

            RenderGraph* graph_{};
            std::size_t passIndex_{};
        };

        [[nodiscard]] RenderGraphImageHandle importImage(RenderGraphImageDesc desc) {
            images_.push_back(std::move(desc));
            return RenderGraphImageHandle{
                .index = static_cast<std::uint32_t>(images_.size() - 1),
            };
        }

        PassBuilder addPass(std::string name) {
            Pass pass{
                .name = std::move(name),
                .type = {},
                .paramsType = {},
                .colorWriteSlots = {},
                .shaderReadSlots = {},
                .transferWriteSlots = {},
                .callback = {},
            };
            passes_.push_back(std::move(pass));
            return PassBuilder{*this, passes_.size() - 1};
        }

        PassBuilder addPass(std::string name, std::string type) {
            Pass pass{
                .name = std::move(name),
                .type = std::move(type),
                .paramsType = {},
                .colorWriteSlots = {},
                .shaderReadSlots = {},
                .transferWriteSlots = {},
                .callback = {},
            };
            passes_.push_back(std::move(pass));
            return PassBuilder{*this, passes_.size() - 1};
        }

        [[nodiscard]] Result<RenderGraphCompileResult> compile() const {
            return compile(nullptr);
        }

        [[nodiscard]] Result<RenderGraphCompileResult>
        compile(const RenderGraphSchemaRegistry& schemaRegistry) const {
            return compile(&schemaRegistry);
        }

    private:
        [[nodiscard]] Result<RenderGraphCompileResult>
        compile(const RenderGraphSchemaRegistry* schemaRegistry) const {
            std::vector<RenderGraphImageAccess> currentAccesses;
            currentAccesses.reserve(images_.size());
            for (const RenderGraphImageDesc& image : images_) {
                currentAccesses.push_back(RenderGraphImageAccess{
                    .state = image.initialState,
                    .shaderStage = RenderGraphShaderStage::None,
                });
            }

            RenderGraphCompileResult result;
            result.passes.reserve(passes_.size());

            for (const Pass& pass : passes_) {
                auto slotsValidated = validateWriteSlots(pass);
                if (!slotsValidated) {
                    return std::unexpected{std::move(slotsValidated.error())};
                }

                if (schemaRegistry != nullptr) {
                    auto schemaValidated = validateSchema(pass, *schemaRegistry);
                    if (!schemaValidated) {
                        return std::unexpected{std::move(schemaValidated.error())};
                    }
                }

                RenderGraphCompiledPass compiledPass{
                    .name = pass.name,
                    .type = pass.type,
                    .paramsType = pass.paramsType,
                    .transitionsBefore = {},
                    .colorWrites = imageHandles(pass.colorWriteSlots),
                    .shaderReads = imageHandles(pass.shaderReadSlots),
                    .transferWrites = imageHandles(pass.transferWriteSlots),
                    .colorWriteSlots = pass.colorWriteSlots,
                    .shaderReadSlots = pass.shaderReadSlots,
                    .transferWriteSlots = pass.transferWriteSlots,
                };

                auto colorTransitions =
                    transitionImages(compiledPass.colorWriteSlots,
                                     RenderGraphImageAccess{
                                         .state = RenderGraphImageState::ColorAttachment,
                                         .shaderStage = RenderGraphShaderStage::None,
                                     },
                                     currentAccesses, compiledPass);
                if (!colorTransitions) {
                    return std::unexpected{std::move(colorTransitions.error())};
                }

                auto shaderReadTransitions =
                    transitionImages(compiledPass.shaderReadSlots,
                                     RenderGraphImageAccess{
                                         .state = RenderGraphImageState::ShaderRead,
                                         .shaderStage = RenderGraphShaderStage::None,
                                     },
                                     currentAccesses, compiledPass);
                if (!shaderReadTransitions) {
                    return std::unexpected{std::move(shaderReadTransitions.error())};
                }

                auto transferTransitions =
                    transitionImages(compiledPass.transferWriteSlots,
                                     RenderGraphImageAccess{
                                         .state = RenderGraphImageState::TransferDst,
                                         .shaderStage = RenderGraphShaderStage::None,
                                     },
                                     currentAccesses, compiledPass);
                if (!transferTransitions) {
                    return std::unexpected{std::move(transferTransitions.error())};
                }

                result.passes.push_back(std::move(compiledPass));
            }

            for (std::size_t index = 0; index < images_.size(); ++index) {
                const RenderGraphImageDesc& image = images_[index];
                const RenderGraphImageAccess finalAccess{
                    .state = image.finalState,
                    .shaderStage = RenderGraphShaderStage::None,
                };
                if (currentAccesses[index] == finalAccess) {
                    continue;
                }

                const RenderGraphImageHandle imageHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                result.finalTransitions.push_back(
                    makeTransition(imageHandle, image, currentAccesses[index], finalAccess));
            }

            return result;
        }

    public:
        [[nodiscard]] Result<void> execute() const {
            auto compiled = compile();
            if (!compiled) {
                return std::unexpected{std::move(compiled.error())};
            }

            return execute(*compiled);
        }

        [[nodiscard]] Result<void>
        execute(const RenderGraphExecutorRegistry& executorRegistry) const {
            auto compiled = compile();
            if (!compiled) {
                return std::unexpected{std::move(compiled.error())};
            }

            return execute(*compiled, executorRegistry);
        }

        [[nodiscard]] Result<void> execute(const RenderGraphCompileResult& compiled) const {
            return execute(compiled, nullptr);
        }

        [[nodiscard]] Result<void>
        execute(const RenderGraphCompileResult& compiled,
                const RenderGraphExecutorRegistry& executorRegistry) const {
            return execute(compiled, &executorRegistry);
        }

    private:
        [[nodiscard]] Result<void>
        execute(const RenderGraphCompileResult& compiled,
                const RenderGraphExecutorRegistry* executorRegistry) const {
            if (compiled.passes.size() != passes_.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Compiled render graph pass count does not match the graph.",
                }};
            }

            for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
                const RenderGraphCompiledPass& pass = compiled.passes[index];
                const RenderGraphPassCallback* callback = &passes_[index].callback;
                if (!*callback && executorRegistry != nullptr) {
                    callback = executorRegistry->find(pass.type);
                }
                if (callback == nullptr || !*callback) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        missingCallbackMessage(pass),
                    }};
                }

                auto executed = (*callback)(RenderGraphPassContext{
                    .name = pass.name,
                    .type = pass.type,
                    .paramsType = pass.paramsType,
                    .transitionsBefore = pass.transitionsBefore,
                    .colorWrites = pass.colorWrites,
                    .shaderReads = pass.shaderReads,
                    .transferWrites = pass.transferWrites,
                    .colorWriteSlots = pass.colorWriteSlots,
                    .shaderReadSlots = pass.shaderReadSlots,
                    .transferWriteSlots = pass.transferWriteSlots,
                });
                if (!executed) {
                    return std::unexpected{std::move(executed.error())};
                }
            }

            return {};
        }

    public:
        [[nodiscard]] std::string
        formatDebugTables(const RenderGraphCompileResult& compiled) const {
            std::string output;
            output += "### RenderGraph Resources\n\n";
            output += "| # | Name | Format | Extent | Initial | Final |\n";
            output += "|---:|---|---|---:|---|---|\n";
            for (std::size_t index = 0; index < images_.size(); ++index) {
                const RenderGraphImageDesc& image = images_[index];
                output += "| ";
                output += std::to_string(index);
                output += " | ";
                output += image.name;
                output += " | ";
                output += imageFormatName(image.format);
                output += " | ";
                output += std::to_string(image.extent.width);
                output += "x";
                output += std::to_string(image.extent.height);
                output += " | ";
                output += imageStateName(image.initialState);
                output += " | ";
                output += imageStateName(image.finalState);
                output += " |\n";
            }

            output += "\n### RenderGraph Passes\n\n";
            output += "| # | Name | Type | Params | Before Transitions | Color Writes | "
                      "Shader Reads | Transfer Writes |\n";
            output += "|---:|---|---|---|---:|---|---|---|\n";
            for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
                const RenderGraphCompiledPass& pass = compiled.passes[index];
                output += "| ";
                output += std::to_string(index);
                output += " | ";
                output += pass.name;
                output += " | ";
                output += pass.type.empty() ? "-" : pass.type;
                output += " | ";
                output += pass.paramsType.empty() ? "-" : pass.paramsType;
                output += " | ";
                output += std::to_string(pass.transitionsBefore.size());
                output += " | ";
                output += imageSlotList(pass.colorWriteSlots);
                output += " | ";
                output += imageSlotList(pass.shaderReadSlots);
                output += " | ";
                output += imageSlotList(pass.transferWriteSlots);
                output += " |\n";
            }

            output += "\n### RenderGraph Slots\n\n";
            output += "| Pass | Access | Slot | Image |\n";
            output += "|---|---|---|---|\n";
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                appendSlotRows(output, pass.name, "ColorWrite", pass.colorWriteSlots);
                appendSlotRows(output, pass.name, "ShaderRead", pass.shaderReadSlots);
                appendSlotRows(output, pass.name, "TransferWrite", pass.transferWriteSlots);
            }

            output += "\n### RenderGraph Transitions\n\n";
            output += "| Phase | Pass | Image | Old State | New State |\n";
            output += "|---|---|---|---|---|\n";
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                    appendTransitionRow(output, "Before", pass.name, transition);
                }
            }
            for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
                appendTransitionRow(output, "Final", "-", transition);
            }

            return output;
        }

    private:
        [[nodiscard]] Result<void> validateImageHandle(RenderGraphImageHandle image) const {
            if (image.index >= images_.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph image handle is out of range.",
                }};
            }

            return {};
        }

        [[nodiscard]] Result<void> validateWriteSlots(const Pass& pass) const {
            auto colorSlots = validateSlots(pass, pass.colorWriteSlots);
            if (!colorSlots) {
                return std::unexpected{std::move(colorSlots.error())};
            }

            auto shaderReadSlots = validateShaderReadSlots(pass);
            if (!shaderReadSlots) {
                return std::unexpected{std::move(shaderReadSlots.error())};
            }

            auto transferSlots = validateSlots(pass, pass.transferWriteSlots);
            if (!transferSlots) {
                return std::unexpected{std::move(transferSlots.error())};
            }

            for (const RenderGraphImageSlot& colorSlot : pass.colorWriteSlots) {
                for (const RenderGraphImageSlot& shaderReadSlot : pass.shaderReadSlots) {
                    if (colorSlot.name == shaderReadSlot.name) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            duplicateSlotMessage(pass, colorSlot.name),
                        }};
                    }
                }
                for (const RenderGraphImageSlot& transferSlot : pass.transferWriteSlots) {
                    if (colorSlot.name == transferSlot.name) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            duplicateSlotMessage(pass, colorSlot.name),
                        }};
                    }
                }
            }
            for (const RenderGraphImageSlot& shaderReadSlot : pass.shaderReadSlots) {
                for (const RenderGraphImageSlot& transferSlot : pass.transferWriteSlots) {
                    if (shaderReadSlot.name == transferSlot.name) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            duplicateSlotMessage(pass, shaderReadSlot.name),
                        }};
                    }
                }
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateSchema(const Pass& pass, const RenderGraphSchemaRegistry& schemaRegistry) const {
            if (pass.type.empty()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name +
                        "' cannot be schema-validated without a type.",
                }};
            }

            const RenderGraphPassSchema* schema = schemaRegistry.find(pass.type);
            if (schema == nullptr) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' has no registered schema for type '" +
                        pass.type + "'.",
                }};
            }

            if (pass.paramsType != schema->paramsType) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' expected params type '" +
                        schema->paramsType + "' but found '" + pass.paramsType + "'.",
                }};
            }

            auto colorSlots = validateSlotsAgainstSchema(
                pass, pass.colorWriteSlots, RenderGraphSlotAccess::ColorWrite, *schema);
            if (!colorSlots) {
                return std::unexpected{std::move(colorSlots.error())};
            }

            auto shaderReadSlots = validateSlotsAgainstSchema(
                pass, pass.shaderReadSlots, RenderGraphSlotAccess::ShaderRead, *schema);
            if (!shaderReadSlots) {
                return std::unexpected{std::move(shaderReadSlots.error())};
            }

            auto transferSlots = validateSlotsAgainstSchema(
                pass, pass.transferWriteSlots, RenderGraphSlotAccess::TransferWrite, *schema);
            if (!transferSlots) {
                return std::unexpected{std::move(transferSlots.error())};
            }

            for (const RenderGraphResourceSlotSchema& slotSchema : schema->resourceSlots) {
                if (slotSchema.optional) {
                    continue;
                }
                if (!hasSlot(pass, slotSchema)) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' is missing required slot '" +
                            slotSchema.name + "'.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateSlotsAgainstSchema(const Pass& pass, std::span<const RenderGraphImageSlot> slots,
                                   RenderGraphSlotAccess access,
                                   const RenderGraphPassSchema& schema) const {
            for (const RenderGraphImageSlot& slot : slots) {
                if (findSlotSchema(schema, slot.name, access, slot.shaderStage) == nullptr) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' declares slot '" + slot.name +
                            "' that is not allowed by schema '" + schema.type + "'.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] bool hasSlot(const Pass& pass,
                                   const RenderGraphResourceSlotSchema& slotSchema) const {
            const std::span<const RenderGraphImageSlot> slots =
                slotsForAccess(pass, slotSchema.access);
            for (const RenderGraphImageSlot& slot : slots) {
                if (slot.name == slotSchema.name && slot.shaderStage == slotSchema.shaderStage) {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] static const RenderGraphResourceSlotSchema*
        findSlotSchema(const RenderGraphPassSchema& schema, std::string_view name,
                       RenderGraphSlotAccess access, RenderGraphShaderStage shaderStage) {
            for (const RenderGraphResourceSlotSchema& slotSchema : schema.resourceSlots) {
                if (slotSchema.name == name && slotSchema.access == access &&
                    slotSchema.shaderStage == shaderStage) {
                    return &slotSchema;
                }
            }

            return nullptr;
        }

        [[nodiscard]] std::span<const RenderGraphImageSlot>
        slotsForAccess(const Pass& pass, RenderGraphSlotAccess access) const {
            switch (access) {
            case RenderGraphSlotAccess::ColorWrite:
                return pass.colorWriteSlots;
            case RenderGraphSlotAccess::ShaderRead:
                return pass.shaderReadSlots;
            case RenderGraphSlotAccess::TransferWrite:
                return pass.transferWriteSlots;
            }
            return {};
        }

        [[nodiscard]] Result<void>
        validateSlots(const Pass& pass, std::span<const RenderGraphImageSlot> slots) const {
            for (std::size_t index = 0; index < slots.size(); ++index) {
                const RenderGraphImageSlot& slot = slots[index];
                if (slot.name.empty()) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' has an unnamed resource slot.",
                    }};
                }

                auto validated = validateImageHandle(slot.image);
                if (!validated) {
                    return std::unexpected{std::move(validated.error())};
                }

                for (std::size_t otherIndex = index + 1; otherIndex < slots.size(); ++otherIndex) {
                    if (slot.name == slots[otherIndex].name) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            duplicateSlotMessage(pass, slot.name),
                        }};
                    }
                }
            }

            return {};
        }

        [[nodiscard]] Result<void> validateShaderReadSlots(const Pass& pass) const {
            auto slots = validateSlots(pass, pass.shaderReadSlots);
            if (!slots) {
                return std::unexpected{std::move(slots.error())};
            }

            for (const RenderGraphImageSlot& slot : pass.shaderReadSlots) {
                if (slot.shaderStage == RenderGraphShaderStage::None) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' declares shader read slot '" +
                            slot.name + "' without a shader stage.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] static std::vector<RenderGraphImageHandle>
        imageHandles(std::span<const RenderGraphImageSlot> slots) {
            std::vector<RenderGraphImageHandle> handles;
            handles.reserve(slots.size());
            for (const RenderGraphImageSlot& slot : slots) {
                handles.push_back(slot.image);
            }
            return handles;
        }

        [[nodiscard]] Result<void>
        transitionImages(std::span<const RenderGraphImageSlot> imageSlots,
                         RenderGraphImageAccess requiredAccess,
                         std::vector<RenderGraphImageAccess>& currentAccesses,
                         RenderGraphCompiledPass& compiledPass) const {
            for (const RenderGraphImageSlot& slot : imageSlots) {
                RenderGraphImageHandle imageHandle = slot.image;
                auto validated = validateImageHandle(imageHandle);
                if (!validated) {
                    return std::unexpected{std::move(validated.error())};
                }

                RenderGraphImageAccess slotAccess = requiredAccess;
                if (slotAccess.state == RenderGraphImageState::ShaderRead) {
                    slotAccess.shaderStage = slot.shaderStage;
                }

                const RenderGraphImageDesc& image = images_[imageHandle.index];
                if (currentAccesses[imageHandle.index] != slotAccess) {
                    compiledPass.transitionsBefore.push_back(makeTransition(
                        imageHandle, image, currentAccesses[imageHandle.index], slotAccess));
                    currentAccesses[imageHandle.index] = slotAccess;
                }
            }

            return {};
        }

        [[nodiscard]] static RenderGraphImageTransition
        makeTransition(RenderGraphImageHandle imageHandle, const RenderGraphImageDesc& image,
                       RenderGraphImageAccess oldAccess, RenderGraphImageAccess newAccess) {
            return RenderGraphImageTransition{
                .image = imageHandle,
                .imageName = image.name,
                .oldState = oldAccess.state,
                .oldShaderStage = oldAccess.shaderStage,
                .newState = newAccess.state,
                .newShaderStage = newAccess.shaderStage,
            };
        }

        [[nodiscard]] static std::string_view imageFormatName(RenderGraphImageFormat format) {
            switch (format) {
            case RenderGraphImageFormat::B8G8R8A8Srgb:
                return "B8G8R8A8Srgb";
            case RenderGraphImageFormat::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] static std::string_view imageStateName(RenderGraphImageState state) {
            switch (state) {
            case RenderGraphImageState::ColorAttachment:
                return "ColorAttachment";
            case RenderGraphImageState::ShaderRead:
                return "ShaderRead";
            case RenderGraphImageState::TransferDst:
                return "TransferDst";
            case RenderGraphImageState::Present:
                return "Present";
            case RenderGraphImageState::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] static std::string
        missingCallbackMessage(const RenderGraphCompiledPass& pass) {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "'";
            if (!pass.type.empty()) {
                message += " of type '";
                message += pass.type;
                message += "'";
            }
            message += " is missing an execute callback.";
            return message;
        }

        [[nodiscard]] static std::string duplicateSlotMessage(const Pass& pass,
                                                              std::string_view slotName) {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "' declares duplicate resource slot '";
            message += slotName;
            message += "'.";
            return message;
        }

        [[nodiscard]] std::string imageHandleLabel(RenderGraphImageHandle image) const {
            std::string label = "#";
            label += std::to_string(image.index);
            if (image.index < images_.size() && !images_[image.index].name.empty()) {
                label += " ";
                label += images_[image.index].name;
            }
            return label;
        }

        [[nodiscard]] std::string
        imageHandleList(std::span<const RenderGraphImageHandle> images) const {
            if (images.empty()) {
                return "-";
            }

            std::string labels;
            for (std::size_t index = 0; index < images.size(); ++index) {
                if (index > 0) {
                    labels += ", ";
                }
                labels += imageHandleLabel(images[index]);
            }
            return labels;
        }

        [[nodiscard]] static std::string_view shaderStageName(RenderGraphShaderStage stage) {
            switch (stage) {
            case RenderGraphShaderStage::Fragment:
                return "fragment";
            case RenderGraphShaderStage::Compute:
                return "compute";
            case RenderGraphShaderStage::None:
            default:
                return "";
            }
        }

        [[nodiscard]] std::string imageAccessName(RenderGraphImageState state,
                                                  RenderGraphShaderStage shaderStage) const {
            std::string name{imageStateName(state)};
            if (state == RenderGraphImageState::ShaderRead &&
                shaderStage != RenderGraphShaderStage::None) {
                name += "(";
                name += shaderStageName(shaderStage);
                name += ")";
            }
            return name;
        }

        [[nodiscard]] std::string slotImageLabel(const RenderGraphImageSlot& slot) const {
            std::string label = slot.name;
            if (slot.shaderStage != RenderGraphShaderStage::None) {
                label += "(";
                label += shaderStageName(slot.shaderStage);
                label += ")";
            }
            label += "=";
            label += imageHandleLabel(slot.image);
            return label;
        }

        [[nodiscard]] std::string imageSlotList(std::span<const RenderGraphImageSlot> slots) const {
            if (slots.empty()) {
                return "-";
            }

            std::string labels;
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (index > 0) {
                    labels += ", ";
                }
                labels += slotImageLabel(slots[index]);
            }
            return labels;
        }

        void appendSlotRows(std::string& output, std::string_view passName, std::string_view access,
                            std::span<const RenderGraphImageSlot> slots) const {
            if (slots.empty()) {
                return;
            }

            for (const RenderGraphImageSlot& slot : slots) {
                output += "| ";
                output += passName;
                output += " | ";
                output += access;
                output += " | ";
                output += slot.name;
                if (slot.shaderStage != RenderGraphShaderStage::None) {
                    output += "(";
                    output += shaderStageName(slot.shaderStage);
                    output += ")";
                }
                output += " | ";
                output += imageHandleLabel(slot.image);
                output += " |\n";
            }
        }

        void appendTransitionRow(std::string& output, std::string_view phase,
                                 std::string_view passName,
                                 const RenderGraphImageTransition& transition) const {
            output += "| ";
            output += phase;
            output += " | ";
            output += passName;
            output += " | ";
            output += imageHandleLabel(transition.image);
            output += " | ";
            output += imageAccessName(transition.oldState, transition.oldShaderStage);
            output += " | ";
            output += imageAccessName(transition.newState, transition.newShaderStage);
            output += " |\n";
        }

        std::vector<RenderGraphImageDesc> images_;
        std::vector<Pass> passes_;
    };

} // namespace vke
