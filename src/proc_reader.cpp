#include "tlscope/proc_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace tlscope {

std::vector<int> ProcReader::list_pids() const
{
    std::vector<int> pids;
    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        return pids;
    }

    while (dirent* entry = readdir(dir)) {
        if (is_numeric_name(entry->d_name)) {
            pids.push_back(std::stoi(entry->d_name));
        }
    }
    closedir(dir);
    std::sort(pids.begin(), pids.end());
    return pids;
}

std::vector<int> ProcReader::list_tids(int pid) const
{
    std::vector<int> tids;
    const std::string path = "/proc/" + std::to_string(pid) + "/task";
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return tids;
    }

    while (dirent* entry = readdir(dir)) {
        if (is_numeric_name(entry->d_name)) {
            tids.push_back(std::stoi(entry->d_name));
        }
    }
    closedir(dir);
    std::sort(tids.begin(), tids.end());
    return tids;
}

std::optional<TaskIdentity> ProcReader::read_identity(int pid, int tid) const
{
    auto stat = read_task_stat(pid, tid);
    if (!stat) {
        return std::nullopt;
    }

    TaskIdentity identity;
    identity.pid = pid;
    identity.tid = tid;
    identity.comm = stat->comm;
    identity.cmdline = read_cmdline(pid);
    identity.start_time_ticks = stat->start_time_ticks;
    return identity;
}

std::optional<SchedstatFields> ProcReader::read_schedstat(int pid, int tid) const
{
    const std::string path = "/proc/" + std::to_string(pid) + "/task/" +
                             std::to_string(tid) + "/schedstat";
    const std::string text = read_small_file(path);
    if (text.empty()) {
        return std::nullopt;
    }

    std::istringstream in(text);
    SchedstatFields fields;
    if (!(in >> fields.exec_ns >> fields.wait_ns >> fields.timeslices)) {
        return std::nullopt;
    }
    return fields;
}

std::optional<TaskStatFields> ProcReader::read_task_stat(int pid, int tid) const
{
    const std::string path = "/proc/" + std::to_string(pid) + "/task/" +
                             std::to_string(tid) + "/stat";
    const std::string text = read_small_file(path);
    if (text.empty()) {
        return std::nullopt;
    }

    const auto open = text.find('(');
    const auto close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return std::nullopt;
    }

    TaskStatFields fields;
    fields.comm = text.substr(open + 1, close - open - 1);

    const std::string rest = trim(text.substr(close + 1));
    const auto tokens = split_ws(rest);
    if (tokens.size() < 20) {
        return std::nullopt;
    }

    fields.state = tokens[0].empty() ? '?' : tokens[0][0];

    try {
        fields.utime_ticks = std::stoull(tokens[11]);
        fields.stime_ticks = std::stoull(tokens[12]);
        fields.start_time_ticks = std::stoull(tokens[19]);
    } catch (const std::exception&) {
        return std::nullopt;
    }

    return fields;
}

std::optional<RawTaskSample> ProcReader::read_sample(const TaskIdentity& identity,
                                                     bool allow_stat_fallback) const
{
    auto current_identity = read_identity(identity.pid, identity.tid);
    if (!current_identity) {
        return std::nullopt;
    }

    RawTaskSample sample;
    sample.identity = *current_identity;
    sample.timestamp_ns = monotonic_raw_ns();

    auto schedstat = read_schedstat(identity.pid, identity.tid);
    if (schedstat) {
        sample.exec_ns = schedstat->exec_ns;
        sample.wait_ns = schedstat->wait_ns;
        sample.timeslices = schedstat->timeslices;
        sample.source = SampleSource::Schedstat;
        sample.status = "ok";
        return sample;
    }

    if (!allow_stat_fallback) {
        return std::nullopt;
    }

    auto stat = read_task_stat(identity.pid, identity.tid);
    if (!stat) {
        return std::nullopt;
    }

    const auto ticks = stat->utime_ticks + stat->stime_ticks;
    const auto hz = clock_ticks_per_second();
    if (hz <= 0) {
        return std::nullopt;
    }

    sample.exec_ns = static_cast<std::uint64_t>(
        (static_cast<long double>(ticks) * 1000000000.0L) / static_cast<long double>(hz));
    sample.wait_ns = 0;
    sample.timeslices = 0;
    sample.source = SampleSource::StatFallback;
    sample.status = "stat-fallback";
    return sample;
}

int ProcReader::online_cpu_count() const
{
    const std::string online = read_small_file("/sys/devices/system/cpu/online");
    const int parsed = count_cpu_online_range(online);
    if (parsed > 0) {
        return parsed;
    }

    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<int>(count) : 1;
}

long ProcReader::clock_ticks_per_second() const
{
    return sysconf(_SC_CLK_TCK);
}

std::uint64_t ProcReader::monotonic_raw_ns()
{
    timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

bool ProcReader::is_numeric_name(const char* name)
{
    if (name == nullptr || *name == '\0') {
        return false;
    }
    for (const char* p = name; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

std::string ProcReader::read_small_file(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        return {};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string ProcReader::read_cmdline(int pid)
{
    std::string text = read_small_file("/proc/" + std::to_string(pid) + "/cmdline");
    for (char& ch : text) {
        if (ch == '\0') {
            ch = ' ';
        }
    }
    return trim(text);
}

std::vector<std::string> ProcReader::split_ws(const std::string& text)
{
    std::vector<std::string> tokens;
    std::istringstream in(text);
    std::string token;
    while (in >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string ProcReader::trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

int ProcReader::count_cpu_online_range(const std::string& text)
{
    int count = 0;
    std::istringstream ranges(text);
    std::string part;
    while (std::getline(ranges, part, ',')) {
        part = trim(part);
        if (part.empty()) {
            continue;
        }

        const auto dash = part.find('-');
        try {
            if (dash == std::string::npos) {
                std::stoi(part);
                ++count;
            } else {
                const int begin = std::stoi(part.substr(0, dash));
                const int end = std::stoi(part.substr(dash + 1));
                if (end >= begin) {
                    count += end - begin + 1;
                }
            }
        } catch (const std::exception&) {
            return 0;
        }
    }
    return count;
}

} // namespace tlscope
