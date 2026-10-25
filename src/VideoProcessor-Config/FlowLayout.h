#pragma once
#include <QLayout>
#include <QWidget>
#include <QList>
#include <algorithm>

// A row of controls that wraps at the width actually granted by its parent.
// Its minimum width is one control, never the sum of every control in the row.
class FlowLayout final : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent = nullptr, int gap = 6) : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(gap);
    }
    ~FlowLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.append(item); invalidate(); }
    void insertWidget(int index, QWidget* widget)
    {
        addChildWidget(widget);
        items_.insert(std::clamp(index, 0, count()), new QWidgetItem(widget));
        invalidate();
    }
    int count() const override { return static_cast<int>(items_.size()); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index); }
    QLayoutItem* takeAt(int index) override
    {
        if (index < 0 || index >= count()) return nullptr;
        auto* item = items_.takeAt(index);
        invalidate();
        return item;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (auto* item : items_) if (!item->isEmpty()) size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
    int arrange(const QRect& rect, bool apply) const
    {
        const auto margins = contentsMargins();
        const QRect area = rect.marginsRemoved(margins);
        const int width = std::max(1, area.width());
        int x = area.x(), y = area.y(), rowHeight = 0;
        for (auto* item : items_)
        {
            if (item->isEmpty()) continue;
            QSize size = item->sizeHint();
            size.setWidth(std::min(size.width(), width));
            if (item->hasHeightForWidth()) size.setHeight(item->heightForWidth(size.width()));
            if (rowHeight > 0 && x + size.width() > area.x() + width)
            {
                x = area.x();
                y += rowHeight + spacing();
                rowHeight = 0;
            }
            if (apply) item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing();
            rowHeight = std::max(rowHeight, size.height());
        }
        return y + rowHeight - rect.y() + margins.bottom();
    }
    QList<QLayoutItem*> items_;
};
