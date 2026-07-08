#include "tlscope/proc_reader.hpp"
#include "tlscope/sample_engine.hpp"
#include "tlscope/target_resolver.hpp"

#include <QApplication>
#include <QPainter>
#include <QPen>
#include <QPolygon>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct QtOptions {
    tlscope::TargetSpec target;
    tlscope::SamplerConfig sampler;
    std::uint64_t window_ms = 500;
};

int parse_int(const QString& text, const QString& option)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok) {
        throw std::runtime_error("invalid integer for " + option.toStdString());
    }
    return value;
}

double parse_double(const QString& text, const QString& option)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok) {
        throw std::runtime_error("invalid number for " + option.toStdString());
    }
    return value;
}

QString require_value(const QStringList& args, int& index, const QString& option)
{
    if (index + 1 >= args.size()) {
        throw std::runtime_error("missing value for " + option.toStdString());
    }
    ++index;
    return args[index];
}

QtOptions parse_options(const QStringList& args)
{
    QtOptions options;
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args[i];
        if (arg == "--pid") {
            options.target.pids.push_back(parse_int(require_value(args, i, arg), arg));
        } else if (arg == "--tid") {
            options.target.tids.push_back(parse_int(require_value(args, i, arg), arg));
        } else if (arg == "--name-regex") {
            options.target.name_regex = require_value(args, i, arg).toStdString();
        } else if (arg == "--thread-regex") {
            options.target.thread_regex = require_value(args, i, arg).toStdString();
        } else if (arg == "--sample-ms") {
            options.sampler.sample_ms = parse_double(require_value(args, i, arg), arg);
        } else if (arg == "--window-ms") {
            options.window_ms = static_cast<std::uint64_t>(
                parse_int(require_value(args, i, arg), arg));
        } else if (arg == "--no-stat-fallback") {
            options.sampler.allow_stat_fallback = false;
        }
    }

    if (options.sampler.sample_ms <= 0.0) {
        throw std::runtime_error("--sample-ms must be greater than zero");
    }
    return options;
}

std::uint64_t ns_from_ms(double ms)
{
    return static_cast<std::uint64_t>(ms * 1000000.0);
}

struct HistoryPoint {
    std::uint64_t timestamp_ns = 0;
    double total_load = 0.0;
};

class MonitorWidget : public QWidget {
public:
    explicit MonitorWidget(QtOptions options, QWidget* parent = nullptr)
        : QWidget(parent),
          options_(std::move(options)),
          resolver_(reader_),
          engine_(reader_)
    {
        setWindowTitle("thor-load-scope");
        setMinimumSize(420, 260);

        auto resolved = resolver_.resolve(options_.target);
        targets_ = std::move(resolved.targets);
        if (targets_.empty()) {
            status_ = "No targets resolved";
        }

        sample_timer_.setInterval(std::max(1, static_cast<int>(options_.sampler.sample_ms)));
        connect(&sample_timer_, &QTimer::timeout, this, [this]() { sample_once(); });
        sample_timer_.start();

        render_timer_.setInterval(33);
        connect(&render_timer_, &QTimer::timeout, this, [this]() { update(); });
        render_timer_.start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(18, 20, 24));

        painter.setPen(QColor(230, 235, 240));
        painter.drawText(12, 22, QString::fromStdString(header_text()));

        const QRect plot = rect().adjusted(54, 38, -16, -38);
        painter.setPen(QColor(78, 86, 98));
        painter.drawRect(plot);

        const double one_core = online_cpus_ > 0 ? 100.0 / online_cpus_ : 100.0;
        const double max_history = max_load();
        const double max_y = std::max({ one_core * 2.0, max_history * 1.2, 1.0 });

        draw_grid(painter, plot, max_y, one_core);
        draw_total_line(painter, plot, max_y);

        painter.setPen(QColor(170, 180, 190));
        painter.drawText(12, height() - 12, QString::fromStdString(status_));
    }

