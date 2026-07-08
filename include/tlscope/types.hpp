#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tlscope {

enum class SampleSource {
    Schedstat,
    StatFallback
};

struct TaskIdentity {
    int pid = -1;
    int tid = -1;
    std::string comm;
    std::string cmdline;
    std::uint64_t start_time_ticks = 0;
};

struct TaskKey {
    int pid = -1;
    int tid = -1;

    bool operator<(const TaskKey& other) const
    {
        if (pid != other.pid) {
            return pid < other.pid;
        }
        return tid < other.tid;
    }
};

struct RawTaskSample {
    TaskIdentity identity;
    std::uint64_t timestamp_ns = 0;
    std::uint64_t exec_ns = 0;
    std::uint64_t wait_ns = 0;
    std::uint64_t timeslices = 0;
    SampleSource source = SampleSource::Schedstat;
    std::string status = "ok";
};

struct LoadPoint {
    TaskIdentity identity;
    std::uint64_t timestamp_ns = 0;
    double load_percent = 0.0;
    double wait_ratio_percent = 0.0;
    std::uint64_t exec_delta_ns = 0;
    std::uint64_t wait_delta_ns = 0;
    std::uint64_t wall_delta_ns = 0;
    std::uint64_t jitter_ns = 0;
    SampleSource source = SampleSource::Schedstat;
    bool valid = false;
    std::string status = "warmup";
};

struct TargetSpec {
    std::vector<int> pids;
    std::vector<int> tids;
    std::string name_regex;
    std::string thread_regex;
};

struct SamplerConfig {
    double sample_ms = 5.0;
    std::uint64_t refresh_target_ms = 500;
    bool allow_stat_fallback = true;
};

inline const char* source_name(SampleSource source)
{
    return source == SampleSource::Schedstat ? "schedstat" : "stat";
}

} // namespace tlscope
