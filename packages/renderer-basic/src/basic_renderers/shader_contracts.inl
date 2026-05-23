        [[nodiscard]] Error shaderError(std::string message) {
            return Error{ErrorDomain::Shader, 0, std::move(message)};
        }

        [[nodiscard]] Result<void> expectString(std::string_view actual, std::string_view expected,
                                                std::string_view context) {
            if (actual == expected) {
                return {};
            }

            return std::unexpected{shaderError(std::string{context} + " expected '" +
                                               std::string{expected} + "' but found '" +
                                               std::string{actual} + "'")};
        }

        [[nodiscard]] Result<void> expectUint(std::uint32_t actual, std::uint32_t expected,
                                              std::string_view context) {
            if (actual == expected) {
                return {};
            }

            return std::unexpected{shaderError(std::string{context} + " expected " +
                                               std::to_string(expected) + " but found " +
                                               std::to_string(actual))};
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>>
        readSpirvFile(const std::filesystem::path& path) {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file) {
                return std::unexpected{shaderError("Failed to open SPIR-V file: " + path.string())};
            }

            const std::streamsize size = file.tellg();
            if (size <= 0 || size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
                return std::unexpected{
                    shaderError("SPIR-V file size is invalid: " + path.string())};
            }

            std::vector<char> bytes(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file.read(bytes.data(), size)) {
                return std::unexpected{shaderError("Failed to read SPIR-V file: " + path.string())};
            }

            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }

        [[nodiscard]] const ShaderVertexInputReflection*
        findVertexInput(const ShaderReflection& reflection, std::string_view semantic,
                        std::uint32_t semanticIndex) {
            for (const ShaderVertexInputReflection& input : reflection.vertexInputs) {
                if (input.semantic == semantic && input.semanticIndex == semanticIndex) {
                    return &input;
                }
            }

            return nullptr;
        }

        [[nodiscard]] Result<void>
        validateVertexInput(const ShaderReflection& reflection, std::string_view semantic,
                            std::uint32_t semanticIndex, std::uint32_t expectedLocation,
                            std::string_view expectedScalarType, std::uint32_t expectedRowCount,
                            std::uint32_t expectedColumnCount) {
            const ShaderVertexInputReflection* input =
                findVertexInput(reflection, semantic, semanticIndex);
            if (input == nullptr) {
                return std::unexpected{
                    shaderError("Missing shader vertex input semantic: " + std::string{semantic} +
                                std::to_string(semanticIndex))};
            }

            auto location = expectUint(input->location, expectedLocation,
                                       "Shader vertex input " + std::string{semantic} +
                                           std::to_string(semanticIndex) + " location");
            if (!location) {
                return std::unexpected{std::move(location.error())};
            }
            auto scalarType = expectString(input->scalarType, expectedScalarType,
                                           "Shader vertex input " + std::string{semantic} +
                                               std::to_string(semanticIndex) + " scalarType");
            if (!scalarType) {
                return std::unexpected{std::move(scalarType.error())};
            }
            auto rowCount = expectUint(input->rowCount, expectedRowCount,
                                       "Shader vertex input " + std::string{semantic} +
                                           std::to_string(semanticIndex) + " rowCount");
            if (!rowCount) {
                return std::unexpected{std::move(rowCount.error())};
            }
            auto columnCount = expectUint(input->columnCount, expectedColumnCount,
                                          "Shader vertex input " + std::string{semantic} +
                                              std::to_string(semanticIndex) + " columnCount");
            if (!columnCount) {
                return std::unexpected{std::move(columnCount.error())};
            }

            return {};
        }

        [[nodiscard]] Result<void> validateNoResourceBindings(const ShaderReflection& reflection,
                                                              std::string_view shaderName) {
            auto descriptorCount =
                expectUint(reflection.descriptorBindingCount, 0,
                           std::string{shaderName} + " descriptor binding count");
            if (!descriptorCount) {
                return std::unexpected{std::move(descriptorCount.error())};
            }

            auto pushConstantCount = expectUint(reflection.pushConstantCount, 0,
                                                std::string{shaderName} + " push constant count");
            if (!pushConstantCount) {
                return std::unexpected{std::move(pushConstantCount.error())};
            }

            return {};
        }

        [[nodiscard]] const ShaderDescriptorBindingReflection*
        findDescriptorBinding(const ShaderResourceSignature& signature, std::uint32_t set,
                              std::uint32_t binding) {
            for (const ShaderDescriptorBindingReflection& descriptor :
                 signature.descriptorBindings) {
                if (descriptor.set == set && descriptor.binding == binding) {
                    return &descriptor;
                }
            }

            return nullptr;
        }

        [[nodiscard]] Result<void>
        validateDescriptorBinding(const ShaderResourceSignature& signature, std::uint32_t set,
                                  std::uint32_t binding, std::string_view expectedKind,
                                  std::string_view context,
                                  std::string_view expectedStageVisibility = "fragment") {
            const ShaderDescriptorBindingReflection* descriptor =
                findDescriptorBinding(signature, set, binding);
            if (descriptor == nullptr) {
                return std::unexpected{
                    shaderError(std::string{context} + " missing descriptor binding")};
            }

            auto descriptorSet = expectUint(descriptor->set, set, std::string{context} + " set");
            if (!descriptorSet) {
                return std::unexpected{std::move(descriptorSet.error())};
            }
            auto bindingIndex =
                expectUint(descriptor->binding, binding, std::string{context} + " binding");
            if (!bindingIndex) {
                return std::unexpected{std::move(bindingIndex.error())};
            }
            auto count = expectUint(descriptor->count, 1, std::string{context} + " count");
            if (!count) {
                return std::unexpected{std::move(count.error())};
            }
            auto kind =
                expectString(descriptor->kind, expectedKind, std::string{context} + " kind");
            if (!kind) {
                return std::unexpected{std::move(kind.error())};
            }
            auto stage = expectString(descriptor->stageVisibility, expectedStageVisibility,
                                      std::string{context} + " stage");
            if (!stage) {
                return std::unexpected{std::move(stage.error())};
            }

            return {};
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateBasicTriangleReflection(const std::filesystem::path& shaderDirectory) {
            auto vertexReflection =
                readShaderReflection(shaderDirectory / "basic_triangle.vert.reflection.json");
            if (!vertexReflection) {
                return std::unexpected{std::move(vertexReflection.error())};
            }
            auto fragmentReflection =
                readShaderReflection(shaderDirectory / "basic_triangle.frag.reflection.json");
            if (!fragmentReflection) {
                return std::unexpected{std::move(fragmentReflection.error())};
            }

            auto vertexEntry = expectString(vertexReflection->entry, "vertexMain",
                                            "Triangle vertex shader reflection entry");
            if (!vertexEntry) {
                return std::unexpected{std::move(vertexEntry.error())};
            }
            auto vertexStage = expectString(vertexReflection->stage, "vertex",
                                            "Triangle vertex shader reflection stage");
            if (!vertexStage) {
                return std::unexpected{std::move(vertexStage.error())};
            }
            auto fragmentEntry = expectString(fragmentReflection->entry, "fragmentMain",
                                              "Triangle fragment shader reflection entry");
            if (!fragmentEntry) {
                return std::unexpected{std::move(fragmentEntry.error())};
            }
            auto fragmentStage = expectString(fragmentReflection->stage, "fragment",
                                              "Triangle fragment shader reflection stage");
            if (!fragmentStage) {
                return std::unexpected{std::move(fragmentStage.error())};
            }

            auto vertexInputCount =
                expectUint(static_cast<std::uint32_t>(vertexReflection->vertexInputs.size()), 2,
                           "Triangle vertex shader input count");
            if (!vertexInputCount) {
                return std::unexpected{std::move(vertexInputCount.error())};
            }
            auto position =
                validateVertexInput(*vertexReflection, "POSITION", 0, 0, "float32", 1, 2);
            if (!position) {
                return std::unexpected{std::move(position.error())};
            }
            auto color = validateVertexInput(*vertexReflection, "COLOR", 0, 1, "float32", 1, 3);
            if (!color) {
                return std::unexpected{std::move(color.error())};
            }

            auto vertexResources =
                validateNoResourceBindings(*vertexReflection, "Triangle vertex shader");
            if (!vertexResources) {
                return std::unexpected{std::move(vertexResources.error())};
            }
            auto fragmentResources =
                validateNoResourceBindings(*fragmentReflection, "Triangle fragment shader");
            if (!fragmentResources) {
                return std::unexpected{std::move(fragmentResources.error())};
            }

            const std::array shaderReflections{*vertexReflection, *fragmentReflection};
            ShaderResourceSignature signature = shaderResourceSignature(shaderReflections);
            auto descriptorSignature =
                expectUint(signature.descriptorBindingCount, 0,
                           "Triangle pipeline layout descriptor binding signature");
            if (!descriptorSignature) {
                return std::unexpected{std::move(descriptorSignature.error())};
            }
            auto pushConstantSignature = expectUint(
                signature.pushConstantCount, 0, "Triangle pipeline layout push constant signature");
            if (!pushConstantSignature) {
                return std::unexpected{std::move(pushConstantSignature.error())};
            }

            return signature;
        }

        [[nodiscard]] Result<void>
        validateMesh3DReflection(const std::filesystem::path& shaderDirectory) {
            auto vertexReflection =
                readShaderReflection(shaderDirectory / "basic_mesh3d.vert.reflection.json");
            if (!vertexReflection) {
                return std::unexpected{std::move(vertexReflection.error())};
            }
            auto fragmentReflection =
                readShaderReflection(shaderDirectory / "basic_mesh3d.frag.reflection.json");
            if (!fragmentReflection) {
                return std::unexpected{std::move(fragmentReflection.error())};
            }

            auto vertexEntry = expectString(vertexReflection->entry, "mesh3DVertexMain",
                                            "Mesh3D vertex shader reflection entry");
            if (!vertexEntry) {
                return std::unexpected{std::move(vertexEntry.error())};
            }
            auto vertexStage = expectString(vertexReflection->stage, "vertex",
                                            "Mesh3D vertex shader reflection stage");
            if (!vertexStage) {
                return std::unexpected{std::move(vertexStage.error())};
            }
            auto fragmentEntry = expectString(fragmentReflection->entry, "mesh3DFragmentMain",
                                              "Mesh3D fragment shader reflection entry");
            if (!fragmentEntry) {
                return std::unexpected{std::move(fragmentEntry.error())};
            }
            auto fragmentStage = expectString(fragmentReflection->stage, "fragment",
                                              "Mesh3D fragment shader reflection stage");
            if (!fragmentStage) {
                return std::unexpected{std::move(fragmentStage.error())};
            }

            auto vertexInputCount =
                expectUint(static_cast<std::uint32_t>(vertexReflection->vertexInputs.size()), 2,
                           "Mesh3D vertex shader input count");
            if (!vertexInputCount) {
                return std::unexpected{std::move(vertexInputCount.error())};
            }
            auto position =
                validateVertexInput(*vertexReflection, "POSITION", 0, 0, "float32", 1, 3);
            if (!position) {
                return std::unexpected{std::move(position.error())};
            }
            auto color = validateVertexInput(*vertexReflection, "COLOR", 0, 1, "float32", 1, 3);
            if (!color) {
                return std::unexpected{std::move(color.error())};
            }

            return {};
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateDescriptorLayoutReflection(const std::filesystem::path& shaderDirectory) {
            auto fragmentReflection =
                readShaderReflection(shaderDirectory / "descriptor_layout.frag.reflection.json");
            if (!fragmentReflection) {
                return std::unexpected{std::move(fragmentReflection.error())};
            }

            auto fragmentEntry = expectString(fragmentReflection->entry, "descriptorFragmentMain",
                                              "Descriptor layout fragment shader reflection entry");
            if (!fragmentEntry) {
                return std::unexpected{std::move(fragmentEntry.error())};
            }
            auto fragmentStage = expectString(fragmentReflection->stage, "fragment",
                                              "Descriptor layout fragment shader reflection stage");
            if (!fragmentStage) {
                return std::unexpected{std::move(fragmentStage.error())};
            }

            const std::array shaderReflections{*fragmentReflection};
            ShaderResourceSignature signature = shaderResourceSignature(shaderReflections);
            auto descriptorSignature =
                expectUint(signature.descriptorBindingCount, 3,
                           "Descriptor layout smoke descriptor binding signature");
            if (!descriptorSignature) {
                return std::unexpected{std::move(descriptorSignature.error())};
            }
            auto pushConstantSignature = expectUint(
                signature.pushConstantCount, 0, "Descriptor layout smoke push constant signature");
            if (!pushConstantSignature) {
                return std::unexpected{std::move(pushConstantSignature.error())};
            }

            auto constantBuffer = validateDescriptorBinding(
                signature, 0, 0, "constantBuffer", "Descriptor layout smoke constant buffer");
            if (!constantBuffer) {
                return std::unexpected{std::move(constantBuffer.error())};
            }
            auto texture = validateDescriptorBinding(signature, 0, 1, "texture",
                                                     "Descriptor layout smoke sampled image");
            if (!texture) {
                return std::unexpected{std::move(texture.error())};
            }
            auto sampler = validateDescriptorBinding(signature, 0, 2, "sampler",
                                                     "Descriptor layout smoke sampler");
            if (!sampler) {
                return std::unexpected{std::move(sampler.error())};
            }

            return signature;
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateFullscreenTextureReflection(const std::filesystem::path& shaderDirectory) {
            auto vertexReflection =
                readShaderReflection(shaderDirectory / "descriptor_layout.vert.reflection.json");
            if (!vertexReflection) {
                return std::unexpected{std::move(vertexReflection.error())};
            }
            auto fragmentSignature = validateDescriptorLayoutReflection(shaderDirectory);
            if (!fragmentSignature) {
                return std::unexpected{std::move(fragmentSignature.error())};
            }

            auto vertexEntry = expectString(vertexReflection->entry, "descriptorVertexMain",
                                            "Fullscreen vertex shader reflection entry");
            if (!vertexEntry) {
                return std::unexpected{std::move(vertexEntry.error())};
            }
            auto vertexStage = expectString(vertexReflection->stage, "vertex",
                                            "Fullscreen vertex shader reflection stage");
            if (!vertexStage) {
                return std::unexpected{std::move(vertexStage.error())};
            }
            auto vertexInputCount =
                expectUint(static_cast<std::uint32_t>(vertexReflection->vertexInputs.size()), 0,
                           "Fullscreen vertex shader input count");
            if (!vertexInputCount) {
                return std::unexpected{std::move(vertexInputCount.error())};
            }
            return *fragmentSignature;
        }

        [[nodiscard]] Result<ShaderResourceSignature>
        validateBasicComputeReflection(const std::filesystem::path& shaderDirectory) {
            auto computeReflection =
                readShaderReflection(shaderDirectory / "basic_compute.comp.reflection.json");
            if (!computeReflection) {
                return std::unexpected{std::move(computeReflection.error())};
            }

            auto computeEntry = expectString(computeReflection->entry, "computeMain",
                                             "Compute shader reflection entry");
            if (!computeEntry) {
                return std::unexpected{std::move(computeEntry.error())};
            }
            auto computeStage = expectString(computeReflection->stage, "compute",
                                             "Compute shader reflection stage");
            if (!computeStage) {
                return std::unexpected{std::move(computeStage.error())};
            }

            const std::array shaderReflections{*computeReflection};
            ShaderResourceSignature signature = shaderResourceSignature(shaderReflections);
            auto descriptorSignature = expectUint(signature.descriptorBindingCount, 1,
                                                  "Compute dispatch descriptor binding signature");
            if (!descriptorSignature) {
                return std::unexpected{std::move(descriptorSignature.error())};
            }
            auto pushConstantSignature = expectUint(signature.pushConstantCount, 0,
                                                    "Compute dispatch push constant signature");
            if (!pushConstantSignature) {
                return std::unexpected{std::move(pushConstantSignature.error())};
            }

            const ShaderDescriptorBindingReflection* storageBuffer =
                findDescriptorBinding(signature, 0, 0);
            if (storageBuffer == nullptr) {
                return std::unexpected{
                    shaderError("Compute dispatch storage buffer missing descriptor binding")};
            }
            auto storageCount =
                expectUint(storageBuffer->count, 1, "Compute dispatch storage buffer count");
            if (!storageCount) {
                return std::unexpected{std::move(storageCount.error())};
            }
            if (storageBuffer->kind != "mutableTypedBuffer" &&
                storageBuffer->kind != "mutableRawBuffer") {
                return std::unexpected{
                    shaderError("Compute dispatch storage buffer kind expected mutable storage "
                                "buffer but found " +
                                storageBuffer->kind)};
            }
            auto storageStage = expectString(storageBuffer->stageVisibility, "compute",
                                             "Compute dispatch storage buffer stage");
            if (!storageStage) {
                return std::unexpected{std::move(storageStage.error())};
            }

            return signature;
        }
