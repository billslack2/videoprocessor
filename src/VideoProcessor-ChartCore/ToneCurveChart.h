#pragma once
#include "ToneCurveProvider.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

// One Brightness chart: the tone curve (source nits in, on-screen nits out) or
// gamma tracking (signed grey ΔE ITP from PQ, 720 × ΔPQ, with its ±1 band).
// Both axes run in PQ, labelled in nits. PQ, the source as mastered, is the one
// reference. Every number drawn comes from libplacebo through the Result.
class ToneCurveChart : public QWidget
{
public:
    enum class Kind { Tone, Tracking };
    struct Data
    {
        std::vector<double> sourcePq;
        ToneCurveProvider::Result result;
        double targetPq = 0.0;     // the display peak, top of the tone chart
        bool sceneAverage = true;  // false: the peak is the metadata peak
        std::vector<std::pair<double, QString>> xTicks; // PQ, label
        std::vector<std::pair<double, QString>> yTicks; // PQ, label (tone chart)
        double (*pqToNits)(double) = nullptr;
        bool showDefault = true; // libplacebo's default curve, switched by its chip
        QString profileName;     // the edited profile, named in the hover
        QColor profileColor = QColor(QStringLiteral("#49b6ff")); // its chip's colour
        // Other profiles drawn for comparison; the chips are the legend.
        struct Compare { QString name; QColor color; std::vector<double> pq; double targetPq = 0.0; };
        std::vector<Compare> compares;
    };

    explicit ToneCurveChart(Kind kind, QWidget* parent = nullptr) : QWidget(parent), kind_(kind)
    {
        setMinimumHeight(150);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(kind == Kind::Tone ? QStringLiteral("Tone curve chart") :
            QStringLiteral("Gamma tracking chart"));
    }
    void setData(Data chart) { data_ = std::move(chart); valid_ = true; update(); }
    void clear() { valid_ = false; update(); }

