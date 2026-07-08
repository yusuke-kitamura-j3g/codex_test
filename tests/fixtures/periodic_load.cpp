#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

std::atomic<bool> g_stop { false };

struct Options {
    int threads = 1;
    double busy_ms = 2.0;
    double period_ms = 16.667;
    double duration_sec = 30.0;
    int pin_cpu = -1;
};

void on_signal(int)
{
    g_stop.store(true);
}

int parse_int(const std::string& text, const std::string& option)
{
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + option);
    }
}

double parse_double(const std::string& text, const std::string& option)
{
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid number for " + option);
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

Options parse_args(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads") {
            options.threads = parse_int(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--busy-ms") {
            options.busy_ms = parse_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--period-ms") {
            options.period_ms = parse_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--duration-sec") {
            options.duration_sec = parse_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--pin-cpu") {
            options.pin_cpu = parse_int(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "tlscope-periodic-load --threads N --busy-ms MS --period-ms MS "
                         "--duration-sec SEC [--pin-cpu CPU]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (options.threads <= 0 || options.busy_ms < 0.0 ||
        options.period_ms <= 0.0 || options.duration_sec <= 0.0) {
        throw std::runtime_error("invalid load parameters");
    }
    if (options.busy_ms > options.period_ms) {
        throw std::runtime_error("--busy-ms must be <= --period-ms");
    }
    return options;
}

void pin_current_thread(int cpu)
{
    if (cpu < 0) {
        return;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

long get_tid()
{
    return syscall(SYS_gettid);
}

void worker(const Options& options, int index)
{
    if (options.pin_cpu >= 0) {
        pin_current_thread(options.pin_cpu + index);
    }

    std::cout << "worker index=" << index
              << " pid=" << getpid()
              << " tid=" << get_tid() << '\n';

    using clock = std::chrono::steady_clock;
    const auto busy = std::chrono::duration<double, std::milli>(options.busy_ms);
    const auto period = std::chrono::duration<double, std::milli>(options.period_ms);
    auto next = clock::now();

    volatile double sink = 0.0;
    while (!g_stop.load()) {
        const auto busy_until = clock::now() + busy;
        while (!g_stop.load() && clock::now() < busy_until) {
            sink += std::sin(static_cast<double>(index) + sink);
        }

        next += std::chrono::duration_cast<clock::duration>(period);
        std::this_thread::sleep_until(next);
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        const auto options = parse_args(argc, argv);
        std::cout << "fixture pid=" << getpid()
                  << " threads=" << options.threads
                  << " busy_ms=" << options.busy_ms
                  << " period_ms=" << options.period_ms << '\n';

        std::vector<std::thread> threads;
        for (int i = 0; i < options.threads; ++i) {
            threads.emplace_back(worker, options, i);
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(options.duration_sec));
        g_stop.store(true);

        for (auto& thread : threads) {
            thread.join();
        }
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << '\n';
        return 2;
    }
}
