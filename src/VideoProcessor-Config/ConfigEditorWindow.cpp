#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include "ConfigEditorWindow.h"
#include <ConfigurationApplyPolicy.h>
#include <ConfigurationLiveApply.h>
#include <ActiveProfileStatus.h>

#include <ConfigEditorCore.h>
#include <RendererProfileConfig.h>

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHash>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIntValidator>
#include <QLabel>
#include <QLayout>
#include <QLibrary>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRect>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QShowEvent>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QWindowStateChangeEvent>
#include <QWinEventNotifier>

#include <algorithm>
#include <functional>
#include <cctype>
#include <limits>
#include <map>
#include <set>

namespace
{
constexpr int kPageMargin = 16;
constexpr int kCardPadding = 12;
constexpr int kResponsiveContentWidth = 720;
constexpr int kRendererNameRole = Qt::UserRole + 1;

using DocumentSection = std::map<std::string, std::string>;
using DocumentSnapshot = std::map<std::string, DocumentSection>;

DocumentSnapshot captureDocumentSnapshot(
    const ConfigEditorCore::ConfigDocument& document)
{
    DocumentSnapshot snapshot;
    for (const std::string& section : document.SectionNames())
    {
        snapshot[section];
        for (const auto& setting : document.SectionSettings(section))
            snapshot[section][setting.first] = setting.second;
    }
    return snapshot;
}

std::vector<ConfigurationApplyPolicy::Change> changedDocumentValues(
    const DocumentSnapshot& previous, const DocumentSnapshot& current)
{
    std::set<std::string> names;
    for (const auto& entry : previous) names.insert(entry.first);
    for (const auto& entry : current) names.insert(entry.first);
    std::vector<ConfigurationApplyPolicy::Change> changed;
    for (const std::string& name : names)
    {
        const auto before = previous.find(name);
        const auto after = current.find(name);
        if (before == previous.end())
        {
            if (after->second.empty()) changed.push_back({ name, {} });
            for (const auto& setting : after->second)
                changed.push_back({ name, setting.first });
            continue;
        }
        if (after == current.end())
        {
            if (before->second.empty()) changed.push_back({ name, {} });
            for (const auto& setting : before->second)
                changed.push_back({ name, setting.first });
            continue;
        }
        for (const auto& setting : before->second)
        {
            const auto replacement = after->second.find(setting.first);
            if (replacement == after->second.end() ||
                replacement->second != setting.second)
                changed.push_back({ name, setting.first });
        }
        for (const auto& setting : after->second)
            if (before->second.find(setting.first) == before->second.end())
                changed.push_back({ name, setting.first });
    }
    return changed;
}

bool snapshotUsesDirectShowRenderer(const DocumentSnapshot& snapshot)
{
    for (const auto& section : snapshot)
    {
        if (ConfigurationApplyPolicy::NormalizeSection(section.first) != "general")
            continue;
        for (const auto& setting : section.second)
        {
            if (ConfigFile::NormalizeName(setting.first) != "renderer") continue;
            std::string normalized = setting.second;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return normalized.find("vp renderer") == std::string::npos &&
                normalized.find("videoprocessor renderer") == std::string::npos;
        }
    }
    return true;
}

QString effectCategory(const std::string& section)
{
    const std::string normalized = ConfigurationApplyPolicy::NormalizeSection(section);
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "queue") ||
        normalized == "queue_recovery") return QStringLiteral("Queue");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "directshow"))
        return QStringLiteral("DirectShow");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "vprenderer"))
        return QStringLiteral("VP Renderer");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "viewport") ||
        ConfigurationApplyPolicy::HasPrefix(normalized, "profiles.viewport"))
        return QStringLiteral("Viewport");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "profiles"))
        return QStringLiteral("Profiles");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "lldv"))
        return QStringLiteral("LLDV");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "shader") ||
        ConfigurationApplyPolicy::HasPrefix(normalized, "shaders"))
        return QStringLiteral("Shaders");
    if (ConfigurationApplyPolicy::HasPrefix(normalized, "actions") ||
        ConfigurationApplyPolicy::HasPrefix(normalized, "event_actions"))
        return QStringLiteral("Actions");
    if (normalized == "shortcuts") return QStringLiteral("Shortcuts");
    if (normalized == "logging") return QStringLiteral("Logging");
    if (normalized == "general" || normalized == "command_line" ||
        normalized == "renderer_alias" || normalized == "decklink" ||
        normalized == "p010_conversion" || normalized == "ppm_correction" ||
        normalized == "display_refresh_rate_override")
        return QStringLiteral("Startup / input");
    return QStringLiteral("Preserved content");
}

constexpr int ActiveProfileRole = Qt::UserRole + 12;

class ProfileStateItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem base(option);
        initStyleOption(&base, index);
        const bool active = index.data(ActiveProfileRole).toBool();
        // Keep the standard selection treatment, including its full-width
        // background. Drawing the control ourselves is necessary because the
        // base delegate re-initializes its option and would discard this text
        // offset before it paints.
        // Reserve the marker column for every profile row so names stay
        // aligned as the active selection changes.
        base.text.prepend(QStringLiteral("   "));
        const QWidget* widget = base.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &base, painter, widget);
        if (active)
        {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(QStringLiteral("#26b46b")));
            constexpr int diameter = 8;
            painter->drawEllipse(option.rect.left() + 4,
                option.rect.center().y() - diameter / 2, diameter, diameter);
            painter->restore();
        }
    }
};

// The platform delegate centers its editor using the editor's size hint. Our
// themed QLineEdit has a larger minimum height than the parameter row, which
// made the editor appear to jump downward when a value was clicked. Parameter
// values are all plain text, so give every value cell the same compact editor
// geometry instead of allowing that platform-dependent repositioning.
class ShaderParameterItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
        const QModelIndex& index) const override
    {
        if (index.column() != 1) return nullptr;
        auto* editor = new QLineEdit(parent);
        editor->setObjectName(QStringLiteral("config.shader.nls.parameter_value_editor"));
        // The application theme gives ordinary form fields a comfortable
        // minimum height. Table editors must instead fit the existing row.
        editor->setStyleSheet(QStringLiteral(
            "QLineEdit { min-height: 0; padding: 0 7px; }"));
        editor->setMinimumSize(0, 0);
        editor->setMaximumHeight(QWIDGETSIZE_MAX);
        return editor;
    }

    void updateEditorGeometry(QWidget* editor,
        const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        // Keep a one-pixel inset for the table gridline while occupying the
        // full row height. This is deterministic for every shader parameter.
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }
};

class ResponsiveCardGrid final : public QWidget
{
public:
    ResponsiveCardGrid()
    {
        layout_ = new QGridLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setHorizontalSpacing(14);
        layout_->setVerticalSpacing(14);
        layout_->setAlignment(Qt::AlignTop);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    }

    void addCard(QWidget* card)
    {
        // Cards share their row height on wide, two-column pages while their
        // contents stay top-justified. This keeps the visual grid aligned
        // without allowing controls to drift apart as the window grows.
        card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        if (card->layout()) card->layout()->setAlignment(Qt::AlignTop);
        cards_.push_back(card);
        reflow(width());
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        reflow(event->size().width());
    }

private:
    void reflow(int availableWidth)
    {
        const int columns = availableWidth >= kResponsiveContentWidth ? 2 : 1;
        if (columns != columns_ || layout_->count() != cards_.size())
        {
            columns_ = columns;
            for (QWidget* card : cards_) layout_->removeWidget(card);
            for (int index = 0; index < cards_.size(); ++index)
                layout_->addWidget(cards_[index], index / columns_, index % columns_, Qt::AlignTop);
            for (int column = 0; column < 2; ++column)
                layout_->setColumnStretch(column, column < columns_ ? 1 : 0);
        }

        for (QWidget* card : cards_) card->setMinimumHeight(0);
        if (columns_ != 2) return;

        layout_->activate();
        const int rowCount = (cards_.size() + columns_ - 1) / columns_;
        for (int row = 0; row < rowCount; ++row) layout_->setRowMinimumHeight(row, 0);
        for (int first = 0; first < cards_.size(); first += columns_)
        {
            const int second = first + 1;
            const int rowHeight = second < cards_.size() ?
                qMax(cards_[first]->sizeHint().height(), cards_[second]->sizeHint().height()) :
                cards_[first]->sizeHint().height();
            layout_->setRowMinimumHeight(first / columns_, rowHeight);
            cards_[first]->setMinimumHeight(rowHeight);
            if (second < cards_.size()) cards_[second]->setMinimumHeight(rowHeight);
        }
    }

    QGridLayout* layout_ = nullptr;
    QList<QWidget*> cards_;
    int columns_ = 0;
};

class ResponsiveSplitter final : public QSplitter
{
public:
    ResponsiveSplitter() : QSplitter(Qt::Horizontal)
    {
        setChildrenCollapsible(false);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        const Qt::Orientation wanted =
            event->size().width() >= kResponsiveContentWidth ? Qt::Horizontal : Qt::Vertical;
        if (orientation() != wanted) setOrientation(wanted);
        QSplitter::resizeEvent(event);
    }
};

QLabel* helpLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setProperty("help", true);
    label->setWordWrap(true);
    return label;
}

QLineEdit* readOnlyValue(const QString& text)
{
    auto* edit = new QLineEdit(text);
    edit->setReadOnly(true);
    return edit;
}

QWidget* fieldWithHelp(const QString& labelText, QWidget* field, const QString& help)
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto* label = new QLabel(labelText);
    label->setBuddy(field);
    if (field->accessibleName().isEmpty()) field->setAccessibleName(labelText);
    if (!help.isEmpty()) field->setAccessibleDescription(help);
    layout->addWidget(label);
    layout->addWidget(field);
    if (!help.isEmpty()) layout->addWidget(helpLabel(help));
    return widget;
}

QString displayName(const QString& section, const QString& root)
{
    if (section.compare(root, Qt::CaseInsensitive) == 0) return QStringLiteral("Profile 1");
    QString name = section.mid(root.size() + 1);
    if (root.compare(QStringLiteral("vprenderer.viewport"), Qt::CaseInsensitive) == 0 &&
        name.startsWith(QStringLiteral("viewport_"), Qt::CaseInsensitive))
        name.remove(0, 9);
    name.replace(QStringLiteral("__"), QStringLiteral("\x1"));
    name.replace(u'_', u' ');
    name.replace(QStringLiteral("\x1"), QStringLiteral("_"));
    if (QRegularExpression(QStringLiteral("^profile \\d+$"), QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
        name[0] = name[0].toUpper();
    return name.isEmpty() ? QStringLiteral("Profile") : name;
}

QString profileIdentifier(const QString& name)
{
    QString result;
    for (const QChar character : name.trimmed())
    {
        if (character.unicode() < 128 && character.isLetterOrNumber()) result += character;
        else if (character == u'-') result += character;
        else if (character == u'_') result += QStringLiteral("__");
        else if (character.isSpace()) result += u'_';
        else result += u'_';
    }
    while (result.contains(QStringLiteral("___"))) result.replace(QStringLiteral("___"), QStringLiteral("__"));
    if (result.isEmpty() || !result.front().isLetter()) result.prepend(QStringLiteral("profile_"));
    return result;
}

QString canonicalShortcutText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return trimmed;
    std::string canonical;
    return RendererProfileConfig::CanonicalizeKeyChord(
        trimmed.toStdString(), canonical) ?
        QString::fromStdString(canonical) : trimmed;
}

bool configuredBooleanValue(const QString& value, bool defaultValue)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) return defaultValue;
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("yes") ||
        normalized == QStringLiteral("1") || normalized == QStringLiteral("on"))
        return true;
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("no") ||
        normalized == QStringLiteral("0") || normalized == QStringLiteral("off"))
        return false;
    return defaultValue;
}

QCheckBox* configuredCheck(const QString& text, const QString& value, bool defaultValue = false)
{
    auto* check = new QCheckBox(text);
    check->setTristate(false);
    check->setChecked(configuredBooleanValue(value, defaultValue));
    check->setEnabled(false);
    return check;
}

QStringList discoverValues(const char* exportName, bool hideLegacyRenderers = true)
{
    static QLibrary library(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("VideoProcessorConfigDiscovery.dll")));
    if (!library.load()) return {};
    using DiscoverNoArgument = wchar_t* (__stdcall*)();
    using DiscoverRenderers = wchar_t* (__stdcall*)(BOOL);
    wchar_t* values = nullptr;
    if (qstrcmp(exportName, "VPDiscoverRenderers") == 0)
    {
        auto discover = reinterpret_cast<DiscoverRenderers>(library.resolve(exportName));
        if (discover) values = discover(hideLegacyRenderers ? TRUE : FALSE);
    }
	else
	{
        auto discover = reinterpret_cast<DiscoverNoArgument>(library.resolve(exportName));
        if (discover) values = discover();
    }
    QStringList result;
    for (const wchar_t* current = values; current && *current; current += wcslen(current) + 1)
        result.push_back(QString::fromWCharArray(current));
    if (values) CoTaskMemFree(values);
    return result;
}

QStringList discoverCaptureConnections(const QString& captureDeviceName)
{
    static QLibrary library(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("VideoProcessorConfigDiscovery.dll")));
    if (!library.load()) return {};
    using DiscoverConnections = wchar_t* (__stdcall*)(const wchar_t*);
    auto discover = reinterpret_cast<DiscoverConnections>(
        library.resolve("VPDiscoverCaptureConnections"));
    if (!discover) return {};
    wchar_t* values = discover(reinterpret_cast<const wchar_t*>(
        captureDeviceName.utf16()));
    QStringList result;
    for (const wchar_t* current = values; current && *current;
        current += wcslen(current) + 1)
        result.push_back(QString::fromWCharArray(current));
    if (values) CoTaskMemFree(values);
    return result;
}

QStringList withDefaultChoice(QStringList values)
{
    values.prepend(QString());
    return values;
}

