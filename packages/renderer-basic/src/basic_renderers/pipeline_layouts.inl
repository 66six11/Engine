        struct ShaderStageQuery {
            std::string_view stageVisibility;
            std::string_view context;
        };

        [[nodiscard]] Result<VkShaderStageFlags> shaderStageFlags(ShaderStageQuery query) {
            VkShaderStageFlags flags{};
            std::size_t begin = 0;
            while (begin <= query.stageVisibility.size()) {
                const std::size_t end = query.stageVisibility.find('|', begin);
                const std::size_t tokenEnd =
                    end == std::string_view::npos ? query.stageVisibility.size() : end;
                const std::string_view token =
                    query.stageVisibility.substr(begin, tokenEnd - begin);

                if (token == "vertex") {
                    flags |= VK_SHADER_STAGE_VERTEX_BIT;
                } else if (token == "fragment") {
                    flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
                } else if (token == "compute") {
                    flags |= VK_SHADER_STAGE_COMPUTE_BIT;
                } else if (!token.empty()) {
                    return std::unexpected{shaderError("Unsupported shader stage visibility for " +
                                                       std::string{query.context} + ": " +
                                                       std::string{token})};
                }

                if (end == std::string_view::npos) {
                    break;
                }
                begin = end + 1;
            }

            if (flags == 0) {
                return std::unexpected{shaderError("Missing shader stage visibility for " +
                                                   std::string{query.context})};
            }

            return flags;
        }

        [[nodiscard]] Result<VkDescriptorType>
        descriptorType(const ShaderDescriptorBindingReflection& binding) {
            if (binding.kind == "constantBuffer") {
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            }
            if (binding.kind == "texture") {
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            }
            if (binding.kind == "sampler") {
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            }
            if (binding.kind == "combinedTextureSampler") {
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            if (binding.kind == "mutableTexture") {
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            if (binding.kind == "typedBuffer" || binding.kind == "mutableTypedBuffer" ||
                binding.kind == "rawBuffer" || binding.kind == "mutableRawBuffer") {
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }

            return std::unexpected{shaderError("Unsupported descriptor binding kind for " +
                                               binding.name + ": " + binding.kind)};
        }

        struct PipelineLayoutResources {
            std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts;
            VulkanPipelineLayout pipelineLayout;
        };

        [[nodiscard]] Result<PipelineLayoutResources>
        createPipelineLayoutResources(VkDevice device, const ShaderResourceSignature& signature) {
            std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsBySet;
            for (const ShaderDescriptorBindingReflection& binding : signature.descriptorBindings) {
                if (binding.count == 0) {
                    return std::unexpected{
                        shaderError("Descriptor binding count must be non-zero: " + binding.name)};
                }

                if (binding.set >= bindingsBySet.size()) {
                    bindingsBySet.resize(static_cast<std::size_t>(binding.set) + 1);
                }

                auto type = descriptorType(binding);
                if (!type) {
                    return std::unexpected{std::move(type.error())};
                }
                auto stages = shaderStageFlags(ShaderStageQuery{
                    .stageVisibility = binding.stageVisibility,
                    .context = "descriptor " + binding.name,
                });
                if (!stages) {
                    return std::unexpected{std::move(stages.error())};
                }

                bindingsBySet[binding.set].push_back(VkDescriptorSetLayoutBinding{
                    .binding = binding.binding,
                    .descriptorType = *type,
                    .descriptorCount = binding.count,
                    .stageFlags = *stages,
                    .pImmutableSamplers = nullptr,
                });
            }

            for (std::vector<VkDescriptorSetLayoutBinding>& setBindings : bindingsBySet) {
                std::ranges::sort(setBindings, {}, &VkDescriptorSetLayoutBinding::binding);
            }

            PipelineLayoutResources resources;
            resources.descriptorSetLayouts.reserve(bindingsBySet.size());
            std::vector<VkDescriptorSetLayout> setLayoutHandles;
            setLayoutHandles.reserve(bindingsBySet.size());
            for (const std::vector<VkDescriptorSetLayoutBinding>& setBindings : bindingsBySet) {
                auto setLayout = VulkanDescriptorSetLayout::create(VulkanDescriptorSetLayoutDesc{
                    .device = device,
                    .bindings = setBindings,
                });
                if (!setLayout) {
                    return std::unexpected{std::move(setLayout.error())};
                }

                setLayoutHandles.push_back(setLayout->handle());
                resources.descriptorSetLayouts.push_back(std::move(*setLayout));
            }

            std::vector<VkPushConstantRange> pushConstantRanges;
            pushConstantRanges.reserve(signature.pushConstants.size());
            for (const ShaderPushConstantReflection& pushConstant : signature.pushConstants) {
                if (pushConstant.size == 0) {
                    return std::unexpected{shaderError(
                        "Push constant range size must be non-zero: " + pushConstant.name)};
                }
                auto stages = shaderStageFlags(ShaderStageQuery{
                    .stageVisibility = pushConstant.stageVisibility,
                    .context = "push constant " + pushConstant.name,
                });
                if (!stages) {
                    return std::unexpected{std::move(stages.error())};
                }

                pushConstantRanges.push_back(VkPushConstantRange{
                    .stageFlags = *stages,
                    .offset = pushConstant.offset,
                    .size = pushConstant.size,
                });
            }

            auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
                .device = device,
                .setLayouts = setLayoutHandles,
                .pushConstantRanges = pushConstantRanges,
            });
            if (!pipelineLayout) {
                return std::unexpected{std::move(pipelineLayout.error())};
            }

            resources.pipelineLayout = std::move(*pipelineLayout);
            return resources;
        }
