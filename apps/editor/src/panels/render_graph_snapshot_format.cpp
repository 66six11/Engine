#include "panels/render_graph_snapshot_format.hpp"

#include <string>
#include <string_view>

namespace asharia::editor::render_graph_snapshot_format {

    namespace {

        [[nodiscard]] const char* imageLifetimeName(asharia::RenderGraphImageLifetime lifetime) {
            switch (lifetime) {
            case asharia::RenderGraphImageLifetime::Imported:
                return "Imported";
            case asharia::RenderGraphImageLifetime::Transient:
                return "Transient";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* bufferLifetimeName(asharia::RenderGraphBufferLifetime lifetime) {
            switch (lifetime) {
            case asharia::RenderGraphBufferLifetime::Imported:
                return "Imported";
            case asharia::RenderGraphBufferLifetime::Transient:
                return "Transient";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* imageFormatName(asharia::RenderGraphImageFormat format) {
            switch (format) {
            case asharia::RenderGraphImageFormat::Undefined:
                return "Undefined";
            case asharia::RenderGraphImageFormat::B8G8R8A8Srgb:
                return "B8G8R8A8Srgb";
            case asharia::RenderGraphImageFormat::B8G8R8A8Unorm:
                return "B8G8R8A8Unorm";
            case asharia::RenderGraphImageFormat::D32Sfloat:
                return "D32Sfloat";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* shaderStageName(asharia::RenderGraphShaderStage stage) {
            switch (stage) {
            case asharia::RenderGraphShaderStage::None:
                return "";
            case asharia::RenderGraphShaderStage::Fragment:
                return "fragment";
            case asharia::RenderGraphShaderStage::Compute:
                return "compute";
            }
            return "unknown";
        }

        [[nodiscard]] const char* imageStateName(asharia::RenderGraphImageState state) {
            switch (state) {
            case asharia::RenderGraphImageState::Undefined:
                return "Undefined";
            case asharia::RenderGraphImageState::ColorAttachment:
                return "ColorAttachment";
            case asharia::RenderGraphImageState::ShaderRead:
                return "ShaderRead";
            case asharia::RenderGraphImageState::DepthAttachmentRead:
                return "DepthAttachmentRead";
            case asharia::RenderGraphImageState::DepthAttachmentWrite:
                return "DepthAttachmentWrite";
            case asharia::RenderGraphImageState::DepthSampledRead:
                return "DepthSampledRead";
            case asharia::RenderGraphImageState::TransferSrc:
                return "TransferSrc";
            case asharia::RenderGraphImageState::TransferDst:
                return "TransferDst";
            case asharia::RenderGraphImageState::Present:
                return "Present";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* bufferStateName(asharia::RenderGraphBufferState state) {
            switch (state) {
            case asharia::RenderGraphBufferState::Undefined:
                return "Undefined";
            case asharia::RenderGraphBufferState::TransferRead:
                return "TransferRead";
            case asharia::RenderGraphBufferState::TransferWrite:
                return "TransferWrite";
            case asharia::RenderGraphBufferState::HostRead:
                return "HostRead";
            case asharia::RenderGraphBufferState::ShaderRead:
                return "ShaderRead";
            case asharia::RenderGraphBufferState::StorageReadWrite:
                return "StorageReadWrite";
            }
            return "Unknown";
        }

        [[nodiscard]] const char* slotAccessName(asharia::RenderGraphSlotAccess access) {
            switch (access) {
            case asharia::RenderGraphSlotAccess::ColorWrite:
                return "ColorWrite";
            case asharia::RenderGraphSlotAccess::ShaderRead:
                return "ShaderRead";
            case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
                return "DepthAttachmentRead";
            case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
                return "DepthAttachmentWrite";
            case asharia::RenderGraphSlotAccess::DepthSampledRead:
                return "DepthSampledRead";
            case asharia::RenderGraphSlotAccess::TransferRead:
                return "TransferRead";
            case asharia::RenderGraphSlotAccess::TransferWrite:
                return "TransferWrite";
            case asharia::RenderGraphSlotAccess::BufferShaderRead:
                return "BufferShaderRead";
            case asharia::RenderGraphSlotAccess::BufferTransferRead:
                return "BufferTransferRead";
            case asharia::RenderGraphSlotAccess::BufferTransferWrite:
                return "BufferTransferWrite";
            case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
                return "BufferStorageReadWrite";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string imageAccessName(asharia::RenderGraphImageAccess access) {
            std::string name{imageStateName(access.state)};
            const std::string_view stage{shaderStageName(access.shaderStage)};
            if (!stage.empty()) {
                name += "(";
                name += stage;
                name += ")";
            }
            return name;
        }

        [[nodiscard]] std::string bufferAccessName(asharia::RenderGraphBufferAccess access) {
            std::string name{bufferStateName(access.state)};
            const std::string_view stage{shaderStageName(access.shaderStage)};
            if (!stage.empty()) {
                name += "(";
                name += stage;
                name += ")";
            }
            return name;
        }

        [[nodiscard]] bool accessReads(asharia::RenderGraphSlotAccess access) {
            switch (access) {
            case asharia::RenderGraphSlotAccess::ShaderRead:
            case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
            case asharia::RenderGraphSlotAccess::DepthSampledRead:
            case asharia::RenderGraphSlotAccess::TransferRead:
            case asharia::RenderGraphSlotAccess::BufferShaderRead:
            case asharia::RenderGraphSlotAccess::BufferTransferRead:
            case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
                return true;
            case asharia::RenderGraphSlotAccess::ColorWrite:
            case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
            case asharia::RenderGraphSlotAccess::TransferWrite:
            case asharia::RenderGraphSlotAccess::BufferTransferWrite:
                return false;
            }
            return false;
        }

        [[nodiscard]] bool accessWrites(asharia::RenderGraphSlotAccess access) {
            switch (access) {
            case asharia::RenderGraphSlotAccess::ColorWrite:
            case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
            case asharia::RenderGraphSlotAccess::TransferWrite:
            case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
                return true;
            case asharia::RenderGraphSlotAccess::ShaderRead:
            case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
            case asharia::RenderGraphSlotAccess::DepthSampledRead:
            case asharia::RenderGraphSlotAccess::TransferRead:
            case asharia::RenderGraphSlotAccess::BufferShaderRead:
            case asharia::RenderGraphSlotAccess::BufferTransferRead:
                return false;
            }
            return false;
        }

    } // namespace

    const char* boolName(bool value) {
        return value ? "yes" : "no";
    }

    std::string fallbackText(std::string_view value) {
        return value.empty() ? "-" : std::string{value};
    }

    const char* viewportKindName(EditorViewportKind kind) {
        switch (kind) {
        case EditorViewportKind::Scene:
            return "Scene";
        case EditorViewportKind::Game:
            return "Game";
        case EditorViewportKind::Preview:
            return "Preview";
        }
        return "Unknown";
    }

    const char* resourceKindName(asharia::RenderGraphResourceKind kind) {
        switch (kind) {
        case asharia::RenderGraphResourceKind::Image:
            return "Image";
        case asharia::RenderGraphResourceKind::Buffer:
            return "Buffer";
        }
        return "Unknown";
    }

    const char* accessRoleName(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::ColorWrite:
            return "Color";
        case asharia::RenderGraphSlotAccess::ShaderRead:
            return "Sample";
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
            return "Depth R";
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
            return "Depth W";
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
            return "Depth Sample";
        case asharia::RenderGraphSlotAccess::TransferRead:
            return "Copy Src";
        case asharia::RenderGraphSlotAccess::TransferWrite:
            return "Copy Dst";
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
            return "Buffer R";
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
            return "Buffer Src";
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            return "Buffer Dst";
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return "Storage RW";
        }
        return "Unknown";
    }

    const char* accessDirectionName(asharia::RenderGraphSlotAccess access) {
        switch (access) {
        case asharia::RenderGraphSlotAccess::BufferStorageReadWrite:
            return "Read/Write";
        case asharia::RenderGraphSlotAccess::ColorWrite:
        case asharia::RenderGraphSlotAccess::DepthAttachmentWrite:
        case asharia::RenderGraphSlotAccess::TransferWrite:
        case asharia::RenderGraphSlotAccess::BufferTransferWrite:
            return "Write";
        case asharia::RenderGraphSlotAccess::ShaderRead:
        case asharia::RenderGraphSlotAccess::DepthAttachmentRead:
        case asharia::RenderGraphSlotAccess::DepthSampledRead:
        case asharia::RenderGraphSlotAccess::TransferRead:
        case asharia::RenderGraphSlotAccess::BufferShaderRead:
        case asharia::RenderGraphSlotAccess::BufferTransferRead:
            return "Read";
        }
        return "Unknown";
    }

    std::string resourceLabel(asharia::RenderGraphResourceKind kind, std::uint32_t index,
                              std::string_view name) {
        std::string label = kind == asharia::RenderGraphResourceKind::Image ? "#" : "b#";
        label += std::to_string(index);
        label += " ";
        label += fallbackText(name);
        return label;
    }

    std::string passHeaderLabel(const asharia::RenderGraphDiagnosticsPassNode& pass) {
        std::string label = "#" + std::to_string(pass.passIndex);
        if (!pass.name.empty()) {
            label += " ";
            label += pass.name;
        }
        return label;
    }

    AccessCell accessCellFor(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                             const asharia::RenderGraphDiagnosticsResourceNode& resource,
                             const asharia::RenderGraphDiagnosticsPassNode& pass) {
        AccessCell cell;
        for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
            if (edge.passIndex != pass.passIndex || edge.resourceKind != resource.kind ||
                edge.resourceIndex != resource.resourceIndex) {
                continue;
            }

            cell.read = cell.read || accessReads(edge.access);
            cell.write = cell.write || accessWrites(edge.access);
            ++cell.accessCount;

            if (!cell.detail.empty()) {
                cell.detail += "\n";
            }
            cell.detail += edge.slotName;
            cell.detail += ": ";
            cell.detail += accessDirectionName(edge.access);
            cell.detail += " ";
            cell.detail += accessRoleName(edge.access);
            cell.detail += " / ";
            cell.detail += slotAccessName(edge.access);
            const std::string_view stage{shaderStageName(edge.shaderStage)};
            if (!stage.empty()) {
                cell.detail += "(";
                cell.detail += stage;
                cell.detail += ")";
            }
        }
        return cell;
    }

    std::string accessCellLabel(const AccessCell& cell) {
        if (cell.accessCount == 0U) {
            return "-";
        }
        if (cell.read && cell.write) {
            return "rw";
        }
        if (cell.write) {
            return "w";
        }
        return "r";
    }

    std::string resourceShape(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        if (resource.kind == asharia::RenderGraphResourceKind::Image) {
            return std::string{imageFormatName(resource.imageFormat)} + " " +
                   std::to_string(resource.imageExtent.width) + "x" +
                   std::to_string(resource.imageExtent.height);
        }
        return std::to_string(resource.bufferByteSize) + " bytes";
    }

    std::string resourceLifetime(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        return resource.kind == asharia::RenderGraphResourceKind::Image
                   ? imageLifetimeName(resource.imageLifetime)
                   : bufferLifetimeName(resource.bufferLifetime);
    }

    std::string resourceAccessRange(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        if (resource.kind == asharia::RenderGraphResourceKind::Image) {
            return imageAccessName(resource.imageInitialAccess) + " -> " +
                   imageAccessName(resource.imageFinalAccess);
        }
        return bufferAccessName(resource.bufferInitialAccess) + " -> " +
               bufferAccessName(resource.bufferFinalAccess);
    }

    std::string unityResourcePrefix(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
        std::string prefix = resource.kind == asharia::RenderGraphResourceKind::Image ? "T " : "B ";
        if ((resource.kind == asharia::RenderGraphResourceKind::Image &&
             resource.imageLifetime == asharia::RenderGraphImageLifetime::Imported) ||
            (resource.kind == asharia::RenderGraphResourceKind::Buffer &&
             resource.bufferLifetime == asharia::RenderGraphBufferLifetime::Imported)) {
            prefix += "<- ";
        }
        return prefix;
    }

} // namespace asharia::editor::render_graph_snapshot_format