    static QString Nits(double nits)
    {
        if (!std::isfinite(nits)) return QStringLiteral("-");
        const int decimals = nits < 1 ? 3 : nits < 10 ? 2 : nits < 100 ? 1 : 0;
        return QString::number(nits, 'f', decimals);
    }

protected:
    // No taller than two-thirds of its width: a tall window leaves space below
    // the chart rather than stretching it.
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        const int cap = std::max(minimumHeight(), width() * 2 / 3);
        if (maximumHeight() != cap) setMaximumHeight(cap);
    }
    void mouseMoveEvent(QMouseEvent* event) override { hoverX_ = event->position().x(); update(); }
    void leaveEvent(QEvent*) override { hoverX_ = -1.0; update(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#101b27"))); // his card colour
        painter.setRenderHint(QPainter::Antialiasing);
        // Not "small": the Windows headers define it as a macro.
        QFont chartFont = font();
        chartFont.setPixelSize(11);
        painter.setFont(chartFont);
        const QFontMetrics metrics(chartFont);
        if (!valid_ || data_.sourcePq.empty() ||
            data_.result.profilePq.size() != data_.sourcePq.size())
            return;

        const bool tone = kind_ == Kind::Tone;
        const QRectF plot = QRectF(rect()).adjusted(48, 8, -10, -22);
        if (plot.width() < 40 || plot.height() < 40) return;

        // The y axis: the display peak for the tone curve; fitted to the data
        // up to the scene peak for gamma tracking, so small differences show.
        double targetPq = data_.targetPq;
        for (const auto& compare : data_.compares) targetPq = std::max(targetPq, compare.targetPq);
        double yLow = 0.0, yHigh = targetPq * 1.03;
        std::vector<std::pair<double, QString>> yTicks = data_.yTicks;
        if (!tone)
        {
            double low = -1.5, high = 1.5;
            const auto fit = [&](double out, size_t i) {
                const double delta = Delta(out, data_.sourcePq[i]);
                low = std::min(low, delta);
                high = std::max(high, delta);
            };
            for (size_t i = 0; i < data_.sourcePq.size(); ++i)
            {
                if (data_.sourcePq[i] > data_.result.scenePeakPq) break;
                fit(data_.result.profilePq[i], i);
                if (data_.showDefault) fit(data_.result.defaultPq[i], i);
                for (const auto& compare : data_.compares)
                    if (i < compare.pq.size()) fit(compare.pq[i], i);
            }
            const double pad = (high - low) * 0.08;
            yLow = low - pad;
            yHigh = high + pad;
            yTicks.clear();
            const double raw = (yHigh - yLow) / 5.0, magnitude = std::pow(10.0, std::floor(std::log10(raw)));
            double step = magnitude * 10.0;
            for (double factor : { 1.0, 2.0, 2.5, 5.0, 10.0 })
                if (factor * magnitude >= raw) { step = factor * magnitude; break; }
            const int decimals = std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));
            for (double v = std::ceil(yLow / step) * step; v <= yHigh + 1e-12; v += step)
                yTicks.emplace_back(v, (v > 1e-9 ? QStringLiteral("+") : QString()) +
                    QString::number(std::abs(v) < 1e-9 ? 0.0 : v, 'f', decimals));
        }
        const auto sx = [&](double x) { return plot.left() + x * plot.width(); };
        const auto sy = [&](double y) {
            return plot.bottom() - (y - yLow) / (yHigh - yLow) * plot.height();
        };

        // Grid and labels; a label too close to the last one drawn is skipped.
        const QColor grid(QStringLiteral("#1b2a39")), axis(QStringLiteral("#2d475f"));
        const QColor ink(QStringLiteral("#9fb2c4"));
        double lastLabel = -1e9;
        for (const auto& [x, label] : data_.xTicks)
        {
            painter.setPen(grid);
            painter.drawLine(QPointF(sx(x), plot.top()), QPointF(sx(x), plot.bottom()));
            const double width = metrics.horizontalAdvance(label);
            if (sx(x) - width / 2 < lastLabel + 6) continue;
            painter.setPen(ink);
            painter.drawText(QPointF(sx(x) - width / 2, plot.bottom() + 14), label);
            lastLabel = sx(x) + width / 2;
        }
        lastLabel = 1e9;
        for (const auto& [y, label] : yTicks)
        {
            if (y < yLow || y > yHigh) continue;
            painter.setPen(grid);
            painter.drawLine(QPointF(plot.left(), sy(y)), QPointF(plot.right(), sy(y)));
            if (sy(y) > lastLabel - 12 || sy(y) > plot.bottom() - 6) continue;
            painter.setPen(ink);
            painter.drawText(QPointF(plot.left() - 6 - metrics.horizontalAdvance(label), sy(y) + 4), label);
            lastLabel = sy(y);
        }
        painter.setPen(axis);
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.setPen(ink);
        const QString xLabel = QStringLiteral("source nits");
        painter.drawText(QPointF(plot.right() - metrics.horizontalAdvance(xLabel), height() - 2.0), xLabel);

        // Lines leaving the plot (PQ above the display peak) are cut at its
        // edge, never flattened onto it.
        painter.save();
        painter.setClipRect(plot);
        const QColor reference(QStringLiteral("#c9d6e2"));
        if (!tone)
        {
            QColor band = reference;
            band.setAlpha(28);
            painter.fillRect(QRectF(QPointF(plot.left(), sy(1.0)), QPointF(plot.right(), sy(-1.0))), band);
        }
        const auto line = [&](auto value, const QPen& pen) {
            QPainterPath path;
            for (size_t i = 0; i < data_.sourcePq.size(); ++i)
            {
                const QPointF point(sx(data_.sourcePq[i]), sy(value(i)));
                if (i == 0) path.moveTo(point); else path.lineTo(point);
            }
            painter.strokePath(path, pen);
        };
        QColor referenceLine = reference;
        referenceLine.setAlpha(150);
        const QPen referencePen(referenceLine, 1.5);
        const QPen profilePen(data_.profileColor, 2.8); // bold, as its chip
        QPen defaultPen(QColor(QStringLiteral("#e8a93b")), 1.6, Qt::DashLine);
        const auto plotted = [&](double out, size_t i) { return tone ? out : Delta(out, data_.sourcePq[i]); };
        if (data_.showDefault)
            line([&](size_t i) { return plotted(data_.result.defaultPq[i], i); }, defaultPen);
        if (tone) line([&](size_t i) { return data_.sourcePq[i]; }, referencePen);
        else
        {
            painter.setPen(referencePen);
            painter.drawLine(QPointF(plot.left(), sy(0.0)), QPointF(plot.right(), sy(0.0)));
        }
        for (const auto& compare : data_.compares)
            if (compare.pq.size() == data_.sourcePq.size())
                line([&](size_t i) { return plotted(compare.pq[i], i); }, QPen(compare.color, 1.6));
        line([&](size_t i) { return plotted(data_.result.profilePq[i], i); }, profilePen);
        painter.restore();

        // The scene peak, or the mastering peak without peak detection.
        const double peakX = sx(data_.result.scenePeakPq);
        painter.setPen(QPen(ink, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(peakX, plot.top()), QPointF(peakX, plot.bottom()));
        if (tone && data_.pqToNits)
        {
            const QString peak = (data_.sceneAverage ? QStringLiteral("scene peak ") :
                QStringLiteral("mastering peak ")) + Nits(data_.pqToNits(data_.result.scenePeakPq));
            painter.setPen(ink);
            painter.drawText(QPointF(peakX - 4 - metrics.horizontalAdvance(peak), plot.top() + 12), peak);
        }

        // Key, top left: only PQ; the compare chips name every other line.
        const double keyX = plot.left() + 8, keyY = plot.top() + 12;
        painter.setPen(referencePen);
        painter.drawLine(QPointF(keyX, keyY - 4), QPointF(keyX + 16, keyY - 4));
        painter.setPen(ink);
        painter.drawText(QPointF(keyX + 20, keyY), QStringLiteral("PQ, as mastered"));

        if (hoverX_ >= plot.left() && hoverX_ <= plot.right() && data_.pqToNits)
            drawHover(painter, plot, metrics, sx, sy);
    }

