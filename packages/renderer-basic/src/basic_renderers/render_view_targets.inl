        [[nodiscard]] BasicRenderViewTarget
        basicSwapchainRenderViewTarget(const VulkanFrameRecordContext& frame) {
            return BasicRenderViewTarget{
                .image = frame.image,
                .imageView = frame.imageView,
                .format = frame.format,
                .extent = frame.extent,
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .finalUsage = BasicRenderViewTargetFinalUsage::Present,
            };
        }

        [[nodiscard]] BasicRenderViewTarget
        basicSampledRenderViewTarget(VulkanSampledTextureView target) {
            return BasicRenderViewTarget{
                .image = target.image,
                .imageView = target.imageView,
                .format = target.format,
                .extent = target.extent,
                .aspectMask = target.aspectMask,
                .finalUsage = BasicRenderViewTargetFinalUsage::SampledTexture,
            };
        }

        [[nodiscard]] Result<void>
        validateBasicRenderViewTarget(const BasicRenderViewTarget& target,
                                      std::string_view context) {
            if (target.image == VK_NULL_HANDLE || target.imageView == VK_NULL_HANDLE ||
                target.format == VK_FORMAT_UNDEFINED || target.extent.width == 0 ||
                target.extent.height == 0 || target.aspectMask == 0) {
                return std::unexpected{
                    Error{ErrorDomain::Vulkan, 0,
                          std::string{context} + " requires a complete render view target"}};
            }

            return {};
        }

        [[nodiscard]] RenderGraphImageState
        basicRenderViewFinalState(BasicRenderViewTargetFinalUsage usage) {
            switch (usage) {
            case BasicRenderViewTargetFinalUsage::SampledTexture:
                return RenderGraphImageState::ShaderRead;
            case BasicRenderViewTargetFinalUsage::Present:
            default:
                return RenderGraphImageState::Present;
            }
        }

        [[nodiscard]] RenderGraphShaderStage
        basicRenderViewFinalShaderStage(BasicRenderViewTargetFinalUsage usage) {
            switch (usage) {
            case BasicRenderViewTargetFinalUsage::SampledTexture:
                return RenderGraphShaderStage::Fragment;
            case BasicRenderViewTargetFinalUsage::Present:
            default:
                return RenderGraphShaderStage::None;
            }
        }

        [[nodiscard]] RenderGraphImageDesc basicRenderViewTargetDesc(
            const BasicRenderViewTarget& target, RenderGraphImageState initialState,
            std::string_view name,
            RenderGraphShaderStage initialShaderStage = RenderGraphShaderStage::None) {
            return RenderGraphImageDesc{
                .name = std::string{name},
                .format = basicRenderGraphImageFormat(target.format),
                .extent = basicRenderGraphExtent(target.extent),
                .initialState = initialState,
                .initialShaderStage = initialShaderStage,
                .finalState = basicRenderViewFinalState(target.finalUsage),
                .finalShaderStage = basicRenderViewFinalShaderStage(target.finalUsage),
            };
        }

        [[nodiscard]] VulkanRenderGraphImageBinding
        basicRenderViewTargetBinding(RenderGraphImageHandle image,
                                     const BasicRenderViewTarget& target) {
            return VulkanRenderGraphImageBinding{
                .image = image,
                .vulkanImage = target.image,
                .vulkanImageView = target.imageView,
                .aspectMask = target.aspectMask,
                .debugName = {},
            };
        }
