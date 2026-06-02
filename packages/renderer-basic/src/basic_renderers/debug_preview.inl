[[nodiscard]] Result<void>
executeBasicDebugImageCopyPass(const VulkanFrameRecordContext& frame, RenderGraphPassContext pass,
                               std::span<const VulkanRenderGraphImageBinding> bindings,
                               VkExtent2D copyExtent, BasicDebugPreviewResult* previewResult,
                               BasicRenderViewExecutionEventRecorder* eventRecorder) {
    [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
    [[maybe_unused]] const auto debugLabel =
        VulkanDebugLabelScope::begin(frame, renderGraphPassDebugLabel(pass, bindings));

    if (eventRecorder != nullptr) {
        eventRecorder->beginPass(pass);
    }
    auto transitions = recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
    if (!transitions) {
        return std::unexpected{std::move(transitions.error())};
    }
    if (pass.commands.size() != 1 ||
        pass.commands.front().kind != RenderGraphCommandKind::CopyImage ||
        pass.commands.front().name != "source" || pass.commands.front().secondaryName != "target") {
        return std::unexpected{renderGraphError(
            "Debug image copy pass expected one copyImage(source, target) command")};
    }

    auto source = findVulkanRenderGraphTransferRead(pass, "source", bindings);
    if (!source) {
        return std::unexpected{std::move(source.error())};
    }
    auto target = findVulkanRenderGraphTransferWrite(pass, "target", bindings);
    if (!target) {
        return std::unexpected{std::move(target.error())};
    }

    recordImageCopy(frame, *source, *target, copyExtent);
    if (eventRecorder != nullptr) {
        eventRecorder->append(pass, BasicRenderViewExecutionEventKind::CopyImage,
                              "CopyImage source -> target",
                              firstCommandIndex(pass, RenderGraphCommandKind::CopyImage), {}, {},
                              source->image.index, target->image.index);
        eventRecorder->endPass(pass);
    }
    if (previewResult != nullptr) {
        ++previewResult->copiesRecorded;
    }
    return {};
}

struct BasicRenderViewImageCandidate {
    RenderGraphImageHandle image{};
    std::string_view name;
    RenderGraphImageFormat format{RenderGraphImageFormat::Undefined};
    RenderGraphExtent2D extent{};
    VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
};

[[nodiscard]] const BasicRenderViewImageCandidate*
findBasicRenderViewImageCandidate(std::span<const BasicRenderViewImageCandidate> candidates,
                                  std::uint32_t sourceImageResourceIndex) {
    for (const BasicRenderViewImageCandidate& candidate : candidates) {
        if (candidate.image.index == sourceImageResourceIndex) {
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] bool sameRenderGraphExtent(RenderGraphExtent2D lhs, RenderGraphExtent2D rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

void setBasicDebugPreviewResult(BasicDebugPreviewRequest* request, BasicDebugPreviewStatus status,
                                std::string message = {}) {
    if (request == nullptr || request->result == nullptr) {
        return;
    }

    *request->result = BasicDebugPreviewResult{
        .status = status,
        .sourceImageResourceIndex = request->sourceImageResourceIndex,
        .copiedAfterPassIndex = request->afterPassIndex,
        .message = std::move(message),
        .copiesRecorded = {},
    };
}

[[nodiscard]] Result<bool>
tryAddBasicDebugPreviewPass(RenderGraph& graph, BasicDebugPreviewRequest* request,
                            std::span<const BasicRenderViewImageCandidate> candidates,
                            std::vector<VulkanRenderGraphImageBinding>& bindings,
                            const VulkanFrameRecordContext& frame,
                            BasicRenderViewExecutionEventRecorder* eventRecorder) {
    if (request == nullptr) {
        return false;
    }

    setBasicDebugPreviewResult(request, BasicDebugPreviewStatus::Unavailable);
    auto previewTarget = validateBasicRenderViewTarget(request->target, "Debug preview target");
    if (!previewTarget) {
        return std::unexpected{std::move(previewTarget.error())};
    }
    if (request->target.finalUsage != BasicRenderViewTargetFinalUsage::SampledTexture) {
        return std::unexpected{
            renderGraphError("Debug preview target must declare SampledTexture final usage")};
    }

    const BasicRenderViewImageCandidate* source =
        findBasicRenderViewImageCandidate(candidates, request->sourceImageResourceIndex);
    if (source == nullptr) {
        setBasicDebugPreviewResult(request, BasicDebugPreviewStatus::Unavailable,
                                   "Selected image resource is not in the replay graph.");
        return false;
    }
    if (source->format != RenderGraphImageFormat::B8G8R8A8Srgb ||
        source->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
        setBasicDebugPreviewResult(request, BasicDebugPreviewStatus::Unavailable,
                                   "Selected image is not a supported color image.");
        return false;
    }

    auto targetFormat = basicRenderGraphImageFormat(request->target.format, "Debug preview target");
    if (!targetFormat) {
        return std::unexpected{std::move(targetFormat.error())};
    }
    const RenderGraphExtent2D targetExtent = basicRenderGraphExtent(request->target.extent);
    if (*targetFormat != source->format || !sameRenderGraphExtent(targetExtent, source->extent) ||
        request->target.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
        setBasicDebugPreviewResult(
            request, BasicDebugPreviewStatus::Unavailable,
            "Selected image and preview target do not have matching color shape.");
        return false;
    }

    auto previewTargetDesc = basicRenderViewTargetDesc(
        request->target, RenderGraphImageState::Undefined, "DebugPreviewTarget");
    if (!previewTargetDesc) {
        return std::unexpected{std::move(previewTargetDesc.error())};
    }
    const auto previewTargetImage = graph.importImage(*previewTargetDesc);
    bindings.push_back(basicRenderViewTargetBinding(previewTargetImage, request->target));

    setBasicDebugPreviewResult(request, BasicDebugPreviewStatus::Available);
    graph.addPass("DebugImageCopy", kBasicDebugImageCopyPassType)
        .readTransfer("source", source->image)
        .writeTransfer("target", previewTargetImage)
        .recordCommands(
            [](RenderGraphCommandList& commands) { commands.copyImage("source", "target"); })
        .execute([&frame, &bindings, request,
                  eventRecorder](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicDebugImageCopyPass(frame, pass, bindings, request->target.extent,
                                                  request == nullptr ? nullptr : request->result,
                                                  eventRecorder);
        });
    return true;
}

[[nodiscard]] Result<bool> tryAddBasicDebugPreviewPassAfterPass(
    RenderGraph& graph, BasicDebugPreviewRequest* request,
    std::span<const BasicRenderViewImageCandidate> candidates,
    std::vector<VulkanRenderGraphImageBinding>& bindings, const VulkanFrameRecordContext& frame,
    BasicRenderViewExecutionEventRecorder* eventRecorder, std::size_t completedPassIndex) {
    if (request == nullptr || !request->afterPassIndex ||
        *request->afterPassIndex != completedPassIndex) {
        return false;
    }

    return tryAddBasicDebugPreviewPass(graph, request, candidates, bindings, frame, eventRecorder);
}

struct BasicDebugPreviewSourcePassCursor {
    RenderGraph& graph;
    BasicDebugPreviewRequest* request{};
    std::span<const BasicRenderViewImageCandidate> candidates;
    std::vector<VulkanRenderGraphImageBinding>& bindings;
    const VulkanFrameRecordContext& frame;
    BasicRenderViewExecutionEventRecorder& eventRecorder;
    std::size_t nextSourcePassIndex{};

    void advanceSourcePassWithoutPreview() {
        ++nextSourcePassIndex;
    }

    [[nodiscard]] Result<void> tryAddPreviewAfterSourcePass() {
        const std::size_t completedPassIndex = nextSourcePassIndex++;
        auto debugPreviewAdded = tryAddBasicDebugPreviewPassAfterPass(
            graph, request, candidates, bindings, frame, &eventRecorder, completedPassIndex);
        if (!debugPreviewAdded) {
            return std::unexpected{std::move(debugPreviewAdded.error())};
        }
        return {};
    }

    [[nodiscard]] Result<void> tryAddEndOfGraphPreview() {
        if (request == nullptr || request->afterPassIndex) {
            return {};
        }

        auto debugPreviewAdded =
            tryAddBasicDebugPreviewPass(graph, request, candidates, bindings, frame,
                                        &eventRecorder);
        if (!debugPreviewAdded) {
            return std::unexpected{std::move(debugPreviewAdded.error())};
        }
        return {};
    }

    void markUnrecordedSelectedPassUnavailable() {
        if (request != nullptr && request->afterPassIndex && request->result != nullptr &&
            request->result->status == BasicDebugPreviewStatus::NotRequested) {
            setBasicDebugPreviewResult(request, BasicDebugPreviewStatus::Unavailable,
                                       "Selected pass was not recorded during replay.");
        }
    }
};