void openValidationError(QWidget* parent, const QString& message)
{
    auto* dialog = new QDialog(parent, Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
    dialog->setWindowTitle(QStringLiteral("Unable to save configuration"));
    dialog->setObjectName(QStringLiteral("noticeDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setMinimumWidth(440);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(16);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto* headingRow = new QHBoxLayout;
    headingRow->setSpacing(12);
    auto* badge = new QLabel(QStringLiteral("!"));
    badge->setObjectName(QStringLiteral("errorBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(34, 34);
    auto* heading = new QLabel(QStringLiteral("Check this setting"));
    heading->setObjectName(QStringLiteral("noticeTitle"));
    headingRow->addWidget(badge);
    headingRow->addWidget(heading, 1);
    layout->addLayout(headingRow);

    auto* detail = new QLabel(message);
    detail->setObjectName(QStringLiteral("noticeMessage"));
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(detail);

    auto* actions = new QHBoxLayout;
    actions->addStretch();
    auto* ok = new QPushButton(QStringLiteral("OK"));
    ok->setProperty("primary", true);
    ok->setDefault(true);
    actions->addWidget(ok);
    layout->addLayout(actions);
    QObject::connect(ok, &QPushButton::clicked, dialog, &QDialog::accept);
    dialog->open();
}

bool isSharedInputSetting(const QString& key)
{
    return key == QStringLiteral("video_conversion") ||
        key == QStringLiteral("container_colorspace") ||
        key == QStringLiteral("hdr_colorspace") ||
        key == QStringLiteral("hdr_luminance");
}

bool isShaderStructuralKey(const QString& key)
{
    static const QStringList keys = {
        QStringLiteral("label"), QStringLiteral("shortcut"), QStringLiteral("when"),
        QStringLiteral("type"),
        QStringLiteral("shader_type"), QStringLiteral("hlsl_file"),
        QStringLiteral("glsl_file"), QStringLiteral("stage"), QStringLiteral("order")
    };
    return keys.contains(key, Qt::CaseInsensitive);
}

struct ShaderEditorState
{
    QString section;
    bool loading = false;
};

struct ShaderParameterControls
{
    QToolButton* toggle = nullptr;
    QWidget* fields = nullptr;
    QTableWidget* table = nullptr;
    std::function<void()> reload;
};

ShaderParameterControls addShaderParameterControls(QWidget* dialogParent,
    QObject* signalContext, QVBoxLayout* detailsLayout,
    const std::shared_ptr<ShaderEditorState>& state,
    std::function<ConfigEditorCore::ConfigDocument*()> document,
    std::function<void()> markDirty, const QString& objectPrefix,
    const QString& accessibleName, const QString& removeDescription)
{
    ShaderParameterControls result;
    result.toggle = new QToolButton;
    result.toggle->setObjectName(objectPrefix + QStringLiteral(".parameters_toggle"));
    result.toggle->setText(QStringLiteral("Custom parameters"));
    result.toggle->setProperty("profileSection", true);
    result.toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    result.toggle->setArrowType(Qt::RightArrow);
    result.toggle->setCheckable(true);
    result.toggle->setChecked(false);
    result.toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    result.toggle->setAccessibleDescription(
        QStringLiteral("Expand or collapse custom parameters."));
    detailsLayout->addWidget(result.toggle);
    result.fields = new QWidget;
    auto* parameterLayout = new QVBoxLayout(result.fields);
    parameterLayout->setContentsMargins(0, 0, 0, 0);
    parameterLayout->setSpacing(10);
    result.table = new QTableWidget(0, 2);
    result.table->setAccessibleName(accessibleName);
    result.table->setAccessibleDescription(
        QStringLiteral("Shader-specific parameter names and values."));
    result.table->setObjectName(objectPrefix + QStringLiteral(".parameters"));
    result.table->setHorizontalHeaderLabels({ QStringLiteral("Parameter"), QStringLiteral("Value") });
    result.table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    result.table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    result.table->verticalHeader()->setVisible(false);
    result.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    result.table->setSelectionMode(QAbstractItemView::SingleSelection);
    result.table->setItemDelegateForColumn(1,
        new ShaderParameterItemDelegate(result.table));
    auto updateHeight = [table = result.table]
    {
        const int frame = table->frameWidth() * 2;
        const int header = table->horizontalHeader()->height();
        int rows = 0;
        for (int row = 0; row < table->rowCount(); ++row)
            rows += table->rowHeight(row);
        table->setFixedHeight(frame + header + rows);
    };
    result.table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("+ Add parameter"));
    auto* remove = new QPushButton(QStringLiteral("Remove selected"));
    remove->setProperty("danger", true);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    parameterLayout->addWidget(new QLabel(QStringLiteral("Custom parameters")));
    parameterLayout->addWidget(helpLabel(QStringLiteral(
        "Optional implementation-specific key/value pairs. Untouched keys and comments are preserved.")));
    parameterLayout->addWidget(result.table);
    parameterLayout->addLayout(buttons);
    result.fields->setVisible(false);
    detailsLayout->addWidget(result.fields);
    QObject::connect(result.table->model(), &QAbstractItemModel::rowsInserted, signalContext,
        [updateHeight](const QModelIndex&, int, int) { updateHeight(); });
    QObject::connect(result.table->model(), &QAbstractItemModel::rowsRemoved, signalContext,
        [updateHeight](const QModelIndex&, int, int) { updateHeight(); });
    QObject::connect(result.toggle, &QToolButton::toggled, signalContext,
        [toggle = result.toggle, fields = result.fields, state](bool open)
    {
        fields->setVisible(open && !state->section.isEmpty());
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    QObject::connect(result.table, &QTableWidget::cellChanged, signalContext,
        [state, table = result.table, document, markDirty](int row, int column)
    {
        ConfigEditorCore::ConfigDocument* current = document();
        if (state->loading || state->section.isEmpty() || !current || column != 1) return;
        const auto* keyItem = table->item(row, 0);
        const auto* valueItem = table->item(row, 1);
        if (!keyItem || !valueItem) return;
        current->SetKnown(state->section.toStdString(),
            keyItem->text().toLocal8Bit().constData(),
            valueItem->text().toLocal8Bit().constData());
        markDirty();
    });
    QObject::connect(add, &QPushButton::clicked, signalContext,
        [dialogParent, state, table = result.table, document, markDirty]
    {
        ConfigEditorCore::ConfigDocument* current = document();
        if (state->section.isEmpty() || !current) return;
        bool accepted = false;
        const QString key = QInputDialog::getText(dialogParent,
            QStringLiteral("Add shader parameter"), QStringLiteral("Parameter name"),
            QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted || key.isEmpty()) return;
        if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]*$")).match(key).hasMatch() ||
            isShaderStructuralKey(key))
        {
            QMessageBox::warning(dialogParent, QStringLiteral("Shader parameter"),
                QStringLiteral("Use a non-reserved key beginning with a letter or underscore."));
            return;
        }
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->text().compare(key, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(dialogParent, QStringLiteral("Shader parameter"),
                    QStringLiteral("That parameter already exists."));
                return;
            }
        const QString value = QInputDialog::getText(dialogParent,
            QStringLiteral("Add shader parameter"), QStringLiteral("Value"),
            QLineEdit::Normal, QString(), &accepted);
        if (!accepted) return;
        current->SetKnown(state->section.toStdString(), key.toLocal8Bit().constData(),
            value.toLocal8Bit().constData());
        state->loading = true;
        const int row = table->rowCount();
        table->insertRow(row);
        auto* keyItem = new QTableWidgetItem(key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, keyItem);
        table->setItem(row, 1, new QTableWidgetItem(value));
        state->loading = false;
        markDirty();
    });
    QObject::connect(remove, &QPushButton::clicked, signalContext,
        [dialogParent, state, table = result.table, document, markDirty, removeDescription]
    {
        ConfigEditorCore::ConfigDocument* current = document();
        const int row = table->currentRow();
        if (state->section.isEmpty() || !current || row < 0) return;
        const QString key = table->item(row, 0)->text();
        if (QMessageBox::question(dialogParent, QStringLiteral("Remove shader parameter"),
            QStringLiteral("Remove '%1' from this %2?").arg(key, removeDescription),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) return;
        current->RemoveKnown(state->section.toStdString(), key.toLocal8Bit().constData());
        state->loading = true;
        table->removeRow(row);
        state->loading = false;
        markDirty();
    });
    result.reload = [state, table = result.table, toggle = result.toggle,
        fields = result.fields, document, updateHeight]
    {
        ConfigEditorCore::ConfigDocument* current = document();
        const bool available = !state->section.isEmpty() && current;
        toggle->setVisible(available);
        fields->setVisible(available && toggle->isChecked());
        table->setRowCount(0);
        if (!available) return;
        for (const auto& setting : current->SectionSettings(state->section.toStdString()))
        {
            const QString key = QString::fromLocal8Bit(setting.first.c_str());
            if (isShaderStructuralKey(key)) continue;
            const int row = table->rowCount();
            table->insertRow(row);
            auto* keyItem = new QTableWidgetItem(key);
            keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(row, 0, keyItem);
            table->setItem(row, 1,
                new QTableWidgetItem(QString::fromLocal8Bit(setting.second.c_str())));
        }
        updateHeight();
    };
    return result;
}

QString controlName(const QString& section, const QString& key)
{
    return QStringLiteral("config.%1.%2").arg(section, key);
}

QString accessibleSettingName(QString key)
{
    key.replace(u'_', u' ');
    key.replace(u'.', u' ');
    if (!key.isEmpty()) key[0] = key[0].toUpper();
    return key;
}

bool openPathExternally(const QString& path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

QString friendlyChoiceLabel(const QString& raw)
{
    static const QHash<QString, QString> labels = {
        { QStringLiteral("AUTO"), QStringLiteral("Auto") },
        { QStringLiteral("NONE"), QStringLiteral("Disabled") },
        { QStringLiteral("V210_TO_P010"), QStringLiteral("V210 to P010") },
        { QStringLiteral("FOLLOW_INPUT"), QStringLiteral("Follow input") },
        { QStringLiteral("FOLLOW_INPUT_LLDV"), QStringLiteral("Follow input (LLDV)") },
        { QStringLiteral("FOLLOW_CONTAINER"), QStringLiteral("Follow container") },
        { QStringLiteral("HDR_LUMINANCE_USER"), QStringLiteral("User values") },
        { QStringLiteral("FAST"), QStringLiteral("Fast") },
        { QStringLiteral("BALANCED"), QStringLiteral("Balanced") },
        { QStringLiteral("HIGH"), QStringLiteral("High") },
        { QStringLiteral("DIRECT"), QStringLiteral("Direct") },
        { QStringLiteral("COMPOSED"), QStringLiteral("Composed") },
        { QStringLiteral("FULL"), QStringLiteral("Full") },
        { QStringLiteral("LIMITED"), QStringLiteral("Limited") },
        { QStringLiteral("SMALL"), QStringLiteral("Small") },
        { QStringLiteral("ON"), QStringLiteral("On") },
        { QStringLiteral("OFF"), QStringLiteral("Off") },
        { QStringLiteral("LIGHT"), QStringLiteral("Light") },
        { QStringLiteral("DEFAULT"), QStringLiteral("Standard") },
        { QStringLiteral("BT1886"), QStringLiteral("BT.1886") },
        { QStringLiteral("SRGB"), QStringLiteral("sRGB") },
        { QStringLiteral("REC709"), QStringLiteral("Rec. 709") },
        { QStringLiteral("REC601_525"), QStringLiteral("Rec. 601 (525-line)") },
        { QStringLiteral("REC601_625"), QStringLiteral("Rec. 601 (625-line)") },
        { QStringLiteral("BT709"), QStringLiteral("BT.709") },
        { QStringLiteral("BT601"), QStringLiteral("BT.601") },
        { QStringLiteral("BT2020"), QStringLiteral("BT.2020") },
        { QStringLiteral("BT2020_CONST"), QStringLiteral("BT.2020 constant luminance") },
        { QStringLiteral("BT2020_10"), QStringLiteral("BT.2020 (10-bit)") },
        { QStringLiteral("BT2020_12"), QStringLiteral("BT.2020 (12-bit)") },
        { QStringLiteral("P3_D65"), QStringLiteral("P3-D65") },
        { QStringLiteral("P3_DCI"), QStringLiteral("DCI-P3") },
        { QStringLiteral("P3_D60"), QStringLiteral("P3-D60") },
        { QStringLiteral("HIGH_QUALITY"), QStringLiteral("High quality") },
        { QStringLiteral("EWA_LANCZOSSHARP"), QStringLiteral("EWA Lanczos sharp") },
        { QStringLiteral("EWA_LANCZOS"), QStringLiteral("EWA Lanczos") },
        { QStringLiteral("EWA_LANCZOS4SHARPEST"), QStringLiteral("EWA Lanczos 4 sharpest") },
        { QStringLiteral("CATMULL_ROM"), QStringLiteral("Catmull-Rom") },
        { QStringLiteral("NEAREST"), QStringLiteral("Nearest") },
        { QStringLiteral("OVERSAMPLE"), QStringLiteral("Oversample") },
        { QStringLiteral("GAUSSIAN"), QStringLiteral("Gaussian") },
        { QStringLiteral("LANCZOS"), QStringLiteral("Lanczos") },
        { QStringLiteral("BOX"), QStringLiteral("Box") },
        { QStringLiteral("HERMITE"), QStringLiteral("Hermite") },
        { QStringLiteral("MITCHELL"), QStringLiteral("Mitchell") },
        { QStringLiteral("BICUBIC"), QStringLiteral("Bicubic") },
        { QStringLiteral("BILINEAR"), QStringLiteral("Bilinear") },
        { QStringLiteral("SPLINE"), QStringLiteral("Spline") },
        { QStringLiteral("BT2390"), QStringLiteral("BT.2390") },
        { QStringLiteral("ST2094-40"), QStringLiteral("ST 2094-40") },
        { QStringLiteral("REINHARD"), QStringLiteral("Reinhard") },
        { QStringLiteral("PERCEPTUAL"), QStringLiteral("Perceptual") },
        { QStringLiteral("SOFTCLIP"), QStringLiteral("Soft clip") },
        { QStringLiteral("RELATIVE"), QStringLiteral("Relative") },
        { QStringLiteral("DESATURATE"), QStringLiteral("Desaturate") },
        { QStringLiteral("GAMMA_1.8"), QStringLiteral("Gamma 1.8") },
        { QStringLiteral("GAMMA_2.0"), QStringLiteral("Gamma 2.0") },
        { QStringLiteral("GAMMA_2.2"), QStringLiteral("Gamma 2.2") },
        { QStringLiteral("GAMMA_2.6"), QStringLiteral("Gamma 2.6") },
        { QStringLiteral("GAMMA_2.8"), QStringLiteral("Gamma 2.8") },
        { QStringLiteral("LINEAR_RGB"), QStringLiteral("Linear RGB") },
        { QStringLiteral("8BIT_GAMMA_2.2"), QStringLiteral("8-bit gamma 2.2") },
        { QStringLiteral("LOG_100_1"), QStringLiteral("Log 100:1") },
        { QStringLiteral("LOG_316_1"), QStringLiteral("Log 316:1") },
        { QStringLiteral("HYBRID_LOG_GAMMA"), QStringLiteral("HLG") },
        { QStringLiteral("240M"), QStringLiteral("SMPTE 240M") },
        { QStringLiteral("YCGCO"), QStringLiteral("YCgCo") },
        { QStringLiteral("NTSC_SYSM"), QStringLiteral("NTSC System M") },
        { QStringLiteral("NTSC_SYSBG"), QStringLiteral("NTSC System B/G") },
        { QStringLiteral("CIE1931_ZYX"), QStringLiteral("CIE 1931 ZYX") },
        { QStringLiteral("ACES"), QStringLiteral("ACES") },
        { QStringLiteral("DCI-P3"), QStringLiteral("DCI-P3") },
        { QStringLiteral("PQ"), QStringLiteral("PQ") },
        { QStringLiteral("FCC"), QStringLiteral("FCC") }
    };
    return labels.value(raw.trimmed().toUpper(), raw);
}
}

QRect ConfigEditorPlacement::ClampFrameToWorkArea(const QRect& frame,
    const QRect& workArea)
{
    if (!frame.isValid() || !workArea.isValid()) return frame;
    const int maximumX = workArea.right() - frame.width() + 1;
    const int maximumY = workArea.bottom() - frame.height() + 1;
    const int x = frame.width() <= workArea.width() ?
        std::max(workArea.left(), std::min(frame.left(), maximumX)) :
        workArea.left();
    const int y = frame.height() <= workArea.height() ?
        std::max(workArea.top(), std::min(frame.top(), maximumY)) :
        workArea.top();
    return QRect(x, y, frame.width(), frame.height());
}

ConfigEditorWindow::ConfigEditorWindow(QString configPath, quintptr ownerHandle,
    bool testMode, const QStringList& testFilteredRenderers,
    const QStringList& testAllRenderers)
    : configPath_(std::move(configPath)), ownerHandle_(ownerHandle),
      testMode_(testMode),
      document_(std::make_unique<ConfigEditorCore::ConfigDocument>())
{
    // A VP-launched editor can acquire a native HWND while it is still being
    // constructed. Keep that frame off-screen until reveal() has finished
    // layout/polish work and the event loop is ready to paint it.
    if (!testMode_)
        setAttribute(Qt::WA_DontShowOnScreen, true);
    // Windows advertises a roughly 150 ms combo-box animation through Qt.
    // Config is a dense control surface; dropdowns should open synchronously.
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    if (ownerHandle_)
    {
        DWORD processId = 0;
        if (IsWindow(reinterpret_cast<HWND>(ownerHandle_)))
            GetWindowThreadProcessId(reinterpret_cast<HWND>(ownerHandle_), &processId);
        ownerProcessId_ = processId;
    }
    setWindowTitle(QStringLiteral("VideoProcessor Configuration"));
    setAccessibleName(QStringLiteral("VideoProcessor Configuration"));
    setObjectName(QStringLiteral("root"));
    // This must be a Qt-owned flag, not only a later SetWindowPos call: the
    // QWindows backend can otherwise recreate or restyle the native HWND and
    // silently remove WS_EX_TOPMOST after Config has been revealed.
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    // This is the smallest dashboard layout that keeps both card columns,
    // navigation, and the fixed action footer usable without clipping.
    resize(1040, 700);
    setMinimumSize(1040, 700);
    loadConfiguration();
    migrateLldvSingleton();
    migrateSharedRefreshRate();
    migrateSeparatedRendererProfiles();
    migrateViewportZoomProfiles();
    if (!testMode_) loadDiscoveryCache();
    else
    {
        filteredRenderers_ = testFilteredRenderers;
        allRenderers_ = testAllRenderers;
    }
    setCentralWidget(createShell());
    if (!testMode_)
    {
        activeProfileTimer_ = new QTimer(this);
        connect(activeProfileTimer_, &QTimer::timeout, this,
            &ConfigEditorWindow::refreshActiveProfileIndicators);
        refreshActiveProfileIndicators();
        shaderCacheStatusTimer_ = new QTimer(this);
        shaderCacheStatusTimer_->setInterval(2000);
        connect(shaderCacheStatusTimer_, &QTimer::timeout, this,
            &ConfigEditorWindow::refreshShaderCacheStatus);
        shaderCacheStatusTimer_->start();
    }
    if (!testMode_) setupTray();
	const std::wstring revealEventName =
		ConfigurationLiveApply::ConfigurationEditorRevealEventName(
			GetCurrentProcessId());
	revealEvent_ = CreateEventW(nullptr, FALSE, FALSE,
		revealEventName.c_str());
	if (revealEvent_)
	{
		revealEventNotifier_ = new QWinEventNotifier(revealEvent_, this);
		connect(revealEventNotifier_, &QWinEventNotifier::activated, this,
			[this]
		{
			// Consume the auto-reset event through the notifier and route the
			// command through the same current-window path as a tray click.
			reveal();
		});
	}
    const QKeySequence configToggle(value(QStringLiteral("shortcuts"),
        QStringLiteral("config_editor"), QStringLiteral("Ctrl+Shift+S")));
    if (!configToggle.isEmpty())
    {
        auto* toggleShortcut = new QShortcut(configToggle, this);
        toggleShortcut->setContext(Qt::ApplicationShortcut);
        connect(toggleShortcut, &QShortcut::activated, this, [this]
        {
            // The Config shortcut is a reveal command everywhere, including
            // when Qt itself owns the keyboard focus. It must not toggle the
            // editor back into the tray.
            reveal();
        });
    }

    if (ownerHandle_)
    {
        // A background/warm editor still needs a usable native receiver.  Do
        // not wait for the first visible show cycle: VP may already have a
        // pending reveal by the time it acknowledges this association.
        (void)winId();
        applyNativeOwner();
        publishNativeAssociation();
    }
}

ConfigEditorWindow::~ConfigEditorWindow()
{
	if (monitorDiscoveryThread_)
	{
		monitorDiscoveryThread_->requestInterruption();
		monitorDiscoveryThread_->wait();
		monitorDiscoveryThread_ = nullptr;
	}
	if (revealEventNotifier_)
		revealEventNotifier_->setEnabled(false);
	if (revealEvent_)
	{
		CloseHandle(revealEvent_);
		revealEvent_ = nullptr;
	}
    removeScopedTopmost();
    clearNativeOwner();
}

void ConfigEditorWindow::selectPage(int index)
{
    if (!pages_ || index < 0 || index >= pages_->count()) return;
    pages_->setCurrentIndex(index);
    if (!navigation_) return;
    const int navigationIndex = index == 4 || index == 11 || index == 13 ? 2 :
        index == 12 ? 3 :
        index == 8 || index == 9 ? 8 : index;
    for (QAbstractButton* button : navigation_->findChildren<QAbstractButton*>())
    {
        const bool selected = button->property("pageIndex").isValid() &&
            button->property("pageIndex").toInt() == navigationIndex;
        if (button->isCheckable()) button->setChecked(selected);
        if (selected)
        {
            QWidget* container = button->parentWidget();
            if (container && container->property("navChildren").toBool())
            {
                container->setVisible(true);
                if (auto* header = qobject_cast<QToolButton*>(container->property("navHeader").value<QObject*>()))
                    header->setArrowType(Qt::DownArrow);
            }
        }
    }
}

void ConfigEditorWindow::loadConfiguration()
{
    document_ = std::make_unique<ConfigEditorCore::ConfigDocument>();
    std::wstring error;
	configurationLoaded_ = document_->Load(QFileInfo(configPath_).absoluteFilePath().toStdWString(), error);
	hasPendingMigrations_ = false;
	savedSnapshot_ = configurationLoaded_ ? captureDocumentSnapshot(*document_) :
		DocumentSnapshot{};
	bool migratedLegacyInputPolicy = false;
	if (configurationLoaded_)
	{
		for (const char* inputKey : { "video_conversion", "container_colorspace",
			"hdr_colorspace", "hdr_luminance" })
		{
			const std::string general = document_->Get("general", inputKey);
			const std::string directShow = document_->Get("directshow", inputKey);
			const std::string vpRendererInput =
				document_->Get("vprenderer.input_processing", inputKey);
			const std::string interimVpRendererInput =
				document_->Get("vprenderer.input", inputKey);
			const std::string legacyVpRenderer =
				document_->Get("vprenderer", inputKey);
			if (!interimVpRendererInput.empty())
			{
				if (vpRendererInput.empty())
				{
					document_->AddSection("vprenderer.input_processing");
					document_->SetKnown("vprenderer.input_processing", inputKey,
						interimVpRendererInput);
				}
				document_->RemoveKnown("vprenderer.input", inputKey);
				migratedLegacyInputPolicy = true;
			}
			if (!legacyVpRenderer.empty())
			{
				if (vpRendererInput.empty() && interimVpRendererInput.empty())
				{
					document_->AddSection("vprenderer.input_processing");
					document_->SetKnown("vprenderer.input_processing", inputKey,
						legacyVpRenderer);
				}
				document_->RemoveKnown("vprenderer", inputKey);
				migratedLegacyInputPolicy = true;
			}
			if (general.empty() && !directShow.empty() &&
				vpRendererInput.empty() && interimVpRendererInput.empty() &&
				legacyVpRenderer.empty())
			{
				document_->SetKnown("general", inputKey, directShow);
				document_->RemoveKnown("directshow", inputKey);
				migratedLegacyInputPolicy = true;
			}
		}
		if (document_->SectionSettings("vprenderer.input").empty())
			document_->RemoveSection("vprenderer.input");
		if (document_->SectionSettings("vprenderer").empty())
			document_->RemoveSection("vprenderer");
	}
	dirty_ = configurationLoaded_ && (!document_->existedAtLoad ||
		migratedLegacyInputPolicy);
	hasPendingMigrations_ = migratedLegacyInputPolicy;
}

void ConfigEditorWindow::migrateViewportZoomProfiles()
{
    if (!configurationLoaded_ || !document_) return;

    const QString legacyRoot = QStringLiteral("vprenderer.viewport");
    const QStringList legacySections = profileSections(legacyRoot);
    if (legacySections.isEmpty()) return;

    const QStringList zoomKeys = {
        QStringLiteral("automatic_crop"),
        QStringLiteral("crop_narrower_content_to_fill_screen"),
        QStringLiteral("crop_narrower_content_aspect_limit"),
        QStringLiteral("crop_wider_content_to_fill_screen"),
        QStringLiteral("crop_wider_content_aspect_limit"),
        QStringLiteral("subtitle_fit"),
        QStringLiteral("hdr_peak_analysis_picture_only"),
        QStringLiteral("subtitle_hold_seconds"),
        QStringLiteral("subtitle_engage_drift_ms"),
        QStringLiteral("subtitle_release_drift_ms"),
        QStringLiteral("subtitle_padding_pixels"),
        QStringLiteral("subtitle_target_buffer_pixels") };
    const QStringList screenKeys = {
        QStringLiteral("screen_aspect"),
        QStringLiteral("vertical_alignment"),
        QStringLiteral("anamorphic_enabled"),
        QStringLiteral("anamorphic_scale") };

    bool migrated = false;
    for (const QString& legacySection : legacySections)
    {
        QStringList configuredZoomKeys;
        for (const QString& key : zoomKeys)
            if (!value(legacySection, key).isEmpty())
                configuredZoomKeys.push_back(key);
        if (configuredZoomKeys.isEmpty()) continue;

        const QString suffix = legacySection.compare(legacyRoot,
            Qt::CaseInsensitive) == 0 ? QStringLiteral("profile_1") :
            legacySection.mid(legacyRoot.size() + 1);
        const QString zoomSection = QStringLiteral("vprenderer.zoom.%1")
            .arg(suffix);
        document_->AddSection(zoomSection.toStdString());
        const QString label = value(legacySection, QStringLiteral("label"));
        if (!label.isEmpty() && value(zoomSection,
            QStringLiteral("label")).isEmpty())
            document_->SetKnown(zoomSection.toStdString(), "label",
                label.toLocal8Bit().constData());
        for (const QString& key : configuredZoomKeys)
        {
            const QString configured = value(legacySection, key);
            if (value(zoomSection, key).isEmpty())
                document_->SetKnown(zoomSection.toStdString(),
                    key.toLocal8Bit().constData(),
                    configured.toLocal8Bit().constData());
            document_->RemoveKnown(legacySection.toStdString(),
                key.toLocal8Bit().constData());
        }
        // A legacy viewport shortcut selected all viewport settings. Copy it
        // only to the new Zoom profile when that legacy profile actually
        // owned Zoom fields. This preserves F2's matching Scope zoom profile
        // while letting Shift+2 affect Zoom alone.
        for (const QString& selector : { QStringLiteral("shortcut"),
            QStringLiteral("when") })
        {
            const QString configured = value(legacySection, selector);
            if (!configured.isEmpty() && value(zoomSection, selector).isEmpty())
                document_->SetKnown(zoomSection.toStdString(),
                    selector.toLocal8Bit().constData(),
                    configured.toLocal8Bit().constData());
        }

        bool hasScreenSetting = false;
        for (const QString& key : screenKeys)
            if (!value(legacySection, key).isEmpty())
            {
                hasScreenSetting = true;
                break;
            }
        // A crop-only legacy profile must not remain a selectable Screen
        // profile. If it did, Shift+2 would still replace F2's Scope screen
        // with the inherited 16:9 baseline.
        if (!hasScreenSetting)
            document_->RemoveSection(legacySection.toStdString());
        migrated = true;
    }
    if (migrated)
    {
        dirty_ = true;
        hasPendingMigrations_ = true;
    }
}

void ConfigEditorWindow::migrateSeparatedRendererProfiles()
{
    if (!configurationLoaded_ || !document_) return;

    // Scaling, Color, and Output used to live together in Rendering.  They
    // are now independent profile families, so moving settings must also
    // remove a source profile which contains nothing except its old selector.
    // Leaving that shell behind is not benign: its shortcut would still select
    // Rendering and replace an unrelated active Rendering baseline.
    struct Transfer
    {
        const char* targetRoot;
        QStringList keys;
    };
    const std::vector<Transfer> transfers = {
        { "vprenderer.output", { QStringLiteral("output_path_profile"),
            QStringLiteral("output_presentation"),
            QStringLiteral("output_range"),
            QStringLiteral("output_transport_gamma"),
            QStringLiteral("output_diagnostics"),
            QStringLiteral("diagnostic_allow_limited_g22"),
            QStringLiteral("diagnostic_allow_full_g22"),
            QStringLiteral("diagnostic_disable_compute"),
            QStringLiteral("diagnostic_force_8bit_sdr_swapchain"),
            QStringLiteral("diagnostic_vp_owned_dxgi_presenter"),
            QStringLiteral("diagnostic_disable_shader_cache") } },
        { "vprenderer.color", { QStringLiteral("sdr_target_primaries"),
            QStringLiteral("output_gamma"),
            QStringLiteral("report_bt2020_to_display"),
            QStringLiteral("sdr_adjust_gamma"),
            QStringLiteral("sdr_input_transfer") } },
        { "vprenderer.scaling", { QStringLiteral("upscaler"),
            QStringLiteral("downscaler"), QStringLiteral("sigmoid") } }
    };

    bool migrated = false;
    const QString renderingRoot = QStringLiteral("vprenderer");
    const QStringList legacySections = profileSections(renderingRoot);
    for (const QString& renderingSection : legacySections)
    {
        const QString suffix = renderingSection.compare(renderingRoot,
            Qt::CaseInsensitive) == 0 ? QStringLiteral("Default") :
            renderingSection.mid(renderingRoot.size() + 1);
        for (const Transfer& transfer : transfers)
        {
            QStringList configuredKeys;
            for (const QString& key : transfer.keys)
                if (!value(renderingSection, key).isEmpty())
                    configuredKeys.push_back(key);
            if (configuredKeys.isEmpty()) continue;

            const QString targetSection = QString::fromLatin1(transfer.targetRoot) +
                QStringLiteral(".%1").arg(suffix);
            document_->AddSection(targetSection.toStdString());
            for (const QString& key : configuredKeys)
            {
                const QString configured = value(renderingSection, key);
                if (value(targetSection, key).isEmpty())
                    document_->SetKnown(targetSection.toStdString(),
                        key.toStdString().c_str(),
                        configured.toLocal8Bit().constData());
                document_->RemoveKnown(renderingSection.toStdString(),
                    key.toStdString().c_str());
            }
            // A legacy selector selected all of its settings.  Copy it only to
            // a target family that actually received settings from this
            // profile; the old family is pruned below when it owns nothing.
            for (const QString& selector : { QStringLiteral("shortcut"),
                QStringLiteral("when") })
            {
                const QString configured = value(renderingSection, selector);
                if (!configured.isEmpty() && value(targetSection, selector).isEmpty())
                    document_->SetKnown(targetSection.toStdString(),
                        selector.toStdString().c_str(),
                        configured.toLocal8Bit().constData());
            }
            // Before Output had its own family, output_gamma doubled as the
            // transport gamma for a limited RGB experiment. Preserve that
            // legacy meaning before Color takes ownership of output_gamma.
            if (QString::fromLatin1(transfer.targetRoot) ==
                QStringLiteral("vprenderer.output") &&
                value(targetSection, QStringLiteral("output_range")).compare(
                    QStringLiteral("limited"), Qt::CaseInsensitive) == 0 &&
                value(targetSection, QStringLiteral("output_transport_gamma")).isEmpty())
            {
                const QString legacyGamma = value(renderingSection,
                    QStringLiteral("output_gamma")).toLower();
                if (legacyGamma == QStringLiteral("2.2") ||
                    legacyGamma == QStringLiteral("2.4"))
                    document_->SetKnown(targetSection.toStdString(),
                        "output_transport_gamma",
                        legacyGamma.toLocal8Bit().constData());
            }
            migrated = true;
        }

        bool hasRenderingSetting = false;
        for (const auto& setting : document_->SectionSettings(
            renderingSection.toStdString()))
        {
            const QString key = QString::fromLocal8Bit(setting.first.c_str());
            if (key.compare(QStringLiteral("shortcut"), Qt::CaseInsensitive) != 0 &&
                key.compare(QStringLiteral("when"), Qt::CaseInsensitive) != 0)
            {
                hasRenderingSetting = true;
                break;
            }
        }
        if (!hasRenderingSetting &&
            document_->RemoveSection(renderingSection.toStdString()))
            migrated = true;
    }
    if (migrated)
    {
        dirty_ = true;
        hasPendingMigrations_ = true;
    }
}

void ConfigEditorWindow::loadDiscoveryCache()
{
    // Hardware discovery can enumerate COM registrations and capture/display
    // devices. Do it once before the pages are created; General, Actions, and
    // Shortcuts then share an immutable startup snapshot.
    captureDevices_ = discoverValues("VPDiscoverCaptureDevices");
    for (const QString& captureDevice : captureDevices_)
        captureConnections_.insert(captureDevice,
            discoverCaptureConnections(captureDevice));
    monitors_ = discoverValues("VPDiscoverMonitors");
    filteredRenderers_ = discoverValues("VPDiscoverRenderers", true);
    allRenderers_ = discoverValues("VPDiscoverRenderers", false);
}

void ConfigEditorWindow::migrateLldvSingleton()
{
    if (!configurationLoaded_ || !document_) return;

    QStringList sections = profileSections(QStringLiteral("lldv"));
    if (sections.isEmpty()) return;

    QString section;
    for (const QString& candidate : sections)
        if (candidate.compare(QStringLiteral("lldv"), Qt::CaseInsensitive) == 0)
        {
            section = candidate;
            break;
        }

    // LLDV metadata is one application-wide input contract, not a selectable
    // rendering profile. Canonicalize the common one-named-section form while
    // retaining multi-section files for backwards-compatible manual recovery.
    if (section.isEmpty() && sections.size() == 1)
    {
        const QString legacy = sections.front();
        if (document_->RenameSection(legacy.toStdString(), "lldv"))
        {
            section = QStringLiteral("lldv");
            dirty_ = true;
            hasPendingMigrations_ = true;
        }
    }
    if (section.isEmpty()) section = sections.front();

    bool hasMetadata = false;
    for (const char* key : { "max_cll", "max_fall",
        "mastering_min_luminance", "mastering_max_luminance" })
    {
        if (!document_->Get(section.toStdString().c_str(), key).empty())
        {
            hasMetadata = true;
            break;
        }
    }

    // Explicit LLDV metadata is an unambiguous request to use VP's established
    // LLDV-aware follow modes. Previously Config displayed and saved these
    // values while ordinary FOLLOW_INPUT silently bypassed all four of them.
    const auto normalizedGeneral = [this](const char* key)
    {
        return QString::fromLocal8Bit(document_->Get("general", key).c_str())
            .trimmed().toLower();
    };
    if (hasMetadata && normalizedGeneral("hdr_colorspace") == QStringLiteral("follow_input") &&
        normalizedGeneral("hdr_luminance") == QStringLiteral("follow_input"))
    {
        document_->SetKnown("general", "hdr_colorspace", "FOLLOW_INPUT_LLDV");
        document_->SetKnown("general", "hdr_luminance", "FOLLOW_INPUT_LLDV");
        dirty_ = true;
        hasPendingMigrations_ = true;
    }
}

void ConfigEditorWindow::migrateSharedRefreshRate()
{
    if (!configurationLoaded_ || !document_) return;

    const QString root = QStringLiteral("vprenderer");
    const QString key = QStringLiteral("switch_refresh_rate");
    QString shared = value(QStringLiteral("general"), key);
    bool migrated = false;
    for (const QString& section : profileSections(root))
    {
        const QString configured = value(section, key);
        if (configured.isEmpty()) continue;
        if (shared.isEmpty()) shared = configured;
        document_->RemoveKnown(section.toStdString(), key.toStdString().c_str());
        migrated = true;
    }
    if (migrated && !shared.isEmpty())
    {
        document_->AddSection("general");
        document_->SetKnown("general", key.toStdString().c_str(),
            shared.toLocal8Bit().constData());
        dirty_ = true;
        hasPendingMigrations_ = true;
    }
    if (document_->SectionSettings(root.toStdString()).empty())
        document_->RemoveSection(root.toStdString());
}

void ConfigEditorWindow::refreshActiveProfileIndicators()
{
    // A combo popup has its own transient native window. Avoid unrelated model
    // notifications while the operator is opening or selecting from it.
    if (hasActiveOwnedPopup()) return;

    const uint32_t expectedProcessId = ownerProcessId_;
    ActiveProfileStatus::Snapshot active;
    const bool available = ActiveProfileStatus::Read(expectedProcessId, active);
    const QString renderer = available ? QString::fromLocal8Bit(active.renderer) : QString();
    const QString color = available ? QString::fromLocal8Bit(active.color) : QString();
    const QString scaling = available ? QString::fromLocal8Bit(active.scaling) : QString();
    const QString output = available ? QString::fromLocal8Bit(active.output) : QString();
    const QString viewport = available ? QString::fromLocal8Bit(active.viewport) : QString();
    const QString zoom = available ? QString::fromLocal8Bit(active.zoom) : QString();
    const QString queue = available ? QString::fromLocal8Bit(active.queue) : QString();
    const QString sourceEotf = available ?
        QString::fromLocal8Bit(active.sourceEotf).trimmed() : QString();
    // The renderer declares captured SDR as BT.1886 before applying the
    // configured SDR-transfer policy. For HDR inputs, expose the actual EOTF
    // so an SDR-only setting does not imply it is currently active.
    liveSourceTransfer_ = sourceEotf.compare(QStringLiteral("SDR"),
        Qt::CaseInsensitive) == 0 ? QStringLiteral("BT.1886") : sourceEotf;
    const bool shaderAvailable = available &&
        ActiveProfileStatus::ShaderSetIsCurrent(active);
    QStringList shaders;
    if (shaderAvailable)
        for (uint32_t index = 0; index < active.shaderCount; ++index)
            shaders.push_back(QString::fromLocal8Bit(active.shaders[index]));
    applyActiveProfileIndicators(available, queue, renderer, color, viewport,
        shaders, shaderAvailable, zoom, scaling, output);
    refreshRendererAutoStatus();
}

void ConfigEditorWindow::setActiveProfileStatusForTesting(const QString& queue,
    const QString& renderer, const QString& color, const QString& viewport,
    const QStringList& shaders, bool shaderAvailable, const QString& zoom,
    const QString& scaling, const QString& output)
{
    applyActiveProfileIndicators(true, queue, renderer, color, viewport, shaders,
        shaderAvailable, zoom, scaling, output);
}

void ConfigEditorWindow::applyActiveProfileIndicators(bool available,
    const QString& queue, const QString& renderer, const QString& color,
    const QString& viewport,
    const QStringList& shaders, bool shaderAvailable, const QString& zoom,
    const QString& scaling, const QString& output)
{
    for (const ProfileListBinding& binding : activeProfileLists_)
    {
        const bool shaderList = binding.sectionPrefix.startsWith(
            QStringLiteral("shader."));
        const QString activeSection = binding.sectionPrefix == QStringLiteral("vprenderer") ? renderer :
            (binding.sectionPrefix == QStringLiteral("vprenderer.color") ? color :
                (binding.sectionPrefix == QStringLiteral("vprenderer.scaling") ? scaling :
                    (binding.sectionPrefix == QStringLiteral("vprenderer.output") ? output :
                        (binding.sectionPrefix == QStringLiteral("vprenderer.viewport") ? viewport :
                            (binding.sectionPrefix == QStringLiteral("vprenderer.zoom") ? zoom :
                                (binding.sectionPrefix == QStringLiteral("queue") ? queue : QString()))))));
        for (int index = 0; binding.list && index < binding.list->count(); ++index)
        {
            QListWidgetItem* item = binding.list->item(index);
            const QString section = item->data(Qt::UserRole).toString();
            const bool isActive = shaderList ?
                (shaderAvailable && std::any_of(shaders.cbegin(), shaders.cend(),
                    [&section](const QString& shader)
                    {
                        return shader.compare(section, Qt::CaseInsensitive) == 0;
                    })) :
                (available && !activeSection.isEmpty() &&
                    section.compare(activeSection, Qt::CaseInsensitive) == 0);
            // QListWidget emits dataChanged even when the assigned value is
            // identical. Do not continually invalidate every profile list on
            // the UI thread for an unchanged shared-memory snapshot.
            if (item->data(ActiveProfileRole).toBool() != isActive)
                item->setData(ActiveProfileRole, isActive);
        }
    }
    refreshRendererAutoStatus();
}

void ConfigEditorWindow::refreshMonitorDiscovery()
{
    if (testMode_ || monitorDiscoveryThread_) return;

    // Configuration activation can coincide with a Windows display-topology
    // rebuild. Keep hardware discovery away from the UI thread; every selector
    // continues serving the cache while this refresh is in flight.
    QPointer<ConfigEditorWindow> guarded(this);
    monitorDiscoveryThread_ = QThread::create([guarded]
    {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const QStringList discovered = discoverValues("VPDiscoverMonitors");
        if (SUCCEEDED(initialized)) CoUninitialize();
        if (!guarded) return;
        QMetaObject::invokeMethod(guarded, [guarded, discovered]
        {
            if (guarded) guarded->applyMonitorDiscovery(discovered);
        }, Qt::QueuedConnection);
    });
    monitorDiscoveryThread_->setParent(this);
    QThread* worker = monitorDiscoveryThread_;
    connect(worker, &QThread::finished, this, [this, worker]
    {
        if (monitorDiscoveryThread_ == worker)
            monitorDiscoveryThread_ = nullptr;
        worker->deleteLater();
    });
    worker->start();
}

void ConfigEditorWindow::applyMonitorDiscovery(const QStringList& discovered)
{
    monitors_ = discovered;
    if (!monitorChoice_) return;

    QString selected = monitorChoice_->currentData().toString();
    if (monitorChoice_->isEditable())
    {
        const int current = monitorChoice_->currentIndex();
        if (current < 0 || monitorChoice_->currentText() !=
            monitorChoice_->itemText(current))
            selected = monitorChoice_->currentText().trimmed();
    }
    if (selected.isEmpty())
        selected = value(QStringLiteral("general"),
            QStringLiteral("fullscreen_monitor_name"));

    const QSignalBlocker blocker(monitorChoice_);
    monitorChoice_->clear();
    monitorChoice_->addItem(QStringLiteral("Default monitor"), QString());
    for (const QString& monitor : monitors_)
        monitorChoice_->addItem(monitor, monitor);

    int index = monitorChoice_->findData(selected, Qt::UserRole,
        Qt::MatchFixedString);
    if (!selected.isEmpty() && index < 0)
    {
        // A projector or secondary display can be offline while Config is
        // open. Keep the configured/typed target intact and editable; active
        // discovery is advisory, not validation.
        monitorChoice_->addItem(selected, selected);
        index = monitorChoice_->count() - 1;
    }
    monitorChoice_->setCurrentIndex(index >= 0 ? index : 0);
    if (monitorChoice_->isEditable())
        monitorChoice_->setEditText(monitorChoice_->itemText(
            monitorChoice_->currentIndex()));
}

QString ConfigEditorWindow::value(const QString& section, const QString& key, const QString& fallback) const
{
    if (!configurationLoaded_ || !document_) return fallback;
    const std::string result = document_->Get(section.toStdString().c_str(), key.toStdString().c_str());
    if (result.empty()) return fallback;
    return QString::fromLocal8Bit(result.c_str());
}

QStringList ConfigEditorWindow::profileSections(const QString& root) const
{
    QStringList result;
    if (!configurationLoaded_ || !document_) return result;
    const QString prefix = root + u'.';
    for (const std::string& raw : document_->SectionNamesWithPrefix(root.toStdString()))
    {
        const QString section = QString::fromLocal8Bit(raw.c_str());
        if (section.compare(root, Qt::CaseInsensitive) == 0 ||
            (section.startsWith(prefix, Qt::CaseInsensitive) && !section.mid(prefix.size()).contains(u'.')))
        {
            const QString suffix = section.mid(prefix.size()).toLower();
            if (root.compare(QStringLiteral("vprenderer"), Qt::CaseInsensitive) == 0 &&
                RendererProfileConfig::IsRendererChildRoot(suffix.toStdString()))
                continue;
            result.push_back(section);
        }
    }
    return result;
}

QString ConfigEditorWindow::inheritedSharedInputLabel(const QString& key) const
{
    // The General control is authoritative for the effective display value.
    // This also covers choices whose stored value is empty, such as Container
    // color space's "Follow input" policy.
    if (QComboBox* general = findChild<QComboBox*>(
        controlName(QStringLiteral("general"), key)))
    {
        const QString selected = general->currentData().toString();
        return selected.isEmpty() ? general->currentText() :
            friendlyChoiceLabel(selected);
    }

    const QString configured = value(QStringLiteral("general"), key);
    if (!configured.isEmpty()) return friendlyChoiceLabel(configured);
    if (key == QStringLiteral("container_colorspace"))
        return QStringLiteral("Follow input");
    if (key == QStringLiteral("video_conversion"))
        return QStringLiteral("Disabled");
    if (key == QStringLiteral("hdr_colorspace") ||
        key == QStringLiteral("hdr_luminance"))
        return QStringLiteral("Follow input");
    return {};
}

void ConfigEditorWindow::refreshInheritedSharedInputChoices(const QString& key)
{
    if (!isSharedInputSetting(key)) return;

    const QString inheritedLabel = inheritedSharedInputLabel(key);
    const QString text = inheritedLabel.isEmpty() ?
        QStringLiteral("Inherited / not set") :
        QStringLiteral("Inherited: %1").arg(inheritedLabel);
    const QString tooltip = inheritedLabel.isEmpty() ?
        QStringLiteral("No General value is currently set. Selecting a value creates a renderer override.") :
        QStringLiteral("Inherited from General: %1. Selecting a value creates a renderer override.")
            .arg(inheritedLabel);
    for (const QString& section : { QStringLiteral("directshow"),
        QStringLiteral("vprenderer.input_processing") })
    {
        QComboBox* combo = findChild<QComboBox*>(controlName(section, key));
        if (!combo) continue;
        const QSignalBlocker blocker(combo);
        combo->setItemText(0, text);
        if (combo->currentData().toString().trimmed().isEmpty())
        {
            combo->setToolTip(tooltip);
            combo->setProperty("inherited", true);
        }
    }
    refreshRendererAutoStatus();
}

void ConfigEditorWindow::refreshRendererAutoStatus()
{
    const auto qualityControl = findChild<QComboBox*>(
        QStringLiteral("config.vprenderer.quality"));
    const QString quality = qualityControl ?
        qualityControl->currentData().toString().trimmed().toLower() :
        QStringLiteral("high");
    const auto qualityValue = [&quality](const QString& high,
        const QString& balanced, const QString& fast)
    {
        if (quality == QStringLiteral("fast")) return fast;
        if (quality == QStringLiteral("balanced")) return balanced;
        return high;
    };
    const auto isAuto = [](QWidget* control)
    {
        if (const auto* combo = qobject_cast<QComboBox*>(control))
            return combo->currentData().toString().trimmed().compare(
                QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
        if (const auto* edit = qobject_cast<QLineEdit*>(control))
            return edit->text().trimmed().compare(QStringLiteral("auto"),
                Qt::CaseInsensitive) == 0;
        return false;
    };
    for (const RendererAutoStatusBinding& binding : rendererAutoStatusBindings_)
    {
        if (!binding.label || !binding.control) continue;
        if (!isAuto(binding.control))
        {
            binding.label->clear();
            binding.label->setVisible(false);
            continue;
        }

        QString text;
        if (binding.key == QStringLiteral("sdr_black_nits"))
        {
            const auto* targetWhite = findChild<QLineEdit*>(
                controlName(binding.sectionPrefix, QStringLiteral("sdr_target_nits")));
            bool valid = false;
            const double whiteNits = targetWhite ?
                targetWhite->text().trimmed().toDouble(&valid) : 0.0;
            // libplacebo's documented SDR contrast constant is 1000:1.
            text = valid && whiteNits > 0.0 ? QString::number(
                whiteNits / 1000.0, 'f', 3) + QStringLiteral(" nits") :
                QStringLiteral("0.203 nits");
        }
        else if (binding.key == QStringLiteral("output_gamma"))
            text = QStringLiteral("sRGB");
        else if (binding.key == QStringLiteral("sdr_adjust_gamma"))
            // This selector controls the conversion policy, rather than
            // declaring the input transfer. Do not echo the live input EOTF
            // here: that belongs only to sdr_input_transfer below.
            text = QStringLiteral("Conditional SDR-to-sRGB policy");
        else if (binding.key == QStringLiteral("sdr_input_transfer"))
            text = liveSourceTransfer_.isEmpty() ?
                QStringLiteral("Source unavailable") : liveSourceTransfer_;
        else if (binding.key == QStringLiteral("tone_mapping"))
            text = QStringLiteral("Spline");
        else if (binding.key == QStringLiteral("gamut_mapping"))
            text = QStringLiteral("Perceptual");
        else if (binding.key == QStringLiteral("peak_detection"))
        {
            text = qualityValue(QStringLiteral("High quality"),
                QStringLiteral("Standard"), QStringLiteral("Off"));
        }
        else if (binding.key == QStringLiteral("contrast_recovery"))
        {
            text = qualityValue(QStringLiteral("0.30"), QStringLiteral("0.00"),
                QStringLiteral("0.00"));
        }
        else if (binding.key == QStringLiteral("upscaler"))
        {
            text = qualityValue(QStringLiteral("EWA Lanczos sharp"),
                QStringLiteral("Lanczos"), QStringLiteral("Built-in sampling"));
        }
        else if (binding.key == QStringLiteral("downscaler"))
        {
            text = qualityValue(QStringLiteral("Hermite"),
                QStringLiteral("Hermite"), QStringLiteral("Built-in sampling"));
        }
        else if (binding.key == QStringLiteral("deband_strength"))
        {
            text = qualityValue(QStringLiteral("Standard"), QStringLiteral("Off"),
                QStringLiteral("Off"));
        }
        else if (binding.key == QStringLiteral("sigmoid"))
        {
            text = qualityValue(QStringLiteral("On"), QStringLiteral("On"),
                QStringLiteral("Off"));
        }
        else if (binding.key == QStringLiteral("dithering"))
        {
            text = qualityValue(QStringLiteral("Blue-noise dithering"),
                QStringLiteral("Blue-noise dithering"), QStringLiteral("Off"));
        }
        else if (binding.key == QStringLiteral("display_bit_depth"))
            text = QStringLiteral("Output format");
        else if (binding.key == QStringLiteral("lut_reference_nits") ||
            binding.key == QStringLiteral("lut_reference_range") ||
            binding.key == QStringLiteral("lut_reference_transfer") ||
            binding.key == QStringLiteral("lut_reference_primaries"))
        {
            text = QStringLiteral("Not constrained");
        }
        else if (binding.sectionPrefix == QStringLiteral("vprenderer.output") &&
            binding.key == QStringLiteral("output_presentation"))
        {
            text = QStringLiteral("Flip");
        }
        else if (binding.sectionPrefix == QStringLiteral("vprenderer.output") &&
            binding.key == QStringLiteral("output_range"))
        {
            text = QStringLiteral("Full RGB");
        }
        else if (binding.sectionPrefix == QStringLiteral("vprenderer.output") &&
            binding.key == QStringLiteral("output_transport_gamma"))
        {
            const auto* range = findChild<QComboBox*>(
                QStringLiteral("config.vprenderer.output.output_range"));
            text = range && range->currentData().toString().compare(
                QStringLiteral("limited"), Qt::CaseInsensitive) == 0 ?
                QStringLiteral("2.4") : QStringLiteral("Not used");
        }
        if (text.isEmpty())
        {
            binding.label->clear();
            binding.label->setVisible(false);
            continue;
        }
        binding.label->setText(QStringLiteral("Auto: %1").arg(text));
        binding.label->setVisible(true);
    }
}

QLineEdit* ConfigEditorWindow::bindTextField(const QString& section, const QString& key, const QString& fallback)
{
    auto* edit = new QLineEdit(value(section, key, fallback));
    edit->setObjectName(controlName(section, key));
    edit->setAccessibleName(accessibleSettingName(key));
    connect(edit, &QLineEdit::textChanged, this, [this, section, key](const QString& text)
    {
        if (!document_) return;
        const std::string sectionText = section.toStdString();
        const std::string keyText = key.toStdString();
        if (text.trimmed().isEmpty()) document_->RemoveKnown(sectionText, keyText.c_str());
        else document_->SetKnown(sectionText, keyText.c_str(), text.toLocal8Bit().constData());
        markDirty();
    });
    return edit;
}

QComboBox* ConfigEditorWindow::bindChoiceField(const QString& section, const QString& key,
    const QStringList& values, const QStringList& labels, bool editable)
{
    auto* combo = new QComboBox;
    combo->setObjectName(controlName(section, key));
    combo->setAccessibleName(accessibleSettingName(key));
    combo->setEditable(editable);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    combo->view()->setStyleSheet(QStringLiteral(R"(
        QAbstractItemView {
            color: #e7f0f8;
            background: #111e2b;
            border: 1px solid #3a5871;
            outline: 0;
            padding: 6px;
            selection-background-color: #173c59;
            selection-color: #f4fbff;
        }
        QAbstractItemView::item {
            min-height: 28px;
            padding: 5px 10px;
            border-radius: 4px;
        }
        QAbstractItemView::item:hover { background: #173047; }
        QAbstractItemView::item:selected { background: #173c59; color: #f4fbff; }
    )"));
    for (int index = 0; index < values.size(); ++index)
    {
        QString display = index < labels.size() ? labels[index] : friendlyChoiceLabel(values[index]);
        combo->addItem(display, values[index]);
    }

    QString fallback;
    const bool backendInputSetting =
        (section.compare(QStringLiteral("directshow"), Qt::CaseInsensitive) == 0 ||
         section.compare(QStringLiteral("vprenderer.input_processing"), Qt::CaseInsensitive) == 0) &&
        isSharedInputSetting(key);
    if (backendInputSetting)
    {
        // A renderer-owned omission is an inherited selector value. DirectShow
        // keeps [general] as its source; VP Renderer retains the historical
        // DirectShow location as a final fallback for unsaved legacy files.
        fallback = value(QStringLiteral("general"), key);
        if (fallback.isEmpty() && section.compare(QStringLiteral("vprenderer.input_processing"),
            Qt::CaseInsensitive) == 0)
            fallback = value(QStringLiteral("directshow"), key);
    }
    else if (section.compare(QStringLiteral("directshow"), Qt::CaseInsensitive) == 0)
        fallback = value(QStringLiteral("general"), key);
    else if (section.compare(QStringLiteral("vprenderer.input_processing"), Qt::CaseInsensitive) == 0)
    {
        fallback = value(QStringLiteral("general"), key);
        if (fallback.isEmpty()) fallback = value(QStringLiteral("directshow"), key);
    }
    else if (section.compare(QStringLiteral("general"), Qt::CaseInsensitive) == 0 &&
        isSharedInputSetting(key))
        fallback = value(QStringLiteral("directshow"), key);
    const QString raw = value(section, key);
    const bool inherited = backendInputSetting && raw.isEmpty();
    const QString configured = inherited ? fallback : value(section, key, fallback);
    if (inherited)
    {
        const QString inheritedLabel = inheritedSharedInputLabel(key);
        combo->setItemText(0, inheritedLabel.isEmpty() ?
            QStringLiteral("Inherited / not set") :
            QStringLiteral("Inherited: %1").arg(inheritedLabel));
        combo->setProperty("inherited", true);
        combo->setToolTip(inheritedLabel.isEmpty() ?
            QStringLiteral("No General value is currently set. Selecting a value creates a renderer override.") :
            QStringLiteral("Inherited from General: %1. Selecting a value creates a renderer override.")
                .arg(inheritedLabel));
        combo->setCurrentIndex(0);
    }
    else if (!configured.isEmpty())
    {
        int index = combo->findData(configured, Qt::UserRole, Qt::MatchFixedString);
        if (index < 0)
            for (int candidate = 0; candidate < combo->count(); ++candidate)
                if (combo->itemData(candidate).toString().compare(
                    configured, Qt::CaseInsensitive) == 0)
                {
                    index = candidate;
                    break;
                }
        bool customValue = false;
        if (index < 0)
        {
            const QString display = friendlyChoiceLabel(configured);
            combo->addItem(display, configured);
            index = combo->count() - 1;
            customValue = true;
        }
        combo->setCurrentIndex(index);
        if (editable && customValue) combo->setEditText(friendlyChoiceLabel(configured));
    }
    else if (combo->count() > 0)
        combo->setCurrentIndex(0);

    // Keep the inherit command meaningful even while a renderer override is
    // selected: users can see the value they will get before choosing it.
    if (backendInputSetting)
    {
        const QString inheritedLabel = inheritedSharedInputLabel(key);
        combo->setItemText(0, inheritedLabel.isEmpty() ?
            QStringLiteral("Inherited / not set") :
            QStringLiteral("Inherited: %1").arg(inheritedLabel));
    }

    auto save = [this, section, key](const QString& selected)
    {
        if (!document_) return;
        QString normalized = selected;
        if (section.compare(QStringLiteral("general"), Qt::CaseInsensitive) == 0 &&
            key.compare(QStringLiteral("renderer"), Qt::CaseInsensitive) == 0 &&
            normalized.compare(QStringLiteral("VideoProcessor Renderer (Alpha)"), Qt::CaseInsensitive) == 0)
            normalized = QStringLiteral("VP Renderer");
        const std::string sectionText = section.toStdString();
        const std::string keyText = key.toStdString();
        if (section.compare(QStringLiteral("directshow"), Qt::CaseInsensitive) == 0 &&
            !isSharedInputSetting(key))
            document_->RemoveKnown("general", keyText.c_str());
        if (normalized.trimmed().isEmpty()) document_->RemoveKnown(sectionText, keyText.c_str());
        else if (!document_->SetKnown(sectionText, keyText.c_str(),
            normalized.toLocal8Bit().constData()))
        {
            document_->AddSection(sectionText);
            document_->SetKnown(sectionText, keyText.c_str(),
                normalized.toLocal8Bit().constData());
        }
        markDirty();
        if (section.compare(QStringLiteral("general"), Qt::CaseInsensitive) == 0)
            refreshInheritedSharedInputChoices(key);
    };
    if (editable)
    {
        connect(combo, qOverload<int>(&QComboBox::activated), this, [combo, save](int index)
        {
            if (index >= 0) save(combo->itemData(index).toString());
        });
        connect(combo->lineEdit(), &QLineEdit::textChanged, this,
            [combo, save](const QString& text)
        {
            const int index = combo->findText(text, Qt::MatchFixedString);
            save(index >= 0 ? combo->itemData(index).toString() : text);
        });
    }
    else
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [combo, save, backendInputSetting](int index)
        {
            if (index < 0) return;
            const bool inherited = backendInputSetting &&
                combo->itemData(index).toString().trimmed().isEmpty();
            combo->setProperty("inherited", inherited);
            combo->style()->unpolish(combo);
            combo->style()->polish(combo);
            save(combo->itemData(index).toString());
        });
    return combo;
}

QCheckBox* ConfigEditorWindow::bindCheckField(const QString& label, const QString& section,
    const QString& key, bool defaultValue)
{
    auto* check = configuredCheck(label, value(section, key), defaultValue);
    check->setObjectName(controlName(section, key));
    check->setEnabled(true);
    connect(check, &QCheckBox::toggled, this, [this, section, key](bool checked)
    {
        if (!document_) return;
        document_->SetKnown(section.toStdString(), key.toStdString().c_str(), checked ? "true" : "false");
        markDirty();
    });
    return check;
}

void ConfigEditorWindow::markDirty()
{
    if (QObject* edited = sender(); edited &&
        edited->objectName().startsWith(QStringLiteral("config.")))
        editOrder_[edited->objectName()] = ++editSerial_;
    dirty_ = document_ &&
        captureDocumentSnapshot(*document_) != savedSnapshot_;
    updateEffectSummary();
    updateValidationState();
}

void ConfigEditorWindow::prepareRendererPopup()
{
    if (!rendererChoice_) return;
    auto* list = qobject_cast<QListView*>(rendererChoice_->view());
    if (!list) return;

    // QComboBox otherwise defers polishing and item geometry until the first
    // popup open. Renderer discovery is already cached; prepare the cached
    // view now so the user's first click has no one-time layout penalty.
    list->setUniformItemSizes(true);
    rendererChoice_->ensurePolished();
    list->ensurePolished();
    list->viewport()->ensurePolished();
    list->doItemsLayout();
    if (rendererChoice_->count() > 0)
    {
        list->sizeHintForRow(0);
        list->sizeHintForColumn(rendererChoice_->modelColumn());
        list->visualRect(rendererChoice_->model()->index(
            qMax(0, rendererChoice_->currentIndex()),
            rendererChoice_->modelColumn(), rendererChoice_->rootModelIndex()));
    }
    list->window()->ensurePolished();
}

void ConfigEditorWindow::updateEffectSummary()
{
    if (!effectSummary_) return;
    if (!configurationLoaded_ || !document_)
    {
        effectSummary_->setText(QStringLiteral("Configuration unavailable"));
        return;
    }
    if (!dirty_)
    {
        effectSummary_->setText(QStringLiteral("No pending changes"));
        return;
    }
    const DocumentSnapshot currentSnapshot = captureDocumentSnapshot(*document_);
    const std::vector<ConfigurationApplyPolicy::Change> changed = changedDocumentValues(
        savedSnapshot_, currentSnapshot);
    const auto action = ConfigurationApplyPolicy::ClassifyChanges(changed,
        snapshotUsesDirectShowRenderer(savedSnapshot_));
    QStringList sections;
    for (const ConfigurationApplyPolicy::Change& change : changed)
    {
        const QString category = effectCategory(change.section);
        if (!sections.contains(category)) sections.push_back(category);
    }
    effectSummary_->setText(QString::fromLatin1(
        ConfigurationApplyPolicy::ActionLabel(action)) +
        QStringLiteral(": ") + sections.join(QStringLiteral(", ")));
}

bool ConfigEditorWindow::validateCandidate(std::wstring& error,
    bool allowActionDrafts) const
{
    QStringList fields;
    const QStringList errors = validationErrors(fields, allowActionDrafts);
    if (errors.isEmpty()) return true;
    error = errors.front().toStdWString();
    return false;
}

QStringList ConfigEditorWindow::validationErrors(QStringList& fields,
    bool allowActionDrafts, bool includeCoreValidation) const
{
    QStringList errors;
    fields.clear();
    if (!configurationLoaded_ || !document_)
    {
        errors.push_back(QStringLiteral("The configuration was not loaded."));
        fields.push_back(QString());
        return errors;
    }
    ConfigEditorCore::ConfigDocument validationDocument = *document_;
    std::wstring coreError;
    bool coreValid = !includeCoreValidation;
    if (includeCoreValidation && allowActionDrafts)
    {
        const int maximumDrafts = static_cast<int>(
            validationDocument.SectionNamesWithPrefix("actions").size());
        for (int attempt = 0; attempt <= maximumDrafts; ++attempt)
        {
            if (ConfigEditorCore::ValidateCandidate(validationDocument, coreError))
            {
                coreValid = true;
                break;
            }
            const QString message = QString::fromStdWString(coreError);
            const QRegularExpression actionError(
                QStringLiteral("^\\[(actions\\.[^\\]]+)\\] (?!enabled(?: |$))"));
            const QRegularExpressionMatch match = actionError.match(message);
            if (!match.hasMatch()) break;
            const std::string section = match.captured(1).toStdString();
            if (!configuredBooleanValue(QString::fromLocal8Bit(
                validationDocument.Get(section.c_str(), "enabled").c_str()), true))
                break;
            validationDocument.SetKnown(section, "enabled", "false");
        }
        if (!coreValid)
            coreValid = ConfigEditorCore::ValidateCandidate(
                validationDocument, coreError);
    }
    else if (includeCoreValidation)
        coreValid = ConfigEditorCore::ValidateCandidate(
            validationDocument, coreError);

    if (includeCoreValidation && !coreValid)
    {
        const QString message = QString::fromStdWString(coreError);
        errors.push_back(message);
        const QRegularExpression sectionError(
            QStringLiteral("^\\[([^\\]]+)\\]"));
        const QRegularExpressionMatch sectionMatch = sectionError.match(message);
        const QRegularExpression quotedKey(
            QStringLiteral("\\bkey ['\"]([A-Za-z0-9_.-]+)['\"]"));
        const QRegularExpressionMatch quotedMatch = quotedKey.match(message);
        const QRegularExpression plainKey(
            QStringLiteral("^\\[[^\\]]+\\]\\s+([A-Za-z0-9_.-]+)"));
        const QRegularExpressionMatch plainMatch = plainKey.match(message);
        const QString key = quotedMatch.hasMatch() ? quotedMatch.captured(1) :
            (plainMatch.hasMatch() ? plainMatch.captured(1) : QString());
        fields.push_back(sectionMatch.hasMatch() && !key.isEmpty() ?
            controlName(sectionMatch.captured(1), key) : QString());
    }

    auto discoveredContains = [](const QStringList& discovered,
        const QString& candidate)
    {
        for (const QString& value : discovered)
            if (value.trimmed().compare(candidate.trimmed(),
                Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };

    QString renderer = value(QStringLiteral("general"),
        QStringLiteral("renderer")).trimmed();
    if (renderer.compare(QStringLiteral("VideoProcessor Renderer (Alpha)"),
        Qt::CaseInsensitive) == 0)
        renderer = QStringLiteral("VP Renderer");
    // Discovery is advisory for validation. A transient or unavailable helper
    // must not make every unrelated profile edit unsavable. When it returns a
    // real list we remain strict; otherwise accept VP's built-in choices.
    const QStringList acceptedRenderers = allRenderers_.isEmpty() ?
        QStringList{ QStringLiteral("VP Renderer"),
            QStringLiteral("DirectShow - madVR") } : allRenderers_;
	if (!renderer.isEmpty() &&
		!discoveredContains(acceptedRenderers, renderer))
    {
        errors.push_back(QStringLiteral("[general] renderer '%1' was not discovered. Choose an available renderer.")
            .arg(renderer.isEmpty() ? QStringLiteral("(empty)") : renderer));
        fields.push_back(controlName(QStringLiteral("general"),
            QStringLiteral("renderer")));
    }

    const QString captureDevice = value(QStringLiteral("general"),
        QStringLiteral("capture_device")).trimmed();
	if (!captureDevice.isEmpty() && !captureDevices_.isEmpty() &&
		!discoveredContains(captureDevices_, captureDevice))
    {
        errors.push_back(QStringLiteral("[general] capture_device '%1' was not discovered. Choose an available capture device.")
            .arg(captureDevice.isEmpty() ? QStringLiteral("(empty)") : captureDevice));
        fields.push_back(controlName(QStringLiteral("general"),
            QStringLiteral("capture_device")));
    }
    return errors;
}

QString ConfigEditorWindow::displayWarning() const
{
    const QString display = value(QStringLiteral("general"),
        QStringLiteral("fullscreen_monitor_name")).trimmed();
    if (display.isEmpty()) return {};
    for (const QString& discovered : monitors_)
        if (discovered.trimmed().compare(display, Qt::CaseInsensitive) == 0)
            return {};
    return QStringLiteral("Warning: Display '%1' is not currently discoverable.")
        .arg(display);
}

bool ConfigEditorWindow::updateValidationState()
{
    QStringList fields;
    // Keep ordinary editing entirely in memory. Full schema/shader validation
    // serializes a candidate beside the configuration and is intentionally
    // reserved for Apply/OK, where safe persistence validates again.
    const QStringList errors = validationErrors(fields, true, false);
    const bool valid = errors.isEmpty();
    validationValid_ = valid;
    if (saveButton_)
        saveButton_->setEnabled(configurationLoaded_ && valid);
    if (applyButton_)
        applyButton_->setEnabled(configurationLoaded_ && dirty_ && valid);
    if (!valid)
    {
        int latest = 0;
        quint64 latestOrder = 0;
        for (int index = 0; index < fields.size(); ++index)
        {
            quint64 order = editOrder_.value(fields[index], 0);
            if (order == 0 && !fields[index].isEmpty())
            {
                const QString keySuffix = fields[index].mid(
                    fields[index].lastIndexOf(u'.'));
                for (auto edited = editOrder_.cbegin(); edited != editOrder_.cend(); ++edited)
                    if (edited.key().endsWith(keySuffix, Qt::CaseInsensitive))
                        order = std::max(order, edited.value());
            }
            if (order >= latestOrder)
            {
                latest = index;
                latestOrder = order;
            }
        }
        QString message = errors[latest];
        const int additional = errors.size() - 1;
        if (additional > 0)
            message += QStringLiteral(" And %1 additional error%2.")
                .arg(additional).arg(additional == 1 ? QString() : QStringLiteral("s"));
        setStatus(message, true);
    }
    else if (const QString warning = displayWarning(); !warning.isEmpty())
        setWarningStatus(warning);
    else if (dirty_)
        setStatus(QStringLiteral("Unsaved changes. OK or Apply will validate again before saving."));
    else
        setStatus(QStringLiteral("Configuration loaded."));
    return valid;
}

void ConfigEditorWindow::applyNativeOwner()
{
	ownerApplied_ = false;
}

void ConfigEditorWindow::clearNativeOwner()
{
    ownerApplied_ = false;
}

bool ConfigEditorWindow::nativeOwnerIsValid() const
{
    const HWND owner = reinterpret_cast<HWND>(ownerHandle_);
    if (!owner || !IsWindow(owner) || ownerProcessId_ == 0) return false;
    DWORD actualProcessId = 0;
    GetWindowThreadProcessId(owner, &actualProcessId);
    return actualProcessId == ownerProcessId_;
}

void ConfigEditorWindow::publishNativeAssociation()
{
    const HWND owner = reinterpret_cast<HWND>(ownerHandle_);
    const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
    if (!nativeOwnerIsValid() || !editor || !IsWindow(editor)) return;
    const quintptr handle = reinterpret_cast<quintptr>(editor);
    if (publishedWindowHandle_ == handle) return;

    static const UINT associationMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Association.v1");
    // Association is an advertisement, not a request. Posting prevents a
    // reverse SendMessage cycle when VP is itself synchronously activating us.
    if (associationMessage && PostMessageW(owner, associationMessage,
        GetCurrentProcessId(), reinterpret_cast<LPARAM>(editor)))
        publishedWindowHandle_ = handle;
}

void ConfigEditorWindow::positionForReveal()
{
    const HWND target = reinterpret_cast<HWND>(presentationTargetHandle_);
    if (!target || !IsWindow(target) ||
        presentationTargetProcessId_ == 0 ||
        presentationTargetProcessId_ != ownerProcessId_)
        return;
    DWORD actualProcessId = 0;
    GetWindowThreadProcessId(target, &actualProcessId);
    if (actualProcessId != presentationTargetProcessId_) return;
    const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) return;
    const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
    RECT current{};
    if (!editor || !IsWindow(editor) || !GetWindowRect(editor, &current)) return;
    const QRect frame(current.left, current.top,
        current.right - current.left, current.bottom - current.top);
    const RECT& work = monitorInfo.rcWork;
    const QRect workArea(work.left, work.top,
        work.right - work.left, work.bottom - work.top);
    const QRect placed = ConfigEditorPlacement::ClampFrameToWorkArea(
        frame, workArea);
    SetWindowPos(editor, nullptr, placed.left(), placed.top(), 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void ConfigEditorWindow::applyScopedTopmost()
{
    if (!pendingTopmostReassert_ || !scopedTopmostEligible_ || !isVisible())
        return;
	if (hasActiveOwnedPopup())
	{
		if (!topmostReassertDeferredForPopup_)
		{
			topmostReassertDeferredForPopup_ = true;
			qInfo("Configuration editor z-order repair deferred: active Qt popup");
		}
		// A native topmost request can reorder the popup out from under its menu
		// or combo list. Keep the repair pending and retry from the Qt event loop
		// after the popup has had a chance to close.
		QTimer::singleShot(50, this, [this] { applyScopedTopmost(); });
		return;
	}
    // Config is an operator modal surface.  While it is visible, retain its
    // topmost placement even after another application receives foreground.
    // This deliberately matches madVR's configuration behavior and keeps the
    // editor above VP's exclusive fullscreen host.
    const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
    if (!editor || !IsWindow(editor)) return;
    scopedTopmost_ = SetWindowPos(editor, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE;
    pendingTopmostReassert_ = false;
	if (topmostReassertDeferredForPopup_)
	{
		topmostReassertDeferredForPopup_ = false;
		qInfo("Configuration editor z-order repair applied after Qt popup closed");
	}
}

void ConfigEditorWindow::removeScopedTopmost()
{
    QWindow* editorWindow = windowHandle();
    const HWND editor = editorWindow ?
        reinterpret_cast<HWND>(editorWindow->winId()) : nullptr;
    if (editor && IsWindow(editor) &&
        (scopedTopmost_ ||
            (GetWindowLongPtrW(editor, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0))
    {
        SetWindowPos(editor, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    scopedTopmost_ = false;
}

bool ConfigEditorWindow::hasActiveOwnedPopup() const
{
    QWidget* popup = QApplication::activePopupWidget();
    if (!popup || !popup->isVisible()) return false;
    for (QWidget* parent = popup->parentWidget(); parent;
        parent = parent->parentWidget())
        if (parent == this) return true;
    QWindow* transient = popup->windowHandle() ?
        popup->windowHandle()->transientParent() : nullptr;
    return transient && transient == windowHandle();
}

bool ConfigEditorWindow::notifyVideoProcessor()
{
    // Test-mode editors represent the normal VP-absent standalone case. This
    // also prevents a UI test from disturbing a developer's running VP.
    if (testMode_) return false;
    SetLastError(ERROR_SUCCESS);
    HANDLE changedEvent = CreateEventW(nullptr, FALSE, FALSE,
        ConfigurationLiveApply::ChangedEventName);
    if (!changedEvent) return false;
    const bool videoProcessorWasListening = GetLastError() == ERROR_ALREADY_EXISTS;
    const bool signaled = videoProcessorWasListening && SetEvent(changedEvent) != FALSE;
    CloseHandle(changedEvent);
    return signaled;
}

void ConfigEditorWindow::applyChanges()
{
    if (!dirty_) return;
    saveChanges();
}

void ConfigEditorWindow::setRendererDiscoveryForTesting(
    const QStringList& allRenderers, const QStringList& filteredRenderers)
{
    allRenderers_ = allRenderers;
    filteredRenderers_ = filteredRenderers;
    rebuildConfigurationShell();
}

void ConfigEditorWindow::rebuildConfigurationShell()
{
    // Recreate the editor after a full document reload (for example Cancel).
    // Ordinary renderer-visibility changes are handled in place and never
    // enter this comparatively expensive path.
    const int currentPage = pages_ ? pages_->currentIndex() : 0;
	activeProfileLists_.clear();
	rendererAutoStatusBindings_.clear();
	monitorChoice_ = nullptr;
	rendererChoice_ = nullptr;
	actionRendererTarget_ = nullptr;
	rendererShortcutForm_ = nullptr;
    QWidget* replacement = createShell();
    QWidget* previous = takeCentralWidget();
    setCentralWidget(replacement);
    selectPage(currentPage);
    if (previous) previous->deleteLater();
}

void ConfigEditorWindow::applyRendererVisibilityFilter(bool hideLegacyRenderers)
{
    const auto containsRenderer = [](const QStringList& renderers,
        const QString& candidate)
    {
        return std::any_of(renderers.cbegin(), renderers.cend(),
            [&candidate](const QString& renderer)
            {
                return renderer.compare(candidate, Qt::CaseInsensitive) == 0;
            });
    };
    const auto isLegacy = [this, &containsRenderer](const QString& renderer)
    {
        return containsRenderer(allRenderers_, renderer) &&
            !containsRenderer(filteredRenderers_, renderer);
    };
    const auto filterCombo = [hideLegacyRenderers, &isLegacy](QComboBox* combo)
    {
        auto* view = combo ? qobject_cast<QListView*>(combo->view()) : nullptr;
        if (!view) return;
        for (int index = 0; index < combo->count(); ++index)
        {
            const QString renderer = combo->itemData(index,
                kRendererNameRole).toString();
            view->setRowHidden(index,
                hideLegacyRenderers && !renderer.isEmpty() &&
                isLegacy(renderer));
        }
    };

    filterCombo(rendererChoice_);
    filterCombo(actionRendererTarget_);
    if (!rendererShortcutForm_) return;
    for (int row = 0; row < rendererShortcutForm_->rowCount(); ++row)
    {
        QLayoutItem* fieldItem = rendererShortcutForm_->itemAt(row,
            QFormLayout::FieldRole);
        QWidget* field = fieldItem ? fieldItem->widget() : nullptr;
        const QString renderer = field ?
            field->property("rendererName").toString() : QString();
        rendererShortcutForm_->setRowVisible(row,
            !hideLegacyRenderers || renderer.isEmpty() ||
            !isLegacy(renderer));
    }
}

bool ConfigEditorWindow::saveChanges()
{
    if (!configurationLoaded_ || !document_) return false;

    // VP-0123 migration. Before VP Renderer had its own input policy, a
    // DirectShow-only field acted as a shared setting. If it is the only
    // spelling for a key, move it to [general] so both renderer pages show
    // Inherit and preserve the exact former behavior. Mixed files retain their
    // explicit backend override; a configured [general] value always remains
    // the shared default.
    for (const char* inputKey : { "video_conversion", "container_colorspace",
        "hdr_colorspace", "hdr_luminance" })
    {
        const QString general = value(QStringLiteral("general"),
            QString::fromLatin1(inputKey)).trimmed();
        const QString directShow = value(QStringLiteral("directshow"),
            QString::fromLatin1(inputKey)).trimmed();
        const QString vpRenderer = value(QStringLiteral("vprenderer.input_processing"),
            QString::fromLatin1(inputKey)).trimmed();
        if (general.isEmpty() && !directShow.isEmpty() && vpRenderer.isEmpty())
        {
            document_->SetKnown("general", inputKey,
                directShow.toLocal8Bit().constData());
            document_->RemoveKnown("directshow", inputKey);
        }
    }

    const DocumentSnapshot currentSnapshot = captureDocumentSnapshot(*document_);
    const std::vector<ConfigurationApplyPolicy::Change> changed = changedDocumentValues(
        savedSnapshot_, currentSnapshot);
    const auto action = ConfigurationApplyPolicy::ClassifyChanges(changed,
        snapshotUsesDirectShowRenderer(savedSnapshot_));
    // Persist shortcut spelling in the same canonical form used by the
    // accelerator parser. Case is not a modifier: L and l are both L, while
    // Shift+L is the distinct shifted chord.
    const QStringList shortcutRoots = {
        QStringLiteral("shortcuts"), QStringLiteral("queue"),
        QStringLiteral("lldv"), QStringLiteral("vprenderer"),
        QStringLiteral("shader"), QStringLiteral("shaders") };
    for (const QString& root : shortcutRoots)
    {
        for (const std::string& section : document_->SectionNamesWithPrefix(
            root.toStdString()))
        {
            for (const auto& setting : document_->SectionSettings(section))
            {
                const bool isShortcut =
                    (root == QStringLiteral("shortcuts") &&
                        ConfigFile::NormalizeName(setting.first) !=
                            "foreground_only") ||
                    ConfigFile::NormalizeName(setting.first) == "shortcut";
                if (!isShortcut || ConfigFile::Trim(setting.second).empty()) continue;
                std::string canonical;
                if (RendererProfileConfig::CanonicalizeKeyChord(setting.second, canonical))
                    document_->SetKnown(section, setting.first.c_str(), canonical);
            }
        }
    }

    // Saving is also the boundary at which an unfinished enabled action becomes
    // a draft.  Do not make users discover every required action field merely
    // to persist work in progress; disable only the action rejected by the
    // runtime validator, then validate the remaining configuration normally.
    QStringList draftedActions;
    const int maximumDrafts = static_cast<int>(document_->SectionNamesWithPrefix("actions").size());
    for (int attempt = 0; attempt <= maximumDrafts; ++attempt)
    {
        std::wstring validationError;
        if (ConfigEditorCore::ValidateCandidate(*document_, validationError)) break;

        const QString message = QString::fromStdWString(validationError);
        const QRegularExpression actionError(
            QStringLiteral("^\\[(actions\\.[^\\]]+)\\] (?!enabled(?: |$))"));
        const QRegularExpressionMatch match = actionError.match(message);
        if (!match.hasMatch()) break;

        const QString section = match.captured(1);
        if (!configuredBooleanValue(value(section, QStringLiteral("enabled")), true)) break;
        document_->SetKnown(section.toStdString(), "enabled", "false");
        draftedActions.push_back(displayName(section, QStringLiteral("actions")));
    }

	ConfigEditorCore::SaveResult result;
	std::wstring error;
	const bool creatingConfiguration = !document_->existedAtLoad;
    if (!validateCandidate(error))
    {
        setStatus(QString::fromStdWString(error), true);
        if (saveButton_) saveButton_->setEnabled(false);
        if (applyButton_) applyButton_->setEnabled(false);
        return false;
    }
    // Config is the interactive authority for this file. Save the user's
    // validated editor state instead of refusing a stale-file conflict.
    if (!ConfigEditorCore::SaveSafely(*document_, result, error, true))
    {
        setStatus(QString::fromStdWString(error), true);
        if (!testMode_) openValidationError(this, QString::fromStdWString(error));
        return false;
    }
    dirty_ = false;
    saveButton_->setEnabled(true);
    if (applyButton_) applyButton_->setEnabled(false);
    savedSnapshot_ = captureDocumentSnapshot(*document_);
    updateEffectSummary();

    // The editor is a separate process. Signal the running VP instance only
    // after the atomic safe-save has committed the complete validated file.
    // An auto-reset event also makes saves harmless when VP is not running.
    const bool notified = notifyVideoProcessor();
    if (!draftedActions.isEmpty())
    {
        if (auto* actions = findChild<QListWidget*>(QStringLiteral("config.actions.items"));
            actions && actions->currentItem() &&
            draftedActions.contains(actions->currentItem()->text(), Qt::CaseInsensitive))
        {
            if (auto* enabled = findChild<QCheckBox*>(QStringLiteral("config.actions.enabled")))
            {
                const QSignalBlocker blocker(enabled);
                enabled->setChecked(false);
            }
        }
        setStatus(QStringLiteral("Changes saved%1. Incomplete action%2 %3 saved as disabled draft%2.")
            .arg(notified ? QStringLiteral(" and sent to VideoProcessor") : QStringLiteral("; VideoProcessor could not be notified"))
            .arg(draftedActions.size() == 1 ? QString() : QStringLiteral("s"),
                draftedActions.join(QStringLiteral(", "))));
    }
	else if (creatingConfiguration)
    {
        const QString effect = QString::fromLatin1(
            ConfigurationApplyPolicy::ActionLabel(action));
        setStatus(notified ?
            QStringLiteral("Configuration created safely at %1. %2 was requested.")
                .arg(configPath_, effect) :
            QStringLiteral("Configuration created safely at %1. It will take effect when VideoProcessor next starts.")
                .arg(configPath_), false);
    }
    else
    {
        const QString effect = QString::fromLatin1(
            ConfigurationApplyPolicy::ActionLabel(action));
        setStatus(notified ?
            QStringLiteral("Changes saved safely. %1 was requested.").arg(effect) :
            QStringLiteral("Changes saved safely. Takes effect when VideoProcessor next starts."), false);
    }
    return true;
}

QWidget* ConfigEditorWindow::createShell()
{
    pages_ = nullptr;
    navigation_ = nullptr;
    status_ = nullptr;
    effectSummary_ = nullptr;
    monitorChoice_ = nullptr;
    rendererChoice_ = nullptr;
    rendererShortcutForm_ = nullptr;
    actionRendererTarget_ = nullptr;
    applyButton_ = nullptr;
    saveButton_ = nullptr;
    configurationHost_ = nullptr;
    shaderCacheStatus_ = nullptr;
    activeProfileLists_.clear();
    rendererAutoStatusBindings_.clear();
    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("brandHeader"));
    header->setMinimumHeight(56);
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 6, 16, 6);
    auto* icon = new QLabel;
    icon->setPixmap(windowIcon().pixmap(32, 32));
    icon->setAccessibleName(QStringLiteral("VideoProcessor"));
    headerLayout->addWidget(icon);
    auto* brand = new QWidget;
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(5, 0, 0, 0);
    brandLayout->setSpacing(6);
    brand->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    auto* title = new QLabel(QStringLiteral("VideoProcessor Configuration"));
    title->setObjectName(QStringLiteral("brandTitle"));
    // Profile detail pages can legitimately request more horizontal space than
    // the initial window. Keep the product identity intact rather than
    // allowing the header labels to be the widgets that get compressed away.
    title->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    brandLayout->addWidget(title);
    headerLayout->addWidget(brand);
    headerLayout->addStretch();
    rootLayout->addWidget(header);

    auto* center = new QWidget;
    auto* centerLayout = new QHBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    navigation_ = new QWidget;
    navigation_->setObjectName(QStringLiteral("sidebar"));
    navigation_->setMinimumWidth(172);
    navigation_->setMaximumWidth(200);
    navigation_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    navigation_->setAccessibleName(QStringLiteral("Settings navigation"));
    auto* navLayout = new QVBoxLayout(navigation_);
    navLayout->setContentsMargins(12, 14, 0, 12);
    navLayout->setSpacing(2);
    auto* caption = new QLabel(QStringLiteral("Settings"));
    caption->setObjectName(QStringLiteral("sidebarCaption"));
    navLayout->addWidget(caption);

    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("settingsPages"));
    pages_->addWidget(createStartupPage());
    pages_->addWidget(createQueuePage());
    pages_->addWidget(createRendererPage());
    pages_->addWidget(createDirectShowPage());
    pages_->addWidget(createViewportPage());
    pages_->addWidget(createLldvPage());
    pages_->addWidget(createShortcutsPage());
    pages_->addWidget(createActionsPage());
    pages_->addWidget(createStandardShadersPage());
    pages_->addWidget(createNlsShadersPage());
    pages_->addWidget(createLogsPage());
    pages_->addWidget(createInputProcessingPage(
        QStringLiteral("Input processing"),
        QStringLiteral("Override the General input policy for VP Renderer, or inherit it."),
        QStringLiteral("vprenderer.input_processing")));
    pages_->addWidget(createInputProcessingPage(
        QStringLiteral("Input processing"),
        QStringLiteral("Override the General input policy for DirectShow, or inherit it."),
        QStringLiteral("directshow")));
    pages_->addWidget(createOutputPage());
    pages_->addWidget(createShadersSetupPage());
    pages_->addWidget(createShortcutsSetupPage());
    pages_->addWidget(createColorConfigPage());
	pages_->addWidget(createScalingPage());
	pages_->addWidget(createZoomPage());

    auto* navGroup = new QButtonGroup(root);
    navGroup->setExclusive(true);
    const auto addLeaf = [this, navLayout, navGroup](const QString& title, int page)
    {
        QPushButton* button = addNavigationButton(title, page);
        navGroup->addButton(button, page);
        navLayout->addWidget(button);
        if (page == 0) button->setChecked(true);
        return button;
    };
    addLeaf(QStringLiteral("General"), 0);
    addLeaf(QStringLiteral("Queue"), 1);
    addLeaf(QStringLiteral("LLDV"), 5);
    QPushButton* shadersNavigation = addLeaf(QStringLiteral("Shaders"), 14);
    addLeaf(QStringLiteral("Actions"), 7);
    QPushButton* shortcutsNavigation = addLeaf(QStringLiteral("Shortcuts"), 15);
    addLeaf(QStringLiteral("Logs"), 10);
    navLayout->addSpacing(8);
    QPushButton* vpNavigation = addLeaf(QStringLiteral("VP Renderer"), 2);
    QPushButton* directShowNavigation = addLeaf(QStringLiteral("DirectShow"), 3);
    navLayout->addStretch();

    auto* pageHost = new QWidget;
    pageHost->setObjectName(QStringLiteral("sectionPageHost"));
    auto* pageHostLayout = new QVBoxLayout(pageHost);
    pageHostLayout->setContentsMargins(0, 0, 0, 0);
    pageHostLayout->setSpacing(0);
    auto* sectionTabs = new QTabBar;
    sectionTabs->setObjectName(QStringLiteral("settingsSectionTabs"));
    sectionTabs->setAccessibleName(QStringLiteral("Settings section pages"));
    sectionTabs->setProperty("sectionTabs", true);
    sectionTabs->setExpanding(false);
    sectionTabs->setDrawBase(false);
    sectionTabs->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    pageHostLayout->addWidget(sectionTabs, 0, Qt::AlignLeft);
    pageHostLayout->addWidget(pages_, 1);

    const auto showSectionTabs = [sectionTabs](
        const std::vector<std::pair<QString, int>>& entries, int currentPage)
    {
        QSignalBlocker blocker(sectionTabs);
        bool sameTabs = sectionTabs->count() == static_cast<int>(entries.size());
        for (int index = 0; sameTabs && index < sectionTabs->count(); ++index)
            sameTabs = sectionTabs->tabText(index) == entries[index].first &&
                sectionTabs->tabData(index).toInt() == entries[index].second;
        if (!sameTabs)
        {
            while (sectionTabs->count() > 0) sectionTabs->removeTab(0);
            for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            {
                sectionTabs->addTab(entries[index].first);
                sectionTabs->setTabData(index, entries[index].second);
            }
        }
        int selected = 0;
        for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            if (entries[index].second == currentPage) selected = index;
        sectionTabs->setCurrentIndex(selected);
        sectionTabs->setVisible(!entries.empty());
    };
    const auto updateSectionTabs = [showSectionTabs, shadersNavigation,
        shortcutsNavigation, vpNavigation, directShowNavigation](int page)
    {
        if (page == 8 || page == 9 || page == 14)
        {
            shadersNavigation->setChecked(true);
            showSectionTabs({ { QStringLiteral("Setup"), 14 },
                { QStringLiteral("Standard"), 8 },
                { QStringLiteral("NLS"), 9 } }, page);
        }
        else if (page == 2 || page == 4 || page == 11 || page == 13 ||
			page == 16 || page == 17 || page == 18)
        {
            vpNavigation->setChecked(true);
            showSectionTabs({ { QStringLiteral("Rendering"), 2 },
				{ QStringLiteral("Scaling"), 17 },
				{ QStringLiteral("Color"), 16 },
                { QStringLiteral("Output"), 13 },
				{ QStringLiteral("Screen"), 4 },
				{ QStringLiteral("Zoom"), 18 },
				{ QStringLiteral("Processing"), 11 } }, page);
        }
        else if (page == 3 || page == 12)
        {
            directShowNavigation->setChecked(true);
            showSectionTabs({ { QStringLiteral("General"), 3 },
                { QStringLiteral("Input Processing"), 12 } }, page);
        }
        else if (page == 6 || page == 15)
        {
            shortcutsNavigation->setChecked(true);
            showSectionTabs({ { QStringLiteral("Setup"), 15 },
                { QStringLiteral("Shortcuts"), 6 } }, page);
        }
        else
            showSectionTabs({}, page);
    };
    connect(sectionTabs, &QTabBar::currentChanged, this,
        [this, sectionTabs](int tab)
        {
            if (tab < 0) return;
            const int page = sectionTabs->tabData(tab).toInt();
            if (page >= 0 && page < pages_->count() && pages_->currentIndex() != page)
                selectPage(page);
        });
    connect(pages_, &QStackedWidget::currentChanged, this, updateSectionTabs);
    updateSectionTabs(pages_->currentIndex());
    centerLayout->addWidget(navigation_);
    centerLayout->addWidget(pageHost, 1);
    configurationHost_ = center;
    rootLayout->addWidget(center, 1);

    auto* footer = new QWidget;
    footer->setObjectName(QStringLiteral("footer"));
    footer->setMinimumHeight(54);
    footer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 8, 16, 8);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("configurationStatus"));
    status_->setAccessibleName(QStringLiteral("Configuration status"));
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    status_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    status_->setWordWrap(true);
    footerLayout->addWidget(status_, 1);
    effectSummary_ = new QLabel;
    effectSummary_->setObjectName(QStringLiteral("configurationEffectSummary"));
    effectSummary_->setAccessibleName(QStringLiteral("Pending configuration effect"));
    effectSummary_->setWordWrap(true);
    effectSummary_->setMinimumWidth(180);
    footerLayout->addWidget(effectSummary_);
    applyButton_ = new QPushButton(QStringLiteral("&Apply"));
    saveButton_ = new QPushButton(QStringLiteral("&OK"));
    auto* cancel = new QPushButton(QStringLiteral("&Cancel"));
    applyButton_->setObjectName(QStringLiteral("applyConfiguration"));
    saveButton_->setObjectName(QStringLiteral("okConfiguration"));
    cancel->setObjectName(QStringLiteral("cancelConfiguration"));
    saveButton_->setProperty("primary", true);
    saveButton_->setDefault(true);
    applyButton_->setEnabled(configurationLoaded_ && dirty_);
    saveButton_->setEnabled(configurationLoaded_);
    footerLayout->addWidget(saveButton_);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(applyButton_);
    rootLayout->addWidget(footer);
    updateEffectSummary();

    connect(saveButton_, &QPushButton::clicked, this, [this]
    {
        // A clean OK is just a close. A dirty OK must remain open if validation
        // or safe persistence fails, so the user can correct the candidate.
        if (!dirty_) hide();
        else if (saveChanges()) hide();
    });
    connect(cancel, &QPushButton::clicked, this, [this]
    {
        // Cancel is intentionally an editor-only operation: the working copy
        // is discarded without touching the file or signaling VideoProcessor.
        dirty_ = false;
        hide();
        loadConfiguration();
		rebuildConfigurationShell();
    });
    connect(applyButton_, &QPushButton::clicked, this, [this] { applyChanges(); });
    auto* saveShortcut = new QShortcut(QKeySequence::Save, root);
    saveShortcut->setContext(Qt::ApplicationShortcut);
    connect(saveShortcut, &QShortcut::activated, applyButton_, [this]
    {
        if (applyButton_ && applyButton_->isEnabled()) applyButton_->click();
    });
    auto selectAdjacentPage = [this](int direction)
    {
        if (!pages_ || pages_->count() == 0) return;
        selectPage((pages_->currentIndex() + direction + pages_->count()) % pages_->count());
    };
    auto* nextPage = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), root);
    nextPage->setContext(Qt::ApplicationShortcut);
    connect(nextPage, &QShortcut::activated, this,
        [selectAdjacentPage] { selectAdjacentPage(1); });
    auto* previousPage = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), root);
    previousPage->setContext(Qt::ApplicationShortcut);
    connect(previousPage, &QShortcut::activated, this,
        [selectAdjacentPage] { selectAdjacentPage(-1); });

    applyRendererVisibilityFilter(configuredBooleanValue(value(
        QStringLiteral("general"), QStringLiteral("hide_legacy_renderers")), true));
    updateValidationState();
    return root;
}

QPushButton* ConfigEditorWindow::addNavigationButton(const QString& text, int pageIndex)
{
    auto* button = new QPushButton;
    button->setText(text);
    button->setAccessibleName(text);
    button->setAccessibleDescription(QStringLiteral("Open the %1 settings page.").arg(text));
    button->setProperty("nav", true);
    button->setProperty("pageIndex", pageIndex);
    button->setCheckable(true);
    button->setFlat(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(button, &QToolButton::clicked, this, [this, pageIndex] { selectPage(pageIndex); });
    return button;
}

QWidget* ConfigEditorWindow::createCard(const QString& title, const QString& description, QWidget* content)
{
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    layout->setSpacing(6);
    auto* heading = new QLabel(title);
    heading->setProperty("cardTitle", true);
    layout->addWidget(heading);
    if (!description.isEmpty()) layout->addWidget(helpLabel(description));
    layout->addSpacing(4);
    layout->addWidget(content);
    return card;
}

QWidget* ConfigEditorWindow::createPage(const QString& title, const QString& description, QWidget* body)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setAccessibleName(QStringLiteral("%1 settings").arg(title));
    scroll->setObjectName(QStringLiteral("pageViewport"));
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("pageViewport"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kPageMargin, 10, kPageMargin, 8);
    layout->setSpacing(4);
    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("pageTitle"));
    heading->setAccessibleName(QStringLiteral("%1 settings").arg(title));
    layout->addWidget(heading);
    auto* subtitle = new QLabel(description);
    subtitle->setObjectName(QStringLiteral("pageDescription"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);
    layout->addSpacing(10);
    layout->addWidget(body);
    layout->addStretch();
    scroll->setWidget(page);
    return scroll;
}

QWidget* ConfigEditorWindow::createStartupPage()
{
    // Keep old configurations loadable, but migrate the former public display
    // name in the pending document so the next editor save uses the final name.
    if (configurationLoaded_ && document_ &&
        value(QStringLiteral("general"), QStringLiteral("renderer")).compare(
            QStringLiteral("VideoProcessor Renderer (Alpha)"), Qt::CaseInsensitive) == 0)
    {
        document_->SetKnown("general", "renderer", "VP Renderer");
        dirty_ = true;
        hasPendingMigrations_ = true;
    }

	auto* cards = new ResponsiveCardGrid;
	auto persistDiscoveredDefault = [this](const QString& key,
		const QStringList& choices)
	{
		if (!configurationLoaded_ || !document_ ||
			!value(QStringLiteral("general"), key).trimmed().isEmpty())
			return;
		QString selected;
		for (const QString& choice : choices)
			if (!choice.trimmed().isEmpty())
			{
				selected = choice.trimmed();
				break;
			}
		if (selected.isEmpty()) return;
		// A missing or zero-byte file has no legacy renderer-profile layout to
		// preserve. Mark the document as the unified format before adding startup
		// hardware keys so [general] is validated with the application schema.
		if (document_->loadedBytes.empty())
			document_->AddSection("vprenderer");
		document_->AddSection("general");
		if (document_->SetKnown("general", key.toLocal8Bit().constData(),
			selected.toLocal8Bit().constData()))
		{
			dirty_ = true;
			hasPendingMigrations_ = true;
		}
	};

    auto* hardware = new QWidget;
    auto* hardwareForm = new QFormLayout(hardware);
    hardwareForm->setContentsMargins(0, 0, 0, 0);
    hardwareForm->setHorizontalSpacing(18);
    hardwareForm->setVerticalSpacing(8);
	persistDiscoveredDefault(QStringLiteral("capture_device"), captureDevices_);
	auto* captureDevice = bindChoiceField(QStringLiteral("general"),
        QStringLiteral("capture_device"), captureDevices_.isEmpty() ?
            QStringList{ QString() } : captureDevices_,
        captureDevices_.isEmpty() ? QStringList{ QStringLiteral("No capture devices discovered") } : QStringList{});
    hardwareForm->addRow(QStringLiteral("Capture device"), captureDevice);

    auto* captureConnection = new QComboBox;
    captureConnection->setObjectName(QStringLiteral("fieldControl"));
    captureConnection->setObjectName(controlName(QStringLiteral("general"),
        QStringLiteral("capture_input")));
    auto updateCaptureConnections = [this, captureConnection](const QString& deviceName)
    {
        const QSignalBlocker blocker(captureConnection);
        captureConnection->clear();
        QStringList connections;
        for (auto found = captureConnections_.cbegin();
            found != captureConnections_.cend(); ++found)
            if (found.key().compare(deviceName, Qt::CaseInsensitive) == 0)
            {
                connections = found.value();
                break;
            }
        QStringList uniqueConnections;
        for (const QString& connection : connections)
        {
            const QString normalized = connection.trimmed();
            if (normalized.isEmpty() ||
                uniqueConnections.contains(normalized, Qt::CaseInsensitive))
                continue;
            uniqueConnections.push_back(normalized);
        }
        const QString configured = value(QStringLiteral("general"),
            QStringLiteral("capture_input"));
        if (uniqueConnections.size() == 1)
        {
            captureConnection->addItem(uniqueConnections.front(),
                uniqueConnections.front());
            captureConnection->setCurrentIndex(0);
            captureConnection->setEnabled(false);
            captureConnection->setToolTip(
                QStringLiteral("This capture device exposes only one input connection."));
            return;
        }

        captureConnection->setEnabled(uniqueConnections.size() > 1);
        captureConnection->setToolTip(uniqueConnections.isEmpty() ?
            QStringLiteral("No input connections were discovered for this capture device.") :
            QStringLiteral("Choose the capture device input connection."));
        captureConnection->addItem(uniqueConnections.isEmpty() ?
            QStringLiteral("No inputs discovered") : QStringLiteral("Device default"),
            QString());
        for (const QString& connection : uniqueConnections)
            captureConnection->addItem(connection, connection);
        if (!configured.isEmpty())
        {
            int index = captureConnection->findData(configured, Qt::UserRole,
                Qt::MatchFixedString);
            if (index < 0)
            {
                captureConnection->addItem(
                    QStringLiteral("%1 (unavailable)").arg(configured), configured);
                index = captureConnection->count() - 1;
            }
            captureConnection->setCurrentIndex(index);
        }
    };
    updateCaptureConnections(captureDevice->currentText());
    connect(captureDevice, &QComboBox::currentTextChanged, this,
        updateCaptureConnections);
    connect(captureConnection, qOverload<int>(&QComboBox::activated), this,
        [this, captureConnection](int index)
    {
        if (!document_ || index < 0) return;
        const QString selected = captureConnection->itemData(index).toString();
        if (selected.isEmpty()) document_->RemoveKnown("general", "capture_input");
        else document_->SetKnown("general", "capture_input",
            selected.toLocal8Bit().constData());
        markDirty();
    });
    hardwareForm->addRow(QStringLiteral("Input connection"), captureConnection);
    auto* behavior = new QWidget;
    auto* behaviorLayout = new QVBoxLayout(behavior);
    behaviorLayout->setContentsMargins(0, 0, 0, 0);
    behaviorLayout->setSpacing(8);
    behaviorLayout->addWidget(bindCheckField(QStringLiteral("Start fullscreen"), QStringLiteral("general"), QStringLiteral("fullscreen")));
    behaviorLayout->addWidget(bindCheckField(QStringLiteral("Windowed fullscreen"), QStringLiteral("general"), QStringLiteral("windowed_fullscreen_mode")));
    behaviorLayout->addWidget(bindCheckField(QStringLiteral("Start as Video Only"),
        QStringLiteral("general"), QStringLiteral("noui"),
        configuredBooleanValue(value(QStringLiteral("general"),
            QStringLiteral("no_ui")), false)));
    behaviorLayout->addWidget(bindCheckField(QStringLiteral("Start minimized"), QStringLiteral("general"), QStringLiteral("startminimized")));
    // Scene detection is the sole supported choice here. It is deliberately
    // opt-in: an absent value means off, and clearing the checkbox writes the
    // explicit false value. The basic correction and feature-disable switches
    // are retained for manual compatibility editing only.
    behaviorLayout->addWidget(bindCheckField(QStringLiteral("Scene detection"),
        QStringLiteral("general"), QStringLiteral("scene_detect"), false));

    auto* source = new QWidget;
    auto* sourceForm = new QFormLayout(source);
    sourceForm->setContentsMargins(0, 0, 0, 0);
    sourceForm->setVerticalSpacing(8);
    monitorChoice_ = bindChoiceField(QStringLiteral("general"),
        QStringLiteral("fullscreen_monitor_name"), withDefaultChoice(monitors_),
        { QStringLiteral("Default monitor") });
    sourceForm->addRow(QStringLiteral("Monitor"), monitorChoice_);
	const bool hideLegacy = configuredBooleanValue(value(QStringLiteral("general"),
		QStringLiteral("hide_legacy_renderers")), true);
	const QStringList visibleRenderers = hideLegacy ? filteredRenderers_ : allRenderers_;
	persistDiscoveredDefault(QStringLiteral("renderer"), visibleRenderers);
    rendererChoice_ = bindChoiceField(QStringLiteral("general"),
        QStringLiteral("renderer"), allRenderers_.isEmpty() ?
            QStringList{ QString() } : allRenderers_,
        allRenderers_.isEmpty() ? QStringList{ QStringLiteral("No renderers discovered") } : QStringList{});
    for (int index = 0; index < rendererChoice_->count(); ++index)
        rendererChoice_->setItemData(index,
            rendererChoice_->itemData(index).toString(), kRendererNameRole);
    sourceForm->addRow(QStringLiteral("Renderer"), rendererChoice_);
    auto* hideLegacyRenderers = configuredCheck(
        QStringLiteral("Hide legacy renderers"), value(QStringLiteral("general"),
            QStringLiteral("hide_legacy_renderers")), true);
    hideLegacyRenderers->setObjectName(controlName(QStringLiteral("general"),
        QStringLiteral("hide_legacy_renderers")));
    hideLegacyRenderers->setEnabled(true);
    sourceForm->addRow(QString(), hideLegacyRenderers);
    connect(hideLegacyRenderers, &QCheckBox::toggled, this,
        [this](bool hide) { applyRendererVisibilityFilter(hide); });
    connect(hideLegacyRenderers, &QCheckBox::toggled, this,
        [this, hideLegacyRenderers](bool checked)
    {
        if (!document_) return;
        document_->SetKnown("general", "hide_legacy_renderers",
            checked ? "true" : "false");
        editOrder_[hideLegacyRenderers->objectName()] = ++editSerial_;
        dirty_ = true;
        if (effectSummary_)
            effectSummary_->setText(QStringLiteral(
                "Save required: Config renderer visibility"));
        if (saveButton_)
            saveButton_->setEnabled(configurationLoaded_ && validationValid_);
        if (applyButton_)
            applyButton_->setEnabled(configurationLoaded_ && validationValid_);
    });
    applyRendererVisibilityFilter(hideLegacy);
    prepareRendererPopup();
    sourceForm->addRow(QString(), bindCheckField(
        QStringLiteral("Switch refresh rate"), QStringLiteral("general"),
        QStringLiteral("switch_refresh_rate"), true));
	auto* profileChangeDisplay = new QSpinBox;
	profileChangeDisplay->setObjectName(controlName(QStringLiteral("general"),
		QStringLiteral("profile_change_display_seconds")));
	profileChangeDisplay->setAccessibleName(
		QStringLiteral("Profile change display duration"));
	profileChangeDisplay->setRange(0, 60);
	profileChangeDisplay->setSuffix(QStringLiteral(" seconds"));
	profileChangeDisplay->setSpecialValueText(QStringLiteral("Off"));
	profileChangeDisplay->setToolTip(QStringLiteral(
		"How long profile selections remain visible, including the fade. Set 0 to never display them."));
	bool profileDisplayOk = false;
	const int configuredProfileDisplay = value(QStringLiteral("general"),
		QStringLiteral("profile_change_display_seconds")).toInt(&profileDisplayOk);
	profileChangeDisplay->setValue(profileDisplayOk ? configuredProfileDisplay : 5);
	connect(profileChangeDisplay, qOverload<int>(&QSpinBox::valueChanged), this,
		[this, profileChangeDisplay](int seconds)
	{
		if (!document_) return;
		document_->SetKnown("general", "profile_change_display_seconds",
			std::to_string(seconds));
		editOrder_[profileChangeDisplay->objectName()] = ++editSerial_;
		markDirty();
	});
	sourceForm->addRow(QStringLiteral("Profile display"),
		profileChangeDisplay);

    auto* input = new QWidget;
    auto* inputForm = new QFormLayout(input);
    inputForm->setContentsMargins(0, 0, 0, 0);
    inputForm->setVerticalSpacing(8);
    inputForm->addRow(QStringLiteral("Video conversion"), bindChoiceField(QStringLiteral("general"),
        QStringLiteral("video_conversion"), { QStringLiteral("NONE"), QStringLiteral("V210_TO_P010") },
        { QStringLiteral("Disabled"), QStringLiteral("V210 to P010") }));
    inputForm->addRow(QStringLiteral("Container color space"), bindChoiceField(QStringLiteral("general"),
        QStringLiteral("container_colorspace"),
        { QString(), QStringLiteral("BT2020"), QStringLiteral("P3_D65"), QStringLiteral("P3_DCI"),
          QStringLiteral("P3_D60"), QStringLiteral("REC709"), QStringLiteral("REC601_525"), QStringLiteral("REC601_625") },
        { QStringLiteral("Follow input") }));
    inputForm->addRow(QStringLiteral("HDR color space"), bindChoiceField(QStringLiteral("general"),
        QStringLiteral("hdr_colorspace"),
        { QStringLiteral("FOLLOW_INPUT"), QStringLiteral("FOLLOW_INPUT_LLDV"),
          QStringLiteral("FOLLOW_CONTAINER"), QStringLiteral("BT2020"), QStringLiteral("P3"), QStringLiteral("REC709") },
        { QStringLiteral("Follow input"), QStringLiteral("Follow input (LLDV)"),
          QStringLiteral("Follow container"), QStringLiteral("BT.2020"), QStringLiteral("P3"), QStringLiteral("Rec. 709") }));
    inputForm->addRow(QStringLiteral("HDR luminance"), bindChoiceField(QStringLiteral("general"),
        QStringLiteral("hdr_luminance"),
        { QStringLiteral("FOLLOW_INPUT"), QStringLiteral("FOLLOW_INPUT_LLDV"), QStringLiteral("HDR_LUMINANCE_USER") },
        { QStringLiteral("Follow input"), QStringLiteral("Follow input (LLDV)"), QStringLiteral("User values") }));

    cards->addCard(createCard(QStringLiteral("Hardware"),
        QStringLiteral("Capture selection used when VP starts."), hardware));
    cards->addCard(createCard(QStringLiteral("General behavior"),
        QStringLiteral("How VideoProcessor opens and prepares a source."), behavior));
    cards->addCard(createCard(QStringLiteral("Display"),
        QStringLiteral("Monitor targeting and refresh-rate behavior. Refresh switching currently applies to VP Renderer."), source));
    cards->addCard(createCard(QStringLiteral("Input processing"),
        QStringLiteral("Shared source conversion and metadata defaults. Apply restarts the renderer; renderer pages may override each value."), input));
    return createPage(QStringLiteral("General"), QStringLiteral("Choose how VideoProcessor starts and which hardware it uses."), cards);
}

QWidget* ConfigEditorWindow::createProfilePage(const QString& title, const QString& description,
    const QString& sectionPrefix)
{
    // A short-lived Color Config split placed the SDR white/black targets in
    // the wrong group. Restore them to the matching Rendering profile in the
    // pending document. The disk file changes only after the user explicitly
    // applies the editor changes.
    if (configurationLoaded_ && document_ &&
        sectionPrefix == QStringLiteral("vprenderer"))
    {
        const QStringList luminanceKeys = {
            QStringLiteral("sdr_target_nits"),
            QStringLiteral("sdr_black_nits")
        };
        bool migrated = false;
        for (const QString& renderingSection : profileSections(
            QStringLiteral("vprenderer")))
        {
            const QString suffix = renderingSection == QStringLiteral("vprenderer") ?
                QStringLiteral("Default") :
                renderingSection.mid(QStringLiteral("vprenderer.").size());
            const QString colorSection =
                QStringLiteral("vprenderer.color.%1").arg(suffix);
            for (const QString& key : luminanceKeys)
            {
                const QString configured = value(colorSection, key);
                if (configured.isEmpty()) continue;
                if (value(renderingSection, key).isEmpty())
                    document_->SetKnown(renderingSection.toStdString(),
                        key.toStdString().c_str(),
                        configured.toLocal8Bit().constData());
                document_->RemoveKnown(colorSection.toStdString(),
                    key.toStdString().c_str());
                migrated = true;
            }
        }
        if (migrated)
        {
            dirty_ = true;
            hasPendingMigrations_ = true;
        }
    }

    // Literal roots are the legacy unnamed form. Profiles in the editor are
    // named and their file order alone selects the default, so migrate a root
    // to a unique generated name in the pending document. Disk is unchanged
    // until the user explicitly saves.
    const bool rendererOwnsInputPolicy = sectionPrefix == QStringLiteral("vprenderer") &&
        ( !value(sectionPrefix, QStringLiteral("video_conversion")).isEmpty() ||
          !value(sectionPrefix, QStringLiteral("container_colorspace")).isEmpty() ||
          !value(sectionPrefix, QStringLiteral("hdr_colorspace")).isEmpty() ||
          !value(sectionPrefix, QStringLiteral("hdr_luminance")).isEmpty() );
    if (configurationLoaded_ && document_ && !rendererOwnsInputPolicy &&
        profileSections(sectionPrefix).contains(sectionPrefix, Qt::CaseInsensitive))
    {
        const QStringList before = profileSections(sectionPrefix);
        QString renamed = sectionPrefix + QStringLiteral(".profile_1");
        int suffix = 2;
        while (before.contains(renamed, Qt::CaseInsensitive))
            renamed = sectionPrefix + QStringLiteral(".profile_%1").arg(suffix++);
        if (document_->RenameSection(sectionPrefix.toStdString(), renamed.toStdString()))
        {
            QStringList ordered = before;
            ordered[ordered.indexOf(sectionPrefix, 0, Qt::CaseInsensitive)] = renamed;
            const QString newDefault = ordered.front();
            if (newDefault != renamed)
            {
                QStringList defaultOnlyKeys;
                for (const QString& key : defaultOnlyKeys)
                {
                    const QString configured = value(renamed, key);
                    if (configured.isEmpty()) continue;
                    const std::string keyText = key.toStdString();
                    document_->RemoveKnown(renamed.toStdString(), keyText.c_str());
                    document_->SetKnown(newDefault.toStdString(), keyText.c_str(), configured.toLocal8Bit().constData());
                }
            }
            dirty_ = true;
            hasPendingMigrations_ = true;
        }
    }

    // Fold legacy queue ownership into the first ordered queue profile in the
    // pending document. Compatibility readers remain in place for old files,
    // but every save from this editor uses [queue.*] exclusively.
    if (configurationLoaded_ && document_ && sectionPrefix == QStringLiteral("queue"))
    {
        const QStringList queues = profileSections(sectionPrefix);
        if (!queues.isEmpty())
        {
            const QString baseline = queues.front();
            bool migrated = false;
            auto migrate = [this, &baseline, &migrated](const QString& oldSection,
                const QString& oldKey, const QString& newKey)
            {
                const QString legacy = value(oldSection, oldKey);
                if (legacy.isEmpty()) return;
                if (value(baseline, newKey).isEmpty())
                    document_->SetKnown(baseline.toStdString(),
                        newKey.toStdString().c_str(), legacy.toLocal8Bit().constData());
                document_->RemoveKnown(oldSection.toStdString(),
                    oldKey.toStdString().c_str());
                migrated = true;
            };
            migrate(QStringLiteral("queue_recovery"),
                QStringLiteral("reset_after_render_restart_seconds"),
                QStringLiteral("reset_after_render_restart_seconds"));
            migrate(QStringLiteral("queue_recovery"),
                QStringLiteral("reset_queue_too_large_percent"),
                QStringLiteral("reset_queue_too_large_percent"));
            migrate(QStringLiteral("directshow"),
                QStringLiteral("presentation_lead_frames"),
                QStringLiteral("lead_frames"));
            migrate(QStringLiteral("command_line"),
                QStringLiteral("queue_size"), QStringLiteral("queue_size"));
            for (const QString& queueSection : queues)
            {
                const QString legacy = value(queueSection,
                    QStringLiteral("steady_reserve_frames"));
                if (legacy.isEmpty()) continue;
                if (value(queueSection, QStringLiteral("target_frames")).isEmpty())
                    document_->SetKnown(queueSection.toStdString(), "target_frames",
                        legacy.toLocal8Bit().constData());
                document_->RemoveKnown(queueSection.toStdString(),
                    "steady_reserve_frames");
                migrated = true;
            }
            if (migrated) markDirty();
        }
    }
    struct Field
    {
        QString key;
        QWidget* widget = nullptr;
        enum Kind { Text, Boolean, Choice, Integer } kind = Text;
        double displayScale = 1.0;
    };
    struct State { QString section; bool loading = false; };
    struct QueuePolicy
    {
        const char* id;
        const char* label;
        const char* summary;
        int capacity;
        int lead;
        int target;
    };
    static constexpr QueuePolicy queuePolicies[] = {
        { "low_latency", "Low latency", "Keeps at most two VP queue frames; targets one frame.", 2, 0, 1 },
        { "balanced", "Balanced", "Uses a moderate queue for ordinary playback.", 16, 3, 2 },
        { "reliable", "Reliable", "Uses a larger queue for the most resilient playback.", 32, 4, 3 },
    };
    auto state = std::make_shared<State>();
    auto fields = std::make_shared<std::vector<Field>>();

    auto* splitter = new ResponsiveSplitter;

    auto* listContent = new QWidget;
    auto* listLayout = new QVBoxLayout(listContent);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    auto* add = new QPushButton(QStringLiteral("+ Add profile"));
    add->setObjectName(controlName(sectionPrefix, QStringLiteral("add_profile")));
    listLayout->addWidget(add);
    auto* actions = new QHBoxLayout;
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    auto* up = new QPushButton(QStringLiteral("Move up"));
    auto* down = new QPushButton(QStringLiteral("Move down"));
    remove->setObjectName(controlName(sectionPrefix, QStringLiteral("remove_profile")));
    up->setObjectName(controlName(sectionPrefix, QStringLiteral("move_up")));
    down->setObjectName(controlName(sectionPrefix, QStringLiteral("move_down")));
    remove->setProperty("danger", true);
    actions->addWidget(remove);
    actions->addWidget(up);
    actions->addWidget(down);
    listLayout->addLayout(actions);
    auto* list = new QListWidget;
    list->setObjectName(controlName(sectionPrefix, QStringLiteral("profiles")));
    list->setAccessibleName(QStringLiteral("%1 profiles").arg(title));
    list->setAccessibleDescription(
        QStringLiteral("Ordered profiles. The first profile is the default. Use the buttons or drag to reorder."));
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDragDropOverwriteMode(false);
    const bool showsActiveProfile = sectionPrefix == QStringLiteral("queue") ||
        sectionPrefix == QStringLiteral("vprenderer") ||
        sectionPrefix == QStringLiteral("vprenderer.scaling") ||
        sectionPrefix == QStringLiteral("vprenderer.color") ||
        sectionPrefix == QStringLiteral("vprenderer.output") ||
        sectionPrefix == QStringLiteral("vprenderer.viewport") ||
        sectionPrefix == QStringLiteral("vprenderer.zoom");
    if (showsActiveProfile)
    {
        list->setItemDelegate(new ProfileStateItemDelegate(list));
        activeProfileLists_.push_back({ list, sectionPrefix });
    }
    listLayout->addWidget(list, 1);

    auto* detail = new QWidget;
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(12);
    auto* selectedTitle = new QLabel(QStringLiteral("Profile"));
    selectedTitle->setProperty("cardTitle", true);
    detailLayout->addWidget(selectedTitle);
    auto* name = new QLineEdit;
    name->setObjectName(controlName(sectionPrefix, QStringLiteral("name")));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Name"), name,
        QStringLiteral("The first profile in the list is the default; list order determines precedence.")));
    auto* shortcut = new QLineEdit;
    shortcut->setObjectName(controlName(sectionPrefix, QStringLiteral("shortcut")));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Shortcut key"), shortcut,
        QStringLiteral("Optional. Activates this profile in addition to its rule.")));
    auto* useRule = new QCheckBox(QStringLiteral("Use rule"));
    useRule->setObjectName(controlName(sectionPrefix, QStringLiteral("use_rule")));
    detailLayout->addWidget(useRule);
    auto* rule = new QPlainTextEdit;
    rule->setObjectName(controlName(sectionPrefix, QStringLiteral("when")));
    rule->setPlaceholderText(QStringLiteral("${key} == \"F2\""));
    rule->setMaximumBlockCount(3);
    rule->setMinimumHeight(72);
    rule->setMaximumHeight(96);
    auto* ruleField = fieldWithHelp(QStringLiteral("When should VP use this profile?"), rule,
        QStringLiteral("Optional source-condition rule. Example: ${width} >= 1920 && ${eotf} == \"HDR\"."));
    detailLayout->addWidget(ruleField);

    auto* profileFields = new QWidget;
    auto* profileFieldsLayout = new QVBoxLayout(profileFields);
    profileFieldsLayout->setContentsMargins(0, 8, 0, 0);
    profileFieldsLayout->setSpacing(8);
    QFormLayout* form = nullptr;
    auto createForm = [] (QWidget* parent)
    {
        auto* created = new QFormLayout(parent);
        created->setContentsMargins(0, 0, 0, 0);
        created->setHorizontalSpacing(18);
        created->setVerticalSpacing(8);
        return created;
    };
    auto addPlainForm = [&]
    {
        auto* content = new QWidget;
        profileFieldsLayout->addWidget(content);
        return createForm(content);
    };
    auto addCollapsibleSection = [&](const QString& id, const QString& heading,
        const QString& description, bool expanded)
    {
        const QString sectionObjectPrefix = sectionPrefix == QStringLiteral("vprenderer.viewport") ?
            QStringLiteral("screenSection") :
            (sectionPrefix == QStringLiteral("vprenderer.zoom") ?
                QStringLiteral("zoomSection") : QStringLiteral("rendererSection"));
        auto* section = new QWidget(profileFields);
        auto* sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(5);
        auto* toggle = new QToolButton;
        toggle->setObjectName(QStringLiteral("%1.%2").arg(sectionObjectPrefix, id));
        toggle->setText(heading);
        toggle->setProperty("profileSection", true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        toggle->setCheckable(true);
        toggle->setChecked(expanded);
        toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toggle->setAccessibleDescription(
            QStringLiteral("Expand or collapse the %1 settings.").arg(heading));
        sectionLayout->addWidget(toggle);
        auto* content = new QWidget(section);
        content->setObjectName(QStringLiteral("%1.%2.content").arg(sectionObjectPrefix, id));
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(12, 4, 4, 8);
        contentLayout->setSpacing(7);
        if (!description.isEmpty()) contentLayout->addWidget(helpLabel(description));
        auto* formContent = new QWidget;
        QFormLayout* sectionForm = createForm(formContent);
        contentLayout->addWidget(formContent);
        sectionLayout->addWidget(content);
        // The content must be parented and attached before it can be made
        // visible. Calling setVisible(true) while it is parentless promotes it
        // to a temporary native top-level window on Windows.
        content->setVisible(expanded);
        connect(toggle, &QToolButton::toggled, this, [toggle, content](bool open)
        {
            content->setVisible(open);
            toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        profileFieldsLayout->addWidget(section);
        return sectionForm;
    };
    if (sectionPrefix != QStringLiteral("vprenderer") &&
        sectionPrefix != QStringLiteral("vprenderer.color") &&
        sectionPrefix != QStringLiteral("vprenderer.viewport") &&
        sectionPrefix != QStringLiteral("vprenderer.zoom"))
        form = addPlainForm();
    QCheckBox* anamorphicEnabled = nullptr;
    QLineEdit* anamorphicValue = nullptr;
    const int fixedUnitFieldWidth = QLineEdit().sizeHint().width();
    const auto deprecatedViewportAlias = [sectionPrefix](const QString& key) -> QString
    {
        if (sectionPrefix != QStringLiteral("vprenderer.viewport")) return {};
        if (key == QStringLiteral("screen_aspect")) return QStringLiteral("scope_screen_aspect");
        if (key == QStringLiteral("automatic_crop")) return QStringLiteral("scope_automatic_crop");
        if (key == QStringLiteral("subtitle_fit")) return QStringLiteral("scope_subtitle_fit");
        if (key == QStringLiteral("subtitle_hold_seconds")) return QStringLiteral("scope_subtitle_hold_seconds");
        if (key == QStringLiteral("subtitle_engage_drift_ms")) return QStringLiteral("scope_subtitle_engage_drift_ms");
        if (key == QStringLiteral("subtitle_release_drift_ms")) return QStringLiteral("scope_subtitle_release_drift_ms");
        if (key == QStringLiteral("subtitle_padding_pixels")) return QStringLiteral("scope_subtitle_padding_pixels");
        return {};
    };
    auto addText = [&](const QString& label, const QString& key,
        const QString& unit = {}, double displayScale = 1.0)
    {
        auto* edit = new QLineEdit;
        edit->setObjectName(controlName(sectionPrefix, key));
        edit->setAccessibleName(label);
        if (!unit.isEmpty()) edit->setAccessibleDescription(
            QStringLiteral("Value in %1.").arg(unit));
        QWidget* control = edit;
        if (!unit.isEmpty())
        {
            auto* row = new QWidget;
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);
            edit->setFixedWidth(fixedUnitFieldWidth);
            edit->setAlignment(Qt::AlignRight);
            rowLayout->addWidget(edit);
            auto* unitLabel = new QLabel(unit);
            unitLabel->setObjectName(controlName(sectionPrefix, key) + QStringLiteral(".unit"));
            rowLayout->addWidget(unitLabel);
            rowLayout->addStretch();
            control = row;
        }
        form->addRow(label, control);
        fields->push_back({ key, edit, Field::Text, displayScale });
        connect(edit, &QLineEdit::textChanged, this,
            [this, state, key, sectionPrefix, deprecatedViewportAlias,
                displayScale, edit](const QString& text)
        {
            if (state->loading || state->section.isEmpty() || !document_) return;
            // Text inherited from the default profile is deliberately muted.
            // As soon as the operator types an override, update that state
            // immediately instead of leaving the new value looking inherited
            // until they reselect the profile.
            const bool inherited = text.trimmed().isEmpty() &&
                state->section != sectionPrefix;
            if (edit->property("inherited").toBool() != inherited)
            {
                edit->setProperty("inherited", inherited);
                edit->style()->unpolish(edit);
                edit->style()->polish(edit);
            }
            const QString alias = deprecatedViewportAlias(key);
            if (!alias.isEmpty())
                document_->RemoveKnown(state->section.toStdString(), alias.toStdString().c_str());
			if (text.trimmed().isEmpty())
				document_->RemoveKnown(state->section.toStdString(),
					key.toStdString().c_str());
			else
            {
                QString stored = text.trimmed().compare(
                    QStringLiteral("Auto"), Qt::CaseInsensitive) == 0 ?
                    QStringLiteral("AUTO") : text;
                if (displayScale != 1.0)
                {
                    bool ok = false;
                    const double displayed = text.trimmed().toDouble(&ok);
                    if (ok)
                        stored = QString::number(displayed / displayScale,
                            'g', 12);
                }
                document_->SetKnown(state->section.toStdString(), key.toStdString().c_str(), stored.toLocal8Bit().constData());
            }
            markDirty();
        });
        return edit;
    };
    auto addBoolean = [&](const QString& label, const QString& key)
    {
        auto* check = new QCheckBox;
        check->setObjectName(controlName(sectionPrefix, key));
        check->setAccessibleName(label);
        check->setTristate(false);
        form->addRow(label, check);
        fields->push_back({ key, check, Field::Boolean });
        connect(check, &QCheckBox::toggled, this,
            [this, state, key, deprecatedViewportAlias, check](bool checked)
        {
            if (state->loading || state->section.isEmpty() || !document_) return;
            if (check->property("inherited").toBool())
            {
                check->setProperty("inherited", false);
                check->style()->unpolish(check);
                check->style()->polish(check);
            }
            const QString alias = deprecatedViewportAlias(key);
            if (!alias.isEmpty())
                document_->RemoveKnown(state->section.toStdString(), alias.toStdString().c_str());
            document_->SetKnown(state->section.toStdString(), key.toStdString().c_str(), checked ? "true" : "false");
            markDirty();
        });
		return check;
    };
    auto addChoice = [&](const QString& label, const QString& key,
        const QStringList& choices, bool allowInherited = true)
    {
        auto* combo = new QComboBox;
        combo->setObjectName(controlName(sectionPrefix, key));
        combo->setAccessibleName(label);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (allowInherited)
            combo->addItem(QStringLiteral("Inherited / not set"), QString());
        for (const QString& choice : choices)
            combo->addItem(friendlyChoiceLabel(choice), choice);
        form->addRow(label, combo);
        fields->push_back({ key, combo, Field::Choice });
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, state, key, combo, sectionPrefix](int index)
        {
            if (state->loading || state->section.isEmpty() || !document_ || index < 0) return;
            const QString selected = combo->itemData(index).toString();
            const bool inherited = selected.isEmpty() &&
                state->section != sectionPrefix;
            if (combo->property("inherited").toBool() != inherited)
            {
                combo->setProperty("inherited", inherited);
                combo->style()->unpolish(combo);
                combo->style()->polish(combo);
            }
            const bool canonicalDebanding =
                sectionPrefix == QStringLiteral("vprenderer") &&
                key == QStringLiteral("deband_strength");
            // deband_strength is the single canonical control. Preserve
            // reading the legacy deband toggle, but never write both keys
            // from the editor because they have overlapping semantics.
            if (canonicalDebanding)
                document_->RemoveKnown(state->section.toStdString(), "deband");
            if (selected.isEmpty()) document_->RemoveKnown(state->section.toStdString(), key.toStdString().c_str());
            else
            {
                document_->SetKnown(state->section.toStdString(), key.toStdString().c_str(), selected.toLocal8Bit().constData());
            }
            markDirty();
        });
        return combo;
    };
    const auto addRendererAutoStatus = [this, &form, sectionPrefix](
        const QString& key, QWidget* control)
    {
        if ((sectionPrefix != QStringLiteral("vprenderer") &&
             sectionPrefix != QStringLiteral("vprenderer.scaling") &&
             sectionPrefix != QStringLiteral("vprenderer.color") &&
             sectionPrefix != QStringLiteral("vprenderer.output")) ||
            !form || !control)
            return;
        auto* status = helpLabel(QString());
        status->setObjectName(controlName(sectionPrefix, key) +
            QStringLiteral(".auto_status"));
        QFont statusFont = status->font();
        statusFont.setItalic(true);
        status->setFont(statusFont);
        status->setVisible(false);
        int row = -1;
        QFormLayout::ItemRole role = QFormLayout::SpanningRole;
        QWidget* fieldControl = control;
        while (fieldControl && row < 0)
        {
            form->getWidgetPosition(fieldControl, &row, &role);
            if (row < 0)
                fieldControl = fieldControl->parentWidget();
        }
        if (row < 0 || role != QFormLayout::FieldRole)
        {
            delete status;
            return;
        }
        auto* field = new QWidget;
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(2);
        form->removeWidget(fieldControl);
        fieldLayout->addWidget(fieldControl);
        fieldLayout->addWidget(status);
        form->setWidget(row, QFormLayout::FieldRole, field);
        rendererAutoStatusBindings_.push_back(
            { sectionPrefix, key, control, status });
        if (auto* combo = qobject_cast<QComboBox*>(control))
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int) { refreshRendererAutoStatus(); });
        else if (auto* edit = qobject_cast<QLineEdit*>(control))
            connect(edit, &QLineEdit::textChanged, this,
                [this](const QString&) { refreshRendererAutoStatus(); });
    };
    auto addInteger = [&](const QString& label, const QString& key,
        int minimum, int maximum, const QString& unit)
    {
        auto* spin = new QSpinBox;
        spin->setObjectName(controlName(sectionPrefix, key));
        spin->setAccessibleName(label);
        if (!unit.isEmpty()) spin->setAccessibleDescription(
            QStringLiteral("Value in %1.").arg(unit));
        spin->setRange(minimum, maximum);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignRight);
        spin->setFixedWidth(fixedUnitFieldWidth);
        if (key == QStringLiteral("reset_queue_too_large_percent"))
            spin->setToolTip(QStringLiteral(
                "Allowed range: 1-200%. Observed depth can exceed 100% because "
                "the complete queue path includes work outside nominal capacity."));
        QWidget* control = spin;
        if (!unit.isEmpty())
        {
            auto* row = new QWidget;
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);
            rowLayout->addWidget(spin);
            auto* unitLabel = new QLabel(unit);
            unitLabel->setObjectName(controlName(sectionPrefix, key) +
                QStringLiteral(".unit"));
            rowLayout->addWidget(unitLabel);
            rowLayout->addStretch();
            control = row;
        }
        form->addRow(label, control);
        fields->push_back({ key, spin, Field::Integer });
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, state, key](int selected)
        {
            if (state->loading || state->section.isEmpty() || !document_) return;
            document_->SetKnown(state->section.toStdString(),
                key.toStdString().c_str(), std::to_string(selected));
            markDirty();
        });
    };

    QComboBox* queuePolicy = nullptr;
    QLabel* queuePolicySummary = nullptr;
    if (sectionPrefix == QStringLiteral("queue"))
    {
        queuePolicy = new QComboBox;
        queuePolicy->setObjectName(QStringLiteral("config.queue.policy_preset"));
        queuePolicy->setAccessibleName(QStringLiteral("Queue buffering policy"));
        queuePolicy->addItem(QStringLiteral("Custom"), QString());
        for (const QueuePolicy& policy : queuePolicies)
            queuePolicy->addItem(QString::fromLatin1(policy.label),
                QString::fromLatin1(policy.id));
        form->addRow(QStringLiteral("Buffering policy"), queuePolicy);
        queuePolicySummary = helpLabel(QStringLiteral(
            "Choose a policy for a complete queue baseline."));
        queuePolicySummary->setObjectName(QStringLiteral("config.queue.policy_summary"));
        form->addRow(QString(), queuePolicySummary);
        form = addCollapsibleSection(QStringLiteral("queueAdvanced"),
            QStringLiteral("Advanced"), QStringLiteral(
                "Fine-tune individual queue values. Editing one switches this profile to Custom."), false);
        addInteger(QStringLiteral("Queue depth"), QStringLiteral("queue_size"),
            1, INT_MAX, QStringLiteral("frames"));
        addInteger(QStringLiteral("Lead frames"), QStringLiteral("lead_frames"),
            0, 16, QStringLiteral("frames"));
        addInteger(QStringLiteral("Startup pre-roll"), QStringLiteral("startup_preroll_frames"),
            0, 16, QStringLiteral("frames"));
        addInteger(QStringLiteral("Target frames"), QStringLiteral("target_frames"),
            0, 16, QStringLiteral("frames"));
        addInteger(QStringLiteral("Active-picture lookahead"), QStringLiteral("active_picture_lookahead_frames"),
            0, 8, QStringLiteral("frames"));
        addInteger(QStringLiteral("Queue reset delay"), QStringLiteral("reset_after_render_restart_seconds"),
            1, INT_MAX, QStringLiteral("seconds"));
        addInteger(QStringLiteral("Queue recovery threshold"), QStringLiteral("reset_queue_too_large_percent"),
            1, 200, QStringLiteral("%"));
    }
    else if (sectionPrefix == QStringLiteral("vprenderer.color"))
    {
        form = addCollapsibleSection(QStringLiteral("calibration"),
            QStringLiteral("Display calibration"), QStringLiteral(
                "Describe the calibrated Rec.709 or BT.2020 display response this Color Config targets."), false);
        addChoice(QStringLiteral("Display primaries"),
            QStringLiteral("sdr_target_primaries"),
            { QStringLiteral("REC709"), QStringLiteral("BT2020") }, false);
        auto* outputGamma = addChoice(QStringLiteral("Display transfer / gamma"),
            QStringLiteral("output_gamma"),
            { QStringLiteral("AUTO"), QStringLiteral("bt1886"),
                QStringLiteral("srgb"), QStringLiteral("1.8"),
                QStringLiteral("2.0"), QStringLiteral("2.2"),
                QStringLiteral("2.4"), QStringLiteral("2.6"),
                QStringLiteral("2.8") });
        outputGamma->setToolTip(QStringLiteral(
            "The calibrated display transfer VP targets during color-managed rendering. "
            "It does not change Windows' normal Full RGB / sRGB presentation declaration."));
        addRendererAutoStatus(QStringLiteral("output_gamma"), outputGamma);
        addBoolean(QStringLiteral("Report BT.2020 to display"),
            QStringLiteral("report_bt2020_to_display"));

        form = addCollapsibleSection(QStringLiteral("sourceColor"),
            QStringLiteral("Source transfer"), QStringLiteral(
                "How VP interprets SDR source transfer before this Color Config maps it to the calibrated display target."), false);
        auto* sdrAdjustGamma = addChoice(QStringLiteral("SDR source transfer handling"),
            QStringLiteral("sdr_adjust_gamma"),
            { QStringLiteral("AUTO"), QStringLiteral("on"), QStringLiteral("off") });
        sdrAdjustGamma->setItemText(1, QStringLiteral("Auto"));
        sdrAdjustGamma->setItemText(2, QStringLiteral("On"));
        sdrAdjustGamma->setItemText(3, QStringLiteral("Off"));
        addRendererAutoStatus(QStringLiteral("sdr_adjust_gamma"), sdrAdjustGamma);
        auto* sdrInputTransfer = addChoice(QStringLiteral("SDR input transfer"),
            QStringLiteral("sdr_input_transfer"),
            { QStringLiteral("AUTO"), QStringLiteral("bt1886"), QStringLiteral("srgb"),
                QStringLiteral("1.8"), QStringLiteral("2.0"), QStringLiteral("2.2"),
                QStringLiteral("2.4"), QStringLiteral("2.6"), QStringLiteral("2.8") });
        sdrInputTransfer->setToolTip(QStringLiteral(
            "Declares how SDR source codes are interpreted when gamma adjustment is On."));
        addRendererAutoStatus(QStringLiteral("sdr_input_transfer"), sdrInputTransfer);

    }
    else if (sectionPrefix == QStringLiteral("vprenderer"))
    {
		form = addPlainForm();
		auto* renderingQuality = addChoice(QStringLiteral("Rendering quality"),
			QStringLiteral("quality"), { QStringLiteral("high"),
			QStringLiteral("balanced"), QStringLiteral("fast") });
		connect(renderingQuality, qOverload<int>(&QComboBox::currentIndexChanged), this,
			[this](int) { refreshRendererAutoStatus(); });

        form = addCollapsibleSection(QStringLiteral("toneMapping"),
            QStringLiteral("Tone mapping"), QStringLiteral(
                "HDR tone mapping, gamut compression, dynamic peak handling, and contrast recovery."), false);
        auto* sdrTargetWhiteLevel = addText(QStringLiteral("Target white level"),
            QStringLiteral("sdr_target_nits"), QStringLiteral("nits"));
        connect(sdrTargetWhiteLevel, &QLineEdit::textChanged, this,
            [this](const QString&) { refreshRendererAutoStatus(); });
        auto* sdrBlackLevel = addText(QStringLiteral("Target black level"),
            QStringLiteral("sdr_black_nits"), QStringLiteral("nits"));
        sdrBlackLevel->setPlaceholderText(QStringLiteral("Auto or a numeric value"));
        addRendererAutoStatus(QStringLiteral("sdr_black_nits"), sdrBlackLevel);
        auto* toneMapping = addChoice(QStringLiteral("Tone mapping"), QStringLiteral("tone_mapping"), { QStringLiteral("AUTO"), QStringLiteral("spline"), QStringLiteral("bt2390"), QStringLiteral("st2094-40"), QStringLiteral("reinhard") });
        addRendererAutoStatus(QStringLiteral("tone_mapping"), toneMapping);
        auto* gamutMapping = addChoice(QStringLiteral("Gamut mapping"), QStringLiteral("gamut_mapping"), { QStringLiteral("AUTO"), QStringLiteral("perceptual"), QStringLiteral("softclip"), QStringLiteral("relative"), QStringLiteral("desaturate") });
        addRendererAutoStatus(QStringLiteral("gamut_mapping"), gamutMapping);
        auto* peakDetection = addChoice(QStringLiteral("Peak detection"), QStringLiteral("peak_detection"), { QStringLiteral("AUTO"), QStringLiteral("high_quality"), QStringLiteral("on"), QStringLiteral("off") });
        peakDetection->setItemText(peakDetection->findData(QStringLiteral("on")),
            QStringLiteral("Standard"));
        addRendererAutoStatus(QStringLiteral("peak_detection"), peakDetection);
        auto* contrastRecovery = addText(QStringLiteral("Contrast recovery (0 to 2)"), QStringLiteral("contrast_recovery"));
        contrastRecovery->setPlaceholderText(QStringLiteral("Auto or a value from 0 to 2"));
        addRendererAutoStatus(QStringLiteral("contrast_recovery"), contrastRecovery);

        form = addCollapsibleSection(QStringLiteral("processing"), QStringLiteral("Processing"),
            QString(), false);
        auto* debanding = addChoice(QStringLiteral("Debanding"), QStringLiteral("deband_strength"), { QStringLiteral("AUTO"), QStringLiteral("default"), QStringLiteral("light"), QStringLiteral("off") });
        addRendererAutoStatus(QStringLiteral("deband_strength"), debanding);
        auto* dithering = addChoice(QStringLiteral("Dithering"), QStringLiteral("dithering"),
            { QStringLiteral("AUTO"), QStringLiteral("on"), QStringLiteral("off") });
        dithering->setItemText(1, QStringLiteral("Auto"));
        dithering->setItemText(2, QStringLiteral("On"));
        dithering->setItemText(3, QStringLiteral("Off"));
        dithering->setToolTip(QStringLiteral(
            "Auto uses the rendering-quality preset. On uses libplacebo's default dithering. Off disables dithering."));
        addRendererAutoStatus(QStringLiteral("dithering"), dithering);
        auto* displayBitDepth = addChoice(QStringLiteral("Display bit depth"),
            QStringLiteral("display_bit_depth"),
            { QStringLiteral("AUTO"), QStringLiteral("10"), QStringLiteral("8") });
        displayBitDepth->setItemText(1, QStringLiteral("Auto"));
        displayBitDepth->setItemText(2, QStringLiteral("10-bit or higher"));
        displayBitDepth->setItemText(3, QStringLiteral("8-bit"));
        displayBitDepth->setToolTip(QStringLiteral(
            "Native display precision used as the dithering target. Auto uses the active RGB swapchain depth."));
        addRendererAutoStatus(QStringLiteral("display_bit_depth"), displayBitDepth);

		form = addCollapsibleSection(QStringLiteral("lut"), QStringLiteral("Calibration LUT (3D LUT)"),
            QStringLiteral("Optional lookup-table file and the signal reference used to interpret it."), false);
        const QString lutDirectoryPath = QFileInfo(configPath_).absoluteDir()
            .filePath(QStringLiteral("luts"));
        const auto discoveredLuts = [lutDirectoryPath]()
        {
            QStringList result;
            const QDir lutDirectory(lutDirectoryPath);
            const QFileInfoList lutFiles = lutDirectory.entryInfoList(
                { QStringLiteral("*.cube"), QStringLiteral("*.CUBE") },
                QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo& lutFile : lutFiles)
                result << QStringLiteral("luts/%1").arg(lutFile.fileName());
            return result;
        };
        auto* lutSelector = addChoice(QStringLiteral("3D LUT file"),
            QStringLiteral("lut"), discoveredLuts());
        lutSelector->setToolTip(QStringLiteral(
            "Select a .cube file from VP's luts folder. The selected file is stored relative to the configuration."));
        const auto refreshLutSelector = [this, state, lutSelector, discoveredLuts]
        {
            const QSignalBlocker blocker(lutSelector);
            const QString selected = lutSelector->currentData().toString();
            const QStringList available = discoveredLuts();
            const bool selectionWasRemoved = !selected.isEmpty() &&
                !available.contains(selected, Qt::CaseInsensitive);
            if (selectionWasRemoved && !state->loading && !state->section.isEmpty() && document_)
            {
                document_->RemoveKnown(state->section.toStdString(), "lut");
                markDirty();
            }
            lutSelector->clear();
            lutSelector->addItem(QStringLiteral("Inherited / not set"), QString());
            for (const QString& lut : available)
                lutSelector->addItem(lut, lut);
            lutSelector->setCurrentIndex(selectionWasRemoved ? 0 :
                std::max(0, lutSelector->findData(selected)));
        };
        auto* lutWatcher = new QFileSystemWatcher(lutSelector);
        const auto watchLutDirectory = [lutWatcher, lutDirectoryPath]
        {
            if (QDir(lutDirectoryPath).exists() &&
                !lutWatcher->directories().contains(lutDirectoryPath))
                lutWatcher->addPath(lutDirectoryPath);
        };
        watchLutDirectory();
        connect(lutWatcher, &QFileSystemWatcher::directoryChanged, this,
            [refreshLutSelector](const QString&) { refreshLutSelector(); });
        auto* openLutFolder = new QPushButton;
        openLutFolder->setObjectName(QStringLiteral("config.vprenderer.lut.open_folder"));
        openLutFolder->setText(QStringLiteral("Open LUT folder"));
        openLutFolder->setToolTip(QStringLiteral("Open the folder where VideoProcessor discovers 3D LUT files."));
        openLutFolder->setAccessibleName(QStringLiteral("Open LUT folder"));
        openLutFolder->setMaximumWidth(170);
        connect(openLutFolder, &QPushButton::clicked, this,
            [this, lutDirectoryPath, watchLutDirectory, refreshLutSelector]
        {
            if (!QDir().mkpath(lutDirectoryPath))
            {
                QMessageBox::warning(this, QStringLiteral("LUT folder"),
                    QStringLiteral("VideoProcessor could not create the LUT folder."));
                return;
            }
            watchLutDirectory();
            refreshLutSelector();
            if (!openPathExternally(lutDirectoryPath))
                QMessageBox::warning(this, QStringLiteral("LUT folder"),
                    QStringLiteral("Windows could not open the LUT folder."));
        });
        form->addRow(QString(), openLutFolder);
        form->addRow(QString(), helpLabel(QStringLiteral(
            "Put .cube files in the luts folder next to VideoProcessor.cfg (normally the VP installation). "
            "The selector refreshes automatically when the folder changes. If a selected file is removed, "
            "VP returns that profile to its inherited LUT setting; Reload also refreshes the list.")));
        auto* lutReferenceLuminance = addChoice(
            QStringLiteral("LUT reference luminance (nits)"),
            QStringLiteral("lut_reference_nits"),
            { QStringLiteral("AUTO"), QStringLiteral("40"),
                QStringLiteral("80"),
                QStringLiteral("100"), QStringLiteral("120"),
                QStringLiteral("160"), QStringLiteral("203"),
                QStringLiteral("250"), QStringLiteral("300"),
                QStringLiteral("400"), QStringLiteral("500") });
        lutReferenceLuminance->setToolTip(QStringLiteral(
            "Choose Auto or a common reference luminance from 40 to 500 nits. "
            "Existing custom values remain available in their profile."));
        addRendererAutoStatus(QStringLiteral("lut_reference_nits"), lutReferenceLuminance);
        auto* lutReferenceRange = addChoice(QStringLiteral("LUT reference range"), QStringLiteral("lut_reference_range"), { QStringLiteral("AUTO"), QStringLiteral("full"), QStringLiteral("limited") });
        addRendererAutoStatus(QStringLiteral("lut_reference_range"), lutReferenceRange);
        auto* lutReferenceTransfer = addChoice(QStringLiteral("LUT reference transfer"), QStringLiteral("lut_reference_transfer"), { QStringLiteral("AUTO"), QStringLiteral("srgb"), QStringLiteral("bt1886"), QStringLiteral("2.2"), QStringLiteral("2.4") });
        addRendererAutoStatus(QStringLiteral("lut_reference_transfer"), lutReferenceTransfer);
        auto* lutReferencePrimaries = addChoice(QStringLiteral("LUT reference primaries"), QStringLiteral("lut_reference_primaries"), { QStringLiteral("AUTO"), QStringLiteral("REC709"), QStringLiteral("P3_D65"), QStringLiteral("BT2020") });
        addRendererAutoStatus(QStringLiteral("lut_reference_primaries"), lutReferencePrimaries);

    }
    else if (sectionPrefix == QStringLiteral("vprenderer.output"))
    {
        form = addCollapsibleSection(QStringLiteral("advancedOutput"),
            QStringLiteral("Advanced output"), QStringLiteral(
                "Presentation preference and RGB transport for this output path. "
                "Windows determines the final presentation path."), false);
        form->addRow(QString(), helpLabel(QStringLiteral(
            "Keep these controls on Auto unless the display chain has a known "
            "transport requirement; the OSD reports the effective transport.")));
        auto* outputPresentation = addChoice(QStringLiteral("Presentation preference"),
            QStringLiteral("output_presentation"),
            { QStringLiteral("AUTO"), QStringLiteral("direct"),
                QStringLiteral("composed") });
        outputPresentation->setToolTip(QStringLiteral(
            "Auto is recommended. Direct and Composed are compatibility preferences; "
            "Windows and the renderer choose the effective presentation path."));
		addRendererAutoStatus(QStringLiteral("output_presentation"), outputPresentation);
        auto* outputRange = addChoice(QStringLiteral("RGB output range"),
            QStringLiteral("output_range"),
            { QStringLiteral("AUTO"), QStringLiteral("full"),
                QStringLiteral("limited") });
        outputRange->setToolTip(QStringLiteral(
            "Auto normally uses Full RGB. Select Limited only for a known "
            "limited-range display chain or a transport diagnostic."));
		addRendererAutoStatus(QStringLiteral("output_range"), outputRange);
        auto* outputTransportGamma = addChoice(
            QStringLiteral("Limited transport transfer"),
            QStringLiteral("output_transport_gamma"),
            { QStringLiteral("AUTO"), QStringLiteral("2.2"),
                QStringLiteral("2.4") });
        outputTransportGamma->setToolTip(QStringLiteral(
            "Only applies when RGB output range is Limited. Auto uses the "
            "standard limited-range transport. Display calibration remains "
            "part of the selected Color Config."));
		addRendererAutoStatus(QStringLiteral("output_transport_gamma"), outputTransportGamma);
        auto* outputCompatibility = helpLabel(QString());
        outputCompatibility->setObjectName(
            QStringLiteral("config.vprenderer.output.advanced_output.compatibility"));
        form->addRow(QString(), outputCompatibility);

        form = addCollapsibleSection(QStringLiteral("outputExperiments"),
            QStringLiteral("Output Experiments (beta)"), QStringLiteral(
                "Implementation diagnostics for repeatable renderer testing. "
                "They do not change display calibration, presentation preference, or RGB range. "
                "Changes are saved with this output profile; Apply performs "
                "a hard capture-and-renderer reinitialization before they take effect."), false);
        form->addRow(QString(), helpLabel(QStringLiteral(
            "Diagnostic presets set only the beta controls below. Apply always "
            "hard-reinitializes capture and renderer state.")));
        auto* outputPathProfile = addChoice(QStringLiteral("Diagnostic preset"),
            QStringLiteral("output_path_profile"),
            { QStringLiteral("legacy"), QStringLiteral("proposed"),
                QStringLiteral("custom") });

        outputPathProfile->setItemText(outputPathProfile->findData(QStringLiteral("legacy")),
            QStringLiteral("Normal diagnostics"));
        outputPathProfile->setItemText(outputPathProfile->findData(QStringLiteral("proposed")),
            QStringLiteral("Output investigation"));
        outputPathProfile->setItemText(outputPathProfile->findData(QStringLiteral("custom")),
            QStringLiteral("Custom diagnostics"));
		addBoolean(QStringLiteral("Enable limited + pure Gamma 2.2 experiment"),
			QStringLiteral("diagnostic_allow_limited_g22"));
        addBoolean(QStringLiteral("Disable D3D11 compute shaders"),
            QStringLiteral("diagnostic_disable_compute"));
        addBoolean(QStringLiteral("Force 8-bit SDR swapchain"),
            QStringLiteral("diagnostic_force_8bit_sdr_swapchain"));
        addBoolean(QStringLiteral("Force VP-owned DXGI presenter (flip only, beta)"),
            QStringLiteral("diagnostic_vp_owned_dxgi_presenter"));
        form->addRow(QString(), helpLabel(QStringLiteral(
            "Experimental only: VP owns the DXGI swapchain and Present call. "
            "It supports flip/direct only; Composed always uses libplacebo's "
            "proven presenter.")));
        addBoolean(QStringLiteral("Capture detailed output diagnostics"),
            QStringLiteral("output_diagnostics"));
        addBoolean(QStringLiteral("Disable shader cache"),
            QStringLiteral("diagnostic_disable_shader_cache"));
        const auto updateOutputCompatibility = [this, outputPresentation,
            outputRange, outputTransportGamma, outputCompatibility]()
        {
            const auto* vpOwned = findChild<QCheckBox*>(
                QStringLiteral("config.vprenderer.output.diagnostic_vp_owned_dxgi_presenter"));
            QStringList notices;
            if (outputPresentation->currentData().toString().compare(
                QStringLiteral("composed"), Qt::CaseInsensitive) == 0 &&
                vpOwned && vpOwned->isChecked())
            {
                notices << QStringLiteral(
				"Notice: VP-owned DXGI is Direct-only; Composed uses libplacebo's presenter.");
			}
			const QString gamma = outputTransportGamma->currentData().toString();
			const bool limited = outputRange->currentData().toString().compare(
				QStringLiteral("limited"), Qt::CaseInsensitive) == 0;
			if (limited)
			{
				const auto* limitedG22 = findChild<QCheckBox*>(
					QStringLiteral("config.vprenderer.output.diagnostic_allow_limited_g22"));
				if (gamma == QStringLiteral("2.2") &&
					(!limitedG22 || !limitedG22->isChecked()))
				{
					notices << QStringLiteral(
						"Blocked: Limited RGB with Gamma 2.2 transport is a diagnostic experiment. Use Auto for normal output.");
				}
				else if (gamma != QStringLiteral("AUTO") &&
					gamma != QStringLiteral("2.4") && gamma != QStringLiteral("2.2"))
				{
					notices << QStringLiteral(
						"Blocked: Limited RGB supports Auto/2.4, plus the 2.2 diagnostic experiment.");
				}
				else
				{
					notices << QStringLiteral(
						"Limited RGB changes output transport only; display calibration remains in Rendering.");
				}
			}
            outputCompatibility->setText(notices.join(QStringLiteral("\n")));
            outputCompatibility->setVisible(!notices.isEmpty());
        };
        connect(outputPresentation, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [updateOutputCompatibility](int) { updateOutputCompatibility(); });
        connect(outputTransportGamma, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [updateOutputCompatibility](int) { updateOutputCompatibility(); });
        connect(outputRange, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [updateOutputCompatibility](int) { updateOutputCompatibility(); });
        if (auto* vpOwned = findChild<QCheckBox*>(
            QStringLiteral("config.vprenderer.output.diagnostic_vp_owned_dxgi_presenter")))
        {
            connect(vpOwned, &QCheckBox::toggled, this,
                [updateOutputCompatibility](bool) { updateOutputCompatibility(); });
        }
		if (auto* limitedG22 = findChild<QCheckBox*>(
			QStringLiteral("config.vprenderer.output.diagnostic_allow_limited_g22")))
		{
			connect(limitedG22, &QCheckBox::toggled, this,
				[updateOutputCompatibility](bool) { updateOutputCompatibility(); });
        }
        updateOutputCompatibility();
        const auto applyOutputPathProfile = [this, state, fields,
            outputPathProfile, updateOutputCompatibility](const QString& profile)
        {
            if (state->loading || state->section.isEmpty() || !document_ ||
                profile.isEmpty() || profile == QStringLiteral("custom")) return;
            struct Value { const char* key; const char* value; };
		static constexpr Value legacy[] = {
				{ "diagnostic_allow_limited_g22", "false" },
				{ "diagnostic_disable_compute", "false" },
                { "diagnostic_force_8bit_sdr_swapchain", "false" },
                { "diagnostic_vp_owned_dxgi_presenter", "false" },
                { "output_diagnostics", "false" },
                { "diagnostic_disable_shader_cache", "false" }
            };
		static constexpr Value proposed[] = {
				{ "diagnostic_allow_limited_g22", "true" },
                { "diagnostic_disable_compute", "false" },
                { "diagnostic_force_8bit_sdr_swapchain", "false" },
                { "diagnostic_vp_owned_dxgi_presenter", "false" },
                { "output_diagnostics", "true" },
                { "diagnostic_disable_shader_cache", "false" }
            };
            const Value* values = profile == QStringLiteral("proposed") ?
                proposed : legacy;
            const size_t count = profile == QStringLiteral("proposed") ?
                std::size(proposed) : std::size(legacy);
            const std::string section = state->section.toStdString();
            document_->SetKnown(section, "output_path_profile",
                profile.toLocal8Bit().constData());
            for (size_t index = 0; index < count; ++index)
            {
                document_->SetKnown(section, values[index].key, values[index].value);
                for (const Field& field : *fields)
                    if (field.key == QString::fromLatin1(values[index].key))
                    {
                        const QSignalBlocker blocker(field.widget);
                        if (field.kind == Field::Boolean)
                            qobject_cast<QCheckBox*>(field.widget)->setChecked(
                                QString::fromLatin1(values[index].value) == QStringLiteral("true"));
                        else if (field.kind == Field::Choice)
                            qobject_cast<QComboBox*>(field.widget)->setCurrentIndex(
                                qobject_cast<QComboBox*>(field.widget)->findData(
                                    QString::fromLatin1(values[index].value)));
                        break;
                    }
            }
            const QSignalBlocker profileBlocker(outputPathProfile);
            outputPathProfile->setCurrentIndex(outputPathProfile->findData(profile));
            updateOutputCompatibility();
            markDirty();
        };
        connect(outputPathProfile, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [outputPathProfile, applyOutputPathProfile](int)
        { applyOutputPathProfile(outputPathProfile->currentData().toString()); });
        const auto markOutputPathCustom = [this, state, outputPathProfile]
        {
            if (state->loading || state->section.isEmpty() || !document_ ||
                outputPathProfile->currentData().toString() == QStringLiteral("custom")) return;
            const QSignalBlocker blocker(outputPathProfile);
            outputPathProfile->setCurrentIndex(outputPathProfile->findData(
                QStringLiteral("custom")));
            document_->SetKnown(state->section.toStdString(), "output_path_profile", "custom");
            markDirty();
        };
        for (const Field& field : *fields)
			if (field.key.startsWith(QStringLiteral("diagnostic_")) ||
                field.key == QStringLiteral("output_diagnostics"))
            {
                if (field.kind == Field::Boolean)
                    connect(qobject_cast<QCheckBox*>(field.widget), &QCheckBox::toggled,
                        this, [markOutputPathCustom](bool) { markOutputPathCustom(); });
                else if (field.kind == Field::Choice && field.widget != outputPathProfile)
                    connect(qobject_cast<QComboBox*>(field.widget),
                        qOverload<int>(&QComboBox::currentIndexChanged), this,
                        [markOutputPathCustom](int) { markOutputPathCustom(); });
            }
        auto* resetOutputExperiments = new QPushButton(
            QStringLiteral("Restore Normal Diagnostics"));
        resetOutputExperiments->setObjectName(
            QStringLiteral("config.vprenderer.output.output_experiments.reset_defaults"));
        resetOutputExperiments->setToolTip(QStringLiteral(
            "Restore this profile's normal diagnostic settings."));
        resetOutputExperiments->setAccessibleName(
            QStringLiteral("Restore normal diagnostic settings"));
        connect(resetOutputExperiments, &QPushButton::clicked, this,
            [this, state, applyOutputPathProfile]
        {
            if (state->section.isEmpty() || !document_) return;
            if (QMessageBox::question(this,
                QStringLiteral("Restore normal diagnostics"),
				QStringLiteral("Restore normal diagnostic settings for this output "
				"profile? This leaves display calibration, presentation preference, "
                    "and RGB range unchanged."),
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) return;
            applyOutputPathProfile(QStringLiteral("legacy"));
        });
        form->addRow(QString(), resetOutputExperiments);
    }
	else if (sectionPrefix == QStringLiteral("vprenderer.scaling"))
	{
		form = addPlainForm();
		auto* upscaler = addChoice(QStringLiteral("Upscaler"),
			QStringLiteral("upscaler"), { QStringLiteral("AUTO"),
			QStringLiteral("ewa_lanczos4sharpest"),
			QStringLiteral("ewa_lanczossharp"), QStringLiteral("ewa_lanczos"),
			QStringLiteral("lanczos"), QStringLiteral("catmull_rom"),
			QStringLiteral("bicubic"), QStringLiteral("gaussian"),
			QStringLiteral("oversample"), QStringLiteral("bilinear"),
			QStringLiteral("nearest"), QStringLiteral("none") });
		upscaler->setItemText(upscaler->findData(QStringLiteral("none")),
			QStringLiteral("Use GPU"));
		addRendererAutoStatus(QStringLiteral("upscaler"), upscaler);
		auto* downscaler = addChoice(QStringLiteral("Downscaler"),
			QStringLiteral("downscaler"), { QStringLiteral("AUTO"),
			QStringLiteral("lanczos"), QStringLiteral("mitchell"),
			QStringLiteral("catmull_rom"),
			QStringLiteral("bicubic"), QStringLiteral("gaussian"),
			QStringLiteral("hermite"), QStringLiteral("bilinear"),
			QStringLiteral("box"), QStringLiteral("gpu") });
		downscaler->setItemText(downscaler->findData(QStringLiteral("gpu")),
			QStringLiteral("Use GPU"));
		addRendererAutoStatus(QStringLiteral("downscaler"), downscaler);
		auto* antiRinging = addChoice(QStringLiteral("Anti-ringing"),
			QStringLiteral("sigmoid"), { QStringLiteral("AUTO"),
			QStringLiteral("on"), QStringLiteral("off") });
		antiRinging->setToolTip(QStringLiteral(
			"Optional sigmoidization before upscaling. It reduces ringing artifacts; Auto uses the scaling-quality preset."));
		addRendererAutoStatus(QStringLiteral("sigmoid"), antiRinging);
	}
	else if (sectionPrefix == QStringLiteral("vprenderer.viewport") ||
		sectionPrefix == QStringLiteral("vprenderer.zoom"))
    {
		const bool showGeometry = sectionPrefix == QStringLiteral("vprenderer.viewport");
		const bool showZoom = sectionPrefix == QStringLiteral("vprenderer.zoom");
		if (showGeometry)
		{
        form = addCollapsibleSection(QStringLiteral("geometry"),
            QStringLiteral("Screen geometry"), QStringLiteral(
                "Physical screen shape, picture placement, and fitting controls."), true);
        auto* screenAspect = addText(
            QStringLiteral("Screen aspect ratio"),
            QStringLiteral("screen_aspect"));
        screenAspect->setPlaceholderText(QStringLiteral("16:9, 32:15, or 2100x1000"));
        screenAspect->setToolTip(QStringLiteral(
            "The physical screen shape. Enter a ratio, decimal aspect, or "
            "screen dimensions. This same value controls destination layout, "
            "black-bar cropping, subtitle fitting, and NLS's target aspect."));
        form->addRow(QString(), helpLabel(QStringLiteral(
            "Enter the physical screen shape as a ratio (for example 2.1:1), "
            "a decimal (2.1), or dimensions (2100x1000). VP uses this value "
            "consistently for black bars, cropping, subtitles, and NLS.")));
        auto* verticalAlignment = addChoice(
            QStringLiteral("Vertical picture alignment"),
            QStringLiteral("vertical_alignment"),
            { QStringLiteral("top"), QStringLiteral("center"),
                QStringLiteral("bottom") });
        verticalAlignment->setToolTip(QStringLiteral(
            "Sets the picture's resting position when unused vertical screen "
            "space remains. Subtitle fitting may temporarily move it away "
            "from this edge to keep HDMI subtitle pixels visible."));
        form->addRow(QString(), helpLabel(QStringLiteral(
            "Top and Bottom support one-sided masking. Center preserves the "
            "current presentation. Enabled subtitle fitting can still move "
            "the picture upward or downward as required.")));
        anamorphicEnabled = new QCheckBox(QStringLiteral("Enable anamorphic lens compensation"));
        anamorphicEnabled->setObjectName(controlName(sectionPrefix,
            QStringLiteral("anamorphic_enabled")));
        form->addRow(QString(), anamorphicEnabled);
        anamorphicValue = addText(QStringLiteral("Lens expansion ratio"), QStringLiteral("anamorphic_scale"));
        connect(anamorphicEnabled, &QCheckBox::toggled, this, [this, state, anamorphicValue](bool enabled)
        {
            anamorphicValue->setEnabled(enabled);
            if (state->loading || state->section.isEmpty()) return;
            if (!enabled) document_->RemoveKnown(state->section.toStdString(), "anamorphic_scale");
            else if (anamorphicValue->text().trimmed().isEmpty())
            {
                anamorphicValue->setText(QStringLiteral("1:1"));
                document_->SetKnown(state->section.toStdString(), "anamorphic_scale", "1:1");
            }
            markDirty();
        });
		}
		if (showZoom && !showGeometry)
			form = addPlainForm();
		if (showZoom)
		{
		addBoolean(QStringLiteral("Automatically crop black bars"),
			QStringLiteral("automatic_crop"));
		auto* cropNarrower = addBoolean(
			QStringLiteral("Crop narrower content to fill screen"),
			QStringLiteral("crop_narrower_content_to_fill_screen"));
		auto* cropNarrowerLimit = addText(QStringLiteral("Aspect ratio limit"),
			QStringLiteral("crop_narrower_content_aspect_limit"));
		cropNarrowerLimit->setPlaceholderText(QStringLiteral("Optional, e.g. 2.20:1"));
		cropNarrowerLimit->setEnabled(cropNarrower->isChecked());
		connect(cropNarrower, &QCheckBox::toggled, this,
			[cropNarrowerLimit](bool enabled)
			{ cropNarrowerLimit->setEnabled(enabled); });

		auto* cropWider = addBoolean(
			QStringLiteral("Crop wider content to fill screen"),
			QStringLiteral("crop_wider_content_to_fill_screen"));
		auto* cropWiderLimit = addText(QStringLiteral("Aspect ratio limit"),
			QStringLiteral("crop_wider_content_aspect_limit"));
		cropWiderLimit->setPlaceholderText(QStringLiteral("Optional, e.g. 2.76:1"));
		cropWiderLimit->setEnabled(cropWider->isChecked());
		connect(cropWider, &QCheckBox::toggled, this,
			[cropWiderLimit](bool enabled)
			{ cropWiderLimit->setEnabled(enabled); });

        form = addCollapsibleSection(QStringLiteral("subtitles"),
            QStringLiteral("Subtitles"), QStringLiteral(
                "Keep subtitle content visible, control how VP moves it into the screen, "
                "and keep edge overlays out of HDR peak analysis."), false);
        addBoolean(QStringLiteral("Keep subtitles inside screen bounds"), QStringLiteral("subtitle_fit"));
        auto* pictureOnlyHdrAnalysis = addBoolean(
            QStringLiteral("Analyze center 75% of picture for HDR"),
            QStringLiteral("hdr_peak_analysis_picture_only"));
        pictureOnlyHdrAnalysis->setToolTip(QStringLiteral(
            "When a current trusted active-picture rectangle is available, "
            "always use its middle 75% of height and full available width for "
            "libplacebo's HDR peak and average-luminance analysis. This fixed "
            "inset rejects subtitles and OSDs that cross from a black bar into "
            "the picture. Invalid or stale geometry falls back to full-frame analysis."));
        auto* subtitleHold = addText(QStringLiteral("Subtitle hold"),
            QStringLiteral("subtitle_hold_seconds"), QStringLiteral("ms"), 1000.0);
        subtitleHold->setValidator(new QIntValidator(250, 30000, subtitleHold));
        subtitleHold->setToolTip(QStringLiteral(
            "Must be between 250 and 30000 ms. The minimum spans the "
            "renderer's scheduled subtitle-analysis cadence."));
        addText(QStringLiteral("Subtitle engage drift"), QStringLiteral("subtitle_engage_drift_ms"), QStringLiteral("ms"));
        addText(QStringLiteral("Subtitle release drift"), QStringLiteral("subtitle_release_drift_ms"), QStringLiteral("ms"));
        addText(QStringLiteral("Subtitle padding"), QStringLiteral("subtitle_padding_pixels"), QStringLiteral("pixels"));
        auto* subtitleTargetBuffer = addText(
            QStringLiteral("Subtitle target buffer"),
            QStringLiteral("subtitle_target_buffer_pixels"),
            QStringLiteral("pixels"));
        subtitleTargetBuffer->setToolTip(QStringLiteral(
            "Must be between 0 and 50 pixels. Adds outward reserve to an "
            "accepted subtitle target so small later extent changes do not "
            "start another movement."));
		}
    }
    else
    {
        addText(QStringLiteral("MaxCLL"), QStringLiteral("max_cll"), QStringLiteral("nits"));
        addText(QStringLiteral("MaxFALL"), QStringLiteral("max_fall"), QStringLiteral("nits"));
        addText(QStringLiteral("Mastering minimum"), QStringLiteral("mastering_min_luminance"), QStringLiteral("nits"));
        addText(QStringLiteral("Mastering maximum"), QStringLiteral("mastering_max_luminance"), QStringLiteral("nits"));
    }
    detailLayout->addWidget(profileFields);
    detailLayout->addStretch();

    const auto updateQueuePolicyPresentation = [queuePolicy, queuePolicySummary,
        profileFields]()
    {
        if (!queuePolicy) return;
        const QString selectedId = queuePolicy->currentData().toString();
        if (selectedId.isEmpty())
        {
            queuePolicySummary->setText(QStringLiteral(
                "Custom queue values are shown in Advanced."));
            if (auto* advanced = profileFields->findChild<QToolButton*>(
                QStringLiteral("rendererSection.queueAdvanced")); advanced &&
                !advanced->isChecked())
                advanced->setChecked(true);
        }
        else
        {
            for (const QueuePolicy& policy : queuePolicies)
                if (selectedId == QString::fromLatin1(policy.id))
                {
                    queuePolicySummary->setText(QString::fromLatin1(policy.summary));
                    break;
                }
        }
    };
    const auto updateQueuePolicyFromValues = [fields, queuePolicy,
        updateQueuePolicyPresentation]()
    {
        if (!queuePolicy) return;
        const auto queueValue = [fields](const QString& key) -> int
        {
            for (const Field& field : *fields)
                if (field.key == key && field.kind == Field::Integer)
                    return qobject_cast<QSpinBox*>(field.widget)->value();
            return -1;
        };
        QString policyId;
        for (const QueuePolicy& policy : queuePolicies)
            if (queueValue(QStringLiteral("queue_size")) == policy.capacity &&
                queueValue(QStringLiteral("lead_frames")) == policy.lead &&
                queueValue(QStringLiteral("target_frames")) == policy.target &&
                queueValue(QStringLiteral("startup_preroll_frames")) == 0 &&
                queueValue(QStringLiteral("active_picture_lookahead_frames")) == 0 &&
                queueValue(QStringLiteral("reset_after_render_restart_seconds")) == 5 &&
                queueValue(QStringLiteral("reset_queue_too_large_percent")) == 75)
            {
                policyId = QString::fromLatin1(policy.id);
                break;
            }
        const QSignalBlocker blocker(queuePolicy);
        queuePolicy->setCurrentIndex(std::max(0, queuePolicy->findData(policyId)));
        updateQueuePolicyPresentation();
    };

    auto loadDetails = [this, state, fields, selectedTitle, name, shortcut, rule, ruleField, useRule, remove, up, down, list,
        profileFields, sectionPrefix, anamorphicEnabled, anamorphicValue,
        deprecatedViewportAlias, queuePolicy, updateQueuePolicyFromValues](QListWidgetItem* current)
    {
        state->loading = true;
        state->section = current ? current->data(Qt::UserRole).toString() : QString();
        const bool available = !state->section.isEmpty();
        name->setEnabled(available);
        shortcut->setEnabled(available);
        useRule->setEnabled(available);
        rule->setEnabled(available && useRule->isChecked());
        profileFields->setEnabled(available);
        remove->setEnabled(available && list->count() > 1);
        up->setEnabled(available && list->currentRow() > 0);
        down->setEnabled(available && list->currentRow() + 1 < list->count());
        if (!available)
        {
            selectedTitle->setText(QStringLiteral("Add a profile to configure it"));
            name->clear();
            shortcut->clear();
            useRule->setChecked(false);
            ruleField->setVisible(false);
            state->loading = false;
            return;
        }
        const QString section = state->section;
        const QString display = current->data(Qt::UserRole + 1).toString();
        selectedTitle->setText(current->data(ActiveProfileRole).toBool() ?
            QStringLiteral("%1 (Active)").arg(display) :
            QStringLiteral("%1 (Not active)").arg(display));
        name->setText(display);
        shortcut->setText(canonicalShortcutText(
            value(section, QStringLiteral("shortcut"))));
        const QString expression = value(section, QStringLiteral("when"));
        rule->setPlainText(expression);
        useRule->setChecked(!expression.isEmpty());
        ruleField->setVisible(!expression.isEmpty());
        auto fallback = [this, sectionPrefix](const QString& key) -> QString
        {
            if (sectionPrefix == QStringLiteral("queue"))
            {
                if (key == QStringLiteral("queue_size")) return QStringLiteral("32");
                if (key == QStringLiteral("lead_frames")) return QStringLiteral("1");
                if (key == QStringLiteral("target_frames")) return QStringLiteral("4");
                if (key == QStringLiteral("active_picture_lookahead_frames")) return QStringLiteral("0");
                if (key == QStringLiteral("startup_preroll_frames")) return QStringLiteral("0");
                if (key == QStringLiteral("steady_reserve_frames")) return QStringLiteral("4");
                if (key == QStringLiteral("reset_after_render_restart_seconds")) return QStringLiteral("5");
                if (key == QStringLiteral("reset_queue_too_large_percent")) return QStringLiteral("75");
            }
            if (sectionPrefix == QStringLiteral("vprenderer.viewport"))
            {
                if (key == QStringLiteral("screen_aspect")) return QStringLiteral("16:9");
                if (key == QStringLiteral("vertical_alignment")) return QStringLiteral("center");
            }
            if (sectionPrefix == QStringLiteral("vprenderer.zoom"))
            {
                if (key == QStringLiteral("subtitle_hold_seconds")) return QStringLiteral("2");
                if (key == QStringLiteral("subtitle_engage_drift_ms")) return QStringLiteral("0");
                if (key == QStringLiteral("subtitle_release_drift_ms")) return QStringLiteral("0");
                if (key == QStringLiteral("subtitle_padding_pixels")) return QStringLiteral("20");
                if (key == QStringLiteral("subtitle_target_buffer_pixels")) return QStringLiteral("10");
            }
            if (sectionPrefix == QStringLiteral("lldv"))
            {
                QString enabled = value(QStringLiteral("general"), QStringLiteral("newlldv"));
                if (enabled.isEmpty()) enabled = value(QStringLiteral("general"), QStringLiteral("new_lldv"));
                if (enabled.isEmpty()) enabled = value(QStringLiteral("command_line"), QStringLiteral("newlldv"));
                if (enabled.isEmpty()) enabled = value(QStringLiteral("command_line"), QStringLiteral("new_lldv"));
                const QString normalized = enabled.trimmed().toLower();
                const bool modern = normalized == QStringLiteral("true") || normalized == QStringLiteral("yes") ||
                    normalized == QStringLiteral("on") || normalized == QStringLiteral("1");
                if (key == QStringLiteral("max_cll")) return QStringLiteral("1000");
                if (key == QStringLiteral("max_fall")) return modern ? QStringLiteral("401") : QStringLiteral("1000");
                if (key == QStringLiteral("mastering_min_luminance")) return modern ? QStringLiteral("0.001") : QStringLiteral("0.0001");
                if (key == QStringLiteral("mastering_max_luminance")) return modern ? QStringLiteral("4000") : QStringLiteral("1000");
            }
            if (sectionPrefix == QStringLiteral("vprenderer"))
            {
                if (key == QStringLiteral("quality")) return QStringLiteral("high");
                if (key == QStringLiteral("output_path_profile")) return QStringLiteral("legacy");
                if (key == QStringLiteral("sdr_target_primaries")) return QStringLiteral("REC709");
                if (key == QStringLiteral("sdr_target_nits")) return QStringLiteral("203");
                if (key == QStringLiteral("sdr_black_nits") ||
                    key == QStringLiteral("contrast_recovery") ||
                    key == QStringLiteral("lut_reference_nits")) return QStringLiteral("Auto");
                if (key == QStringLiteral("deband_strength")) return QStringLiteral("AUTO");
                if (key == QStringLiteral("lut")) return {};
                if (key == QStringLiteral("report_bt2020_to_display") ||
                    key == QStringLiteral("output_diagnostics") ||
                    key == QStringLiteral("diagnostic_disable_shader_cache") ||
                    key == QStringLiteral("diagnostic_disable_compute") ||
                    key == QStringLiteral("diagnostic_force_8bit_sdr_swapchain") ||
                    key == QStringLiteral("diagnostic_allow_limited_g22") ||
                    key == QStringLiteral("diagnostic_allow_full_g22") ||
                    key == QStringLiteral("diagnostic_vp_owned_dxgi_presenter")) return QStringLiteral("false");
                return QStringLiteral("AUTO");
            }
            return {};
        };
        const bool defaultProfile = list->currentRow() == 0;
        const auto profileValue = [this, deprecatedViewportAlias, sectionPrefix](
            const QString& profileSection, const QString& key) -> QString
        {
            QString configured = value(profileSection, key);
            if (configured.isEmpty() && sectionPrefix == QStringLiteral("vprenderer") &&
                key == QStringLiteral("deband_strength"))
            {
                const QString legacy = value(profileSection, QStringLiteral("deband")).trimmed();
                if (legacy.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0 ||
                    legacy.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
                    configured = QStringLiteral("default");
                else if (legacy.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0 ||
                    legacy.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0)
                    configured = QStringLiteral("off");
                else if (legacy.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
                    configured = QStringLiteral("AUTO");
            }
            const QString alias = deprecatedViewportAlias(key);
            if (configured.isEmpty() && !alias.isEmpty())
                configured = value(profileSection, alias);
            return configured;
        };
        for (const Field& field : *fields)
        {
            QString raw = profileValue(section, field.key);
			const bool scalingDownscaler =
				sectionPrefix == QStringLiteral("vprenderer.scaling") &&
				field.key == QStringLiteral("downscaler");
			if (scalingDownscaler && !raw.isEmpty())
			{
				const QString normalized = raw.trimmed().toLower();
				static const QStringList supported = {
					QStringLiteral("auto"), QStringLiteral("box"),
					QStringLiteral("hermite"), QStringLiteral("bilinear"),
					QStringLiteral("bicubic"), QStringLiteral("gaussian"),
					QStringLiteral("catmull_rom"), QStringLiteral("mitchell"),
					QStringLiteral("lanczos"), QStringLiteral("gpu")
				};
				if (!supported.contains(normalized))
				{
					// Removed legacy values must not reappear as synthetic combo-box
					// entries. Treat them as omitted/Auto and persist their removal
					// the next time the user saves this configuration.
					document_->RemoveKnown(section.toStdString(),
						field.key.toStdString().c_str());
					raw.clear();
					markDirty();
				}
			}
			const bool viewportAspectLimit =
				sectionPrefix == QStringLiteral("vprenderer.zoom") &&
				(field.key == QStringLiteral("crop_narrower_content_aspect_limit") ||
				 field.key == QStringLiteral("crop_wider_content_aspect_limit"));
			if (viewportAspectLimit && !raw.isEmpty())
			{
				AspectRatio ignored;
				std::string parseError;
				if (!AspectRatioParser::Parse(raw.toStdString(), 1.0, 4.0,
					ignored, parseError))
				{
					// Optional limits are best-effort. Clean up any legacy or
					// malformed value on the next save rather than carrying it on.
					document_->RemoveKnown(section.toStdString(),
						field.key.toStdString().c_str());
					raw.clear();
				}
			}
            const bool defaultOnlyRendererField = sectionPrefix == QStringLiteral("vprenderer") &&
                (field.key == QStringLiteral("output_diagnostics") ||
                 field.key == QStringLiteral("diagnostic_disable_shader_cache"));
            const bool defaultOnlyField = defaultOnlyRendererField;
            QString configured = raw;
			if (configured.isEmpty() && list->count() > 0)
                configured = defaultProfile ? fallback(field.key) :
                    profileValue(list->item(0)->data(Qt::UserRole).toString(), field.key);
            if (configured.isEmpty()) configured = fallback(field.key);
            if (defaultOnlyField && !defaultProfile && list->count() > 0)
                configured = value(list->item(0)->data(Qt::UserRole).toString(), field.key, fallback(field.key));
            field.widget->setEnabled(!defaultOnlyField || defaultProfile);
			field.widget->setProperty("inherited", raw.isEmpty() && !defaultProfile);
			field.widget->setToolTip(raw.isEmpty() ?
                (defaultProfile ?
                    QStringLiteral("Using the built-in default. Editing creates an explicit setting.") :
                    QStringLiteral("Inherited from the default profile. Editing creates an override.")) :
                QString());
            field.widget->style()->unpolish(field.widget);
            field.widget->style()->polish(field.widget);
            if (field.kind == Field::Text)
            {
                QString displayed = configured.compare(
                    QStringLiteral("AUTO"), Qt::CaseInsensitive) == 0 ?
                    QStringLiteral("Auto") : configured;
                if (field.displayScale != 1.0)
                {
                    bool ok = false;
                    const double stored = configured.toDouble(&ok);
                    if (ok)
                        displayed = QString::number(stored * field.displayScale,
                            'g', 12);
                }
                qobject_cast<QLineEdit*>(field.widget)->setText(displayed);
            }
            else if (field.kind == Field::Integer)
                qobject_cast<QSpinBox*>(field.widget)->setValue(configured.toInt());
            else if (field.kind == Field::Boolean)
            {
                auto* check = qobject_cast<QCheckBox*>(field.widget);
                check->setChecked(configured.compare(
                    QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            }
            else
            {
                auto* combo = qobject_cast<QComboBox*>(field.widget);
                const int autoIndex = combo->findData(QStringLiteral("AUTO"),
                    Qt::UserRole, Qt::MatchFixedString);
                // A VP Renderer root profile already has an explicit quality
                // policy. Where libplacebo offers AUTO, it is the meaningful
                // default: an additional empty "Use default" choice is both
                // redundant and ambiguous. Keep that omission available only
                // to named profiles, where it means inherit this root value.
                const bool rootRendererExplicitDefault = defaultProfile &&
                    sectionPrefix == QStringLiteral("vprenderer") &&
                    (field.key == QStringLiteral("quality") ||
                     field.key == QStringLiteral("sdr_target_primaries"));
                const bool hasUnspecifiedChoice = combo->count() > 0 &&
                    combo->itemData(0).toString().isEmpty();
                const bool hideUnspecifiedChoice = rootRendererExplicitDefault ||
                    (defaultProfile && sectionPrefix == QStringLiteral("vprenderer") &&
                        autoIndex >= 0);
                if (auto* view = qobject_cast<QListView*>(combo->view()))
                    view->setRowHidden(0, hasUnspecifiedChoice &&
                        hideUnspecifiedChoice);
                const QString inheritedDisplay = friendlyChoiceLabel(configured);
                if (hasUnspecifiedChoice)
                {
                    if (raw.isEmpty() && !configured.isEmpty())
                        combo->setItemText(0, defaultProfile ?
                            QStringLiteral("Default: %1").arg(inheritedDisplay) :
                            QStringLiteral("Inherited: %1").arg(inheritedDisplay));
                    else
                        combo->setItemText(0, defaultProfile ?
                            QStringLiteral("Use default") : QStringLiteral("Use inherited value"));
                }
                const int effectiveIndex = autoIndex >= 0 ? autoIndex :
                    combo->findData(configured, Qt::UserRole, Qt::MatchFixedString);
                int index = raw.isEmpty() ?
                    (hideUnspecifiedChoice || !hasUnspecifiedChoice ? effectiveIndex : 0) :
                    (configured.isEmpty() && hasUnspecifiedChoice ? 0 :
                        combo->findData(configured, Qt::UserRole, Qt::MatchFixedString));
                if (index < 0)
                    for (int candidate = 0; candidate < combo->count(); ++candidate)
                        if (combo->itemData(candidate).toString().compare(
                            configured, Qt::CaseInsensitive) == 0)
                        { index = candidate; break; }
                if (index < 0 && sectionPrefix == QStringLiteral("vprenderer") &&
                    field.key == QStringLiteral("lut") && !raw.isEmpty())
                {
                    // LUTs are deliberately selected only from VP's luts
                    // directory. A removed file must not linger as a fake
                    // selector item; clear it in the pending document and
                    // let the user save that removal explicitly.
                    document_->RemoveKnown(section.toStdString(), "lut");
                    markDirty();
                    index = 0;
                }
                else if (index < 0)
                {
                    combo->addItem(friendlyChoiceLabel(configured), configured);
                    index = combo->count() - 1;
                }
                combo->setCurrentIndex(index);
            }
        }
        if (anamorphicEnabled && anamorphicValue)
        {
            const QString configured = value(section, QStringLiteral("anamorphic_scale"));
            anamorphicEnabled->setChecked(!configured.isEmpty());
            anamorphicValue->setEnabled(!configured.isEmpty());
            if (configured.isEmpty()) anamorphicValue->setText(QStringLiteral("1:1"));
        }
        if (queuePolicy)
            updateQueuePolicyFromValues();
        state->loading = false;
        if (sectionPrefix == QStringLiteral("vprenderer") ||
			sectionPrefix == QStringLiteral("vprenderer.scaling") ||
            sectionPrefix == QStringLiteral("vprenderer.color") ||
            sectionPrefix == QStringLiteral("vprenderer.output"))
            refreshRendererAutoStatus();
    };

    if (queuePolicy)
    {
        auto* advanced = profileFields->findChild<QToolButton*>(
            QStringLiteral("rendererSection.queueAdvanced"));
        connect(advanced, &QToolButton::toggled, this, [queuePolicy, advanced](bool open)
        {
            if (!open && queuePolicy->currentData().toString().isEmpty())
                advanced->setChecked(true);
        });
        connect(queuePolicy, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, queuePolicy, state, fields, updateQueuePolicyPresentation](int index)
        {
            if (state->loading || state->section.isEmpty() || !document_ || index < 0) return;
            const QString selectedId = queuePolicy->itemData(index).toString();
            if (selectedId.isEmpty())
            {
                updateQueuePolicyPresentation();
                return;
            }
            const QueuePolicy* selected = nullptr;
            for (const QueuePolicy& policy : queuePolicies)
                if (selectedId == QString::fromLatin1(policy.id)) { selected = &policy; break; }
            if (!selected) return;
            const std::string section = state->section.toStdString();
            const auto setValue = [this, &section](const char* key, int value)
            {
                document_->SetKnown(section, key, std::to_string(value));
            };
            setValue("queue_size", selected->capacity);
            setValue("lead_frames", selected->lead);
            setValue("startup_preroll_frames", 0);
            setValue("target_frames", selected->target);
            setValue("active_picture_lookahead_frames", 0);
            setValue("reset_after_render_restart_seconds", 5);
            setValue("reset_queue_too_large_percent", 75);
            for (const Field& field : *fields)
                if (field.kind == Field::Integer)
                {
                    int value = 0;
                    if (field.key == QStringLiteral("queue_size")) value = selected->capacity;
                    else if (field.key == QStringLiteral("lead_frames")) value = selected->lead;
                    else if (field.key == QStringLiteral("target_frames")) value = selected->target;
                    else if (field.key == QStringLiteral("reset_after_render_restart_seconds")) value = 5;
                    else if (field.key == QStringLiteral("reset_queue_too_large_percent")) value = 75;
                    QSignalBlocker blocker(field.widget);
                    qobject_cast<QSpinBox*>(field.widget)->setValue(value);
                }
            markDirty();
            updateQueuePolicyPresentation();
        });
        for (const Field& field : *fields)
            if (field.kind == Field::Integer)
                connect(qobject_cast<QSpinBox*>(field.widget),
                    qOverload<int>(&QSpinBox::valueChanged), this,
                    [state, updateQueuePolicyFromValues](int)
                {
                    if (state->loading) return;
                    updateQueuePolicyFromValues();
                });
    }

    std::function<void(const QString&)> refresh = [this, list, sectionPrefix, loadDetails](const QString& wanted)
    {
        list->clear();
        const QStringList sections = profileSections(sectionPrefix);
        int selected = 0;
        for (int index = 0; index < sections.size(); ++index)
        {
            const QString configuredLabel = value(sections[index], QStringLiteral("label"));
            const QString shown = configuredLabel.isEmpty() ? displayName(sections[index], sectionPrefix) : configuredLabel;
            auto* item = new QListWidgetItem(index == 0 ? shown + QStringLiteral("  (Default)") : shown);
            item->setData(Qt::UserRole, sections[index]);
            item->setData(Qt::UserRole + 1, shown);
            item->setSizeHint(QSize(0, 36));
            list->addItem(item);
            if (sections[index] == wanted) selected = index;
        }
        if (list->count() == 0) loadDetails(nullptr);
        else list->setCurrentRow(selected);
        refreshActiveProfileIndicators();
    };

    connect(list, &QListWidget::currentItemChanged, this, [loadDetails](QListWidgetItem* current) { loadDetails(current); });
    connect(shortcut, &QLineEdit::textChanged, this, [this, state](const QString& text)
    {
        if (state->loading || state->section.isEmpty()) return;
        if (text.trimmed().isEmpty()) document_->RemoveKnown(state->section.toStdString(), "shortcut");
        else document_->SetKnown(state->section.toStdString(), "shortcut", text.toLocal8Bit().constData());
        markDirty();
    });
    connect(shortcut, &QLineEdit::editingFinished, this, [this, state, shortcut]
    {
        if (state->loading || state->section.isEmpty() || shortcut->text().trimmed().isEmpty()) return;
        std::string canonical;
        if (!RendererProfileConfig::CanonicalizeKeyChord(shortcut->text().toStdString(), canonical))
        {
            QMessageBox::warning(this, QStringLiteral("Shortcut key"),
                QStringLiteral("Use one key with optional Ctrl, Alt, or Shift modifiers (for example Ctrl+F2)."));
            return;
        }
        const QString normalized = QString::fromStdString(canonical);
        if (normalized == shortcut->text()) return;
        shortcut->setText(normalized);
        document_->SetKnown(state->section.toStdString(), "shortcut", canonical);
        markDirty();
    });
    connect(useRule, &QCheckBox::toggled, this, [this, state, rule, ruleField](bool enabled)
    {
        ruleField->setVisible(enabled);
        rule->setEnabled(enabled);
        if (state->loading || state->section.isEmpty()) return;
        if (!enabled) document_->RemoveKnown(state->section.toStdString(), "when");
        markDirty();
    });
    connect(rule, &QPlainTextEdit::textChanged, this, [this, state, rule]
    {
        if (state->loading || state->section.isEmpty()) return;
        QString text = rule->toPlainText().trimmed();
        text.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
        if (text.isEmpty()) document_->RemoveKnown(state->section.toStdString(), "when");
        else document_->SetKnown(state->section.toStdString(), "when", text.toLocal8Bit().constData());
        markDirty();
    });
    connect(name, &QLineEdit::editingFinished, this, [this, state, name, sectionPrefix, refresh]
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        const QString requested = name->text().trimmed();
        const QString configuredLabel = value(state->section, QStringLiteral("label"));
        const QString currentName = configuredLabel.isEmpty() ?
            displayName(state->section, sectionPrefix) : configuredLabel;
        if (requested.isEmpty())
        {
            name->setText(currentName);
            QMessageBox::warning(this, QStringLiteral("Profile name"),
                QStringLiteral("Profile names cannot be empty."));
            return;
        }
        for (const QString& existingSection : profileSections(sectionPrefix))
        {
            if (existingSection.compare(state->section, Qt::CaseInsensitive) == 0) continue;
            const QString existingLabel = value(existingSection, QStringLiteral("label"));
            const QString existingName = existingLabel.isEmpty() ? displayName(existingSection, sectionPrefix) : existingLabel;
            if (existingName.compare(requested, Qt::CaseInsensitive) == 0)
            {
                name->setText(currentName);
                QMessageBox::warning(this, QStringLiteral("Profile name"), QStringLiteral("Profile names must be unique."));
                return;
            }
        }
        QString identifier = profileIdentifier(requested);
        if (identifier.size() > 64)
        {
            name->setText(currentName);
            QMessageBox::warning(this, QStringLiteral("Profile name"), QStringLiteral("Profile names must encode to 64 characters or fewer."));
            return;
        }
        QString renamed = sectionPrefix + u'.' + identifier;
        if (renamed == state->section) return;
        const QStringList existing = profileSections(sectionPrefix);
        if (existing.contains(renamed, Qt::CaseInsensitive) &&
            renamed.compare(state->section, Qt::CaseInsensitive) != 0)
        {
            QMessageBox::warning(this, QStringLiteral("Profile name"), QStringLiteral("Another profile already uses that name."));
            name->setText(currentName);
            return;
        }
        const QString old = state->section;
        if (!document_->RenameSection(old.toStdString(), renamed.toStdString())) return;
        if (sectionPrefix == QStringLiteral("vprenderer.viewport") ||
            sectionPrefix == QStringLiteral("vprenderer.zoom"))
            document_->SetKnown(renamed.toStdString(), "label", requested.toLocal8Bit().constData());
        state->section = renamed;
        markDirty();
        refresh(renamed);
    });
    connect(add, &QPushButton::clicked, this, [this, sectionPrefix, refresh]
    {
        if (!document_) return;
        const QStringList existing = profileSections(sectionPrefix);
        QString section;
        for (int number = 1; number <= 1000; ++number)
        {
            const QString requested = QStringLiteral("New %1").arg(number);
            bool nameInUse = false;
            for (const QString& existingSection : existing)
            {
                const QString configuredLabel = value(existingSection, QStringLiteral("label"));
                const QString existingName = configuredLabel.isEmpty() ?
                    displayName(existingSection, sectionPrefix) : configuredLabel;
                if (existingName.compare(requested, Qt::CaseInsensitive) == 0)
                {
                    nameInUse = true;
                    break;
                }
            }
            const QString candidate = sectionPrefix + u'.' + profileIdentifier(requested);
            if (!nameInUse && !existing.contains(candidate, Qt::CaseInsensitive))
            {
                section = candidate;
                break;
            }
        }
        if (section.isEmpty())
        {
            setStatus(QStringLiteral(
                "Cannot add profile: New 1 through New 1000 are already in use."), true);
            return;
        }
        if (!document_->AddSection(section.toStdString())) return;
        markDirty();
        refresh(section);
    });
    auto transferDefaultOnlySettings = [this, sectionPrefix](const QString& from, const QString& to)
    {
        if (from.isEmpty() || to.isEmpty() || from == to) return;
        QStringList keys;
        for (const QString& keyText : keys)
        {
            const std::string key = keyText.toStdString();
            const QString configured = value(from, keyText);
            if (configured.isEmpty()) continue;
            document_->RemoveKnown(from.toStdString(), key.c_str());
            document_->SetKnown(to.toStdString(), key.c_str(), configured.toLocal8Bit().constData());
        }
    };
    auto normalizeRootForOrdering = [this, state, sectionPrefix, refresh]()
    {
        const QStringList sections = profileSections(sectionPrefix);
        if (!sections.contains(sectionPrefix, Qt::CaseInsensitive)) return;
        QString identifier = QStringLiteral("profile_1");
        QString renamed = sectionPrefix + u'.' + identifier;
        int suffix = 2;
        while (sections.contains(renamed, Qt::CaseInsensitive))
            renamed = sectionPrefix + QStringLiteral(".profile_%1").arg(suffix++);
        const QString wanted = state->section.compare(sectionPrefix, Qt::CaseInsensitive) == 0 ? renamed : state->section;
        if (!document_->RenameSection(sectionPrefix.toStdString(), renamed.toStdString())) return;
        state->section = wanted;
        refresh(wanted);
    };
    connect(remove, &QPushButton::clicked, this, [this, state, refresh, list, transferDefaultOnlySettings]
    {
        if (state->section.isEmpty() || QMessageBox::question(this, QStringLiteral("Remove profile"),
            QStringLiteral("Remove this profile?"), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) return;
        if (list->currentRow() == 0 && list->count() > 1)
            transferDefaultOnlySettings(state->section, list->item(1)->data(Qt::UserRole).toString());
        document_->RemoveSection(state->section.toStdString());
        markDirty();
        refresh({});
    });
    auto move = [this, state, list, refresh, transferDefaultOnlySettings, normalizeRootForOrdering](int delta)
    {
        normalizeRootForOrdering();
        const int source = list->currentRow();
        const int target = source + delta;
        if (source < 0 || target < 0 || target >= list->count()) return;
        const QString moved = state->section;
        const QString other = list->item(target)->data(Qt::UserRole).toString();
        const QString previousDefault = list->item(0)->data(Qt::UserRole).toString();
        const bool changed = delta < 0 ? document_->MoveSectionBefore(moved.toStdString(), other.toStdString()) :
            document_->MoveSectionAfter(moved.toStdString(), other.toStdString());
        if (!changed) return;
        const QString newDefault = target == 0 ? moved : (source == 0 ? other : previousDefault);
        transferDefaultOnlySettings(previousDefault, newDefault);
        markDirty();
        refresh(moved);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    auto reordering = std::make_shared<bool>(false);
    connect(list->model(), &QAbstractItemModel::rowsMoved, this,
        [this, state, list, refresh, sectionPrefix, transferDefaultOnlySettings, reordering]
        (const QModelIndex&, int sourceStart, int, const QModelIndex&, int destinationRow)
    {
        if (*reordering || list->count() < 2) return;
        *reordering = true;
        QStringList ordered;
        for (int index = 0; index < list->count(); ++index)
            ordered.push_back(list->item(index)->data(Qt::UserRole).toString());
        const int movedIndex = destinationRow > sourceStart ? destinationRow - 1 : destinationRow;
        QString oldDefault = sourceStart == 0 ? ordered.value(movedIndex) :
            (movedIndex == 0 ? ordered.value(1) : ordered.value(0));
        const int rootIndex = ordered.indexOf(sectionPrefix);
        if (rootIndex >= 0)
        {
            const QStringList existing = profileSections(sectionPrefix);
            QString renamed = sectionPrefix + QStringLiteral(".profile_1");
            int suffix = 2;
            while (existing.contains(renamed, Qt::CaseInsensitive))
                renamed = sectionPrefix + QStringLiteral(".profile_%1").arg(suffix++);
            if (document_->RenameSection(sectionPrefix.toStdString(), renamed.toStdString()))
            {
                ordered[rootIndex] = renamed;
                if (oldDefault == sectionPrefix) oldDefault = renamed;
            }
        }
        for (int index = 1; index < ordered.size(); ++index)
            document_->MoveSectionAfter(ordered[index].toStdString(), ordered[index - 1].toStdString());
        const QString newDefault = ordered.value(0);
        transferDefaultOnlySettings(oldDefault, newDefault);
        const QString selected = ordered.value(qBound(0, movedIndex, ordered.size() - 1));
        markDirty();
        refresh(selected);
        *reordering = false;
    });
    refresh({});

    splitter->addWidget(createCard(QStringLiteral("Profiles"),
        QStringLiteral("The first profile in the list is the default. Add, remove, or reorder profiles here."), listContent));
    splitter->addWidget(createCard(QStringLiteral("Profile details"),
        QStringLiteral("Shortcut and rule come first, followed by profile-specific settings."), detail));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 310, 620 });
    return createPage(title, description, splitter);
}

QWidget* ConfigEditorWindow::createQueuePage()
{
    return createProfilePage(QStringLiteral("Queue"),
        QStringLiteral("Configure ordered queue profiles. Choose a buffering policy for a complete baseline, or tune its advanced values."),
        QStringLiteral("queue"));
}

QWidget* ConfigEditorWindow::createRendererPage()
{
    return createProfilePage(QStringLiteral("Rendering"),
        QStringLiteral("Configure ordered rendering profiles. The first profile in the list is the default."), QStringLiteral("vprenderer"));
}

QWidget* ConfigEditorWindow::createScalingPage()
{
	return createProfilePage(QStringLiteral("Scaling"),
		QStringLiteral("Configure ordered scaling profiles. The first profile in the list is the default."),
		QStringLiteral("vprenderer.scaling"));
}

QWidget* ConfigEditorWindow::createColorConfigPage()
{
    return createProfilePage(QStringLiteral("Color Config"),
        QStringLiteral("Select independent Rec.709 or BT.2020 configurations before rendering. Each Color Config owns display calibration and SDR source-transfer handling; Screen Config selection remains independent."),
        QStringLiteral("vprenderer.color"));
}

void ConfigEditorWindow::refreshShaderCacheStatus()
{
    const QDir rendererDirectory(QFileInfo(configPath_).absoluteDir().filePath(
        QStringLiteral("vprenderer")));
    const QFileInfo cache(rendererDirectory.filePath(
        QStringLiteral("VideoProcessorShaderCache.bin")));
    if (shaderCacheStatus_)
    {
        shaderCacheStatus_->setText(cache.exists() ?
            QStringLiteral("Persistent cache: %1 MB · Updated %2")
                .arg(cache.size() / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(cache.lastModified().toString(QStringLiteral("MMM d, h:mm AP"))) :
            QStringLiteral("Persistent cache: Not created yet"));
    }
}

QWidget* ConfigEditorWindow::createOutputPage()
{
    return createProfilePage(QStringLiteral("Output"),
        QStringLiteral("Configure output transport and diagnostic profiles separately from live Color Config selections."),
        QStringLiteral("vprenderer.output"));
}

QWidget* ConfigEditorWindow::createInputProcessingPage(const QString& title,
    const QString& description, const QString& section)
{
    auto* input = new QWidget;
    auto* inputForm = new QFormLayout(input);
    inputForm->setContentsMargins(0, 0, 0, 0);
    inputForm->setVerticalSpacing(8);
    inputForm->addRow(QStringLiteral("Video conversion"), bindChoiceField(section,
        QStringLiteral("video_conversion"),
        { QString(), QStringLiteral("NONE"), QStringLiteral("V210_TO_P010") },
        { QStringLiteral("Inherited / not set"), QStringLiteral("Disabled"),
          QStringLiteral("V210 to P010") }));
    inputForm->addRow(QStringLiteral("Container color space"), bindChoiceField(section,
        QStringLiteral("container_colorspace"),
        { QString(), QStringLiteral("BT2020"), QStringLiteral("P3_D65"), QStringLiteral("P3_DCI"),
          QStringLiteral("P3_D60"), QStringLiteral("REC709"), QStringLiteral("REC601_525"),
          QStringLiteral("REC601_625") },
        { QStringLiteral("Inherited / not set") }));
    inputForm->addRow(QStringLiteral("HDR color space"), bindChoiceField(section,
        QStringLiteral("hdr_colorspace"),
        { QString(), QStringLiteral("FOLLOW_INPUT"), QStringLiteral("FOLLOW_INPUT_LLDV"),
          QStringLiteral("FOLLOW_CONTAINER"), QStringLiteral("BT2020"), QStringLiteral("P3"),
          QStringLiteral("REC709") },
        { QStringLiteral("Inherited / not set"), QStringLiteral("Follow input"),
          QStringLiteral("Follow input (LLDV)"), QStringLiteral("Follow container"),
          QStringLiteral("BT.2020"), QStringLiteral("P3"), QStringLiteral("Rec. 709") }));
    inputForm->addRow(QStringLiteral("HDR luminance"), bindChoiceField(section,
        QStringLiteral("hdr_luminance"),
        { QString(), QStringLiteral("FOLLOW_INPUT"), QStringLiteral("FOLLOW_INPUT_LLDV"),
          QStringLiteral("HDR_LUMINANCE_USER") },
        { QStringLiteral("Inherited / not set"), QStringLiteral("Follow input"),
          QStringLiteral("Follow input (LLDV)"), QStringLiteral("User values") }));

    auto* cards = new ResponsiveCardGrid;
    cards->addCard(createCard(QStringLiteral("Input processing"),
        QStringLiteral("Choose per-renderer overrides, or inherit the General defaults. Changes apply after restart."),
        input));
    return createPage(title, description, cards);
}

QWidget* ConfigEditorWindow::createDirectShowPage()
{
    const QString section = QStringLiteral("directshow");
    auto* cards = new ResponsiveCardGrid;

    auto* timing = new QWidget;
    auto* timingForm = new QFormLayout(timing);
    timingForm->setContentsMargins(0, 0, 0, 0);
    timingForm->setVerticalSpacing(8);
    timingForm->addRow(QStringLiteral("Start/stop method"), bindChoiceField(section,
        QStringLiteral("renderer_start_stop_time_method"),
        { QStringLiteral("CLOCK_SMART"), QStringLiteral("CLOCK_SMART2"),
          QStringLiteral("RATIONAL_RATIONAL"), QStringLiteral("CLOCK_RATIONAL"),
          QStringLiteral("CLOCK_THEO"), QStringLiteral("CLOCK_CLOCK"),
          QStringLiteral("THEO_THEO"), QStringLiteral("CLOCK_NONE"),
          QStringLiteral("THEO_NONE"), QStringLiteral("NONE") },
        { QStringLiteral("Clock Smart"), QStringLiteral("Clock Smart 2"),
          QStringLiteral("Rational / Rational"), QStringLiteral("Clock / Rational"),
          QStringLiteral("Clock / Theoretical"), QStringLiteral("Clock / Clock"),
          QStringLiteral("Theoretical / Theoretical"), QStringLiteral("Clock / None"),
          QStringLiteral("Theoretical / None"), QStringLiteral("None") }));
    QString configuredFrameOffset = value(section, QStringLiteral("frame_offset"));
    if (configuredFrameOffset.isEmpty())
        configuredFrameOffset = value(QStringLiteral("general"), QStringLiteral("frame_offset"), QStringLiteral("AUTO"));
    const bool automaticFrameOffset = configuredFrameOffset.isEmpty() ||
        configuredFrameOffset.compare(QStringLiteral("AUTO"), Qt::CaseInsensitive) == 0;
    auto* frameOffsetRow = new QWidget;
    auto* frameOffsetLayout = new QHBoxLayout(frameOffsetRow);
    frameOffsetLayout->setContentsMargins(0, 0, 0, 0);
    frameOffsetLayout->setSpacing(12);
    auto* frameOffsetAuto = new QCheckBox(QStringLiteral("Auto"));
    frameOffsetAuto->setObjectName(QStringLiteral("config.directshow.frame_offset.auto"));
    frameOffsetAuto->setChecked(automaticFrameOffset);
    auto* frameOffsetValue = new QSpinBox;
    frameOffsetValue->setObjectName(QStringLiteral("config.directshow.frame_offset.value"));
    frameOffsetValue->setAccessibleName(QStringLiteral("Frame offset in milliseconds"));
    frameOffsetValue->setRange(0, INT_MAX);
    frameOffsetValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    frameOffsetValue->setMaximumWidth(180);
    frameOffsetValue->setValue(automaticFrameOffset ? 0 : configuredFrameOffset.toInt());
    frameOffsetValue->setEnabled(!automaticFrameOffset);
    frameOffsetValue->setFixedWidth(180);
    frameOffsetLayout->addWidget(frameOffsetValue);
    frameOffsetLayout->addWidget(frameOffsetAuto);
    frameOffsetLayout->addStretch();
    connect(frameOffsetAuto, &QCheckBox::toggled, this,
        [this, frameOffsetValue](bool automatic)
    {
        if (!document_) return;
        frameOffsetValue->setEnabled(!automatic);
        document_->RemoveKnown("general", "frame_offset");
        document_->SetKnown("directshow", "frame_offset",
            automatic ? "AUTO" : std::to_string(frameOffsetValue->value()));
        markDirty();
    });
    connect(frameOffsetValue, qOverload<int>(&QSpinBox::valueChanged), this,
        [this, frameOffsetAuto](int offset)
    {
        if (!document_ || frameOffsetAuto->isChecked()) return;
        document_->RemoveKnown("general", "frame_offset");
        document_->SetKnown("directshow", "frame_offset", std::to_string(offset));
        markDirty();
    });
    timingForm->addRow(QStringLiteral("Frame offset (ms)"), frameOffsetRow);

    auto* overrides = new QWidget;
    auto* overrideForm = new QFormLayout(overrides);
    overrideForm->setContentsMargins(0, 0, 0, 0);
    overrideForm->setVerticalSpacing(8);
    overrideForm->addRow(QStringLiteral("Nominal range"), bindChoiceField(section, QStringLiteral("renderer_nominal_range"),
        { QStringLiteral("AUTO"), QStringLiteral("FULL"), QStringLiteral("LIMITED"), QStringLiteral("SMALL") }));
    overrideForm->addRow(QStringLiteral("Transfer function"), bindChoiceField(section, QStringLiteral("renderer_transfer_function"),
        { QStringLiteral("AUTO"), QStringLiteral("PQ"), QStringLiteral("REC709"), QStringLiteral("BT2020_CONST"),
          QStringLiteral("GAMMA_1.8"), QStringLiteral("GAMMA_2.0"), QStringLiteral("GAMMA_2.2"), QStringLiteral("GAMMA_2.6"),
          QStringLiteral("GAMMA_2.8"), QStringLiteral("LINEAR_RGB"), QStringLiteral("204M"), QStringLiteral("8BIT_GAMMA_2.2"),
          QStringLiteral("LOG_100_1"), QStringLiteral("LOG_316_1"), QStringLiteral("BT2020"), QStringLiteral("HYBRID_LOG_GAMMA") }));
    overrideForm->addRow(QStringLiteral("Transfer matrix"), bindChoiceField(section, QStringLiteral("renderer_transfer_matrix"),
        { QStringLiteral("AUTO"), QStringLiteral("BT2020_10"), QStringLiteral("BT2020_12"), QStringLiteral("BT709"),
          QStringLiteral("BT601"), QStringLiteral("240M"), QStringLiteral("FCC"), QStringLiteral("YCGCO") }));
    overrideForm->addRow(QStringLiteral("Primaries"), bindChoiceField(section, QStringLiteral("renderer_primaries"),
        { QStringLiteral("AUTO"), QStringLiteral("BT2020"), QStringLiteral("DCI-P3"), QStringLiteral("BT709"),
          QStringLiteral("NTSC_SYSM"), QStringLiteral("NTSC_SYSBG"), QStringLiteral("CIE1931_ZYX"), QStringLiteral("ACES") }));

    cards->addCard(createCard(QStringLiteral("Timing"),
        QStringLiteral("Timing controls used only by DirectShow renderers."), timing));
    cards->addCard(createCard(QStringLiteral("Renderer overrides"),
        QStringLiteral("DirectShow color overrides. Auto lets the renderer decide."), overrides));
    return createPage(QStringLiteral("General"),
        QStringLiteral("Configure timing and color overrides used only by DirectShow renderers."), cards);
}

QWidget* ConfigEditorWindow::createViewportPage()
{
	return createProfilePage(QStringLiteral("Screen"),
		QStringLiteral("Configure VP Renderer screen geometry and selection. The first profile in the list is the default."),
		QStringLiteral("vprenderer.viewport"));
}

QWidget* ConfigEditorWindow::createZoomPage()
{
	return createProfilePage(QStringLiteral("Zoom"),
		QStringLiteral("Configure independent crop/fill and subtitle profiles. Zoom shortcuts never change the selected Screen geometry."),
		QStringLiteral("vprenderer.zoom"));
}

QWidget* ConfigEditorWindow::createLldvPage()
{
    const QStringList sections = profileSections(QStringLiteral("lldv"));
    const QString section = sections.isEmpty() ? QStringLiteral("lldv") : sections.front();

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    layout->addWidget(helpLabel(QStringLiteral(
        "Uses these values for recognized HDFury LLDV input and sends the same effective metadata to DirectShow and VP Renderer.")));
    layout->addWidget(bindCheckField(QStringLiteral("Alternate LLDV detection"),
        QStringLiteral("general"), QStringLiteral("newlldv")));
    layout->addWidget(helpLabel(QStringLiteral(
        "Uses VP's newer LLDV metadata detection heuristic. Restart VideoProcessor after changing this setting.")));

    if (sections.size() > 1)
    {
        auto* preserved = helpLabel(QStringLiteral(
            "This file contains %1 additional LLDV section(s). The editor uses the first section and preserves the others unchanged.")
            .arg(sections.size() - 1));
        preserved->setProperty("warning", true);
        preserved->setToolTip(sections.mid(1).join(u'\n'));
        layout->addWidget(preserved);
    }

    QString enabled = value(QStringLiteral("general"), QStringLiteral("newlldv"));
    if (enabled.isEmpty()) enabled = value(QStringLiteral("general"), QStringLiteral("new_lldv"));
    if (enabled.isEmpty()) enabled = value(QStringLiteral("command_line"), QStringLiteral("newlldv"));
    if (enabled.isEmpty()) enabled = value(QStringLiteral("command_line"), QStringLiteral("new_lldv"));
    const QString normalized = enabled.trimmed().toLower();
    const bool modern = normalized == QStringLiteral("true") || normalized == QStringLiteral("yes") ||
        normalized == QStringLiteral("on") || normalized == QStringLiteral("1");

    auto* formContent = new QWidget;
    auto* form = new QFormLayout(formContent);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(12);
    const auto addValue = [this, form, section](const QString& label, const QString& key,
        const QString& fallback)
    {
        auto* edit = bindTextField(section, key, fallback);
        edit->setMaximumWidth(320);
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(edit);
        rowLayout->addWidget(new QLabel(QStringLiteral("nits")));
        rowLayout->addStretch();
        form->addRow(label, row);
    };
    addValue(QStringLiteral("MaxCLL"), QStringLiteral("max_cll"), QStringLiteral("1000"));
    addValue(QStringLiteral("MaxFALL"), QStringLiteral("max_fall"),
        modern ? QStringLiteral("401") : QStringLiteral("1000"));
    addValue(QStringLiteral("Mastering minimum"), QStringLiteral("mastering_min_luminance"),
        modern ? QStringLiteral("0.001") : QStringLiteral("0.0001"));
    addValue(QStringLiteral("Mastering maximum"), QStringLiteral("mastering_max_luminance"),
        modern ? QStringLiteral("4000") : QStringLiteral("1000"));
    layout->addWidget(formContent);

    return createPage(QStringLiteral("LLDV"),
        QStringLiteral("Configure the LLDV metadata values shared by every renderer."),
        createCard(QStringLiteral("LLDV metadata"),
            QStringLiteral("LLDV is one shared input-metadata policy. Config saves it in the singleton [lldv] section."),
            content));
}

QWidget* ConfigEditorWindow::createNlsShadersPage()
{
    auto state = std::make_shared<ShaderEditorState>();
    const QString root = QStringLiteral("shader.nls");

    auto* splitter = new ResponsiveSplitter;

    auto* selection = new QWidget;
    auto* selectionLayout = new QVBoxLayout(selection);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    selectionLayout->setSpacing(10);
    auto* list = new QListWidget;
    list->setAccessibleName(QStringLiteral("NLS modes"));
    list->setAccessibleDescription(
        QStringLiteral("Shipped nonlinear-stretch modes plus the special Off option."));
    list->setObjectName(QStringLiteral("config.shader.nls.modes"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDragDropOverwriteMode(false);
    list->setItemDelegate(new ProfileStateItemDelegate(list));
    activeProfileLists_.push_back({ list, QStringLiteral("shader.nls") });
    auto* orderActions = new QHBoxLayout;
    auto* moveUp = new QPushButton(QStringLiteral("Move up"));
    auto* moveDown = new QPushButton(QStringLiteral("Move down"));
    moveUp->setObjectName(QStringLiteral("config.shader.nls.move_up"));
    moveDown->setObjectName(QStringLiteral("config.shader.nls.move_down"));
    moveUp->setAccessibleDescription(
        QStringLiteral("Move the selected NLS mode earlier in the selection order."));
    moveDown->setAccessibleDescription(
        QStringLiteral("Move the selected NLS mode later in the selection order."));
    orderActions->addWidget(moveUp);
    orderActions->addWidget(moveDown);
    orderActions->addStretch();
    selectionLayout->addLayout(orderActions);
    selectionLayout->addWidget(list, 1);

    QStringList manualSections;
    QStringList nlsSections;
    bool editableSingleGroup = true;
    if (document_)
    {
        const QString groupType = value(root, QStringLiteral("type")).trimmed();
        const bool rootIsEffect = !value(root, QStringLiteral("shader_type")).trimmed().isEmpty() ||
            !value(root, QStringLiteral("hlsl_file")).trimmed().isEmpty() ||
            !value(root, QStringLiteral("glsl_file")).trimmed().isEmpty();
        editableSingleGroup = !rootIsEffect &&
            (groupType.isEmpty() || groupType.compare(QStringLiteral("single"), Qt::CaseInsensitive) == 0);
        for (const std::string& section : document_->SectionNamesWithPrefix("shader"))
        {
            const QString name = QString::fromStdString(section);
            const bool directNlsChild = name.startsWith(root + u'.', Qt::CaseInsensitive) &&
                !name.mid(root.size() + 1).contains(u'.');
            const bool standardSection = name.compare(
                QStringLiteral("shader.standard"), Qt::CaseInsensitive) == 0 ||
                name.startsWith(QStringLiteral("shader.standard."), Qt::CaseInsensitive);
            if (editableSingleGroup && directNlsChild &&
                value(name, QStringLiteral("shader_type")).compare(
                    QStringLiteral("nls"), Qt::CaseInsensitive) == 0)
                nlsSections.push_back(name);
            else if (name.compare(root, Qt::CaseInsensitive) != 0 && !standardSection)
                manualSections.push_back(name);
        }
    }
    if (!editableSingleGroup)
    {
        auto* warning = helpLabel(QStringLiteral(
            "This NLS group uses advanced multi/effect semantics and is preserved for manual editing."));
        warning->setProperty("warning", true);
        selectionLayout->addWidget(warning);
    }
    if (!manualSections.isEmpty())
    {
        auto* custom = helpLabel(QStringLiteral(
            "%1 custom/manual shader section(s) are preserved unchanged. "
            "Use manual configuration for their program-specific parameters.")
            .arg(manualSections.size()));
        custom->setProperty("warning", true);
        custom->setToolTip(manualSections.join(u'\n'));
        selectionLayout->addWidget(custom);
    }

    auto* details = new QWidget;
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("NLS selection"));
    title->setProperty("cardTitle", true);
    detailsLayout->addWidget(title);

    auto* shortcut = new QLineEdit;
    shortcut->setObjectName(QStringLiteral("config.shader.nls.shortcut"));
    shortcut->setMaximumWidth(280);
    detailsLayout->addWidget(fieldWithHelp(QStringLiteral("Shortcut key"), shortcut,
        QStringLiteral("Optional. Selects this NLS mode; Off disables NLS.")));
    auto* useRule = new QCheckBox(QStringLiteral("Select automatically with a rule"));
    useRule->setObjectName(QStringLiteral("config.shader.nls.use_rule"));
    detailsLayout->addWidget(useRule);
    auto* rule = new QPlainTextEdit;
    rule->setObjectName(QStringLiteral("config.shader.nls.when"));
    rule->setMinimumHeight(72);
    rule->setMaximumHeight(96);
    rule->setMaximumBlockCount(3);
    rule->setPlaceholderText(QStringLiteral("${eotf} == \"HDR\""));
    auto* ruleField = fieldWithHelp(QStringLiteral("Rule"), rule,
        QStringLiteral("Optional source-condition rule. Shortcut and rule are alternatives; either may select this mode."));
    detailsLayout->addWidget(ruleField);

    auto* normalFields = new QWidget;
    auto* normalLayout = new QVBoxLayout(normalFields);
    normalLayout->setContentsMargins(0, 4, 0, 0);
    normalLayout->setSpacing(10);
    auto* label = new QLineEdit;
    label->setObjectName(QStringLiteral("config.shader.nls.label"));
    normalLayout->addWidget(fieldWithHelp(QStringLiteral("Display name"), label,
        QStringLiteral("The friendly name shown for this included shader mode.")));
    detailsLayout->addWidget(normalFields);

    auto* advanced = new QWidget;
    auto* advancedForm = new QFormLayout(advanced);
    advancedForm->setContentsMargins(0, 0, 0, 0);
    advancedForm->setHorizontalSpacing(18);
    advancedForm->setVerticalSpacing(10);
    auto* stage = new QComboBox;
    stage->setObjectName(QStringLiteral("config.shader.nls.stage"));
    stage->setAccessibleName(QStringLiteral("Shader stage"));
    stage->addItem(QStringLiteral("Before resize"), QStringLiteral("pre_resize"));
    stage->addItem(QStringLiteral("After resize"), QStringLiteral("post_resize"));
    auto* hlsl = new QLineEdit;
    auto* glsl = new QLineEdit;
    hlsl->setObjectName(QStringLiteral("config.shader.nls.hlsl_file"));
    glsl->setObjectName(QStringLiteral("config.shader.nls.glsl_file"));
    hlsl->setAccessibleName(QStringLiteral("DirectShow shader file"));
    glsl->setAccessibleName(QStringLiteral("VP Renderer shader file"));
    hlsl->setPlaceholderText(QStringLiteral("Leave blank when unsupported"));
    glsl->setPlaceholderText(QStringLiteral("Leave blank when unsupported"));
    advancedForm->addRow(QStringLiteral("Stage"), stage);
    advancedForm->addRow(QStringLiteral("DirectShow shader file"), hlsl);
    advancedForm->addRow(QStringLiteral("VP Renderer shader file"), glsl);
    detailsLayout->addWidget(advanced);

    const ShaderParameterControls parameterEditor = addShaderParameterControls(
        this, this, detailsLayout, state, [this] { return document_.get(); },
        [this] { markDirty(); }, QStringLiteral("config.shader.nls"),
        QStringLiteral("Shader parameters"), QStringLiteral("shader mode"));
    auto* offExplanation = helpLabel(QStringLiteral(
        "NLS is disabled. Choose an included mode on the left to configure its stretch behavior."));
    offExplanation->setProperty("emptyState", true);
    detailsLayout->addWidget(offExplanation);
    detailsLayout->addStretch();

    auto setText = [this, state](const char* key, const QString& text)
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        const QByteArray keyBytes(key);
        if (text.trimmed().isEmpty())
            document_->RemoveKnown(state->section.toStdString(), keyBytes.constData());
        else
        {
            document_->AddSection(state->section.toStdString());
            document_->SetKnown(state->section.toStdString(), keyBytes.constData(),
                text.trimmed().toLocal8Bit().constData());
        }
        markDirty();
    };
    const std::vector<std::pair<QLineEdit*, const char*>> textFields = {
        { label, "label" },
        { hlsl, "hlsl_file" }, { glsl, "glsl_file" }
    };
    for (const auto& field : textFields)
        connect(field.first, &QLineEdit::textChanged, this,
            [setText, field](const QString& text) { setText(field.second, text); });
    connect(shortcut, &QLineEdit::textChanged, this,
        [setText](const QString& text) { setText("shortcut", text); });
    connect(shortcut, &QLineEdit::editingFinished, this, [this, state, shortcut]
    {
        if (state->loading || state->section.isEmpty() || shortcut->text().trimmed().isEmpty()) return;
        std::string canonical;
        if (!RendererProfileConfig::CanonicalizeKeyChord(shortcut->text().toStdString(), canonical))
        {
            QMessageBox::warning(this, QStringLiteral("Shortcut key"),
                QStringLiteral("Use one key with optional Ctrl, Alt, or Shift modifiers."));
            return;
        }
        const QString normalized = QString::fromStdString(canonical);
        if (normalized != shortcut->text()) shortcut->setText(normalized);
    });
    auto setChoice = [this, state](const char* key, QComboBox* combo)
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        const QString stored = combo->currentData().toString();
        document_->SetKnown(state->section.toStdString(), key,
            (stored.isEmpty() ? combo->currentText().trimmed() : stored)
                .toLocal8Bit().constData());
        markDirty();
    };
    connect(stage, &QComboBox::currentTextChanged, this,
        [setChoice, stage](const QString&) { setChoice("stage", stage); });
    connect(useRule, &QCheckBox::toggled, this, [this, state, rule, ruleField](bool enabled)
    {
        ruleField->setVisible(enabled);
        rule->setEnabled(enabled);
        if (state->loading || state->section.isEmpty() || !document_) return;
        if (!enabled) document_->RemoveKnown(state->section.toStdString(), "when");
        markDirty();
    });
    connect(rule, &QPlainTextEdit::textChanged, this, [this, state, rule]
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        QString expression = rule->toPlainText().trimmed();
        expression.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
        if (expression.isEmpty()) document_->RemoveKnown(state->section.toStdString(), "when");
        else
        {
            document_->AddSection(state->section.toStdString());
            document_->SetKnown(state->section.toStdString(), "when", expression.toLocal8Bit().constData());
        }
        markDirty();
    });

    auto load = [this, state, root, title, shortcut, useRule, rule, ruleField,
        normalFields, parameterEditor, advanced, offExplanation, label,
        stage, hlsl, glsl]
        (QListWidgetItem* item)
    {
        state->loading = true;
        state->section = item ? item->data(Qt::UserRole).toString() : QString();
        const bool available = !state->section.isEmpty();
        const bool member = available && state->section.compare(root, Qt::CaseInsensitive) != 0;
        title->setText(item ? item->text() : QStringLiteral("No NLS modes are configured"));
        shortcut->setEnabled(available);
        useRule->setEnabled(available);
        normalFields->setVisible(member);
        advanced->setVisible(member);
        offExplanation->setVisible(available && !member);
        if (!available)
        {
            shortcut->clear();
            useRule->setChecked(false);
            ruleField->setVisible(false);
            parameterEditor.reload();
            parameterEditor.toggle->setVisible(false);
            state->loading = false;
            return;
        }
        shortcut->setText(canonicalShortcutText(
            value(state->section, QStringLiteral("shortcut"))));
        const QString expression = value(state->section, QStringLiteral("when"));
        useRule->setChecked(!expression.isEmpty());
        rule->setPlainText(expression);
        ruleField->setVisible(!expression.isEmpty());
        rule->setEnabled(!expression.isEmpty());
        label->setText(value(state->section, QStringLiteral("label")));
        auto loadCombo = [this, state](QComboBox* combo, const QString& key, const QString& fallback)
        {
            const QString configured = value(state->section, key, fallback);
            int index = combo->findData(configured, Qt::UserRole, Qt::MatchFixedString);
            if (index < 0)
                for (int candidate = 0; candidate < combo->count(); ++candidate)
                    if (combo->itemData(candidate).toString().compare(
                        configured, Qt::CaseInsensitive) == 0)
                    { index = candidate; break; }
            if (index < 0) { combo->addItem(friendlyChoiceLabel(configured), configured); index = combo->count() - 1; }
            combo->setCurrentIndex(index);
        };
        loadCombo(stage, QStringLiteral("stage"), QStringLiteral("pre_resize"));
        parameterEditor.reload();
        parameterEditor.toggle->setVisible(member);
        parameterEditor.fields->setVisible(member && parameterEditor.toggle->isChecked());
        // A logical shader member may implement only one renderer. Preserve an
        // omitted backend file as blank rather than silently substituting NLS.
        hlsl->setText(value(state->section, QStringLiteral("hlsl_file")));
        glsl->setText(value(state->section, QStringLiteral("glsl_file")));
        state->loading = false;
    };
    connect(list, &QListWidget::currentItemChanged, this,
        [load](QListWidgetItem* current) { load(current); });

    {
        QStringList sections{ root };
        if (document_)
            sections.append(nlsSections);
        for (const QString& section : sections)
        {
            const bool off = section.compare(root, Qt::CaseInsensitive) == 0;
            QString shown = off ? QStringLiteral("Off") : value(section, QStringLiteral("label"));
            if (shown.isEmpty()) shown = displayName(section, root);
            auto* item = new QListWidgetItem(shown);
            item->setData(Qt::UserRole, section);
            item->setSizeHint(QSize(0, 34));
            if (off)
                item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsDropEnabled);
            list->addItem(item);
        }
    }
    // NLS is an exclusive group: list/file order determines which matching
    // mode wins. The special Off entry stays fixed; numeric shader `order`
    // remains a manual-only setting for custom multi-effect shader stacks.
    auto updateMoveActions = [list, moveUp, moveDown]
    {
        const int row = list->currentRow();
        moveUp->setEnabled(row > 1);
        moveDown->setEnabled(row > 0 && row < list->count() - 1);
    };
    auto persistModeOrder = [this, list, root]()
    {
        if (!document_ || list->count() < 2) return;
        bool changed = false;
        QString previous = root;
        for (int row = 1; row < list->count(); ++row)
        {
            const QString section = list->item(row)->data(Qt::UserRole).toString();
            if (section.isEmpty()) continue;
            changed = document_->MoveSectionAfter(section.toStdString(),
                previous.toStdString()) || changed;
            previous = section;
        }
        if (changed) markDirty();
    };
    auto move = [list, persistModeOrder, updateMoveActions](int delta)
    {
        const int source = list->currentRow();
        const int target = source + delta;
        if (source <= 0 || target <= 0 || target >= list->count()) return;
        QListWidgetItem* item = list->takeItem(source);
        list->insertItem(target, item);
        list->setCurrentRow(target);
        persistModeOrder();
        updateMoveActions();
    };
    connect(moveUp, &QPushButton::clicked, this, [move] { move(-1); });
    connect(moveDown, &QPushButton::clicked, this, [move] { move(1); });
    connect(list, &QListWidget::currentRowChanged, this,
        [updateMoveActions](int) { updateMoveActions(); });
    auto modeReordering = std::make_shared<bool>(false);
    connect(list->model(), &QAbstractItemModel::rowsMoved, this,
        [persistModeOrder, updateMoveActions, modeReordering]
        (const QModelIndex&, int, int, const QModelIndex&, int)
    {
        if (*modeReordering) return;
        *modeReordering = true;
        persistModeOrder();
        updateMoveActions();
        *modeReordering = false;
    });
    if (list->count() > 0) list->setCurrentRow(0);
    else load(nullptr);
    refreshActiveProfileIndicators();
    updateMoveActions();

    splitter->addWidget(createCard(QStringLiteral("NLS modes"),
        QStringLiteral("Off disables NLS. Reorder included modes by dragging or using the buttons; the first matching mode wins."), selection));
    splitter->addWidget(createCard(QStringLiteral("Mode details"),
        QStringLiteral("Required shader setup is always visible. Custom shader parameters can be edited separately."), details));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 310, 620 });
    return createPage(QStringLiteral("NLS"),
        QStringLiteral("Configure included nonlinear stretch (NLS) modes without rewriting custom shader sections."), splitter);
}

