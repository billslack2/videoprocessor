#pragma once
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <vector>

namespace WorkspaceDockIcons
{
    inline QIcon panel(bool open)
    {
        QPixmap image(48, 48); image.fill(Qt::transparent);
        QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.scale(2, 2);
        p.setPen(QPen(QColor("#c4d5e5"), 1.5));
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2); p.drawLine(QPointF(9, 4), QPointF(9, 20));
        if (open) p.fillRect(QRectF(4, 5, 4, 14), QColor("#6c9fc6"));
        p.drawLine(QPointF(open ? 16 : 13, 9), QPointF(open ? 13 : 16, 12));
        p.drawLine(QPointF(open ? 13 : 16, 12), QPointF(open ? 16 : 13, 15));
        return QIcon(image);
    }
    inline QIcon list()
    {
        QPixmap image(48, 48); image.fill(Qt::transparent);
        QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.scale(2, 2);
        p.setPen(QPen(QColor("#c4d5e5"), 1.5));
        for (int y : { 6, 12, 18 }) { p.drawRect(QRectF(3, y - 1, 2, 2)); p.drawLine(9, y, 21, y); }
        return QIcon(image);
    }
    inline QString controlsStyle()
    {
        return QStringLiteral("QToolButton { color: #c4d5e5; background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; } "
            "QToolButton:hover { background: #193249; border-color: #36556c; } QToolButton:focus { border-color: #49b6ff; } "
            "QToolButton:checked { background: #1b405a; border-color: #427699; } QToolButton:disabled { color: #536879; }");
    }
    inline QToolButton* button(const QString& name, const QString& description, const QIcon& icon)
    {
        auto* result = new QToolButton;
        result->setObjectName(name); result->setAccessibleName(description); result->setToolTip(description);
        result->setIcon(icon); result->setIconSize(QSize(22, 22)); result->setFixedSize(36, 32);
        result->setStyleSheet(controlsStyle()); result->setFocusPolicy(Qt::StrongFocus);
        return result;
    }
}

// Keep the original navigation and its actions. The rail is only another way
// to reach those actions; it is not a separate settings-selection model.
class AdaptiveNavigationHost final : public QWidget
{
public:
    AdaptiveNavigationHost(QWidget* sidebar, QWidget* page) : sidebar_(sidebar)
    {
        setObjectName(QStringLiteral("settingsNavigationHost"));
        auto* row = new QHBoxLayout(this); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
        rail_ = new QWidget; rail_->setObjectName(QStringLiteral("settingsNavigationRail")); rail_->setFixedWidth(48);
        rail_->setStyleSheet(QStringLiteral("#settingsNavigationRail { background: #0d1722; border-right: 1px solid #294052; }"));
        auto* railLayout = new QVBoxLayout(rail_); railLayout->setContentsMargins(6, 8, 6, 6); railLayout->setSpacing(6);
        railExpand_ = WorkspaceDockIcons::button(QStringLiteral("settingsRailExpand"), QStringLiteral("Expand settings navigation"), WorkspaceDockIcons::panel(false));
        railLayout->addWidget(railExpand_);
        auto* actionsScroll = new QScrollArea; actionsScroll->setFrameShape(QFrame::NoFrame); actionsScroll->setWidgetResizable(true);
        actionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* actions = new QWidget; railActions_ = new QVBoxLayout(actions); railActions_->setContentsMargins(0, 0, 0, 0); railActions_->setSpacing(4); railActions_->addStretch();
        actions->setObjectName(QStringLiteral("settingsRailActions"));
        actionsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: #0d1722; border: none; } QWidget#settingsRailActions { background: #0d1722; }"));
        actionsScroll->viewport()->setStyleSheet(QStringLiteral("background: #0d1722;"));
        actionsScroll->setWidget(actions); railLayout->addWidget(actionsScroll, 1);
        dock_ = new QScrollArea; dock_->setWidgetResizable(true); dock_->setFrameShape(QFrame::NoFrame);
        dock_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); dock_->setFixedWidth(190); dock_->setWidget(sidebar_);
        dockToggle_ = WorkspaceDockIcons::button(QStringLiteral("settingsDockToggle"), QStringLiteral("Collapse settings navigation"), WorkspaceDockIcons::panel(true));
        if (auto* layout = qobject_cast<QVBoxLayout*>(sidebar_->layout()))
        {
            auto* header = new QWidget; auto* line = new QHBoxLayout(header); line->setContentsMargins(0, 0, 4, 0);
            if (layout->count() && qobject_cast<QLabel*>(layout->itemAt(0)->widget()))
            {
                auto* item = layout->takeAt(0); line->addWidget(item->widget(), 1); delete item;
            }
            else line->addWidget(new QLabel(QStringLiteral("Settings")), 1);
            line->addWidget(dockToggle_); layout->insertWidget(0, header);
        }
        row->addWidget(rail_); row->addWidget(dock_); row->addWidget(page, 1);
        rail_->hide();
        connect(dockToggle_, &QToolButton::clicked, this, [this] { setManualExpanded(false); });
        connect(railExpand_, &QToolButton::clicked, this, [this] { setManualExpanded(true); });
    }
    void addRailButton(QAbstractButton* original, const QIcon& icon)
    {
        auto* button = WorkspaceDockIcons::button(QStringLiteral("settingsRail.") + original->objectName(), original->text().remove('&'), icon);
        button->setCheckable(original->isCheckable()); button->setChecked(original->isChecked()); button->setEnabled(original->isEnabled()); button->setVisible(!original->isHidden());
        railActions_->insertWidget(railActions_->count() - 1, button);
        proxies_.push_back({ original, button }); original->installEventFilter(this);
        connect(button, &QToolButton::clicked, original, [original] { original->click(); });
        connect(original, &QAbstractButton::toggled, button, &QToolButton::setChecked);
    }
    void setAdvancedFocus(bool active)
    {
        if (advanced_ == active) return;
        advanced_ = active;
        if (active) autoCollapsed_ = requestedExpanded_;
        else autoCollapsed_ = false;
        refresh();
    }
    void restorePanel() { autoCollapsed_ = false; refresh(); }
    bool isPanelExpanded() const { return !dock_->isHidden(); }
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); refresh(); }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::EnabledChange || event->type() == QEvent::Show || event->type() == QEvent::Hide ||
            event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)
            for (const auto& entry : proxies_) if (entry.first == watched && entry.first && entry.second)
            {
                entry.second->setEnabled(entry.first->isEnabled());
                // isHidden tracks explicit page availability, unlike isVisible
                // which also becomes false when the whole navigation docks away.
                entry.second->setVisible(!entry.first->isHidden());
            }
        return QWidget::eventFilter(watched, event);
    }
