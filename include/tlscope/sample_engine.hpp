#pragma once

#include "tlscope/proc_reader.hpp"
#include "tlscope/types.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace tlscope {

class SampleEngine {
public:
    explicit SampleEngine(const ProcReader& reader);

    std::vector<LoadPoint> sample_once(const std::vector<TaskIdentity>& targets,
                                       const SamplerConfig& config,
                                       int online_cpu_count,
                                       std::uint64_t expected_sample_ns);

    void reset();

private:
    const ProcReader& reader_;
    std::map<TaskKey, RawTaskSample> previous_;
};

} // namespace tlscope