private:
    void sample_once()
    {
        if (targets_.empty()) {
            return;
        }

        online_cpus_ = reader_.online_cpu_count();
        auto points = engine_.sample_once(targets_,
                                          options_.sampler,
                                          online_cpus_,
                                          ns_from_ms(options_.sampler.sample_ms));

        double total = 0.0;
        for (const auto& point : points) {
            if (point.valid) {
                total += point.load_percent;
            }
        }

        const auto now = tlscope::ProcReader::monotonic_raw_ns();
        history_.push_back({ now, total });
        const auto window_ns = options_.window_ms * 1000000ULL;
        while (!history_.empty() && now - history_.front().timestamp_ns > window_ns) {
            history_.pop_front();
        }

        latest_total_ = total;
        status_ = "targets=" + std::to_string(targets_.size()) +
                  " total=" + fixed3(total) + "% sample=" +
                  fixed3(options_.sampler.sample_ms) + "ms";
    }

    std::string header_text() const
    {
        return "window=" + std::to_string(options_.window_ms) + "ms online_cpu=" +
               std::to_string(online_cpus_) + " one_core=" +
               fixed3(online_cpus_ > 0 ? 100.0 / online_cpus_ : 0.0) + "%";
    }

    double max_load() const
    {
        double value = latest_total_;
        for (const auto& point : history_) {
            value = std::max(value, point.total_load);
        }
        return value;
    }

    static std::string fixed3(double value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        return buffer;
    }

    void draw_grid(QPainter& painter, const QRect& plot, double max_y, double one_core)
    {
        painter.setPen(QColor(50, 56, 66));
        for (int i = 0; i <= 4; ++i) {
            const int y = plot.bottom() - (plot.height() * i / 4);
            painter.drawLine(plot.left(), y, plot.right(), y);
            painter.setPen(QColor(150, 160, 170));
            painter.drawText(6, y + 4, QString::number(max_y * i / 4.0, 'f', 1) + "%");
            painter.setPen(QColor(50, 56, 66));
        }

        if (one_core > 0.0 && one_core <= max_y) {
            const int y = plot.bottom() - static_cast<int>((one_core / max_y) * plot.height());
            painter.setPen(QColor(230, 190, 80));
            painter.drawLine(plot.left(), y, plot.right(), y);
        }
    }

    void draw_total_line(QPainter& painter, const QRect& plot, double max_y)
    {
        if (history_.size() < 2 || max_y <= 0.0) {
            return;
        }

        const auto now = history_.back().timestamp_ns;
        const auto window_ns = options_.window_ms * 1000000ULL;
        QPolygon polyline;
        polyline.reserve(static_cast<int>(history_.size()));

        for (const auto& point : history_) {
            const double age = static_cast<double>(now - point.timestamp_ns);
            const double x_ratio = 1.0 - std::min(1.0, age / static_cast<double>(window_ns));
            const double y_ratio = std::min(1.0, point.total_load / max_y);
            const int x = plot.left() + static_cast<int>(x_ratio * plot.width());
            const int y = plot.bottom() - static_cast<int>(y_ratio * plot.height());
            polyline << QPoint(x, y);
        }

        painter.setPen(QPen(QColor(90, 210, 160), 2));
        painter.drawPolyline(polyline);
    }

    QtOptions options_;
    tlscope::ProcReader reader_;
    tlscope::TargetResolver resolver_;
    tlscope::SampleEngine engine_;
    std::vector<tlscope::TaskIdentity> targets_;
    QTimer sample_timer_;
    QTimer render_timer_;
    std::deque<HistoryPoint> history_;
    int online_cpus_ = 1;
    double latest_total_ = 0.0;
    std::string status_ = "starting";
};

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QtOptions options;
    try {
        options = parse_options(app.arguments());
    } catch (const std::exception& err) {
        std::cerr << "error: " << err.what() << '\n';
        return 2;
    }

    MonitorWidget widget(options);
    if (QScreen* screen = QApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        const int width = area.width() / 2;
        const int height = area.height() / 2;
        widget.setGeometry(area.right() - width, area.top(), width, height);
    }
    widget.show();

    return app.exec();
}
