#pragma once

#include "tlscope/proc_reader.hpp"
#include "tlscope/types.hpp"

#include <string>
#include <vector>

namespace tlscope {

struct ResolveResult {
    std::vector<TaskIdentity> targets;
    std::vector<std::string> warnings;
};

class TargetResolver {
public:
    explicit TargetResolver(const ProcReader& reader);

    ResolveResult resolve(const TargetSpec& spec) const;

private:
    const ProcReader& reader_;

    void add_pid_targets(int pid,
                         const TargetSpec& spec,
                         ResolveResult& result) const;
    void add_exact_tid(int pid,
                       int tid,
                       ResolveResult& result) const;
};

} // namespace tlscope
