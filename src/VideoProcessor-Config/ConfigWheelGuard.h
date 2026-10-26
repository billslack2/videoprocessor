#pragma once

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QPointer>
#include <QScrollBar>
#include <QWheelEvent>

// In Config, wheel gestures navigate; values change only through deliberate
// clicking, dragging, typing or keyboard input. An application filter also
// covers startup selectors, child editors and controls created in later dialogs.
class ConfigWheelGuard final : public QObject
{
public:
    static void install()
    {
        static QPointer<ConfigWheelGuard> guard;
        if (!guard)
        {
            guard = new ConfigWheelGuard(qApp);
            qApp->installEventFilter(guard);
        }
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Wheel) return false;
        auto* receiver = qobject_cast<QWidget*>(watched);
        if (!receiver) return false;
        // The user explicitly opened a list/menu: keep its normal wheel use.
        if (receiver->window()->windowType() == Qt::Popup) return false;
        QWidget* control = nullptr;
        for (QWidget* widget = receiver; widget; widget = widget->parentWidget())
        {
            if (qobject_cast<QScrollBar*>(widget)) return false;
            if (qobject_cast<QComboBox*>(widget) ||
                qobject_cast<QAbstractSpinBox*>(widget) ||
                qobject_cast<QAbstractSlider*>(widget))
            {
                control = widget;
                break;
            }
            if (qobject_cast<QAbstractScrollArea*>(widget)) break;
        }
        if (!control) return false;
        auto* wheel = static_cast<QWheelEvent*>(event);
        for (QWidget* ancestor = control->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
        {
            auto* scroll = qobject_cast<QAbstractScrollArea*>(ancestor);
            if (!scroll) continue;
            QWidget* viewport = scroll->viewport();
            QWheelEvent forwarded(viewport->mapFromGlobal(wheel->globalPosition().toPoint()),
                wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted(),
                wheel->source(), wheel->pointingDevice());
            QCoreApplication::sendEvent(viewport, &forwarded);
            break;
        }
        // Consume even without a scroll container, so toolbar selectors and
        // standalone dialog fields cannot change as the pointer crosses them.
        wheel->accept();
        return true;
    }
private:
    explicit ConfigWheelGuard(QObject* parent) : QObject(parent) {}
};