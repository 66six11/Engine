#include <cstdlib>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/rendergraph/render_graph.hpp"

namespace {

    constexpr std::string_view kColorWritePass = "test.color-write";
    constexpr std::string_view kTransferWritePass = "test.transfer-write";
    constexpr std::string_view kSamplePresentPass = "test.sample-present";
    constexpr std::string_view kTextureReadPass = "test.texture-read";
    constexpr std::string_view kImageCopyPass = "test.image-copy";
    constexpr std::string_view kStorageReadWritePass = "test.storage-rw";
    constexpr std::string_view kBufferTransferReadPass = "test.buffer-transfer-read";
    constexpr std::string_view kBufferTransferWritePass = "test.buffer-transfer-write";
    constexpr std::string_view kSideEffectPass = "test.side-effect";

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    [[nodiscard]] bool expect(bool condition, std::string_view message) {
        if (!condition) {
            logFailure(message);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool expectCompileFailure(
        const asharia::Result<asharia::RenderGraphCompileResult>& compiled,
        std::string_view expectedMessage, std::string_view context) {
        if (compiled) {
            std::cerr << "RenderGraph accepted invalid graph: " << context << '\n';
            return false;
        }

        if (!contains(compiled.error().message, expectedMessage)) {
            std::cerr << "RenderGraph produced unexpected error for " << context << ": "
                      << compiled.error().message << '\n';
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
            .type = std::string{kSideEffectPass},
            .paramsType = {},
            .resourceSlots = {},
            .allowedCommands = {},
            .allowCulling = true,
            .hasSideEffects = true,
        });

        return schemas;
    }

    [[nodiscard]] bool cullsUnusedTransientButKeepsImportedWrites(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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

    [[nodiscard]] bool keepsSideEffectPassAndExecutesIt(
        const asharia::RenderGraphSchemaRegistry& schemas) {
        asharia::RenderGraph graph;
        int callbackCount = 0;
        graph.addPass("SideEffectMarker", std::string{kSideEffectPass})
            .execute([&callbackCount](asharia::RenderGraphPassContext context)
                         -> asharia::Result<void> {
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

    [[nodiscard]] bool reordersFutureProducerBeforeConsumer(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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

    [[nodiscard]] bool keepsImportedInitialReadBeforeOverwrite(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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

    [[nodiscard]] bool compilesImageTransferCopy(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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
        for (const asharia::RenderGraphImageTransition& transition :
             copyPass.transitionsBefore) {
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
        const asharia::RenderGraphDiagnosticsCommandNode& copyCommand =
            snapshot.commands.front();
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

    [[nodiscard]] bool compilesBufferFillCommand(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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

    [[nodiscard]] bool rejectsMissingProducers(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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

    [[nodiscard]] bool rejectsImportedResourcesWithoutFinalState(
        const asharia::RenderGraphSchemaRegistry& schemas) {
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
        bufferGraph.addPass("ReadImportedBufferWithoutFinal",
                            std::string{kBufferTransferReadPass})
            .readTransferBuffer("source", importedBuffer);
        return expectCompileFailure(bufferGraph.compile(schemas),
                                    "must declare an explicit final state",
                                    "imported buffer without final state");
    }

} // namespace

int main() {
    const asharia::RenderGraphSchemaRegistry schemas = makeCompileTestSchemas();
    const bool passed = cullsUnusedTransientButKeepsImportedWrites(schemas) &&
                        keepsSideEffectPassAndExecutesIt(schemas) &&
                        reordersFutureProducerBeforeConsumer(schemas) &&
                        keepsImportedInitialReadBeforeOverwrite(schemas) &&
                        buildsDiagnosticsSnapshot(schemas) &&
                        compilesImageTransferCopy(schemas) &&
                        compilesBufferFillCommand(schemas) &&
                        rejectsMissingProducers(schemas) &&
                        rejectsImportedResourcesWithoutFinalState(schemas);

    if (passed) {
        std::cout << "RenderGraph compile tests passed.\n";
    }

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
