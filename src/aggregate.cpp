#include "tlscope/aggregate.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace tlscope {

AggregatePoint aggregate_points(const std::vector<LoadPoint>& points)
{
    AggregatePoint aggregate;
    aggregate.target_count = points.size();

    std::set<int> processes;
    for (const auto& point : points) {
        if (point.identity.pid >= 0) {
            processes.insert(point.identity.pid);
        }
        aggregate.timestamp_ns = std::max(aggregate.timestamp_ns, point.timestamp_ns);

        if (!point.valid) {
            continue;
        }

        ++aggregate.valid_count;
        aggregate.load_percent += point.load_percent;
        aggregate.exec_delta_ns += point.exec_delta_ns;
        aggregate.wait_delta_ns += point.wait_delta_ns;
        aggregate.wall_delta_ns += point.wall_delta_ns;
    }

    aggregate.process_count = processes.size();
    if (aggregate.wall_delta_ns > 0) {
        aggregate.wait_ratio_percent =
            100.0 * static_cast<double>(aggregate.wait_delta_ns) /
            static_cast<double>(aggregate.wall_delta_ns);
    }

    return aggregate;
}

std::vector<ProcessAggregate> aggregate_by_process(const std::vector<LoadPoint>& points)
{
    std::map<int, ProcessAggregate> by_pid;
    for (const auto& point : points) {
        if (point.identity.pid < 0) {
            continue;
        }

        auto& process = by_pid[point.identity.pid];
        process.pid = point.identity.pid;
        if (process.comm.empty()) {
            process.comm = point.identity.comm;
        }
        if (process.cmdline.empty()) {
            process.cmdline = point.identity.cmdline;
        }
        ++process.target_count;

        if (point.valid) {
            ++process.valid_count;
            process.load_percent += point.load_percent;
        }
    }

    std::vector<ProcessAggregate> processes;
    processes.reserve(by_pid.size());
    for (auto& item : by_pid) {
        processes.push_back(std::move(item.second));
    }

    std::sort(processes.begin(), processes.end(),
              [](const ProcessAggregate& lhs, const ProcessAggregate& rhs) {
                  if (lhs.load_percent != rhs.load_percent) {
                      return lhs.load_percent > rhs.load_percent;
                  }
                  return lhs.pid < rhs.pid;
              });
    return processes;
}

} // namespace tlscope
