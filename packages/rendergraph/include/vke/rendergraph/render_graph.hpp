#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstddef>
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

    struct RenderGraphImageDesc {
        std::string name;
        VkImage image{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageLayout initialLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout finalLayout{VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    };

    struct RenderGraphImageTransition {
        RenderGraphImageHandle image{};
        std::string imageName;
        VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkPipelineStageFlags2 srcStageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 srcAccessMask{};
        VkPipelineStageFlags2 dstStageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 dstAccessMask{};
    };

    struct RenderGraphCompiledPass {
        std::string name;
        std::vector<RenderGraphImageTransition> transitionsBefore;
        std::vector<RenderGraphImageHandle> colorWrites;
    };

    struct RenderGraphCompileResult {
        std::vector<RenderGraphCompiledPass> passes;
        std::vector<RenderGraphImageTransition> finalTransitions;
    };

    class RenderGraph {
    private:
        struct Pass {
            std::string name;
            std::vector<RenderGraphImageHandle> colorWrites;
        };

        struct RenderGraphImageUsage {
            VkPipelineStageFlags2 stageMask{VK_PIPELINE_STAGE_2_NONE};
            VkAccessFlags2 accessMask{};
        };

    public:
        class PassBuilder {
        public:
            PassBuilder& writeColor(RenderGraphImageHandle image) {
                graph_->passes_[passIndex_].colorWrites.push_back(image);
                return *this;
            }

            [[nodiscard]] std::string_view name() const {
                return graph_->passes_[passIndex_].name;
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
            };
            passes_.push_back(std::move(pass));
            return PassBuilder{*this, passes_.size() - 1};
        }

        [[nodiscard]] Result<RenderGraphCompileResult> compile() const {
            std::vector<VkImageLayout> currentLayouts;
            currentLayouts.reserve(images_.size());
            for (const RenderGraphImageDesc& image : images_) {
                currentLayouts.push_back(image.initialLayout);
            }

            RenderGraphCompileResult result;
            result.passes.reserve(passes_.size());

            for (const Pass& pass : passes_) {
                RenderGraphCompiledPass compiledPass{
                    .name = pass.name,
                    .colorWrites = pass.colorWrites,
                };

                for (RenderGraphImageHandle imageHandle : pass.colorWrites) {
                    auto validated = validateImageHandle(imageHandle);
                    if (!validated) {
                        return std::unexpected{std::move(validated.error())};
                    }

                    const RenderGraphImageDesc& image = images_[imageHandle.index];
                    const VkImageLayout requiredLayout =
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    if (currentLayouts[imageHandle.index] != requiredLayout) {
                        compiledPass.transitionsBefore.push_back(makeTransition(
                            imageHandle, image, currentLayouts[imageHandle.index], requiredLayout,
                            usageForLayout(currentLayouts[imageHandle.index]),
                            RenderGraphImageUsage{
                                .stageMask =
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                .accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            }));
                        currentLayouts[imageHandle.index] = requiredLayout;
                    }
                }

                result.passes.push_back(std::move(compiledPass));
            }

            for (std::size_t index = 0; index < images_.size(); ++index) {
                const RenderGraphImageDesc& image = images_[index];
                if (currentLayouts[index] == image.finalLayout) {
                    continue;
                }

                const RenderGraphImageHandle imageHandle{
                    .index = static_cast<std::uint32_t>(index),
                };
                result.finalTransitions.push_back(makeTransition(
                    imageHandle, image, currentLayouts[index], image.finalLayout,
                    usageForLayout(currentLayouts[index]), usageForLayout(image.finalLayout)));
            }

            return result;
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

        [[nodiscard]] static RenderGraphImageUsage usageForLayout(VkImageLayout layout) {
            switch (layout) {
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return RenderGraphImageUsage{
                    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                };
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return RenderGraphImageUsage{
                    .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                };
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            case VK_IMAGE_LAYOUT_UNDEFINED:
            default:
                return {};
            }
        }

        [[nodiscard]] static RenderGraphImageTransition makeTransition(
            RenderGraphImageHandle imageHandle, const RenderGraphImageDesc& image,
            VkImageLayout oldLayout, VkImageLayout newLayout, RenderGraphImageUsage srcUsage,
            RenderGraphImageUsage dstUsage) {
            return RenderGraphImageTransition{
                .image = imageHandle,
                .imageName = image.name,
                .oldLayout = oldLayout,
                .newLayout = newLayout,
                .srcStageMask = srcUsage.stageMask,
                .srcAccessMask = srcUsage.accessMask,
                .dstStageMask = dstUsage.stageMask,
                .dstAccessMask = dstUsage.accessMask,
            };
        }

        std::vector<RenderGraphImageDesc> images_;
        std::vector<Pass> passes_;
    };

} // namespace vke
