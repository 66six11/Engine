#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "vke/core/log.hpp"
#include "vke/core/version.hpp"
#include "vke/profiling/frame_profiler.hpp"
#include "vke/renderer_basic/render_graph_schemas.hpp"
#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"
#include "vke/renderer_basic_vulkan/clear_frame.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/deferred_deletion_queue.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"
#include "vke/window_glfw/glfw_window.hpp"

namespace {

    constexpr vke::VulkanDebugLabelMode kSmokeDebugLabels = vke::VulkanDebugLabelMode::Required;

    bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return arg != nullptr && arg == expected; });
    }

    std::optional<std::string_view> argValue(std::span<char*> args, std::string_view option) {
        for (std::size_t index = 1; index + 1 < args.size(); ++index) {
            if (args[index] != nullptr && args[index] == option && args[index + 1] != nullptr) {
                return std::string_view{args[index + 1]};
            }
        }
        return std::nullopt;
    }

    void printVersion() {
        std::cout << vke::kEngineName << ' ' << vke::kEngineVersion.major << '.'
                  << vke::kEngineVersion.minor << '.' << vke::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout << "Usage: vke-sample-viewer [--help] [--version] [--smoke-window] "
                     "[--smoke-vulkan] [--smoke-frame] [--smoke-rendergraph] "
                     "[--smoke-transient] [--smoke-dynamic-rendering] [--smoke-resize] "
                     "[--smoke-triangle] [--smoke-depth-triangle] [--smoke-mesh] "
                     "[--smoke-mesh-3d] [--smoke-draw-list] "
                     "[--smoke-descriptor-layout] [--smoke-fullscreen-texture] "
                     "[--smoke-offscreen-viewport] [--smoke-deferred-deletion] "
                     "[--bench-rendergraph --warmup N --frames N --output path]\n";
    }

    bool isRenderableExtent(vke::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, vke::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    std::string_view triangleSmokeTitle(bool useDepth, vke::BasicMeshKind meshKind) {
        if (meshKind == vke::BasicMeshKind::IndexedQuad) {
            return "VkEngine Mesh Smoke";
        }
        if (useDepth) {
            return "VkEngine Depth Triangle Smoke";
        }
        return "VkEngine Triangle Smoke";
    }

    std::string_view triangleSmokeOutOfDateMessage(bool useDepth, vke::BasicMeshKind meshKind) {
        if (meshKind == vke::BasicMeshKind::IndexedQuad) {
            return "Swapchain remained out of date during mesh smoke.";
        }
        if (useDepth) {
            return "Swapchain remained out of date during depth triangle smoke.";
        }
        return "Swapchain remained out of date during triangle smoke.";
    }

    std::string_view triangleSmokeRenderedPrefix(bool useDepth, vke::BasicMeshKind meshKind) {
        if (meshKind == vke::BasicMeshKind::IndexedQuad) {
            return "Rendered indexed mesh frames: ";
        }
        if (useDepth) {
            return "Rendered depth triangle frames: ";
        }
        return "Rendered triangle frames: ";
    }

    bool validatePipelineCacheStats(vke::BasicPipelineCacheStats stats, std::string_view context) {
        if (stats.created != 1 || stats.reused < 2) {
            vke::logError(std::string{context} +
                          " did not reuse its renderer pipeline after the first frame.");
            return false;
        }
        return true;
    }

    bool validateOffscreenViewportStats(vke::BasicOffscreenViewportStats stats,
                                        std::string_view context) {
        if (stats.renderTargetsCreated != 2 || stats.renderTargetsReused < 2 ||
            stats.renderTargetsDeferredForDeletion != 1) {
            vke::logError(std::string{context} +
                          " did not resize and reuse its offscreen viewport render target.");
            return false;
        }
        return true;
    }

    bool validateOffscreenViewportTarget(vke::BasicOffscreenViewportTarget target,
                                         VkFormat expectedFormat, VkExtent2D expectedExtent,
                                         std::string_view context) {
        if (target.image == VK_NULL_HANDLE || target.imageView == VK_NULL_HANDLE ||
            target.format != expectedFormat || target.extent.width != expectedExtent.width ||
            target.extent.height != expectedExtent.height ||
            target.sampledLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            vke::logError(std::string{context} +
                          " did not expose a sampled offscreen viewport target.");
            return false;
        }
        return true;
    }

    bool validateDescriptorAllocatorStats(vke::VulkanDescriptorAllocatorStats stats,
                                          std::string_view context,
                                          std::uint64_t expectedSets = 1) {
        if (stats.poolsCreated != 1 || stats.allocationCalls != 1 ||
            stats.setsAllocated != expectedSets) {
            vke::logError(std::string{context} +
                          " did not allocate descriptors through the descriptor allocator.");
            return false;
        }
        return true;
    }

    bool validateBufferUploadStats(vke::VulkanBufferStats stats, std::uint64_t expectedBuffers,
                                   std::string_view context) {
        if (stats.created != expectedBuffers || stats.hostUploadCreated != expectedBuffers ||
            stats.uploadCalls != expectedBuffers || stats.allocatedBytes == 0 ||
            stats.uploadedBytes == 0 || stats.uploadedBytes > stats.allocatedBytes) {
            vke::logError(std::string{context} +
                          " did not record the expected buffer upload counters.");
            return false;
        }
        return true;
    }

    bool validateDebugLabelStats(vke::VulkanDebugLabelStats stats, std::string_view context) {
        if (!stats.available || stats.regionsBegun == 0 ||
            stats.regionsBegun != stats.regionsEnded) {
            vke::logError(std::string{context} + " did not record balanced Vulkan debug labels.");
            return false;
        }
        return true;
    }

    bool validateTimestampStats(vke::VulkanTimestampQueryStats stats,
                                std::span<const vke::VulkanTimestampRegionTiming> timings,
                                std::string_view context) {
        if (!stats.available || stats.framesBegun == 0 || stats.framesResolved == 0 ||
            stats.regionsBegun == 0 || stats.regionsBegun != stats.regionsEnded ||
            stats.regionsResolved == 0 || stats.queryReadbacks == 0 || timings.empty()) {
            vke::logError(std::string{context} +
                          " did not record delayed Vulkan timestamp query results.");
            return false;
        }

        const bool hasFrameTiming =
            std::ranges::any_of(timings, [](const vke::VulkanTimestampRegionTiming& timing) {
                return timing.name == "VulkanFrame" && timing.milliseconds >= 0.0;
            });
        if (!hasFrameTiming) {
            vke::logError(std::string{context} +
                          " did not read back a VulkanFrame timestamp duration.");
            return false;
        }

        return true;
    }

    struct RenderGraphBenchOptions {
        std::size_t warmupFrames{60};
        std::size_t measuredFrames{600};
        std::filesystem::path outputPath{"build/perf/rendergraph.jsonl"};
    };

    struct RenderGraphBenchStats {
        double averageMilliseconds{};
        double p50Milliseconds{};
        double p95Milliseconds{};
        double maxMilliseconds{};
    };

    bool parseSizeOption(std::span<char*> args, std::string_view option, std::size_t& value) {
        const std::optional<std::string_view> text = argValue(args, option);
        if (!text) {
            return true;
        }

        std::size_t parsed{};
        const char* begin = text->data();
        const char* end = text->data() + text->size();
        const auto parsedResult = std::from_chars(begin, end, parsed);
        if (parsedResult.ec != std::errc{} || parsedResult.ptr != end || parsed == 0U) {
            vke::logError("Invalid positive integer for " + std::string{option} + ".");
            return false;
        }

        value = parsed;
        return true;
    }

    std::optional<RenderGraphBenchOptions> parseRenderGraphBenchOptions(std::span<char*> args) {
        RenderGraphBenchOptions options;
        if (!parseSizeOption(args, "--warmup", options.warmupFrames) ||
            !parseSizeOption(args, "--frames", options.measuredFrames)) {
            return std::nullopt;
        }

        if (const std::optional<std::string_view> output = argValue(args, "--output")) {
            options.outputPath = std::filesystem::path{std::string{*output}};
        }

        return options;
    }

    vke::RenderGraph createBenchRenderGraph() {
        vke::RenderGraph graph;
        const auto backbuffer = graph.importImage(vke::RenderGraphImageDesc{
            .name = "BenchBackbuffer",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::Present,
        });
        const auto sceneColor = graph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "BenchSceneColor",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
        });
        const auto depth = graph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "BenchDepth",
            .format = vke::RenderGraphImageFormat::D32Sfloat,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
        });

        graph.addPass("BenchClearScene", vke::kBasicDynamicClearPassType)
            .setParams(vke::kBasicDynamicClearParamsType,
                       vke::BasicTransferClearParams{.color = {0.02F, 0.08F, 0.10F, 1.0F}})
            .writeColor("target", sceneColor)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.02F, 0.08F, 0.10F, 1.0F});
            });
        graph.addPass("BenchDepthDraw", vke::kBasicRasterDepthTrianglePassType)
            .setParamsType(vke::kBasicRasterDepthTriangleParamsType)
            .writeColor("target", sceneColor)
            .writeDepth("depth", depth);
        graph.addPass("BenchComposite", vke::kBasicRasterFullscreenPassType)
            .setParams(vke::kBasicRasterFullscreenParamsType,
                       vke::BasicFullscreenParams{.tint = {1.0F, 1.0F, 1.0F, 1.0F}})
            .readTexture("source", sceneColor, vke::RenderGraphShaderStage::Fragment)
            .writeColor("target", backbuffer)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/BenchComposite", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setVec4("Tint", std::array{1.0F, 1.0F, 1.0F, 1.0F})
                    .drawFullscreenTriangle();
            });

        return graph;
    }

    [[nodiscard]] std::uint64_t
    renderGraphTransitionCount(const vke::RenderGraphCompileResult& compiled) {
        auto count = static_cast<std::uint64_t>(compiled.finalTransitions.size());
        count += static_cast<std::uint64_t>(compiled.finalBufferTransitions.size());
        for (const vke::RenderGraphCompiledPass& pass : compiled.passes) {
            count += static_cast<std::uint64_t>(pass.transitionsBefore.size());
            count += static_cast<std::uint64_t>(pass.bufferTransitionsBefore.size());
        }
        return count;
    }

    void addRenderGraphBenchCounters(vke::FrameProfiler& profiler,
                                     const vke::RenderGraphCompileResult& compiled) {
        profiler.addCounter("declaredPassCount",
                            static_cast<std::uint64_t>(compiled.declaredPassCount));
        profiler.addCounter("declaredImageCount",
                            static_cast<std::uint64_t>(compiled.declaredImageCount));
        profiler.addCounter("declaredBufferCount",
                            static_cast<std::uint64_t>(compiled.declaredBufferCount));
        profiler.addCounter("compiledPassCount",
                            static_cast<std::uint64_t>(compiled.passes.size()));
        profiler.addCounter("dependencyEdgeCount",
                            static_cast<std::uint64_t>(compiled.dependencies.size()));
        profiler.addCounter("transitionCount", renderGraphTransitionCount(compiled));
        profiler.addCounter("culledPassCount",
                            static_cast<std::uint64_t>(compiled.culledPasses.size()));
        profiler.addCounter("transientImageCount",
                            static_cast<std::uint64_t>(compiled.transientImages.size()));
        profiler.addCounter("transientBufferCount",
                            static_cast<std::uint64_t>(compiled.transientBuffers.size()));
    }

    [[nodiscard]] std::optional<double> cpuScopeMilliseconds(const vke::FrameProfile& frame,
                                                             std::string_view scopeName) {
        for (const vke::CpuScopeSample& scope : frame.cpuScopes) {
            if (scope.name == scopeName && scope.endNanoseconds >= scope.beginNanoseconds) {
                const std::uint64_t elapsed = scope.endNanoseconds - scope.beginNanoseconds;
                return static_cast<double>(elapsed) / 1'000'000.0;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderGraphBenchStats makeBenchStats(std::vector<double> values) {
        if (values.empty()) {
            return {};
        }

        const double average =
            std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        std::ranges::sort(values);
        const auto percentile = [&values](double value) {
            const auto index =
                static_cast<std::size_t>((static_cast<double>(values.size() - 1U) * value));
            return values[index];
        };

        return RenderGraphBenchStats{
            .averageMilliseconds = average,
            .p50Milliseconds = percentile(0.50),
            .p95Milliseconds = percentile(0.95),
            .maxMilliseconds = values.back(),
        };
    }

    void writeBenchSummary(std::ostream& output, const RenderGraphBenchOptions& options,
                           const RenderGraphBenchStats& recordStats,
                           const RenderGraphBenchStats& compileStats,
                           const RenderGraphBenchStats& totalStats,
                           const vke::FrameProfile& lastFrame) {
        output << R"({"type":"summary","warmupFrames":)" << options.warmupFrames
               << R"(,"measuredFrames":)" << options.measuredFrames << R"(,"outputPath":)";
        vke::writeJsonString(output, options.outputPath.generic_string());
        output << R"(,"recordGraph":{"averageMilliseconds":)" << recordStats.averageMilliseconds
               << R"(,"p50Milliseconds":)" << recordStats.p50Milliseconds
               << R"(,"p95Milliseconds":)" << recordStats.p95Milliseconds
               << R"(,"maxMilliseconds":)" << recordStats.maxMilliseconds << '}';
        output << R"(,"compileGraph":{"averageMilliseconds":)" << compileStats.averageMilliseconds
               << R"(,"p50Milliseconds":)" << compileStats.p50Milliseconds
               << R"(,"p95Milliseconds":)" << compileStats.p95Milliseconds
               << R"(,"maxMilliseconds":)" << compileStats.maxMilliseconds << '}';
        output << R"(,"total":{"averageMilliseconds":)" << totalStats.averageMilliseconds
               << R"(,"p50Milliseconds":)" << totalStats.p50Milliseconds << R"(,"p95Milliseconds":)"
               << totalStats.p95Milliseconds << R"(,"maxMilliseconds":)"
               << totalStats.maxMilliseconds << '}';
        output << R"(,"lastCounters":{)";
        for (std::size_t index = 0; index < lastFrame.counters.size(); ++index) {
            const vke::CounterSample& counter = lastFrame.counters[index];
            if (index > 0) {
                output << ',';
            }
            vke::writeJsonString(output, counter.name);
            output << ':' << counter.value;
        }
        output << "}}\n";
    }

    int runBenchRenderGraph(std::span<char*> args) {
        const std::optional<RenderGraphBenchOptions> parsedOptions =
            parseRenderGraphBenchOptions(args);
        if (!parsedOptions) {
            return EXIT_FAILURE;
        }

        const RenderGraphBenchOptions& options = *parsedOptions;
        const vke::RenderGraphSchemaRegistry schemas = vke::basicRenderGraphSchemaRegistry();
        for (std::size_t frame = 0; frame < options.warmupFrames; ++frame) {
            vke::RenderGraph graph = createBenchRenderGraph();
            auto compiled = graph.compile(schemas);
            if (!compiled) {
                vke::logError(compiled.error().message);
                return EXIT_FAILURE;
            }
        }

        vke::FrameProfiler profiler{options.measuredFrames};
        std::vector<double> recordMilliseconds;
        std::vector<double> compileMilliseconds;
        std::vector<double> totalMilliseconds;
        recordMilliseconds.reserve(options.measuredFrames);
        compileMilliseconds.reserve(options.measuredFrames);
        totalMilliseconds.reserve(options.measuredFrames);

        for (std::size_t frame = 0; frame < options.measuredFrames; ++frame) {
            profiler.beginFrame(vke::FrameProfileInfo{
                .frameIndex = static_cast<std::uint64_t>(frame),
                .target = vke::ProfileTarget::Bench,
                .viewName = "RenderGraphBench",
            });

            vke::RenderGraph graph;
            const vke::CpuScopeHandle recordScope = profiler.beginCpuScope("RecordGraph");
            graph = createBenchRenderGraph();
            profiler.endCpuScope(recordScope);

            const vke::CpuScopeHandle compileScope = profiler.beginCpuScope("CompileGraph");
            auto compiled = graph.compile(schemas);
            profiler.endCpuScope(compileScope);
            if (!compiled) {
                vke::logError(compiled.error().message);
                return EXIT_FAILURE;
            }

            addRenderGraphBenchCounters(profiler, *compiled);
            profiler.endFrame();

            const vke::FrameProfile& profile = profiler.lastFrame();
            const double recordMs = cpuScopeMilliseconds(profile, "RecordGraph").value_or(0.0);
            const double compileMs = cpuScopeMilliseconds(profile, "CompileGraph").value_or(0.0);
            recordMilliseconds.push_back(recordMs);
            compileMilliseconds.push_back(compileMs);
            totalMilliseconds.push_back(recordMs + compileMs);
        }

        const std::filesystem::path parentPath = options.outputPath.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath);
        }

        std::ofstream output{options.outputPath};
        if (!output) {
            vke::logError("Failed to open render graph benchmark output file.");
            return EXIT_FAILURE;
        }

        const RenderGraphBenchStats recordStats = makeBenchStats(recordMilliseconds);
        const RenderGraphBenchStats compileStats = makeBenchStats(compileMilliseconds);
        const RenderGraphBenchStats totalStats = makeBenchStats(totalMilliseconds);
        writeBenchSummary(output, options, recordStats, compileStats, totalStats,
                          profiler.lastFrame());
        vke::writeFrameProfileJsonl(output, profiler.frames());

        std::cout << "RenderGraph bench frames: " << options.measuredFrames
                  << ", record avg ms: " << recordStats.averageMilliseconds
                  << ", compile avg ms: " << compileStats.averageMilliseconds
                  << ", total p95 ms: " << totalStats.p95Milliseconds
                  << ", output: " << options.outputPath.generic_string() << '\n';

        return EXIT_SUCCESS;
    }

    int runSmokeDeferredDeletion() {
        vke::VulkanDeferredDeletionQueue queue;
        std::vector<int> retired;

        if (queue.enqueue(0, {})) {
            vke::logError("Deferred deletion queue accepted an empty callback.");
            return EXIT_FAILURE;
        }

        const bool enqueued = queue.enqueue(2, [&retired]() { retired.push_back(2); }) &&
                              queue.enqueue(1, [&retired]() { retired.push_back(1); }) &&
                              queue.enqueue(3, [&retired]() { retired.push_back(3); });
        if (!enqueued) {
            vke::logError("Deferred deletion queue rejected a valid callback.");
            return EXIT_FAILURE;
        }

        vke::VulkanDeferredDeletionStats stats = queue.stats();
        if (stats.pending != 3 || stats.enqueued != 3 || stats.retired != 0 || stats.flushed != 0 ||
            queue.pendingCount() != 3 || queue.empty()) {
            vke::logError("Deferred deletion queue reported unexpected initial counters.");
            return EXIT_FAILURE;
        }

        if (queue.retireCompleted(1) != 1 || retired != std::vector<int>{1}) {
            vke::logError("Deferred deletion queue retired the wrong epoch 1 callbacks.");
            return EXIT_FAILURE;
        }

        if (!queue.enqueue(2, [&retired]() { retired.push_back(22); })) {
            vke::logError("Deferred deletion queue rejected a valid late callback.");
            return EXIT_FAILURE;
        }

        if (queue.retireCompleted(2) != 2 || retired != std::vector<int>{1, 2, 22}) {
            vke::logError("Deferred deletion queue retired the wrong epoch 2 callbacks.");
            return EXIT_FAILURE;
        }

        stats = queue.stats();
        if (stats.pending != 1 || stats.enqueued != 4 || stats.retired != 3 || stats.flushed != 0) {
            vke::logError("Deferred deletion queue reported unexpected post-retire counters.");
            return EXIT_FAILURE;
        }

        if (queue.flush() != 1 || retired != std::vector<int>{1, 2, 22, 3}) {
            vke::logError("Deferred deletion queue flush retired the wrong callbacks.");
            return EXIT_FAILURE;
        }

        stats = queue.stats();
        if (stats.pending != 0 || stats.enqueued != 4 || stats.retired != 4 || stats.flushed != 1 ||
            !queue.empty()) {
            vke::logError("Deferred deletion queue reported unexpected final counters.");
            return EXIT_FAILURE;
        }

        std::cout << "Deferred deletion queue enqueued: " << stats.enqueued
                  << ", retired: " << stats.retired << ", flushed: " << stats.flushed << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeWindow() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        vke::logInfo("GLFW smoke window created.");
        vke::GlfwWindow::pollEvents();
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeVulkan() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Vulkan Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc desc{
            .applicationName = "VkEngine Vulkan Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .enableValidation = false,
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(desc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        const auto& info = context->deviceInfo();
        std::cout << "Vulkan device: " << info.name << " API "
                  << vke::vulkanVersionString(info.apiVersion) << '\n';
        return EXIT_SUCCESS;
    }

    int runSmokeFrame(const vke::VulkanFrameRecordCallback& record, std::string_view title,
                      VkClearColorValue clearColor) {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = std::string{title}});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = std::string{title},
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(*context, vke::VulkanFrameLoopDesc{
                                                                    .width = framebuffer.width,
                                                                    .height = framebuffer.height,
                                                                    .clearColor = clearColor,
                                                                });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(record);
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during frame smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), title)) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), title)) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeFrame() {
        return runSmokeFrame(vke::recordBasicClearFrame, "VkEngine Frame Smoke",
                             VkClearColorValue{{0.02F, 0.12F, 0.18F, 1.0F}});
    }

    int runSmokeDynamicRendering() {
        return runSmokeFrame(vke::recordBasicDynamicClearFrame, "VkEngine Dynamic Rendering Smoke",
                             VkClearColorValue{{0.18F, 0.06F, 0.14F, 1.0F}});
    }

    int runSmokeTransient() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(
            *glfw, vke::WindowDesc{.title = "VkEngine Transient Image Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Transient Image Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::BasicTransientFrameRecorder recorder{context->device(), context->allocator()};

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.08F, 0.14F, 0.22F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanFrameRecordCallback record =
            [&recorder](const vke::VulkanFrameRecordContext& frame) {
                return recorder.record(frame);
            };

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(record);
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during transient smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const vke::VulkanDeferredDeletionStats deletionStats = frameLoop->deferredDeletionStats();
        if (deletionStats.enqueued == 0 || deletionStats.retired == 0) {
            vke::logError("Transient smoke did not retire deferred Vulkan resource destruction.");
            return EXIT_FAILURE;
        }
        const vke::VulkanTransientImagePoolStats transientPoolStats = recorder.transientPoolStats();
        if (transientPoolStats.created == 0 || transientPoolStats.reused == 0 ||
            transientPoolStats.retired == 0) {
            vke::logError("Transient smoke did not reuse a retired transient Vulkan image.");
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Transient smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "Transient smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered transient frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeResize() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Resize Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Resize Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.06F, 0.10F, 0.18F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        auto firstFrame = frameLoop->renderFrame(vke::recordBasicDynamicClearFrame);
        if (!firstFrame) {
            vke::logError(firstFrame.error().message);
            return EXIT_FAILURE;
        }
        if (*firstFrame == vke::VulkanFrameStatus::OutOfDate) {
            vke::logError("Initial resize smoke frame was unexpectedly out of date.");
            return EXIT_FAILURE;
        }

        frameLoop->setTargetExtent(0, 0);
        auto zeroExtent = frameLoop->recreate();
        if (!zeroExtent) {
            vke::logError(zeroExtent.error().message);
            return EXIT_FAILURE;
        }
        if (*zeroExtent != vke::VulkanFrameStatus::OutOfDate) {
            vke::logError("Zero-sized resize smoke did not report OutOfDate.");
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto restoredFramebuffer = window->framebufferExtent();
        frameLoop->setTargetExtent(restoredFramebuffer.width, restoredFramebuffer.height);
        auto recreated = frameLoop->recreate();
        if (!recreated) {
            vke::logError(recreated.error().message);
            return EXIT_FAILURE;
        }
        if (*recreated != vke::VulkanFrameStatus::Recreated) {
            vke::logError("Resize smoke did not recreate the swapchain after extent restore.");
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(vke::recordBasicDynamicClearFrame);
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during resize smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Resize smoke frames: " << extent.width << 'x' << extent.height << '\n';
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeTriangle(bool useDepth = false,
                         vke::BasicMeshKind meshKind = vke::BasicMeshKind::Triangle) {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        const std::string_view title = triangleSmokeTitle(useDepth, meshKind);
        auto window = vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = std::string{title}});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = std::string{title},
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto triangleRenderer = vke::BasicTriangleRenderer::create(vke::BasicTriangleRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
            .meshKind = meshKind,
        });
        if (!triangleRenderer) {
            vke::logError(triangleRenderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&triangleRenderer, useDepth](const vke::VulkanFrameRecordContext& recordContext) {
                    if (useDepth) {
                        return triangleRenderer->recordFrameWithDepth(recordContext);
                    }
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError(std::string{triangleSmokeOutOfDateMessage(useDepth, meshKind)});
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(triangleRenderer->pipelineCacheStats(), title)) {
            return EXIT_FAILURE;
        }
        const std::uint64_t expectedBuffers =
            meshKind == vke::BasicMeshKind::IndexedQuad ? 2ULL : 1ULL;
        if (!validateBufferUploadStats(triangleRenderer->bufferStats(), expectedBuffers, title)) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), title)) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), title)) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << triangleSmokeRenderedPrefix(useDepth, meshKind) << extent.width << 'x'
                  << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before triangle pipeline teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeDescriptorLayout() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(
            *glfw, vke::WindowDesc{.title = "VkEngine Descriptor Layout Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Descriptor Layout Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto validated =
            vke::validateBasicDescriptorLayoutSmoke(vke::BasicDescriptorLayoutSmokeDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!validated) {
            vke::logError(validated.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Descriptor layout smoke: set 0 bindings 0-2 buffer/image/sampler allocated\n";
        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeMesh3D() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Mesh 3D Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Mesh 3D Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto meshRenderer = vke::BasicMesh3DRenderer::create(vke::BasicMesh3DRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
        });
        if (!meshRenderer) {
            vke::logError(meshRenderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&meshRenderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return meshRenderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during mesh 3D smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(meshRenderer->pipelineCacheStats(), "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(meshRenderer->bufferStats(), 2, "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "Mesh 3D smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered mesh 3D frames: " << extent.width << 'x' << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before mesh 3D teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeDrawList() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Draw List Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Draw List Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.010F, 0.012F, 0.018F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        constexpr auto drawItems = vke::basicDrawListSmokeItems();
        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = vke::BasicDrawListRenderer::create(vke::BasicDrawListRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
            .drawItems = drawItems,
        });
        if (!renderer) {
            vke::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&renderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during draw list smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(), "Draw list smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 2, "Draw list smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Draw list smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(), "Draw list smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered draw list frames: " << extent.width << 'x' << extent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before draw list teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeFullscreenTexture() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(
            *glfw, vke::WindowDesc{.title = "VkEngine Fullscreen Texture Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Fullscreen Texture Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer =
            vke::BasicFullscreenTextureRenderer::create(vke::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            vke::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        for (int frame = 0; frame < 3; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);

            auto status = frameLoop->renderFrame(
                [&renderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during fullscreen texture smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(),
                                        "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDescriptorAllocatorStats(renderer->descriptorAllocatorStats(),
                                              "Fullscreen texture smoke", 2)) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 1, "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(),
                                    "Fullscreen texture smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D extent = frameLoop->extent();
        std::cout << "Rendered fullscreen texture frames: " << extent.width << 'x' << extent.height
                  << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before fullscreen texture teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runSmokeOffscreenViewport() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(
            *glfw, vke::WindowDesc{.title = "VkEngine Offscreen Viewport Smoke"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Offscreen Viewport Smoke",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
            .debugLabels = kSmokeDebugLabels,
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        const auto framebuffer = window->framebufferExtent();
        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.0F, 0.0F, 0.0F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer =
            vke::BasicFullscreenTextureRenderer::create(vke::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            vke::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        VkExtent2D lastViewportExtent{};
        for (int frame = 0; frame < 4; ++frame) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
            VkExtent2D viewportExtent{
                .width = currentFramebuffer.width,
                .height = currentFramebuffer.height,
            };
            if (frame >= 2) {
                viewportExtent.width = std::max(1U, currentFramebuffer.width / 2U);
                viewportExtent.height = std::max(1U, currentFramebuffer.height / 2U);
            }
            lastViewportExtent = viewportExtent;

            auto status = frameLoop->renderFrame(
                [&renderer, viewportExtent](const vke::VulkanFrameRecordContext& recordContext) {
                    return renderer->recordOffscreenViewportFrame(recordContext, viewportExtent);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }
            if (*status == vke::VulkanFrameStatus::OutOfDate) {
                vke::logError("Swapchain remained out of date during offscreen viewport smoke.");
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
        }

        if (!validatePipelineCacheStats(renderer->pipelineCacheStats(),
                                        "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateOffscreenViewportStats(renderer->offscreenViewportStats(),
                                            "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateOffscreenViewportTarget(renderer->offscreenViewportTarget(),
                                             frameLoop->format(), lastViewportExtent,
                                             "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        const vke::VulkanDeferredDeletionStats deletionStats = frameLoop->deferredDeletionStats();
        if (deletionStats.enqueued < 2 || deletionStats.retired < 2) {
            vke::logError("Offscreen viewport smoke did not retire resized viewport resources.");
            return EXIT_FAILURE;
        }
        if (!validateDescriptorAllocatorStats(renderer->descriptorAllocatorStats(),
                                              "Offscreen viewport smoke", 2)) {
            return EXIT_FAILURE;
        }
        if (!validateBufferUploadStats(renderer->bufferStats(), 1, "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateDebugLabelStats(frameLoop->debugLabelStats(), "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }
        if (!validateTimestampStats(frameLoop->timestampStats(),
                                    frameLoop->latestTimestampTimings(),
                                    "Offscreen viewport smoke")) {
            return EXIT_FAILURE;
        }

        const VkExtent2D swapchainExtent = frameLoop->extent();
        std::cout << "Rendered offscreen viewport frames: " << lastViewportExtent.width << 'x'
                  << lastViewportExtent.height << " inside swapchain " << swapchainExtent.width
                  << 'x' << swapchainExtent.height << '\n';
        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before offscreen viewport teardown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        window->requestClose();
        return EXIT_SUCCESS;
    }

    int runInteractiveViewer() {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window =
            vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Sample Viewer"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Sample Viewer",
            .requiredInstanceExtensions = *extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(*window, instance);
                },
        };

        auto context = vke::VulkanContext::create(contextDesc);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        auto framebuffer = window->framebufferExtent();
        while (!window->shouldClose() && !isRenderableExtent(framebuffer)) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            vke::GlfwWindow::pollEvents();
            framebuffer = window->framebufferExtent();
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = vke::VulkanFrameLoop::create(
            *context, vke::VulkanFrameLoopDesc{
                          .width = framebuffer.width,
                          .height = framebuffer.height,
                          .clearColor = VkClearColorValue{{0.015F, 0.02F, 0.025F, 1.0F}},
                      });
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto triangleRenderer = vke::BasicTriangleRenderer::create(vke::BasicTriangleRendererDesc{
            .device = context->device(),
            .allocator = context->allocator(),
            .shaderDirectory = shaderDir,
        });
        if (!triangleRenderer) {
            vke::logError(triangleRenderer.error().message);
            return EXIT_FAILURE;
        }

        while (!window->shouldClose()) {
            vke::GlfwWindow::pollEvents();
            const auto currentFramebuffer = window->framebufferExtent();
            frameLoop->setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
            if (!isRenderableExtent(currentFramebuffer)) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                continue;
            }

            if (!extentMatches(frameLoop->extent(), currentFramebuffer)) {
                auto recreated = frameLoop->recreate();
                if (!recreated) {
                    vke::logError(recreated.error().message);
                    return EXIT_FAILURE;
                }
                if (*recreated == vke::VulkanFrameStatus::OutOfDate) {
                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(16ms);
                    continue;
                }
            }

            auto status = frameLoop->renderFrame(
                [&triangleRenderer](const vke::VulkanFrameRecordContext& recordContext) {
                    return triangleRenderer->recordFrame(recordContext);
                });
            if (!status) {
                vke::logError(status.error().message);
                return EXIT_FAILURE;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1ms);
        }

        const VkResult idleResult = vkQueueWaitIdle(context->graphicsQueue());
        if (idleResult != VK_SUCCESS) {
            vke::logError("Failed to wait for Vulkan queue before viewer shutdown: " +
                          vke::vkResultName(idleResult));
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    bool validateSmokeRenderGraphVulkanMappings(const vke::RenderGraphCompileResult& compiled) {
        const auto vulkanFinalTransition =
            vke::vulkanImageTransition(compiled.finalTransitions.front());
        if (vulkanFinalTransition.oldLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanFinalTransition.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            vke::logError("Render graph Vulkan transition mapping produced unexpected layouts.");
            return false;
        }

        const VkImageMemoryBarrier2 barrier =
            vke::vulkanImageBarrier(vulkanFinalTransition, VK_NULL_HANDLE);
        if (barrier.oldLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            barrier.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
            barrier.srcStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            barrier.srcAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError("Render graph Vulkan barrier mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanShaderReadTransition =
            vke::vulkanImageTransition(compiled.passes[1].transitionsBefore.front());
        if (vulkanShaderReadTransition.oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
            vulkanShaderReadTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanShaderReadTransition.srcStageMask != VK_PIPELINE_STAGE_2_TRANSFER_BIT ||
            vulkanShaderReadTransition.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
            vulkanShaderReadTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanShaderReadTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError("Render graph Vulkan shader-read mapping produced unexpected masks.");
            return false;
        }

        const VkPipelineStageFlags2 depthTestsStages =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        const auto vulkanDepthWriteTransition =
            vke::vulkanImageTransition(compiled.passes[2].transitionsBefore.front());
        if (vulkanDepthWriteTransition.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
            vulkanDepthWriteTransition.newLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
            vulkanDepthWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanDepthWriteTransition.srcAccessMask != 0 ||
            vulkanDepthWriteTransition.dstStageMask != depthTestsStages ||
            vulkanDepthWriteTransition.dstAccessMask !=
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
            vke::logError("Render graph Vulkan depth-write mapping produced unexpected masks.");
            return false;
        }

        const VkImageMemoryBarrier2 depthBarrier = vke::vulkanImageBarrier(
            vulkanDepthWriteTransition, VK_NULL_HANDLE, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (depthBarrier.subresourceRange.aspectMask != VK_IMAGE_ASPECT_DEPTH_BIT) {
            vke::logError("Render graph Vulkan depth barrier used an unexpected aspect mask.");
            return false;
        }

        const auto vulkanDepthSampledTransition =
            vke::vulkanImageTransition(compiled.passes[3].transitionsBefore.front());
        if (vulkanDepthSampledTransition.oldLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
            vulkanDepthSampledTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanDepthSampledTransition.srcStageMask != depthTestsStages ||
            vulkanDepthSampledTransition.srcAccessMask !=
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ||
            vulkanDepthSampledTransition.dstStageMask != VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanDepthSampledTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError(
                "Render graph Vulkan depth sampled-read mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanTransientWriteTransition =
            vke::vulkanImageTransition(compiled.passes[4].transitionsBefore.front());
        if (vulkanTransientWriteTransition.oldLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
            vulkanTransientWriteTransition.newLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
            vulkanTransientWriteTransition.srcStageMask != VK_PIPELINE_STAGE_2_NONE ||
            vulkanTransientWriteTransition.srcAccessMask != 0 ||
            vulkanTransientWriteTransition.dstStageMask !=
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT ||
            vulkanTransientWriteTransition.dstAccessMask !=
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
            vke::logError("Render graph Vulkan transient write mapping produced unexpected masks.");
            return false;
        }

        const auto vulkanTransientSampleTransition =
            vke::vulkanImageTransition(compiled.passes[5].transitionsBefore.front());
        if (vulkanTransientSampleTransition.oldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
            vulkanTransientSampleTransition.newLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
            vulkanTransientSampleTransition.srcStageMask !=
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT ||
            vulkanTransientSampleTransition.srcAccessMask !=
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT ||
            vulkanTransientSampleTransition.dstStageMask !=
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
            vulkanTransientSampleTransition.dstAccessMask != VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
            vke::logError(
                "Render graph Vulkan transient sampled-read mapping produced unexpected masks.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphTransientPlan(const vke::RenderGraphCompileResult& compiled) {
        if (compiled.transientImages.size() != 1) {
            vke::logError("Render graph did not produce the expected transient allocation plan.");
            return false;
        }

        const vke::RenderGraphTransientImageAllocation& transient =
            compiled.transientImages.front();
        if (transient.image.index != 2 || transient.imageName != "TransientColor" ||
            transient.format != vke::RenderGraphImageFormat::B8G8R8A8Srgb ||
            transient.extent.width != 640 || transient.extent.height != 360 ||
            transient.firstPassIndex != 4 || transient.lastPassIndex != 5 ||
            transient.finalState != vke::RenderGraphImageState::ShaderRead ||
            transient.finalShaderStage != vke::RenderGraphShaderStage::Fragment) {
            vke::logError("Render graph transient allocation plan contained unexpected fields.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphCommands(const vke::RenderGraphCompileResult& compiled) {
        if (compiled.passes.size() != 6) {
            vke::logError("Render graph command smoke received an unexpected pass count.");
            return false;
        }

        const auto& clearCommands = compiled.passes[0].commands;
        if (clearCommands.size() != 1 ||
            clearCommands.front().kind != vke::RenderGraphCommandKind::ClearColor ||
            clearCommands.front().name != "target") {
            vke::logError("Render graph clear command summary contained unexpected fields.");
            return false;
        }

        const auto& sampleCommands = compiled.passes[1].commands;
        if (sampleCommands.size() != 4 ||
            sampleCommands[0].kind != vke::RenderGraphCommandKind::SetShader ||
            sampleCommands[0].name != "Hidden/SmokeSample" ||
            sampleCommands[1].kind != vke::RenderGraphCommandKind::SetTexture ||
            sampleCommands[1].secondaryName != "source" ||
            sampleCommands[2].kind != vke::RenderGraphCommandKind::SetFloat ||
            sampleCommands[3].kind != vke::RenderGraphCommandKind::DrawFullscreenTriangle) {
            vke::logError("Render graph sample command summary contained unexpected fields.");
            return false;
        }

        const auto& transientCommands = compiled.passes[5].commands;
        if (transientCommands.size() != 4 ||
            transientCommands[0].kind != vke::RenderGraphCommandKind::SetShader ||
            transientCommands[1].kind != vke::RenderGraphCommandKind::SetTexture ||
            transientCommands[2].kind != vke::RenderGraphCommandKind::SetVec4 ||
            transientCommands[3].kind != vke::RenderGraphCommandKind::DrawFullscreenTriangle) {
            vke::logError("Render graph transient command summary contained unexpected fields.");
            return false;
        }

        return true;
    }

    bool hasNoDepthSlots(vke::RenderGraphPassContext context) {
        return context.depthReads.empty() && context.depthWrites.empty() &&
               context.depthSampledReads.empty() && context.depthReadSlots.empty() &&
               context.depthWriteSlots.empty() && context.depthSampledReadSlots.empty();
    }

    vke::Result<void> validateClearTransferContext(vke::RenderGraphPassContext context) {
        if (context.name != "ClearColor" || context.type != "basic.clear-transfer" ||
            context.paramsType != "basic.clear-transfer.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !hasNoDepthSlots(context) ||
            context.transferWrites.size() != 1 || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || context.transferWriteSlots.size() != 1 ||
            context.transferWriteSlots.front().name != "target") {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateSampleFragmentContext(vke::RenderGraphPassContext context) {
        if (context.name != "SampleColor" || context.type != "basic.sample-fragment" ||
            context.paramsType != "basic.sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            context.shaderReads.size() != 1 || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            context.shaderReadSlots.size() != 1 || !context.transferWriteSlots.empty() ||
            context.shaderReadSlots.front().name != "source" ||
            context.shaderReadSlots.front().shaderStage != vke::RenderGraphShaderStage::Fragment) {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph shader-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateDepthWriteContext(vke::RenderGraphPassContext context) {
        if (context.name != "WriteDepth" || context.type != "basic.depth-write" ||
            context.paramsType != "basic.depth-write.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !context.depthReads.empty() ||
            context.depthWrites.size() != 1 || !context.depthSampledReads.empty() ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || !context.depthReadSlots.empty() ||
            context.depthWriteSlots.size() != 1 || !context.depthSampledReadSlots.empty() ||
            !context.transferWriteSlots.empty() ||
            context.depthWriteSlots.front().name != "depth") {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph depth-write executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateDepthSampleContext(vke::RenderGraphPassContext context) {
        if (context.name != "SampleDepth" || context.type != "basic.depth-sample-fragment" ||
            context.paramsType != "basic.depth-sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            !context.shaderReads.empty() || !context.depthReads.empty() ||
            !context.depthWrites.empty() || context.depthSampledReads.size() != 1 ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            !context.shaderReadSlots.empty() || !context.depthReadSlots.empty() ||
            !context.depthWriteSlots.empty() || context.depthSampledReadSlots.size() != 1 ||
            !context.transferWriteSlots.empty() ||
            context.depthSampledReadSlots.front().name != "depth" ||
            context.depthSampledReadSlots.front().shaderStage !=
                vke::RenderGraphShaderStage::Fragment) {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph depth sampled-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateTransientWriteContext(vke::RenderGraphPassContext context) {
        if (context.name != "WriteTransientColor" || context.type != "basic.transient-color" ||
            context.paramsType != "basic.transient-color.params" ||
            context.transitionsBefore.size() != 1 || context.colorWrites.size() != 1 ||
            !context.shaderReads.empty() || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || context.colorWriteSlots.size() != 1 ||
            !context.shaderReadSlots.empty() || !context.transferWriteSlots.empty() ||
            context.colorWriteSlots.front().name != "target") {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph transient write executor received unexpected pass context.",
            }};
        }

        return {};
    }

    vke::Result<void> validateTransientSampleContext(vke::RenderGraphPassContext context) {
        if (context.name != "SampleTransientColor" ||
            context.type != "basic.transient-sample-fragment" ||
            context.paramsType != "basic.transient-sample-fragment.params" ||
            context.transitionsBefore.size() != 1 || !context.colorWrites.empty() ||
            context.shaderReads.size() != 1 || !hasNoDepthSlots(context) ||
            !context.transferWrites.empty() || !context.colorWriteSlots.empty() ||
            context.shaderReadSlots.size() != 1 || !context.transferWriteSlots.empty() ||
            context.shaderReadSlots.front().name != "source" ||
            context.shaderReadSlots.front().shaderStage != vke::RenderGraphShaderStage::Fragment) {
            return std::unexpected{vke::Error{
                vke::ErrorDomain::RenderGraph,
                0,
                "Render graph transient sampled-read executor received unexpected pass context.",
            }};
        }

        return {};
    }

    void registerSmokeRenderGraphExecutors(vke::RenderGraphExecutorRegistry& executors,
                                           int& callbackCount) {
        executors.registerExecutor(
            "basic.clear-transfer",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateClearTransferContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.sample-fragment",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateSampleFragmentContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.depth-write",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateDepthWriteContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.depth-sample-fragment",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateDepthSampleContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.transient-color",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateTransientWriteContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
        executors.registerExecutor(
            "basic.transient-sample-fragment",
            [&callbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                auto validated = validateTransientSampleContext(context);
                if (!validated) {
                    return validated;
                }
                ++callbackCount;
                return {};
            });
    }

    struct ExpectedRenderGraphCompileFailure {
        std::string_view message;
        std::string_view context;
    };

    bool expectRenderGraphCompileFailure(const vke::Result<vke::RenderGraphCompileResult>& compiled,
                                         ExpectedRenderGraphCompileFailure expected) {
        if (compiled) {
            vke::logError("Render graph accepted invalid graph: " + std::string{expected.context});
            return false;
        }
        if (compiled.error().message.find(expected.message) == std::string::npos) {
            vke::logError("Render graph produced an unexpected error for " +
                          std::string{expected.context} + ": " + compiled.error().message);
            return false;
        }

        return true;
    }

    enum class BuiltinSchemaSmokePass : std::uint8_t {
        TransferClear,
        DynamicClear,
        TransientPresent,
        RasterTriangle,
        RasterDepthTriangle,
        RasterMesh3D,
        RasterFullscreen,
        RasterDrawList,
    };

    struct BuiltinSchemaSmokeCase {
        BuiltinSchemaSmokePass pass;
        std::string_view type;
        std::string_view paramsType;
        std::string_view missingSlot;
        std::string_view context;
    };

    struct BuiltinSchemaSmokeImages {
        vke::RenderGraphImageHandle colorTarget{};
        vke::RenderGraphImageHandle colorSource{};
        vke::RenderGraphImageHandle depthTarget{};
        vke::RenderGraphImageHandle unexpectedTarget{};
    };

    struct BuiltinSchemaSmokeCompileOptions {
        std::string_view paramsType;
        std::string_view omittedSlot;
        bool addUnexpectedSlot{};
    };

    BuiltinSchemaSmokeImages createBuiltinSchemaSmokeImages(vke::RenderGraph& graph) {
        return BuiltinSchemaSmokeImages{
            .colorTarget = graph.importImage(vke::RenderGraphImageDesc{
                .name = "BuiltinSchemaColorTarget",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = vke::RenderGraphImageState::Undefined,
                .finalState = vke::RenderGraphImageState::Present,
            }),
            .colorSource = graph.importImage(vke::RenderGraphImageDesc{
                .name = "BuiltinSchemaColorSource",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = vke::RenderGraphImageState::ShaderRead,
                .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
                .finalState = vke::RenderGraphImageState::ShaderRead,
                .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
            }),
            .depthTarget = graph.importImage(vke::RenderGraphImageDesc{
                .name = "BuiltinSchemaDepthTarget",
                .format = vke::RenderGraphImageFormat::D32Sfloat,
                .extent = vke::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = vke::RenderGraphImageState::Undefined,
                .finalState = vke::RenderGraphImageState::DepthAttachmentWrite,
            }),
            .unexpectedTarget = graph.importImage(vke::RenderGraphImageDesc{
                .name = "BuiltinSchemaUnexpectedTarget",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 16, .height = 16},
                .initialState = vke::RenderGraphImageState::Undefined,
                .finalState = vke::RenderGraphImageState::Present,
            }),
        };
    }

    void addBuiltinSchemaSmokeSlots(BuiltinSchemaSmokePass passKind,
                                    vke::RenderGraph::PassBuilder& pass,
                                    BuiltinSchemaSmokeImages images,
                                    std::string_view omittedSlot = {}) {
        switch (passKind) {
        case BuiltinSchemaSmokePass::TransferClear:
            if (omittedSlot != "target") {
                pass.writeTransfer("target", images.colorTarget);
            }
            break;
        case BuiltinSchemaSmokePass::DynamicClear:
        case BuiltinSchemaSmokePass::RasterTriangle:
            if (omittedSlot != "target") {
                pass.writeColor("target", images.colorTarget);
            }
            break;
        case BuiltinSchemaSmokePass::TransientPresent:
            if (omittedSlot != "source") {
                pass.readTexture("source", images.colorSource,
                                 vke::RenderGraphShaderStage::Fragment);
            }
            if (omittedSlot != "target") {
                pass.writeTransfer("target", images.colorTarget);
            }
            break;
        case BuiltinSchemaSmokePass::RasterDepthTriangle:
        case BuiltinSchemaSmokePass::RasterMesh3D:
        case BuiltinSchemaSmokePass::RasterDrawList:
            if (omittedSlot != "target") {
                pass.writeColor("target", images.colorTarget);
            }
            if (omittedSlot != "depth") {
                pass.writeDepth("depth", images.depthTarget);
            }
            break;
        case BuiltinSchemaSmokePass::RasterFullscreen:
            if (omittedSlot != "source") {
                pass.readTexture("source", images.colorSource,
                                 vke::RenderGraphShaderStage::Fragment);
            }
            if (omittedSlot != "target") {
                pass.writeColor("target", images.colorTarget);
            }
            break;
        }
    }

    vke::Result<vke::RenderGraphCompileResult>
    compileBuiltinSchemaSmokePass(const BuiltinSchemaSmokeCase& testCase,
                                  const vke::RenderGraphSchemaRegistry& schemas,
                                  BuiltinSchemaSmokeCompileOptions options) {
        vke::RenderGraph graph;
        const BuiltinSchemaSmokeImages images = createBuiltinSchemaSmokeImages(graph);
        auto pass = graph.addPass(std::string{testCase.context}, std::string{testCase.type});
        pass.setParamsType(std::string{options.paramsType});
        addBuiltinSchemaSmokeSlots(testCase.pass, pass, images, options.omittedSlot);
        if (options.addUnexpectedSlot) {
            pass.writeTransfer("unexpected", images.unexpectedTarget);
        }

        return graph.compile(schemas);
    }

    bool validateBuiltinSchemaSmokeCase(const vke::RenderGraphSchemaRegistry& builtinSchemas,
                                        const BuiltinSchemaSmokeCase& testCase) {
        if (!expectRenderGraphCompileFailure(
                compileBuiltinSchemaSmokePass(testCase, builtinSchemas,
                                              BuiltinSchemaSmokeCompileOptions{
                                                  .paramsType = testCase.paramsType,
                                                  .omittedSlot = testCase.missingSlot,
                                                  .addUnexpectedSlot = false,
                                              }),
                ExpectedRenderGraphCompileFailure{
                    .message = "is missing required slot",
                    .context = testCase.context,
                })) {
            return false;
        }
        if (!expectRenderGraphCompileFailure(
                compileBuiltinSchemaSmokePass(testCase, builtinSchemas,
                                              BuiltinSchemaSmokeCompileOptions{
                                                  .paramsType = testCase.paramsType,
                                                  .omittedSlot = {},
                                                  .addUnexpectedSlot = true,
                                              }),
                ExpectedRenderGraphCompileFailure{
                    .message = "that is not allowed by schema",
                    .context = testCase.context,
                })) {
            return false;
        }
        if (!expectRenderGraphCompileFailure(
                compileBuiltinSchemaSmokePass(testCase, builtinSchemas,
                                              BuiltinSchemaSmokeCompileOptions{
                                                  .paramsType = "builtin.invalid-params",
                                                  .omittedSlot = {},
                                                  .addUnexpectedSlot = false,
                                              }),
                ExpectedRenderGraphCompileFailure{
                    .message = "expected params type",
                    .context = testCase.context,
                })) {
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphBuiltinSchemaFailures() {
        const vke::RenderGraphSchemaRegistry builtinSchemas = vke::basicRenderGraphSchemaRegistry();
        const std::array builtinCases{
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::TransferClear,
                .type = vke::kBasicTransferClearPassType,
                .paramsType = vke::kBasicTransferClearParamsType,
                .missingSlot = "target",
                .context = "builtin transfer clear",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::DynamicClear,
                .type = vke::kBasicDynamicClearPassType,
                .paramsType = vke::kBasicDynamicClearParamsType,
                .missingSlot = "target",
                .context = "builtin dynamic clear",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::TransientPresent,
                .type = vke::kBasicTransientPresentPassType,
                .paramsType = vke::kBasicTransientPresentParamsType,
                .missingSlot = "source",
                .context = "builtin transient present",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterTriangle,
                .type = vke::kBasicRasterTrianglePassType,
                .paramsType = vke::kBasicRasterTriangleParamsType,
                .missingSlot = "target",
                .context = "builtin raster triangle",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterDepthTriangle,
                .type = vke::kBasicRasterDepthTrianglePassType,
                .paramsType = vke::kBasicRasterDepthTriangleParamsType,
                .missingSlot = "depth",
                .context = "builtin raster depth triangle",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterMesh3D,
                .type = vke::kBasicRasterMesh3DPassType,
                .paramsType = vke::kBasicRasterMesh3DParamsType,
                .missingSlot = "depth",
                .context = "builtin raster mesh3D",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterFullscreen,
                .type = vke::kBasicRasterFullscreenPassType,
                .paramsType = vke::kBasicRasterFullscreenParamsType,
                .missingSlot = "source",
                .context = "builtin raster fullscreen",
            },
            BuiltinSchemaSmokeCase{
                .pass = BuiltinSchemaSmokePass::RasterDrawList,
                .type = vke::kBasicRasterDrawListPassType,
                .paramsType = vke::kBasicRasterDrawListParamsType,
                .missingSlot = "depth",
                .context = "builtin raster draw list",
            },
        };

        return std::ranges::all_of(
            builtinCases, [&builtinSchemas](const BuiltinSchemaSmokeCase& testCase) {
                return validateBuiltinSchemaSmokeCase(builtinSchemas, testCase);
            });
    }

    bool validateSmokeRenderGraphNegativeCompiles(const vke::RenderGraphSchemaRegistry& schemas) {
        vke::RenderGraph missingProducerGraph;
        const auto orphanColor =
            missingProducerGraph.createTransientImage(vke::RenderGraphImageDesc{
                .name = "OrphanTransientColor",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
            });
        int missingProducerCallbackCount = 0;
        missingProducerGraph.addPass("SampleOrphanTransient", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", orphanColor, vke::RenderGraphShaderStage::Fragment)
            .execute([&missingProducerCallbackCount](vke::RenderGraphPassContext) {
                ++missingProducerCallbackCount;
                return vke::Result<void>{};
            });

        auto missingProducerCompiled = missingProducerGraph.compile(schemas);
        if (missingProducerCompiled) {
            vke::logError("Render graph accepted a transient read without a producer.");
            return false;
        }
        if (missingProducerCompiled.error().message.find("before any pass writes it") ==
            std::string::npos) {
            vke::logError("Render graph produced an unexpected missing-producer error: " +
                          missingProducerCompiled.error().message);
            return false;
        }

        auto missingProducerExecuted = missingProducerGraph.execute();
        if (missingProducerExecuted) {
            vke::logError("Render graph executed a transient read without a producer.");
            return false;
        }
        if (missingProducerCallbackCount != 0) {
            vke::logError(
                "Render graph invoked a callback after missing-producer compile failure.");
            return false;
        }

        vke::RenderGraph missingSchemaGraph;
        const auto schemaBackbuffer = missingSchemaGraph.importImage(vke::RenderGraphImageDesc{
            .name = "UnknownSchemaBackbuffer",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::Present,
        });
        int missingSchemaCallbackCount = 0;
        missingSchemaGraph.addPass("UnknownTypedPass", "basic.unknown-pass")
            .setParamsType("basic.unknown-pass.params")
            .writeTransfer("target", schemaBackbuffer)
            .execute([&missingSchemaCallbackCount](vke::RenderGraphPassContext) {
                ++missingSchemaCallbackCount;
                return vke::Result<void>{};
            });

        auto missingSchemaCompiled = missingSchemaGraph.compile(schemas);
        if (missingSchemaCompiled) {
            vke::logError("Render graph accepted a pass type without a registered schema.");
            return false;
        }
        if (missingSchemaCompiled.error().message.find("has no registered schema") ==
            std::string::npos) {
            vke::logError("Render graph produced an unexpected missing-schema error: " +
                          missingSchemaCompiled.error().message);
            return false;
        }
        if (missingSchemaCallbackCount != 0) {
            vke::logError("Render graph invoked a callback during missing-schema compile.");
            return false;
        }

        vke::RenderGraph mixedReadWriteGraph;
        const auto mixedReadWriteImage = mixedReadWriteGraph.importImage(vke::RenderGraphImageDesc{
            .name = "MixedReadWriteImage",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
            .initialState = vke::RenderGraphImageState::ShaderRead,
            .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
            .finalState = vke::RenderGraphImageState::Present,
        });
        int mixedReadWriteCallbackCount = 0;
        mixedReadWriteGraph.addPass("MixedReadWritePass", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", mixedReadWriteImage, vke::RenderGraphShaderStage::Fragment)
            .writeColor("target", mixedReadWriteImage)
            .execute([&mixedReadWriteCallbackCount](vke::RenderGraphPassContext) {
                ++mixedReadWriteCallbackCount;
                return vke::Result<void>{};
            });
        if (!expectRenderGraphCompileFailure(
                mixedReadWriteGraph.compile(schemas),
                ExpectedRenderGraphCompileFailure{
                    .message = "more than once",
                    .context = "same-image shader read and color write",
                })) {
            return false;
        }
        auto mixedReadWriteExecuted = mixedReadWriteGraph.execute();
        if (mixedReadWriteExecuted || mixedReadWriteCallbackCount != 0) {
            vke::logError("Render graph executed a same-image shader read and color write pass.");
            return false;
        }

        vke::RenderGraph mixedColorTransferGraph;
        const auto mixedColorTransferImage =
            mixedColorTransferGraph.importImage(vke::RenderGraphImageDesc{
                .name = "MixedColorTransferImage",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
                .initialState = vke::RenderGraphImageState::Undefined,
                .finalState = vke::RenderGraphImageState::Present,
            });
        mixedColorTransferGraph.addPass("MixedColorTransferPass", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("clearTarget", mixedColorTransferImage)
            .writeColor("colorTarget", mixedColorTransferImage);
        if (!expectRenderGraphCompileFailure(mixedColorTransferGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "more than once",
                                                 .context = "same-image transfer and color write",
                                             })) {
            return false;
        }

        vke::RenderGraph mixedDepthGraph;
        const auto mixedDepthImage = mixedDepthGraph.importImage(vke::RenderGraphImageDesc{
            .name = "MixedDepthImage",
            .format = vke::RenderGraphImageFormat::D32Sfloat,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::DepthSampledRead,
            .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        mixedDepthGraph.addPass("MixedDepthPass", "basic.depth-write")
            .setParamsType("basic.depth-write.params")
            .writeDepth("depthWrite", mixedDepthImage)
            .readDepthTexture("depthSample", mixedDepthImage,
                              vke::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(
                mixedDepthGraph.compile(schemas),
                ExpectedRenderGraphCompileFailure{
                    .message = "more than once",
                    .context = "same-image depth write and sampled read",
                })) {
            return false;
        }

        vke::RenderGraph ambiguousProducerGraph;
        const auto ambiguousProducerImage =
            ambiguousProducerGraph.createTransientImage(vke::RenderGraphImageDesc{
                .name = "AmbiguousProducerImage",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
            });
        ambiguousProducerGraph.addPass("ReadBeforeTwoWriters", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", ambiguousProducerImage, vke::RenderGraphShaderStage::Fragment);
        ambiguousProducerGraph.addPass("FutureWriterA", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", ambiguousProducerImage);
        ambiguousProducerGraph.addPass("FutureWriterB", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", ambiguousProducerImage);
        auto ambiguousProducerCompiled = ambiguousProducerGraph.compile(schemas);
        if (!expectRenderGraphCompileFailure(ambiguousProducerCompiled,
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "Candidate writers",
                                                 .context = "ambiguous producer diagnostics",
                                             })) {
            return false;
        }
        const std::string& ambiguousProducerError = ambiguousProducerCompiled.error().message;
        if (ambiguousProducerError.find("ReadBeforeTwoWriters") == std::string::npos ||
            ambiguousProducerError.find("AmbiguousProducerImage") == std::string::npos ||
            ambiguousProducerError.find("FutureWriterA") == std::string::npos ||
            ambiguousProducerError.find("FutureWriterB") == std::string::npos) {
            vke::logError("Render graph ambiguous producer diagnostic omitted context: " +
                          ambiguousProducerError);
            return false;
        }

        vke::RenderGraph missingFinalStateGraph;
        const auto missingFinalImage = missingFinalStateGraph.importImage(vke::RenderGraphImageDesc{
            .name = "ImportedTextureWithoutFinalState",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
            .initialState = vke::RenderGraphImageState::ShaderRead,
            .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        missingFinalStateGraph.addPass("SampleImportedWithoutFinal", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", missingFinalImage, vke::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(missingFinalStateGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "must declare an explicit final state",
                                                 .context = "imported image without final state",
                                             })) {
            return false;
        }

        vke::RenderGraph explicitFinalStateGraph;
        const auto explicitFinalImage =
            explicitFinalStateGraph.importImage(vke::RenderGraphImageDesc{
                .name = "ExplicitFinalImportedTexture",
                .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
                .initialState = vke::RenderGraphImageState::ShaderRead,
                .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
                .finalState = vke::RenderGraphImageState::ShaderRead,
                .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
            });
        explicitFinalStateGraph.addPass("SampleExplicitFinalImported", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", explicitFinalImage, vke::RenderGraphShaderStage::Fragment);
        auto explicitFinalCompiled = explicitFinalStateGraph.compile(schemas);
        if (!explicitFinalCompiled) {
            vke::logError("Render graph rejected an imported image with explicit final state: " +
                          explicitFinalCompiled.error().message);
            return false;
        }
        if (!explicitFinalCompiled->finalTransitions.empty()) {
            vke::logError(
                "Render graph produced a final transition for an already shader-readable import.");
            return false;
        }

        vke::RenderGraph cycleGraph;
        const auto cycleImageA = cycleGraph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "CycleImageA",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
        });
        const auto cycleImageB = cycleGraph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "CycleImageB",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
        });
        cycleGraph.addPass("CycleFirst", "basic.cycle-read-write")
            .setParamsType("basic.cycle-read-write.params")
            .readTexture("source", cycleImageA, vke::RenderGraphShaderStage::Fragment)
            .writeColor("target", cycleImageB);
        cycleGraph.addPass("CycleSecond", "basic.cycle-read-write")
            .setParamsType("basic.cycle-read-write.params")
            .readTexture("source", cycleImageB, vke::RenderGraphShaderStage::Fragment)
            .writeColor("target", cycleImageA);
        auto cycleCompiled = cycleGraph.compile(schemas);
        if (!expectRenderGraphCompileFailure(cycleCompiled,
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "dependency cycle",
                                                 .context = "cyclic dependency diagnostics",
                                             })) {
            return false;
        }
        const std::string& cycleError = cycleCompiled.error().message;
        if (cycleError.find("CycleFirst") == std::string::npos ||
            cycleError.find("CycleSecond") == std::string::npos ||
            cycleError.find("CycleImageA") == std::string::npos ||
            cycleError.find("Cycle edge") == std::string::npos) {
            vke::logError("Render graph cycle diagnostic omitted pass, image, or edge context: " +
                          cycleError);
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphCulling(const vke::RenderGraphSchemaRegistry& schemas) {
        vke::RenderGraph cullingGraph;
        const auto cullingBackbuffer = cullingGraph.importImage(vke::RenderGraphImageDesc{
            .name = "CullingBackbuffer",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::Present,
        });
        const auto unusedTransient = cullingGraph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "UnusedTransient",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 32, .height = 32},
        });

        int visibleCallbackCount = 0;
        int culledCallbackCount = 0;
        int sideEffectCallbackCount = 0;
        cullingGraph.addPass("VisibleClear", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", cullingBackbuffer)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.0F, 0.0F, 0.0F, 1.0F});
            })
            .execute([&visibleCallbackCount](vke::RenderGraphPassContext) {
                ++visibleCallbackCount;
                return vke::Result<void>{};
            });
        cullingGraph.addPass("WriteUnusedTransient", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", unusedTransient)
            .allowCulling()
            .execute([&culledCallbackCount](vke::RenderGraphPassContext) {
                ++culledCallbackCount;
                return vke::Result<void>{};
            });
        cullingGraph.addPass("SideEffectMarker", "basic.side-effect")
            .setParamsType("basic.side-effect.params")
            .execute([&sideEffectCallbackCount](
                         vke::RenderGraphPassContext context) -> vke::Result<void> {
                if (!context.allowCulling || !context.hasSideEffects) {
                    return std::unexpected{vke::Error{
                        vke::ErrorDomain::RenderGraph,
                        0,
                        "Render graph side-effect context did not preserve culling flags.",
                    }};
                }
                ++sideEffectCallbackCount;
                return vke::Result<void>{};
            });

        auto compiled = cullingGraph.compile(schemas);
        if (!compiled) {
            vke::logError(compiled.error().message);
            return false;
        }
        if (compiled->passes.size() != 2 || compiled->culledPasses.size() != 1 ||
            !compiled->transientImages.empty()) {
            vke::logError("Render graph culling smoke produced an unexpected compile plan.");
            return false;
        }
        if (compiled->culledPasses.front().name != "WriteUnusedTransient" ||
            compiled->culledPasses.front().declarationIndex != 1) {
            vke::logError("Render graph culling smoke culled the wrong pass.");
            return false;
        }
        if (compiled->passes[0].name != "VisibleClear" ||
            compiled->passes[1].name != "SideEffectMarker" || !compiled->passes[1].hasSideEffects) {
            vke::logError("Render graph culling smoke kept the wrong active passes.");
            return false;
        }

        auto executed = cullingGraph.execute(*compiled);
        if (!executed) {
            vke::logError(executed.error().message);
            return false;
        }
        if (visibleCallbackCount != 1 || sideEffectCallbackCount != 1 || culledCallbackCount != 0) {
            vke::logError("Render graph culling smoke invoked unexpected callbacks.");
            return false;
        }

        return true;
    }

    bool validateSmokeRenderGraphBuffers(const vke::RenderGraphSchemaRegistry& schemas) {
        vke::RenderGraph bufferGraph;
        const auto transientBuffer = bufferGraph.createTransientBuffer(vke::RenderGraphBufferDesc{
            .name = "TransientUploadBuffer",
            .byteSize = 1024,
        });

        int writeCallbackCount = 0;
        int readCallbackCount = 0;
        bufferGraph.addPass("ReadTransientBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", transientBuffer, vke::RenderGraphShaderStage::Fragment)
            .execute(
                [&readCallbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                    if (context.name != "ReadTransientBuffer" ||
                        context.type != "basic.buffer-read-fragment" ||
                        context.paramsType != "basic.buffer-read-fragment.params" ||
                        !context.bufferWrites.empty() || context.bufferReads.size() != 1 ||
                        context.bufferReadSlots.size() != 1 || !context.bufferWriteSlots.empty() ||
                        context.bufferReadSlots.front().name != "source" ||
                        context.bufferReadSlots.front().shaderStage !=
                            vke::RenderGraphShaderStage::Fragment ||
                        context.bufferTransitionsBefore.size() != 1 ||
                        context.bufferTransitionsBefore.front().oldState !=
                            vke::RenderGraphBufferState::TransferWrite ||
                        context.bufferTransitionsBefore.front().newState !=
                            vke::RenderGraphBufferState::ShaderRead ||
                        context.bufferTransitionsBefore.front().newShaderStage !=
                            vke::RenderGraphShaderStage::Fragment) {
                        return std::unexpected{vke::Error{
                            vke::ErrorDomain::RenderGraph,
                            0,
                            "Render graph buffer read executor received unexpected pass context.",
                        }};
                    }
                    ++readCallbackCount;
                    return {};
                });
        bufferGraph.addPass("WriteTransientBuffer", "basic.buffer-transfer-write")
            .setParamsType("basic.buffer-transfer-write.params")
            .writeBuffer("target", transientBuffer)
            .execute(
                [&writeCallbackCount](vke::RenderGraphPassContext context) -> vke::Result<void> {
                    if (context.name != "WriteTransientBuffer" ||
                        context.type != "basic.buffer-transfer-write" ||
                        context.paramsType != "basic.buffer-transfer-write.params" ||
                        !context.bufferReads.empty() || context.bufferWrites.size() != 1 ||
                        !context.bufferReadSlots.empty() || context.bufferWriteSlots.size() != 1 ||
                        context.bufferWriteSlots.front().name != "target" ||
                        context.bufferTransitionsBefore.size() != 1 ||
                        context.bufferTransitionsBefore.front().oldState !=
                            vke::RenderGraphBufferState::Undefined ||
                        context.bufferTransitionsBefore.front().newState !=
                            vke::RenderGraphBufferState::TransferWrite) {
                        return std::unexpected{vke::Error{
                            vke::ErrorDomain::RenderGraph,
                            0,
                            "Render graph buffer write executor received unexpected pass context.",
                        }};
                    }
                    ++writeCallbackCount;
                    return {};
                });

        auto compiled = bufferGraph.compile(schemas);
        if (!compiled) {
            vke::logError(compiled.error().message);
            return false;
        }
        if (compiled->declaredBufferCount != 1 || compiled->passes.size() != 2 ||
            compiled->passes[0].name != "WriteTransientBuffer" ||
            compiled->passes[1].name != "ReadTransientBuffer" ||
            compiled->dependencies.size() != 1 || compiled->transientBuffers.size() != 1 ||
            !compiled->finalBufferTransitions.empty()) {
            vke::logError("Render graph buffer smoke produced an unexpected compile plan.");
            return false;
        }

        auto executed = bufferGraph.execute(*compiled);
        if (!executed) {
            vke::logError(executed.error().message);
            return false;
        }
        if (writeCallbackCount != 1 || readCallbackCount != 1) {
            vke::logError("Render graph buffer smoke invoked unexpected callbacks.");
            return false;
        }

        vke::RenderGraph importedReadGraph;
        const auto importedBuffer = importedReadGraph.importBuffer(vke::RenderGraphBufferDesc{
            .name = "ImportedReadBuffer",
            .byteSize = 256,
            .initialState = vke::RenderGraphBufferState::ShaderRead,
            .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
            .finalState = vke::RenderGraphBufferState::ShaderRead,
            .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        importedReadGraph.addPass("ReadImportedBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", importedBuffer, vke::RenderGraphShaderStage::Fragment);
        auto importedReadCompiled = importedReadGraph.compile(schemas);
        if (!importedReadCompiled) {
            vke::logError("Render graph rejected a shader-readable imported buffer: " +
                          importedReadCompiled.error().message);
            return false;
        }
        if (!importedReadCompiled->finalBufferTransitions.empty() ||
            !importedReadCompiled->transientBuffers.empty() ||
            !importedReadCompiled->passes.front().bufferTransitionsBefore.empty()) {
            vke::logError("Render graph produced unexpected transitions for imported buffer read.");
            return false;
        }

        vke::RenderGraph missingFinalStateGraph;
        const auto missingFinalBuffer =
            missingFinalStateGraph.importBuffer(vke::RenderGraphBufferDesc{
                .name = "ImportedBufferWithoutFinalState",
                .byteSize = 64,
                .initialState = vke::RenderGraphBufferState::ShaderRead,
                .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
            });
        missingFinalStateGraph.addPass("ReadImportedWithoutFinal", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", missingFinalBuffer, vke::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(missingFinalStateGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "must declare an explicit final state",
                                                 .context = "imported buffer without final state",
                                             })) {
            return false;
        }

        vke::RenderGraph missingProducerGraph;
        const auto orphanBuffer = missingProducerGraph.createTransientBuffer(
            vke::RenderGraphBufferDesc{.name = "OrphanTransientBuffer", .byteSize = 64});
        missingProducerGraph.addPass("ReadOrphanBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", orphanBuffer, vke::RenderGraphShaderStage::Fragment);
        if (!expectRenderGraphCompileFailure(missingProducerGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "reads buffer",
                                                 .context = "transient buffer without producer",
                                             })) {
            return false;
        }

        vke::RenderGraph missingStageGraph;
        const auto stageBuffer = missingStageGraph.importBuffer(vke::RenderGraphBufferDesc{
            .name = "BufferMissingShaderStage",
            .byteSize = 64,
            .initialState = vke::RenderGraphBufferState::ShaderRead,
            .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
            .finalState = vke::RenderGraphBufferState::ShaderRead,
            .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        missingStageGraph.addPass("ReadBufferWithoutStage", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", stageBuffer, vke::RenderGraphShaderStage::None);
        if (!expectRenderGraphCompileFailure(missingStageGraph.compile(schemas),
                                             ExpectedRenderGraphCompileFailure{
                                                 .message = "without a shader stage",
                                                 .context = "buffer read without shader stage",
                                             })) {
            return false;
        }

        vke::RenderGraph zeroSizeGraph;
        const auto zeroSizeBuffer = zeroSizeGraph.importBuffer(vke::RenderGraphBufferDesc{
            .name = "ZeroSizeBuffer",
            .byteSize = 0,
            .initialState = vke::RenderGraphBufferState::ShaderRead,
            .initialShaderStage = vke::RenderGraphShaderStage::Fragment,
            .finalState = vke::RenderGraphBufferState::ShaderRead,
            .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        zeroSizeGraph.addPass("ReadZeroSizeBuffer", "basic.buffer-read-fragment")
            .setParamsType("basic.buffer-read-fragment.params")
            .readBuffer("source", zeroSizeBuffer, vke::RenderGraphShaderStage::Fragment);
        return expectRenderGraphCompileFailure(zeroSizeGraph.compile(schemas),
                                               ExpectedRenderGraphCompileFailure{
                                                   .message = "non-zero byte size",
                                                   .context = "zero-size render graph buffer",
                                               });
    }

    int runSmokeRenderGraph() {
        vke::RenderGraph graph;
        const auto backbuffer = graph.importImage(vke::RenderGraphImageDesc{
            .name = "Backbuffer",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::Present,
        });
        const auto depthBuffer = graph.importImage(vke::RenderGraphImageDesc{
            .name = "DepthBuffer",
            .format = vke::RenderGraphImageFormat::D32Sfloat,
            .extent = vke::RenderGraphExtent2D{.width = 1280, .height = 720},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::DepthSampledRead,
            .finalShaderStage = vke::RenderGraphShaderStage::Fragment,
        });
        const auto transientColor = graph.createTransientImage(vke::RenderGraphImageDesc{
            .name = "TransientColor",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 640, .height = 360},
        });

        int callbackCount = 0;
        graph.addPass("ClearColor", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", backbuffer)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.clearColor("target", std::array{0.02F, 0.12F, 0.18F, 1.0F});
            });
        graph.addPass("SampleColor", "basic.sample-fragment")
            .setParamsType("basic.sample-fragment.params")
            .readTexture("source", backbuffer, vke::RenderGraphShaderStage::Fragment)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/SmokeSample", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setFloat("Exposure", 1.0F)
                    .drawFullscreenTriangle();
            });
        graph.addPass("WriteDepth", "basic.depth-write")
            .setParamsType("basic.depth-write.params")
            .writeDepth("depth", depthBuffer);
        graph.addPass("SampleDepth", "basic.depth-sample-fragment")
            .setParamsType("basic.depth-sample-fragment.params")
            .readDepthTexture("depth", depthBuffer, vke::RenderGraphShaderStage::Fragment);
        graph.addPass("SampleTransientColor", "basic.transient-sample-fragment")
            .setParamsType("basic.transient-sample-fragment.params")
            .readTexture("source", transientColor, vke::RenderGraphShaderStage::Fragment)
            .recordCommands([](vke::RenderGraphCommandList& commands) {
                commands.setShader("Hidden/TransientSample", "Fragment")
                    .setTexture("SourceTex", "source")
                    .setVec4("Tint", std::array{1.0F, 0.85F, 0.65F, 1.0F})
                    .drawFullscreenTriangle();
            });
        graph.addPass("WriteTransientColor", "basic.transient-color")
            .setParamsType("basic.transient-color.params")
            .writeColor("target", transientColor);

        vke::RenderGraphSchemaRegistry schemas;
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.clear-transfer",
            .paramsType = "basic.clear-transfer.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = vke::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {vke::RenderGraphCommandKind::ClearColor},
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.sample-fragment",
            .paramsType = "basic.sample-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = vke::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    vke::RenderGraphCommandKind::SetShader,
                    vke::RenderGraphCommandKind::SetTexture,
                    vke::RenderGraphCommandKind::SetFloat,
                    vke::RenderGraphCommandKind::DrawFullscreenTriangle,
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.depth-write",
            .paramsType = "basic.depth-write.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = vke::RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.depth-sample-fragment",
            .paramsType = "basic.depth-sample-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = vke::RenderGraphSlotAccess::DepthSampledRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.transient-color",
            .paramsType = "basic.transient-color.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = vke::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.transient-sample-fragment",
            .paramsType = "basic.transient-sample-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = vke::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands =
                {
                    vke::RenderGraphCommandKind::SetShader,
                    vke::RenderGraphCommandKind::SetTexture,
                    vke::RenderGraphCommandKind::SetVec4,
                    vke::RenderGraphCommandKind::DrawFullscreenTriangle,
                },
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.cycle-read-write",
            .paramsType = "basic.cycle-read-write.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = vke::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                    vke::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = vke::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.side-effect",
            .paramsType = "basic.side-effect.params",
            .resourceSlots = {},
            .allowedCommands = {},
            .allowCulling = true,
            .hasSideEffects = true,
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.buffer-transfer-write",
            .paramsType = "basic.buffer-transfer-write.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = vke::RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = vke::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(vke::RenderGraphPassSchema{
            .type = "basic.buffer-read-fragment",
            .paramsType = "basic.buffer-read-fragment.params",
            .resourceSlots =
                {
                    vke::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = vke::RenderGraphSlotAccess::BufferShaderRead,
                        .shaderStage = vke::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            vke::logError(compiled.error().message);
            return EXIT_FAILURE;
        }
        if (compiled->finalTransitions.empty()) {
            vke::logError("Render graph did not produce a final transition.");
            return EXIT_FAILURE;
        }
        if (compiled->passes.size() != 6 || compiled->passes[1].transitionsBefore.empty() ||
            compiled->passes[2].transitionsBefore.empty() ||
            compiled->passes[3].transitionsBefore.empty() ||
            compiled->passes[4].transitionsBefore.empty() ||
            compiled->passes[5].transitionsBefore.empty() ||
            compiled->transientImages.size() != 1) {
            vke::logError("Render graph did not produce the expected shader-read pass transition.");
            return EXIT_FAILURE;
        }
        if (compiled->passes[4].name != "WriteTransientColor" ||
            compiled->passes[4].declarationIndex != 5 ||
            compiled->passes[5].name != "SampleTransientColor" ||
            compiled->passes[5].declarationIndex != 4) {
            vke::logError(
                "Render graph compiler did not reorder transient producer before reader.");
            return EXIT_FAILURE;
        }
        if (compiled->dependencies.size() != 3) {
            vke::logError("Render graph compiler produced an unexpected dependency count.");
            return EXIT_FAILURE;
        }
        if (!compiled->culledPasses.empty()) {
            vke::logError("Render graph compiler culled an unexpected smoke pass.");
            return EXIT_FAILURE;
        }

        std::cout << graph.formatDebugTables(*compiled) << '\n';

        if (!validateSmokeRenderGraphTransientPlan(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphCommands(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphVulkanMappings(*compiled)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphNegativeCompiles(schemas)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphBuiltinSchemaFailures()) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphCulling(schemas)) {
            return EXIT_FAILURE;
        }

        if (!validateSmokeRenderGraphBuffers(schemas)) {
            return EXIT_FAILURE;
        }

        vke::RenderGraph invalidCommandGraph;
        const auto invalidBackbuffer = invalidCommandGraph.importImage(vke::RenderGraphImageDesc{
            .name = "InvalidBackbuffer",
            .format = vke::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = vke::RenderGraphExtent2D{.width = 16, .height = 16},
            .initialState = vke::RenderGraphImageState::Undefined,
            .finalState = vke::RenderGraphImageState::Present,
        });
        invalidCommandGraph.addPass("InvalidClearCommand", "basic.clear-transfer")
            .setParamsType("basic.clear-transfer.params")
            .writeTransfer("target", invalidBackbuffer)
            .recordCommands(
                [](vke::RenderGraphCommandList& commands) { commands.drawFullscreenTriangle(); });
        auto invalidCompiled = invalidCommandGraph.compile(schemas);
        if (invalidCompiled) {
            vke::logError("Render graph schema accepted an invalid command kind.");
            return EXIT_FAILURE;
        }

        vke::RenderGraphExecutorRegistry executors;
        registerSmokeRenderGraphExecutors(executors, callbackCount);

        auto executed = graph.execute(*compiled, executors);
        if (!executed) {
            vke::logError(executed.error().message);
            return EXIT_FAILURE;
        }

        std::cout << "Render graph passes: " << compiled->passes.size()
                  << ", final transitions: " << compiled->finalTransitions.size()
                  << ", dependencies: " << compiled->dependencies.size()
                  << ", culled: " << compiled->culledPasses.size()
                  << ", callbacks: " << callbackCount << '\n';
        return compiled->passes.size() == 6 && compiled->transientImages.size() == 1 &&
                       compiled->finalTransitions.size() == 1 &&
                       compiled->dependencies.size() == 3 && compiled->culledPasses.empty() &&
                       callbackCount == 6
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

} // namespace

int main(int argc, char** argv) {
    try {
        std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (args.size() == 1) {
            return runInteractiveViewer();
        }

        if (hasArg(args, "--help")) {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--smoke-window")) {
            return runSmokeWindow();
        }

        if (hasArg(args, "--smoke-vulkan")) {
            return runSmokeVulkan();
        }

        if (hasArg(args, "--smoke-frame")) {
            return runSmokeFrame();
        }

        if (hasArg(args, "--smoke-rendergraph")) {
            return runSmokeRenderGraph();
        }

        if (hasArg(args, "--bench-rendergraph")) {
            return runBenchRenderGraph(args);
        }

        if (hasArg(args, "--smoke-transient")) {
            return runSmokeTransient();
        }

        if (hasArg(args, "--smoke-dynamic-rendering")) {
            return runSmokeDynamicRendering();
        }

        if (hasArg(args, "--smoke-resize")) {
            return runSmokeResize();
        }

        if (hasArg(args, "--smoke-triangle")) {
            return runSmokeTriangle();
        }

        if (hasArg(args, "--smoke-depth-triangle")) {
            return runSmokeTriangle(true);
        }

        if (hasArg(args, "--smoke-mesh")) {
            return runSmokeTriangle(false, vke::BasicMeshKind::IndexedQuad);
        }

        if (hasArg(args, "--smoke-mesh-3d")) {
            return runSmokeMesh3D();
        }

        if (hasArg(args, "--smoke-draw-list")) {
            return runSmokeDrawList();
        }

        if (hasArg(args, "--smoke-descriptor-layout")) {
            return runSmokeDescriptorLayout();
        }

        if (hasArg(args, "--smoke-fullscreen-texture")) {
            return runSmokeFullscreenTexture();
        }

        if (hasArg(args, "--smoke-offscreen-viewport")) {
            return runSmokeOffscreenViewport();
        }

        if (hasArg(args, "--smoke-deferred-deletion")) {
            return runSmokeDeferredDeletion();
        }

        printVersion();
        printUsage();
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        vke::logError(exception.what());
    } catch (...) {
        vke::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
