#pragma once

#include <cstddef>
#include <functional>
#include <span>

namespace asharia::editor::detail {

    // Entries must be ordered by a stable, strictly increasing stream identity.
    // A dispatch advances at most one transition: every completion/close probe
    // runs before any render probe, and the next scan resumes after the stream
    // that made progress.
    template <typename Entry, std::size_t Extent, typename Cursor, typename IdProjection,
              typename PriorityAction, typename RenderAction>
    [[nodiscard]] bool dispatchOneStableRoundRobin(std::span<Entry, Extent> entries, Cursor& cursor,
                                                   IdProjection idProjection,
                                                   PriorityAction priorityAction,
                                                   RenderAction renderAction) {
        if (entries.empty()) {
            return false;
        }

        std::size_t startIndex{};
        while (startIndex < entries.size() &&
               std::invoke(idProjection, entries[startIndex]) <= cursor) {
            ++startIndex;
        }
        if (startIndex == entries.size()) {
            startIndex = 0U;
        }

        const auto dispatchPhase = [&](auto& action) {
            for (std::size_t offset = 0U; offset < entries.size(); ++offset) {
                Entry& entry = entries[(startIndex + offset) % entries.size()];
                if (!std::invoke(action, entry)) {
                    continue;
                }
                cursor = std::invoke(idProjection, entry);
                return true;
            }
            return false;
        };

        if (dispatchPhase(priorityAction)) {
            return true;
        }
        return dispatchPhase(renderAction);
    }

} // namespace asharia::editor::detail