QWidget* ConfigEditorWindow::createShadersSetupPage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* statusContent = new QWidget;
    auto* statusLayout = new QVBoxLayout(statusContent);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(8);
    shaderCacheStatus_ = helpLabel(QStringLiteral("Checking shader cache..."));
    shaderCacheStatus_->setObjectName(QStringLiteral("config.shader.cache.status"));
    statusLayout->addWidget(shaderCacheStatus_);
    layout->addWidget(createCard(QStringLiteral("Shader cache"),
        QStringLiteral("VP Renderer compiles shaders when they are required and stores them here."),
        statusContent));

    auto* controls = new QWidget;
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);
    auto* buttons = new QHBoxLayout;
    auto* clearCache = new QPushButton(QStringLiteral("Clear shader cache"));
    clearCache->setObjectName(QStringLiteral("config.shader.cache.clear"));
    clearCache->setProperty("danger", true);
    clearCache->setAccessibleDescription(QStringLiteral(
        "Clear the persistent VP Renderer shader cache at the next renderer start."));
    buttons->addWidget(clearCache);
    buttons->addStretch();
    controlsLayout->addLayout(buttons);
    controlsLayout->addWidget(helpLabel(QStringLiteral(
        "The cache is rebuilt automatically as VP Renderer uses shaders.")));
    layout->addWidget(createCard(QStringLiteral("Cache maintenance"),
        QStringLiteral("Remove the persistent VP Renderer shader cache."),
        controls));
    layout->addStretch();

    connect(clearCache, &QPushButton::clicked, this, [this]
    {
        const QDir configurationDirectory = QFileInfo(configPath_).absoluteDir();
        const QString rendererDirectory = configurationDirectory.filePath(
            QStringLiteral("vprenderer"));
        if (!QDir().mkpath(rendererDirectory))
        {
            setStatus(QStringLiteral("Could not access the VP Renderer cache folder."), true);
            return;
        }
        const QDir cacheDirectory(rendererDirectory);
        const QString cachePath = cacheDirectory.filePath(
            QStringLiteral("VideoProcessorShaderCache.bin"));
        const QString temporaryPath = cachePath + QStringLiteral(".tmp");
        const QString requestPath = cacheDirectory.filePath(
            QStringLiteral("VideoProcessorShaderCache.clear"));
        QFile request(requestPath);
        if (!request.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            request.write("clear\n") < 0)
        {
            setStatus(QStringLiteral("Could not request a shader cache clear."), true);
            return;
        }
        request.close();
        QFile::remove(cachePath);
        QFile::remove(temporaryPath);
        setStatus(QStringLiteral(
            "Shader cache will be cleared at the next VP Renderer start."));
        refreshShaderCacheStatus();
    });
    refreshShaderCacheStatus();
    return createPage(QStringLiteral("Shaders"),
        QStringLiteral("Inspect or clear VP Renderer shader storage."), content);
}

