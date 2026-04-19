#include "vke/rhi_vulkan/vulkan_context.hpp"

#include "vke/core/error.hpp"
#include "vke/core/log.hpp"
#include "vke/core/version.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

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

constexpr std::string_view kValidationLayer{"VK_LAYER_KHRONOS_validation"};
constexpr std::string_view kDebugUtilsExtension{VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

Error vkError(std::string message, VkResult result = VK_ERROR_UNKNOWN) {
    if (result != VK_SUCCESS) {
        message += ": ";
        message += vkResultName(result);
    }

    return Error{ErrorDomain::Vulkan, static_cast<int>(result), std::move(message)};
}

std::vector<VkLayerProperties> enumerateInstanceLayers() {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);

    std::vector<VkLayerProperties> layers(count);
    if (count > 0) {
        vkEnumerateInstanceLayerProperties(&count, layers.data());
    }

    return layers;
}

std::vector<VkExtensionProperties> enumerateInstanceExtensions() {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);

    std::vector<VkExtensionProperties> extensions(count);
    if (count > 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    }

    return extensions;
}

bool hasLayer(std::span<const VkLayerProperties> layers, std::string_view name) {
    return std::ranges::any_of(layers, [name](const VkLayerProperties& layer) {
        return name == layer.layerName;
    });
}

bool hasExtension(std::span<const VkExtensionProperties> extensions, std::string_view name) {
    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
        return name == extension.extensionName;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*) {
    const std::string_view message = callbackData != nullptr && callbackData->pMessage != nullptr
        ? callbackData->pMessage
        : "Vulkan validation message without text.";

    // Some third-party overlay layers advertise an older Vulkan API version than the app requests.
    // Keep validation output focused on engine issues instead of known external layer noise.
    if (message.contains("Layer VK_LAYER_OBS_HOOK uses API version")
        && message.contains("older than the application specified API version")) {
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
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    return createInfo;
}

VkResult createDebugMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    VkDebugUtilsMessengerEXT* messenger) {
    const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (function == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    return function(instance, createInfo, nullptr, messenger);
}

void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (function != nullptr) {
        function(instance, messenger, nullptr);
    }
}

struct QueueSelection {
    std::uint32_t graphicsFamily{};
};

std::optional<QueueSelection> selectQueues(VkPhysicalDevice physicalDevice) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);

    std::vector<VkQueueFamilyProperties> queues(count);
    if (count > 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queues.data());
    }

    for (std::uint32_t index = 0; index < queues.size(); ++index) {
        if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return QueueSelection{index};
        }
    }

    return std::nullopt;
}

bool supportsRequiredVersion(const VkPhysicalDeviceProperties& properties, bool requireVulkan14) {
    if (!requireVulkan14) {
        return true;
    }

    return properties.apiVersion >= VK_API_VERSION_1_4;
}

struct PhysicalDeviceCandidate {
    VkPhysicalDevice device{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties{};
    QueueSelection queues{};
};

std::optional<PhysicalDeviceCandidate> choosePhysicalDevice(
    VkInstance instance,
    bool requireVulkan14) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);

    std::vector<VkPhysicalDevice> devices(count);
    if (count > 0) {
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
    }

    std::optional<PhysicalDeviceCandidate> best;

    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        const auto queues = selectQueues(device);
        if (!queues || !supportsRequiredVersion(properties, requireVulkan14)) {
            continue;
        }

        PhysicalDeviceCandidate candidate{device, properties, *queues};
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

Result<VkInstance> createInstance(
    const VulkanContextDesc& desc,
    bool validationAvailable,
    bool debugUtilsAvailable) {
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion != nullptr) {
        const VkResult versionResult = vkEnumerateInstanceVersion(&loaderVersion);
        if (versionResult != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to enumerate Vulkan instance version", versionResult)};
        }
    }

    if (desc.requireVulkan14 && loaderVersion < VK_API_VERSION_1_4) {
        return std::unexpected{vkError("Vulkan loader does not support Vulkan 1.4")};
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

    std::array<const char*, 1> validationLayers{kValidationLayer.data()};

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = desc.applicationName.c_str();
    appInfo.applicationVersion = VK_MAKE_API_VERSION(
        0,
        kEngineVersion.major,
        kEngineVersion.minor,
        kEngineVersion.patch);
    appInfo.pEngineName = kEngineName.data();
    appInfo.engineVersion = appInfo.applicationVersion;
    appInfo.apiVersion = desc.requireVulkan14 ? VK_API_VERSION_1_4 : VK_API_VERSION_1_3;

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
        return std::unexpected{vkError("Failed to create Vulkan instance", result)};
    }

    return instance;
}

