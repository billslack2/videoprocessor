#pragma once

#include <QAbstractItemModel>
#include <QListWidget>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>

#include <functional>
#include <utility>


struct ProfileListPolicy
{
    std::function<QStringList()> sections;
    std::function<QString(const QString&)> displayName;
    std::function<QString()> addProfile;
    std::function<bool(const QString&)> removeProfile;
    std::function<QString(const QString&)> normalizeForOrdering;
    std::function<bool(const QString&, const QString&)> moveAfter;
    std::function<void()> markDirty;
    std::function<void()> refreshIndicators;
    QString removeTitle = QStringLiteral("Remove profile");
    QString removeQuestion = QStringLiteral("Remove this profile?");
    bool decorateFirstAsDefault = true;
    bool enabled = true;
};


// Shared lifecycle component for profile and shader-profile lists. Domain
// pages provide persistence and naming adapters; this class exclusively owns
// add/remove, edit selection, default order, button state, and drag ordering.
class ProfileListController final : public QObject
{
public:
    ProfileListController(QObject* context, QWidget* dialogParent,
        QListWidget* list, QPushButton* add, QPushButton* remove,
        QPushButton* up, QPushButton* down, ProfileListPolicy policy)
        : QObject(context), dialogParent_(dialogParent), list_(list), add_(add),
          remove_(remove), up_(up), down_(down), policy_(std::move(policy))
    {
        add_->setEnabled(policy_.enabled);
        QObject::connect(add_, &QPushButton::clicked, this, [this]
        {
            if (!policy_.enabled || !policy_.addProfile) return;
            const QString section = policy_.addProfile();
            if (section.isEmpty()) return;
            if (policy_.markDirty) policy_.markDirty();
            refresh(section);
        });
        QObject::connect(remove_, &QPushButton::clicked, this, [this]
        {
            QListWidgetItem* item = list_->currentItem();
            if (!policy_.enabled || !item || !policy_.removeProfile) return;
            const QString section = item->data(Qt::UserRole).toString();
            if (section.isEmpty()) return;
            const QString shown = item->data(Qt::UserRole + 1).toString();
            QString question = policy_.removeQuestion;
            question.replace(QStringLiteral("%1"), shown);
            if (QMessageBox::question(dialogParent_, policy_.removeTitle,
                question, QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes)
                return;
            const int oldRow = list_->currentRow();
            if (!policy_.removeProfile(section)) return;
            if (policy_.markDirty) policy_.markDirty();
            const QStringList remaining = policy_.sections ?
                policy_.sections() : QStringList();
            refresh(remaining.value(qMin(oldRow, remaining.size() - 1)));
        });
        QObject::connect(up_, &QPushButton::clicked, this,
            [this] { moveCurrent(-1); });
        QObject::connect(down_, &QPushButton::clicked, this,
            [this] { moveCurrent(1); });
        QObject::connect(list_, &QListWidget::currentRowChanged, this,
            [this](int) { updateActions(); });
        QObject::connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int, int, const QModelIndex&, int)
        {
            if (reordering_) return;
            persistOrder();
        });
    }

    void refresh(const QString& wanted = {})
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        const QStringList sections = policy_.sections ?
            policy_.sections() : QStringList();
        int selected = sections.isEmpty() ? -1 : 0;
        for (int index = 0; index < sections.size(); ++index)
        {
            const QString shown = policy_.displayName ?
                policy_.displayName(sections[index]) : sections[index];
            auto* item = new QListWidgetItem(index == 0 &&
                policy_.decorateFirstAsDefault ?
                shown + QStringLiteral("  (Default)") : shown);
            item->setData(Qt::UserRole, sections[index]);
            item->setData(Qt::UserRole + 1, shown);
            item->setSizeHint(QSize(0, 36));
            list_->addItem(item);
            if (sections[index].compare(wanted, Qt::CaseInsensitive) == 0)
                selected = index;
        }
        list_->setCurrentRow(selected);
        updateActions();
        if (policy_.refreshIndicators) policy_.refreshIndicators();
        // currentItemChanged is blocked while rebuilding, so explicitly load
        // the selected row once the list has stable item identities.
        if (currentChanged_) currentChanged_(list_->currentItem());
    }

    void setCurrentChanged(std::function<void(QListWidgetItem*)> callback)
    {
        currentChanged_ = std::move(callback);
        QObject::connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current)
        {
            if (currentChanged_) currentChanged_(current);
        });
    }

    void updateCurrentLabel(const QString& label)
    {
        QListWidgetItem* item = list_->currentItem();
        if (!item) return;
        QString shown = label.trimmed();
        if (shown.isEmpty())
        {
            const QString section = item->data(Qt::UserRole).toString();
            shown = policy_.displayName ? policy_.displayName(section) :
                QStringLiteral("Unnamed profile");
        }
        item->setData(Qt::UserRole + 1, shown);
        item->setText(list_->currentRow() == 0 &&
            policy_.decorateFirstAsDefault ?
            shown + QStringLiteral("  (Default)") : shown);
    }

private:
    void updateActions()
    {
        const int row = list_->currentRow();
        const bool selected = policy_.enabled && row >= 0;
        remove_->setEnabled(selected);
        up_->setEnabled(selected && row > 0);
        down_->setEnabled(selected && row + 1 < list_->count());
    }

    void moveCurrent(int delta)
    {
        if (!policy_.enabled) return;
        const int source = list_->currentRow();
        const int target = source + delta;
        if (source < 0 || target < 0 || target >= list_->count()) return;
        reordering_ = true;
        QListWidgetItem* item = list_->takeItem(source);
        list_->insertItem(target, item);
        list_->setCurrentRow(target);
        reordering_ = false;
        persistOrder();
    }

    void persistOrder()
    {
        if (reordering_ || !policy_.enabled || list_->count() < 1) return;
        reordering_ = true;
        QStringList ordered;
        for (int index = 0; index < list_->count(); ++index)
        {
            QListWidgetItem* item = list_->item(index);
            QString section = item->data(Qt::UserRole).toString();
            if (policy_.normalizeForOrdering)
                section = policy_.normalizeForOrdering(section);
            item->setData(Qt::UserRole, section);
            ordered.push_back(section);
        }
        bool changed = false;
        if (policy_.moveAfter)
            for (int index = 1; index < ordered.size(); ++index)
                changed = policy_.moveAfter(
                    ordered[index], ordered[index - 1]) || changed;
        const QString selected = list_->currentItem() ?
            list_->currentItem()->data(Qt::UserRole).toString() : QString();
        if (changed && policy_.markDirty) policy_.markDirty();
        reordering_ = false;
        refresh(selected);
    }

private:
    QWidget* dialogParent_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    ProfileListPolicy policy_;
    std::function<void(QListWidgetItem*)> currentChanged_;
    bool reordering_ = false;
};
