#include "tlscope/target_resolver.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <set>

namespace tlscope {

namespace {

bool has_text(const std::string& value)
{
    return !value.empty();
}

bool regex_matches(const std::regex* regex, const std::string& value)
{
    return regex == nullptr || std::regex_search(value, *regex);
}

} // namespace

TargetResolver::TargetResolver(const ProcReader& reader)
    : reader_(reader)
{
}

ResolveResult TargetResolver::resolve(const TargetSpec& spec) const
{
    ResolveResult result;

    std::optional<std::regex> name_re;
    std::optional<std::regex> thread_re;
    try {
        if (has_text(spec.name_regex)) {
            name_re.emplace(spec.name_regex);
        }
        if (has_text(spec.thread_regex)) {
            thread_re.emplace(spec.thread_regex);
        }
    } catch (const std::regex_error& err) {
        result.warnings.push_back(std::string("invalid regex: ") + err.what());
        return result;
    }

    if (!spec.pids.empty()) {
        for (int pid : spec.pids) {
            add_pid_targets(pid, spec, result);
        }
    } else if (!spec.tids.empty()) {
        for (int pid : reader_.list_pids()) {
            for (int tid : spec.tids) {
                add_exact_tid(pid, tid, result);
            }
        }
    } else if (name_re || thread_re) {
        const std::regex* name_ptr = name_re ? &*name_re : nullptr;
        const std::regex* thread_ptr = thread_re ? &*thread_re : nullptr;

        for (int pid : reader_.list_pids()) {
            auto process_identity = reader_.read_identity(pid, pid);
            const bool process_match = process_identity &&
                (regex_matches(name_ptr, process_identity->comm) ||
                 regex_matches(name_ptr, process_identity->cmdline));

            if (name_re && !process_match && !thread_re) {
                continue;
            }

            for (int tid : reader_.list_tids(pid)) {
                auto identity = reader_.read_identity(pid, tid);
                if (!identity) {
                    continue;
                }

                const bool thread_match = regex_matches(thread_ptr, identity->comm);
                if (name_re && thread_re) {
                    if (process_match && thread_match) {
                        result.targets.push_back(*identity);
                    }
                } else if (name_re) {
                    if (process_match) {
                        result.targets.push_back(*identity);
                    }
                } else if (thread_re && thread_match) {
                    result.targets.push_back(*identity);
                }
            }
        }
    } else {
        result.warnings.push_back("no target specified; use --pid, --tid, --name-regex, or --thread-regex");
    }

    std::map<TaskKey, TaskIdentity> unique;
    for (const auto& target : result.targets) {
        unique[{target.pid, target.tid}] = target;
    }
    result.targets.clear();
    for (const auto& item : unique) {
        result.targets.push_back(item.second);
    }

    return result;
}

void TargetResolver::add_pid_targets(int pid,
                                     const TargetSpec& spec,
                                     ResolveResult& result) const
{
    std::optional<std::regex> thread_re;
    if (!spec.thread_regex.empty()) {
        thread_re.emplace(spec.thread_regex);
    }

    if (!spec.tids.empty()) {
        for (int tid : spec.tids) {
            add_exact_tid(pid, tid, result);
        }
        return;
    }

    for (int tid : reader_.list_tids(pid)) {
        auto identity = reader_.read_identity(pid, tid);
        if (identity && regex_matches(thread_re ? &*thread_re : nullptr, identity->comm)) {
            result.targets.push_back(*identity);
        }
    }

    if (result.targets.empty()) {
        result.warnings.push_back("pid " + std::to_string(pid) + " has no readable tasks");
    }
}

void TargetResolver::add_exact_tid(int pid,
                                   int tid,
                                   ResolveResult& result) const
{
    auto identity = reader_.read_identity(pid, tid);
    if (identity) {
        result.targets.push_back(*identity);
    }
}

} // namespace tlscope
