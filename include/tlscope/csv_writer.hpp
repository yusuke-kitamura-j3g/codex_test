#pragma once

#include "tlscope/aggregate.hpp"
#include "tlscope/types.hpp"

#include <fstream>
#include <string>

namespace tlscope {

class CsvWriter {
public:
    CsvWriter() = default;
    explicit CsvWriter(const std::string& path);

    bool open(const std::string& path);
    bool is_open() const;
    void write(const LoadPoint& point);
    void write_aggregate(const AggregatePoint& point);
    void flush();

private:
    std::ofstream out_;
};

} // namespace tlscope
