#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/rendergraph/render_graph.hpp"

namespace {

    constexpr std::string_view kColorWritePass = "test.color-write";
    constexpr std::string_view kColorReadWritePass = "test.color-read-write";
    constexpr std::string_view kDepthWritePass = "test.depth-write";
    constexpr std::string_view kTransferWritePass = "test.transfer-write";
    constexpr std::string_view kSamplePresentPass = "test.sample-present";
    constexpr std::string_view kTextureReadPass = "test.texture-read";
    constexpr std::string_view kImageCopyPass = "test.image-copy";
    constexpr std::string_view kStorageReadWritePass = "test.storage-rw";
    constexpr std::string_view kBufferTransferReadPass = "test.buffer-transfer-read";
    constexpr std::string_view kBufferTransferWritePass = "test.buffer-transfer-write";
    constexpr std::string_view kBufferCopyPass = "test.buffer-copy";
    constexpr std::string_view kBufferToImageCopyPass = "test.buffer-to-image-copy";
    constexpr std::string_view kImageToBufferCopyPass = "test.image-to-buffer-copy";
    constexpr std::string_view kIndexedMeshPass = "test.indexed-mesh";
    constexpr std::string_view kSideEffectPass = "test.side-effect";

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    struct ExpectedError {
        ExpectedError(const char* value) : text{value} {}
        std::string_view text;
    };

    struct FailureContext {
        FailureContext(const char* value) : text{value} {}
        std::string_view text;
    };

