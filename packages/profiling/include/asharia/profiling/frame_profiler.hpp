#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asharia {

    enum class ProfileTarget {
        Game,
        Scene,
        Preview,
        EditorHost,
        Bench,
    };

    struct FrameProfileInfo {
        std::uint64_t frameIndex{};
        ProfileTarget target{ProfileTarget::Game};
        std::string viewName;
    };

    struct CpuScopeSample {
        std::string name;
        std::uint64_t beginNanoseconds{};
        std::uint64_t endNanoseconds{};
    };

    struct GpuScopeSample {
        std::string name;
        double beginMilliseconds{};
        double endMilliseconds{};
    };

    struct CounterSample {
        std::string name;
        std::uint64_t value{};
    };

    struct FrameProfile {
        FrameProfileInfo info;
        std::vector<CpuScopeSample> cpuScopes;
        std::vector<GpuScopeSample> gpuScopes;
        std::vector<CounterSample> counters;
    };

    struct CpuScopeHandle {
        std::size_t index{};
        bool valid{};
    };

    [[nodiscard]] inline std::string_view profileTargetName(ProfileTarget target) {
        switch (target) {
        case ProfileTarget::Game:
            return "Game";
        case ProfileTarget::Scene:
            return "Scene";
        case ProfileTarget::Preview:
            return "Preview";
        case ProfileTarget::EditorHost:
            return "EditorHost";
        case ProfileTarget::Bench:
            return "Bench";
        }

        return "Unknown";
    }

    class FrameProfiler {
    public:
        explicit FrameProfiler(std::size_t maxFrames = 1024)
            : maxFrames_(maxFrames > 0U ? maxFrames : 1U) {
            frames_.reserve(maxFrames_);
        }

        void beginFrame(FrameProfileInfo info) {
            current_ = FrameProfile{};
            current_.info = std::move(info);
            frameOrigin_ = Clock::now();
            frameActive_ = true;
        }

        void endFrame() {
            if (!frameActive_) {
                return;
            }

            frameActive_ = false;
            if (frames_.size() == maxFrames_) {
                frames_.erase(frames_.begin());
            }
            frames_.push_back(std::move(current_));
        }

        [[nodiscard]] CpuScopeHandle beginCpuScope(std::string_view name) {
            if (!frameActive_) {
                return {};
            }

            const std::size_t index = current_.cpuScopes.size();
            current_.cpuScopes.push_back(CpuScopeSample{
                .name = std::string{name},
                .beginNanoseconds = elapsedNanoseconds(),
                .endNanoseconds = {},
            });
            return CpuScopeHandle{.index = index, .valid = true};
        }

        void endCpuScope(CpuScopeHandle handle) {
            if (!frameActive_ || !handle.valid || handle.index >= current_.cpuScopes.size()) {
                return;
            }

            current_.cpuScopes[handle.index].endNanoseconds = elapsedNanoseconds();
        }

        void addCounter(std::string_view name, std::uint64_t value) {
            if (!frameActive_) {
                return;
            }

            current_.counters.push_back(CounterSample{
                .name = std::string{name},
                .value = value,
            });
        }

        [[nodiscard]] std::span<const FrameProfile> frames() const {
            return frames_;
        }

        [[nodiscard]] const FrameProfile& lastFrame() const {
            return frames_.back();
        }

    private:
        using Clock = std::chrono::steady_clock;

        [[nodiscard]] std::uint64_t elapsedNanoseconds() const {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - frameOrigin_)
                    .count();
            return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U;
        }

        std::size_t maxFrames_{};
        std::vector<FrameProfile> frames_;
        FrameProfile current_;
        Clock::time_point frameOrigin_{};
        bool frameActive_{};
    };

    class CpuProfileScope {
    public:
        CpuProfileScope(FrameProfiler& profiler, std::string_view name)
            : profiler_(&profiler), handle_(profiler.beginCpuScope(name)) {}

        CpuProfileScope(const CpuProfileScope&) = delete;
        CpuProfileScope& operator=(const CpuProfileScope&) = delete;

        CpuProfileScope(CpuProfileScope&& other) noexcept
            : profiler_(std::exchange(other.profiler_, nullptr)), handle_(other.handle_) {
            other.handle_ = {};
        }

        CpuProfileScope& operator=(CpuProfileScope&& other) noexcept {
            if (this != &other) {
                end();
                profiler_ = std::exchange(other.profiler_, nullptr);
                handle_ = other.handle_;
                other.handle_ = {};
            }
            return *this;
        }

        ~CpuProfileScope() {
            end();
        }

    private:
        void end() {
            if (profiler_ != nullptr) {
                profiler_->endCpuScope(handle_);
                profiler_ = nullptr;
                handle_ = {};
            }
        }

        FrameProfiler* profiler_{};
        CpuScopeHandle handle_{};
    };

    inline void writeJsonString(std::ostream& output, std::string_view text) {
        output << '"';
        for (const char character : text) {
            switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << character;
                break;
            }
        }
        output << '"';
    }

    inline void writeFrameProfileJsonl(std::ostream& output,
                                       std::span<const FrameProfile> frames) {
        for (const FrameProfile& frame : frames) {
            output << "{\"type\":\"frame\",\"frameIndex\":" << frame.info.frameIndex
                   << ",\"target\":";
            writeJsonString(output, profileTargetName(frame.info.target));
            output << ",\"viewName\":";
            writeJsonString(output, frame.info.viewName);

            output << ",\"cpuScopes\":[";
            for (std::size_t index = 0; index < frame.cpuScopes.size(); ++index) {
                const CpuScopeSample& scope = frame.cpuScopes[index];
                if (index > 0) {
                    output << ',';
                }
                output << "{\"name\":";
                writeJsonString(output, scope.name);
                output << ",\"beginNanoseconds\":" << scope.beginNanoseconds
                       << ",\"endNanoseconds\":" << scope.endNanoseconds << '}';
            }
            output << ']';

            output << ",\"gpuScopes\":[";
            for (std::size_t index = 0; index < frame.gpuScopes.size(); ++index) {
                const GpuScopeSample& scope = frame.gpuScopes[index];
                if (index > 0) {
                    output << ',';
                }
                output << "{\"name\":";
                writeJsonString(output, scope.name);
                output << ",\"beginMilliseconds\":" << scope.beginMilliseconds
                       << ",\"endMilliseconds\":" << scope.endMilliseconds << '}';
            }
            output << ']';

            output << ",\"counters\":[";
            for (std::size_t index = 0; index < frame.counters.size(); ++index) {
                const CounterSample& counter = frame.counters[index];
                if (index > 0) {
                    output << ',';
                }
                output << "{\"name\":";
                writeJsonString(output, counter.name);
                output << ",\"value\":" << counter.value << '}';
            }
            output << "]}\n";
        }
    }

} // namespace asharia