QWidget* ConfigEditorWindow::createStandardShadersPage()
{
    auto state = std::make_shared<ShaderEditorState>();
    const QString root = QStringLiteral("shader.standard");

    auto* splitter = new ResponsiveSplitter;
    auto* selection = new QWidget;
    auto* selectionLayout = new QVBoxLayout(selection);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    selectionLayout->setSpacing(10);
    auto* list = new QListWidget;
    list->setObjectName(QStringLiteral("config.shader.standard.items"));
    list->setAccessibleName(QStringLiteral("Standard shaders"));
    list->setAccessibleDescription(QStringLiteral(
        "Included ordinary shader configurations. They remain inactive until given a shortcut or rule."));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setItemDelegate(new ProfileStateItemDelegate(list));
    activeProfileLists_.push_back({ list, root });
    auto* addShader = new QPushButton(QStringLiteral("+ Add shader"));
    addShader->setObjectName(QStringLiteral("config.shader.standard.add"));
    addShader->setAccessibleDescription(
        QStringLiteral("Add a new standard custom shader configuration."));
    auto* removeShader = new QPushButton(QStringLiteral("Remove selected"));
    removeShader->setObjectName(QStringLiteral("config.shader.standard.remove"));
    removeShader->setProperty("danger", true);
    removeShader->setAccessibleDescription(
        QStringLiteral("Remove the selected standard shader configuration."));
    auto* shaderButtons = new QHBoxLayout;
    shaderButtons->setContentsMargins(0, 0, 0, 0);
    shaderButtons->addWidget(addShader);
    shaderButtons->addWidget(removeShader);
    shaderButtons->addStretch();
    selectionLayout->addLayout(shaderButtons);
    selectionLayout->addWidget(list, 1);
    selectionLayout->addWidget(helpLabel(QStringLiteral(
        "These entries are independent effects. By default none has a shortcut or rule, so none is active.")));
    auto* details = new QWidget;
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("Standard shader details"));
    title->setProperty("cardTitle", true);
    detailsLayout->addWidget(title);
    auto* shortcut = new QLineEdit;
    shortcut->setObjectName(QStringLiteral("config.shader.standard.shortcut"));
    shortcut->setMaximumWidth(280);
    detailsLayout->addWidget(fieldWithHelp(QStringLiteral("Shortcut key"), shortcut,
        QStringLiteral("Optional. Leave blank to keep this shader inactive.")));
    auto* useRule = new QCheckBox(QStringLiteral("Select automatically with a rule"));
    useRule->setObjectName(QStringLiteral("config.shader.standard.use_rule"));
    detailsLayout->addWidget(useRule);
    auto* rule = new QPlainTextEdit;
    rule->setObjectName(QStringLiteral("config.shader.standard.when"));
    rule->setMinimumHeight(72);
    rule->setMaximumHeight(96);
    rule->setMaximumBlockCount(3);
    rule->setPlaceholderText(QStringLiteral("${eotf} == \"HDR\""));
    auto* ruleField = fieldWithHelp(QStringLiteral("Rule"), rule,
        QStringLiteral("Optional source-condition rule. Shortcut and rule are alternatives; either may select this shader."));
    detailsLayout->addWidget(ruleField);

    auto* label = new QLineEdit;
    label->setObjectName(QStringLiteral("config.shader.standard.label"));
    detailsLayout->addWidget(fieldWithHelp(QStringLiteral("Display name"), label,
        QStringLiteral("The friendly name shown for this standard shader.")));
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    auto* stage = new QComboBox;
    stage->setObjectName(QStringLiteral("config.shader.standard.stage"));
    stage->setAccessibleName(QStringLiteral("Shader stage"));
    stage->addItem(QStringLiteral("Before resize"), QStringLiteral("pre_resize"));
    stage->addItem(QStringLiteral("After resize"), QStringLiteral("post_resize"));
    auto* hlsl = new QLineEdit;
    auto* glsl = new QLineEdit;
    hlsl->setObjectName(QStringLiteral("config.shader.standard.hlsl_file"));
    glsl->setObjectName(QStringLiteral("config.shader.standard.glsl_file"));
    hlsl->setAccessibleName(QStringLiteral("DirectShow shader file"));
    glsl->setAccessibleName(QStringLiteral("VP Renderer shader file"));
    hlsl->setPlaceholderText(QStringLiteral("Leave blank when unsupported"));
    glsl->setPlaceholderText(QStringLiteral("Leave blank when unsupported"));
    form->addRow(QStringLiteral("Stage"), stage);
    form->addRow(QStringLiteral("DirectShow shader file"), hlsl);
    form->addRow(QStringLiteral("VP Renderer shader file"), glsl);
    detailsLayout->addLayout(form);
    auto* unsupported = helpLabel(QStringLiteral(
        "A blank backend file is intentional: that renderer does not support this shader implementation. "
        "Other existing shader sections are preserved unchanged."));
    detailsLayout->addWidget(unsupported);
    const ShaderParameterControls parameterEditor = addShaderParameterControls(
        this, this, detailsLayout, state, [this] { return document_.get(); },
        [this] { markDirty(); }, QStringLiteral("config.shader.standard"),
        QStringLiteral("Standard shader parameters"), QStringLiteral("standard shader"));
    auto* empty = helpLabel(QStringLiteral(
        "No standard shader configurations are present. The shipped example configuration includes optional defaults."));
    empty->setProperty("emptyState", true);
    detailsLayout->addWidget(empty);
    detailsLayout->addStretch();

    auto setText = [this, state](const char* key, const QString& text)
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        const QByteArray keyBytes(key);
        if (text.trimmed().isEmpty())
            document_->RemoveKnown(state->section.toStdString(), keyBytes.constData());
        else
        {
            document_->AddSection(state->section.toStdString());
            document_->SetKnown(state->section.toStdString(), keyBytes.constData(),
                text.trimmed().toLocal8Bit().constData());
        }
        markDirty();
    };
    for (const auto& field : std::vector<std::pair<QLineEdit*, const char*>>{
        { label, "label" }, { hlsl, "hlsl_file" }, { glsl, "glsl_file" } })
        connect(field.first, &QLineEdit::textChanged, this,
            [setText, field](const QString& text) { setText(field.second, text); });
    connect(shortcut, &QLineEdit::textChanged, this,
        [setText](const QString& text) { setText("shortcut", text); });
    connect(shortcut, &QLineEdit::editingFinished, this, [this, state, shortcut]
    {
        if (state->loading || state->section.isEmpty() || shortcut->text().trimmed().isEmpty()) return;
        std::string canonical;
        if (!RendererProfileConfig::CanonicalizeKeyChord(shortcut->text().toStdString(), canonical))
        {
            QMessageBox::warning(this, QStringLiteral("Shortcut key"),
                QStringLiteral("Use one key with optional Ctrl, Alt, or Shift modifiers."));
            return;
        }
        const QString normalized = QString::fromStdString(canonical);
        if (normalized != shortcut->text()) shortcut->setText(normalized);
    });
    connect(stage, &QComboBox::currentTextChanged, this, [this, state, stage](const QString&)
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        document_->SetKnown(state->section.toStdString(), "stage",
            stage->currentData().toString().toLocal8Bit().constData());
        markDirty();
    });
    connect(useRule, &QCheckBox::toggled, this, [this, state, rule, ruleField](bool enabled)
    {
        ruleField->setVisible(enabled);
        rule->setEnabled(enabled);
        if (state->loading || state->section.isEmpty() || !document_) return;
        if (!enabled) document_->RemoveKnown(state->section.toStdString(), "when");
        markDirty();
    });
    connect(rule, &QPlainTextEdit::textChanged, this, [this, state, rule]
    {
        if (state->loading || state->section.isEmpty() || !document_) return;
        QString expression = rule->toPlainText().trimmed();
        expression.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
        if (expression.isEmpty()) document_->RemoveKnown(state->section.toStdString(), "when");
        else
        {
            document_->AddSection(state->section.toStdString());
            document_->SetKnown(state->section.toStdString(), "when", expression.toLocal8Bit().constData());
        }
        markDirty();
    });

    connect(addShader, &QPushButton::clicked, this, [this, list, state, root]
    {
        if (!document_) return;
        bool accepted = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Add standard shader"),
            QStringLiteral("Shader name"), QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted || name.isEmpty()) return;
        QString member = name.toLower();
        member.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]+")),
            QStringLiteral("_"));
        member.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
        if (member.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("Standard shader"),
                QStringLiteral("Use a shader name containing at least one letter or number."));
            return;
        }
        if (member.front().isDigit()) member.prepend(QStringLiteral("shader_"));
        const QString section = root + u'.' + member;
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString().compare(
                section, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(this, QStringLiteral("Standard shader"),
                    QStringLiteral("A standard shader with that name already exists."));
                return;
            }
        document_->AddSection(root.toStdString());
        document_->SetKnown(root.toStdString(), "type", "multi");
        document_->AddSection(section.toStdString());
        document_->SetKnown(section.toStdString(), "shader_type", "custom");
        document_->SetKnown(section.toStdString(), "label", name.toLocal8Bit().constData());
        document_->SetKnown(section.toStdString(), "stage", "pre_resize");
        auto* item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, section);
        item->setSizeHint(QSize(0, 34));
        list->addItem(item);
        list->setCurrentItem(item);
        markDirty();
    });
    connect(removeShader, &QPushButton::clicked, this, [this, list, state]
    {
        QListWidgetItem* item = list->currentItem();
        if (!item || !document_) return;
        const QString section = item->data(Qt::UserRole).toString();
        if (section.isEmpty()) return;
        if (QMessageBox::question(this, QStringLiteral("Remove standard shader"),
            QStringLiteral("Remove '%1'? This does not delete either shader file.")
                .arg(item->text()),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) return;
        document_->RemoveSection(section.toStdString());
        const int row = list->row(item);
        delete list->takeItem(row);
        if (list->count() > 0) list->setCurrentRow(qMin(row, list->count() - 1));
        else state->section.clear();
        markDirty();
    });

    auto load = [this, state, title, shortcut, useRule, rule, ruleField, label,
        stage, hlsl, glsl, parameterEditor, empty, unsupported](QListWidgetItem* item)
    {
        state->loading = true;
        state->section = item ? item->data(Qt::UserRole).toString() : QString();
        const bool available = !state->section.isEmpty();
        title->setText(item ? item->text() : QStringLiteral("No standard shaders are configured"));
        const std::vector<QWidget*> controls = {
            shortcut, useRule, rule, label, stage, hlsl, glsl
        };
        for (QWidget* control : controls) control->setEnabled(available);
        unsupported->setVisible(available);
        empty->setVisible(!available);
        if (!available)
        {
            shortcut->clear();
            useRule->setChecked(false);
            rule->clear();
            ruleField->setVisible(false);
            label->clear();
            hlsl->clear();
            glsl->clear();
            parameterEditor.reload();
            state->loading = false;
            return;
        }
        shortcut->setText(canonicalShortcutText(value(state->section, QStringLiteral("shortcut"))));
        const QString expression = value(state->section, QStringLiteral("when"));
        useRule->setChecked(!expression.isEmpty());
        rule->setPlainText(expression);
        ruleField->setVisible(!expression.isEmpty());
        rule->setEnabled(!expression.isEmpty());
        label->setText(value(state->section, QStringLiteral("label")));
        const QString configuredStage = value(state->section, QStringLiteral("stage"), QStringLiteral("pre_resize"));
        int index = stage->findData(configuredStage, Qt::UserRole, Qt::MatchFixedString);
        if (index < 0) { stage->addItem(friendlyChoiceLabel(configuredStage), configuredStage); index = stage->count() - 1; }
        stage->setCurrentIndex(index);
        hlsl->setText(value(state->section, QStringLiteral("hlsl_file")));
        glsl->setText(value(state->section, QStringLiteral("glsl_file")));
        parameterEditor.reload();
        state->loading = false;
    };
    connect(list, &QListWidget::currentItemChanged, this,
        [load](QListWidgetItem* current) { load(current); });

    if (document_)
        for (const std::string& raw : document_->SectionNamesWithPrefix(root.toStdString()))
        {
            const QString section = QString::fromStdString(raw);
            const QString member = section.mid(root.size() + 1);
            if (member.isEmpty() || member.contains(u'.') ||
                value(section, QStringLiteral("shader_type")).compare(
                    QStringLiteral("custom"), Qt::CaseInsensitive) != 0)
                continue;
            QString shown = value(section, QStringLiteral("label"));
            if (shown.isEmpty()) shown = displayName(section, root);
            auto* item = new QListWidgetItem(shown);
            item->setData(Qt::UserRole, section);
            item->setSizeHint(QSize(0, 34));
            list->addItem(item);
        }
    if (list->count() > 0) list->setCurrentRow(0);
    else load(nullptr);
    refreshActiveProfileIndicators();

    splitter->addWidget(createCard(QStringLiteral("Standard shaders"),
        QStringLiteral("Optional included effects. They may be selected independently; no default shortcut is assigned."), selection));
    splitter->addWidget(createCard(QStringLiteral("Shader details"),
        QStringLiteral("Configure the selected standard shader without changing NLS modes or other manual shader sections."), details));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 310, 620 });
    return createPage(QStringLiteral("Standard"),
        QStringLiteral("Configure optional ordinary shader effects separately from NLS."), splitter);
}

