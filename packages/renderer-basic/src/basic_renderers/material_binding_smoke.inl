namespace {

    namespace material = asharia::material;

    [[nodiscard]] Error materialBindingSmokeError(std::string message) {
        return Error{ErrorDomain::Material, 0, std::move(message)};
    }

    [[nodiscard]] material::MaterialResourceBinding materialBindingSmokeParamsBinding() {
        return material::MaterialResourceBinding{
            .set = 0,
            .binding = 0,
            .name = "materialParams",
            .kind = material::MaterialResourceKind::UniformBuffer,
            .visibility = material::MaterialShaderVisibility::AllGraphics,
            .arrayCount = 1,
        };
    }

    [[nodiscard]] material::MaterialResourceBinding materialBindingSmokeTextureBinding() {
        return material::MaterialResourceBinding{
            .set = 0,
            .binding = 1,
            .name = "baseColorTexture",
            .kind = material::MaterialResourceKind::SampledImage,
            .visibility = material::MaterialShaderVisibility::Fragment,
            .arrayCount = 1,
        };
    }

    [[nodiscard]] material::MaterialResourceBinding materialBindingSmokeSamplerBinding() {
        return material::MaterialResourceBinding{
            .set = 0,
            .binding = 2,
            .name = "baseColorSampler",
            .kind = material::MaterialResourceKind::Sampler,
            .visibility = material::MaterialShaderVisibility::Fragment,
            .arrayCount = 1,
        };
    }

    [[nodiscard]] material::MaterialResourceSignature materialBindingSmokeSignature() {
        return material::MaterialResourceSignature{
            .bindings =
                {
                    materialBindingSmokeParamsBinding(),
                    materialBindingSmokeTextureBinding(),
                    materialBindingSmokeSamplerBinding(),
                },
        };
    }

    [[nodiscard]] material::MaterialPipelineKey
    materialBindingSmokePipelineKey(std::uint64_t signatureHash) {
        return material::MaterialPipelineKey{
            .shader =
                material::MaterialShaderIdentity{
                    .shaderProgram = "builtin.forward.material-binding-smoke",
                    .shaderHash = 0x1010U,
                    .reflectionHash = 0x2020U,
                },
            .resourceSignatureHash = signatureHash,
            .renderState =
                material::MaterialRenderStateKey{
                    .topology = material::MaterialPrimitiveTopology::TriangleList,
                    .cullMode = material::MaterialCullMode::Back,
                    .frontFace = material::MaterialFrontFace::CounterClockwise,
                    .depthTestEnabled = true,
                    .depthWriteEnabled = true,
                    .depthCompare = material::MaterialCompareOp::LessOrEqual,
                    .colorBlend = material::MaterialBlendMode::Disabled,
                    .vertexLayoutHash = 0x3030U,
                    .colorFormatHash = 0x4040U,
                    .depthFormatHash = 0x5050U,
                },
        };
    }

    [[nodiscard]] Result<VkDescriptorType>
    materialDescriptorType(material::MaterialResourceKind kind, std::string_view name) {
        switch (kind) {
        case material::MaterialResourceKind::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case material::MaterialResourceKind::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case material::MaterialResourceKind::SampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case material::MaterialResourceKind::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case material::MaterialResourceKind::CombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }

        return std::unexpected{
            materialBindingSmokeError("Material binding smoke resource \"" + std::string{name} +
                                      "\" has an unsupported resource kind.")};
    }

    [[nodiscard]] Result<VkShaderStageFlags>
    materialStageFlags(material::MaterialShaderVisibility visibility, std::string_view name) {
        VkShaderStageFlags flags{};
        if ((visibility & material::MaterialShaderVisibility::Vertex) !=
            material::MaterialShaderVisibility::None) {
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if ((visibility & material::MaterialShaderVisibility::Fragment) !=
            material::MaterialShaderVisibility::None) {
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if ((visibility & material::MaterialShaderVisibility::Compute) !=
            material::MaterialShaderVisibility::None) {
            flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }

        if (flags == 0) {
            return std::unexpected{
                materialBindingSmokeError("Material binding smoke resource \"" +
                                          std::string{name} +
                                          "\" has no Vulkan shader stage flags.")};
        }

        return flags;
    }

    [[nodiscard]] Result<PipelineLayoutResources> createMaterialPipelineLayoutResources(
        VkDevice device,
        const material::MaterialResourceSignature& signature) {
        if (auto validSignature = material::validateMaterialResourceSignature(signature);
            !validSignature) {
            return std::unexpected{std::move(validSignature.error())};
        }

        std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsBySet;
        for (const material::MaterialResourceBinding& binding : signature.bindings) {
            if (binding.set >= bindingsBySet.size()) {
                bindingsBySet.resize(static_cast<std::size_t>(binding.set) + 1);
            }

            auto descriptor = materialDescriptorType(binding.kind, binding.name);
            if (!descriptor) {
                return std::unexpected{std::move(descriptor.error())};
            }
            auto stages = materialStageFlags(binding.visibility, binding.name);
            if (!stages) {
                return std::unexpected{std::move(stages.error())};
            }

            bindingsBySet[binding.set].push_back(VkDescriptorSetLayoutBinding{
                .binding = binding.binding,
                .descriptorType = *descriptor,
                .descriptorCount = binding.arrayCount,
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

        auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
            .device = device,
            .setLayouts = setLayoutHandles,
            .pushConstantRanges = {},
        });
        if (!pipelineLayout) {
            return std::unexpected{std::move(pipelineLayout.error())};
        }

        resources.pipelineLayout = std::move(*pipelineLayout);
        return resources;
    }

    [[nodiscard]] Result<std::uint64_t> validateMaterialBindingPipelineKey(
        const material::MaterialResourceSignature& signature,
        const material::MaterialPipelineKey& key) {
        auto signatureHash = material::makeMaterialResourceSignatureHash(signature);
        if (!signatureHash) {
            return std::unexpected{std::move(signatureHash.error())};
        }
        if (auto validKey = material::validateMaterialPipelineKey(key); !validKey) {
            return std::unexpected{std::move(validKey.error())};
        }
        if (key.resourceSignatureHash != *signatureHash) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke rejected stale resource signature hash for shader \"" +
                key.shader.shaderProgram + "\": expected " + std::to_string(*signatureHash) +
                " but pipeline key used " + std::to_string(key.resourceSignatureHash) + ".")};
        }

        return material::makeMaterialPipelineKeyHash(key);
    }

    [[nodiscard]] VoidResult expectMaterialBindingRejection(VoidResult rejected,
                                                           std::string_view expectedToken,
                                                           std::string_view context) {
        if (rejected) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke accepted invalid " + std::string{context} + ".")};
        }
        if (rejected.error().message.find(expectedToken) == std::string::npos) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke produced incomplete diagnostic for " +
                std::string{context} + ": " + rejected.error().message)};
        }
        return {};
    }

    [[nodiscard]] VoidResult validateMaterialBindingNegativeDiagnostics(
        VkDevice device,
        const material::MaterialResourceSignature& signature,
        const material::MaterialPipelineKey& key) {
        auto kindMismatch = signature;
        kindMismatch.bindings[1].kind = material::MaterialResourceKind::CombinedImageSampler;
        if (auto rejected = expectMaterialBindingRejection(
                material::validateMaterialSignatureCompatibility(kindMismatch, signature),
                "expected sampled-image", "kind mismatch");
            !rejected) {
            return std::unexpected{std::move(rejected.error())};
        }

        auto staleKey = key;
        staleKey.resourceSignatureHash += 1;
        auto staleKeyHash = validateMaterialBindingPipelineKey(signature, staleKey);
        if (staleKeyHash) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke accepted a stale pipeline key signature hash.")};
        }
        if (staleKeyHash.error().message.find("stale resource signature hash") ==
            std::string::npos) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke produced incomplete stale pipeline key diagnostic: " +
                staleKeyHash.error().message)};
        }

        auto invalidVisibility = signature;
        invalidVisibility.bindings[0].visibility = material::MaterialShaderVisibility::None;
        auto invalidLayout = createMaterialPipelineLayoutResources(device, invalidVisibility);
        if (invalidLayout) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke accepted a signature with no shader visibility.")};
        }
        if (invalidLayout.error().message.find("invalid shader visibility") ==
            std::string::npos) {
            return std::unexpected{materialBindingSmokeError(
                "Material binding smoke produced incomplete visibility diagnostic: " +
                invalidLayout.error().message)};
        }

        return {};
    }

} // namespace

