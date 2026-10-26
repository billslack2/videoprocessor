#pragma once

#include <QIcon>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <cmath>

// Faithful vector reconstruction of the approved Option 02 Signal duotone
// reference: filled cool-gray geometry, shaded panels and cyan signal details.
// Paint at the requested resolution for the sidebar, collapsed rail and DPI scale.
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
        QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2,
            rect.y() + (rect.height() - side) / 2);
        painter->scale(side / 20, side / 20);
        if (mode == QIcon::Disabled) painter->setOpacity(.45);
        const QColor light("#bfd3e2"), gray("#91aec4"), shade("#577b96");
        const QColor inset("#183247"), cyan("#35c8e9"), highlight("#8ae4f4");
        auto stroke = [&](const QColor& color, qreal width = 1.4) {
            painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
        };
        auto fill = [&](const QColor& color) {
            painter->setPen(Qt::NoPen); painter->setBrush(color);
        };
        auto shaded = [&](const QColor& top, const QColor& bottom, qreal y1, qreal y2) {
            QLinearGradient gradient(0, y1, 0, y2);
            gradient.setColorAt(0, top); gradient.setColorAt(1, bottom);
            painter->setPen(Qt::NoPen); painter->setBrush(gradient);
        };
        auto polygon = [&](std::initializer_list<QPointF> points) {
            painter->drawPolygon(QPolygonF(points));
        };
        auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
            painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
        };
        switch (page_)
        {
        case 0: // General: filled calibration gear and cyan center.
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
            gear.addPolygon(outline); gear.closeSubpath();
            gear.addEllipse(QPointF(10, 10), 4.1, 4.1);
            gear.setFillRule(Qt::OddEvenFill);
            shaded(light, shade, 2, 18); painter->drawPath(gear);
            fill(cyan); painter->drawEllipse(QPointF(10, 10), 2.1, 2.1);
            break;
        }
        case 22: // Display: substantial monitor frame and cyan screen corner.
            shaded(light, shade, 3, 17);
            painter->drawRoundedRect(QRectF(2, 3, 16, 12), 1.3, 1.3);
            fill(inset); painter->drawRoundedRect(QRectF(3.8, 4.8, 12.4, 8.4), .3, .3);
            fill(gray); polygon({{8.2, 14}, {11.8, 14}, {12.5, 17}, {7.5, 17}});
            fill(light); painter->drawRoundedRect(QRectF(6, 17, 8, 1.4), .5, .5);
            stroke(cyan, 1.5); line(13, 6, 14.5, 6); line(14.5, 6, 14.5, 7.5);
            break;
        case 1: // Queue: overlapping video frames with a cyan play triangle.
            fill(shade); painter->drawRoundedRect(QRectF(1.5, 2.5, 13, 12), 1, 1);
            fill(gray); painter->drawRoundedRect(QRectF(3.2, 4.2, 13, 12), 1, 1);
            fill(inset); painter->drawRoundedRect(QRectF(4.3, 5.4, 11, 9.6), .5, .5);
            shaded(QColor("#a3becf"), QColor("#456b86"), 6.5, 18);
            painter->drawRoundedRect(QRectF(5.5, 6.5, 13, 11), 1, 1);
            fill(cyan); polygon({{10, 9}, {15.2, 12}, {10, 15}});
            break;
        case 5: // DV: framed luminance curve, as in the approved tile.
        {
            shaded(QColor("#203e55"), QColor("#416a85"), 2, 18);
            painter->drawRoundedRect(QRectF(2, 2, 16, 16), 1.6, 1.6);
            QPainterPath lower;
            lower.moveTo(4, 14.7); lower.cubicTo(10, 14.7, 8, 9.4, 15.5, 7.5);
            stroke(light, 1.5); painter->drawPath(lower);
            QPainterPath upper;
            upper.moveTo(4, 14.7); upper.cubicTo(11, 14.7, 8.8, 5, 15.5, 5);
            stroke(cyan, 2); painter->drawPath(upper);
            stroke(highlight, .7); painter->drawPath(upper);
            break;
        }
        case 14: // Shaders: three shaded diamond layers and a cyan ray.
            fill(shade); polygon({{2.2, 14.5}, {10, 10}, {17.8, 14.5}, {10, 19}});
            fill(gray); polygon({{2.2, 11}, {10, 6.5}, {17.8, 11}, {10, 15.5}});
            shaded(light, gray, 3, 12);
            polygon({{2.2, 7.5}, {10, 3}, {17.8, 7.5}, {10, 12}});
            fill(QColor("#6cc5df")); polygon({{8.2, 7}, {10.1, 5.8}, {12, 7}, {10.1, 8.2}});
            stroke(cyan, 1.4); line(13.8, 4.3, 17, 1);
            break;
        case 7: // Actions: filled lightning with an internal cyan face.
            shaded(light, gray, 2, 18);
            polygon({{11.5, 1.5}, {3.7, 11.5}, {8.6, 11.5}, {7.8, 18.5},
                {16.3, 8}, {11.2, 8}});
            fill(cyan); polygon({{10.9, 4.3}, {6.5, 10.1}, {10, 10.1},
                {9.3, 15.4}, {13.7, 9.4}, {10.4, 9.4}});
            break;
        case 15: // Shortcuts: filled keyboard, pale keys and cyan space key.
            shaded(light, shade, 4.5, 16);
            painter->drawRoundedRect(QRectF(1.5, 4.5, 17, 11.5), 1.3, 1.3);
            fill(inset); painter->drawRoundedRect(QRectF(3.1, 6.1, 13.8, 8.3), .4, .4);
            fill(light);
            for (qreal x : {4.3, 7.6, 10.9, 14.2})
                painter->drawRoundedRect(QRectF(x, 7.1, 1.7, 2.6), .3, .3);
            fill(cyan); painter->drawRoundedRect(QRectF(5.7, 11.6, 8.6, 1.6), .4, .4);
            break;
        case 10: // Logs: framed slate with three cyan entries.
            shaded(light, shade, 1.5, 18.5);
            painter->drawRoundedRect(QRectF(3.5, 1.5, 13, 17), 1.2, 1.2);
            fill(inset); painter->drawRoundedRect(QRectF(5.2, 3.2, 9.6, 13.6), .3, .3);
            stroke(cyan, 1.5);
            for (qreal y : {6.5, 10., 13.5}) line(7.2, y, 12.8, y);
            break;
        case 2: // VP Renderer: cyan film frame, perforations and play core.
            fill(QColor("#12364b")); painter->drawRoundedRect(QRectF(1.5, 3, 17, 14), 1, 1);
            stroke(cyan, 1.4); painter->drawRoundedRect(QRectF(1.5, 3, 17, 14), 1, 1);
            fill(cyan);
            for (qreal x : {3.3, 6., 8.7, 11.4, 14.1, 16.8})
                for (qreal y : {4.1, 14.6})
                    painter->drawRoundedRect(QRectF(x - .55, y, 1.1, 1.1), .15, .15);
            fill(inset); painter->drawRect(QRectF(4.5, 6.5, 11, 7));
            fill(cyan); polygon({{7.8, 6.9}, {13.3, 10}, {7.8, 13.1}});
            break;
        case 3: // DirectShow: three round filter nodes, center accented cyan.
            stroke(shade, 3); line(4.2, 13, 10, 6); line(10, 6, 15.8, 13);
            stroke(light, 1.4); line(4.2, 13, 10, 6); line(10, 6, 15.8, 13);
            for (const QPointF& node : {QPointF(4.2, 13), QPointF(15.8, 13)})
            {
                fill(shade); painter->drawEllipse(node, 3.3, 3.3);
                fill(light); painter->drawEllipse(node, 2.1, 2.1);
            }
            fill(gray); painter->drawEllipse(QPointF(10, 6), 3.7, 3.7);
            fill(cyan); painter->drawEllipse(QPointF(10, 6), 2.5, 2.5);
            break;
        case 20: // Updates: filled tray and cyan down arrow.
        {
            QPainterPath tray;
            tray.addRoundedRect(QRectF(2.5, 11.2, 15, 6.6), .8, .8);
            QPainterPath opening;
            opening.addRect(QRectF(4.5, 10.5, 11, 5));
            tray = tray.subtracted(opening);
            shaded(light, gray, 11.2, 18); painter->drawPath(tray);
            fill(light); polygon({{8, 1.5}, {12, 1.5}, {12, 9}, {15, 9},
                {10, 14}, {5, 9}, {8, 9}});
            fill(cyan); polygon({{9.2, 2.4}, {10.8, 2.4}, {10.8, 10.2},
                {12.4, 10.2}, {10, 12.6}, {7.6, 10.2}, {9.2, 10.2}});
            break;
        }
        default:
            stroke(gray); painter->drawEllipse(QPointF(10, 10), 6, 6);
            break;
        }
        painter->restore();
    }
private:
    int page_;
};

inline QIcon create(int page) { return QIcon(new Engine(page)); }
}