#include "vke/rhi_vulkan/vulkan_context.hpp"

#include "vke/core/error.hpp"
#include "vke/core/log.hpp"
#include "vke/core/version.hpp"
#include "vke/rhi_vulkan/vulkan_error.hpp"

#define VMA_IMPLEMENTATION
// clang-format off
#include <vk_mem_alloc.h>
// clang-format on
#include <algorithm>
#include <array>
#include <expected>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace vke {
    namespace {

        constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
        constexpr std::string_view kDebugUtilsExtension{VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
        constexpr const char* kSwapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

        Result<std::vector<VkLayerProperties>> enumerateInstanceLayers() {
            while (true) {
                std::uint32_t count = 0;
                VkResult result = vkEnumerateInstanceLayerProperties(&count, nullptr);
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan instance layers", result)};
                }

                std::vector<VkLayerProperties> layers(count);
                if (count == 0) {
                    return layers;
                }

                result = vkEnumerateInstanceLayerProperties(&count, layers.data());
                if (result == VK_SUCCESS) {
                    layers.resize(count);
                    return layers;
                }

                if (result != VK_INCOMPLETE) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan instance layers", result)};
                }
            }
        }

        Result<std::vector<VkExtensionProperties>> enumerateInstanceExtensions() {
            while (true) {
                std::uint32_t count = 0;
                VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan instance extensions", result)};
                }

                std::vector<VkExtensionProperties> extensions(count);
                if (count == 0) {
                    return extensions;
                }

                result = vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
                if (result == VK_SUCCESS) {
                    extensions.resize(count);
                    return extensions;
                }

                if (result != VK_INCOMPLETE) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan instance extensions", result)};
                }
            }
        }

        Result<std::vector<VkExtensionProperties>>
        enumerateDeviceExtensions(VkPhysicalDevice device) {
            std::uint32_t count = 0;
            VkResult result =
                vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to enumerate Vulkan device extensions", result)};
            }

            std::vector<VkExtensionProperties> extensions(count);
            if (count > 0) {
                result = vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                              extensions.data());
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan device extensions", result)};
                }
            }

            return extensions;
        }

        bool hasLayer(std::span<const VkLayerProperties> layers, std::string_view name) {
            return std::ranges::any_of(
                layers, [name](const VkLayerProperties& layer) { return name == layer.layerName; });
        }

        bool hasExtension(std::span<const VkExtensionProperties> extensions,
                          std::string_view name) {
            return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
                return name == extension.extensionName;
            });
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL
        debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                      [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
                      const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                      [[maybe_unused]] void* userData) {
            const std::string_view message =
                callbackData != nullptr && callbackData->pMessage != nullptr
                    ? callbackData->pMessage
                    : "Vulkan validation message without text.";

            // Some third-party overlay layers advertise an older Vulkan API version than the app
            // requests. Keep validation output focused on engine issues instead of known external
            // layer noise.
            if (message.contains("Layer VK_LAYER_OBS_HOOK uses API version") &&
                message.contains("older than the application specified API version")) {
                return VK_FALSE;
            }

            if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
                logError(message);
            } else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
                logWarning(message);
            } else {
                logTrace(message);
            }

            return VK_FALSE;
        }

        VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo() {
            VkDebugUtilsMessengerCreateInfoEXT createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            createInfo.pfnUserCallback = debugCallback;
            return createInfo;
        }

        VkResult createDebugMessenger(VkInstance instance,
                                      const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
                                      VkDebugUtilsMessengerEXT* messenger) {
            // Vulkan extension entry points are discovered as generic function pointers by design.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (function == nullptr) {
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }

            return function(instance, createInfo, nullptr, messenger);
        }

        void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
            // Vulkan extension entry points are discovered as generic function pointers by design.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (function != nullptr) {
                function(instance, messenger, nullptr);
            }
        }

        struct QueueSelection {
            std::uint32_t graphicsFamily{};
        };

        Result<bool> queueSupportsPresentation(VkPhysicalDevice physicalDevice,
                                               std::uint32_t queueFamily, VkSurfaceKHR surface) {
            if (surface == VK_NULL_HANDLE) {
                return true;
            }

            VkBool32 supported = VK_FALSE;
            const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
                physicalDevice, queueFamily, surface, &supported);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to query Vulkan queue presentation support", result)};
            }

            return supported == VK_TRUE;
        }

        Result<std::optional<QueueSelection>> selectQueues(VkPhysicalDevice physicalDevice,
                                                           VkSurfaceKHR surface) {
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);

            std::vector<VkQueueFamilyProperties> queues(count);
            if (count > 0) {
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queues.data());
            }

            for (std::uint32_t index = 0; index < queues.size(); ++index) {
                auto supportsPresent = queueSupportsPresentation(physicalDevice, index, surface);
                if (!supportsPresent) {
                    return std::unexpected{std::move(supportsPresent.error())};
                }

                if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && *supportsPresent) {
                    return QueueSelection{index};
                }
            }

            return std::nullopt;
        }

        bool supportsRequiredVersion(const VkPhysicalDeviceProperties& properties,
                                     bool requireVulkan14) {
            if (!requireVulkan14) {
                return true;
            }

            return properties.apiVersion >= VK_API_VERSION_1_4;
        }

        Result<bool> supportsSwapchain(VkPhysicalDevice device, bool required) {
            if (!required) {
                return true;
            }

            auto extensions = enumerateDeviceExtensions(device);
            if (!extensions) {
                return std::unexpected{std::move(extensions.error())};
            }

            return hasExtension(*extensions, kSwapchainExtension);
        }

        struct PhysicalDeviceCandidate {
            VkPhysicalDevice device{VK_NULL_HANDLE};
            VkPhysicalDeviceProperties properties{};
            QueueSelection queues{};
        };

        Result<std::optional<PhysicalDeviceCandidate>>
        choosePhysicalDevice(VkInstance instance, VkSurfaceKHR surface, bool requireVulkan14) {
            std::uint32_t count = 0;
            VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to enumerate Vulkan physical devices", result)};
            }

            std::vector<VkPhysicalDevice> devices(count);
            if (count > 0) {
                result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan physical devices", result)};
                }
            }

            std::optional<PhysicalDeviceCandidate> best;

            for (VkPhysicalDevice device : devices) {
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(device, &properties);

                auto queues = selectQueues(device, surface);
                if (!queues) {
                    return std::unexpected{std::move(queues.error())};
                }

                auto swapchainSupported = supportsSwapchain(device, surface != VK_NULL_HANDLE);
                if (!swapchainSupported) {
                    return std::unexpected{std::move(swapchainSupported.error())};
                }

                if (!*queues || !supportsRequiredVersion(properties, requireVulkan14) ||
                    !*swapchainSupported) {
                    continue;
                }

                PhysicalDeviceCandidate candidate{
                    .device = device,
                    .properties = properties,
                    .queues = **queues,
                };
                if (!best || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    best = candidate;
                }
            }

            return best;
        }

        std::vector<const char*> makePointerList(const std::vector<std::string>& strings) {
            std::vector<const char*> pointers;
            pointers.reserve(strings.size());
            for (const std::string& string : strings) {
                pointers.push_back(string.c_str());
            }
            return pointers;
        }

        struct InstanceCreateResult {
            VkInstance instance{VK_NULL_HANDLE};
            std::uint32_t apiVersion{VK_API_VERSION_1_3};
        };

        Result<InstanceCreateResult> createInstance(const VulkanContextDesc& desc,
                                                    bool validationAvailable,
                                                    bool debugUtilsAvailable) {
            std::uint32_t loaderVersion = VK_API_VERSION_1_0;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
            if (enumerateInstanceVersion != nullptr) {
                const VkResult versionResult = enumerateInstanceVersion(&loaderVersion);
                if (versionResult != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to enumerate Vulkan instance version", versionResult)};
                }
            }

            const std::uint32_t requestedApiVersion =
                desc.requireVulkan14 ? VK_API_VERSION_1_4 : VK_API_VERSION_1_3;
            if (loaderVersion < requestedApiVersion) {
                return std::unexpected{vulkanError("Vulkan loader does not support Vulkan " +
                                               vulkanVersionString(requestedApiVersion))};
            }

            std::vector<std::string> extensions{
                desc.requiredInstanceExtensions.begin(),
                desc.requiredInstanceExtensions.end(),
            };

            if (desc.enableValidation && debugUtilsAvailable) {
                extensions.emplace_back(kDebugUtilsExtension);
            }

            std::ranges::sort(extensions);
            extensions.erase(std::ranges::unique(extensions).begin(), extensions.end());

            const auto extensionPointers = makePointerList(extensions);

            std::array<const char*, 1> validationLayers{kValidationLayer};

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = desc.applicationName.c_str();
            appInfo.applicationVersion = VK_MAKE_API_VERSION(
                0, kEngineVersion.major, kEngineVersion.minor, kEngineVersion.patch);
            appInfo.pEngineName = kEngineName.data();
            appInfo.engineVersion = appInfo.applicationVersion;
            appInfo.apiVersion = requestedApiVersion;

            auto debugInfo = debugMessengerCreateInfo();

            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensionPointers.size());
            createInfo.ppEnabledExtensionNames = extensionPointers.data();

            if (desc.enableValidation && validationAvailable) {
                createInfo.enabledLayerCount = static_cast<std::uint32_t>(validationLayers.size());
                createInfo.ppEnabledLayerNames = validationLayers.data();
                createInfo.pNext = &debugInfo;
            }

            VkInstance instance = VK_NULL_HANDLE;
            const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan instance", result)};
            }

            return InstanceCreateResult{
                .instance = instance,
                .apiVersion = requestedApiVersion,
            };
        }

        Result<VkDevice> createDevice(VkPhysicalDevice physicalDevice,
                                      std::uint32_t graphicsQueueFamily,
                                      VkPhysicalDeviceVulkan11Features features11,
                                      VkPhysicalDeviceVulkan13Features features13,
                                      VkPhysicalDeviceVulkan14Features features14,
                                      bool enableSwapchain) {
            constexpr float kQueuePriority = 1.0F;

            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = graphicsQueueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &kQueuePriority;

            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &features11;
            features11.pNext = &features13;
            features13.pNext = &features14;

            VkDeviceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pNext = &features2;
            createInfo.queueCreateInfoCount = 1;
            createInfo.pQueueCreateInfos = &queueInfo;

            std::array<const char*, 1> swapchainExtensions{kSwapchainExtension};
            if (enableSwapchain) {
                createInfo.enabledExtensionCount =
                    static_cast<std::uint32_t>(swapchainExtensions.size());
                createInfo.ppEnabledExtensionNames = swapchainExtensions.data();
            }

            VkDevice device = VK_NULL_HANDLE;
            const VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan logical device", result)};
            }

            return device;
        }

        Result<VmaAllocator> createAllocator(VkInstance instance, VkPhysicalDevice physicalDevice,
                                             VkDevice device, std::uint32_t apiVersion) {
            VmaAllocatorCreateInfo createInfo{};
            createInfo.instance = instance;
            createInfo.physicalDevice = physicalDevice;
            createInfo.device = device;
            createInfo.vulkanApiVersion = apiVersion;

            VmaAllocator allocator = nullptr;
            const VkResult result = vmaCreateAllocator(&createInfo, &allocator);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create VMA allocator", result)};
            }

            return allocator;
        }

    } // namespace

    std::string vkResultName(VkResult result) {
        switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        default:
            return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
        }
    }

    std::string vulkanVersionString(std::uint32_t version) {
        std::ostringstream stream;
        stream << VK_API_VERSION_MAJOR(version) << '.' << VK_API_VERSION_MINOR(version) << '.'
               << VK_API_VERSION_PATCH(version);
        return stream.str();
    }

    VulkanContext::VulkanContext(VulkanContext&& other) noexcept {
        *this = std::move(other);
    }

    VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();

        instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
        instanceApiVersion_ = std::exchange(other.instanceApiVersion_, VK_API_VERSION_1_3);
        debugMessenger_ = std::exchange(other.debugMessenger_, VK_NULL_HANDLE);
        surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
        physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
        graphicsQueueFamily_ = std::exchange(other.graphicsQueueFamily_, 0);
        allocator_ = std::exchange(other.allocator_, nullptr);
        deviceInfo_ = std::move(other.deviceInfo_);
        return *this;
    }

    VulkanContext::~VulkanContext() {
        destroy();
    }

    void VulkanContext::destroy() {
        if (allocator_ != nullptr) {
            vmaDestroyAllocator(allocator_);
        }

        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
        }

        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }

        if (debugMessenger_ != VK_NULL_HANDLE) {
            destroyDebugMessenger(instance_, debugMessenger_);
        }

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
        }

        instance_ = VK_NULL_HANDLE;
        instanceApiVersion_ = VK_API_VERSION_1_3;
        debugMessenger_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
        physicalDevice_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        graphicsQueueFamily_ = 0;
        allocator_ = nullptr;
    }

    Result<VulkanContext> VulkanContext::create(const VulkanContextDesc& desc) {
        auto layers = enumerateInstanceLayers();
        if (!layers) {
            return std::unexpected{std::move(layers.error())};
        }

        auto extensions = enumerateInstanceExtensions();
        if (!extensions) {
            return std::unexpected{std::move(extensions.error())};
        }

        const bool validationAvailable = hasLayer(*layers, kValidationLayer);
        if (desc.enableValidation && !validationAvailable) {
            return std::unexpected{vulkanError("Requested Vulkan validation layer is not available")};
        }

        const bool debugUtilsAvailable = hasExtension(*extensions, kDebugUtilsExtension);
        if (desc.enableValidation && !debugUtilsAvailable) {
            return std::unexpected{vulkanError("Requested VK_EXT_debug_utils is not available")};
        }

        for (const std::string& extension : desc.requiredInstanceExtensions) {
            if (!hasExtension(*extensions, extension)) {
                return std::unexpected{
                    vulkanError("Required Vulkan instance extension is not available: " + extension)};
            }
        }

        auto instance = createInstance(desc, validationAvailable, debugUtilsAvailable);
        if (!instance) {
            return std::unexpected{std::move(instance.error())};
        }

        VulkanContext context;
        context.instance_ = instance->instance;
        context.instanceApiVersion_ = instance->apiVersion;

        if (desc.enableValidation) {
            auto debugInfo = debugMessengerCreateInfo();
            const VkResult result =
                createDebugMessenger(context.instance_, &debugInfo, &context.debugMessenger_);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan debug messenger", result)};
            }
        }

        if (desc.createSurface) {
            auto surface = desc.createSurface(context.instance_);
            if (!surface) {
                return std::unexpected{std::move(surface.error())};
            }

            context.surface_ = *surface;
        }

        auto candidate =
            choosePhysicalDevice(context.instance_, context.surface_, desc.requireVulkan14);
        if (!candidate) {
            return std::unexpected{std::move(candidate.error())};
        }

        if (!*candidate) {
            return std::unexpected{
                vulkanError("Failed to find a Vulkan 1.4 graphics and presentation device")};
        }

        const PhysicalDeviceCandidate& selected = **candidate;

        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceVulkan14Features features14{};
        features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;

        VkPhysicalDeviceFeatures2 queriedFeatures{};
        queriedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        queriedFeatures.pNext = &features11;
        features11.pNext = &features13;
        features13.pNext = &features14;
        vkGetPhysicalDeviceFeatures2(selected.device, &queriedFeatures);

        if (features13.synchronization2 != VK_TRUE || features13.dynamicRendering != VK_TRUE) {
            return std::unexpected{
                vulkanError("Selected device does not support synchronization2 and dynamic rendering")};
        }
        if (features11.shaderDrawParameters != VK_TRUE) {
            return std::unexpected{
                vulkanError("Selected device does not support shaderDrawParameters")};
        }

        features11 = {};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features11.shaderDrawParameters = VK_TRUE;

        features13 = {};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.synchronization2 = VK_TRUE;
        features13.dynamicRendering = VK_TRUE;

        features14 = {};
        features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;

        auto device = createDevice(selected.device, selected.queues.graphicsFamily, features11,
                                   features13, features14,
                                   context.surface_ != VK_NULL_HANDLE);
        if (!device) {
            return std::unexpected{std::move(device.error())};
        }

        context.physicalDevice_ = selected.device;
        context.device_ = *device;
        context.graphicsQueueFamily_ = selected.queues.graphicsFamily;
        vkGetDeviceQueue(context.device_, context.graphicsQueueFamily_, 0, &context.graphicsQueue_);

        auto allocator =
            createAllocator(context.instance_, context.physicalDevice_, context.device_,
                            context.instanceApiVersion_);
        if (!allocator) {
            return std::unexpected{std::move(allocator.error())};
        }

        context.allocator_ = *allocator;
        context.deviceInfo_ = VulkanDeviceInfo{
            .name = selected.properties.deviceName,
            .vendorId = selected.properties.vendorID,
            .deviceId = selected.properties.deviceID,
            .apiVersion = selected.properties.apiVersion,
            .graphicsQueueFamily = context.graphicsQueueFamily_,
        };

        logInfo("Selected Vulkan device: " + context.deviceInfo_.name + " (" +
                vulkanVersionString(context.deviceInfo_.apiVersion) + ")");

        return context;
    }

    VkInstance VulkanContext::instance() const {
        return instance_;
    }

    std::uint32_t VulkanContext::instanceApiVersion() const {
        return instanceApiVersion_;
    }

    VkSurfaceKHR VulkanContext::surface() const {
        return surface_;
    }

    VkPhysicalDevice VulkanContext::physicalDevice() const {
        return physicalDevice_;
    }

    VkDevice VulkanContext::device() const {
        return device_;
    }

    VkQueue VulkanContext::graphicsQueue() const {
        return graphicsQueue_;
    }

    std::uint32_t VulkanContext::graphicsQueueFamily() const {
        return graphicsQueueFamily_;
    }

    VmaAllocator VulkanContext::allocator() const {
        return allocator_;
    }

    const VulkanDeviceInfo& VulkanContext::deviceInfo() const {
        return deviceInfo_;
    }

} // namespace vke
