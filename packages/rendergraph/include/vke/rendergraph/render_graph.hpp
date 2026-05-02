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
        TransferDst,
        Present,
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
        RenderGraphImageState newState{RenderGraphImageState::Undefined};
    };

    struct RenderGraphCompiledPass {
        std::string name;
        std::string type;
        std::vector<RenderGraphImageTransition> transitionsBefore;
        std::vector<RenderGraphImageHandle> colorWrites;
        std::vector<RenderGraphImageHandle> transferWrites;
    };

    struct RenderGraphCompileResult {
        std::vector<RenderGraphCompiledPass> passes;
        std::vector<RenderGraphImageTransition> finalTransitions;
    };

    struct RenderGraphPassContext {
        std::string_view name;
        std::string_view type;
        std::span<const RenderGraphImageTransition> transitionsBefore;
        std::span<const RenderGraphImageHandle> colorWrites;
        std::span<const RenderGraphImageHandle> transferWrites;
    };

    using RenderGraphPassCallback = std::function<Result<void>(RenderGraphPassContext)>;

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
            std::vector<RenderGraphImageHandle> colorWrites;
            std::vector<RenderGraphImageHandle> transferWrites;
            RenderGraphPassCallback callback;
        };

    public:
        class PassBuilder {
        public:
            PassBuilder& writeColor(RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].colorWrites.push_back(image);
                return *this;
            }

            PassBuilder& writeTransfer(RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].transferWrites.push_back(image);
                return *this;
            }

            [[nodiscard]] std::string_view name() const {
                return graph_->passes_[passIndex_].name;
            }

            [[nodiscard]] std::string_view type() const {
                return graph_->passes_[passIndex_].type;
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
                .colorWrites = {},
                .transferWrites = {},
                .callback = {},
            };
            passes_.push_back(std::move(pass));
            return PassBuilder{*this, passes_.size() - 1};
        }

        PassBuilder addPass(std::string name, std::string type) {
            Pass pass{
                .name = std::move(name),
                .type = std::move(type),
                .colorWrites = {},
                .transferWrites = {},
                .callback = {},
            };
            passes_.push_back(std::move(pass));
            return PassBuilder{*this, passes_.size() - 1};
        }

        [[nodiscard]] Result<RenderGraphCompileResult> compile() const {
            std::vector<RenderGraphImageState> currentStates;
            currentStates.reserve(images_.size());
            for (const RenderGraphImageDesc& image : images_) {
                currentStates.push_back(image.initialState);
            }

            RenderGraphCompileResult result;
            result.passes.reserve(passes_.size());

            for (const Pass& pass : passes_) {
                RenderGraphCompiledPass compiledPass{
                    .name = pass.name,
                    .type = pass.type,
                    .transitionsBefore = {},
                    .colorWrites = pass.colorWrites,
                    .transferWrites = pass.transferWrites,
                };

                auto colorTransitions =
                    transitionImages(pass.colorWrites, RenderGraphImageState::ColorAttachment,
                                     currentStates, compiledPass);
                if (!colorTransitions) {
                    return std::unexpected{std::move(colorTransitions.error())};
                }

                auto transferTransitions =
                    transitionImages(pass.transferWrites, RenderGraphImageState::TransferDst,
                                     currentStates, compiledPass);
                if (!transferTransitions) {
                    return std::unexpected{std::move(transferTransitions.error())};
                }

                result.passes.push_back(std::move(compiledPass));
            }

            for (std::size_t index = 0; index < images_.size(); ++index) {
                const RenderGraphImageDesc& image = images_[index];
                if (currentStates[index] == image.finalState) {
                    continue;
                }

                const RenderGraphImageHandle imageHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                result.finalTransitions.push_back(
                    makeTransition(imageHandle, image, currentStates[index], image.finalState));
            }

            return result;
        }

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
                    .transitionsBefore = pass.transitionsBefore,
                    .colorWrites = pass.colorWrites,
                    .transferWrites = pass.transferWrites,
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
            output += "| # | Name | Type | Before Transitions | Color Writes | Transfer Writes |\n";
            output += "|---:|---|---|---:|---|---|\n";
            for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
                const RenderGraphCompiledPass& pass = compiled.passes[index];
                output += "| ";
                output += std::to_string(index);
                output += " | ";
                output += pass.name;
                output += " | ";
                output += pass.type.empty() ? "-" : pass.type;
                output += " | ";
                output += std::to_string(pass.transitionsBefore.size());
                output += " | ";
                output += imageHandleList(pass.colorWrites);
                output += " | ";
                output += imageHandleList(pass.transferWrites);
                output += " |\n";
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

        [[nodiscard]] Result<void>
        transitionImages(std::span<const RenderGraphImageHandle> imageHandles,
                         RenderGraphImageState requiredState,
                         std::vector<RenderGraphImageState>& currentStates,
                         RenderGraphCompiledPass& compiledPass) const {
            for (RenderGraphImageHandle imageHandle : imageHandles) {
                auto validated = validateImageHandle(imageHandle);
                if (!validated) {
                    return std::unexpected{std::move(validated.error())};
                }

                const RenderGraphImageDesc& image = images_[imageHandle.index];
                if (currentStates[imageHandle.index] != requiredState) {
                    compiledPass.transitionsBefore.push_back(makeTransition(
                        imageHandle, image, currentStates[imageHandle.index], requiredState));
                    currentStates[imageHandle.index] = requiredState;
                }
            }

            return {};
        }

        [[nodiscard]] static RenderGraphImageTransition
        makeTransition(RenderGraphImageHandle imageHandle, const RenderGraphImageDesc& image,
                       RenderGraphImageState oldState, RenderGraphImageState newState) {
            return RenderGraphImageTransition{
                .image = imageHandle,
                .imageName = image.name,
                .oldState = oldState,
                .newState = newState,
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
            output += imageStateName(transition.oldState);
            output += " | ";
            output += imageStateName(transition.newState);
            output += " |\n";
        }

        std::vector<RenderGraphImageDesc> images_;
        std::vector<Pass> passes_;
    };

} // namespace vke