private:
    QString ProfileLabel() const
    {
        return data_.profileName.isEmpty() ? QStringLiteral("Profile") :
            data_.profileName + QStringLiteral(" (editing)");
    }
    // Signed grey ΔE ITP: grey's ICtCp I is its PQ code, so 720 × ΔPQ.
    static double Delta(double outPq, double referencePq) { return 720.0 * (outPq - referencePq); }

    template<typename X, typename Y>
    void drawHover(QPainter& painter, const QRectF& plot, const QFontMetrics& metrics, X sx, Y sy)
    {
        const double x = (hoverX_ - plot.left()) / plot.width();
        size_t index = 0;
        for (size_t i = 1; i < data_.sourcePq.size(); ++i)
            if (std::abs(data_.sourcePq[i] - x) < std::abs(data_.sourcePq[index] - x)) index = i;
        const double source = data_.sourcePq[index];
        const double profile = data_.result.profilePq[index], standard = data_.result.defaultPq[index];
        const auto nits = [&](double pq) { return Nits(data_.pqToNits(pq)); };
        const auto delta = [&](double pq) {
            const double value = Delta(pq, source);
            return (value > 0 ? QStringLiteral("+") : QString()) + QString::number(value, 'f', 1);
        };
        // Every drawn line: the profile, the default if shown, then the compares.
        std::vector<std::tuple<QString, double, QColor>> shown{ { ProfileLabel(), profile, data_.profileColor } };
        if (data_.showDefault) shown.emplace_back(QStringLiteral("Default"), standard, QColor(QStringLiteral("#e8a93b")));
        for (const auto& compare : data_.compares)
            if (index < compare.pq.size()) shown.emplace_back(compare.name, compare.pq[index], compare.color);
        QStringList lines;
        lines << QStringLiteral("%1 nits in").arg(nits(source));
        for (const auto& [name, value, color] : shown)
            lines << (kind_ == Kind::Tone ? QStringLiteral("%1: %2 nits").arg(name, nits(value)) :
                QStringLiteral("%1: ΔE %2 (%3 nits)").arg(name, delta(value), nits(value)));
        if (kind_ == Kind::Tone) lines << QStringLiteral("PQ: %1 nits").arg(nits(source));

        const QColor ink(QStringLiteral("#e5eef6"));
        painter.setPen(QPen(QColor(QStringLiteral("#6f8597")), 1.0));
        painter.drawLine(QPointF(sx(source), plot.top()), QPointF(sx(source), plot.bottom()));
        for (const auto& [name, value, color] : shown)
        {
            const double y = kind_ == Kind::Tone ? value : Delta(value, source);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(sx(source), std::clamp(sy(y), plot.top(), plot.bottom())), 3.5, 3.5);
        }
        int width = 0;
        for (const QString& text : lines) width = std::max(width, metrics.horizontalAdvance(text));
        const double boxWidth = width + 16.0, boxHeight = lines.size() * 15.0 + 8.0;
        double left = sx(source) + 12;
        if (left + boxWidth > plot.right()) left = sx(source) - 12 - boxWidth;
        const QRectF box(left, plot.top() + 20, boxWidth, boxHeight);
        painter.setPen(QPen(QColor(QStringLiteral("#2d475f")), 1.0));
        painter.setBrush(QColor(QStringLiteral("#0f1a26")));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(ink);
        for (int i = 0; i < lines.size(); ++i)
            painter.drawText(QPointF(box.left() + 8, box.top() + 16 + i * 15.0), lines[i]);
    }

    Kind kind_;
    Data data_;
    bool valid_ = false;
    double hoverX_ = -1.0;
};