private:
    void setManualExpanded(bool expanded)
    {
        requestedExpanded_ = expanded; autoCollapsed_ = false; refresh();
        (isPanelExpanded() ? dockToggle_ : railExpand_)->setFocus(Qt::OtherFocusReason);
    }
    void refresh()
    {
        if (refreshing_) return;
        refreshing_ = true;
        // Normally profiles yield width first. Keep a usable navigation rail on
        // extremely narrow windows without forgetting the user's dock choice.
        const bool expanded = requestedExpanded_ && !autoCollapsed_ && width() >= 640;
        QWidget* focus = QApplication::focusWidget();
        const bool moveFocus = !expanded && focus && (focus == sidebar_ || sidebar_->isAncestorOf(focus));
        dock_->setVisible(expanded); rail_->setVisible(!expanded);
        setProperty("navigationExpanded", expanded);
        if (moveFocus) railExpand_->setFocus(Qt::OtherFocusReason);
        refreshing_ = false;
    }
    QWidget *sidebar_ = nullptr, *rail_ = nullptr;
    QScrollArea* dock_ = nullptr;
    QVBoxLayout* railActions_ = nullptr;
    QToolButton *dockToggle_ = nullptr, *railExpand_ = nullptr;
    std::vector<std::pair<QPointer<QAbstractButton>, QPointer<QToolButton>>> proxies_;
    bool requestedExpanded_ = true, autoCollapsed_ = false, advanced_ = false, refreshing_ = false;
};

// Keep profile context readable without allowing a long name to dictate pane
// width or introduce height-for-width expansion into the scrolling workspace.
class WorkspaceProfileLabel final : public QLabel
{
public:
    void setProfileName(const QString& text) { fullText_ = text; updateLabel(); updateGeometry(); }
    QSize sizeHint() const override { return QSize(qMin(420, fontMetrics().horizontalAdvance(fullText_)), fontMetrics().height() + 2); }
    QSize minimumSizeHint() const override { return QSize(0, fontMetrics().height() + 2); }
protected:
    void resizeEvent(QResizeEvent* event) override { QLabel::resizeEvent(event); updateLabel(); }
    void changeEvent(QEvent* event) override { QLabel::changeEvent(event); if (event->type() == QEvent::FontChange) updateLabel(); }
private:
    void updateLabel() { QLabel::setText(fontMetrics().elidedText(fullText_, Qt::ElideRight, width())); }
    QString fullText_;
};

