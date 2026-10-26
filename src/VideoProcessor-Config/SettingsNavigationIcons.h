#pragma once

#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <cmath>

// Original Signal duotone navigation artwork, drawn in a 20-unit optical grid.
// An icon engine repaints the paths at the requested size instead of scaling a
// fixed bitmap, including in the collapsed settings rail and on high-DPI screens.
namespace SettingsNavigationIcons
{
class Engine final : public QIconEngine
{
public:
    explicit Engine(int page) : page_(page) {}
    QIconEngine* clone() const override { return new Engine(page_); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    void paint(QPainter* painter, const QRect& rect,
        QIcon::Mode mode, QIcon::State state) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2,
            rect.y() + (rect.height() - side) / 2);
        painter->scale(side / 20, side / 20);
        const bool disabled = mode == QIcon::Disabled;
        const QColor body(disabled ? "#617787" :
            (state == QIcon::On || mode == QIcon::Selected ? "#deedf7" : "#b4c8d8"));
        const QColor signal(disabled ? "#526d7b" : "#35cce8");
        auto stroke = [&](const QColor& color) {
            painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
        };
        auto fill = [&](const QColor& color) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
        };
        auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
            painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
        };
        auto polyline = [&](std::initializer_list<QPointF> points) {
            painter->drawPolyline(QPolygonF(points));
        };
        stroke(body);
        switch (page_)
        {
        case 0: // General: calibration gear, with a separate cyan center.
        {
            QPolygonF outline;
            constexpr qreal pi = 3.14159265358979323846;
            for (int tooth = 0; tooth < 8; ++tooth)
                for (int point = 0; point < 4; ++point)
                {
                    const qreal angle = (tooth * 45 + point * 11.25 - 5.625) * pi / 180;
                    const qreal radius = point == 1 || point == 2 ? 8 : 6.4;
                    outline << QPointF(10 + std::cos(angle) * radius,
                        10 + std::sin(angle) * radius);
                }
            QPainterPath gear;
            gear.addPolygon(outline);
            gear.closeSubpath();
            gear.addEllipse(QPointF(10, 10), 4.2, 4.2);
            gear.setFillRule(Qt::OddEvenFill);
            fill(body); painter->drawPath(gear);
            fill(signal); painter->drawEllipse(QPointF(10, 10), 2, 2);
            break;
        }
        case 22: // Display: monitor and calibration corner.
            painter->drawRoundedRect(QRectF(2.5, 3.5, 15, 10.5), 1, 1);
            line(10, 14, 10, 17); line(6.5, 17, 13.5, 17);
            stroke(signal); polyline({{13, 6}, {15, 6}, {15, 8}});
            break;
        case 1: // Queue: ordered tracks, with a leading signal chevron.
            for (qreal y : {5.5, 10., 14.5}) line(8.5, y, 17, y);
            stroke(signal); polyline({{3, 6}, {6.5, 10}, {3, 14}});
            break;
        case 5: // DV: paired luminance curves, rather than a calendar.
        {
            QPainterPath compressed;
            compressed.moveTo(2.5, 15.5);
            compressed.cubicTo(8.5, 15.5, 9, 10, 17.5, 10);
            painter->drawPath(compressed);
            QPainterPath expanded;
            expanded.moveTo(2.5, 15.5);
            expanded.cubicTo(10, 15.5, 7, 4, 17.5, 4);
            stroke(signal); painter->drawPath(expanded);
            break;
        }
        case 14: // Shaders: four pixel quadrants and a transformation ray.
            fill(body);
            for (const QPointF& tile : {QPointF(4, 6), QPointF(4, 12), QPointF(10, 12)})
                painter->drawRoundedRect(QRectF(tile, QSizeF(4, 4)), .4, .4);
            fill(signal); painter->drawRoundedRect(QRectF(10, 6, 4, 4), .4, .4);
            stroke(signal); line(14, 4, 17, 1.5);
            break;
        case 7: // Actions: lightning with a small signal segment.
            painter->drawPolygon(QPolygonF{{11.5, 2}, {4.5, 11}, {9, 11},
                {8, 18}, {15.5, 8.5}, {11, 8.5}});
            stroke(signal); line(7.5, 10, 11.5, 10);
            break;
        case 15: // Shortcuts: keyboard, with an accented space key.
            painter->drawRoundedRect(QRectF(2, 5, 16, 10.5), 1, 1);
            fill(body);
            for (qreal x : {4.5, 8., 11.5, 15.})
                painter->drawRoundedRect(QRectF(x, 7.5, 1.5, 2), .25, .25);
            stroke(signal); line(6, 12.5, 14, 12.5);
            break;
        case 10: // Logs: event sheet with an active entry.
            painter->drawRoundedRect(QRectF(4.5, 2.5, 11, 15), 1, 1);
            line(7.5, 10, 12.5, 10); line(7.5, 13.5, 11, 13.5);
            stroke(signal); line(7.5, 6.5, 12.5, 6.5);
            break;
        case 2: // VP Renderer: open raster frame and a four-pixel core.
            polyline({{6.5, 2.5}, {2.5, 2.5}, {2.5, 6.5}});
            polyline({{13.5, 2.5}, {17.5, 2.5}, {17.5, 6.5}});
            polyline({{2.5, 13.5}, {2.5, 17.5}, {6.5, 17.5}});
            polyline({{13.5, 17.5}, {17.5, 17.5}, {17.5, 13.5}});
            fill(body);
            for (const QPointF& tile : {QPointF(6.5, 6.5), QPointF(6.5, 10.5), QPointF(10.5, 10.5)})
                painter->drawRoundedRect(QRectF(tile, QSizeF(3, 3)), .3, .3);
            fill(signal); painter->drawRoundedRect(QRectF(10.5, 6.5, 3, 3), .3, .3);
            break;
        case 3: // DirectShow: offset filter blocks, joined by a signal path.
            stroke(signal); polyline({{8, 6}, {10, 6}, {10, 14}, {12, 14}});
            stroke(body);
            painter->drawRoundedRect(QRectF(2, 3.5, 6, 5), .5, .5);
            painter->drawRoundedRect(QRectF(12, 11.5, 6, 5), .5, .5);
            break;
        case 20: // Updates: incoming signal and an open tray.
            polyline({{3, 12}, {3, 17}, {17, 17}, {17, 12}});
            stroke(signal); line(10, 2.5, 10, 12);
            polyline({{6.5, 8.5}, {10, 12}, {13.5, 8.5}});
            break;
        default:
            painter->drawEllipse(QPointF(10, 10), 6, 6);
            break;
        }
        painter->restore();
    }
private:
    int page_;
};

inline QIcon create(int page) { return QIcon(new Engine(page)); }
}