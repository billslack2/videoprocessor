#pragma once
#include "ToneCurveChart.h"
#include "ToneCurveProvider.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

// One Colour chart: where the patches land on the display (u'v'), or the
// chosen patch's saturation against its source level. Every number drawn
// comes from the colour model, through libplacebo; the source colour, the
// ring, is the reference, as PQ is for grey.
class ColourChart : public QWidget
{
public:
    enum class Kind { Map, Sweep, Hue };
    // A line or set of markers: the edited profile (bold), libplacebo's
    // default (dashed), or a compared profile. On the map, one point per
    // patch; on the sweep, one per source level.
    struct Series
    {
        QString name;
        QColor color;
        bool bold = false;
        bool dashed = false;
        std::vector<ToneCurveProvider::ColourPoint> points;
    };
    struct Data
    {
        ToneCurveProvider::GamutCorners display, rec709, bt2020;
        QString displayName;
        double mapNits = 50.0;          // the map's source level
        QString mapLabel;               // the picture zone the map shows
        size_t patch = 0;               // the sweep's patch
        std::vector<double> sweepNits;  // the sweep's source levels
        std::vector<Series> series;
    };

    explicit ColourChart(Kind kind, QWidget* parent = nullptr) : QWidget(parent), kind_(kind)
    {
        setMinimumHeight(kind == Kind::Map ? 220 : kind == Kind::Hue ? 170 : 150);
        if (kind == Kind::Hue) setMaximumHeight(210);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName(kind == Kind::Map ? QStringLiteral("Colour map chart") :
            kind == Kind::Hue ? QStringLiteral("Hue shift chart") : QStringLiteral("Saturation sweep chart"));
    }
    void setData(Data chart) { data_ = std::move(chart); valid_ = true; update(); }
    void clear() { valid_ = false; update(); }

