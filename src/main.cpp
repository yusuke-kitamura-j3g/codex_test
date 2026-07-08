#include "tlscope/aggregate.hpp"
#include "tlscope/csv_writer.hpp"
#include "tlscope/proc_reader.hpp"
#include "tlscope/sample_engine.hpp"
#include "tlscope/target_resolver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop { false };

struct CliOptions {
    tlscope::TargetSpec target;
    tlscope::SamplerConfig sampler;
    bool list_targets = false;
    double duration_sec = 0.0;
    std::uint64_t window_ms = 500;
    std::uint64_t print_ms = 250;
    std::string csv_path;
    bool aggregate_only = false;
    bool show_process_summary = true;
    bool csv_aggregate = false;
};

void on_signal(int)
{
    g_stop.store(true);
}

void usage(std::ostream& out)
{
    out << "thor-load-scope: lightweight /proc CPU load monitor\n\n"
        << "Target options:\n"
        << "  --pid <pid>                 Monitor all tasks in a process\n"
        << "  --tid <tid>                 Monitor a TID; combine with --pid when possible\n"
        << "  --name-regex <regex>        Match process comm/cmdline\n"
        << "  --thread-regex <regex>      Match thread comm\n"
        << "  --list-targets              Print resolved targets and exit\n\n"
        << "Sampling options:\n"
        << "  --sample-ms <ms>            Sampling interval, default 5\n"
        << "  --refresh-target-ms <ms>    Target rescan interval, default 500\n"
        << "  --no-stat-fallback          Do not fall back to /proc stat\n"
        << "  --duration-sec <sec>        Stop after duration\n\n"
        << "Output options:\n"
        << "  --window-ms <300|500|1000>  Rolling graph window, default 500\n"
        << "  --print-ms <ms>             Terminal refresh interval, default 250\n"
        << "  --csv <path>                Write CSV samples\n"
        << "  --csv-aggregate             Also write aggregate rows to CSV\n"
        << "  --aggregate-only            Print only the combined load line\n"
        << "  --no-process-summary        Hide per-process aggregate rows\n"
        << "  --help                      Show this help\n";
}

int parse_int(const std::string& value, const std::string& option)
{
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + option + ": " + value);
    }
}

double parse_double(const std::string& value, const std::string& option)
{
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid number for " + option + ": " + value);
    }
}

std::string require_value(int& i, int argc, char** argv, const std::string& option)
{
    if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + option);
    }
    ++i;
    return argv[i];
}

CliOptions parse_args(int argc, char** argv)
{
    CliOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            std::exit(0);
        } else if (arg == "--pid") {
            options.target.pids.push_back(parse_int(require_value(i, argc, argv, arg), arg));
        } else if (arg == "--tid") {
            options.target.tids.push_back(parse_int(require_value(i, argc, argv, arg), arg));
        } else if (arg == "--name-regex") {
            options.target.name_regex = require_value(i, argc, argv, arg);
        } else if (arg == "--thread-regex") {
            options.target.thread_regex = require_value(i, argc, argv, arg);
        } else if (arg == "--list-targets") {
            options.list_targets = true;
        } else if (arg == "--sample-ms") {
            options.sampler.sample_ms = parse_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--refresh-target-ms") {
            options.sampler.refresh_target_ms =
                static_cast<std::uint64_t>(parse_int(require_value(i, argc, argv, arg), arg));
        } else if (arg == "--no-stat-fallback") {
            options.sampler.allow_stat_fallback = false;
        } else if (arg == "--duration-sec") {
            options.duration_sec = parse_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--window-ms") {
            options.window_ms =
                static_cast<std::uint64_t>(parse_int(require_value(i, argc, argv, arg), arg));
        } else if (arg == "--print-ms") {
            options.print_ms =
                static_cast<std::uint64_t>(parse_int(require_value(i, argc, argv, arg), arg));
        } else if (arg == "--csv") {
            options.csv_path = require_value(i, argc, argv, arg);
        } else if (arg == "--csv-aggregate") {
            options.csv_aggregate = true;
        } else if (arg == "--aggregate-only") {
            options.aggregate_only = true;
        } else if (arg == "--no-process-summary") {
            options.show_process_summary = false;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (options.sampler.sample_ms <= 0.0) {
        throw std::runtime_error("--sample-ms must be greater than zero");
    }
    if (options.print_ms == 0) {
        throw std::runtime_error("--print-ms must be greater than zero");
    }

    return options;
}

std::uint64_t ns_from_ms(double ms)
{
    return static_cast<std::uint64_t>(ms * 1000000.0);
}

std::string sparkline(const std::deque<double>& values, std::size_t width)
{
    static const char* levels = " .:-=+*#%@";
    if (values.empty() || width == 0) {
        return {};
    }

    const double max_value = std::max(0.01, *std::max_element(values.begin(), values.end()));
    std::string out;
    out.reserve(width);

    for (std::size_t x = 0; x < width; ++x) {
        const std::size_t index = x * values.size() / width;
        const double value = values[std::min(index, values.size() - 1)];
        const auto level = static_cast<std::size_t>(
            std::min<double>(9.0, std::max<double>(0.0, (value / max_value) * 9.0)));
        out.push_back(levels[level]);
    }
    return out;
}

void print_targets(const std::vector<tlscope::TaskIdentity>& targets)
{
    std::cout << "pid,tid,comm,cmdline,start_time_ticks\n";
    for (const auto& target : targets) {
        std::cout << target.pid << ','
                  << target.tid << ','
                  << target.comm << ','
                  << target.cmdline << ','
                  << target.start_time_ticks << '\n';
    }
}

