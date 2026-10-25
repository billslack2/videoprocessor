#pragma once
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

// One live editor/model in every layout. Reflow never saves or reconstructs
// controls: focus, unsaved text, selection and each pane's scroll position survive.
class AdaptiveProfileWorkspace final : public QWidget
{
public:
    AdaptiveProfileWorkspace()
    {
        setProperty("adaptiveWorkspace", true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumHeight(0);
        root_ = new QVBoxLayout(this);
        root_->setContentsMargins(0, 0, 0, 0);
        root_->setSpacing(8);
        toolbar_ = new QWidget;
        auto* row = new QHBoxLayout(toolbar_);
        row->setContentsMargins(0, 0, 0, 0);
        caption_ = new QLabel(QStringLiteral("Profile:"));
        choice_ = new QComboBox;
        choice_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        choice_->setMinimumContentsLength(12);
        choice_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        choice_->setAccessibleName(QStringLiteral("Selected profile"));
        caption_->setBuddy(choice_);
        manage_ = new QPushButton(QStringLiteral("Manage profiles"));
        manage_->setCheckable(true);
        manage_->setAccessibleName(QStringLiteral("Show profile management"));
        row->addWidget(caption_);
        row->addWidget(choice_, 1);
        row->addWidget(manage_);
        root_->addWidget(toolbar_);
        manager_ = new QWidget;
        managerLayout_ = new QVBoxLayout(manager_);
        managerLayout_->setContentsMargins(0, 0, 0, 0);
        manager_->hide();
        root_->addWidget(manager_);
        columns_ = new QSplitter(Qt::Horizontal);
        columns_->setChildrenCollapsible(false);
        columns_->setObjectName(QStringLiteral("workspaceColumns"));
        editorHost_ = new QWidget;
        auto* content = new QVBoxLayout(editorHost_);
        content->setContentsMargins(0, 0, 0, 0);
        content->setSpacing(4);
        tabs_ = new QTabBar;
        tabs_->setObjectName(QStringLiteral("workspaceViewTabs"));
        tabs_->setAccessibleName(QStringLiteral("Settings or tone charts"));
        tabs_->setProperty("sectionTabs", true);
        tabs_->setExpanding(false);
        tabs_->addTab(QStringLiteral("Settings"));
        tabs_->addTab(QStringLiteral("Tone charts"));
        tabs_->hide();
        content->addWidget(tabs_);
        editors_ = new QSplitter(Qt::Horizontal);
        editors_->setObjectName(QStringLiteral("workspaceEditors"));
        editors_->setChildrenCollapsible(false);
        content->addWidget(editors_, 1);
        columns_->addWidget(editorHost_);
        root_->addWidget(columns_, 1);
        connect(manage_, &QPushButton::toggled, this, [this] { reflow(); });
        connect(tabs_, &QTabBar::currentChanged, this, [this] { showEditor(); });
    }

    void addWidget(QWidget* widget)
    {
        if (!profiles_)
        {
            profiles_ = widget;
            list_ = widget->findChild<QListWidget*>();
            columns_->insertWidget(0, widget);
            if (list_)
            {
                setObjectName(QStringLiteral("workspace.") + list_->objectName());
                choice_->setObjectName(list_->objectName() + QStringLiteral(".compact"));
                manage_->setObjectName(list_->objectName() + QStringLiteral(".manage"));
                choice_->setModel(list_->model());
                choice_->setCurrentIndex(list_->currentRow());
                choice_->setAccessibleName(list_->accessibleName());
                if (list_->objectName().contains(QStringLiteral("actions")))
                {
                    caption_->setText(QStringLiteral("Action:"));
                    manage_->setText(QStringLiteral("Manage actions"));
                    manage_->setAccessibleName(QStringLiteral("Show action management"));
                }
                connect(choice_, qOverload<int>(&QComboBox::activated), list_, [this](int row) { list_->setCurrentRow(row); });
                const auto syncSelection = [this] {
                    const QSignalBlocker block(choice_);
                    choice_->setCurrentIndex(list_->currentRow());
                };
                connect(list_, &QListWidget::currentRowChanged, choice_, [syncSelection](int) { syncSelection(); });
                // ProfileListController deliberately blocks QListWidget signals
                // while rebuilding. Its selection model still reports the final
                // selection; resync after model changes also follows combo resets.
                connect(list_->selectionModel(), &QItemSelectionModel::currentChanged,
                    choice_, [syncSelection](const QModelIndex&, const QModelIndex&) { syncSelection(); });
                const auto queueSync = [this, syncSelection] {
                    if (selectionSyncPending_) return;
                    selectionSyncPending_ = true;
                    QTimer::singleShot(0, this, [this, syncSelection] {
                        selectionSyncPending_ = false;
                        syncSelection();
                    });
                };
                connect(list_->model(), &QAbstractItemModel::modelReset, this, queueSync);
                connect(list_->model(), &QAbstractItemModel::rowsInserted, this, queueSync);
                connect(list_->model(), &QAbstractItemModel::rowsRemoved, this, queueSync);
                connect(list_->model(), &QAbstractItemModel::rowsMoved, this, queueSync);
            }
        }
        else
        {
            auto* scroll = new QScrollArea;
            scroll->setObjectName(editors_->count() == 0 ? QStringLiteral("workspaceSettingsViewport") : QStringLiteral("workspaceChartsViewport"));
            scroll->setAccessibleName(editors_->count() == 0 ? QStringLiteral("Profile settings") : QStringLiteral("Tone charts"));
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->setMinimumWidth(0);
            scroll->setWidget(widget);
            editors_->addWidget(scroll);
            editors_->setStretchFactor(editors_->count() - 1, 1);
        }
        mode_ = -1;
        QTimer::singleShot(0, this, [this] { reflow(); });
    }
    // Let the containing page spend its remaining height on the live panes.
    // A preferred 580px height becomes a scroll area's height-for-width floor.
    // The real layout minimum also accounts for an expanded profile manager.
    QSize minimumSizeHint() const override { return QSize(0, root_->minimumSize().height()); }
    QSize sizeHint() const override { return QSize(1000, minimumSizeHint().height()); }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        reflow();
    }
private:
    void showEditor()
    {
        if (editors_->count() < 2) return;
        for (int i = 0; i < 2; ++i)
            editors_->widget(i)->setVisible(mode_ != 0 || tabs_->currentIndex() == i);
    }
    void reflow()
    {
        if (!profiles_ || editors_->count() == 0 || reflowing_) return;
        reflowing_ = true;
        const bool charts = editors_->count() > 1;
        // Logical pixels, measured inside the page, after app navigation.
        const int dockAt = charts ? 1400 : 900;
        const int splitAt = 1080;
        const int hysteresis = 40;
        const bool dock = width() >= dockAt + ((mode_ >= 0 && mode_ < 2) ? hysteresis : 0);
        const bool split = !charts || width() >= splitAt + (mode_ == 0 ? hysteresis : 0);
        const int wanted = dock ? 2 : split ? 1 : 0;
        QPointer<QWidget> focus = QApplication::focusWidget();
        if (wanted != mode_)
        {
            if (charts && wanted == 0 && focus)
                for (int i = 0; i < editors_->count(); ++i)
                    if (editors_->widget(i)->isAncestorOf(focus))
                    {
                        const QSignalBlocker blocker(tabs_);
                        tabs_->setCurrentIndex(i);
                    }
            mode_ = wanted;
            if (dock)
            {
                managerLayout_->removeWidget(profiles_);
                columns_->insertWidget(0, profiles_);
                profiles_->setMinimumWidth(280);
                profiles_->setMaximumWidth(340);
                profiles_->setMaximumHeight(QWIDGETSIZE_MAX);
                columns_->setStretchFactor(0, 0);
                columns_->setStretchFactor(1, 1);
                columns_->setSizes({ 290, qMax(400, width() - 300) });
            }
            else
            {
                profiles_->setParent(manager_);
                managerLayout_->addWidget(profiles_);
                profiles_->setMinimumWidth(0);
                profiles_->setMaximumWidth(QWIDGETSIZE_MAX);
                profiles_->setMaximumHeight(250);
            }
            toolbar_->setVisible(!dock);
            tabs_->setVisible(charts && !split);
            for (int i = 0; i < editors_->count(); ++i)
                editors_->widget(i)->setMinimumWidth(charts && split ? 420 : 0);
            showEditor();
            editors_->setSizes({ width() / 2, width() / 2 });
            if (dock && (focus == choice_ || focus == manage_) && list_)
                list_->setFocus(Qt::OtherFocusReason);
            else if (focus && focus->isVisibleTo(this)) focus->setFocus(Qt::OtherFocusReason);
            else if (focus && (focus == profiles_ || profiles_->isAncestorOf(focus)) && !dock) choice_->setFocus(Qt::OtherFocusReason);
        }
        manager_->setVisible(!dock && manage_->isChecked());
        profiles_->setVisible(dock || manage_->isChecked());
        setProperty("workspaceMode", charts && !split ? "tabbed" : dock ? "docked" : "compact");
        reflowing_ = false;
    }
    QVBoxLayout* root_ = nullptr;
    QVBoxLayout* managerLayout_ = nullptr;
    QWidget *toolbar_ = nullptr, *manager_ = nullptr, *profiles_ = nullptr, *editorHost_ = nullptr;
    QLabel* caption_ = nullptr;
    QComboBox* choice_ = nullptr;
    QPushButton* manage_ = nullptr;
    QListWidget* list_ = nullptr;
    QSplitter *columns_ = nullptr, *editors_ = nullptr;
    QTabBar* tabs_ = nullptr;
    int mode_ = -1;
    bool reflowing_ = false;
    bool selectionSyncPending_ = false;
};

// App navigation follows the same dock/compact pattern on every page.
class AdaptiveNavigationHost final : public QWidget
{
public:
    AdaptiveNavigationHost(QWidget* sidebar, QWidget* page, QWidget* compact)
        : sidebar_(sidebar), compact_(compact)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        scroll_ = new QScrollArea;
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setFixedWidth(190);
        scroll_->setWidget(sidebar);
        layout->addWidget(scroll_);
        layout->addWidget(page, 1);
    }
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        const bool dock = width() >= (scroll_->isHidden() ? 1240 : 1200);
        QWidget* focus = QApplication::focusWidget();
        const bool transfer = focus && sidebar_->isAncestorOf(focus) && !dock;
        scroll_->setVisible(dock);
        compact_->setVisible(!dock);
        if (transfer)
            if (auto* choice = compact_->findChild<QComboBox*>()) choice->setFocus(Qt::OtherFocusReason);
    }
private:
    QWidget *sidebar_, *compact_;
    QScrollArea* scroll_;
};