    // A swatch for a patch: its own sRGB values, or its primary at full.
    static QColor Swatch(const ToneCurveProvider::Patch& patch)
    {
        const double scale = patch.srgb8 ? 1.0 : 255.0;
        return QColor(static_cast<int>(patch.r * scale), static_cast<int>(patch.g * scale),
            static_cast<int>(patch.b * scale));
    }

protected:
    // Side by side, each at most as tall as it is wide.
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (kind_ == Kind::Hue) return;
        const int cap = std::max(minimumHeight(), width());
        if (maximumHeight() != cap) setMaximumHeight(cap);
    }
    void mouseMoveEvent(QMouseEvent* event) override { hover_ = event->position(); update(); }
    void leaveEvent(QEvent*) override { hover_ = QPointF(-1.0, -1.0); update(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#101b27"))); // his card colour
        painter.setRenderHint(QPainter::Antialiasing);
        QFont chartFont = font();
        chartFont.setPixelSize(11);
        painter.setFont(chartFont);
        const QFontMetrics metrics(chartFont);
        if (!valid_ || data_.series.empty()) return;
        if (kind_ == Kind::Map) paintMap(painter, metrics);
        else if (kind_ == Kind::Hue) paintHue(painter, metrics);
        else paintSweep(painter, metrics);
    }

private:
    static constexpr const char* kGrid = "#1b2a39";
    static constexpr const char* kAxis = "#2d475f";
    static constexpr const char* kInk = "#9fb2c4";

    void paintMap(QPainter& painter, const QFontMetrics& metrics)
    {
        const QRectF area = QRectF(rect()).adjusted(40, 8, -10, -22);
        if (area.width() < 60 || area.height() < 60) return;
        // Fitted to the three gamuts and every patch drawn, with equal scales
        // on both axes, filling the space there is.
        double u0 = 1.0, u1 = 0.0, v0 = 1.0, v1 = 0.0;
        const auto include = [&](double u, double v) {
            u0 = std::min(u0, u); u1 = std::max(u1, u);
            v0 = std::min(v0, v); v1 = std::max(v1, v);
        };
        for (const auto* corners : { &data_.display, &data_.rec709, &data_.bt2020 })
            for (int i = 0; i < 3; ++i) include(corners->u[i], corners->v[i]);
        for (const Series& series : data_.series)
            for (const auto& point : series.points)
            {
                include(point.sourceU, point.sourceV);
                if (point.valid) include(point.u, point.v);
            }
        const double pad = 0.015;
        u0 -= pad; u1 += pad; v0 -= pad; v1 += pad;
        const double scale = std::min(area.width() / (u1 - u0), area.height() / (v1 - v0));
        // Centre the shorter axis's range in the space.
        const double uMid = (u0 + u1) / 2, vMid = (v0 + v1) / 2;
        u0 = uMid - area.width() / scale / 2; u1 = uMid + area.width() / scale / 2;
        v0 = vMid - area.height() / scale / 2; v1 = vMid + area.height() / scale / 2;
        const QRectF plot = area;
        const auto sx = [&](double u) { return plot.left() + (u - u0) * scale; };
        const auto sy = [&](double v) { return plot.bottom() - (v - v0) * scale; };

        const QColor grid(QString::fromLatin1(kGrid)), ink(QString::fromLatin1(kInk));
        for (double u = std::ceil(u0 * 10.0 - 1e-9) / 10.0; u <= u1 + 1e-9; u += 0.1)
        {
            painter.setPen(grid);
            painter.drawLine(QPointF(sx(u), plot.top()), QPointF(sx(u), plot.bottom()));
            const QString label = QString::number(std::abs(u) < 1e-9 ? 0.0 : u, 'f', 1);
            painter.setPen(ink);
            painter.drawText(QPointF(sx(u) - metrics.horizontalAdvance(label) / 2.0, plot.bottom() + 14), label);
        }
        for (double v = std::ceil(v0 * 10.0 - 1e-9) / 10.0; v <= v1 + 1e-9; v += 0.1)
        {
            painter.setPen(grid);
            painter.drawLine(QPointF(plot.left(), sy(v)), QPointF(plot.right(), sy(v)));
            const QString label = QString::number(std::abs(v) < 1e-9 ? 0.0 : v, 'f', 1);
            painter.setPen(ink);
            painter.drawText(QPointF(plot.left() - 6 - metrics.horizontalAdvance(label), sy(v) + 4), label);
        }
        painter.setPen(ink);
        painter.drawText(QPointF(plot.left() - 38, plot.top() + 10), QStringLiteral("v′"));
        painter.drawText(QPointF(plot.right() + 2 - metrics.horizontalAdvance(QStringLiteral("u′")), plot.bottom() - 4), QStringLiteral("u′"));

        painter.save();
        painter.setClipRect(plot);
        const auto triangle = [&](const ToneCurveProvider::GamutCorners& corners, const QPen& pen) {
            QPainterPath path;
            path.moveTo(sx(corners.u[0]), sy(corners.v[0]));
            path.lineTo(sx(corners.u[1]), sy(corners.v[1]));
            path.lineTo(sx(corners.u[2]), sy(corners.v[2]));
            path.closeSubpath();
            painter.strokePath(path, pen);
        };
        const QColor reference(QStringLiteral("#6b8299"));
        triangle(data_.bt2020, QPen(reference, 1.2, Qt::DashLine));
        triangle(data_.rec709, QPen(reference, 1.2, Qt::DotLine));
        triangle(data_.display, QPen(QColor(QStringLiteral("#c9d6e2")), 1.8));
        // The display's white.
        painter.setPen(QPen(QColor(QStringLiteral("#c9d6e2")), 1.2));
        const QPointF white(sx(data_.display.u[3]), sy(data_.display.v[3]));
        painter.drawLine(white - QPointF(4, 0), white + QPointF(4, 0));
        painter.drawLine(white - QPointF(0, 4), white + QPointF(0, 4));

        // Each patch: its source ring in its own colour, a line to the edited
        // profile's point, and a point per line (hollow: clipped). A patch the
        // scene cannot hold this strong is a dim ring only.
        const auto& patches = ToneCurveProvider::Patches();
        const Series& edited = data_.series.front();
        for (size_t i = 0; i < patches.size() && i < edited.points.size(); ++i)
        {
            const auto& source = edited.points[i];
            const QPointF ring(sx(source.sourceU), sy(source.sourceV));
            if (!source.valid)
            {
                painter.setPen(QPen(QColor(QStringLiteral("#3a4d5f")), 1.2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(ring, 5.0, 5.0);
                continue;
            }
            QColor link = edited.color;
            link.setAlpha(110);
            painter.setPen(QPen(link, 1.0));
            painter.drawLine(ring, QPointF(sx(edited.points[i].u), sy(edited.points[i].v)));
            painter.setPen(QPen(Swatch(patches[i]), 2.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(ring, 5.5, 5.5);
        }
        // The edited profile last, on top.
        for (size_t s = data_.series.size(); s-- > 0;)
        {
            const Series& series = data_.series[s];
            for (size_t i = 0; i < series.points.size() && i < patches.size(); ++i)
            {
                const auto& point = series.points[i];
                if (!point.valid) continue;
                marker(painter, QPointF(sx(point.u), sy(point.v)), series, point.clipped);
            }
        }
        painter.restore();

        // Key, top right: the gamuts; the compare chips name the lines.
        const QString displayKey = QStringLiteral("Display (%1)").arg(data_.displayName);
        const QStringList keys = { displayKey, QStringLiteral("BT.2020"), QStringLiteral("Rec.709"),
            QStringLiteral("Ring: source; dot: on screen, hollow if clipped") };
        int keyWidth = 0;
        for (const QString& key : keys) keyWidth = std::max(keyWidth, metrics.horizontalAdvance(key));
        const double keyX = plot.right() - keyWidth - 28, keyY = plot.top() + 12;
        const QPen keyPens[3] = { QPen(QColor(QStringLiteral("#c9d6e2")), 1.8),
            QPen(reference, 1.2, Qt::DashLine), QPen(reference, 1.2, Qt::DotLine) };
        for (int i = 0; i < 4; ++i)
        {
            const double y = keyY + i * 15.0;
            if (i < 3)
            {
                painter.setPen(keyPens[i]);
                painter.drawLine(QPointF(keyX, y - 4), QPointF(keyX + 18, y - 4));
            }
            painter.setPen(ink);
            painter.drawText(QPointF(i < 3 ? keyX + 24 : keyX, y), keys[i]);
        }

        if (hover_.x() >= plot.left() && hover_.x() <= plot.right() &&
            hover_.y() >= plot.top() && hover_.y() <= plot.bottom())
            hoverMap(painter, metrics, plot, sx, sy);
    }

    template<typename X, typename Y>
    void hoverMap(QPainter& painter, const QFontMetrics& metrics, const QRectF& plot, X sx, Y sy)
    {
        // The nearest patch, by its ring or any of its points.
        const auto& patches = ToneCurveProvider::Patches();
        size_t nearest = patches.size();
        double best = 14.0;
        for (const Series& series : data_.series)
            for (size_t i = 0; i < series.points.size() && i < patches.size(); ++i)
            {
                const auto& point = series.points[i];
                for (const QPointF& at : { QPointF(sx(point.sourceU), sy(point.sourceV)), QPointF(sx(point.u), sy(point.v)) })
                {
                    const double distance = std::hypot(at.x() - hover_.x(), at.y() - hover_.y());
                    if (distance < best && (point.valid || at == QPointF(sx(point.sourceU), sy(point.sourceV))))
                    {
                        best = distance;
                        nearest = i;
                    }
                }
            }
        if (nearest >= patches.size()) return;
        QStringList lines;
        lines << QStringLiteral("%1 in %2: strongest channel at %3 nits").arg(QString::fromLatin1(patches[nearest].name),
            data_.mapLabel, ToneCurveChart::Nits(data_.mapNits));
        const auto& source = data_.series.front().points[nearest];
        if (!source.valid)
            lines << QStringLiteral("This scene cannot hold it this strong.");
        else
            for (const Series& series : data_.series)
                if (nearest < series.points.size())
                    lines << Describe(series.name, series.points[nearest]);
        hoverBox(painter, metrics, plot, QPointF(sx(source.sourceU), sy(source.sourceV)), lines);
    }

    void paintSweep(QPainter& painter, const QFontMetrics& metrics)
    {
        const QRectF plot = QRectF(rect()).adjusted(48, 8, -10, -22);
        if (plot.width() < 40 || plot.height() < 40 || data_.sweepNits.size() < 2) return;
        // Source level on a PQ scale, as the Brightness charts.
        const auto toPq = ToneCurveProvider::Registered().nitsToPq;
        const auto scale = [&](double nits) { return toPq ? toPq(nits) : std::log10(nits); };
        const double x0 = scale(data_.sweepNits.front()), x1 = scale(data_.sweepNits.back());
        // Saturation axis: fitted to the drawn values, always showing 1.00.
        double low = 0.8, high = 1.1;
        for (const Series& series : data_.series)
            for (const auto& point : series.points)
                if (point.valid)
                {
                    low = std::min(low, point.saturation);
                    high = std::max(high, point.saturation);
                }
        const double pad = (high - low) * 0.08;
        low -= pad;
        high += pad;
        const auto sx = [&](double nits) { return plot.left() + (scale(nits) - x0) / (x1 - x0) * plot.width(); };
        const auto sy = [&](double s) { return plot.bottom() - (s - low) / (high - low) * plot.height(); };

        const QColor grid(QString::fromLatin1(kGrid)), axis(QString::fromLatin1(kAxis)), ink(QString::fromLatin1(kInk));
        double lastLabel = -1e9;
        for (double nits : { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0 })
        {
            if (nits < data_.sweepNits.front() * 0.999 || nits > data_.sweepNits.back() * 1.001) continue;
            painter.setPen(grid);
            painter.drawLine(QPointF(sx(nits), plot.top()), QPointF(sx(nits), plot.bottom()));
            const QString label = ToneCurveChart::Nits(nits);
            const double width = metrics.horizontalAdvance(label);
            if (sx(nits) - width / 2 < lastLabel + 6) continue;
            painter.setPen(ink);
            painter.drawText(QPointF(sx(nits) - width / 2, plot.bottom() + 14), label);
            lastLabel = sx(nits) + width / 2;
        }
        const double raw = (high - low) / 5.0, magnitude = std::pow(10.0, std::floor(std::log10(raw)));
        double step = magnitude * 10.0;
        for (double factor : { 1.0, 2.0, 2.5, 5.0, 10.0 })
            if (factor * magnitude >= raw) { step = factor * magnitude; break; }
        for (double s = std::ceil(low / step) * step; s <= high + 1e-12; s += step)
        {
            painter.setPen(grid);
            painter.drawLine(QPointF(plot.left(), sy(s)), QPointF(plot.right(), sy(s)));
            const QString label = QString::number(s, 'f', 2);
            painter.setPen(ink);
            painter.drawText(QPointF(plot.left() - 6 - metrics.horizontalAdvance(label), sy(s) + 4), label);
        }
        painter.setPen(axis);
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.setPen(ink);
        const QString xLabel = QStringLiteral("strongest channel, nits");
        painter.drawText(QPointF(plot.right() - metrics.horizontalAdvance(xLabel), height() - 2.0), xLabel);

        painter.save();
        painter.setClipRect(plot.adjusted(-6, -6, 6, 6));
        // 1.00: the source as mastered keeps all its colour.
        QColor referenceLine(QStringLiteral("#c9d6e2"));
        referenceLine.setAlpha(150);
        painter.setPen(QPen(referenceLine, 1.5));
        painter.drawLine(QPointF(plot.left(), sy(1.0)), QPointF(plot.right(), sy(1.0)));
        for (size_t s = data_.series.size(); s-- > 0;)
        {
            const Series& series = data_.series[s];
            QPainterPath path;
            bool open = false;
            for (size_t i = 0; i < series.points.size() && i < data_.sweepNits.size(); ++i)
            {
                const auto& point = series.points[i];
                if (!point.valid) { open = false; continue; }
                const QPointF at(sx(data_.sweepNits[i]), sy(point.saturation));
                if (open) path.lineTo(at); else path.moveTo(at);
                open = true;
            }
            painter.strokePath(path, pen(series));
            for (size_t i = 0; i < series.points.size() && i < data_.sweepNits.size(); ++i)
                if (series.points[i].valid && series.points[i].clipped)
                    marker(painter, QPointF(sx(data_.sweepNits[i]), sy(series.points[i].saturation)), series, true);
        }
        painter.restore();

        // The map's zone.
        const double mapX = sx(std::clamp(data_.mapNits, data_.sweepNits.front(), data_.sweepNits.back()));
        painter.setPen(QPen(ink, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(mapX, plot.top()), QPointF(mapX, plot.bottom()));
        painter.setPen(ink);
        const QString mapLabel = data_.mapLabel;
        painter.drawText(QPointF(mapX + 4, plot.top() + 12), mapLabel);

        // Key: only the reference; the compare chips name every other line.
        const double keyX = plot.left() + 8, keyY = plot.bottom() - 8;
        painter.setPen(QPen(referenceLine, 1.5));
        painter.drawLine(QPointF(keyX, keyY - 4), QPointF(keyX + 16, keyY - 4));
        painter.setPen(ink);
        painter.drawText(QPointF(keyX + 20, keyY), QStringLiteral("1.00: all the source's colour"));

        if (hover_.x() >= plot.left() && hover_.x() <= plot.right() && hover_.y() >= plot.top() && hover_.y() <= plot.bottom())
        {
            size_t index = 0;
            for (size_t i = 1; i < data_.sweepNits.size(); ++i)
                if (std::abs(sx(data_.sweepNits[i]) - hover_.x()) < std::abs(sx(data_.sweepNits[index]) - hover_.x())) index = i;
            QStringList lines;
            lines << QStringLiteral("%1, strongest channel at %2 nits").arg(QString::fromLatin1(ToneCurveProvider::Patches()[data_.patch].name),
                ToneCurveChart::Nits(data_.sweepNits[index]));
            const auto& first = data_.series.front().points;
            if (index < first.size() && !first[index].valid)
                lines << QStringLiteral("This scene cannot hold it this strong.");
            else
                for (const Series& series : data_.series)
                    if (index < series.points.size())
                        lines << Describe(series.name, series.points[index]);
            painter.setPen(QPen(QColor(QStringLiteral("#6f8597")), 1.0));
            painter.drawLine(QPointF(sx(data_.sweepNits[index]), plot.top()), QPointF(sx(data_.sweepNits[index]), plot.bottom()));
            hoverBox(painter, metrics, plot, QPointF(sx(data_.sweepNits[index]), plot.top() + 8), lines);
        }
    }

    // One column per patch at the map's zone: the edited profile's hue turn
    // as a bar in the patch's colour, every other line as a tick in its own.
    void paintHue(QPainter& painter, const QFontMetrics& metrics)
    {
        const auto& patches = ToneCurveProvider::Patches();
        const QRectF plot = QRectF(rect()).adjusted(40, 10, -10, -26);
        if (plot.width() < 60 || plot.height() < 40 || patches.empty()) return;
        // A fixed scale, so turns compare across scenes, zones and profiles;
        // a turn beyond it is drawn to the edge (the hover gives the number).
        constexpr double top = 10.0, step = 5.0;
        const auto sy = [&](double degrees) {
            return plot.center().y() - std::clamp(degrees, -top, top) / top * plot.height() / 2.0; };
        const double column = plot.width() / patches.size();
        const auto cx = [&](size_t i) { return plot.left() + column * (i + 0.5); };

        const QColor grid(QString::fromLatin1(kGrid)), axis(QString::fromLatin1(kAxis)), ink(QString::fromLatin1(kInk));
        for (double d = -top; d <= top + 1e-9; d += step)
        {
            painter.setPen(std::abs(d) < 1e-9 ? axis : grid);
            painter.drawLine(QPointF(plot.left(), sy(d)), QPointF(plot.right(), sy(d)));
            const QString label = (d > 1e-9 ? QStringLiteral("+") : QString()) + QString::number(d, 'g', 3) + QStringLiteral("°");
            painter.setPen(ink);
            painter.drawText(QPointF(plot.left() - 6 - metrics.horizontalAdvance(label), sy(d) + 4), label);
        }
        const auto& edited = data_.series.front().points;
        for (size_t i = 0; i < patches.size(); ++i)
        {
            const bool held = i < edited.size() && edited[i].valid;
            QColor swatch = Swatch(patches[i]);
            if (!held) swatch.setAlpha(60);
            painter.setPen(Qt::NoPen);
            painter.setBrush(swatch);
            const double side = std::min(column - 2.0, 12.0);
            painter.drawRect(QRectF(cx(i) - side / 2, plot.bottom() + 6, side, side));
            if (!held) continue;
            const double barWidth = std::max(column * 0.6, 2.0);
            painter.setBrush(Swatch(patches[i]));
            painter.setPen(QPen(QColor(QStringLiteral("#c9d6e2")), 0.8));
            painter.drawRect(QRectF(QPointF(cx(i) - barWidth / 2, sy(0.0)), QPointF(cx(i) + barWidth / 2, sy(edited[i].hue))).normalized());
        }
        for (size_t s = 1; s < data_.series.size(); ++s)
        {
            const Series& series = data_.series[s];
            painter.setPen(QPen(series.color, 2.0, series.dashed ? Qt::DashLine : Qt::SolidLine));
            for (size_t i = 0; i < patches.size() && i < series.points.size(); ++i)
                if (series.points[i].valid)
                    painter.drawLine(QPointF(cx(i) - column * 0.45, sy(series.points[i].hue)),
                        QPointF(cx(i) + column * 0.45, sy(series.points[i].hue)));
        }

        if (!plot.adjusted(0, 0, 0, 20).contains(hover_)) return;
        const size_t index = std::min(patches.size() - 1, static_cast<size_t>((hover_.x() - plot.left()) / column));
        QStringList lines;
        lines << QStringLiteral("%1 in %2").arg(QString::fromLatin1(patches[index].name), data_.mapLabel);
        if (index >= edited.size() || !edited[index].valid)
            lines << QStringLiteral("This scene cannot hold it this strong.");
        else
            for (const Series& series : data_.series)
                if (index < series.points.size())
                    lines << Describe(series.name, series.points[index]);
        painter.setPen(QPen(QColor(QStringLiteral("#6f8597")), 1.0));
        painter.drawLine(QPointF(cx(index), plot.top()), QPointF(cx(index), plot.bottom()));
        hoverBox(painter, metrics, plot, QPointF(cx(index), plot.top() + 8), lines);
    }

    static QString Describe(const QString& name, const ToneCurveProvider::ColourPoint& point)
    {
        const QString hue = (point.hue > 0.05 ? QStringLiteral("+") : QString()) + QString::number(point.hue, 'f', 1);
        return QStringLiteral("%1: %2 nits, saturation %3, hue %4°%5").arg(name, ToneCurveChart::Nits(point.nits),
            QString::number(point.saturation, 'f', 2), hue, point.clipped ? QStringLiteral(", clipped") : QString());
    }

    static QPen pen(const Series& series)
    {
        return QPen(series.color, series.bold ? 2.8 : 1.6, series.dashed ? Qt::DashLine : Qt::SolidLine);
    }

    static void marker(QPainter& painter, const QPointF& at, const Series& series, bool hollow)
    {
        const double radius = series.bold ? 4.5 : 3.5;
        if (hollow)
        {
            painter.setPen(QPen(series.color, 1.6));
            painter.setBrush(QColor(QStringLiteral("#101b27")));
        }
        else
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(series.color);
        }
        painter.drawEllipse(at, radius, radius);
    }

    void hoverBox(QPainter& painter, const QFontMetrics& metrics, const QRectF& plot, const QPointF& anchor, const QStringList& lines)
    {
        int width = 0;
        for (const QString& text : lines) width = std::max(width, metrics.horizontalAdvance(text));
        const double boxWidth = width + 16.0, boxHeight = lines.size() * 15.0 + 8.0;
        double left = anchor.x() + 12;
        if (left + boxWidth > plot.right()) left = anchor.x() - 12 - boxWidth;
        left = std::max(left, 2.0);
        double top = anchor.y() + 12;
        if (top + boxHeight > plot.bottom()) top = std::max(plot.top(), anchor.y() - 12 - boxHeight);
        const QRectF box(left, top, boxWidth, boxHeight);
        painter.setPen(QPen(QColor(QString::fromLatin1(kAxis)), 1.0));
        painter.setBrush(QColor(QStringLiteral("#0f1a26")));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(QStringLiteral("#e5eef6")));
        for (int i = 0; i < lines.size(); ++i)
            painter.drawText(QPointF(box.left() + 8, box.top() + 16 + i * 15.0), lines[i]);
    }

    Kind kind_;
    Data data_;
    bool valid_ = false;
    QPointF hover_{ -1.0, -1.0 };
};