Result<VkDevice> createDevice(
    VkPhysicalDevice physicalDevice,
    std::uint32_t graphicsQueueFamily,
    VkPhysicalDeviceVulkan13Features features13) {
    constexpr float kQueuePriority = 1.0F;

    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &kQueuePriority;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device = VK_NULL_HANDLE;
    const VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        return std::unexpected{vkError("Failed to create Vulkan logical device", result)};
    }

    return device;
}

Result<VmaAllocator> createAllocator(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device) {
    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = instance;
    createInfo.physicalDevice = physicalDevice;
    createInfo.device = device;
    createInfo.vulkanApiVersion = VK_API_VERSION_1_4;

    VmaAllocator allocator = nullptr;
    const VkResult result = vmaCreateAllocator(&createInfo, &allocator);
    if (result != VK_SUCCESS) {
        return std::unexpected{vkError("Failed to create VMA allocator", result)};
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
    debugMessenger_ = std::exchange(other.debugMessenger_, VK_NULL_HANDLE);
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

    if (debugMessenger_ != VK_NULL_HANDLE) {
        destroyDebugMessenger(instance_, debugMessenger_);
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }

    instance_ = VK_NULL_HANDLE;
    debugMessenger_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    graphicsQueueFamily_ = 0;
    allocator_ = nullptr;
}

Result<VulkanContext> VulkanContext::create(const VulkanContextDesc& desc) {
    const auto layers = enumerateInstanceLayers();
    const auto extensions = enumerateInstanceExtensions();

    const bool validationAvailable = hasLayer(layers, kValidationLayer);
    if (desc.enableValidation && !validationAvailable) {
        return std::unexpected{vkError("Requested Vulkan validation layer is not available")};
    }

    const bool debugUtilsAvailable = hasExtension(extensions, kDebugUtilsExtension);
    if (desc.enableValidation && !debugUtilsAvailable) {
        return std::unexpected{vkError("Requested VK_EXT_debug_utils is not available")};
    }

    for (const std::string& extension : desc.requiredInstanceExtensions) {
        if (!hasExtension(extensions, extension)) {
            return std::unexpected{vkError("Required Vulkan instance extension is not available: " + extension)};
        }
    }

    auto instance = createInstance(desc, validationAvailable, debugUtilsAvailable);
    if (!instance) {
        return std::unexpected{std::move(instance.error())};
    }

    VulkanContext context;
    context.instance_ = *instance;

    if (desc.enableValidation) {
        auto debugInfo = debugMessengerCreateInfo();
        const VkResult result = createDebugMessenger(context.instance_, &debugInfo, &context.debugMessenger_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to create Vulkan debug messenger", result)};
        }
    }

    auto candidate = choosePhysicalDevice(context.instance_, desc.requireVulkan14);
    if (!candidate) {
        return std::unexpected{vkError("Failed to find a Vulkan 1.4 graphics device")};
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceFeatures2 queriedFeatures{};
    queriedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    queriedFeatures.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(candidate->device, &queriedFeatures);

    if (features13.synchronization2 != VK_TRUE || features13.dynamicRendering != VK_TRUE) {
        return std::unexpected{vkError("Selected device does not support synchronization2 and dynamic rendering")};
    }

    features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;

    auto device = createDevice(candidate->device, candidate->queues.graphicsFamily, features13);
    if (!device) {
        return std::unexpected{std::move(device.error())};
    }

    context.physicalDevice_ = candidate->device;
    context.device_ = *device;
    context.graphicsQueueFamily_ = candidate->queues.graphicsFamily;
    vkGetDeviceQueue(context.device_, context.graphicsQueueFamily_, 0, &context.graphicsQueue_);

    auto allocator = createAllocator(context.instance_, context.physicalDevice_, context.device_);
    if (!allocator) {
        return std::unexpected{std::move(allocator.error())};
    }

    context.allocator_ = *allocator;
    context.deviceInfo_ = VulkanDeviceInfo{
        .name = candidate->properties.deviceName,
        .vendorId = candidate->properties.vendorID,
        .deviceId = candidate->properties.deviceID,
        .apiVersion = candidate->properties.apiVersion,
        .graphicsQueueFamily = context.graphicsQueueFamily_,
    };

    logInfo(
        "Selected Vulkan device: " + context.deviceInfo_.name + " ("
        + vulkanVersionString(context.deviceInfo_.apiVersion) + ")");

    return context;
}

VkInstance VulkanContext::instance() const {
    return instance_;
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
