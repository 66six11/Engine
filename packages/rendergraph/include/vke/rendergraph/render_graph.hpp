#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
        D32Sfloat,
    };

    enum class RenderGraphImageState {
        Undefined,
        ColorAttachment,
        ShaderRead,
        DepthAttachmentRead,
        DepthAttachmentWrite,
        DepthSampledRead,
        TransferDst,
        Present,
    };

    enum class RenderGraphShaderStage {
        None,
        Fragment,
        Compute,
    };

    enum class RenderGraphImageLifetime {
        Imported,
        Transient,
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
        RenderGraphShaderStage initialShaderStage{RenderGraphShaderStage::None};
        RenderGraphImageState finalState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage finalShaderStage{RenderGraphShaderStage::None};
        RenderGraphImageLifetime lifetime{RenderGraphImageLifetime::Imported};
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
        DepthAttachmentRead,
        DepthAttachmentWrite,
        DepthSampledRead,
        TransferWrite,
    };

    struct RenderGraphResourceSlotSchema {
        std::string name;
        RenderGraphSlotAccess access{RenderGraphSlotAccess::ColorWrite};
        RenderGraphShaderStage shaderStage{RenderGraphShaderStage::None};
        bool optional{};
    };

    enum class RenderGraphCommandKind {
        SetShader,
        SetTexture,
        SetFloat,
        SetInt,
        SetVec4,
        DrawFullscreenTriangle,
        ClearColor,
    };

    struct RenderGraphPassSchema {
        std::string type;
        std::string paramsType;
        std::vector<RenderGraphResourceSlotSchema> resourceSlots;
        std::vector<RenderGraphCommandKind> allowedCommands;
        bool allowCulling{};
        bool hasSideEffects{};
    };

    struct RenderGraphCommand {
        RenderGraphCommandKind kind{RenderGraphCommandKind::SetShader};
        std::string name;
        std::string secondaryName;
        std::array<float, 4> floatValues{};
        int intValue{};
    };

    class RenderGraphCommandList {
    public:
        RenderGraphCommandList& setShader(std::string shaderAsset, std::string shaderPass) {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::SetShader,
                .name = std::move(shaderAsset),
                .secondaryName = std::move(shaderPass),
                .floatValues = {},
                .intValue = 0,
            });
            return *this;
        }

        RenderGraphCommandList& setTexture(std::string bindingName, std::string slotName) {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::SetTexture,
                .name = std::move(bindingName),
                .secondaryName = std::move(slotName),
                .floatValues = {},
                .intValue = 0,
            });
            return *this;
        }

        RenderGraphCommandList& setFloat(std::string bindingName, float value) {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::SetFloat,
                .name = std::move(bindingName),
                .secondaryName = {},
                .floatValues = {value, 0.0F, 0.0F, 0.0F},
                .intValue = 0,
            });
            return *this;
        }

        RenderGraphCommandList& setInt(std::string bindingName, int value) {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::SetInt,
                .name = std::move(bindingName),
                .secondaryName = {},
                .floatValues = {},
                .intValue = value,
            });
            return *this;
        }

        RenderGraphCommandList& setVec4(std::string bindingName, std::array<float, 4> value) {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::SetVec4,
                .name = std::move(bindingName),
                .secondaryName = {},
                .floatValues = value,
                .intValue = 0,
            });
            return *this;
        }

        RenderGraphCommandList& drawFullscreenTriangle() {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::DrawFullscreenTriangle,
                .name = {},
                .secondaryName = {},
                .floatValues = {},
                .intValue = 0,
            });
            return *this;
        }

        RenderGraphCommandList& clearColor(std::string slotName, std::array<float, 4> color) {
            commands_.push_back(RenderGraphCommand{
                .kind = RenderGraphCommandKind::ClearColor,
                .name = std::move(slotName),
                .secondaryName = {},
                .floatValues = color,
                .intValue = 0,
            });
            return *this;
        }

        [[nodiscard]] std::span<const RenderGraphCommand> commands() const {
            return commands_;
        }

        [[nodiscard]] std::vector<RenderGraphCommand> takeCommands() && {
            return std::move(commands_);
        }

    private:
        std::vector<RenderGraphCommand> commands_;
    };

    struct RenderGraphCompiledPass {
        std::string name;
        std::string type;
        std::string paramsType;
        std::size_t declarationIndex{};
        bool allowCulling{};
        bool hasSideEffects{};
        std::vector<std::byte> paramsData;
        std::vector<RenderGraphCommand> commands;
        std::vector<RenderGraphImageTransition> transitionsBefore;
        std::vector<RenderGraphImageHandle> colorWrites;
        std::vector<RenderGraphImageHandle> shaderReads;
        std::vector<RenderGraphImageHandle> depthReads;
        std::vector<RenderGraphImageHandle> depthWrites;
        std::vector<RenderGraphImageHandle> depthSampledReads;
        std::vector<RenderGraphImageHandle> transferWrites;
        std::vector<RenderGraphImageSlot> colorWriteSlots;
        std::vector<RenderGraphImageSlot> shaderReadSlots;
        std::vector<RenderGraphImageSlot> depthReadSlots;
        std::vector<RenderGraphImageSlot> depthWriteSlots;
        std::vector<RenderGraphImageSlot> depthSampledReadSlots;
        std::vector<RenderGraphImageSlot> transferWriteSlots;
    };

    struct RenderGraphPassDependency {
        std::size_t fromDeclarationIndex{};
        std::size_t toDeclarationIndex{};
        RenderGraphImageHandle image{};
        std::string imageName;
        std::string reason;
    };

    struct RenderGraphCulledPass {
        std::size_t declarationIndex{};
        std::string name;
        std::string type;
        std::string reason;
    };

    struct RenderGraphTransientImageAllocation {
        RenderGraphImageHandle image{};
        std::string imageName;
        RenderGraphImageFormat format{RenderGraphImageFormat::Undefined};
        RenderGraphExtent2D extent{};
        std::size_t firstPassIndex{};
        std::size_t lastPassIndex{};
        RenderGraphImageState finalState{RenderGraphImageState::Undefined};
        RenderGraphShaderStage finalShaderStage{RenderGraphShaderStage::None};
    };

    struct RenderGraphCompileResult {
        std::size_t declaredPassCount{};
        std::size_t declaredImageCount{};
        std::vector<RenderGraphCompiledPass> passes;
        std::vector<RenderGraphPassDependency> dependencies;
        std::vector<RenderGraphCulledPass> culledPasses;
        std::vector<RenderGraphTransientImageAllocation> transientImages;
        std::vector<RenderGraphImageTransition> finalTransitions;
    };

    struct RenderGraphPassContext {
        std::string_view name;
        std::string_view type;
        std::string_view paramsType;
        bool allowCulling{};
        bool hasSideEffects{};
        std::span<const std::byte> paramsData;
        std::span<const RenderGraphCommand> commands;
        std::span<const RenderGraphImageTransition> transitionsBefore;
        std::span<const RenderGraphImageHandle> colorWrites;
        std::span<const RenderGraphImageHandle> shaderReads;
        std::span<const RenderGraphImageHandle> depthReads;
        std::span<const RenderGraphImageHandle> depthWrites;
        std::span<const RenderGraphImageHandle> depthSampledReads;
        std::span<const RenderGraphImageHandle> transferWrites;
        std::span<const RenderGraphImageSlot> colorWriteSlots;
        std::span<const RenderGraphImageSlot> shaderReadSlots;
        std::span<const RenderGraphImageSlot> depthReadSlots;
        std::span<const RenderGraphImageSlot> depthWriteSlots;
        std::span<const RenderGraphImageSlot> depthSampledReadSlots;
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
            std::vector<std::byte> paramsData;
            std::vector<RenderGraphImageSlot> colorWriteSlots;
            std::vector<RenderGraphImageSlot> shaderReadSlots;
            std::vector<RenderGraphImageSlot> depthReadSlots;
            std::vector<RenderGraphImageSlot> depthWriteSlots;
            std::vector<RenderGraphImageSlot> depthSampledReadSlots;
            std::vector<RenderGraphImageSlot> transferWriteSlots;
            std::vector<RenderGraphCommand> commands;
            bool allowCulling{};
            bool hasSideEffects{};
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

            PassBuilder& readDepth(std::string slotName, RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].depthReadSlots.push_back(RenderGraphImageSlot{
                    .name = std::move(slotName),
                    .image = image,
                });
                return *this;
            }

            PassBuilder& writeDepth(std::string slotName, RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].depthWriteSlots.push_back(RenderGraphImageSlot{
                    .name = std::move(slotName),
                    .image = image,
                });
                return *this;
            }

            PassBuilder& readDepthTexture(std::string slotName, RenderGraphImageHandle image,
                                          RenderGraphShaderStage shaderStage) {
                graph_->passes_[passIndex_].depthSampledReadSlots.push_back(RenderGraphImageSlot{
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

            PassBuilder& allowCulling(bool allow = true) {
                graph_->passes_[passIndex_].allowCulling = allow;
                return *this;
            }

            PassBuilder& hasSideEffects(bool hasSideEffects = true) {
                graph_->passes_[passIndex_].hasSideEffects = hasSideEffects;
                return *this;
            }

            PassBuilder& setParamsType(std::string paramsType) {
                graph_->passes_[passIndex_].paramsType = std::move(paramsType);
                return *this;
            }

            PassBuilder& setParamsData(std::vector<std::byte> paramsData) {
                graph_->passes_[passIndex_].paramsData = std::move(paramsData);
                return *this;
            }

            template <typename Params>
            PassBuilder& setParams(std::string paramsType, const Params& params) {
                static_assert(std::is_trivially_copyable_v<Params>,
                              "RenderGraph pass params must be trivially copyable");
                setParamsType(std::move(paramsType));
                std::vector<std::byte> data(sizeof(Params));
                const auto* bytes = reinterpret_cast<const std::byte*>(&params);
                std::copy(bytes, bytes + sizeof(Params), data.begin());
                return setParamsData(std::move(data));
            }

            PassBuilder& execute(RenderGraphPassCallback callback) {
                graph_->passes_[passIndex_].callback = std::move(callback);
                return *this;
            }

            PassBuilder& setCommands(RenderGraphCommandList commands) {
                graph_->passes_[passIndex_].commands = std::move(commands).takeCommands();
                return *this;
            }

            template <typename Recorder> PassBuilder& recordCommands(Recorder&& recorder) {
                RenderGraphCommandList commands;
                std::forward<Recorder>(recorder)(commands);
                return setCommands(std::move(commands));
            }

        private:
            friend class RenderGraph;

            PassBuilder(RenderGraph& graph, std::size_t passIndex)
                : graph_(&graph), passIndex_(passIndex) {}

            RenderGraph* graph_{};
            std::size_t passIndex_{};
        };

        [[nodiscard]] RenderGraphImageHandle importImage(RenderGraphImageDesc desc) {
            desc.lifetime = RenderGraphImageLifetime::Imported;
            images_.push_back(std::move(desc));
            return RenderGraphImageHandle{
                .index = static_cast<std::uint32_t>(images_.size() - 1),
            };
        }

        [[nodiscard]] RenderGraphImageHandle createTransientImage(RenderGraphImageDesc desc) {
            desc.lifetime = RenderGraphImageLifetime::Transient;
            desc.initialState = RenderGraphImageState::Undefined;
            desc.initialShaderStage = RenderGraphShaderStage::None;
            desc.finalState = RenderGraphImageState::Undefined;
            desc.finalShaderStage = RenderGraphShaderStage::None;
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
                .paramsData = {},
                .colorWriteSlots = {},
                .shaderReadSlots = {},
                .depthReadSlots = {},
                .depthWriteSlots = {},
                .depthSampledReadSlots = {},
                .transferWriteSlots = {},
                .commands = {},
                .allowCulling = {},
                .hasSideEffects = {},
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
                .paramsData = {},
                .colorWriteSlots = {},
                .shaderReadSlots = {},
                .depthReadSlots = {},
                .depthWriteSlots = {},
                .depthSampledReadSlots = {},
                .transferWriteSlots = {},
                .commands = {},
                .allowCulling = {},
                .hasSideEffects = {},
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
            auto imagesValidated = validateImages();
            if (!imagesValidated) {
                return std::unexpected{std::move(imagesValidated.error())};
            }

            for (const Pass& pass : passes_) {
                auto passValidated = validatePass(pass, schemaRegistry);
                if (!passValidated) {
                    return std::unexpected{std::move(passValidated.error())};
                }
            }

            auto dependencies = buildDependencies();
            if (!dependencies) {
                return std::unexpected{std::move(dependencies.error())};
            }

            auto activePasses = findActivePasses(*dependencies, schemaRegistry);
            const std::vector<RenderGraphPassDependency> activeDependencies =
                filterActiveDependencies(*dependencies, activePasses);

            auto passOrder = sortPassesByDependencies(activeDependencies, activePasses);
            if (!passOrder) {
                return std::unexpected{std::move(passOrder.error())};
            }

            std::vector<RenderGraphImageAccess> currentAccesses;
            currentAccesses.reserve(images_.size());
            for (const RenderGraphImageDesc& image : images_) {
                currentAccesses.push_back(RenderGraphImageAccess{
                    .state = image.initialState,
                    .shaderStage = image.initialShaderStage,
                });
            }

            RenderGraphCompileResult result;
            result.declaredPassCount = passes_.size();
            result.declaredImageCount = images_.size();
            result.passes.reserve(activePassCount(activePasses));
            result.dependencies = activeDependencies;
            result.culledPasses = makeCulledPasses(activePasses);

            for (const std::size_t passIndex : *passOrder) {
                const Pass& pass = passes_[passIndex];
                const bool allowPassCulling = passAllowsCulling(pass, schemaRegistry);
                const bool passHasSideEffectsValue = passHasSideEffects(pass, schemaRegistry);

                RenderGraphCompiledPass compiledPass{
                    .name = pass.name,
                    .type = pass.type,
                    .paramsType = pass.paramsType,
                    .declarationIndex = passIndex,
                    .allowCulling = allowPassCulling,
                    .hasSideEffects = passHasSideEffectsValue,
                    .paramsData = pass.paramsData,
                    .commands = pass.commands,
                    .transitionsBefore = {},
                    .colorWrites = imageHandles(pass.colorWriteSlots),
                    .shaderReads = imageHandles(pass.shaderReadSlots),
                    .depthReads = imageHandles(pass.depthReadSlots),
                    .depthWrites = imageHandles(pass.depthWriteSlots),
                    .depthSampledReads = imageHandles(pass.depthSampledReadSlots),
                    .transferWrites = imageHandles(pass.transferWriteSlots),
                    .colorWriteSlots = pass.colorWriteSlots,
                    .shaderReadSlots = pass.shaderReadSlots,
                    .depthReadSlots = pass.depthReadSlots,
                    .depthWriteSlots = pass.depthWriteSlots,
                    .depthSampledReadSlots = pass.depthSampledReadSlots,
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

                auto depthReadTransitions =
                    transitionImages(compiledPass.depthReadSlots,
                                     RenderGraphImageAccess{
                                         .state = RenderGraphImageState::DepthAttachmentRead,
                                         .shaderStage = RenderGraphShaderStage::None,
                                     },
                                     currentAccesses, compiledPass);
                if (!depthReadTransitions) {
                    return std::unexpected{std::move(depthReadTransitions.error())};
                }

                auto depthWriteTransitions =
                    transitionImages(compiledPass.depthWriteSlots,
                                     RenderGraphImageAccess{
                                         .state = RenderGraphImageState::DepthAttachmentWrite,
                                         .shaderStage = RenderGraphShaderStage::None,
                                     },
                                     currentAccesses, compiledPass);
                if (!depthWriteTransitions) {
                    return std::unexpected{std::move(depthWriteTransitions.error())};
                }

                auto depthSampledReadTransitions =
                    transitionImages(compiledPass.depthSampledReadSlots,
                                     RenderGraphImageAccess{
                                         .state = RenderGraphImageState::DepthSampledRead,
                                         .shaderStage = RenderGraphShaderStage::None,
                                     },
                                     currentAccesses, compiledPass);
                if (!depthSampledReadTransitions) {
                    return std::unexpected{std::move(depthSampledReadTransitions.error())};
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
                if (image.lifetime == RenderGraphImageLifetime::Transient) {
                    const RenderGraphImageHandle imageHandle{
                        .index = static_cast<std::uint32_t>(index),
                    };
                    if (!imageUsedByCompiledPasses(result.passes, imageHandle)) {
                        if (imageUsedByDeclaredPasses(imageHandle)) {
                            continue;
                        }
                    }

                    auto allocation =
                        makeTransientAllocation(index, result.passes, currentAccesses[index]);
                    if (!allocation) {
                        return std::unexpected{std::move(allocation.error())};
                    }

                    result.transientImages.push_back(std::move(*allocation));
                    continue;
                }

                const RenderGraphImageAccess finalAccess{
                    .state = image.finalState,
                    .shaderStage = image.finalShaderStage,
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
            if (compiled.declaredPassCount != passes_.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Compiled render graph declaration count does not match the graph.",
                }};
            }

            std::vector<bool> executedDeclarations(passes_.size());
            for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
                const RenderGraphCompiledPass& pass = compiled.passes[index];
                if (pass.declarationIndex >= passes_.size() ||
                    passes_[pass.declarationIndex].name != pass.name) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Compiled render graph pass '" + pass.name +
                            "' does not match the graph declaration.",
                    }};
                }
                if (executedDeclarations[pass.declarationIndex]) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Compiled render graph pass '" + pass.name + "' appears more than once.",
                    }};
                }
                executedDeclarations[pass.declarationIndex] = true;

                const RenderGraphPassCallback* callback = &passes_[pass.declarationIndex].callback;
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
                    .allowCulling = pass.allowCulling,
                    .hasSideEffects = pass.hasSideEffects,
                    .paramsData = pass.paramsData,
                    .commands = pass.commands,
                    .transitionsBefore = pass.transitionsBefore,
                    .colorWrites = pass.colorWrites,
                    .shaderReads = pass.shaderReads,
                    .depthReads = pass.depthReads,
                    .depthWrites = pass.depthWrites,
                    .depthSampledReads = pass.depthSampledReads,
                    .transferWrites = pass.transferWrites,
                    .colorWriteSlots = pass.colorWriteSlots,
                    .shaderReadSlots = pass.shaderReadSlots,
                    .depthReadSlots = pass.depthReadSlots,
                    .depthWriteSlots = pass.depthWriteSlots,
                    .depthSampledReadSlots = pass.depthSampledReadSlots,
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
            output += "| # | Name | Lifetime | Format | Extent | Initial | Final |\n";
            output += "|---:|---|---|---|---:|---|---|\n";
            for (std::size_t index = 0; index < images_.size(); ++index) {
                const RenderGraphImageDesc& image = images_[index];
                output += "| ";
                output += std::to_string(index);
                output += " | ";
                output += image.name;
                output += " | ";
                output += imageLifetimeName(image.lifetime);
                output += " | ";
                output += imageFormatName(image.format);
                output += " | ";
                output += std::to_string(image.extent.width);
                output += "x";
                output += std::to_string(image.extent.height);
                output += " | ";
                output += imageAccessName(image.initialState, image.initialShaderStage);
                output += " | ";
                output += imageAccessName(image.finalState, image.finalShaderStage);
                output += " |\n";
            }

            output += "\n### RenderGraph Passes\n\n";
            output += "| # | Decl # | Name | Type | Params | Cullable | Side Effects | "
                      "Before Transitions | Color Writes | Shader Reads | Depth Reads | "
                      "Depth Writes | Depth Sampled Reads | "
                      "Transfer Writes |\n";
            output += "|---:|---:|---|---|---|---|---|---:|---|---|---|---|---|---|\n";
            for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
                const RenderGraphCompiledPass& pass = compiled.passes[index];
                output += "| ";
                output += std::to_string(index);
                output += " | ";
                output += std::to_string(pass.declarationIndex);
                output += " | ";
                output += pass.name;
                output += " | ";
                output += pass.type.empty() ? "-" : pass.type;
                output += " | ";
                output += pass.paramsType.empty() ? "-" : pass.paramsType;
                output += " | ";
                output += pass.allowCulling ? "yes" : "no";
                output += " | ";
                output += pass.hasSideEffects ? "yes" : "no";
                output += " | ";
                output += std::to_string(pass.transitionsBefore.size());
                output += " | ";
                output += imageSlotList(pass.colorWriteSlots);
                output += " | ";
                output += imageSlotList(pass.shaderReadSlots);
                output += " | ";
                output += imageSlotList(pass.depthReadSlots);
                output += " | ";
                output += imageSlotList(pass.depthWriteSlots);
                output += " | ";
                output += imageSlotList(pass.depthSampledReadSlots);
                output += " | ";
                output += imageSlotList(pass.transferWriteSlots);
                output += " |\n";
            }

            output += "\n### RenderGraph Dependencies\n\n";
            output += "| From | To | Image | Reason |\n";
            output += "|---|---|---|---|\n";
            for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
                output += "| ";
                output += passDeclarationLabel(dependency.fromDeclarationIndex);
                output += " | ";
                output += passDeclarationLabel(dependency.toDeclarationIndex);
                output += " | ";
                output += imageHandleLabel(dependency.image);
                output += " | ";
                output += dependency.reason;
                output += " |\n";
            }

            output += "\n### RenderGraph Culled Passes\n\n";
            output += "| Decl # | Name | Type | Reason |\n";
            output += "|---:|---|---|---|\n";
            for (const RenderGraphCulledPass& pass : compiled.culledPasses) {
                output += "| ";
                output += std::to_string(pass.declarationIndex);
                output += " | ";
                output += pass.name;
                output += " | ";
                output += pass.type.empty() ? "-" : pass.type;
                output += " | ";
                output += pass.reason;
                output += " |\n";
            }

            output += "\n### RenderGraph Slots\n\n";
            output += "| Pass | Access | Slot | Image |\n";
            output += "|---|---|---|---|\n";
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                appendSlotRows(output, pass.name, "ColorWrite", pass.colorWriteSlots);
                appendSlotRows(output, pass.name, "ShaderRead", pass.shaderReadSlots);
                appendSlotRows(output, pass.name, "DepthAttachmentRead", pass.depthReadSlots);
                appendSlotRows(output, pass.name, "DepthAttachmentWrite", pass.depthWriteSlots);
                appendSlotRows(output, pass.name, "DepthSampledRead", pass.depthSampledReadSlots);
                appendSlotRows(output, pass.name, "TransferWrite", pass.transferWriteSlots);
            }

            output += "\n### RenderGraph Commands\n\n";
            output += "| Pass | # | Command | Detail |\n";
            output += "|---|---:|---|---|\n";
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                appendCommandRows(output, pass);
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

            output += "\n### RenderGraph Transients\n\n";
            output += "| Image | Format | Extent | First Pass | Last Pass | Final Access |\n";
            output += "|---|---|---:|---:|---:|---|\n";
            for (const RenderGraphTransientImageAllocation& transient : compiled.transientImages) {
                output += "| ";
                output += imageHandleLabel(transient.image);
                output += " | ";
                output += imageFormatName(transient.format);
                output += " | ";
                output += std::to_string(transient.extent.width);
                output += "x";
                output += std::to_string(transient.extent.height);
                output += " | ";
                output += std::to_string(transient.firstPassIndex);
                output += " | ";
                output += std::to_string(transient.lastPassIndex);
                output += " | ";
                output += imageAccessName(transient.finalState, transient.finalShaderStage);
                output += " |\n";
            }

            return output;
        }

    private:
        struct RenderGraphImageSlotGroup {
            std::string_view access;
            std::span<const RenderGraphImageSlot> slots;
        };

        [[nodiscard]] Result<void> validateImages() const {
            for (const RenderGraphImageDesc& image : images_) {
                if (image.lifetime == RenderGraphImageLifetime::Imported &&
                    image.finalState == RenderGraphImageState::Undefined) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Imported render graph image '" + image.name +
                            "' must declare an explicit final state.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validatePass(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) const {
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

            return {};
        }

        [[nodiscard]] std::vector<bool>
        findActivePasses(std::span<const RenderGraphPassDependency> dependencies,
                         const RenderGraphSchemaRegistry* schemaRegistry) const {
            std::vector<bool> activePasses(passes_.size());
            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                if (!passCanBeCulled(passes_[passIndex], schemaRegistry)) {
                    activePasses[passIndex] = true;
                }
            }

            bool changed = true;
            while (changed) {
                changed = false;
                for (const RenderGraphPassDependency& dependency : dependencies) {
                    if (dependency.toDeclarationIndex >= activePasses.size() ||
                        dependency.fromDeclarationIndex >= activePasses.size()) {
                        continue;
                    }
                    if (activePasses[dependency.toDeclarationIndex] &&
                        !activePasses[dependency.fromDeclarationIndex]) {
                        activePasses[dependency.fromDeclarationIndex] = true;
                        changed = true;
                    }
                }
            }

            return activePasses;
        }

        [[nodiscard]] std::vector<RenderGraphPassDependency>
        filterActiveDependencies(std::span<const RenderGraphPassDependency> dependencies,
                                 const std::vector<bool>& activePasses) const {
            std::vector<RenderGraphPassDependency> activeDependencies;
            activeDependencies.reserve(dependencies.size());
            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex >= activePasses.size() ||
                    dependency.toDeclarationIndex >= activePasses.size()) {
                    continue;
                }
                if (activePasses[dependency.fromDeclarationIndex] &&
                    activePasses[dependency.toDeclarationIndex]) {
                    activeDependencies.push_back(dependency);
                }
            }

            return activeDependencies;
        }

        [[nodiscard]] std::vector<RenderGraphCulledPass>
        makeCulledPasses(const std::vector<bool>& activePasses) const {
            std::vector<RenderGraphCulledPass> culledPasses;
            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                if (passIndex < activePasses.size() && activePasses[passIndex]) {
                    continue;
                }

                const Pass& pass = passes_[passIndex];
                culledPasses.push_back(RenderGraphCulledPass{
                    .declarationIndex = passIndex,
                    .name = pass.name,
                    .type = pass.type,
                    .reason = "cullable pass has no active consumers or side effects",
                });
            }

            return culledPasses;
        }

        [[nodiscard]] static std::size_t activePassCount(const std::vector<bool>& activePasses) {
            std::size_t count = 0;
            for (const bool active : activePasses) {
                if (active) {
                    ++count;
                }
            }

            return count;
        }

        [[nodiscard]] Result<std::vector<RenderGraphPassDependency>> buildDependencies() const {
            std::vector<RenderGraphPassDependency> dependencies;

            for (std::size_t imageIndex = 0; imageIndex < images_.size(); ++imageIndex) {
                const RenderGraphImageHandle imageHandle{
                    .index = static_cast<std::uint32_t>(imageIndex),
                };
                std::vector<std::size_t> writers;
                std::vector<std::size_t> readers;

                for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                    const Pass& pass = passes_[passIndex];
                    if (passWritesImage(pass, imageHandle)) {
                        writers.push_back(passIndex);
                    }
                    if (passReadsImage(pass, imageHandle)) {
                        readers.push_back(passIndex);
                    }
                }

                for (std::size_t writerIndex = 1; writerIndex < writers.size(); ++writerIndex) {
                    addDependency(dependencies, writers[writerIndex - 1], writers[writerIndex],
                                  imageHandle, "write order");
                }

                auto readDependencies =
                    addReadDependencies(dependencies, imageHandle, writers, readers);
                if (!readDependencies) {
                    return std::unexpected{std::move(readDependencies.error())};
                }
            }

            return dependencies;
        }

        [[nodiscard]] Result<void>
        addReadDependencies(std::vector<RenderGraphPassDependency>& dependencies,
                            RenderGraphImageHandle image, std::span<const std::size_t> writers,
                            std::span<const std::size_t> readers) const {
            const RenderGraphImageDesc& imageDesc = images_[image.index];

            for (const std::size_t reader : readers) {
                if (writers.empty()) {
                    if (!imageCanBeReadFromInitialState(imageDesc)) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            missingProducerMessage(reader, image),
                        }};
                    }
                    continue;
                }

                std::size_t sourceWriter{};
                bool hasSourceWriter = false;
                for (const std::size_t writer : writers) {
                    if (writer < reader) {
                        sourceWriter = writer;
                        hasSourceWriter = true;
                    }
                }

                if (!hasSourceWriter && imageCanBeReadFromInitialState(imageDesc)) {
                    for (const std::size_t writer : writers) {
                        if (writer > reader) {
                            addDependency(dependencies, reader, writer, image,
                                          "initial read before overwrite");
                        }
                    }
                    continue;
                }

                if (!hasSourceWriter) {
                    if (writers.size() != 1) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            ambiguousProducerMessage(reader, image, writers),
                        }};
                    }
                    sourceWriter = writers.front();
                    hasSourceWriter = true;
                }

                addDependency(dependencies, sourceWriter, reader, image, "producer read");

                for (const std::size_t writer : writers) {
                    if (writer > reader && writer != sourceWriter) {
                        addDependency(dependencies, reader, writer, image, "read before overwrite");
                    }
                }
            }

            return {};
        }

        void addDependency(std::vector<RenderGraphPassDependency>& dependencies,
                           std::size_t fromPassIndex, std::size_t toPassIndex,
                           RenderGraphImageHandle image, std::string reason) const {
            if (fromPassIndex == toPassIndex) {
                return;
            }

            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex == fromPassIndex &&
                    dependency.toDeclarationIndex == toPassIndex && dependency.image == image &&
                    dependency.reason == reason) {
                    return;
                }
            }

            dependencies.push_back(RenderGraphPassDependency{
                .fromDeclarationIndex = fromPassIndex,
                .toDeclarationIndex = toPassIndex,
                .image = image,
                .imageName = images_[image.index].name,
                .reason = std::move(reason),
            });
        }

        [[nodiscard]] Result<std::vector<std::size_t>>
        sortPassesByDependencies(std::span<const RenderGraphPassDependency> dependencies,
                                 const std::vector<bool>& activePasses) const {
            if (activePasses.size() != passes_.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph active pass set does not match the graph.",
                }};
            }

            std::vector<std::vector<std::size_t>> adjacency(passes_.size());
            std::vector<std::size_t> indegrees(passes_.size());

            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex >= passes_.size() ||
                    dependency.toDeclarationIndex >= passes_.size()) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph dependency references a pass outside the graph.",
                    }};
                }

                if (!activePasses[dependency.fromDeclarationIndex] ||
                    !activePasses[dependency.toDeclarationIndex]) {
                    continue;
                }

                if (addTopoEdge(adjacency, dependency.fromDeclarationIndex,
                                dependency.toDeclarationIndex)) {
                    ++indegrees[dependency.toDeclarationIndex];
                }
            }

            std::vector<std::size_t> order;
            const std::size_t targetPassCount = activePassCount(activePasses);
            order.reserve(targetPassCount);
            std::vector<bool> emitted(passes_.size());

            while (order.size() < targetPassCount) {
                std::size_t nextPass = passes_.size();
                for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                    if (activePasses[passIndex] && !emitted[passIndex] &&
                        indegrees[passIndex] == 0) {
                        nextPass = passIndex;
                        break;
                    }
                }

                if (nextPass == passes_.size()) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        dependencyCycleMessage(dependencies, activePasses, emitted, adjacency),
                    }};
                }

                emitted[nextPass] = true;
                order.push_back(nextPass);
                for (const std::size_t dependent : adjacency[nextPass]) {
                    --indegrees[dependent];
                }
            }

            return order;
        }

        [[nodiscard]] static bool addTopoEdge(std::vector<std::vector<std::size_t>>& adjacency,
                                              std::size_t fromPassIndex, std::size_t toPassIndex) {
            for (const std::size_t existing : adjacency[fromPassIndex]) {
                if (existing == toPassIndex) {
                    return false;
                }
            }

            adjacency[fromPassIndex].push_back(toPassIndex);
            return true;
        }

        [[nodiscard]] std::string
        dependencyCycleMessage(std::span<const RenderGraphPassDependency> dependencies,
                               const std::vector<bool>& activePasses,
                               const std::vector<bool>& emitted,
                               const std::vector<std::vector<std::size_t>>& adjacency) const {
            std::string message = "Render graph contains a pass dependency cycle.";

            std::size_t cycleFrom = passes_.size();
            std::size_t cycleTo = passes_.size();
            if (findDependencyCycleEdge(adjacency, activePasses, emitted, cycleFrom, cycleTo)) {
                message += " Cycle edge ";
                message += passDeclarationLabel(cycleFrom);
                message += " -> ";
                message += passDeclarationLabel(cycleTo);

                const RenderGraphPassDependency* dependency =
                    findDependencyForEdge(dependencies, cycleFrom, cycleTo);
                if (dependency != nullptr) {
                    message += " through image '";
                    message += imageHandleLabel(dependency->image);
                    message += "'";
                    if (!dependency->reason.empty()) {
                        message += " (";
                        message += dependency->reason;
                        message += ")";
                    }
                }

                message += ".";
            }

            message += " Remaining passes: ";
            bool firstRemaining = true;
            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                if (!activePasses[passIndex] || emitted[passIndex]) {
                    continue;
                }
                if (!firstRemaining) {
                    message += ", ";
                }
                firstRemaining = false;
                message += passDeclarationLabel(passIndex);
            }
            if (firstRemaining) {
                message += "-";
            }
            message += ".";

            return message;
        }

        [[nodiscard]] bool
        findDependencyCycleEdge(const std::vector<std::vector<std::size_t>>& adjacency,
                                const std::vector<bool>& activePasses,
                                const std::vector<bool>& emitted, std::size_t& cycleFrom,
                                std::size_t& cycleTo) const {
            std::vector<std::uint8_t> visitStates(passes_.size());
            std::function<bool(std::size_t)> visit = [&](std::size_t passIndex) {
                visitStates[passIndex] = 1;
                for (const std::size_t dependent : adjacency[passIndex]) {
                    if (!activePasses[dependent] || emitted[dependent]) {
                        continue;
                    }
                    if (visitStates[dependent] == 1) {
                        cycleFrom = passIndex;
                        cycleTo = dependent;
                        return true;
                    }
                    if (visitStates[dependent] == 0 && visit(dependent)) {
                        return true;
                    }
                }

                visitStates[passIndex] = 2;
                return false;
            };

            for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
                if (!activePasses[passIndex] || emitted[passIndex] || visitStates[passIndex] != 0) {
                    continue;
                }
                if (visit(passIndex)) {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] static const RenderGraphPassDependency*
        findDependencyForEdge(std::span<const RenderGraphPassDependency> dependencies,
                              std::size_t fromPassIndex, std::size_t toPassIndex) {
            for (const RenderGraphPassDependency& dependency : dependencies) {
                if (dependency.fromDeclarationIndex == fromPassIndex &&
                    dependency.toDeclarationIndex == toPassIndex) {
                    return &dependency;
                }
            }

            return nullptr;
        }

        [[nodiscard]] std::string missingProducerMessage(std::size_t reader,
                                                         RenderGraphImageHandle image) const {
            std::string message = "Render graph pass '";
            message += passDeclarationLabel(reader);
            message += "' reads image '";
            message += imageHandleLabel(image);
            message += "' before any pass writes it. Candidate writers: -.";
            return message;
        }

        [[nodiscard]] std::string
        ambiguousProducerMessage(std::size_t reader, RenderGraphImageHandle image,
                                 std::span<const std::size_t> writers) const {
            std::string message = "Render graph pass '";
            message += passDeclarationLabel(reader);
            message += "' reads image '";
            message += imageHandleLabel(image);
            message += "' before a unique producing writer can be inferred. Candidate writers: ";
            message += passDeclarationList(writers);
            message += ".";
            return message;
        }

        [[nodiscard]] static bool
        imageCanBeReadFromInitialState(const RenderGraphImageDesc& image) {
            return image.lifetime == RenderGraphImageLifetime::Imported &&
                   image.initialState != RenderGraphImageState::Undefined;
        }

        [[nodiscard]] bool passCanBeCulled(const Pass& pass,
                                           const RenderGraphSchemaRegistry* schemaRegistry) const {
            return passAllowsCulling(pass, schemaRegistry) &&
                   !passHasSideEffects(pass, schemaRegistry) && !passWritesImportedImage(pass);
        }

        [[nodiscard]] bool
        passAllowsCulling(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) const {
            const RenderGraphPassSchema* schema = passSchema(pass, schemaRegistry);
            return pass.allowCulling || (schema != nullptr && schema->allowCulling);
        }

        [[nodiscard]] bool
        passHasSideEffects(const Pass& pass,
                           const RenderGraphSchemaRegistry* schemaRegistry) const {
            const RenderGraphPassSchema* schema = passSchema(pass, schemaRegistry);
            return pass.hasSideEffects || (schema != nullptr && schema->hasSideEffects);
        }

        [[nodiscard]] const RenderGraphPassSchema*
        passSchema(const Pass& pass, const RenderGraphSchemaRegistry* schemaRegistry) const {
            if (schemaRegistry == nullptr || pass.type.empty()) {
                return nullptr;
            }

            return schemaRegistry->find(pass.type);
        }

        [[nodiscard]] bool passWritesImportedImage(const Pass& pass) const {
            const std::array<std::span<const RenderGraphImageSlot>, 3> writeSlotGroups{
                pass.colorWriteSlots,
                pass.depthWriteSlots,
                pass.transferWriteSlots,
            };
            for (std::span<const RenderGraphImageSlot> slots : writeSlotGroups) {
                for (const RenderGraphImageSlot& slot : slots) {
                    if (slot.image.index < images_.size() &&
                        images_[slot.image.index].lifetime == RenderGraphImageLifetime::Imported) {
                        return true;
                    }
                }
            }

            return false;
        }

        [[nodiscard]] static bool passReadsImage(const Pass& pass, RenderGraphImageHandle image) {
            return slotsUseImage(pass.shaderReadSlots, image) ||
                   slotsUseImage(pass.depthReadSlots, image) ||
                   slotsUseImage(pass.depthSampledReadSlots, image);
        }

        [[nodiscard]] static bool passWritesImage(const Pass& pass, RenderGraphImageHandle image) {
            return slotsUseImage(pass.colorWriteSlots, image) ||
                   slotsUseImage(pass.depthWriteSlots, image) ||
                   slotsUseImage(pass.transferWriteSlots, image);
        }

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

            auto depthReadSlots = validateSlots(pass, pass.depthReadSlots);
            if (!depthReadSlots) {
                return std::unexpected{std::move(depthReadSlots.error())};
            }

            auto depthWriteSlots = validateSlots(pass, pass.depthWriteSlots);
            if (!depthWriteSlots) {
                return std::unexpected{std::move(depthWriteSlots.error())};
            }

            auto depthSampledReadSlots = validateDepthSampledReadSlots(pass);
            if (!depthSampledReadSlots) {
                return std::unexpected{std::move(depthSampledReadSlots.error())};
            }

            auto transferSlots = validateSlots(pass, pass.transferWriteSlots);
            if (!transferSlots) {
                return std::unexpected{std::move(transferSlots.error())};
            }

            const std::array<std::span<const RenderGraphImageSlot>, 6> slotGroups{
                pass.colorWriteSlots, pass.shaderReadSlots,       pass.depthReadSlots,
                pass.depthWriteSlots, pass.depthSampledReadSlots, pass.transferWriteSlots,
            };
            auto duplicateSlots = validateUniqueSlotNames(pass, slotGroups);
            if (!duplicateSlots) {
                return std::unexpected{std::move(duplicateSlots.error())};
            }

            auto imageAccesses = validateUniqueImageAccesses(pass, slotGroups);
            if (!imageAccesses) {
                return std::unexpected{std::move(imageAccesses.error())};
            }

            return {};
        }

        [[nodiscard]] Result<void> validateUniqueImageAccesses(
            const Pass& pass,
            std::span<const std::span<const RenderGraphImageSlot>> slotGroups) const {
            const std::array<RenderGraphImageSlotGroup, 6> namedGroups{
                RenderGraphImageSlotGroup{
                    .access = "ColorWrite",
                    .slots = slotGroups[0],
                },
                RenderGraphImageSlotGroup{
                    .access = "ShaderRead",
                    .slots = slotGroups[1],
                },
                RenderGraphImageSlotGroup{
                    .access = "DepthAttachmentRead",
                    .slots = slotGroups[2],
                },
                RenderGraphImageSlotGroup{
                    .access = "DepthAttachmentWrite",
                    .slots = slotGroups[3],
                },
                RenderGraphImageSlotGroup{
                    .access = "DepthSampledRead",
                    .slots = slotGroups[4],
                },
                RenderGraphImageSlotGroup{
                    .access = "TransferWrite",
                    .slots = slotGroups[5],
                },
            };

            for (std::size_t groupIndex = 0; groupIndex < namedGroups.size(); ++groupIndex) {
                const RenderGraphImageSlotGroup& group = namedGroups[groupIndex];
                for (std::size_t slotIndex = 0; slotIndex < group.slots.size(); ++slotIndex) {
                    const RenderGraphImageSlot& slot = group.slots[slotIndex];
                    for (std::size_t otherGroupIndex = groupIndex;
                         otherGroupIndex < namedGroups.size(); ++otherGroupIndex) {
                        const RenderGraphImageSlotGroup& otherGroup = namedGroups[otherGroupIndex];
                        const std::size_t otherSlotBegin =
                            otherGroupIndex == groupIndex ? slotIndex + 1 : 0;
                        for (std::size_t otherSlotIndex = otherSlotBegin;
                             otherSlotIndex < otherGroup.slots.size(); ++otherSlotIndex) {
                            const RenderGraphImageSlot& otherSlot =
                                otherGroup.slots[otherSlotIndex];
                            if (slot.image == otherSlot.image) {
                                return std::unexpected{Error{
                                    ErrorDomain::RenderGraph,
                                    0,
                                    imageAccessConflictMessage(pass, slot, group.access, otherSlot,
                                                               otherGroup.access),
                                }};
                            }
                        }
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

            auto depthReadSlots = validateSlotsAgainstSchema(
                pass, pass.depthReadSlots, RenderGraphSlotAccess::DepthAttachmentRead, *schema);
            if (!depthReadSlots) {
                return std::unexpected{std::move(depthReadSlots.error())};
            }

            auto depthWriteSlots = validateSlotsAgainstSchema(
                pass, pass.depthWriteSlots, RenderGraphSlotAccess::DepthAttachmentWrite, *schema);
            if (!depthWriteSlots) {
                return std::unexpected{std::move(depthWriteSlots.error())};
            }

            auto depthSampledReadSlots = validateSlotsAgainstSchema(
                pass, pass.depthSampledReadSlots, RenderGraphSlotAccess::DepthSampledRead, *schema);
            if (!depthSampledReadSlots) {
                return std::unexpected{std::move(depthSampledReadSlots.error())};
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

            auto commands = validateCommandsAgainstSchema(pass, *schema);
            if (!commands) {
                return std::unexpected{std::move(commands.error())};
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateCommandsAgainstSchema(const Pass& pass, const RenderGraphPassSchema& schema) const {
            for (const RenderGraphCommand& command : pass.commands) {
                if (!commandAllowedBySchema(command.kind, schema)) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' command '" +
                            std::string{commandKindName(command.kind)} +
                            "' is not allowed by schema '" + schema.type + "'.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] static bool commandAllowedBySchema(RenderGraphCommandKind commandKind,
                                                         const RenderGraphPassSchema& schema) {
            for (const RenderGraphCommandKind allowed : schema.allowedCommands) {
                if (allowed == commandKind) {
                    return true;
                }
            }

            return false;
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
            case RenderGraphSlotAccess::DepthAttachmentRead:
                return pass.depthReadSlots;
            case RenderGraphSlotAccess::DepthAttachmentWrite:
                return pass.depthWriteSlots;
            case RenderGraphSlotAccess::DepthSampledRead:
                return pass.depthSampledReadSlots;
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

        [[nodiscard]] Result<void> validateDepthSampledReadSlots(const Pass& pass) const {
            auto slots = validateSlots(pass, pass.depthSampledReadSlots);
            if (!slots) {
                return std::unexpected{std::move(slots.error())};
            }

            for (const RenderGraphImageSlot& slot : pass.depthSampledReadSlots) {
                if (slot.shaderStage == RenderGraphShaderStage::None) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' declares depth sampled read slot '" +
                            slot.name + "' without a shader stage.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] Result<void> validateUniqueSlotNames(
            const Pass& pass,
            std::span<const std::span<const RenderGraphImageSlot>> slotGroups) const {
            for (std::size_t groupIndex = 0; groupIndex < slotGroups.size(); ++groupIndex) {
                for (const RenderGraphImageSlot& slot : slotGroups[groupIndex]) {
                    for (std::size_t otherGroupIndex = groupIndex + 1;
                         otherGroupIndex < slotGroups.size(); ++otherGroupIndex) {
                        for (const RenderGraphImageSlot& otherSlot : slotGroups[otherGroupIndex]) {
                            if (slot.name == otherSlot.name) {
                                return std::unexpected{Error{
                                    ErrorDomain::RenderGraph,
                                    0,
                                    duplicateSlotMessage(pass, slot.name),
                                }};
                            }
                        }
                    }
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

        [[nodiscard]] Result<RenderGraphTransientImageAllocation>
        makeTransientAllocation(std::size_t imageIndex,
                                std::span<const RenderGraphCompiledPass> passes,
                                RenderGraphImageAccess finalAccess) const {
            std::size_t firstPass = passes.size();
            std::size_t lastPass{};
            const RenderGraphImageHandle imageHandle{
                .index = static_cast<std::uint32_t>(imageIndex),
            };

            for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
                if (!passUsesImage(passes[passIndex], imageHandle)) {
                    continue;
                }

                if (firstPass == passes.size()) {
                    firstPass = passIndex;
                }
                lastPass = passIndex;
            }

            const RenderGraphImageDesc& image = images_[imageIndex];
            if (firstPass == passes.size()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Transient render graph image '" + image.name + "' is never used.",
                }};
            }

            return RenderGraphTransientImageAllocation{
                .image = imageHandle,
                .imageName = image.name,
                .format = image.format,
                .extent = image.extent,
                .firstPassIndex = firstPass,
                .lastPassIndex = lastPass,
                .finalState = finalAccess.state,
                .finalShaderStage = finalAccess.shaderStage,
            };
        }

        [[nodiscard]] static bool passUsesImage(const RenderGraphCompiledPass& pass,
                                                RenderGraphImageHandle image) {
            return slotsUseImage(pass.colorWriteSlots, image) ||
                   slotsUseImage(pass.shaderReadSlots, image) ||
                   slotsUseImage(pass.depthReadSlots, image) ||
                   slotsUseImage(pass.depthWriteSlots, image) ||
                   slotsUseImage(pass.depthSampledReadSlots, image) ||
                   slotsUseImage(pass.transferWriteSlots, image);
        }

        [[nodiscard]] static bool
        imageUsedByCompiledPasses(std::span<const RenderGraphCompiledPass> passes,
                                  RenderGraphImageHandle image) {
            for (const RenderGraphCompiledPass& pass : passes) {
                if (passUsesImage(pass, image)) {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool imageUsedByDeclaredPasses(RenderGraphImageHandle image) const {
            for (const Pass& pass : passes_) {
                if (passReadsImage(pass, image) || passWritesImage(pass, image)) {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] static bool slotsUseImage(std::span<const RenderGraphImageSlot> slots,
                                                RenderGraphImageHandle image) {
            for (const RenderGraphImageSlot& slot : slots) {
                if (slot.image == image) {
                    return true;
                }
            }

            return false;
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
                if (slotAccess.state == RenderGraphImageState::ShaderRead ||
                    slotAccess.state == RenderGraphImageState::DepthSampledRead) {
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
            case RenderGraphImageFormat::D32Sfloat:
                return "D32Sfloat";
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
            case RenderGraphImageState::DepthAttachmentRead:
                return "DepthAttachmentRead";
            case RenderGraphImageState::DepthAttachmentWrite:
                return "DepthAttachmentWrite";
            case RenderGraphImageState::DepthSampledRead:
                return "DepthSampledRead";
            case RenderGraphImageState::TransferDst:
                return "TransferDst";
            case RenderGraphImageState::Present:
                return "Present";
            case RenderGraphImageState::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] static std::string_view imageLifetimeName(RenderGraphImageLifetime lifetime) {
            switch (lifetime) {
            case RenderGraphImageLifetime::Transient:
                return "Transient";
            case RenderGraphImageLifetime::Imported:
            default:
                return "Imported";
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

        [[nodiscard]] std::string imageAccessConflictMessage(const Pass& pass,
                                                             const RenderGraphImageSlot& slot,
                                                             std::string_view access,
                                                             const RenderGraphImageSlot& otherSlot,
                                                             std::string_view otherAccess) const {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "' declares image '";
            message += imageHandleLabel(slot.image);
            message += "' more than once in slots '";
            message += slot.name;
            message += "' (";
            message += access;
            message += ") and '";
            message += otherSlot.name;
            message += "' (";
            message += otherAccess;
            message += "). Split the operation into separate passes or add an explicit combined "
                       "access state.";
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

        [[nodiscard]] std::string passDeclarationLabel(std::size_t passIndex) const {
            std::string label = "#";
            label += std::to_string(passIndex);
            if (passIndex < passes_.size() && !passes_[passIndex].name.empty()) {
                label += " ";
                label += passes_[passIndex].name;
            }
            return label;
        }

        [[nodiscard]] std::string
        passDeclarationList(std::span<const std::size_t> passIndices) const {
            if (passIndices.empty()) {
                return "-";
            }

            std::string labels;
            for (std::size_t index = 0; index < passIndices.size(); ++index) {
                if (index > 0) {
                    labels += ", ";
                }
                labels += passDeclarationLabel(passIndices[index]);
            }
            return labels;
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

        [[nodiscard]] static std::string_view commandKindName(RenderGraphCommandKind kind) {
            switch (kind) {
            case RenderGraphCommandKind::SetShader:
                return "SetShader";
            case RenderGraphCommandKind::SetTexture:
                return "SetTexture";
            case RenderGraphCommandKind::SetFloat:
                return "SetFloat";
            case RenderGraphCommandKind::SetInt:
                return "SetInt";
            case RenderGraphCommandKind::SetVec4:
                return "SetVec4";
            case RenderGraphCommandKind::DrawFullscreenTriangle:
                return "DrawFullscreenTriangle";
            case RenderGraphCommandKind::ClearColor:
                return "ClearColor";
            }
            return "";
        }

        [[nodiscard]] static std::string commandDetail(const RenderGraphCommand& command) {
            switch (command.kind) {
            case RenderGraphCommandKind::SetShader:
            case RenderGraphCommandKind::SetTexture:
                return command.name + " -> " + command.secondaryName;
            case RenderGraphCommandKind::SetFloat:
                return command.name + " = " + std::to_string(command.floatValues[0]);
            case RenderGraphCommandKind::SetInt:
                return command.name + " = " + std::to_string(command.intValue);
            case RenderGraphCommandKind::SetVec4:
            case RenderGraphCommandKind::ClearColor:
                return command.name + " = (" + std::to_string(command.floatValues[0]) + ", " +
                       std::to_string(command.floatValues[1]) + ", " +
                       std::to_string(command.floatValues[2]) + ", " +
                       std::to_string(command.floatValues[3]) + ")";
            case RenderGraphCommandKind::DrawFullscreenTriangle:
                return "-";
            }
            return "-";
        }

        [[nodiscard]] std::string imageAccessName(RenderGraphImageState state,
                                                  RenderGraphShaderStage shaderStage) const {
            std::string name{imageStateName(state)};
            if ((state == RenderGraphImageState::ShaderRead ||
                 state == RenderGraphImageState::DepthSampledRead) &&
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

        void appendCommandRows(std::string& output, const RenderGraphCompiledPass& pass) const {
            for (std::size_t index = 0; index < pass.commands.size(); ++index) {
                const RenderGraphCommand& command = pass.commands[index];
                output += "| ";
                output += pass.name;
                output += " | ";
                output += std::to_string(index);
                output += " | ";
                output += commandKindName(command.kind);
                output += " | ";
                output += commandDetail(command);
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