QWidget* ConfigEditorWindow::createActionsPage()
{
    struct State { QString section; bool loading = false; };
    auto state = std::make_shared<State>();
    auto* splitter = new ResponsiveSplitter;

    auto* listContent = new QWidget;
    auto* listLayout = new QVBoxLayout(listContent);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    auto* add = new QPushButton(QStringLiteral("+ Add action"));
    add->setObjectName(QStringLiteral("config.actions.add"));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    remove->setProperty("danger", true);
    auto* list = new QListWidget;
    list->setObjectName(QStringLiteral("config.actions.items"));
    list->setAccessibleName(QStringLiteral("Configured actions"));
    list->setAccessibleDescription(QStringLiteral("Actions that VideoProcessor can run after selected events."));
    listLayout->addWidget(add);
    listLayout->addWidget(remove);
    listLayout->addWidget(list, 1);

    auto* detailStack = new QStackedWidget;
    detailStack->setObjectName(QStringLiteral("config.actions.details"));
    auto* emptyDetail = new QWidget;
    auto* emptyLayout = new QVBoxLayout(emptyDetail);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(6);
    auto* emptyTitle = new QLabel(QStringLiteral("No actions configured"));
    emptyTitle->setProperty("cardTitle", true);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(helpLabel(QStringLiteral(
        "Actions are optional. Add an action when you want VideoProcessor to run a command after an event.")));
    emptyLayout->addStretch();
    detailStack->addWidget(emptyDetail);

    auto* detail = new QWidget;
    auto* detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(12);
    auto* selectedTitle = new QLabel(QStringLiteral("Add an action to configure it"));
    selectedTitle->setProperty("cardTitle", true);
    detailLayout->addWidget(selectedTitle);

    auto* name = new QLineEdit;
    name->setObjectName(QStringLiteral("config.actions.name"));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Name"), name,
        QStringLiteral("Names must be unique. Spaces are supported.")));

    auto* enabled = new QCheckBox(QStringLiteral("Enable this action"));
    enabled->setObjectName(QStringLiteral("config.actions.enabled"));
    enabled->setAccessibleDescription(QStringLiteral(
        "Disabled actions are saved as drafts and ignored by VideoProcessor."));
    detailLayout->addWidget(enabled);
    detailLayout->addWidget(helpLabel(QStringLiteral(
        "Leave disabled while setting up an incomplete action. VP ignores disabled drafts.")));

    auto* rendererTarget = new QComboBox;
    actionRendererTarget_ = rendererTarget;
    rendererTarget->setObjectName(QStringLiteral("config.actions.renderer"));
    rendererTarget->setEditable(true);
    rendererTarget->setInsertPolicy(QComboBox::NoInsert);
    rendererTarget->addItem(QStringLiteral("VP Renderer (default)"), QStringLiteral("vprenderer"));
    rendererTarget->addItem(QStringLiteral("All renderers"), QStringLiteral("*"));
    const QStringList& renderers = allRenderers_;
    for (int index = 0; index < renderers.size(); ++index)
    {
        if (renderers[index].compare(QStringLiteral("VP Renderer"), Qt::CaseInsensitive) == 0 ||
            renderers[index].compare(QStringLiteral("VideoProcessor Renderer (Alpha)"), Qt::CaseInsensitive) == 0)
            continue;
        rendererTarget->addItem(QStringLiteral("%1 - %2").arg(index + 1).arg(renderers[index]), QString::number(index + 1));
        rendererTarget->setItemData(rendererTarget->count() - 1,
            renderers[index], kRendererNameRole);
    }
    rendererTarget->setCurrentIndex(0);
    rendererTarget->setEditText(rendererTarget->itemText(0));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Renderer target"), rendererTarget,
        QStringLiteral("Choose VP Renderer, a discovered renderer, or All renderers.")));

    auto* coalesceRole = new QLineEdit;
    coalesceRole->setObjectName(QStringLiteral("config.actions.coalesce_role"));
    coalesceRole->setPlaceholderText(QStringLiteral("Optional, e.g. Color"));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Group"), coalesceRole,
        QStringLiteral("Actions in the same group replace one another while waiting, so only the newest matching action runs. Leave blank to group this action by itself.")));

    auto* delaySeconds = new QSpinBox;
    delaySeconds->setObjectName(QStringLiteral("config.actions.delay_seconds"));
    delaySeconds->setRange(0, 30);
    delaySeconds->setSuffix(QStringLiteral(" seconds"));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Delay"), delaySeconds,
        QStringLiteral("Wait before launching this action. Use 0 for immediate actions; a later trigger in the same coalescing role replaces it while waiting.")));

    auto* events = new QListWidget;
    events->setObjectName(QStringLiteral("config.actions.on"));
    events->setAccessibleName(QStringLiteral("Action events"));
    events->setAccessibleDescription(QStringLiteral("Check one or more events that can run this action."));
    events->setMinimumHeight(170);
    struct ActionEventChoice
    {
        const char* token;
        const char* label;
        const char* description;
    };
    const ActionEventChoice supportedEvents[] = {
        { "state.committed", "Any VP state update", "Runs once after VP accepts a complete source and profile snapshot." },
        { "renderer.ready", "Renderer is ready", "Runs when the selected renderer reaches Ready, including after a rebuild." },
        { "profile.changed", "Any active profile changed", "Runs once when one or more effective profile selections change." },
        { "profile.display.changed", "VP Renderer profile changed", "Runs when the selected VP Renderer rendering profile changes." },
        { "profile.color.changed", "Color Config changed", "Runs when the selected Rec.709 or BT.2020 Color Config changes." },
        { "profile.viewport.changed", "Screen Config profile changed", "Runs when the selected VP Renderer Screen Config profile changes." },
        { "profile.zoom.changed", "Zoom profile changed", "Runs when the selected VP Renderer Zoom profile changes without changing Screen geometry." },
        { "profile.queue.changed", "Queue profile changed", "Runs when the selected Queue profile changes." },
        { "profile.lldv.changed", "LLDV configuration changed", "Runs when the effective LLDV configuration changes." },
        { "profile.input.changed", "VP Renderer input settings changed", "Runs when advanced VP Renderer input settings change." },
        { "profile.scaling.changed", "VP Renderer scaling settings changed", "Runs when advanced VP Renderer scaling settings change." },
        { "source.eotf.changed", "Source EOTF changed", "Runs when the stable source EOTF changes." },
        { "source.transfer.changed", "Source transfer changed", "Runs when the stable source transfer characteristic changes." },
        { "source.colorspace.changed", "Source color space changed", "Runs when the reported source color space changes." },
        { "source.primaries.changed", "Source primaries changed", "Runs when the reported source color primaries change." },
        { "source.format.changed", "Source pixel format changed", "Runs when the stable source pixel format changes." },
        { "source.resolution.changed", "Source resolution changed", "Runs when the stable source resolution changes." },
        { "source.width.changed", "Source width changed", "Runs when the stable source width changes." },
        { "source.height.changed", "Source height changed", "Runs when the stable source height changes." },
        { "source.scan.changed", "Source scan mode changed", "Runs when progressive/interlaced scan reporting changes." },
        { "source.interlaced.changed", "Source interlacing changed", "Runs when the stable interlaced state changes." },
        { "source.hdr_metadata.changed", "HDR metadata availability changed", "Runs when source HDR metadata becomes available or unavailable." },
        { "source.source_rate.changed", "Source frame rate changed", "Runs when the stable nominal source frame rate changes." },
        { "source.cadence.changed", "Source cadence changed", "Runs when VP's stable source cadence changes." },
        { "refresh.applied", "Refresh rate switch applied", "VP Renderer changed the Windows display refresh rate." },
        { "refresh.confirmed", "Refresh rate already matched", "VP Renderer confirmed that the display was already at the requested refresh rate." },
        { "refresh.restored", "Previous refresh rate restored", "VP Renderer restored the display refresh rate it found before playback." }
    };
    for (const ActionEventChoice& event : supportedEvents)
    {
        const QString token = QString::fromLatin1(event.token);
        auto* item = new QListWidgetItem(QString::fromLatin1(event.label), events);
        item->setData(Qt::UserRole, token);
        item->setToolTip(QString::fromLatin1(event.description));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Run on these events"), events,
        QStringLiteral("Choose one or more moments that may trigger this action. Nothing is selected for a new action; hover an entry for details.")));

    auto* rule = new QPlainTextEdit;
    rule->setObjectName(QStringLiteral("config.actions.when"));
    rule->setMinimumHeight(72);
    rule->setMaximumHeight(96);
    rule->setMaximumBlockCount(3);
    rule->setPlaceholderText(QStringLiteral("${eotf} == \"PQ\""));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Only run when (optional)"), rule,
        QStringLiteral("Leave blank to run for every selected event, or add a condition such as ${eotf} == \"PQ\". For Screen Config actions, use its visible name: ${screen_config} == \"Scope\".")));

    auto* command = new QLineEdit;
    command->setObjectName(QStringLiteral("config.actions.run"));
    command->setPlaceholderText(QStringLiteral("C:\\Tools\\action.cmd argument"));
    detailLayout->addWidget(fieldWithHelp(QStringLiteral("Command line"), command,
        QStringLiteral("Required only when enabled. Begin with an .exe, .bat, or .cmd path; arguments may use supported ${variable} placeholders.")));
    detailLayout->addStretch();
    detailStack->addWidget(detail);

    auto actionSections = [this]
    {
        QStringList result;
        if (!document_) return result;
        for (const std::string& raw : document_->SectionNamesWithPrefix("actions"))
        {
            const QString section = QString::fromLocal8Bit(raw.c_str());
            if (section.startsWith(QStringLiteral("actions."), Qt::CaseInsensitive) &&
                !section.mid(8).contains(u'.'))
                result.push_back(section);
        }
        return result;
    };

    auto loadDetails = [this, state, list, remove, detailStack, selectedTitle, name, enabled,
        rendererTarget, coalesceRole, delaySeconds, events, rule, command](QListWidgetItem* current)
    {
        state->loading = true;
        state->section = current ? current->data(Qt::UserRole).toString() : QString();
        const bool available = !state->section.isEmpty();
        remove->setEnabled(available);
        if (!available)
        {
            detailStack->setCurrentIndex(0);
            state->loading = false;
            return;
        }
        detailStack->setCurrentIndex(1);
        const QString shown = displayName(state->section, QStringLiteral("actions"));
        selectedTitle->setText(shown);
        name->setText(shown);
        enabled->setChecked(configuredBooleanValue(
            value(state->section, QStringLiteral("enabled")), true));
        const QString configuredRenderer = value(state->section, QStringLiteral("renderer"), QStringLiteral("vprenderer"));
        int rendererIndex = rendererTarget->findData(configuredRenderer, Qt::UserRole, Qt::MatchFixedString);
        if (rendererIndex < 0)
        {
            rendererTarget->addItem(configuredRenderer, configuredRenderer);
            rendererIndex = rendererTarget->count() - 1;
        }
        rendererTarget->setCurrentIndex(rendererIndex);
        rendererTarget->setEditText(rendererTarget->itemText(rendererIndex));
        coalesceRole->setText(value(state->section, QStringLiteral("coalesce_role")));
        bool validDelay = false;
        int configuredDelay = value(state->section,
            QStringLiteral("delay_seconds")).toInt(&validDelay);
        if (!validDelay)
        {
            configuredDelay = value(QStringLiteral("general"),
                QStringLiteral("event_action_delay_seconds"),
                QStringLiteral("5")).toInt(&validDelay);
            if (!validDelay) configuredDelay = 5;
        }
        delaySeconds->setValue(qBound(0, configuredDelay, 30));
        const QStringList configuredEvents = value(state->section, QStringLiteral("on")).split(u',', Qt::SkipEmptyParts);
        QListWidgetItem* firstChecked = nullptr;
        for (int index = 0; index < events->count(); ++index)
        {
            bool checked = false;
            for (const QString& configured : configuredEvents)
                if (configured.trimmed().compare(
                    events->item(index)->data(Qt::UserRole).toString(), Qt::CaseInsensitive) == 0)
                { checked = true; break; }
            events->item(index)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            if (checked && !firstChecked) firstChecked = events->item(index);
        }
        if (firstChecked)
        {
            events->setCurrentItem(firstChecked);
            events->scrollToItem(firstChecked, QAbstractItemView::PositionAtCenter);
        }
        else
        {
            events->setCurrentItem(nullptr);
            events->scrollToTop();
        }
        rule->setPlainText(value(state->section, QStringLiteral("when")));
        command->setText(value(state->section, QStringLiteral("run")));
        state->loading = false;
    };

    std::function<void(const QString&)> refresh = [list, actionSections, loadDetails](const QString& wanted)
    {
        list->clear();
        int selected = 0;
        const QStringList sections = actionSections();
        for (int index = 0; index < sections.size(); ++index)
        {
            auto* item = new QListWidgetItem(displayName(sections[index], QStringLiteral("actions")));
            item->setData(Qt::UserRole, sections[index]);
            item->setSizeHint(QSize(0, 36));
            list->addItem(item);
            if (sections[index] == wanted) selected = index;
        }
        if (list->count() == 0) loadDetails(nullptr);
        else list->setCurrentRow(selected);
    };

    connect(list, &QListWidget::currentItemChanged, this,
        [loadDetails](QListWidgetItem* current) { loadDetails(current); });
    connect(enabled, &QCheckBox::toggled, this, [this, state](bool checked)
    {
        if (state->loading || state->section.isEmpty()) return;
        if (checked) document_->RemoveKnown(state->section.toStdString(), "enabled");
        else document_->SetKnown(state->section.toStdString(), "enabled", "false");
        markDirty();
    });
    connect(rendererTarget, qOverload<int>(&QComboBox::activated), this,
        [this, state, rendererTarget](int index)
    {
        if (state->loading || state->section.isEmpty() || index < 0) return;
        const QString selected = rendererTarget->itemData(index).toString();
        if (selected == QStringLiteral("vprenderer")) document_->RemoveKnown(state->section.toStdString(), "renderer");
        else document_->SetKnown(state->section.toStdString(), "renderer", selected.toStdString());
        markDirty();
    });
    connect(rendererTarget->lineEdit(), &QLineEdit::editingFinished, this,
        [this, state, rendererTarget]
    {
        if (state->loading || state->section.isEmpty()) return;
        QString selected = rendererTarget->currentText().trimmed();
        const int current = rendererTarget->currentIndex();
        if (current >= 0 && selected == rendererTarget->itemText(current))
            selected = rendererTarget->itemData(current).toString();
        if (selected.isEmpty() || selected.compare(QStringLiteral("vprenderer"), Qt::CaseInsensitive) == 0)
            document_->RemoveKnown(state->section.toStdString(), "renderer");
        else document_->SetKnown(state->section.toStdString(), "renderer", selected.toStdString());
        markDirty();
    });
    connect(coalesceRole, &QLineEdit::textChanged, this,
        [this, state](const QString& text)
    {
        if (state->loading || state->section.isEmpty()) return;
        const QString role = text.trimmed();
        if (role.isEmpty())
            document_->RemoveKnown(state->section.toStdString(), "coalesce_role");
        else
            document_->SetKnown(state->section.toStdString(), "coalesce_role",
                role.toStdString());
        markDirty();
    });
    connect(delaySeconds, qOverload<int>(&QSpinBox::valueChanged), this,
        [this, state](int seconds)
    {
        if (state->loading || state->section.isEmpty()) return;
        document_->SetKnown(state->section.toStdString(), "delay_seconds",
            std::to_string(seconds));
        markDirty();
    });
    connect(events, &QListWidget::itemChanged, this, [this, state, events](QListWidgetItem*)
    {
        if (state->loading || state->section.isEmpty()) return;
        QStringList selected;
        for (int index = 0; index < events->count(); ++index)
            if (events->item(index)->checkState() == Qt::Checked)
                selected.push_back(events->item(index)->data(Qt::UserRole).toString());
        if (selected.isEmpty()) document_->RemoveKnown(state->section.toStdString(), "on");
        else document_->SetKnown(state->section.toStdString(), "on", selected.join(u',').toStdString());
        markDirty();
    });
    connect(rule, &QPlainTextEdit::textChanged, this, [this, state, rule]
    {
        if (state->loading || state->section.isEmpty()) return;
        QString text = rule->toPlainText().trimmed();
        text.replace(QRegularExpression(QStringLiteral("[\\r\\n]+")), QStringLiteral(" "));
        if (text.isEmpty()) document_->RemoveKnown(state->section.toStdString(), "when");
        else document_->SetKnown(state->section.toStdString(), "when", text.toStdString());
        markDirty();
    });
    connect(command, &QLineEdit::textChanged, this, [this, state](const QString& text)
    {
        if (state->loading || state->section.isEmpty()) return;
        if (text.trimmed().isEmpty()) document_->RemoveKnown(state->section.toStdString(), "run");
        else document_->SetKnown(state->section.toStdString(), "run", text.toStdString());
        markDirty();
    });
    connect(name, &QLineEdit::editingFinished, this, [this, state, name, actionSections, refresh]
    {
        if (state->loading || state->section.isEmpty()) return;
        const QString requested = name->text().trimmed();
        if (requested.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("Action name"), QStringLiteral("Action names cannot be empty."));
            return;
        }
        const QString identifier = profileIdentifier(requested);
        if (identifier.size() > 64)
        {
            QMessageBox::warning(this, QStringLiteral("Action name"), QStringLiteral("Action names must encode to 64 characters or fewer."));
            return;
        }
        const QString renamed = QStringLiteral("actions.") + identifier;
        if (actionSections().contains(renamed, Qt::CaseInsensitive) &&
            renamed.compare(state->section, Qt::CaseInsensitive) != 0)
        {
            QMessageBox::warning(this, QStringLiteral("Action name"), QStringLiteral("Another action already uses that name."));
            return;
        }
        if (renamed == state->section || !document_->RenameSection(state->section.toStdString(), renamed.toStdString())) return;
        state->section = renamed;
        markDirty();
        refresh(renamed);
    });
    connect(add, &QPushButton::clicked, this, [this, actionSections, refresh, command]
    {
        bool ok = false;
        const QString requested = QInputDialog::getText(this, QStringLiteral("Add action"),
            QStringLiteral("Action name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || requested.isEmpty()) return;
        const QString identifier = profileIdentifier(requested);
        if (identifier.size() > 64) return;
        QString section = QStringLiteral("actions.") + identifier;
        const QStringList existing = actionSections();
        int suffix = 2;
        while (existing.contains(section, Qt::CaseInsensitive))
            section = QStringLiteral("actions.") + identifier + QString::number(suffix++);
        if (!document_->AddSection(section.toStdString())) return;
        document_->SetKnown(section.toStdString(), "enabled", "false");
        markDirty();
        refresh(section);
        command->setFocus();
    });
    connect(remove, &QPushButton::clicked, this, [this, state, refresh]
    {
        if (state->section.isEmpty() || QMessageBox::question(this, QStringLiteral("Remove action"),
            QStringLiteral("Remove this action?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        document_->RemoveSection(state->section.toStdString());
        markDirty();
        refresh({});
    });
    refresh({});

    splitter->addWidget(createCard(QStringLiteral("Actions"),
        QStringLiteral("Actions have no default or priority. Disabled actions are retained as drafts."), listContent));
    splitter->addWidget(createCard(QStringLiteral("Action details"),
        QStringLiteral("Choose where and when an action runs, plus the command to start."), detailStack));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 310, 620 });
    return createPage(QStringLiteral("Actions"),
        QStringLiteral("Run external commands when selected VideoProcessor events occur."), splitter);
}

QWidget* ConfigEditorWindow::createLogsPage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* enabled = bindCheckField(QStringLiteral("Enable logging"),
        QStringLiteral("logging"), QStringLiteral("enabled"), true);
    enabled->setObjectName(QStringLiteral("config.logging.enabled"));
    enabled->setAccessibleDescription(QStringLiteral(
        "Enable VP logging. Logging is enabled by default."));
    layout->addWidget(enabled);

    auto* enhanced = bindCheckField(QStringLiteral("Enable enhanced logging"),
        QStringLiteral("logging"), QStringLiteral("debug"), false);
    enhanced->setObjectName(QStringLiteral("config.logging.debug"));
    enhanced->setAccessibleDescription(QStringLiteral(
        "Keep all VP logs and write additional live telemetry files for diagnosis."));
    layout->addWidget(enhanced);

    auto* retention = new QSpinBox;
    retention->setObjectName(QStringLiteral("config.logging.debug_log_retention"));
    retention->setAccessibleName(QStringLiteral("Log retention"));
    retention->setRange(1, 100);
    retention->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    retention->setMaximumWidth(108);
    retention->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    retention->setAlignment(Qt::AlignRight);
    retention->setValue(value(QStringLiteral("logging"),
        QStringLiteral("debug_log_retention"), QStringLiteral("10")).toInt());
    retention->setToolTip(QStringLiteral(
        "Keep 1 to 100 total files, including the active VP log. Default: 10."));
    connect(retention, qOverload<int>(&QSpinBox::valueChanged), this,
        [this](int count)
    {
        if (!document_) return;
        document_->SetKnown("logging", "debug_log_retention",
            std::to_string(count));
        markDirty();
    });
    auto syncLoggingControls = [enabled, enhanced, retention]
    {
        const bool loggingEnabled = enabled->isChecked();
        enhanced->setEnabled(loggingEnabled);
        retention->setEnabled(loggingEnabled && !enhanced->isChecked());
    };
    connect(enabled, &QCheckBox::toggled, this, syncLoggingControls);
    connect(enhanced, &QCheckBox::toggled, this, syncLoggingControls);
    syncLoggingControls();
    layout->addWidget(fieldWithHelp(QStringLiteral("Log files to keep"), retention,
        QStringLiteral("1–100 total files, including the active log. Default: 10. Changes apply when VP next starts.")));

    layout->addWidget(helpLabel(QStringLiteral(
        "Enhanced logging keeps all logs and writes additional live telemetry files. Use it only while diagnosing an issue.")));

    QString logDirectory = QFileInfo(configPath_).absoluteDir().filePath(QStringLiteral("logs"));
    const QString logPath = QDir(logDirectory).filePath(QStringLiteral("vp.log"));
    auto* actions = new QHBoxLayout;
    auto* openFolder = new QPushButton(QStringLiteral("Open log folder"));
    auto* openLog = new QPushButton(QStringLiteral("Open current log"));
    openFolder->setAccessibleDescription(QStringLiteral("Open the folder containing VideoProcessor logs."));
    openLog->setAccessibleDescription(QStringLiteral("Open the current VideoProcessor log."));
    actions->addWidget(openFolder);
    actions->addWidget(openLog);
    actions->addStretch();
    layout->addLayout(actions);
    connect(openFolder, &QPushButton::clicked, this, [this, logDirectory]
    {
        if (!QDir(logDirectory).exists())
        {
            QMessageBox::information(this, QStringLiteral("Log folder"),
                QStringLiteral("VP has not created its logs folder yet. Start VideoProcessor once, then try again."));
            return;
        }
        if (!openPathExternally(logDirectory))
            QMessageBox::warning(this, QStringLiteral("Log folder"),
                QStringLiteral("Windows could not open the log folder."));
    });
    connect(openLog, &QPushButton::clicked, this, [this, logPath]
    {
        if (!QFileInfo::exists(logPath))
        {
            QMessageBox::information(this, QStringLiteral("Current log"),
                QStringLiteral("No current VP log exists yet. Start VideoProcessor once, then try again."));
            return;
        }
        if (!openPathExternally(logPath))
            QMessageBox::warning(this, QStringLiteral("Current log"),
                QStringLiteral("Windows could not open the current log."));
    });
    layout->addStretch();

    return createPage(QStringLiteral("Logs"),
        QStringLiteral("Control VideoProcessor log files and diagnostic detail."), content);
}

QWidget* ConfigEditorWindow::createShortcutsPage()
{
    struct ShortcutField
    {
        const char* label;
        const char* key;
        const char* defaultValue;
    };
    const ShortcutField applicationFields[] = {
        { "Open configuration", "config_editor", "Ctrl+Shift+S" },
        { "Toggle video-only UI", "toggle_noui", "Ctrl+Shift+U" },
        { "Toggle fullscreen", "fullscreen_toggle", "Alt+Enter" },
        { "Exit fullscreen", "fullscreen_exit", "Esc" },
        { "Toggle statistics", "toggle_stats_overlay", "Ctrl+I" },
        { "Screenshot", "capture_rendered_output", "Ctrl+Alt+S" },
        { "Re-apply rules", "reapply_rules", "" },
        { "Show profiles", "show_profiles", "Ctrl+Alt+I" },
        { "Automatic transfer", "auto_set", "Ctrl+Shift+A" },
        { "PQ transfer", "pq_set", "Ctrl+Shift+P" }
    };
    const ShortcutField captureFields[] = {
        { "Restart renderer", "renderer_restart", "Shift+R" },
        { "Reset renderer", "renderer_reset", "R" },
        { "DeckLink input 1", "capture_1", "Ctrl+1" },
        { "DeckLink input 2", "capture_2", "Ctrl+2" },
        { "DeckLink input 3", "capture_3", "Ctrl+3" },
        { "DeckLink input 4", "capture_4", "Ctrl+4" },
        { "Video conversion off", "video_conversion_off", "V" },
        { "V210 to P010 conversion", "video_conversion_p010", "Shift+V" }
    };

    auto makeEditor = [this](const QString& key, const QString& defaultValue)
    {
        const std::string section = "shortcuts";
        const std::string keyText = key.toStdString();
        size_t line = 0, start = 0, end = 0;
        const bool explicitlyConfigured = document_ &&
            document_->Find(section, keyText.c_str(), line, start, end);
        const QString initial = canonicalShortcutText(explicitlyConfigured ?
            QString::fromLocal8Bit(document_->Get(section.c_str(), keyText.c_str()).c_str()) :
            defaultValue);
        auto* edit = new QLineEdit(initial);
        edit->setObjectName(controlName(QStringLiteral("shortcuts"), key));
        edit->setAccessibleName(accessibleSettingName(key));
        // QLineEdit's size hint shrinks when the clear button is absent. Keep
        // every shortcut column aligned whether the value is populated,
        // explicitly empty, or currently has no visible clear affordance.
        edit->setFixedWidth(220);
        edit->setClearButtonEnabled(true);
        edit->setToolTip(defaultValue.isEmpty() ?
            QStringLiteral("No built-in shortcut. Clear and save to disable.") :
            QStringLiteral("VP default: %1. Clear and save to disable.").arg(defaultValue));
        connect(edit, &QLineEdit::textChanged, this, [this, key = keyText](const QString& text)
        {
            if (!document_) return;
            if (!document_->SetKnown("shortcuts", key.c_str(), text.toLocal8Bit().constData()))
            {
                document_->AddSection("shortcuts");
                document_->SetKnown("shortcuts", key.c_str(), text.toLocal8Bit().constData());
            }
            markDirty();
        });
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key = keyText]
        {
            const QString entered = edit->text().trimmed();
            if (entered.isEmpty()) return;
            std::string canonical;
            if (!RendererProfileConfig::CanonicalizeKeyChord(entered.toStdString(), canonical))
            {
                edit->setProperty("invalid", true);
                edit->style()->unpolish(edit);
                edit->style()->polish(edit);
                setStatus(QStringLiteral("%1 is not a valid shortcut. Use a chord such as Ctrl+Shift+A.").arg(entered), true);
                return;
            }
            edit->setProperty("invalid", false);
            edit->style()->unpolish(edit);
            edit->style()->polish(edit);
            const QString normalized = QString::fromStdString(canonical);
            if (edit->text() != normalized) edit->setText(normalized);
            else if (document_) document_->SetKnown("shortcuts", key.c_str(), canonical);
        });
        return edit;
    };

    auto makeForm = [&makeEditor](const ShortcutField* fields, size_t count)
    {
        auto* content = new QWidget;
        auto* form = new QFormLayout(content);
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(18);
        form->setVerticalSpacing(8);
        form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        for (size_t index = 0; index < count; ++index)
            form->addRow(QString::fromLatin1(fields[index].label), makeEditor(
                QString::fromLatin1(fields[index].key), QString::fromLatin1(fields[index].defaultValue)));
        return content;
    };

    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto* fixedShortcuts = new ResponsiveCardGrid;
    auto* applicationCard = createCard(QStringLiteral("Application"),
        QStringLiteral("Built-in application and display controls."),
        makeForm(applicationFields, std::size(applicationFields)));
    auto* captureCard = createCard(QStringLiteral("Capture & renderer"),
        QStringLiteral("Capture selection, renderer control, and active-renderer conversion."),
        makeForm(captureFields, std::size(captureFields)));
    fixedShortcuts->addCard(applicationCard);
    fixedShortcuts->addCard(captureCard);
    layout->addWidget(fixedShortcuts);

    const QStringList& renderers = allRenderers_;
    if (!renderers.isEmpty())
    {
        auto* rendererContent = new QWidget;
        auto* rendererForm = new QFormLayout(rendererContent);
        rendererShortcutForm_ = rendererForm;
        rendererForm->setContentsMargins(0, 0, 0, 0);
        rendererForm->setHorizontalSpacing(18);
        rendererForm->setVerticalSpacing(8);
        rendererForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        rendererForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
        for (int index = 0; index < renderers.size(); ++index)
        {
            QLineEdit* edit = makeEditor(QStringLiteral("render.%1").arg(index + 1), QString());
            edit->setProperty("rendererName", renderers[index]);
            rendererForm->addRow(QStringLiteral("%1 - %2").arg(index + 1).arg(renderers[index]), edit);
        }
        layout->addWidget(createCard(QStringLiteral("Renderer selection"),
            QStringLiteral("Optional shortcuts select a renderer by its one-based position in VP's renderer list on this PC."),
            rendererContent));
        applyRendererVisibilityFilter(configuredBooleanValue(value(
            QStringLiteral("general"), QStringLiteral("hide_legacy_renderers")),
            true));
    }
    return createPage(QStringLiteral("Shortcuts"),
        QStringLiteral("Configure keyboard shortcuts. Defaults are shown; clear a field and save to disable it. A running VP applies saved shortcuts immediately."),
        content);
}

