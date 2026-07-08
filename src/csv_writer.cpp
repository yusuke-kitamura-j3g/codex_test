#include "tlscope/csv_writer.hpp"

#include <iomanip>

namespace tlscope {

namespace {

std::string escape_csv(const std::string& value)
{
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }
    escaped += '"';
    return escaped;
}

} // namespace

CsvWriter::CsvWriter(const std::string& path)
{
    open(path);
}

bool CsvWriter::open(const std::string& path)
{
    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_) {
        return false;
    }

    out_ << "timestamp_ns,pid,tid,comm,cmdline,load_percent,wait_ratio_percent,"
            "exec_delta_ns,wait_delta_ns,wall_delta_ns,jitter_ns,source,status\n";
    return true;
}

bool CsvWriter::is_open() const
{
    return out_.is_open();
}

void CsvWriter::write(const LoadPoint& point)
{
    if (!out_) {
        return;
    }

    out_ << point.timestamp_ns << ','
         << point.identity.pid << ','
         << point.identity.tid << ','
         << escape_csv(point.identity.comm) << ','
         << escape_csv(point.identity.cmdline) << ','
         << std::fixed << std::setprecision(6) << point.load_percent << ','
         << std::fixed << std::setprecision(6) << point.wait_ratio_percent << ','
         << point.exec_delta_ns << ','
         << point.wait_delta_ns << ','
         << point.wall_delta_ns << ','
         << point.jitter_ns << ','
         << source_name(point.source) << ','
         << escape_csv(point.status) << '\n';
}

void CsvWriter::flush()
{
    if (out_) {
        out_.flush();
    }
}

} // namespace tlscope
