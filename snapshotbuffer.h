#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

// Owned by the Qt event-loop thread. Values stay in firmware wire order.
class SnapshotBuffer {
public:
    static constexpr int Cells = 28 * 56;
    static constexpr int Capacity = 256; // ~5 seconds at 50 Hz
    static constexpr std::int64_t MaxGapNs = 100'000'000;
    using Values = std::array<double, Cells>;
    struct Frame { std::int64_t time; Values values; };
    struct Result {
        Values values;
        std::int64_t before, after;
        double alpha;
    };
    static std::int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void clear() { count_ = next_ = 0; }
    bool append(std::int64_t time, const Values &values) {
        if (count_ && time <= latestTime()) return false;
        frames_[next_] = {time, values};
        next_ = (next_ + 1) % Capacity;
        if (count_ < Capacity) ++count_;
        return true;
    }
    int size() const { return count_; }
    std::int64_t latestTime() const { return at(count_ - 1).time; }
    std::optional<Result> interpolate(std::int64_t target) const {
        for (int i = 0; i < count_; ++i) {
            const auto &after = at(i);
            if (after.time == target)
                return Result{after.values, target, target, 0.0};
            if (after.time > target) {
                if (i == 0) return std::nullopt;
                const auto &before = at(i - 1);
                const auto gap = after.time - before.time;
                if (gap > MaxGapNs) return std::nullopt;
                const double alpha = double(target - before.time) / double(gap);
                Result result{{}, before.time, after.time, alpha};
                for (int cell = 0; cell < Cells; ++cell)
                    result.values[cell] = before.values[cell] + alpha *
                        (after.values[cell] - before.values[cell]);
                return result;
            }
        }
        return std::nullopt; // Never extrapolate.
    }
private:
    const Frame &at(int index) const {
        return frames_[(next_ - count_ + index + Capacity) % Capacity];
    }
    std::array<Frame, Capacity> frames_{};
    int next_ = 0, count_ = 0;
};
