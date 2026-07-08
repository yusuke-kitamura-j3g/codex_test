#pragma once

#include "tlscope/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tlscope {

struct SchedstatFields {
    std::uint64_t exec_ns = 0;
    std::uint64_t wait_ns = 0;
    std::uint64_t timeslices = 0;
};

struct TaskStatFields {
    std::string comm;
    char state = '?';
    std::uint64_t utime_ticks = 0;
    std::uint64_t stime_ticks = 0;
    std::uint64_t start_time_ticks = 0;
};

class ProcReader {
public:
    std::vector<int> list_pids() const;
    std::vector<int> list_tids(int pid) const;

    std::optional<TaskIdentity> read_identity(int pid, int tid) const;
    std::optional<SchedstatFields> read_schedstat(int pid, int tid) const;
    std::optional<TaskStatFields> read_task_stat(int pid, int tid) const;
    std::optional<RawTaskSample> read_sample(const TaskIdentity& identity,
                                             bool allow_stat_fallback) const;

    int online_cpu_count() const;
    long clock_ticks_per_second() const;

    static std::uint64_t monotonic_raw_ns();

private:
    static bool is_numeric_name(const char* name);
    static std::string read_small_file(const std::string& path);
    static std::string read_cmdline(int pid);
    static std::vector<std::string> split_ws(const std::string& text);
    static std::string trim(std::string text);
    static int count_cpu_online_range(const std::string& text);
};

} // namespace tlscope
