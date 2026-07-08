#include "tlscope/sample_engine.hpp"

#include <algorithm>
#include <cmath>

namespace tlscope {

SampleEngine::SampleEngine(const ProcReader& reader)
    : reader_(reader)
{
}

std::vector<LoadPoint> SampleEngine::sample_once(const std::vector<TaskIdentity>& targets,
                                                 const SamplerConfig& config,
                                                 int online_cpu_count,
                                                 std::uint64_t expected_sample_ns)
{
    std::vector<LoadPoint> points;
    const int cpu_count = std::max(1, online_cpu_count);

    for (const auto& target : targets) {
        auto raw = reader_.read_sample(target, config.allow_stat_fallback);
        if (!raw) {
            LoadPoint point;
            point.identity = target;
            point.timestamp_ns = ProcReader::monotonic_raw_ns();
            point.status = "read-failed";
            point.valid = false;
            points.push_back(point);
            previous_.erase({target.pid, target.tid});
            continue;
        }

        const TaskKey key { raw->identity.pid, raw->identity.tid };
        LoadPoint point;
        point.identity = raw->identity;
        point.timestamp_ns = raw->timestamp_ns;
        point.source = raw->source;
        point.status = raw->status;

        auto previous = previous_.find(key);
        if (previous == previous_.end()) {
            point.valid = false;
            point.status = "warmup";
            previous_[key] = *raw;
            points.push_back(point);
            continue;
        }

        if (previous->second.identity.start_time_ticks != raw->identity.start_time_ticks) {
            point.valid = false;
            point.status = "identity-changed";
            previous_[key] = *raw;
            points.push_back(point);
            continue;
        }

        if (raw->timestamp_ns <= previous->second.timestamp_ns ||
            raw->exec_ns < previous->second.exec_ns ||
            raw->wait_ns < previous->second.wait_ns) {
            point.valid = false;
            point.status = "counter-reset";
            previous_[key] = *raw;
            points.push_back(point);
            continue;
        }

        point.wall_delta_ns = raw->timestamp_ns - previous->second.timestamp_ns;
        point.exec_delta_ns = raw->exec_ns - previous->second.exec_ns;
        point.wait_delta_ns = raw->wait_ns - previous->second.wait_ns;

        const auto expected = expected_sample_ns == 0 ? point.wall_delta_ns : expected_sample_ns;
        point.jitter_ns = point.wall_delta_ns > expected
            ? point.wall_delta_ns - expected
            : expected - point.wall_delta_ns;

        const long double denominator =
            static_cast<long double>(point.wall_delta_ns) * static_cast<long double>(cpu_count);
        point.load_percent = denominator > 0.0L
            ? static_cast<double>(100.0L * static_cast<long double>(point.exec_delta_ns) / denominator)
            : 0.0;

        point.wait_ratio_percent = point.wall_delta_ns > 0
            ? static_cast<double>(100.0L * static_cast<long double>(point.wait_delta_ns) /
                                  static_cast<long double>(point.wall_delta_ns))
            : 0.0;

        point.valid = true;
        previous_[key] = *raw;
        points.push_back(point);
    }

    return points;
}

void SampleEngine::reset()
{
    previous_.clear();
}

} // namespace tlscope