void print_live(const tlscope::AggregatePoint& aggregate,
                const std::vector<tlscope::ProcessAggregate>& processes,
                const std::vector<tlscope::LoadPoint>& points,
                const std::deque<double>& total_history,
                int online_cpus,
                bool aggregate_only,
                bool show_process_summary)
{
    std::cout << "aggregate=" << std::fixed << std::setprecision(3) << aggregate.load_percent
              << "%  processes=" << aggregate.process_count
              << " tasks=" << aggregate.valid_count << "/" << aggregate.target_count
              << "  one_core=" << (100.0 / std::max(1, online_cpus))
              << "%  [" << sparkline(total_history, 48) << "]\n";

    if (aggregate_only) {
        return;
    }

    if (show_process_summary) {
        for (const auto& process : processes) {
            std::cout << "  process pid=" << process.pid
                      << " load=" << std::fixed << std::setprecision(3)
                      << process.load_percent
                      << "% tasks=" << process.valid_count << "/" << process.target_count
                      << " comm=" << process.comm << '\n';
        }
    }

    for (const auto& point : points) {
        std::cout << "  pid=" << point.identity.pid
                  << " tid=" << point.identity.tid
                  << " comm=" << point.identity.comm
                  << " load=" << std::fixed << std::setprecision(3) << point.load_percent
                  << "% wait=" << point.wait_ratio_percent
                  << "% source=" << tlscope::source_name(point.source)
                  << " status=" << point.status << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        const CliOptions options = parse_args(argc, argv);

        tlscope::ProcReader reader;
        tlscope::TargetResolver resolver(reader);
        tlscope::SampleEngine engine(reader);

        auto resolved = resolver.resolve(options.target);
        for (const auto& warning : resolved.warnings) {
            std::cerr << "warning: " << warning << '\n';
        }

        if (options.list_targets) {
            print_targets(resolved.targets);
            return 0;
        }

        if (resolved.targets.empty()) {
            std::cerr << "error: no targets resolved\n";
            usage(std::cerr);
            return 2;
        }

        tlscope::CsvWriter csv;
        if (!options.csv_path.empty() && !csv.open(options.csv_path)) {
            std::cerr << "error: could not open csv: " << options.csv_path << '\n';
            return 2;
        }

        const std::uint64_t sample_ns = ns_from_ms(options.sampler.sample_ms);
        const std::uint64_t print_ns = options.print_ms * 1000000ULL;
        const std::uint64_t refresh_ns = options.sampler.refresh_target_ms * 1000000ULL;
        const std::uint64_t window_ns = options.window_ms * 1000000ULL;
        const std::uint64_t start_ns = tlscope::ProcReader::monotonic_raw_ns();
        const std::uint64_t stop_ns = options.duration_sec > 0.0
            ? start_ns + static_cast<std::uint64_t>(options.duration_sec * 1000000000.0)
            : 0;

        std::uint64_t next_sample_ns = start_ns;
        std::uint64_t next_print_ns = start_ns;
        std::uint64_t next_refresh_ns = start_ns + refresh_ns;
        std::deque<double> total_history;
        std::vector<tlscope::LoadPoint> latest_points;

        std::cout << "resolved_targets=" << resolved.targets.size()
                  << " sample_ms=" << options.sampler.sample_ms
                  << " window_ms=" << options.window_ms << '\n';

        while (!g_stop.load()) {
            const std::uint64_t now_ns = tlscope::ProcReader::monotonic_raw_ns();
            if (stop_ns != 0 && now_ns >= stop_ns) {
                break;
            }

            if (now_ns >= next_refresh_ns) {
                auto refreshed = resolver.resolve(options.target);
                if (!refreshed.targets.empty()) {
                    resolved = std::move(refreshed);
                }
                next_refresh_ns = now_ns + refresh_ns;
            }

            const int online_cpus = reader.online_cpu_count();
            latest_points = engine.sample_once(resolved.targets,
                                               options.sampler,
                                               online_cpus,
                                               sample_ns);
            const auto aggregate = tlscope::aggregate_points(latest_points);
            const auto process_totals = tlscope::aggregate_by_process(latest_points);

            for (const auto& point : latest_points) {
                if (point.valid) {
                    if (csv.is_open()) {
                        csv.write(point);
                    }
                }
            }
            if (csv.is_open() && options.csv_aggregate && aggregate.valid_count > 0) {
                csv.write_aggregate(aggregate);
            }

            total_history.push_back(aggregate.load_percent);
            while (total_history.size() * sample_ns > window_ns && !total_history.empty()) {
                total_history.pop_front();
            }

            if (now_ns >= next_print_ns) {
                print_live(aggregate,
                           process_totals,
                           latest_points,
                           total_history,
                           online_cpus,
                           options.aggregate_only,
                           options.show_process_summary);
                next_print_ns = now_ns + print_ns;
            }

            next_sample_ns += sample_ns;
            const auto after_sample_ns = tlscope::ProcReader::monotonic_raw_ns();
            if (next_sample_ns + sample_ns < after_sample_ns) {
                next_sample_ns = after_sample_ns + sample_ns;
            }
            const std::uint64_t sleep_target = next_sample_ns;
            while (!g_stop.load()) {
                const auto current = tlscope::ProcReader::monotonic_raw_ns();
                if (current >= sleep_target) {
                    break;
                }
                const auto remaining_ns = sleep_target - current;
                const auto sleep_ns = std::min<std::uint64_t>(remaining_ns, 1000000ULL);
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
            }
        }

        csv.flush();
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << '\n';
        usage(std::cerr);
        return 2;
    }
}
