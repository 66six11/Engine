    Result<void> validateBasicDescriptorLayoutSmoke(const BasicDescriptorLayoutSmokeDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot validate descriptor layout smoke without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Cannot validate descriptor layout smoke without an allocator"}};
        }

        auto signature = validateDescriptorLayoutReflection(desc.shaderDirectory);
        if (!signature) {
            return std::unexpected{std::move(signature.error())};
        }

        auto resources = createPipelineLayoutResources(desc.device, *signature);
        if (!resources) {
            return std::unexpected{std::move(resources.error())};
        }

        constexpr std::array<std::uint32_t, 4> uniformData{1, 2, 3, 4};
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

        if (resources->descriptorSetLayouts.empty()) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Descriptor layout smoke produced no set layouts"}};
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
                      "Descriptor layout smoke failed to allocate one descriptor set"}};
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
                      "Descriptor layout smoke did not route allocation through the descriptor "
                      "allocator counters"}};
        }
        const VulkanBufferStats bufferStats = uniformBuffer->stats();
        if (bufferStats.created != 1 || bufferStats.hostUploadCreated != 1 ||
            bufferStats.uploadCalls != 1 || bufferStats.uploadedBytes != sizeof(uniformData)) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Descriptor layout smoke did not record expected buffer upload counters"}};
        }

        return {};
    }
