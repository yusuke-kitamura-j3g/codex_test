#pragma once

#include "tlscope/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tlscope {

struct AggregatePoint {
    std::uint64_t timestamp_ns = 0;
    double load_percent = 0.0;
    double wait_ratio_percent = 0.0;
    std::uint64_t exec_delta_ns = 0;
    std::uint64_t wait_delta_ns = 0;
    std::uint64_t wall_delta_ns = 0;
    std::size_t process_count = 0;
    std::size_t target_count = 0;
    std::size_t valid_count = 0;
};

struct ProcessAggregate {
    int pid = -1;
    std::string comm;
    std::string cmdline;
    double load_percent = 0.0;
    std::size_t target_count = 0;
    std::size_t valid_count = 0;
};

AggregatePoint aggregate_points(const std::vector<LoadPoint>& points);
std::vector<ProcessAggregate> aggregate_by_process(const std::vector<LoadPoint>& points);

} // namespace tlscope
