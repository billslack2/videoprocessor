#include "VpTheme.h"

#include <QComboBox>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace
{
class VpStyle final : public QProxyStyle
{
public:
    VpStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
        SubControl subControl, const QWidget* widget = nullptr) const override
    {
        if (control == CC_ComboBox && subControl == SC_ComboBoxArrow)
        {
            QRect result = option->rect;
            result.setLeft(result.right() - 31);
            return result;
        }
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (element == PE_IndicatorArrowDown && qobject_cast<const QComboBox*>(widget))
        {
            const QRect arrowRect(widget->width() - 31, 0, 31, widget->height());
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->fillRect(arrowRect, QColor(QStringLiteral("#121e2b")));
            painter->setPen(QPen(QColor(QStringLiteral("#2b4258")), 1.0));
            painter->drawLine(arrowRect.topLeft(), arrowRect.bottomLeft());
            const QPointF center = arrowRect.center();
            QPen pen(QColor(QStringLiteral("#a7b9ca")), 1.7,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(pen);
            QPolygonF chevron;
            chevron << QPointF(center.x() - 4.0, center.y() - 2.0)
                    << QPointF(center.x(), center.y() + 2.0)
                    << QPointF(center.x() + 4.0, center.y() - 2.0);
            painter->drawPolyline(chevron);
            painter->restore();
            return;
        }
        if (element == PE_IndicatorCheckBox)
        {
            // The native Fusion indicator is a filled square when checked,
            // which reads like a third state at compact sizes. Draw the only
            // two states the configuration editor supports explicitly.
            const bool checked = option->state.testFlag(State_On);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            const QRectF box = option->rect.adjusted(1, 1, -1, -1);
            painter->setPen(QPen(QColor(checked ? QStringLiteral("#49b6ff") :
                QStringLiteral("#4f6780")), 1.0));
            painter->setBrush(QColor(checked ? QStringLiteral("#167fc7") :
                QStringLiteral("#0c1520")));
            painter->drawRoundedRect(box, 3.0, 3.0);
            if (checked)
            {
                QPen tick(QColor(QStringLiteral("#f3fbff")), 1.8,
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter->setPen(tick);
                const QPointF left(box.left() + box.width() * 0.22,
                    box.top() + box.height() * 0.54);
                const QPointF center(box.left() + box.width() * 0.43,
                    box.top() + box.height() * 0.73);
                const QPointF right(box.left() + box.width() * 0.79,
                    box.top() + box.height() * 0.31);
                painter->drawPolyline(QPolygonF{ left, center, right });
            }
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};
}

QStyle* VpTheme::CreateStyle()
{
    return new VpStyle;
}

QString VpTheme::StyleSheet()
{
    return QStringLiteral(R"(
        QWidget {
            color: #e5eef6;
            font-family: "Segoe UI";
            font-size: 13px;
        }
        QMainWindow, #root, #pageViewport { background: #0b121b; }
        #brandHeader {
            background: #0a1119;
            border: 0;
            border-bottom: 1px solid #233447;
        }
        #brandTitle { color: #f6f9fc; font-size: 20px; font-weight: 700; }
        #brandSubtitle {
            color: #93a9bd;
            background: #101d2a;
            border: 1px solid #2b4359;
            border-radius: 5px;
            padding: 4px 9px;
            font-size: 11px;
            font-weight: 600;
        }
        QToolButton[headerMenu="true"] {
            color: #c8d7e5;
            background: #101d2a;
            border: 1px solid #2d475f;
            border-radius: 5px;
            padding: 5px 10px 5px 11px;
            font-weight: 600;
        }
        QToolButton[headerMenu="true"]:hover { background: #16293a; border-color: #3e6688; }
        QToolButton[headerMenu="true"]::menu-indicator { image: none; width: 0; }
        #sidebar { background: #0d1621; border-right: 1px solid #233447; }
        #sidebarCaption {
            color: #718ba4;
            font-size: 10px;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 0.6px;
        }
        QPushButton[nav="true"] {
            min-height: 30px;
            border: 0;
            border-left: 3px solid transparent;
            border-radius: 3px 0 0 3px;
            background: transparent;
            padding: 3px 12px;
            text-align: left;
            color: #afc0d0;
        }
        QPushButton[nav="true"]:checked {
            border-left-color: #41a8f5;
            background: #13283a;
            color: #edf8ff;
        }
        QPushButton[nav="true"]:hover:!checked { background: #111f2d; color: #e4eff8; }
        QToolButton[navSection="true"] {
            border: 0;
            background: transparent;
            padding: 6px 8px 4px 8px;
            text-align: left;
            color: #d2dfeb;
            font-weight: 600;
        }
        QToolButton[navSection="true"]:hover { background: #111f2d; }
        QToolButton[navSection="true"]::menu-indicator { image: none; }
        QToolButton[profileSection="true"] {
            min-height: 30px;
            color: #d9e7f2;
            background: #111d29;
            border: 1px solid #2b4258;
            border-radius: 5px;
            padding: 4px 9px;
            text-align: left;
            font-weight: 600;
        }
        QToolButton[profileSection="true"]:hover { background: #162839; }
        QToolButton[profileSection="true"]:focus { border: 1px solid #3b9ce5; }
        QPushButton[nav="true"][navChild="true"] {
            padding-left: 14px;
            color: #9eb2c4;
        }
        #pageTitle { color: #f5f9fd; font-size: 23px; font-weight: 650; }
        #pageDescription { color: #9eb2c5; }
        QFrame[card="true"] {
            background: #101b27;
            border: 1px solid #273d53;
            border-radius: 8px;
        }
        QLabel[cardTitle="true"] { color: #f0f6fb; font-size: 16px; font-weight: 650; }
        QLabel[help="true"] { color: #93a8bb; font-size: 12px; }
        QLabel { color: #dce8f2; }
        QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QListWidget {
            min-height: 26px;
            color: #e8f1f8;
            background: #0b1520;
            border: 1px solid #3a5269;
            border-radius: 4px;
            selection-background-color: #197cc3;
            selection-color: #ffffff;
        }
        QLineEdit, QSpinBox { padding: 2px 7px; }
        QLineEdit[invalid="true"] { border: 1px solid #e0645b; background: #27181b; }
        QComboBox { padding: 2px 35px 2px 9px; }
        QComboBox:hover { border-color: #639ac4; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus,
        QPlainTextEdit:focus, QListWidget:focus, QTableWidget:focus {
            border: 1px solid #2a93df;
        }
        QComboBox QAbstractItemView {
            color: #e7f0f8;
            background: #111e2b;
            border: 1px solid #3a5871;
            outline: 0;
            padding: 4px;
            selection-background-color: #173c59;
            selection-color: #f4fbff;
        }
        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled {
            color: #7f95a8;
            background: #111b25;
            border-color: #2c4053;
        }
        QLineEdit[inherited="true"], QComboBox[inherited="true"], QCheckBox[inherited="true"] {
            color: #8ba1b4;
        }
        QLineEdit[readOnly="true"], QPlainTextEdit[readOnly="true"] {
            color: #aebfce;
            background: #101a24;
            border-color: #2d4255;
        }
        QLabel[previewBadge="true"] {
            color: #9bb5c8;
            background: #162533;
            border: 1px solid #314a60;
            border-radius: 9px;
            padding: 3px 8px;
            font-size: 11px;
            font-weight: 600;
        }
        QListWidget { padding: 4px; outline: 0; }
        QListWidget::item { padding: 6px 8px; border-radius: 3px; }
        QListWidget::item:hover { background: #152536; }
        QListWidget::item:selected { background: #173b58; color: #f0f8ff; }
        QTableWidget {
            gridline-color: #2c4255;
            background: #0d1722;
            alternate-background-color: #101c28;
        }
        QTableWidget::item { padding: 5px; }
        QHeaderView::section {
            color: #aebfd0;
            background: #142231;
            border: 0;
            border-bottom: 1px solid #30495f;
            padding: 6px;
            font-weight: 600;
        }
        QCheckBox { spacing: 8px; color: #dce8f2; }
        QCheckBox:disabled { color: #7f94a6; }
        QCheckBox::indicator { width: 15px; height: 15px; }
        QPushButton {
            min-height: 30px;
            padding: 0 13px;
            color: #d9ecfa;
            background: #122334;
            border: 1px solid #35607f;
            border-radius: 5px;
        }
        QPushButton:hover { background: #18324a; border-color: #4d85ac; }
        QPushButton:pressed { background: #0e1e2e; }
        QPushButton:focus { border: 1px solid #3a9de8; }
        QPushButton[nav="true"]:focus {
            border: 0;
            border-left: 3px solid transparent;
            outline: 0;
        }
        QPushButton[nav="true"]:checked:focus {
            border-left-color: #41a8f5;
        }
        QPushButton:disabled { color: #607689; background: #111b26; border-color: #263a4d; }
        QPushButton[primary="true"] { color: #f8fcff; background: #1479c4; border-color: #248dd6; }
        QPushButton[primary="true"]:hover { background: #1c8bd9; border-color: #55b3f4; }
        QPushButton[primary="true"]:disabled { color: #8299ac; background: #23384a; border-color: #2c455a; }
        QPushButton[danger="true"] { color: #ffb4ad; border-color: #9b514f; background: #291a20; }
        QPushButton[danger="true"]:hover { background: #402027; border-color: #df7771; }
        QPushButton[danger="true"]:disabled { color: #6f858f; background: #171d25; border-color: #303e4a; }
        QDialog#noticeDialog, QDialog#confirmationDialog { background: #101b27; }
        QLabel#noticeTitle { color: #f3f8fc; font-size: 18px; font-weight: 650; }
        QLabel#noticeMessage {
            color: #f0c4c0;
            background: #291a20;
            border: 1px solid #7f4446;
            border-radius: 6px;
            padding: 12px;
        }
        QLabel#errorBadge {
            color: white;
            background: #c9463e;
            border-radius: 17px;
            font-size: 20px;
            font-weight: 700;
        }
        QMessageBox { background: #101b27; }
        QMessageBox QLabel { color: #dce8f2; }
        QMenu {
            color: #dce9f3;
            background: #111d29;
            border: 1px solid #314a61;
            padding: 5px;
        }
        QMenu::item { padding: 7px 28px 7px 12px; border-radius: 4px; }
        QMenu::item:selected { background: #173b58; color: #f7fbff; }
        QMenu::separator { height: 1px; background: #2b4258; margin: 5px 4px; }
        QToolTip {
            color: #e8f2fa;
            background: #172636;
            border: 1px solid #42617b;
            padding: 5px;
        }
        #footer { background: #0d1621; border-top: 1px solid #233447; }
        #configurationStatus { color: #9fb2c4; }
        QScrollArea { border: 0; background: #0b121b; }
        QScrollBar:vertical { width: 10px; background: #0d1621; margin: 2px; }
        QScrollBar::handle:vertical { background: #30495f; min-height: 36px; border-radius: 5px; }
        QScrollBar::handle:vertical:hover { background: #43647f; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { height: 10px; background: #0d1621; margin: 2px; }
        QScrollBar::handle:horizontal { background: #30495f; min-width: 36px; border-radius: 5px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QSplitter::handle { background: transparent; width: 12px; }
        QSplitter::handle:vertical { height: 12px; }
        QToolButton[navSection="true"]:focus, QToolButton[headerMenu="true"]:focus {
            border: 1px solid #4daaf1;
        }
        QCheckBox:focus { color: #ffffff; }
    )");
}