    [[nodiscard]] bool expect(bool condition, std::string_view message) {
        if (!condition) {
            logFailure(message);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    expectCompileFailure(const asharia::Result<asharia::RenderGraphCompileResult>& compiled,
                         ExpectedError expectedMessage, FailureContext context) {
        if (compiled) {
            std::cerr << "RenderGraph accepted invalid graph: " << context.text << '\n';
            return false;
        }

        if (!contains(compiled.error().message, expectedMessage.text)) {
            std::cerr << "RenderGraph produced unexpected error for " << context.text << ": "
                      << compiled.error().message << '\n';
            return false;
        }

        return true;
    }

    [[nodiscard]] bool expectExecuteFailure(const asharia::Result<void>& executed,
                                            ExpectedError expectedMessage, FailureContext context) {
        if (executed) {
            std::cerr << "RenderGraph executed invalid graph: " << context.text << '\n';
            return false;
        }

        if (!contains(executed.error().message, expectedMessage.text)) {
            std::cerr << "RenderGraph produced unexpected execute error for " << context.text
                      << ": " << executed.error().message << '\n';
            return false;
        }

        return true;
    }

    [[nodiscard]] asharia::RenderGraphImageDesc importedColorDesc(std::string name) {
        return asharia::RenderGraphImageDesc{
            .name = std::move(name),
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        };
    }

    [[nodiscard]] asharia::RenderGraphImageDesc importedSampledDesc(std::string name) {
        return asharia::RenderGraphImageDesc{
            .name = std::move(name),
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::ShaderRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            .finalState = asharia::RenderGraphImageState::Present,
        };
    }

    [[nodiscard]] asharia::RenderGraphImageDesc transientColorDesc(std::string name) {
        return asharia::RenderGraphImageDesc{
            .name = std::move(name),
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
        };
    }

    [[nodiscard]] asharia::RenderGraphBufferDesc importedStorageDesc(std::string name) {
        return asharia::RenderGraphBufferDesc{
            .name = std::move(name),
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::StorageReadWrite,
            .initialShaderStage = asharia::RenderGraphShaderStage::Compute,
            .finalState = asharia::RenderGraphBufferState::StorageReadWrite,
            .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
        };
    }

    [[nodiscard]] asharia::RenderGraphBufferDesc transientBufferDesc(std::string name) {
        return asharia::RenderGraphBufferDesc{
            .name = std::move(name),
            .byteSize = 256,
        };
    }

    [[nodiscard]] asharia::RenderGraphSchemaRegistry makeCompileTestSchemas() {
        asharia::RenderGraphSchemaRegistry schemas;
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kColorWritePass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
            .allowCulling = true,
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kColorReadWritePass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::ColorReadWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
            .allowCulling = true,
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kDepthWritePass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "depth",
                        .access = asharia::RenderGraphSlotAccess::DepthAttachmentWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
            .allowCulling = true,
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kTransferWritePass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
            .allowCulling = true,
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kSamplePresentPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kTextureReadPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kImageCopyPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::TransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyImage},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kStorageReadWritePass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferStorageReadWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::Compute,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
            .allowCulling = true,
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kBufferTransferReadPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kBufferTransferWritePass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::FillBuffer},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kBufferCopyPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyBuffer},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kBufferToImageCopyPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::TransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyBufferToImage},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kImageToBufferCopyPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::TransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyImageToBuffer},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kIndexedMeshPass},
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "vertices",
                        .access = asharia::RenderGraphSlotAccess::BufferVertexRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "indices",
                        .access = asharia::RenderGraphSlotAccess::BufferIndexRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::DrawIndexed},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = std::string{kSideEffectPass},
            .paramsType = {},
            .resourceSlots = {},
            .allowedCommands = {},
            .allowCulling = true,
            .hasSideEffects = true,
        });

        return schemas;
    }

    [[nodiscard]] bool
    compilesColorAttachmentWriteHazards(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto color = graph.importImage(importedColorDesc("ColorHazardTarget"));

        graph.addPass("InitialColorWrite", std::string{kColorWritePass})
            .writeColor("target", color);
        graph.addPass("RepeatedColorWrite", std::string{kColorWritePass})
            .writeColor("target", color);
        graph.addPass("LoadColor", std::string{kColorReadWritePass})
            .readWriteColor("target", color);
        graph.addPass("RepeatedLoadColor", std::string{kColorReadWritePass})
            .readWriteColor("target", color);

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 4,
                    "RenderGraph did not preserve the color hazard pass sequence.")) {
            return false;
        }

        const auto hasSingleTransition = [](const asharia::RenderGraphCompiledPass& pass,
                                            asharia::RenderGraphImageState oldState,
                                            asharia::RenderGraphImageState newState) {
            return pass.transitionsBefore.size() == 1 &&
                   pass.transitionsBefore.front().oldState == oldState &&
                   pass.transitionsBefore.front().newState == newState;
        };

        if (!expect(hasSingleTransition(compiled->passes[1],
                                        asharia::RenderGraphImageState::ColorAttachment,
                                        asharia::RenderGraphImageState::ColorAttachment),
                    "RenderGraph omitted the repeated ColorAttachment WAW barrier.")) {
            return false;
        }
        if (!expect(hasSingleTransition(compiled->passes[2],
                                        asharia::RenderGraphImageState::ColorAttachment,
                                        asharia::RenderGraphImageState::ColorReadWrite),
                    "RenderGraph omitted the ColorAttachment to ColorReadWrite barrier.")) {
            return false;
        }
        if (!expect(hasSingleTransition(compiled->passes[3],
                                        asharia::RenderGraphImageState::ColorReadWrite,
                                        asharia::RenderGraphImageState::ColorReadWrite),
                    "RenderGraph omitted the repeated ColorReadWrite RAW/WAW barrier.")) {
            return false;
        }
        if (!expect(compiled->passes[2].colorReadWrites.size() == 1 &&
                        compiled->passes[2].colorReadWriteSlots.size() == 1 &&
                        compiled->passes[2].colorWrites.empty(),
                    "RenderGraph did not preserve the compiled ColorReadWrite contract.")) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        const bool hasColorReadWriteEdge = std::ranges::any_of(
            snapshot.accessEdges, [](const asharia::RenderGraphDiagnosticsAccessEdge& edge) {
                return edge.passName == "LoadColor" && edge.slotName == "target" &&
                       edge.access == asharia::RenderGraphSlotAccess::ColorReadWrite;
            });
        if (!expect(hasColorReadWriteEdge,
                    "RenderGraph diagnostics omitted the ColorReadWrite access edge.")) {
            return false;
        }

        const std::string debugTables = graph.formatDebugTables(*compiled);
        if (!expect(contains(debugTables, "ColorReadWrite"),
                    "RenderGraph debug tables omitted the ColorReadWrite contract.")) {
            return false;
        }

        asharia::RenderGraph importedInitialGraph;
        asharia::RenderGraphImageDesc importedInitialDesc =
            importedColorDesc("ImportedColorReadWrite");
        importedInitialDesc.initialState = asharia::RenderGraphImageState::ColorReadWrite;
        const auto importedInitial =
            importedInitialGraph.importImage(std::move(importedInitialDesc));
        bool observedContext = false;
        importedInitialGraph.addPass("LoadImportedColor", std::string{kColorReadWritePass})
            .readWriteColor("target", importedInitial)
            .execute([&observedContext](asharia::RenderGraphPassContext context) {
                observedContext = context.colorReadWrites.size() == 1 &&
                                  context.colorReadWriteSlots.size() == 1 &&
                                  context.colorWrites.empty();
                return asharia::Result<void>{};
            });
        auto importedCompiled = importedInitialGraph.compile(schemas);
        if (!expect(importedCompiled.has_value() && importedCompiled->passes.size() == 1 &&
                        importedCompiled->passes.front().transitionsBefore.size() == 1,
                    "RenderGraph rejected ColorReadWrite from a known imported initial state.")) {
            return false;
        }
        auto executed = importedInitialGraph.execute(*importedCompiled);
        return expect(executed.has_value() && observedContext,
                      "RenderGraph executor omitted the ColorReadWrite pass context contract.");
    }

    [[nodiscard]] bool
    rejectsColorReadWriteWithoutProducer(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto transient =
            graph.createTransientImage(transientColorDesc("UndefinedLoadTarget"));
        graph.addPass("LoadUndefinedColor", std::string{kColorReadWritePass})
            .readWriteColor("target", transient);
        return expectCompileFailure(
            graph.compile(schemas),
            "reads image '#0 UndefinedLoadTarget' before any pass writes it",
            "ColorReadWrite from an undefined transient image");
    }

    [[nodiscard]] bool
    compilesNonColorImageWriteHazards(const asharia::RenderGraphSchemaRegistry& schemas) {
        const auto hasSameStateTransition = [](const asharia::RenderGraphCompiledPass& pass,
                                               asharia::RenderGraphImageState state) {
            return pass.transitionsBefore.size() == 1 &&
                   pass.transitionsBefore.front().oldState == state &&
                   pass.transitionsBefore.front().newState == state;
        };

        asharia::RenderGraph depthGraph;
        const auto depth = depthGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "DepthWriteHazard",
            .format = asharia::RenderGraphImageFormat::D32Sfloat,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::DepthAttachmentWrite,
        });
        depthGraph.addPass("InitialDepthWrite", std::string{kDepthWritePass})
            .writeDepth("depth", depth);
        depthGraph.addPass("RepeatedDepthWrite", std::string{kDepthWritePass})
            .writeDepth("depth", depth);
        auto depthCompiled = depthGraph.compile(schemas);
        if (!expect(
                depthCompiled.has_value() && depthCompiled->passes.size() == 2 &&
                    hasSameStateTransition(depthCompiled->passes[1],
                                           asharia::RenderGraphImageState::DepthAttachmentWrite),
                "RenderGraph omitted the repeated DepthAttachmentWrite WAW barrier.")) {
            return false;
        }

        asharia::RenderGraph transferGraph;
        const auto transfer = transferGraph.importImage(importedColorDesc("TransferWriteHazard"));
        transferGraph.addPass("InitialTransferWrite", std::string{kTransferWritePass})
            .writeTransfer("target", transfer);
        transferGraph.addPass("RepeatedTransferWrite", std::string{kTransferWritePass})
            .writeTransfer("target", transfer);
        auto transferCompiled = transferGraph.compile(schemas);
        return expect(transferCompiled.has_value() && transferCompiled->passes.size() == 2 &&
                          hasSameStateTransition(transferCompiled->passes[1],
                                                 asharia::RenderGraphImageState::TransferDst),
                      "RenderGraph omitted the repeated TransferDst WAW barrier.");
    }

    [[nodiscard]] bool
    cullsUnusedTransientButKeepsImportedWrites(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto backbuffer = graph.importImage(importedColorDesc("Backbuffer"));
        const auto unusedTransient = graph.createTransientImage(transientColorDesc("Unused"));
        const auto importedStorage = graph.importBuffer(importedStorageDesc("ImportedStorage"));

        graph.addPass("CullUnusedTransient", std::string{kColorWritePass})
            .writeColor("target", unusedTransient);
        graph.addPass("KeepImportedColorWrite", std::string{kTransferWritePass})
            .writeTransfer("target", backbuffer);
        graph.addPass("KeepImportedStorageWrite", std::string{kStorageReadWritePass})
            .readWriteStorageBuffer("target", importedStorage,
                                    asharia::RenderGraphShaderStage::Compute);

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }

        if (!expect(compiled->passes.size() == 2,
                    "RenderGraph did not keep both imported resource writes.")) {
            return false;
        }
        if (!expect(compiled->culledPasses.size() == 1,
                    "RenderGraph did not cull exactly one unused transient writer.")) {
            return false;
        }
        if (!expect(compiled->culledPasses.front().name == "CullUnusedTransient",
                    "RenderGraph culled the wrong pass.")) {
            return false;
        }
        if (!expect(compiled->passes[0].name == "KeepImportedColorWrite" &&
                        compiled->passes[1].name == "KeepImportedStorageWrite",
                    "RenderGraph kept imported writes in an unexpected order.")) {
            return false;
        }
        if (!expect(compiled->passes[0].allowCulling && compiled->passes[1].allowCulling,
                    "RenderGraph did not preserve schema culling metadata.")) {
            return false;
        }
        if (!expect(compiled->transientImages.empty(),
                    "RenderGraph allocated a transient used only by a culled pass.")) {
            return false;
        }

        return expect(compiled->finalTransitions.size() == 1,
                      "RenderGraph did not emit the imported image final transition.");
    }

    [[nodiscard]] bool
    keepsSideEffectPassAndExecutesIt(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        int callbackCount = 0;
        graph.addPass("SideEffectMarker", std::string{kSideEffectPass})
            .execute(
                [&callbackCount](asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                    if (!context.allowCulling || !context.hasSideEffects) {
                        return std::unexpected{asharia::Error{
                            asharia::ErrorDomain::RenderGraph,
                            0,
                            "Side-effect pass metadata was not preserved.",
                        }};
                    }

                    ++callbackCount;
                    return {};
                });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 1 && compiled->culledPasses.empty(),
                    "RenderGraph culled a side-effect pass.")) {
            return false;
        }
        if (!expect(compiled->passes.front().hasSideEffects,
                    "RenderGraph did not preserve side-effect metadata in the compiled pass.")) {
            return false;
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            std::cerr << executed.error().message << '\n';
            return false;
        }

        return expect(callbackCount == 1, "RenderGraph did not execute the side-effect pass once.");
    }

    [[nodiscard]] bool
    reordersFutureProducerBeforeConsumer(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto backbuffer = graph.importImage(importedColorDesc("Backbuffer"));
        const auto transient = graph.createTransientImage(transientColorDesc("FutureSource"));

        graph.addPass("ReadBeforeFutureProducer", std::string{kSamplePresentPass})
            .readTexture("source", transient, asharia::RenderGraphShaderStage::Fragment)
            .writeTransfer("target", backbuffer);
        graph.addPass("FutureProducer", std::string{kColorWritePass})
            .writeColor("target", transient);

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }

        if (!expect(compiled->passes.size() == 2,
                    "RenderGraph did not keep producer and consumer passes.")) {
            return false;
        }
        if (!expect(compiled->passes[0].name == "FutureProducer" &&
                        compiled->passes[1].name == "ReadBeforeFutureProducer",
                    "RenderGraph did not topologically reorder the future producer before the "
                    "consumer.")) {
            return false;
        }

        bool foundProducerDependency = false;
        for (const asharia::RenderGraphPassDependency& dependency : compiled->dependencies) {
            if (dependency.fromDeclarationIndex == 1 && dependency.toDeclarationIndex == 0 &&
                dependency.resourceKind == asharia::RenderGraphResourceKind::Image &&
                dependency.reason == "producer read") {
                foundProducerDependency = true;
            }
        }

        return expect(foundProducerDependency,
                      "RenderGraph did not record the producer-read dependency.");
    }

    [[nodiscard]] bool
    keepsImportedInitialReadBeforeOverwrite(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto imported = graph.importImage(importedSampledDesc("ImportedTexture"));

        graph.addPass("ReadImportedInitial", std::string{kTextureReadPass})
            .readTexture("source", imported, asharia::RenderGraphShaderStage::Fragment);
        graph.addPass("OverwriteImportedAfterRead", std::string{kTransferWritePass})
            .writeTransfer("target", imported);

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }

        if (!expect(compiled->passes.size() == 2,
                    "RenderGraph did not keep imported initial read and overwrite passes.")) {
            return false;
        }
        if (!expect(compiled->passes[0].name == "ReadImportedInitial" &&
                        compiled->passes[1].name == "OverwriteImportedAfterRead",
                    "RenderGraph reordered an imported initial read after its overwrite.")) {
            return false;
        }

        bool foundInitialReadDependency = false;
        for (const asharia::RenderGraphPassDependency& dependency : compiled->dependencies) {
            if (dependency.fromDeclarationIndex == 0 && dependency.toDeclarationIndex == 1 &&
                dependency.resourceKind == asharia::RenderGraphResourceKind::Image &&
                dependency.reason == "initial read before overwrite") {
                foundInitialReadDependency = true;
            }
        }

        return expect(foundInitialReadDependency,
                      "RenderGraph did not protect an imported initial read before overwrite.");
    }

    [[nodiscard]] bool
    buildsDiagnosticsSnapshot(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto backbuffer = graph.importImage(importedColorDesc("Backbuffer"));
        const auto transient = graph.createTransientImage(transientColorDesc("TransientColor"));

        graph.addPass("SampleTransient", std::string{kSamplePresentPass})
            .readTexture("source", transient, asharia::RenderGraphShaderStage::Fragment)
            .writeTransfer("target", backbuffer);
        graph.addPass("WriteTransient", std::string{kColorWritePass})
            .writeColor("target", transient);

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        if (!expect(snapshot.declaredPassCount == 2 && snapshot.declaredImageCount == 2 &&
                        snapshot.declaredBufferCount == 0,
                    "RenderGraph diagnostics snapshot did not preserve declared counts.")) {
            return false;
        }
        if (!expect(snapshot.passes.size() == 2 && snapshot.resources.size() == 2 &&
                        snapshot.accessEdges.size() == 3 && snapshot.dependencyEdges.size() == 1 &&
                        snapshot.transitions.size() == 4,
                    "RenderGraph diagnostics snapshot produced unexpected summary counts.")) {
            return false;
        }
        if (!expect(snapshot.passes[0].name == "WriteTransient" &&
                        snapshot.passes[0].declarationIndex == 1 &&
                        snapshot.passes[1].name == "SampleTransient" &&
                        snapshot.passes[1].declarationIndex == 0,
                    "RenderGraph diagnostics snapshot did not preserve compiled pass order.")) {
            return false;
        }
        if (!expect(snapshot.resources[0].name == "Backbuffer" &&
                        snapshot.resources[0].kind == asharia::RenderGraphResourceKind::Image &&
                        snapshot.resources[1].name == "TransientColor" &&
                        snapshot.resources[1].imageLifetime ==
                            asharia::RenderGraphImageLifetime::Transient,
                    "RenderGraph diagnostics snapshot resource nodes were unexpected.")) {
            return false;
        }

        bool foundTransientReadEdge = false;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passName == "SampleTransient" && edge.resourceName == "TransientColor" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::ShaderRead &&
                edge.shaderStage == asharia::RenderGraphShaderStage::Fragment) {
                foundTransientReadEdge = true;
            }
        }
        if (!expect(foundTransientReadEdge,
                    "RenderGraph diagnostics snapshot missed the transient read edge.")) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsDependencyEdge& dependency =
            snapshot.dependencyEdges.front();
        if (!expect(dependency.fromPassIndex == 0 && dependency.toPassIndex == 1 &&
                        dependency.fromDeclarationIndex == 1 &&
                        dependency.toDeclarationIndex == 0 &&
                        dependency.resourceName == "TransientColor" &&
                        dependency.reason == "producer read",
                    "RenderGraph diagnostics snapshot dependency edge was unexpected.")) {
            return false;
        }

        bool foundFinalBackbufferTransition = false;
        for (const asharia::RenderGraphDiagnosticsTransition& transition : snapshot.transitions) {
            if (transition.phase == asharia::RenderGraphDiagnosticsTransitionPhase::Final &&
                transition.resourceName == "Backbuffer" &&
                transition.oldImageAccess.state == asharia::RenderGraphImageState::TransferDst &&
                transition.newImageAccess.state == asharia::RenderGraphImageState::Present) {
                foundFinalBackbufferTransition = true;
            }
        }

        return expect(foundFinalBackbufferTransition,
                      "RenderGraph diagnostics snapshot missed the final transition.");
    }

    [[nodiscard]] bool
    compilesImageTransferCopy(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto source = graph.createTransientImage(transientColorDesc("CopySource"));
        const auto target = graph.importImage(importedColorDesc("CopyTarget"));

        graph.addPass("ProduceCopySource", std::string{kColorWritePass})
            .writeColor("target", source);
        graph.addPass("CopyImage", std::string{kImageCopyPass})
            .readTransfer("source", source)
            .writeTransfer("target", target)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImage("source", "target");
            });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 2,
                    "RenderGraph did not keep image copy producer and copy pass.")) {
            return false;
        }
        const asharia::RenderGraphCompiledPass& copyPass = compiled->passes[1];
        if (!expect(copyPass.transferReadSlots.size() == 1 &&
                        copyPass.transferWriteSlots.size() == 1 &&
                        copyPass.transferReadSlots.front().name == "source" &&
                        copyPass.transferWriteSlots.front().name == "target",
                    "RenderGraph did not preserve image transfer copy slots.")) {
            return false;
        }
        if (!expect(copyPass.commands.size() == 1 &&
                        copyPass.commands.front().kind ==
                            asharia::RenderGraphCommandKind::CopyImage &&
                        copyPass.commands.front().name == "source" &&
                        copyPass.commands.front().secondaryName == "target",
                    "RenderGraph did not preserve image copy command summary.")) {
            return false;
        }

        bool foundTransferReadTransition = false;
        for (const asharia::RenderGraphImageTransition& transition : copyPass.transitionsBefore) {
            if (transition.image == source &&
                transition.oldState == asharia::RenderGraphImageState::ColorAttachment &&
                transition.newState == asharia::RenderGraphImageState::TransferSrc) {
                foundTransferReadTransition = true;
            }
        }
        if (!expect(foundTransferReadTransition,
                    "RenderGraph did not transition copy source to TransferSrc.")) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        if (!expect(snapshot.commands.size() == 1,
                    "RenderGraph diagnostics snapshot missed image copy command nodes.")) {
            return false;
        }
        const asharia::RenderGraphDiagnosticsCommandNode& copyCommand = snapshot.commands.front();
        if (!expect(copyCommand.passIndex == 1 && copyCommand.declarationIndex == 1 &&
                        copyCommand.commandIndex == 0 && copyCommand.passName == "CopyImage" &&
                        copyCommand.kind == asharia::RenderGraphCommandKind::CopyImage &&
                        copyCommand.detail == "source -> target",
                    "RenderGraph diagnostics snapshot image copy command node was unexpected.")) {
            return false;
        }

        bool foundTransferReadEdge = false;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passName == "CopyImage" && edge.resourceName == "CopySource" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::TransferRead) {
                foundTransferReadEdge = true;
            }
        }

        return expect(foundTransferReadEdge,
                      "RenderGraph diagnostics snapshot missed the TransferRead edge.");
    }

    [[nodiscard]] bool
    compilesBufferFillCommand(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto target = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "FillTarget",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::TransferRead,
        });

        graph.addPass("FillBuffer", std::string{kBufferTransferWritePass})
            .writeBuffer("target", target)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.fillBuffer("target", 0xA5000000U);
            });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 1,
                    "RenderGraph did not keep the buffer fill pass.")) {
            return false;
        }

        const asharia::RenderGraphCompiledPass& fillPass = compiled->passes.front();
        if (!expect(fillPass.commands.size() == 1 &&
                        fillPass.commands.front().kind ==
                            asharia::RenderGraphCommandKind::FillBuffer &&
                        fillPass.commands.front().name == "target" &&
                        fillPass.commands.front().uintValues[0] == 0xA5000000U,
                    "RenderGraph did not preserve buffer fill command summary.")) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        if (!expect(snapshot.commands.size() == 1,
                    "RenderGraph diagnostics snapshot missed buffer fill command nodes.")) {
            return false;
        }
        const asharia::RenderGraphDiagnosticsCommandNode& fillCommand = snapshot.commands.front();
        return expect(fillCommand.passIndex == 0 && fillCommand.declarationIndex == 0 &&
                          fillCommand.commandIndex == 0 && fillCommand.passName == "FillBuffer" &&
                          fillCommand.kind == asharia::RenderGraphCommandKind::FillBuffer &&
                          fillCommand.detail == "target = 2768240640",
                      "RenderGraph diagnostics snapshot buffer fill command node was unexpected.");
    }

    [[nodiscard]] bool
    compilesBufferTransferCopy(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto source = graph.createTransientBuffer(transientBufferDesc("CopySource"));
        const auto target = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "CopyTarget",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::TransferRead,
        });

        graph.addPass("ProduceCopySource", std::string{kBufferTransferWritePass})
            .writeBuffer("target", source)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.fillBuffer("target", 0xA5000000U);
            });
        graph.addPass("CopyBuffer", std::string{kBufferCopyPass})
            .readTransferBuffer("source", source)
            .writeBuffer("target", target)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBuffer("source", "target");
            });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 2,
                    "RenderGraph did not keep buffer copy producer and copy pass.")) {
            return false;
        }

        const asharia::RenderGraphCompiledPass& copyPass = compiled->passes[1];
        if (!expect(copyPass.bufferTransferReadSlots.size() == 1 &&
                        copyPass.bufferWriteSlots.size() == 1 &&
                        copyPass.bufferTransferReadSlots.front().name == "source" &&
                        copyPass.bufferWriteSlots.front().name == "target",
                    "RenderGraph did not preserve buffer transfer copy slots.")) {
            return false;
        }
        if (!expect(copyPass.commands.size() == 1 &&
                        copyPass.commands.front().kind ==
                            asharia::RenderGraphCommandKind::CopyBuffer &&
                        copyPass.commands.front().name == "source" &&
                        copyPass.commands.front().secondaryName == "target",
                    "RenderGraph did not preserve buffer copy command summary.")) {
            return false;
        }

        bool foundTransferReadTransition = false;
        for (const asharia::RenderGraphBufferTransition& transition :
             copyPass.bufferTransitionsBefore) {
            if (transition.buffer == source &&
                transition.oldState == asharia::RenderGraphBufferState::TransferWrite &&
                transition.newState == asharia::RenderGraphBufferState::TransferRead) {
                foundTransferReadTransition = true;
            }
        }
        if (!expect(foundTransferReadTransition,
                    "RenderGraph did not transition copy source to TransferRead.")) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        if (!expect(snapshot.commands.size() == 2,
                    "RenderGraph diagnostics snapshot missed buffer copy command nodes.")) {
            return false;
        }

        bool foundCopyCommand = false;
        for (const asharia::RenderGraphDiagnosticsCommandNode& command : snapshot.commands) {
            if (command.passName == "CopyBuffer" &&
                command.kind == asharia::RenderGraphCommandKind::CopyBuffer &&
                command.detail == "source -> target") {
                foundCopyCommand = true;
            }
        }
        if (!expect(foundCopyCommand,
                    "RenderGraph diagnostics snapshot buffer copy command node was unexpected.")) {
            return false;
        }

        bool foundTransferReadEdge = false;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passName == "CopyBuffer" && edge.resourceName == "CopySource" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::BufferTransferRead) {
                foundTransferReadEdge = true;
            }
        }

        return expect(foundTransferReadEdge,
                      "RenderGraph diagnostics snapshot missed the BufferTransferRead edge.");
    }

    [[nodiscard]] bool
    verifyImageBufferTransferDiagnostics(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        bool foundUploadCommand = false;
        bool foundReadbackCommand = false;
        bool foundStagingReadEdge = false;
        bool foundImageWriteEdge = false;
        bool foundImageReadEdge = false;
        bool foundReadbackWriteEdge = false;
        for (const asharia::RenderGraphDiagnosticsCommandNode& command : snapshot.commands) {
            if (command.passName == "UploadTexture" &&
                command.kind == asharia::RenderGraphCommandKind::CopyBufferToImage &&
                command.detail == "source -> target") {
                foundUploadCommand = true;
            }
            if (command.passName == "ReadbackTexture" &&
                command.kind == asharia::RenderGraphCommandKind::CopyImageToBuffer &&
                command.detail == "source -> target") {
                foundReadbackCommand = true;
            }
        }
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passName == "UploadTexture" && edge.resourceName == "TextureStaging" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::BufferTransferRead) {
                foundStagingReadEdge = true;
            }
            if (edge.passName == "UploadTexture" && edge.resourceName == "TextureImage" &&
                edge.slotName == "target" &&
                edge.access == asharia::RenderGraphSlotAccess::TransferWrite) {
                foundImageWriteEdge = true;
            }
            if (edge.passName == "ReadbackTexture" && edge.resourceName == "TextureImage" &&
                edge.slotName == "source" &&
                edge.access == asharia::RenderGraphSlotAccess::TransferRead) {
                foundImageReadEdge = true;
            }
            if (edge.passName == "ReadbackTexture" && edge.resourceName == "TextureReadback" &&
                edge.slotName == "target" &&
                edge.access == asharia::RenderGraphSlotAccess::BufferTransferWrite) {
                foundReadbackWriteEdge = true;
            }
        }
        return expect(
            foundUploadCommand && foundReadbackCommand && foundStagingReadEdge &&
                foundImageWriteEdge && foundImageReadEdge && foundReadbackWriteEdge,
            "RenderGraph diagnostics snapshot missed image/buffer copy commands or edges.");
    }

    [[nodiscard]] bool
    compilesImageBufferTransferCopies(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto source = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "TextureStaging",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::TransferRead,
            .finalState = asharia::RenderGraphBufferState::TransferRead,
        });
        const auto image = graph.importImage(asharia::RenderGraphImageDesc{
            .name = "TextureImage",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 8, .height = 8},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::ShaderRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        const auto readback = graph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "TextureReadback",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::Undefined,
            .finalState = asharia::RenderGraphBufferState::HostRead,
        });

        graph.addPass("UploadTexture", std::string{kBufferToImageCopyPass})
            .readTransferBuffer("source", source)
            .writeTransfer("target", image)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBufferToImage("source", "target");
            });
        graph.addPass("ReadbackTexture", std::string{kImageToBufferCopyPass})
            .readTransfer("source", image)
            .writeBuffer("target", readback)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImageToBuffer("source", "target");
            });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 2 && compiled->dependencies.size() == 1,
                    "RenderGraph did not keep image/buffer transfer copy pass order.")) {
            return false;
        }
        if (!expect(compiled->passes[0].commands.size() == 1 &&
                        compiled->passes[0].commands.front().kind ==
                            asharia::RenderGraphCommandKind::CopyBufferToImage &&
                        compiled->passes[1].commands.size() == 1 &&
                        compiled->passes[1].commands.front().kind ==
                            asharia::RenderGraphCommandKind::CopyImageToBuffer,
                    "RenderGraph did not preserve image/buffer transfer copy commands.")) {
            return false;
        }
        if (!expect(compiled->finalTransitions.size() == 1 &&
                        compiled->finalTransitions.front().newState ==
                            asharia::RenderGraphImageState::ShaderRead &&
                        compiled->finalTransitions.front().newShaderStage ==
                            asharia::RenderGraphShaderStage::Fragment &&
                        compiled->finalBufferTransitions.size() == 1 &&
                        compiled->finalBufferTransitions.front().newState ==
                            asharia::RenderGraphBufferState::HostRead,
                    "RenderGraph did not preserve texture upload final states.")) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        return verifyImageBufferTransferDiagnostics(snapshot);
    }

    [[nodiscard]] bool verifyIndexedMeshSlots(const asharia::RenderGraphCompiledPass& drawPass,
                                              asharia::RenderGraphBufferHandle vertices,
                                              asharia::RenderGraphBufferHandle indices) {
        return expect(drawPass.name == "DrawIndexedMesh" &&
                          drawPass.bufferVertexReads ==
                              std::vector<asharia::RenderGraphBufferHandle>{vertices} &&
                          drawPass.bufferIndexReads ==
                              std::vector<asharia::RenderGraphBufferHandle>{indices} &&
                          drawPass.bufferVertexReadSlots.size() == 1 &&
                          drawPass.bufferVertexReadSlots.front().name == "vertices" &&
                          drawPass.bufferVertexReadSlots.front().shaderStage ==
                              asharia::RenderGraphShaderStage::None &&
                          drawPass.bufferIndexReadSlots.size() == 1 &&
                          drawPass.bufferIndexReadSlots.front().name == "indices" &&
                          drawPass.bufferIndexReadSlots.front().shaderStage ==
                              asharia::RenderGraphShaderStage::None,
                      "RenderGraph did not preserve independent vertex/index buffer slots.");
    }

    [[nodiscard]] bool verifyDrawIndexedCommands(const asharia::RenderGraphCompiledPass& drawPass) {
        const asharia::RenderGraphCommand& draw = drawPass.commands.front();
        if (!expect(draw.kind == asharia::RenderGraphCommandKind::DrawIndexed &&
                        draw.uintValues == std::array<std::uint32_t, 3>{36, 1, 0} &&
                        draw.intValue == 0 && draw.uintValue == 0,
                    "RenderGraph did not preserve DrawIndexed defaults.")) {
            return false;
        }

        asharia::RenderGraphCommandList explicitDrawCommands;
        explicitDrawCommands.drawIndexed(12, 3, 4, -2, 5);
        const asharia::RenderGraphCommand& explicitDraw = explicitDrawCommands.commands().front();
        return expect(explicitDraw.uintValues == std::array<std::uint32_t, 3>{12, 3, 4} &&
                          explicitDraw.intValue == -2 && explicitDraw.uintValue == 5,
                      "RenderGraph did not preserve explicit DrawIndexed arguments.");
    }

    [[nodiscard]] bool
    verifyIndexedMeshTransitions(const asharia::RenderGraphCompiledPass& drawPass,
                                 asharia::RenderGraphBufferHandle vertices,
                                 asharia::RenderGraphBufferHandle indices) {
        const auto hasTransition = [&drawPass](asharia::RenderGraphBufferHandle buffer,
                                               asharia::RenderGraphBufferState newState) {
            return std::ranges::any_of(
                drawPass.bufferTransitionsBefore,
                [buffer, newState](const asharia::RenderGraphBufferTransition& transition) {
                    return transition.buffer == buffer &&
                           transition.oldState == asharia::RenderGraphBufferState::TransferWrite &&
                           transition.newState == newState &&
                           transition.newShaderStage == asharia::RenderGraphShaderStage::None;
                });
        };
        return expect(hasTransition(vertices, asharia::RenderGraphBufferState::VertexRead) &&
                          hasTransition(indices, asharia::RenderGraphBufferState::IndexRead),
                      "RenderGraph did not compile vertex/index read transitions.");
    }

    [[nodiscard]] bool
    verifyIndexedMeshDependencies(const asharia::RenderGraphCompileResult& compiled,
                                  asharia::RenderGraphBufferHandle vertices,
                                  asharia::RenderGraphBufferHandle indices) {
        const auto hasDependency = [&compiled](asharia::RenderGraphBufferHandle buffer) {
            return std::ranges::any_of(
                compiled.dependencies,
                [buffer](const asharia::RenderGraphPassDependency& dependency) {
                    return dependency.toDeclarationIndex == 2 &&
                           dependency.resourceKind == asharia::RenderGraphResourceKind::Buffer &&
                           dependency.buffer == buffer;
                });
        };
        return expect(hasDependency(vertices) && hasDependency(indices),
                      "RenderGraph did not connect mesh uploads to indexed draw reads.");
    }

    [[nodiscard]] bool verifyIndexedMeshLifetimes(const asharia::RenderGraphCompileResult& compiled,
                                                  asharia::RenderGraphBufferHandle vertices,
                                                  asharia::RenderGraphBufferHandle indices) {
        const auto hasLifetime = [&compiled](asharia::RenderGraphBufferHandle buffer,
                                             asharia::RenderGraphBufferState finalState) {
            return std::ranges::any_of(
                compiled.transientBuffers,
                [buffer, finalState](const asharia::RenderGraphTransientBufferAllocation& value) {
                    return value.buffer == buffer && value.finalState == finalState &&
                           value.lastPassIndex == 2;
                });
        };
        return expect(hasLifetime(vertices, asharia::RenderGraphBufferState::VertexRead) &&
                          hasLifetime(indices, asharia::RenderGraphBufferState::IndexRead),
                      "RenderGraph did not extend mesh buffer lifetimes through indexed draw.");
    }

    [[nodiscard]] bool
    verifyIndexedMeshDiagnostics(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
        const auto hasAccessEdge = [&snapshot](std::string_view slotName,
                                               asharia::RenderGraphSlotAccess access) {
            return std::ranges::any_of(
                snapshot.accessEdges,
                [slotName, access](const asharia::RenderGraphDiagnosticsAccessEdge& edge) {
                    return edge.passName == "DrawIndexedMesh" && edge.slotName == slotName &&
                           edge.access == access &&
                           edge.shaderStage == asharia::RenderGraphShaderStage::None;
                });
        };
        const bool hasDrawCommand = std::ranges::any_of(
            snapshot.commands, [](const asharia::RenderGraphDiagnosticsCommandNode& command) {
                return command.passName == "DrawIndexedMesh" &&
                       command.kind == asharia::RenderGraphCommandKind::DrawIndexed &&
                       command.detail ==
                           "indexCount=36, instanceCount=1, firstIndex=0, vertexOffset=0, "
                           "firstInstance=0";
            });
        return expect(
            hasAccessEdge("vertices", asharia::RenderGraphSlotAccess::BufferVertexRead) &&
                hasAccessEdge("indices", asharia::RenderGraphSlotAccess::BufferIndexRead) &&
                hasDrawCommand,
            "RenderGraph diagnostics missed indexed mesh slots or command detail.");
    }

    [[nodiscard]] bool
    compilesIndexedMeshContract(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        const auto vertices = graph.createTransientBuffer(transientBufferDesc("MeshVertices"));
        const auto indices = graph.createTransientBuffer(transientBufferDesc("MeshIndices"));

        const auto executeNoOp = [](asharia::RenderGraphPassContext) -> asharia::Result<void> {
            return {};
        };
        graph.addPass("UploadVertices", std::string{kBufferTransferWritePass})
            .writeBuffer("target", vertices)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.fillBuffer("target", 0x01020304U);
            })
            .execute(executeNoOp);
        graph.addPass("UploadIndices", std::string{kBufferTransferWritePass})
            .writeBuffer("target", indices)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.fillBuffer("target", 0x05060708U);
            })
            .execute(executeNoOp);

        bool observedExecutionContext = false;
        graph.addPass("DrawIndexedMesh", std::string{kIndexedMeshPass})
            .readVertexBuffer("vertices", vertices)
            .readIndexBuffer("indices", indices)
            .recordCommands(
                [](asharia::RenderGraphCommandList& commands) { commands.drawIndexed(36); })
            .execute([&observedExecutionContext](
                         asharia::RenderGraphPassContext context) -> asharia::Result<void> {
                observedExecutionContext =
                    context.bufferVertexReads.size() == 1 && context.bufferIndexReads.size() == 1 &&
                    context.bufferVertexReadSlots.size() == 1 &&
                    context.bufferIndexReadSlots.size() == 1 && context.commands.size() == 1 &&
                    context.commands.front().kind == asharia::RenderGraphCommandKind::DrawIndexed;
                return {};
            });

        auto compiled = graph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }
        if (!expect(compiled->passes.size() == 3,
                    "RenderGraph did not keep indexed mesh upload and draw passes.")) {
            return false;
        }

        const asharia::RenderGraphCompiledPass& drawPass = compiled->passes.back();
        if (!verifyIndexedMeshSlots(drawPass, vertices, indices) ||
            !verifyDrawIndexedCommands(drawPass) ||
            !verifyIndexedMeshTransitions(drawPass, vertices, indices) ||
            !verifyIndexedMeshDependencies(*compiled, vertices, indices) ||
            !verifyIndexedMeshLifetimes(*compiled, vertices, indices)) {
            return false;
        }

        const asharia::RenderGraphDiagnosticsSnapshot snapshot =
            graph.diagnosticsSnapshot(*compiled);
        if (!verifyIndexedMeshDiagnostics(snapshot)) {
            return false;
        }

        const std::string debugTables = graph.formatDebugTables(*compiled);
        if (!expect(contains(debugTables, "BufferVertexRead") &&
                        contains(debugTables, "BufferIndexRead") &&
                        contains(debugTables, "DrawIndexed") &&
                        contains(debugTables, "VertexRead") && contains(debugTables, "IndexRead"),
                    "RenderGraph debug tables missed indexed mesh state.")) {
            return false;
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            std::cerr << executed.error().message << '\n';
            return false;
        }
        return expect(observedExecutionContext,
                      "RenderGraph execution context missed indexed mesh slots or command.");
    }

    [[nodiscard]] bool
    rejectsShaderStagesForVertexAndIndexReads(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph vertexGraph;
        static_cast<void>(vertexGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "VertexReadWithShaderStage",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::VertexRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
            .finalState = asharia::RenderGraphBufferState::VertexRead,
        }));
        if (!expectCompileFailure(vertexGraph.compile(schemas),
                                  "VertexRead state must not declare a shader stage",
                                  "VertexRead buffer with shader stage")) {
            return false;
        }

        asharia::RenderGraph indexGraph;
        static_cast<void>(indexGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "IndexReadWithShaderStage",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::IndexRead,
            .finalState = asharia::RenderGraphBufferState::IndexRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
        }));
        return expectCompileFailure(indexGraph.compile(schemas),
                                    "IndexRead state must not declare a shader stage",
                                    "IndexRead buffer with shader stage");
    }

    [[nodiscard]] bool rejectsMissingProducers(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph imageGraph;
        const auto orphanImage = imageGraph.createTransientImage(transientColorDesc("OrphanImage"));
        imageGraph.addPass("ReadOrphanImage", std::string{kTextureReadPass})
            .readTexture("source", orphanImage, asharia::RenderGraphShaderStage::Fragment);
        if (!expectCompileFailure(imageGraph.compile(schemas), "before any pass writes it",
                                  "transient image read without producer")) {
            return false;
        }

        asharia::RenderGraph bufferGraph;
        const auto orphanBuffer =
            bufferGraph.createTransientBuffer(transientBufferDesc("OrphanBuffer"));
        bufferGraph.addPass("ReadOrphanBuffer", std::string{kBufferTransferReadPass})
            .readTransferBuffer("source", orphanBuffer);
        if (!expectCompileFailure(bufferGraph.compile(schemas), "before any pass writes it",
                                  "transient buffer read without producer")) {
            return false;
        }

        asharia::RenderGraph transferImageGraph;
        const auto orphanTransferImage =
            transferImageGraph.createTransientImage(transientColorDesc("OrphanTransferImage"));
        const auto transferTarget = transferImageGraph.importImage(importedColorDesc("Target"));
        transferImageGraph.addPass("CopyOrphanImage", std::string{kImageCopyPass})
            .readTransfer("source", orphanTransferImage)
            .writeTransfer("target", transferTarget)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImage("source", "target");
            });
        return expectCompileFailure(transferImageGraph.compile(schemas),
                                    "before any pass writes it",
                                    "transient transfer image read without producer");
    }

    [[nodiscard]] bool
    rejectsImportedResourcesWithoutFinalState(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph imageGraph;
        const auto importedImage = imageGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "ImportedWithoutFinal",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::ShaderRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::Fragment,
        });
        imageGraph.addPass("ReadImportedWithoutFinal", std::string{kTextureReadPass})
            .readTexture("source", importedImage, asharia::RenderGraphShaderStage::Fragment);
        if (!expectCompileFailure(imageGraph.compile(schemas),
                                  "must declare an explicit final state",
                                  "imported image without final state")) {
            return false;
        }

        asharia::RenderGraph bufferGraph;
        const auto importedBuffer = bufferGraph.importBuffer(asharia::RenderGraphBufferDesc{
            .name = "ImportedBufferWithoutFinal",
            .byteSize = 256,
            .initialState = asharia::RenderGraphBufferState::TransferRead,
        });
        bufferGraph.addPass("ReadImportedBufferWithoutFinal", std::string{kBufferTransferReadPass})
            .readTransferBuffer("source", importedBuffer);
        return expectCompileFailure(bufferGraph.compile(schemas),
                                    "must declare an explicit final state",
                                    "imported buffer without final state");
    }

    [[nodiscard]] bool
    rejectsExecutingCompiledGraphAfterMutation(const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph callbackGraph;
        int callbackCount = 0;
        auto pass = callbackGraph.addPass("MutableCallback", std::string{kSideEffectPass});
        pass.execute([&callbackCount](asharia::RenderGraphPassContext) -> asharia::Result<void> {
            ++callbackCount;
            return {};
        });

        auto compiled = callbackGraph.compile(schemas);
        if (!compiled) {
            std::cerr << compiled.error().message << '\n';
            return false;
        }

        pass.execute([&callbackCount](asharia::RenderGraphPassContext) -> asharia::Result<void> {
            callbackCount += 100;
            return {};
        });
        if (!expectExecuteFailure(callbackGraph.execute(*compiled), "changed since compile",
                                  "compiled pass executed after callback mutation")) {
            return false;
        }
        if (!expect(callbackCount == 0,
                    "RenderGraph ran a callback from a stale compile result.")) {
            return false;
        }

        asharia::RenderGraph resourceGraph;
        resourceGraph.addPass("StablePass", std::string{kSideEffectPass})
            .execute([](asharia::RenderGraphPassContext) -> asharia::Result<void> { return {}; });
        auto resourceCompiled = resourceGraph.compile(schemas);
        if (!resourceCompiled) {
            std::cerr << resourceCompiled.error().message << '\n';
            return false;
        }

        static_cast<void>(resourceGraph.importImage(importedColorDesc("AddedAfterCompile")));
        static_cast<void>(resourceGraph.importBuffer(importedStorageDesc("BufferAfterCompile")));
        return expectExecuteFailure(resourceGraph.execute(*resourceCompiled),
                                    "changed since compile",
                                    "compiled pass executed after resource mutation");
    }

    [[nodiscard]] asharia::RenderGraphSchemaRegistry makeCommandSlotValidationSchemas() {
        asharia::RenderGraphSchemaRegistry schemas;
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "test.invalid-set-texture",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "color",
                        .access = asharia::RenderGraphSlotAccess::ColorWrite,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::SetTexture},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "test.invalid-clear-color",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::ClearColor},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "test.invalid-image-copy",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::TransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyImage},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "test.invalid-buffer-copy",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Compute,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyBuffer},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "test.invalid-buffer-to-image-copy",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::BufferTransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::ShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Fragment,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyBufferToImage},
        });
        schemas.registerSchema(asharia::RenderGraphPassSchema{
            .type = "test.invalid-image-to-buffer-copy",
            .paramsType = {},
            .resourceSlots =
                {
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "source",
                        .access = asharia::RenderGraphSlotAccess::TransferRead,
                        .shaderStage = asharia::RenderGraphShaderStage::None,
                        .optional = false,
                    },
                    asharia::RenderGraphResourceSlotSchema{
                        .name = "target",
                        .access = asharia::RenderGraphSlotAccess::BufferShaderRead,
                        .shaderStage = asharia::RenderGraphShaderStage::Compute,
                        .optional = false,
                    },
                },
            .allowedCommands = {asharia::RenderGraphCommandKind::CopyImageToBuffer},
        });

        return schemas;
    }

    [[nodiscard]] bool rejectsCommandsWithWrongResourceSlots() {
        const asharia::RenderGraphSchemaRegistry schemas = makeCommandSlotValidationSchemas();

        asharia::RenderGraph textureGraph;
        const auto color = textureGraph.importImage(importedColorDesc("ColorTarget"));
        textureGraph.addPass("SetTextureWithColorSlot", "test.invalid-set-texture")
            .writeColor("color", color)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.setTexture("uTexture", "color");
            });
        if (!expectCompileFailure(textureGraph.compile(schemas),
                                  "command 'SetTexture' references invalid slot 'color'",
                                  "setTexture using a color write slot")) {
            return false;
        }

        asharia::RenderGraph clearGraph;
        const auto sampled = clearGraph.importImage(importedSampledDesc("SampledSource"));
        clearGraph.addPass("ClearColorWithReadSlot", "test.invalid-clear-color")
            .readTexture("source", sampled, asharia::RenderGraphShaderStage::Fragment)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.clearColor("source", {0.0F, 0.0F, 0.0F, 1.0F});
            });
        if (!expectCompileFailure(clearGraph.compile(schemas),
                                  "command 'ClearColor' references invalid slot 'source'",
                                  "clearColor using a shader read slot")) {
            return false;
        }

        asharia::RenderGraph imageCopyGraph;
        const auto source = imageCopyGraph.importImage(importedColorDesc("ImageCopySource"));
        const auto target = imageCopyGraph.importImage(importedSampledDesc("ImageCopyTarget"));
        imageCopyGraph.addPass("CopyImageWithReadTarget", "test.invalid-image-copy")
            .readTransfer("source", source)
            .readTexture("target", target, asharia::RenderGraphShaderStage::Fragment)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImage("source", "target");
            });
        if (!expectCompileFailure(imageCopyGraph.compile(schemas),
                                  "command 'CopyImage' references invalid slot 'target'",
                                  "copyImage using a shader read target slot")) {
            return false;
        }

        asharia::RenderGraph bufferCopyGraph;
        const auto bufferSource =
            bufferCopyGraph.importBuffer(importedStorageDesc("BufferCopySource"));
        const auto bufferTarget =
            bufferCopyGraph.importBuffer(importedStorageDesc("BufferCopyTarget"));
        bufferCopyGraph.addPass("CopyBufferWithReadTarget", "test.invalid-buffer-copy")
            .readTransferBuffer("source", bufferSource)
            .readBuffer("target", bufferTarget, asharia::RenderGraphShaderStage::Compute)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBuffer("source", "target");
            });
        if (!expectCompileFailure(bufferCopyGraph.compile(schemas),
                                  "command 'CopyBuffer' references invalid slot 'target'",
                                  "copyBuffer using a buffer shader read target slot")) {
            return false;
        }

        asharia::RenderGraph bufferToImageGraph;
        const auto uploadSource =
            bufferToImageGraph.importBuffer(importedStorageDesc("UploadSource"));
        const auto uploadTarget =
            bufferToImageGraph.importImage(importedSampledDesc("UploadTarget"));
        bufferToImageGraph
            .addPass("CopyBufferToImageWithReadTarget", "test.invalid-buffer-to-image-copy")
            .readTransferBuffer("source", uploadSource)
            .readTexture("target", uploadTarget, asharia::RenderGraphShaderStage::Fragment)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyBufferToImage("source", "target");
            });
        if (!expectCompileFailure(bufferToImageGraph.compile(schemas),
                                  "command 'CopyBufferToImage' references invalid slot 'target'",
                                  "copyBufferToImage using a shader read image target slot")) {
            return false;
        }

        asharia::RenderGraph imageToBufferGraph;
        const auto readbackSource =
            imageToBufferGraph.importImage(importedColorDesc("ReadbackSource"));
        const auto readbackTarget =
            imageToBufferGraph.importBuffer(importedStorageDesc("ReadbackTarget"));
        imageToBufferGraph
            .addPass("CopyImageToBufferWithReadTarget", "test.invalid-image-to-buffer-copy")
            .readTransfer("source", readbackSource)
            .readBuffer("target", readbackTarget, asharia::RenderGraphShaderStage::Compute)
            .recordCommands([](asharia::RenderGraphCommandList& commands) {
                commands.copyImageToBuffer("source", "target");
            });
        return expectCompileFailure(imageToBufferGraph.compile(schemas),
                                    "command 'CopyImageToBuffer' references invalid slot 'target'",
                                    "copyImageToBuffer using a buffer shader read target slot");
    }

    [[nodiscard]] bool rejectsInvalidResourceDeclarations() {
        asharia::RenderGraph undefinedFormatGraph;
        const auto undefinedFormat = undefinedFormatGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "UndefinedFormat",
            .format = asharia::RenderGraphImageFormat::Undefined,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        undefinedFormatGraph.addPass("WriteUndefinedFormat", std::string{kColorWritePass})
            .writeColor("target", undefinedFormat);
        if (!expectCompileFailure(undefinedFormatGraph.compile(makeCompileTestSchemas()),
                                  "must declare a defined format", "image with undefined format")) {
            return false;
        }

        asharia::RenderGraph zeroExtentGraph;
        const auto zeroExtent = zeroExtentGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "ZeroExtent",
            .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = asharia::RenderGraphExtent2D{.width = 0, .height = 64},
            .initialState = asharia::RenderGraphImageState::Undefined,
            .finalState = asharia::RenderGraphImageState::Present,
        });
        zeroExtentGraph.addPass("WriteZeroExtent", std::string{kColorWritePass})
            .writeColor("target", zeroExtent);
        if (!expectCompileFailure(zeroExtentGraph.compile(makeCompileTestSchemas()),
                                  "must declare a non-zero extent", "image with zero extent")) {
            return false;
        }

        asharia::RenderGraph imageShaderStageGraph;
        const auto imageShaderStage =
            imageShaderStageGraph.importImage(asharia::RenderGraphImageDesc{
                .name = "ImageShaderStageNone",
                .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
                .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
                .initialState = asharia::RenderGraphImageState::ShaderRead,
                .initialShaderStage = asharia::RenderGraphShaderStage::None,
                .finalState = asharia::RenderGraphImageState::ShaderRead,
                .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
            });
        imageShaderStageGraph.addPass("ReadShaderStageNone", std::string{kTextureReadPass})
            .readTexture("source", imageShaderStage, asharia::RenderGraphShaderStage::Fragment);
        if (!expectCompileFailure(imageShaderStageGraph.compile(makeCompileTestSchemas()),
                                  "ShaderRead state must declare a shader stage",
                                  "image ShaderRead state with ShaderStage::None")) {
            return false;
        }

        asharia::RenderGraph depthSampledStageGraph;
        static_cast<void>(depthSampledStageGraph.importImage(asharia::RenderGraphImageDesc{
            .name = "DepthSampledStageNone",
            .format = asharia::RenderGraphImageFormat::D32Sfloat,
            .extent = asharia::RenderGraphExtent2D{.width = 64, .height = 64},
            .initialState = asharia::RenderGraphImageState::DepthSampledRead,
            .initialShaderStage = asharia::RenderGraphShaderStage::None,
            .finalState = asharia::RenderGraphImageState::DepthSampledRead,
            .finalShaderStage = asharia::RenderGraphShaderStage::Fragment,
        }));
        if (!expectCompileFailure(depthSampledStageGraph.compile(makeCompileTestSchemas()),
                                  "DepthSampledRead state must declare a shader stage",
                                  "image DepthSampledRead state with ShaderStage::None")) {
            return false;
        }

        asharia::RenderGraph bufferShaderStageGraph;
        const auto bufferShaderStage =
            bufferShaderStageGraph.importBuffer(asharia::RenderGraphBufferDesc{
                .name = "BufferShaderStageNone",
                .byteSize = 256,
                .initialState = asharia::RenderGraphBufferState::StorageReadWrite,
                .initialShaderStage = asharia::RenderGraphShaderStage::None,
                .finalState = asharia::RenderGraphBufferState::StorageReadWrite,
                .finalShaderStage = asharia::RenderGraphShaderStage::Compute,
            });
        bufferShaderStageGraph
            .addPass("ReadWriteShaderStageNone", std::string{kStorageReadWritePass})
            .readWriteStorageBuffer("target", bufferShaderStage,
                                    asharia::RenderGraphShaderStage::Compute);
        return expectCompileFailure(bufferShaderStageGraph.compile(makeCompileTestSchemas()),
                                    "StorageReadWrite state must declare a shader stage",
                                    "buffer StorageReadWrite state with ShaderStage::None");
    }

    [[nodiscard]] bool rejectsForeignCompiledGraph() {
        asharia::RenderGraph first;
        asharia::RenderGraph second;
        bool called = false;
        first.addPass("same").hasSideEffects().execute(
            [](const asharia::RenderGraphPassContext&) -> asharia::Result<void> { return {}; });
        second.addPass("same").hasSideEffects().execute(
            [&called](const asharia::RenderGraphPassContext&) -> asharia::Result<void> {
                called = true;
                return {};
            });
        auto compiled = first.compile();
        if (!expect(compiled && !second.execute(*compiled) && !called,
                    "A foreign compiled graph executed a local callback.")) {
            return false;
        }
        asharia::RenderGraph copied{first};
        if (!expect(!copied.execute(*compiled), "A graph copy retained source identity.")) {
            return false;
        }
        auto moved = std::move(first);
        if (!expect(static_cast<bool>(moved.execute(*compiled)),
                    "Graph move lost compiled identity.")) {
            return false;
        }
        second = moved;
        if (!expect(!second.execute(*compiled), "Copy assignment retained source identity.")) {
            return false;
        }
        second = std::move(moved);
        return expect(static_cast<bool>(second.execute(*compiled)),
                      "Move assignment lost compiled identity.");
    }

    [[nodiscard]] int runRenderGraphCompileTests() {
        const asharia::RenderGraphSchemaRegistry schemas = makeCompileTestSchemas();
        const bool passed =
            rejectsForeignCompiledGraph() && compilesColorAttachmentWriteHazards(schemas) &&
            compilesNonColorImageWriteHazards(schemas) &&
            rejectsColorReadWriteWithoutProducer(schemas) &&
            cullsUnusedTransientButKeepsImportedWrites(schemas) &&
            keepsSideEffectPassAndExecutesIt(schemas) &&
            reordersFutureProducerBeforeConsumer(schemas) &&
            keepsImportedInitialReadBeforeOverwrite(schemas) &&
            buildsDiagnosticsSnapshot(schemas) && compilesImageTransferCopy(schemas) &&
            compilesBufferFillCommand(schemas) && compilesBufferTransferCopy(schemas) &&
            compilesImageBufferTransferCopies(schemas) && compilesIndexedMeshContract(schemas) &&
            rejectsShaderStagesForVertexAndIndexReads(schemas) &&
            rejectsMissingProducers(schemas) &&
            rejectsImportedResourcesWithoutFinalState(schemas) &&
            rejectsExecutingCompiledGraphAfterMutation(schemas) &&
            rejectsCommandsWithWrongResourceSlots() && rejectsInvalidResourceDeclarations();

        if (passed) {
            std::cout << "RenderGraph compile tests passed.\n";
        }

        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the compile-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        return runRenderGraphCompileTests();
    } catch (const std::exception& error) {
        std::cerr << "RenderGraph compile test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "RenderGraph compile test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}
