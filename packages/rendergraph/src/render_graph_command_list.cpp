#include "asharia/rendergraph/render_graph_command_list.hpp"

#include <span>
#include <utility>
#include <vector>

namespace asharia {
    RenderGraphCommandList& RenderGraphCommandList::setShader(std::string shaderAsset,
                                                              std::string shaderPass) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetShader,
            .name = std::move(shaderAsset),
            .secondaryName = std::move(shaderPass),
            .floatValues = {},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setTexture(std::string bindingName,
                                                               std::string slotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetTexture,
            .name = std::move(bindingName),
            .secondaryName = std::move(slotName),
            .floatValues = {},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setFloat(std::string bindingName, float value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetFloat,
            .name = std::move(bindingName),
            .secondaryName = {},
            .floatValues = {value, 0.0F, 0.0F, 0.0F},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setInt(std::string bindingName, int value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetInt,
            .name = std::move(bindingName),
            .secondaryName = {},
            .floatValues = {},
            .intValue = value,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::setVec4(std::string bindingName,
                                                            std::array<float, 4> value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::SetVec4,
            .name = std::move(bindingName),
            .secondaryName = {},
            .floatValues = value,
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::drawFullscreenTriangle() {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::DrawFullscreenTriangle,
            .name = {},
            .secondaryName = {},
            .floatValues = {},
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::clearColor(std::string slotName,
                                                               std::array<float, 4> color) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::ClearColor,
            .name = std::move(slotName),
            .secondaryName = {},
            .floatValues = color,
            .intValue = 0,
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::fillBuffer(std::string slotName,
                                                               std::uint32_t value) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::FillBuffer,
            .name = std::move(slotName),
            .secondaryName = {},
            .floatValues = {},
            .intValue = 0,
            .uintValues = {value, 0, 0},
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::copyImage(std::string sourceSlotName,
                                                              std::string targetSlotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::CopyImage,
            .name = std::move(sourceSlotName),
            .secondaryName = std::move(targetSlotName),
            .floatValues = {},
            .intValue = 0,
            .uintValues = {},
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::copyBuffer(std::string sourceSlotName,
                                                               std::string targetSlotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::CopyBuffer,
            .name = std::move(sourceSlotName),
            .secondaryName = std::move(targetSlotName),
            .floatValues = {},
            .intValue = 0,
            .uintValues = {},
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::copyBufferToImage(std::string sourceSlotName,
                                                                      std::string targetSlotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::CopyBufferToImage,
            .name = std::move(sourceSlotName),
            .secondaryName = std::move(targetSlotName),
            .floatValues = {},
            .intValue = 0,
            .uintValues = {},
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::copyImageToBuffer(std::string sourceSlotName,
                                                                      std::string targetSlotName) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::CopyImageToBuffer,
            .name = std::move(sourceSlotName),
            .secondaryName = std::move(targetSlotName),
            .floatValues = {},
            .intValue = 0,
            .uintValues = {},
        });
        return *this;
    }

    RenderGraphCommandList& RenderGraphCommandList::dispatch(std::uint32_t groupCountX,
                                                             std::uint32_t groupCountY,
                                                             std::uint32_t groupCountZ) {
        commands_.push_back(RenderGraphCommand{
            .kind = RenderGraphCommandKind::Dispatch,
            .name = {},
            .secondaryName = {},
            .floatValues = {},
            .intValue = 0,
            .uintValues = {groupCountX, groupCountY, groupCountZ},
        });
        return *this;
    }

    std::span<const RenderGraphCommand> RenderGraphCommandList::commands() const {
        return commands_;
    }

    std::vector<RenderGraphCommand> RenderGraphCommandList::takeCommands() && {
        return std::move(commands_);
    }

} // namespace asharia