Result<void> validateBasicMaterialBindingSmoke(const BasicMaterialBindingSmokeDesc& desc) {
    if (desc.device == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Cannot validate material binding smoke without a device"}};
    }
    if (desc.allocator == nullptr) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Cannot validate material binding smoke without an allocator"}};
    }

    auto shaderReflection = validateDescriptorLayoutReflection(desc.shaderDirectory);
    if (!shaderReflection) {
        return std::unexpected{std::move(shaderReflection.error())};
    }

    const material::MaterialResourceSignature signature = materialBindingSmokeSignature();
    auto signatureHash = material::makeMaterialResourceSignatureHash(signature);
    if (!signatureHash) {
        return std::unexpected{std::move(signatureHash.error())};
    }
    const material::MaterialPipelineKey pipelineKey =
        materialBindingSmokePipelineKey(*signatureHash);
    auto pipelineKeyHash = validateMaterialBindingPipelineKey(signature, pipelineKey);
    if (!pipelineKeyHash || *pipelineKeyHash == 0) {
        if (!pipelineKeyHash) {
            return std::unexpected{std::move(pipelineKeyHash.error())};
        }
        return std::unexpected{
            materialBindingSmokeError("Material binding smoke produced a zero pipeline key hash.")};
    }

    if (auto compatible = material::validateMaterialSignatureCompatibility(signature, signature);
        !compatible) {
        return std::unexpected{std::move(compatible.error())};
    }
    if (auto rejected =
            validateMaterialBindingNegativeDiagnostics(desc.device, signature, pipelineKey);
        !rejected) {
        return std::unexpected{std::move(rejected.error())};
    }

    auto resources = createMaterialPipelineLayoutResources(desc.device, signature);
    if (!resources) {
        return std::unexpected{std::move(resources.error())};
    }
    if (resources->descriptorSetLayouts.empty() ||
        resources->descriptorSetLayouts.front().handle() == VK_NULL_HANDLE ||
        resources->pipelineLayout.handle() == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Material binding smoke produced incomplete Vulkan layout resources"}};
    }

    constexpr std::array<std::uint32_t, 4> uniformData{10, 20, 30, 40};
    auto uniformBuffer = VulkanBuffer::create(VulkanBufferDesc{
        .device = desc.device,
        .allocator = desc.allocator,
        .size = sizeof(uniformData),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
    });
    if (!uniformBuffer) {
        return std::unexpected{std::move(uniformBuffer.error())};
    }
    auto uploaded = uniformBuffer->upload(std::as_bytes(std::span{uniformData}));
    if (!uploaded) {
        return std::unexpected{std::move(uploaded.error())};
    }

    auto sampledImage = VulkanImage::create(VulkanImageDesc{
        .device = desc.device,
        .allocator = desc.allocator,
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .extent = VkExtent2D{.width = 1, .height = 1},
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    });
    if (!sampledImage) {
        return std::unexpected{std::move(sampledImage.error())};
    }
    auto sampledImageView = VulkanImageView::create(VulkanImageViewDesc{
        .device = desc.device,
        .image = sampledImage->handle(),
        .format = sampledImage->format(),
        .aspectMask = sampledImage->aspectMask(),
    });
    if (!sampledImageView) {
        return std::unexpected{std::move(sampledImageView.error())};
    }
    auto sampler = VulkanSampler::create(VulkanSamplerDesc{
        .device = desc.device,
    });
    if (!sampler) {
        return std::unexpected{std::move(sampler.error())};
    }

    constexpr std::array poolSizes{
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .count = 1,
        },
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count = 1,
        },
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .count = 1,
        },
    };
    auto descriptorAllocator = VulkanDescriptorAllocator::create(VulkanDescriptorPoolDesc{
        .device = desc.device,
        .maxSets = 1,
        .poolSizes = poolSizes,
    });
    if (!descriptorAllocator) {
        return std::unexpected{std::move(descriptorAllocator.error())};
    }

    const std::array setLayouts{resources->descriptorSetLayouts.front().handle()};
    auto descriptorSets = descriptorAllocator->allocate(VulkanDescriptorSetAllocationDesc{
        .setLayouts = setLayouts,
    });
    if (!descriptorSets) {
        return std::unexpected{std::move(descriptorSets.error())};
    }
    if (descriptorSets->size() != 1 || descriptorSets->front() == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Material binding smoke failed to allocate one descriptor set"}};
    }

    const std::array descriptorWrites{
        VulkanDescriptorBufferWrite{
            .descriptorSet = descriptorSets->front(),
            .binding = 0,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = uniformBuffer->handle(),
            .offset = 0,
            .range = uniformBuffer->size(),
        },
    };
    updateVulkanDescriptorBuffers(desc.device, descriptorWrites);

    const std::array imageDescriptorWrites{
        VulkanDescriptorImageWrite{
            .descriptorSet = descriptorSets->front(),
            .binding = 1,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .imageView = sampledImageView->handle(),
            .sampler = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        VulkanDescriptorImageWrite{
            .descriptorSet = descriptorSets->front(),
            .binding = 2,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .imageView = VK_NULL_HANDLE,
            .sampler = sampler->handle(),
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        },
    };
    updateVulkanDescriptorImages(desc.device, imageDescriptorWrites);

    const VulkanDescriptorAllocatorStats stats = descriptorAllocator->stats();
    if (stats.poolsCreated != 1 || stats.allocationCalls != 1 || stats.setsAllocated != 1) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Material binding smoke did not route allocation through descriptor "
                  "allocator counters"}};
    }
    const VulkanBufferStats bufferStats = uniformBuffer->stats();
    if (bufferStats.created != 1 || bufferStats.hostUploadCreated != 1 ||
        bufferStats.uploadCalls != 1 || bufferStats.uploadedBytes != sizeof(uniformData)) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Material binding smoke did not record expected buffer upload counters"}};
    }

    return {};
}