bool ConfigEditorWindow::savedForegroundOnlyEnabled() const
{
    for (const auto& section : savedSnapshot_)
    {
        if (ConfigurationApplyPolicy::NormalizeSection(section.first) !=
            "shortcuts")
            continue;
        for (const auto& setting : section.second)
            if (ConfigurationApplyPolicy::NormalizeSection(setting.first) ==
                "foreground_only")
                return configuredBooleanValue(
                    QString::fromLocal8Bit(setting.second.c_str()), false);
    }
    return false;
}

void ConfigEditorWindow::returnFocusToPresentationTarget(const char* reason)
{
    const HWND target = reinterpret_cast<HWND>(presentationTargetHandle_);
    DWORD actualProcessId = 0;
    const bool validTarget = target && IsWindow(target) &&
        presentationTargetProcessId_ != 0 &&
        presentationTargetProcessId_ == ownerProcessId_ &&
        GetWindowThreadProcessId(target, &actualProcessId) != 0 &&
        actualProcessId == presentationTargetProcessId_;
    if (!ConfigurationLiveApply::ShouldReturnPresentationFocus(
        savedForegroundOnlyEnabled(), true, validTarget))
        return;

    if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
    AllowSetForegroundWindow(ownerProcessId_);
    BringWindowToTop(target);
    const BOOL requested = SetForegroundWindow(target);
    const HWND foreground = GetForegroundWindow();
    const QString diagnostic = QStringLiteral(
        "VideoProcessor Config focus handoff: reason=%1 target=0x%2 requested=%3 foreground=0x%4\n")
        .arg(QString::fromLatin1(reason))
        .arg(reinterpret_cast<quintptr>(target), 0, 16)
        .arg(requested ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(reinterpret_cast<quintptr>(foreground), 0, 16);
    OutputDebugStringW(reinterpret_cast<LPCWSTR>(diagnostic.utf16()));
}

QWidget* ConfigEditorWindow::createShortcutsSetupPage()
{
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto* handling = new QWidget;
    auto* handlingLayout = new QVBoxLayout(handling);
    handlingLayout->setContentsMargins(0, 0, 0, 0);
    handlingLayout->setSpacing(8);
    handlingLayout->addWidget(bindCheckField(
        QStringLiteral("Only process shortcuts while VideoProcessor is in the foreground"),
        QStringLiteral("shortcuts"), QStringLiteral("foreground_only"), false));
    auto* help = new QLabel(QStringLiteral(
        "When enabled, background applications keep their keystrokes. VideoProcessor asks Windows to return focus after startup, a renderer change, or closing or minimizing Config."));
    help->setWordWrap(true);
    help->setObjectName(QStringLiteral("fieldHelp"));
    handlingLayout->addWidget(help);
    layout->addWidget(createCard(QStringLiteral("Keyboard handling"),
        QStringLiteral("Choose whether shortcuts remain global or require VideoProcessor focus."),
        handling));
    layout->addStretch();

    return createPage(QStringLiteral("Shortcuts"),
        QStringLiteral("Control when VideoProcessor processes configured keyboard shortcuts."),
        content);
}

void ConfigEditorWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    tray_->setToolTip(QStringLiteral("VideoProcessor Configuration"));
    auto* menu = new QMenu(this);
    QAction* open = menu->addAction(QStringLiteral("Open configuration"));
    menu->addSeparator();
    QAction* exit = menu->addAction(QStringLiteral("Exit"));
    tray_->setContextMenu(menu);
    connect(open, &QAction::triggered, this, [this] { reveal(); });
    connect(exit, &QAction::triggered, this, [this] { exitApplication(); });
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) reveal();
    });
    tray_->show();
}

