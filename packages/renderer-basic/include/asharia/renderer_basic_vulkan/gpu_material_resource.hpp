#pragma once

#include <memory>
#include <thread>
#include <vector>

#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_pipeline.hpp"
#include "asharia/shader_slang/reflection.hpp"

namespace asharia {
    enum class BasicGpuMaterialError : int {
        InvalidInput = 1,
        IncompatibleLayout,
        StaleRevision,
        BudgetExceeded,
        WrongThread,
    };

    // Caller supplies paired, compiler-validated SPIR-V/reflection products. No source IO here.
    // MVP uses the existing mesh vertex ABI, an authored fragment, and set 1/binding 0.
    struct BasicGpuMaterialProgramDesc {
        VkDevice device{VK_NULL_HANDLE};
        VkFormat colorFormat{VK_FORMAT_UNDEFINED};
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        std::span<const std::uint32_t> vertexCode;
        std::span<const std::uint32_t> fragmentCode;
        const ShaderReflection* vertexReflection{};
        const ShaderReflection* fragmentReflection{};
    };

    class BasicGpuMaterialProgram final {
    public:
        [[nodiscard]] static Result<std::shared_ptr<const BasicGpuMaterialProgram>>
        create(const BasicGpuMaterialProgramDesc& desc);
        [[nodiscard]] const ShaderDescriptorBindingReflection& binding() const {
            return binding_;
        }

    private:
        friend class BasicGpuMaterialOwner;
        friend class BasicFullscreenTextureRenderer;
        VkDevice device_{VK_NULL_HANDLE};
        VkFormat colorFormat_{VK_FORMAT_UNDEFINED};
        VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
        ShaderDescriptorBindingReflection binding_;
        VulkanShaderModule vertex_;
        VulkanShaderModule fragment_;
        VulkanDescriptorSetLayout emptyLayout_;
        VulkanDescriptorSetLayout materialLayout_;
        VulkanPipelineLayout layout_;
        VulkanGraphicsPipeline pipeline_;
    };

    // Immutable binding version. Frame completion retains it and its shared program.
    class BasicGpuMaterial final {
    public:
        [[nodiscard]] BasicDrawResourceKey key() const {
            return key_;
        }
        [[nodiscard]] std::uint64_t revision() const {
            return revision_;
        }
        [[nodiscard]] const BasicGpuMaterialProgram* program() const {
            return program_.get();
        }

    private:
        friend class BasicGpuMaterialOwner;
        friend class BasicFullscreenTextureRenderer;
        std::shared_ptr<const BasicGpuMaterialProgram> program_;
        BasicDrawResourceKey key_{};
        std::uint64_t revision_{};
        VulkanBuffer parameters_;
        VulkanDescriptorAllocator descriptors_;
        VkDescriptorSet descriptor_{VK_NULL_HANDLE};
    };

    struct BasicGpuMaterialOwnerDesc {
        VmaAllocator allocator{};
        BasicDrawResourceKey key{};
        std::shared_ptr<const BasicGpuMaterialProgram> program;
        std::size_t maxResidentVersions{4};
    };

    // Render-thread-only, one logical material. Failed/stale updates preserve the current version.
    // All owners and acquired references must be released before device/allocator destruction.
    class BasicGpuMaterialOwner final {
    public:
        BasicGpuMaterialOwner(const BasicGpuMaterialOwner&) = delete;
        BasicGpuMaterialOwner& operator=(const BasicGpuMaterialOwner&) = delete;
        BasicGpuMaterialOwner(BasicGpuMaterialOwner&&) noexcept = default;
        BasicGpuMaterialOwner& operator=(BasicGpuMaterialOwner&&) noexcept = default;
        [[nodiscard]] static Result<BasicGpuMaterialOwner> create(BasicGpuMaterialOwnerDesc desc);
        [[nodiscard]] VoidResult update(std::uint64_t revision,
                                        const ShaderParameterBlockReflection& layout,
                                        std::span<const std::byte> parameters);
        [[nodiscard]] Result<std::shared_ptr<const BasicGpuMaterial>> acquire() const;
        [[nodiscard]] VoidResult clear();
        [[nodiscard]] std::size_t residentVersions() const;

    private:
        BasicGpuMaterialOwner() = default;
        BasicGpuMaterialOwnerDesc desc_;
        std::thread::id thread_;
        std::uint64_t lastRevision_{};
        std::shared_ptr<const BasicGpuMaterial> active_;
        std::vector<std::weak_ptr<const BasicGpuMaterial>> residents_;
    };
} // namespace asharia