// Profiles keep the same live list and editor through docking and drawer moves.
// No configuration value or current selection is written by layout changes.
class AdaptiveProfileWorkspace final : public QWidget
{
public:
    std::function<void(bool)> advancedFocusChanged;
    AdaptiveProfileWorkspace()
    {
        setProperty("adaptiveWorkspace", true); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        root_ = new QHBoxLayout(this); root_->setContentsMargins(0, 0, 0, 0); root_->setSpacing(10);
        rail_ = new QWidget; rail_->setObjectName(QStringLiteral("profilesRail")); rail_->setFixedWidth(48);
        rail_->setStyleSheet(QStringLiteral("#profilesRail { background: #0d1722; border: 1px solid #294052; border-radius: 5px; }"));
        auto* railLayout = new QVBoxLayout(rail_); railLayout->setContentsMargins(6, 8, 6, 6); railLayout->setSpacing(8);
        railExpand_ = WorkspaceDockIcons::button({}, QStringLiteral("Expand profiles panel"), WorkspaceDockIcons::panel(false));
        drawerToggle_ = WorkspaceDockIcons::button({}, QStringLiteral("Choose a profile"), WorkspaceDockIcons::list());
        drawerToggle_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon); drawerToggle_->setFixedHeight(54);
        railLayout->addWidget(railExpand_); railLayout->addWidget(drawerToggle_); railLayout->addStretch();
        dock_ = new QWidget; dock_->setObjectName(QStringLiteral("profilesDock")); dock_->setFixedWidth(300);
        dockLayout_ = new QVBoxLayout(dock_); dockLayout_->setContentsMargins(0, 0, 0, 0);
        auto* editorHost = new QWidget; auto* content = new QVBoxLayout(editorHost); content->setContentsMargins(0, 0, 0, 0); content->setSpacing(6);
        auto* toolbar = new QWidget; auto* tools = new QHBoxLayout(toolbar); tools->setContentsMargins(0, 0, 0, 0); tools->setSpacing(8);
        selected_ = new WorkspaceProfileLabel; selected_->setObjectName(QStringLiteral("workspaceSelectedProfile"));
        selected_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); tools->addWidget(selected_, 1);
        advancedToggle_ = new QPushButton(QStringLiteral("Advanced tone mapping"));
        advancedToggle_->setStyleSheet(QStringLiteral("QPushButton:checked { background: #164668; border-color: #49b6ff; color: #e9f5ff; }"));
        advancedToggle_->setCheckable(true); advancedToggle_->setAccessibleName(QStringLiteral("Show advanced tone mapping charts and focus the workspace")); advancedToggle_->hide();
        tools->addWidget(advancedToggle_); content->addWidget(toolbar);
        editors_ = new QSplitter(Qt::Horizontal); editors_->setObjectName(QStringLiteral("workspaceEditors")); editors_->setChildrenCollapsible(false);
        content->addWidget(editors_, 1); root_->addWidget(rail_); root_->addWidget(dock_); root_->addWidget(editorHost, 1); rail_->hide();
        drawer_ = new QFrame(this); drawer_->setObjectName(QStringLiteral("profileDrawer")); drawer_->setFrameShape(QFrame::StyledPanel);
        drawer_->setStyleSheet(QStringLiteral("#profileDrawer { background: #101b27; border: 1px solid #49b6ff; border-radius: 5px; }"));
        drawerLayout_ = new QVBoxLayout(drawer_); drawerLayout_->setContentsMargins(3, 3, 3, 3);
        drawerScroll_ = new QScrollArea; drawerScroll_->setObjectName(QStringLiteral("profileDrawerViewport"));
        drawerScroll_->setFrameShape(QFrame::NoFrame); drawerScroll_->setWidgetResizable(true); drawerScroll_->setMinimumSize(0, 0);
        drawerLayout_->addWidget(drawerScroll_); drawer_->hide();
        connect(railExpand_, &QToolButton::clicked, this, [this] { setManualExpanded(true); });
        connect(drawerToggle_, &QToolButton::clicked, this, [this] { if (drawerOpen_) closeProfileDrawer(); else openProfileDrawer(); });
        connect(advancedToggle_, &QPushButton::toggled, this, [this](bool active) { setAdvancedFocus(active); });
    }
    ~AdaptiveProfileWorkspace() override { if (qApp) qApp->removeEventFilter(this); }
    void addWidget(QWidget* widget)
    {
        if (!profiles_)
        {
            profiles_ = widget; list_ = widget->findChild<QListWidget*>(); dockLayout_->addWidget(widget);
            const QString prefix = list_ ? list_->objectName() : QStringLiteral("profiles");
            setObjectName(QStringLiteral("workspace.") + prefix);
            railExpand_->setObjectName(prefix + QStringLiteral(".railExpand")); drawerToggle_->setObjectName(prefix + QStringLiteral(".drawerToggle"));
            dockToggle_ = WorkspaceDockIcons::button(prefix + QStringLiteral(".dockToggle"), QStringLiteral("Collapse profiles panel"), WorkspaceDockIcons::panel(true));
            drawerPin_ = WorkspaceDockIcons::button(prefix + QStringLiteral(".drawerPin"), QStringLiteral("Dock profiles panel"), WorkspaceDockIcons::panel(false));
            drawerClose_ = WorkspaceDockIcons::button(prefix + QStringLiteral(".drawerClose"), QStringLiteral("Close profiles drawer"), {}); drawerClose_->setText(QString::fromUtf8("×"));
            drawerPin_->hide(); drawerClose_->hide();
            if (auto* layout = qobject_cast<QVBoxLayout*>(profiles_->layout()))
            {
                auto* header = new QWidget; auto* line = new QHBoxLayout(header); line->setContentsMargins(0, 0, 0, 0); line->setSpacing(2);
                if (layout->count() && qobject_cast<QLabel*>(layout->itemAt(0)->widget())) { auto* item = layout->takeAt(0); line->addWidget(item->widget(), 1); delete item; }
                else line->addWidget(new QLabel(QStringLiteral("Profiles")), 1);
                line->addWidget(dockToggle_); line->addWidget(drawerPin_); line->addWidget(drawerClose_); layout->insertWidget(0, header);
            }
            connect(dockToggle_, &QToolButton::clicked, this, [this] { setManualExpanded(false); });
            connect(drawerPin_, &QToolButton::clicked, this, [this] { setManualExpanded(true); });
            connect(drawerClose_, &QToolButton::clicked, this, [this] { closeProfileDrawer(); });
            if (list_)
            {
                connect(list_, &QListWidget::currentRowChanged, this, [this](int) { updateSelection(); });
                connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex&, const QModelIndex&) { updateSelection(); });
                const auto queue = [this] { queueSelectionUpdate(); };
                connect(list_->model(), &QAbstractItemModel::modelReset, this, queue);
                connect(list_->model(), &QAbstractItemModel::rowsInserted, this, queue);
                connect(list_->model(), &QAbstractItemModel::rowsRemoved, this, queue);
                connect(list_->model(), &QAbstractItemModel::rowsMoved, this, queue);
                connect(list_->model(), &QAbstractItemModel::dataChanged, this, queue);
                connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { if (drawerOpen_) closeProfileDrawer(); });
                connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { if (drawerOpen_) closeProfileDrawer(); });
                const bool actions = prefix.contains(QStringLiteral("actions"));
                noun_ = actions ? QStringLiteral("actions") : QStringLiteral("profiles");
                updateSelection();
            }
        }
        else
        {
            const bool chart = editors_->count() > 0;
            auto* scroll = new QScrollArea; scroll->setObjectName(chart ? QStringLiteral("workspaceChartsViewport") : QStringLiteral("workspaceSettingsViewport"));
            scroll->setAccessibleName(chart ? QStringLiteral("Tone charts") : QStringLiteral("Profile settings"));
            scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); scroll->setMinimumWidth(0); scroll->setWidget(widget);
            editors_->addWidget(scroll); editors_->setStretchFactor(editors_->count() - 1, 1);
            if (chart)
            {
                advancedToggle_->setObjectName(QStringLiteral("workspaceAdvancedToggle"));
                advancedToggle_->show(); scroll->hide();
            }
        }
        QTimer::singleShot(0, this, [this] { refresh(); });
    }
    void setAdvancedFocus(bool active)
    {
        if (editors_->count() < 2 || advanced_ == active) return;
        advanced_ = active; restored_ = false;
        advancedToggle_->setText(active ? QStringLiteral("Basic tone mapping") : QStringLiteral("Advanced tone mapping"));
        advancedToggle_->setAccessibleName(active ? QStringLiteral("Return to basic tone mapping") : QStringLiteral("Show advanced tone mapping"));
        { const QSignalBlocker blocker(advancedToggle_); advancedToggle_->setChecked(active); }
        if (active) autoCollapsed_ = requestedExpanded_;
        else autoCollapsed_ = false;
        closeProfileDrawer(false); refresh();
        if (advancedFocusChanged) advancedFocusChanged(active);
    }
    bool isAdvancedFocus() const { return advanced_; }
    void restorePanels()
    {
        autoCollapsed_ = false; restored_ = true; refresh();
        if (advancedFocusChanged) advancedFocusChanged(false);
    }
    // Actual dock visibility; requested expansion survives temporary width limits.
    bool isProfilesExpanded() const { return !dock_->isHidden(); }
    void openProfileDrawer()
    {
        if (!profiles_ || isProfilesExpanded()) return;
        if (!drawerOpen_)
        {
            drawerOpen_ = true; dockLayout_->removeWidget(profiles_); drawerScroll_->setWidget(profiles_);
            dockToggle_->hide(); drawerPin_->show(); drawerClose_->show(); profiles_->show();
            qApp->installEventFilter(this);
        }
        positionDrawer(); drawer_->show(); drawer_->raise();
        if (list_) list_->setFocus(Qt::OtherFocusReason);
        setProperty("profileDrawerOpen", true);
    }
    void closeProfileDrawer(bool restoreFocus = true)
    {
        if (!drawerOpen_) return;
        drawerOpen_ = false; qApp->removeEventFilter(this);
        drawer_->hide(); drawerScroll_->takeWidget(); dockLayout_->addWidget(profiles_);
        drawerPin_->hide(); drawerClose_->hide(); dockToggle_->show(); profiles_->show();
        setProperty("profileDrawerOpen", false);
        if (restoreFocus && isVisible()) drawerToggle_->setFocus(Qt::OtherFocusReason);
    }
    QSize minimumSizeHint() const override { return QSize(0, root_->minimumSize().height()); }
    QSize sizeHint() const override { return QSize(1000, minimumSizeHint().height()); }
    // Qt otherwise propagates the splitter panes' preferred heights through a
    // parent page's wrapping labels, forcing both inner and outer scrolling.
    int heightForWidth(int) const override { return minimumSizeHint().height(); }
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); refresh(); }
    void hideEvent(QHideEvent* event) override { closeProfileDrawer(false); QWidget::hideEvent(event); }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!drawerOpen_) return QWidget::eventFilter(watched, event);
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape &&
            !QApplication::activePopupWidget() && !QApplication::activeModalWidget())
        { closeProfileDrawer(); return true; }
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && widget != drawer_ && !drawer_->isAncestorOf(widget) && widget != drawerToggle_ && widget != railExpand_)
                closeProfileDrawer(false);
        }
        return QWidget::eventFilter(watched, event);
    }
