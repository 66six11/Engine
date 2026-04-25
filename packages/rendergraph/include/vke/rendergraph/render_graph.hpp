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
        std::span<const RenderGraphImageTransition> transitionsBefore;
        std::span<const RenderGraphImageHandle> colorWrites;
        std::span<const RenderGraphImageHandle> transferWrites;
    };

    using RenderGraphPassCallback = std::function<Result<void>(RenderGraphPassContext)>;

    class RenderGraph {
    private:
        struct Pass {
            std::string name;
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

            PassBuilder& execute(RenderGraphPassCallback callback) {
                graph_->passes_[passIndex_].callback = std::move(callback);
                return *this;
            }

        private:
            friend class RenderGraph;

            PassBuilder(RenderGraph& graph, std::size_t passIndex)
                : graph_(&graph)
                , passIndex_(passIndex) {}

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
                    .transitionsBefore = {},
                    .colorWrites = pass.colorWrites,
                    .transferWrites = pass.transferWrites,
                };

                auto colorTransitions = transitionImages(pass.colorWrites,
                                                         RenderGraphImageState::ColorAttachment,
                                                         currentStates, compiledPass);
                if (!colorTransitions) {
                    return std::unexpected{std::move(colorTransitions.error())};
                }

                auto transferTransitions = transitionImages(pass.transferWrites,
                                                            RenderGraphImageState::TransferDst,
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
                result.finalTransitions.push_back(makeTransition(
                    imageHandle, image, currentStates[index], image.finalState));
            }

            return result;
        }

        [[nodiscard]] Result<void> execute() const {
            auto compiled = compile();
            if (!compiled) {
                return std::unexpected{std::move(compiled.error())};
            }

            for (std::size_t index = 0; index < compiled->passes.size(); ++index) {
                const RenderGraphPassCallback& callback = passes_[index].callback;
                if (!callback) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass is missing an execute callback.",
                    }};
                }

                const RenderGraphCompiledPass& pass = compiled->passes[index];
                auto executed = callback(RenderGraphPassContext{
                    .name = pass.name,
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

        [[nodiscard]] Result<void> transitionImages(
            std::span<const RenderGraphImageHandle> imageHandles,
            RenderGraphImageState requiredState, std::vector<RenderGraphImageState>& currentStates,
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

        [[nodiscard]] static RenderGraphImageTransition makeTransition(
            RenderGraphImageHandle imageHandle, const RenderGraphImageDesc& image,
            RenderGraphImageState oldState, RenderGraphImageState newState) {
            return RenderGraphImageTransition{
                .image = imageHandle,
                .imageName = image.name,
                .oldState = oldState,
                .newState = newState,
            };
        }

        std::vector<RenderGraphImageDesc> images_;
        std::vector<Pass> passes_;
    };

} // namespace vke