void ConfigEditorWindow::reveal()
{
    // AppMain deliberately defers the first reveal until the event loop has
    // started. Clearing this only here prevents a transient blank native frame
    // from appearing during construction or owner association.
    setAttribute(Qt::WA_DontShowOnScreen, false);
    // A reveal that arrived after an older queued close is the newer command.
    // Remove only already-posted close requests; a subsequent user close is
    // still delivered normally and returns the editor to the tray.
    QCoreApplication::removePostedEvents(this, QEvent::Close);
    positionForReveal();
    showNormal();
    scopedTopmostEligible_ = true;
    explicitRevealIntent_ = true;
    pendingTopmostReassert_ = true;
    // Let Qt issue its activation/z-order requests first. The native placement
    // below is authoritative and must run last; otherwise Qt can finalize a
    // queued raise afterward and remove WS_EX_TOPMOST a few seconds later.
    raise();
    activateWindow();
    applyScopedTopmost();
	// Execute the native z-order/foreground request for every explicit reveal,
	// even when our cached state already says the window is topmost. Renderer
	// handoffs and exclusive fullscreen can reorder or cover the native window
	// without changing Qt's visibility/topmost bookkeeping.
	const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
	if (editor && IsWindow(editor))
	{
		ShowWindow(editor, SW_RESTORE);
		SetWindowPos(editor, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		BringWindowToTop(editor);
		SetForegroundWindow(editor);
	}
    // Ignore only activation churn caused by this reveal itself. A later,
    // settled user transition away from Config disables lifecycle reasserts.
    QTimer::singleShot(100, this, [this] { explicitRevealIntent_ = false; });
}

void ConfigEditorWindow::exitApplication()
{
    exitRequested_ = true;
    if (tray_) tray_->hide();
    QCoreApplication::quit();
}

void ConfigEditorWindow::closeEvent(QCloseEvent* event)
{
    if (!exitRequested_ && tray_ && tray_->isVisible())
    {
        const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
        returnFocusAfterHide_ = savedForegroundOnlyEnabled() && editor &&
            GetForegroundWindow() == editor;
        if (returnFocusAfterHide_ && ownerProcessId_ != 0)
            AllowSetForegroundWindow(ownerProcessId_);
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void ConfigEditorWindow::hideEvent(QHideEvent* event)
{
    if (activeProfileTimer_) activeProfileTimer_->stop();
    pendingTopmostReassert_ = false;
    explicitRevealIntent_ = false;
    scopedTopmostEligible_ = false;
    removeScopedTopmost();
    clearNativeOwner();
    QMainWindow::hideEvent(event);
    if (returnFocusAfterHide_)
    {
        returnFocusAfterHide_ = false;
        returnFocusToPresentationTarget("config-close");
    }
}

void ConfigEditorWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (activeProfileTimer_ && !activeProfileTimer_->isActive())
    {
        refreshActiveProfileIndicators();
        activeProfileTimer_->start(500);
    }
    scopedTopmostEligible_ = true;
    pendingTopmostReassert_ = true;
    applyNativeOwner();
    publishNativeAssociation();
    // QWindows can finalize its native top-level style/owner after showEvent.
    // Reapply once at the next event-loop turn so that finalization cannot
    // clear the validated cross-process owner.
    QTimer::singleShot(0, this, [this]
    {
        applyNativeOwner();
        publishNativeAssociation();
        applyScopedTopmost();
    });
}

bool ConfigEditorWindow::nativeEvent(const QByteArray& eventType,
    void* nativeMessage, qintptr* result)
{
    Q_UNUSED(eventType);
    MSG* message = static_cast<MSG*>(nativeMessage);
    static const UINT activationMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Activate.v1");
    static const UINT presentationTargetMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.PresentationTarget.v1");
	static const UINT presentationTargetMessageV2 = RegisterWindowMessageW(
		L"VideoProcessor.ConfigEditor.PresentationTarget.v2");
	static const UINT presentationTargetAcknowledgementEndpointMessage =
		RegisterWindowMessageW(
			L"VideoProcessor.ConfigEditor.PresentationTargetAckEndpoint.v1");
	static const UINT presentationTargetAcknowledgementMessageV2 =
		RegisterWindowMessageW(
			L"VideoProcessor.ConfigEditor.PresentationTargetAck.v2");
    static const UINT reassertMessage = RegisterWindowMessageW(
        L"VideoProcessor.ConfigEditor.Reassert.v1");
    if (!message)
        return QMainWindow::nativeEvent(eventType, nativeMessage, result);
	if (presentationTargetAcknowledgementEndpointMessage &&
		message->message == presentationTargetAcknowledgementEndpointMessage)
	{
		const DWORD requestedProcessId = static_cast<DWORD>(message->wParam);
		const HWND requestedEndpoint = reinterpret_cast<HWND>(message->lParam);
		DWORD actualProcessId = 0;
		if (requestedEndpoint && IsWindow(requestedEndpoint))
			GetWindowThreadProcessId(requestedEndpoint, &actualProcessId);
		const bool accepted = requestedProcessId != 0 &&
			requestedProcessId == ownerProcessId_ &&
			actualProcessId == requestedProcessId;
		if (accepted)
		{
			presentationTargetAcknowledgementHandle_ =
				reinterpret_cast<quintptr>(requestedEndpoint);
			presentationTargetAcknowledgementProcessId_ = requestedProcessId;
		}
		if (result) *result = accepted ? 1 : 0;
		return true;
	}

    if (message->message == presentationTargetMessage ||
		message->message == presentationTargetMessageV2)
    {
        const DWORD requestedProcessId = static_cast<DWORD>(message->wParam);
		const quint32 requestedSequence =
			message->message == presentationTargetMessageV2 ?
				static_cast<quint32>(static_cast<quint64>(message->wParam) >> 32) :
				0;
        const HWND requestedTarget = reinterpret_cast<HWND>(message->lParam);
        DWORD actualProcessId = 0;
        if (requestedTarget && IsWindow(requestedTarget))
            GetWindowThreadProcessId(requestedTarget, &actualProcessId);
        const bool accepted = requestedProcessId != 0 &&
            requestedProcessId == ownerProcessId_ &&
            actualProcessId == requestedProcessId;
        if (accepted)
        {
            presentationTargetHandle_ = reinterpret_cast<quintptr>(requestedTarget);
            presentationTargetProcessId_ = requestedProcessId;
			// The warm editor may be revealed through its process event before
			// this asynchronous target update is dispatched.  Place it immediately
			// as well as during reveal so every open follows VP's current display.
			positionForReveal();
			if (isVisible() && scopedTopmostEligible_)
			{
				pendingTopmostReassert_ = true;
				QTimer::singleShot(0, this, [this] { applyScopedTopmost(); });
			}
			// The target is placement metadata only: Config remains an independent
			// native top-level window and never acquires the fullscreen HWND as an
			// owner. v2 acknowledges receiver acceptance asynchronously; VP treats
			// a queued PostMessage as pending, never as accepted.
			const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
			if (requestedSequence != 0 && presentationTargetAcknowledgementMessageV2 &&
				presentationTargetAcknowledgementHandle_ && editor &&
				IsWindow(editor) &&
				presentationTargetAcknowledgementProcessId_ == requestedProcessId &&
				IsWindow(reinterpret_cast<HWND>(
					presentationTargetAcknowledgementHandle_)))
			{
				PostMessageW(reinterpret_cast<HWND>(
					presentationTargetAcknowledgementHandle_),
					presentationTargetAcknowledgementMessageV2,
					static_cast<WPARAM>(requestedSequence),
					reinterpret_cast<LPARAM>(editor));
			}
        }
        if (result) *result = accepted ? 1 : 0;
        return true;
    }
    if (message->message == reassertMessage)
    {
        const DWORD requestedProcessId = static_cast<DWORD>(message->wParam);
        const HWND requestedTarget = reinterpret_cast<HWND>(message->lParam);
        DWORD actualProcessId = 0;
        if (requestedTarget && IsWindow(requestedTarget))
            GetWindowThreadProcessId(requestedTarget, &actualProcessId);
        const bool accepted = requestedProcessId != 0 &&
            requestedProcessId == ownerProcessId_ &&
            actualProcessId == requestedProcessId &&
            reinterpret_cast<quintptr>(requestedTarget) ==
                presentationTargetHandle_;
        if (accepted && scopedTopmostEligible_)
        {
            pendingTopmostReassert_ = true;
            QTimer::singleShot(0, this, [this] { applyScopedTopmost(); });
        }
        if (result) *result = accepted ? 1 : 0;
        return true;
    }
    if (message->message != activationMessage)
        return QMainWindow::nativeEvent(eventType, nativeMessage, result);

    const DWORD requestedProcessId = static_cast<DWORD>(message->wParam);
    const HWND requestedOwner = reinterpret_cast<HWND>(message->lParam);
    if (requestedOwner && IsWindow(requestedOwner) && requestedProcessId != 0)
    {
        DWORD actualProcessId = 0;
        GetWindowThreadProcessId(requestedOwner, &actualProcessId);
        if (actualProcessId == requestedProcessId)
        {
            clearNativeOwner();
            ownerHandle_ = reinterpret_cast<quintptr>(requestedOwner);
            ownerProcessId_ = requestedProcessId;
            if (presentationTargetProcessId_ != requestedProcessId)
            {
                presentationTargetHandle_ = 0;
                presentationTargetProcessId_ = 0;
            }
            ownerApplied_ = false;
            publishedWindowHandle_ = 0;
            if (isVisible()) applyNativeOwner();
        }
    }

    // Acknowledge the cross-process SendMessage immediately. Revealing can
    // synchronously enter showEvent; doing it inside this handler used to send
    // Association back to the VP UI thread while that thread was blocked here.
    // Queue all UI work until this native call stack has unwound.
    if (result) *result = 1;
    QTimer::singleShot(0, this, [this]
    {
        // A popup from the hidden/previous editor state must not survive as an
        // independently floating native window when VP reactivates Config.
        if (QWidget* popup = QApplication::activePopupWidget()) popup->close();
        reveal();
    });
    return true;
}

bool ConfigEditorWindow::event(QEvent* event)
{
    bool returnAfterMinimize = false;
    if (event->type() == QEvent::WindowStateChange)
    {
        const auto* stateChange = static_cast<QWindowStateChangeEvent*>(event);
        const HWND editor = reinterpret_cast<HWND>(effectiveWinId());
        returnAfterMinimize = !(stateChange->oldState() & Qt::WindowMinimized) &&
            editor && GetForegroundWindow() == editor &&
            savedForegroundOnlyEnabled();
        if (returnAfterMinimize && ownerProcessId_ != 0)
            AllowSetForegroundWindow(ownerProcessId_);
    }
    const bool handled = QMainWindow::event(event);
    if (returnAfterMinimize && isMinimized())
        returnFocusToPresentationTarget("config-minimize");
    if (event->type() == QEvent::WinIdChange)
    {
        if (scopedTopmost_)
        {
            scopedTopmost_ = false;
            pendingTopmostReassert_ = true;
        }
        publishedWindowHandle_ = 0;
        QTimer::singleShot(0, this, [this]
        {
            if (isVisible()) applyNativeOwner();
            publishNativeAssociation();
            applyScopedTopmost();
        });
    }
    return handled;
}

void ConfigEditorWindow::setStatus(const QString& message, bool error)
{
    if (!status_) return;
    status_->setText(message);
    status_->setStyleSheet(error ? QStringLiteral("color: #ff8d86;") : QStringLiteral("color: #9fb2c4;"));
    QAccessibleEvent changed(status_, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&changed);
}

void ConfigEditorWindow::setWarningStatus(const QString& message)
{
    if (!status_) return;
    status_->setText(message);
    status_->setStyleSheet(QStringLiteral("color: #e0b45c;"));
    QAccessibleEvent changed(status_, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&changed);
}