private:
    void queueSelectionUpdate()
    {
        if (selectionUpdatePending_) return;
        selectionUpdatePending_ = true;
        QTimer::singleShot(0, this, [this] { selectionUpdatePending_ = false; updateSelection(); });
    }
    void updateSelection()
    {
        if (!list_) return;
        const QString name = list_->currentItem() ? list_->currentItem()->text() : QStringLiteral("No selection");
        selected_->setProfileName(name); selected_->setToolTip(name); selected_->setAccessibleName(QStringLiteral("Selected profile: %1").arg(name));
        drawerToggle_->setText(QString::number(list_->count()));
        const QString description = QStringLiteral("Choose %1 — %2 (%3 available)").arg(noun_, name).arg(list_->count());
        drawerToggle_->setToolTip(description); drawerToggle_->setAccessibleName(description);
    }
    void setManualExpanded(bool expanded)
    {
        requestedExpanded_ = expanded; autoCollapsed_ = false;
        if (!expanded) closeProfileDrawer(false);
        refresh();
        if (expanded && !isProfilesExpanded()) openProfileDrawer();
        else if (expanded) { closeProfileDrawer(false); dockToggle_->setFocus(Qt::OtherFocusReason); }
        else railExpand_->setFocus(Qt::OtherFocusReason);
    }
    void positionDrawer()
    {
        // The page may be taller than its outer scroll viewport. Keep the
        // drawer within the part actually visible, including a scrolled top.
        QRect visible = rect();
        for (QWidget* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget())
        {
            visible = visible.intersected(QRect(mapFrom(ancestor, QPoint(0, 0)), ancestor->size()));
            if (auto* scroll = qobject_cast<QScrollArea*>(ancestor))
                for (QScrollBar* bar : { scroll->horizontalScrollBar(), scroll->verticalScrollBar() })
                {
                    bool connected = false;
                    for (const auto& existing : drawerAncestorScrollBars_) if (existing == bar) connected = true;
                    if (!connected)
                    {
                        drawerAncestorScrollBars_.push_back(bar);
                        // Scrolling the page underneath a drawer invalidates its
                        // visible bounds. Dismiss it without stealing focus.
                        connect(bar, &QScrollBar::valueChanged, this, [this](int) { if (drawerOpen_) closeProfileDrawer(false); });
                    }
                }
        }
        const int width = qMin(360, qMax(0, visible.width()));
        const int preferredX = rail_->isHidden() ? 0 : rail_->width() + 6;
        const int x = qBound(visible.left(), preferredX, visible.right() - width + 1);
        drawer_->setGeometry(x, visible.top(), width, qMax(0, visible.height()));
        drawerPin_->setEnabled(!fitCollapsed_);
        drawerPin_->setToolTip(fitCollapsed_ ? QStringLiteral("Widen the window to dock this panel without squeezing settings") : QStringLiteral("Dock profiles panel"));
    }
    void refresh()
    {
        if (refreshing_ || !profiles_ || editors_->count() == 0) return;
        refreshing_ = true;
        const bool fit = width() >= (fitCollapsed_ ? 752 : 712);
        fitCollapsed_ = !fit;
        const bool expanded = requestedExpanded_ && !autoCollapsed_ && fit;
        QPointer<QWidget> focus = QApplication::focusWidget();
        if (expanded && drawerOpen_) closeProfileDrawer(false);
        dock_->setVisible(expanded); rail_->setVisible(!expanded);
        const int editorWidth = width() - (expanded ? dock_->width() : rail_->width()) - root_->spacing();
        const bool horizontal = !advanced_ || editorWidth >= (stacked_ ? 1120 : 1080);
        const bool changingOrientation = stacked_ != !horizontal;
        stacked_ = !horizontal;
        editors_->setOrientation(horizontal ? Qt::Horizontal : Qt::Vertical);
        if (editors_->count() > 1) editors_->widget(1)->setVisible(advanced_);
        for (int i = 0; i < editors_->count(); ++i)
        {
            editors_->widget(i)->setMinimumWidth(advanced_ && horizontal ? 480 : 0);
            editors_->widget(i)->setMinimumHeight(advanced_ && !horizontal ? 120 : 70);
        }
        if (changingOrientation) editors_->setSizes({ horizontal ? editorWidth / 2 : 180, horizontal ? editorWidth / 2 : 180 });
        setProperty("profilesExpanded", expanded); setProperty("workspaceMode", advanced_ ? horizontal ? "horizontal" : "stacked" : "settings");
        if (focus && !expanded && (focus == profiles_ || profiles_->isAncestorOf(focus)) && !drawerOpen_) railExpand_->setFocus(Qt::OtherFocusReason);
        if (drawerOpen_) positionDrawer();
        refreshing_ = false;
    }
    QHBoxLayout* root_ = nullptr;
    QWidget *rail_ = nullptr, *dock_ = nullptr, *profiles_ = nullptr;
    QFrame* drawer_ = nullptr;
    QScrollArea* drawerScroll_ = nullptr;
    std::vector<QPointer<QScrollBar>> drawerAncestorScrollBars_;
    QVBoxLayout *dockLayout_ = nullptr, *drawerLayout_ = nullptr;
    QListWidget* list_ = nullptr;
    WorkspaceProfileLabel* selected_ = nullptr;
    QToolButton *railExpand_ = nullptr, *drawerToggle_ = nullptr, *dockToggle_ = nullptr, *drawerPin_ = nullptr, *drawerClose_ = nullptr;
    QPushButton *advancedToggle_ = nullptr;
    QSplitter* editors_ = nullptr;
    QString noun_ = QStringLiteral("profiles");
    bool requestedExpanded_ = true, autoCollapsed_ = false, advanced_ = false, restored_ = false;
    bool fitCollapsed_ = false, stacked_ = false, drawerOpen_ = false, refreshing_ = false, selectionUpdatePending_ = false;
};
