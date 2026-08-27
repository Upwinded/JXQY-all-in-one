#include "DialogueEditorWindow.h"

#include "MpcPreviewLabel.h"
#include "../core/AssetPreviewLoader.h"
#include "../core/EditorAssetPath.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <utility>

namespace
{
class DialogueSnapshotCommand final : public QUndoCommand
{
public:
    DialogueSnapshotCommand(
        QByteArray before, QByteArray after,
        std::function<void(const QByteArray&)> apply,
        const QString& description)
        : beforeBytes(std::move(before))
        , afterBytes(std::move(after))
        , applyBytes(std::move(apply))
    {
        setText(description);
    }

    void undo() override { applyBytes(beforeBytes); }
    void redo() override { applyBytes(afterBytes); }

private:
    QByteArray beforeBytes;
    QByteArray afterBytes;
    std::function<void(const QByteArray&)> applyBytes;
};

QString replaceExtension(const QString& path, const QString& extension)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash ? path.left(dot) + extension : path + extension;
}
}

DialogueEditorWindow::DialogueEditorWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("dialogueEditorWindow"));
    undoStack = new QUndoStack(this);
    undoStack->setObjectName(QStringLiteral("dialogueUndoStack"));
    setupUi();
    setupActions();
    setupConnections();
    retranslateDynamicUi();
    updateActionStates();
}

DialogueEditorWindow::~DialogueEditorWindow()
{
    if (undoStack)
    {
        disconnect(undoStack, nullptr, this, nullptr);
        undoStack->clear();
    }
}

bool DialogueEditorWindow::isDialogueFilePath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(
        EditorAssetPath::normalizedAbsolutePath(path));
    return normalized.endsWith(QStringLiteral("/talk.txt"),
                               Qt::CaseInsensitive) &&
        normalized.contains(QStringLiteral("/script/map/"),
                            Qt::CaseInsensitive);
}

bool DialogueEditorWindow::openFile(const QString& requestedPath,
                                    const QString& section)
{
    if (requestedPath.trimmed().isEmpty())
        return false;
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    if (!isDialogueFilePath(normalized) ||
        (documentPathValidator &&
         !documentPathValidator(filePath, normalized)) ||
        !confirmSaveIfModified())
    {
        return false;
    }

    DialogueDocument candidate;
    QString error;
    if (!candidate.openFile(normalized, &error))
    {
        QMessageBox::warning(
            this, tr("无法打开对话"),
            tr("无法读取对话文件：\n%1\n\n%2").arg(normalized, error));
        return false;
    }

    document = std::move(candidate);
    filePath = normalized;
    selectedSection.clear();
    selectedLineRow = -1;
    undoStack->clear();
    undoStack->setClean();
    refreshSectionList();
    const bool selectedRequested = section.isEmpty() || selectSection(section);
    if (!selectedRequested)
    {
        QMessageBox::information(
            this, tr("未找到对话段落"),
            tr("对话文件中没有“%1”，已打开文件供你选择其他段落。")
                .arg(section));
    }
    updateWindowTitle();
    updateActionStates();
    emit documentStatesChanged();
    return true;
}

bool DialogueEditorWindow::selectSection(const QString& section)
{
    commitPendingLineEditors();
    for (int row = 0; row < sectionList->count(); ++row)
    {
        QListWidgetItem* item = sectionList->item(row);
        if (item->data(Qt::UserRole).toString().compare(
                section, Qt::CaseInsensitive) == 0)
        {
            sectionList->setCurrentRow(row);
            return true;
        }
    }

    if (document.hasSection(section))
    {
        searchEdit->clear();
        refreshSectionList();
        return selectSection(section);
    }
    return false;
}

bool DialogueEditorWindow::saveFile()
{
    if (filePath.isEmpty())
        return false;
    commitPendingLineEditors();
    QString error;
    if (!document.saveFile(filePath, &error))
    {
        QMessageBox::warning(
            this, tr("保存失败"),
            tr("无法保存当前对话：\n%1\n\n%2").arg(filePath, error));
        return false;
    }
    undoStack->setClean();
    updateWindowTitle();
    emit documentStatesChanged();
    return true;
}

bool DialogueEditorWindow::hasUnsavedChanges() const
{
    return !undoStack->isClean();
}

QString DialogueEditorWindow::currentFilePath() const
{
    return filePath;
}

QString DialogueEditorWindow::currentSectionName() const
{
    return selectedSection;
}

QString DialogueEditorWindow::displayName() const
{
    if (!selectedSection.isEmpty())
        return selectedSection;
    const QString mapName = QFileInfo(filePath).dir().dirName();
    return mapName.isEmpty() ? tr("未选择段落") : mapName;
}

void DialogueEditorWindow::setCaller(const QString& scriptPath,
                                     int line, int column)
{
    callerScriptPath = EditorAssetPath::normalizedAbsolutePath(scriptPath);
    callerLine = line;
    callerColumn = column;
    updateActionStates();
}

void DialogueEditorWindow::clearCaller()
{
    callerScriptPath.clear();
    callerLine = 0;
    callerColumn = 0;
    updateActionStates();
}

bool DialogueEditorWindow::hasCaller() const
{
    return !callerScriptPath.isEmpty() && callerLine > 0 && callerColumn > 0;
}

void DialogueEditorWindow::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path.isEmpty()
        ? QString()
        : EditorAssetPath::normalizedAbsolutePath(path);
    refreshPortraitChoices();
    refreshPortraitPreview();
}

void DialogueEditorWindow::setDocumentPathValidator(
    std::function<bool(const QString&, const QString&)> validator)
{
    documentPathValidator = std::move(validator);
}

QList<ProjectDocumentState>
DialogueEditorWindow::currentProjectDocuments() const
{
    if (filePath.isEmpty())
        return {};
    return {{filePath, ProjectDocumentType::Dialogue,
             hasUnsavedChanges()}};
}

DesktopRunDocumentSnapshot
DialogueEditorWindow::desktopRunDocumentSnapshot() const
{
    const_cast<DialogueEditorWindow*>(this)->commitPendingLineEditors();
    DesktopRunDocumentSnapshot snapshot;
    snapshot.filePath = filePath;
    snapshot.type = ProjectDocumentType::Dialogue;
    snapshot.dirty = hasUnsavedChanges();
    snapshot.includeInOverlay = snapshot.dirty;
    snapshot.serializationSupported = !filePath.isEmpty();
    snapshot.bytes = document.serializedBytes();
    if (!snapshot.serializationSupported)
        snapshot.diagnosticCode = QStringLiteral("dialogue.document.unsaved");
    return snapshot;
}

ClosePlan DialogueEditorWindow::prepareCloseTransaction() const
{
    const_cast<DialogueEditorWindow*>(this)->commitPendingLineEditors();
    ClosePlan plan;
    if (!hasUnsavedChanges())
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        const_cast<DialogueEditorWindow*>(this),
        tr("保存更改"),
        tr("对话“%1”已修改，是否保存？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    plan.decisions.append(
        choice == QMessageBox::Save ? CloseDecision::Save :
        choice == QMessageBox::Discard ? CloseDecision::Discard :
        CloseDecision::Cancelled);
    return plan;
}

bool DialogueEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    return plan.decisions.size() == 1 && !plan.isCancelled() &&
        (plan.decisions.front() != CloseDecision::Save || saveFile());
}

void DialogueEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() == 1 && !plan.isCancelled())
        allowPreparedClose();
}

AssetsPathSwitchParticipant::Decision
DialogueEditorWindow::prepareAssetsPathSwitch(const QString& path) const
{
    Q_UNUSED(path);
    const_cast<DialogueEditorWindow*>(this)->commitPendingLineEditors();
    if (!hasUnsavedChanges())
        return Decision::Ready;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        const_cast<DialogueEditorWindow*>(this),
        tr("切换项目资源"),
        tr("当前对话已修改。切换项目资源前是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return choice == QMessageBox::Save ? Decision::Save :
        choice == QMessageBox::Discard ? Decision::Discard :
        Decision::Cancelled;
}

bool DialogueEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    return decision != Decision::Cancelled &&
        (decision != Decision::Save || saveFile());
}

void DialogueEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    setAssetsBasePath(path);
}

QString DialogueEditorWindow::currentAssetsPath() const
{
    return assetsBasePath;
}

void DialogueEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose() || confirmSaveIfModified())
    {
        emit documentClosed();
        event->accept();
        return;
    }
    event->ignore();
}

void DialogueEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateDynamicUi();
        refreshSectionList();
        refreshLineList();
        refreshLineEditor();
    }
    QWidget::changeEvent(event);
}

bool DialogueEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (!refreshing &&
        (watched == speakerEdit || watched == textEdit ||
         watched == portraitReferenceCombo->lineEdit()) &&
        event->type() == QEvent::FocusOut)
    {
        commitPendingLineEditors();
    }
    return QWidget::eventFilter(watched, event);
}

void DialogueEditorWindow::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("dialogueToolBar"));
    rootLayout->addWidget(toolBar);

    auto* splitter = new QSplitter(this);
    splitter->setObjectName(QStringLiteral("dialogueMainSplitter"));
    rootLayout->addWidget(splitter, 1);

    auto* sectionPanel = new QWidget(splitter);
    auto* sectionLayout = new QVBoxLayout(sectionPanel);
    auto* sectionLabel = new QLabel(sectionPanel);
    sectionLabel->setObjectName(QStringLiteral("dialogueSectionListLabel"));
    searchEdit = new QLineEdit(sectionPanel);
    searchEdit->setObjectName(QStringLiteral("dialogueSearchEdit"));
    sectionList = new QListWidget(sectionPanel);
    sectionList->setObjectName(QStringLiteral("dialogueSectionList"));
    sectionLayout->addWidget(sectionLabel);
    sectionLayout->addWidget(searchEdit);
    sectionLayout->addWidget(sectionList, 1);

    auto* linePanel = new QWidget(splitter);
    auto* lineLayout = new QVBoxLayout(linePanel);
    sectionSummaryLabel = new QLabel(linePanel);
    sectionSummaryLabel->setObjectName(
        QStringLiteral("dialogueSectionSummaryLabel"));
    lineList = new QListWidget(linePanel);
    lineList->setObjectName(QStringLiteral("dialogueLineList"));
    lineLayout->addWidget(sectionSummaryLabel);
    lineLayout->addWidget(lineList, 1);

    auto* editorPanel = new QWidget(splitter);
    auto* editorLayout = new QVBoxLayout(editorPanel);
    auto* editGroup = new QGroupBox(editorPanel);
    editGroup->setObjectName(QStringLiteral("dialogueLineGroup"));
    auto* form = new QFormLayout(editGroup);
    speakerEdit = new QLineEdit(editGroup);
    speakerEdit->setObjectName(QStringLiteral("dialogueSpeakerEdit"));
    textEdit = new QPlainTextEdit(editGroup);
    textEdit->setObjectName(QStringLiteral("dialogueTextEdit"));
    textEdit->setMinimumHeight(150);
    portraitModeCombo = new QComboBox(editGroup);
    portraitModeCombo->setObjectName(
        QStringLiteral("dialoguePortraitModeCombo"));
    portraitReferenceCombo = new QComboBox(editGroup);
    portraitReferenceCombo->setObjectName(
        QStringLiteral("dialoguePortraitReferenceCombo"));
    portraitReferenceCombo->setEditable(true);
    portraitReferenceCombo->setInsertPolicy(QComboBox::NoInsert);
    form->addRow(QString(), speakerEdit);
    form->addRow(QString(), textEdit);
    form->addRow(QString(), portraitModeCombo);
    form->addRow(QString(), portraitReferenceCombo);

    auto* previewGroup = new QGroupBox(editorPanel);
    previewGroup->setObjectName(QStringLiteral("dialoguePortraitPreviewGroup"));
    auto* previewLayout = new QVBoxLayout(previewGroup);
    portraitPreview = new MpcPreviewLabel(previewGroup);
    portraitPreview->setObjectName(
        QStringLiteral("dialoguePortraitPreview"));
    portraitPreview->setMinimumSize(220, 180);
    portraitPreview->setFrameShape(QFrame::StyledPanel);
    previewLayout->addWidget(portraitPreview, 1);

    preservationLabel = new QLabel(editorPanel);
    preservationLabel->setObjectName(
        QStringLiteral("dialoguePreservationLabel"));
    preservationLabel->setWordWrap(true);
    callerHintLabel = new QLabel(editorPanel);
    callerHintLabel->setObjectName(QStringLiteral("dialogueCallerHintLabel"));
    callerHintLabel->setWordWrap(true);
    editorLayout->addWidget(editGroup);
    editorLayout->addWidget(previewGroup, 1);
    editorLayout->addWidget(preservationLabel);
    editorLayout->addWidget(callerHintLabel);

    splitter->addWidget(sectionPanel);
    splitter->addWidget(linePanel);
    splitter->addWidget(editorPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({230, 330, 520});

    speakerEdit->installEventFilter(this);
    textEdit->installEventFilter(this);
    portraitReferenceCombo->lineEdit()->installEventFilter(this);
}

void DialogueEditorWindow::setupActions()
{
    saveAction = toolBar->addAction(QString());
    saveAction->setObjectName(QStringLiteral("dialogueSaveAction"));
    saveAction->setShortcut(QKeySequence::Save);
    toolBar->addSeparator();
    undoAction = undoStack->createUndoAction(this);
    undoAction->setObjectName(QStringLiteral("dialogueUndoAction"));
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction = undoStack->createRedoAction(this);
    redoAction->setObjectName(QStringLiteral("dialogueRedoAction"));
    redoAction->setShortcut(QKeySequence::Redo);
    toolBar->addAction(undoAction);
    toolBar->addAction(redoAction);
    toolBar->addSeparator();
    playtestAction = toolBar->addAction(QString());
    playtestAction->setObjectName(QStringLiteral("dialoguePlaytestAction"));
    returnAction = toolBar->addAction(QString());
    returnAction->setObjectName(QStringLiteral("dialogueReturnAction"));
}

void DialogueEditorWindow::setupConnections()
{
    connect(saveAction, &QAction::triggered,
            this, &DialogueEditorWindow::saveFile);
    connect(playtestAction, &QAction::triggered,
            this, &DialogueEditorWindow::playtestRequested);
    connect(returnAction, &QAction::triggered, this,
        [this]()
        {
            if (hasCaller())
            {
                emit returnToCallerRequested(
                    callerScriptPath, callerLine, callerColumn);
            }
        });
    connect(searchEdit, &QLineEdit::textChanged, this,
        [this]()
        {
            commitPendingLineEditors();
            refreshSectionList();
        });
    connect(sectionList, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* current, QListWidgetItem*)
        {
            if (refreshing)
                return;
            commitPendingLineEditors();
            selectedSection = current
                ? current->data(Qt::UserRole).toString() : QString();
            selectedLineRow = -1;
            refreshLineList();
            updateWindowTitle();
            updateActionStates();
        });
    connect(lineList, &QListWidget::currentRowChanged, this,
        [this](int row)
        {
            if (refreshing)
                return;
            commitPendingLineEditors();
            selectedLineRow = row;
            refreshLineEditor();
            updateActionStates();
        });
    connect(portraitModeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int)
        {
            if (refreshing)
                return;
            portraitReferenceCombo->setEnabled(
                portraitModeCombo->currentData().toInt() ==
                static_cast<int>(DialoguePortraitMode::Reference));
            refreshPortraitPreview();
            commitPendingLineEditors();
        });
    connect(portraitReferenceCombo, &QComboBox::currentTextChanged,
            this, [this]()
            {
                if (!refreshing)
                    refreshPortraitPreview();
            });
    connect(undoStack, &QUndoStack::cleanChanged, this,
        [this]()
        {
            updateWindowTitle();
            updateActionStates();
            emit documentStatesChanged();
        });
}

void DialogueEditorWindow::retranslateDynamicUi()
{
    saveAction->setText(tr("保存"));
    saveAction->setToolTip(tr("保存当前对话"));
    undoAction->setText(tr("撤销"));
    redoAction->setText(tr("重做"));
    playtestAction->setText(tr("试玩当前对话"));
    playtestAction->setToolTip(tr("进入匹配地图并直接显示当前段落"));
    returnAction->setText(tr("返回调用位置"));
    returnAction->setToolTip(tr("回到打开这段对话的脚本行"));
    searchEdit->setPlaceholderText(tr("搜索段落或对话内容"));

    if (QLabel* label = findChild<QLabel*>(
            QStringLiteral("dialogueSectionListLabel")))
    {
        label->setText(tr("对话段落"));
    }
    if (QGroupBox* group = findChild<QGroupBox*>(
            QStringLiteral("dialogueLineGroup")))
    {
        group->setTitle(tr("当前一句"));
        if (QFormLayout* form = qobject_cast<QFormLayout*>(group->layout()))
        {
            if (QLabel* label = qobject_cast<QLabel*>(
                    form->labelForField(speakerEdit)))
                label->setText(tr("说话人"));
            if (QLabel* label = qobject_cast<QLabel*>(
                    form->labelForField(textEdit)))
                label->setText(tr("文本"));
            if (QLabel* label = qobject_cast<QLabel*>(
                    form->labelForField(portraitModeCombo)))
                label->setText(tr("头像"));
            if (QLabel* label = qobject_cast<QLabel*>(
                    form->labelForField(portraitReferenceCombo)))
                label->setText(tr("头像文件"));
        }
    }
    if (QGroupBox* group = findChild<QGroupBox*>(
            QStringLiteral("dialoguePortraitPreviewGroup")))
    {
        group->setTitle(tr("头像预览"));
    }

    const int previousMode = portraitModeCombo->currentData().toInt();
    const QSignalBlocker blocker(portraitModeCombo);
    portraitModeCombo->clear();
    portraitModeCombo->addItem(
        tr("沿用同侧上一句（通常为上上句）"),
        static_cast<int>(DialoguePortraitMode::KeepPrevious));
    portraitModeCombo->addItem(
        tr("不显示头像"),
        static_cast<int>(DialoguePortraitMode::NoPortrait));
    portraitModeCombo->addItem(
        tr("使用头像文件"),
        static_cast<int>(DialoguePortraitMode::Reference));
    const int modeIndex = portraitModeCombo->findData(previousMode);
    if (modeIndex >= 0)
        portraitModeCombo->setCurrentIndex(modeIndex);
    refreshLineEditor();
    updateActionStates();
}

bool DialogueEditorWindow::loadDocumentBytes(const QByteArray& bytes)
{
    const int wantedRow = selectedLineRow;
    QString error;
    if (!document.load(bytes, &error))
        return false;
    refreshSectionList();
    if (wantedRow >= 0 && wantedRow < lineList->count())
    {
        const QSignalBlocker blocker(lineList);
        selectedLineRow = wantedRow;
        lineList->setCurrentRow(wantedRow);
        refreshLineEditor();
    }
    updateWindowTitle();
    return true;
}

void DialogueEditorWindow::pushSnapshotChange(
    const QByteArray& before, const QByteArray& after,
    const QString& description)
{
    if (before == after)
        return;
    undoStack->push(new DialogueSnapshotCommand(
        before, after,
        [this](const QByteArray& bytes) { loadDocumentBytes(bytes); },
        description));
}

void DialogueEditorWindow::commitPendingLineEditors()
{
    if (refreshing || selectedSection.isEmpty() || selectedLineRow < 0 ||
        selectedLineRow >= document.lineCount(selectedSection))
    {
        return;
    }
    const QByteArray before = document.serializedBytes();
    document.setLine(selectedSection, selectedLineRow,
                     speakerEdit->text(), textEdit->toPlainText());
    const DialoguePortraitMode mode = static_cast<DialoguePortraitMode>(
        portraitModeCombo->currentData().toInt());
    if (mode != DialoguePortraitMode::Reference ||
        !portraitReferenceCombo->currentText().trimmed().isEmpty())
    {
        document.setPortrait(
            selectedSection, selectedLineRow, mode,
            portraitReferenceCombo->currentText());
    }
    const QByteArray after = document.serializedBytes();
    if (before == after)
        return;
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, tr("修改对话"));
}

void DialogueEditorWindow::refreshSectionList()
{
    const QString wantedSection = selectedSection;
    const QString query = searchEdit->text().trimmed();
    const QSignalBlocker blocker(sectionList);
    refreshing = true;
    sectionList->clear();
    int selectedRow = -1;
    for (const QString& section : document.sectionNames())
    {
        const DialogueLine first = document.line(section, 0);
        const QString searchable = section + QLatin1Char(' ') +
            first.speaker + QLatin1Char(' ') + first.text;
        if (!query.isEmpty() &&
            !searchable.contains(query, Qt::CaseInsensitive))
        {
            continue;
        }
        QString preview = first.text.simplified();
        if (preview.size() > 34)
            preview = preview.left(34) + QChar(0x2026);
        auto* item = new QListWidgetItem(
            preview.isEmpty() ? section
                              : tr("%1\n%2").arg(section, preview),
            sectionList);
        item->setData(Qt::UserRole, section);
        item->setToolTip(section);
        if (section.compare(wantedSection, Qt::CaseInsensitive) == 0)
            selectedRow = sectionList->count() - 1;
    }
    refreshing = false;
    if (selectedRow >= 0)
    {
        selectedSection = sectionList->item(selectedRow)->data(
            Qt::UserRole).toString();
        sectionList->setCurrentRow(selectedRow);
        refreshLineList();
    }
    else if (sectionList->count() > 0 && wantedSection.isEmpty())
    {
        selectedSection = sectionList->item(0)->data(
            Qt::UserRole).toString();
        sectionList->setCurrentRow(0);
        refreshLineList();
    }
    else if (selectedRow < 0)
    {
        selectedSection.clear();
        selectedLineRow = -1;
        refreshLineList();
    }
}

void DialogueEditorWindow::refreshLineList()
{
    const int wantedRow = selectedLineRow;
    const QSignalBlocker blocker(lineList);
    refreshing = true;
    lineList->clear();
    const int count = document.lineCount(selectedSection);
    for (int row = 0; row < count; ++row)
    {
        const DialogueLine value = document.line(selectedSection, row);
        QString text = value.text.simplified();
        if (text.size() > 48)
            text = text.left(48) + QChar(0x2026);
        const QString speaker = value.speaker.isEmpty()
            ? tr("旁白") : value.speaker;
        auto* item = new QListWidgetItem(
            tr("%1. %2：%3").arg(value.number).arg(speaker, text),
            lineList);
        item->setToolTip(value.text);
    }
    sectionSummaryLabel->setText(selectedSection.isEmpty()
        ? tr("请选择对话段落")
        : tr("%1 · %2 句").arg(selectedSection).arg(count));
    refreshing = false;
    if (count > 0)
    {
        selectedLineRow = wantedRow >= 0 && wantedRow < count
            ? wantedRow : 0;
        lineList->setCurrentRow(selectedLineRow);
        refreshLineEditor();
    }
    else
    {
        selectedLineRow = -1;
        refreshLineEditor();
    }
}

void DialogueEditorWindow::refreshLineEditor()
{
    refreshing = true;
    const DialogueLine value = document.line(
        selectedSection, selectedLineRow);
    const bool available = value.number > 0;
    speakerEdit->setEnabled(available);
    textEdit->setEnabled(available);
    portraitModeCombo->setEnabled(available);
    portraitReferenceCombo->setEnabled(available &&
        value.portraitMode == DialoguePortraitMode::Reference);
    speakerEdit->setText(value.speaker);
    textEdit->setPlainText(value.text);
    const int modeIndex = portraitModeCombo->findData(
        static_cast<int>(value.portraitMode));
    if (modeIndex >= 0)
        portraitModeCombo->setCurrentIndex(modeIndex);
    if (value.portraitMode == DialoguePortraitMode::Reference &&
        portraitReferenceCombo->findText(value.portraitReference,
                                         Qt::MatchFixedString) < 0)
    {
        portraitReferenceCombo->addItem(value.portraitReference);
    }
    portraitReferenceCombo->setCurrentText(value.portraitReference);
    preservationLabel->setText(filePath.isEmpty()
        ? tr("尚未打开对话文件")
        : tr("未显示的 %1 个字段和原有注释会在保存时保留。")
              .arg(document.hiddenFieldCount()));
    refreshing = false;
    refreshPortraitPreview();
}

void DialogueEditorWindow::refreshPortraitChoices()
{
    const QString current = portraitReferenceCombo->currentText();
    QSet<QString> choices;
    if (!current.trimmed().isEmpty())
        choices.insert(current.trimmed());
    const QStringList roots = {
        QStringLiteral("mpc/portrait"),
        QStringLiteral("asf/portrait")};
    for (const QString& root : roots)
    {
        QDir directory(QDir(assetsBasePath).filePath(root));
        if (!directory.exists())
            continue;
        const QFileInfoList files = directory.entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& file : files)
            choices.insert(file.fileName());
    }
    QStringList sorted = choices.values();
    sorted.sort(Qt::CaseInsensitive);
    const QSignalBlocker blocker(portraitReferenceCombo);
    portraitReferenceCombo->clear();
    portraitReferenceCombo->addItems(sorted);
    portraitReferenceCombo->setCurrentText(current);
}

void DialogueEditorWindow::refreshPortraitPreview()
{
    if (!portraitPreview)
        return;
    const DialoguePortraitMode mode = static_cast<DialoguePortraitMode>(
        portraitModeCombo->currentData().toInt());
    if (mode == DialoguePortraitMode::NoPortrait)
    {
        portraitPreview->clearImage(tr("本句不显示头像"));
        portraitPreview->setToolTip(QString());
        return;
    }
    QString reference;
    QString inheritedDescription;
    if (mode == DialoguePortraitMode::KeepPrevious)
    {
        const DialogueResolvedPortrait inherited = document.resolvedPortrait(
            selectedSection, selectedLineRow - 2);
        if (inherited.reference.isEmpty())
        {
            portraitPreview->clearImage(
                inherited.explicitlyHidden
                    ? tr("本句沿用同侧上一句\n该句不显示头像")
                    : tr("同侧此前没有可沿用的头像"));
            portraitPreview->setToolTip(QString());
            return;
        }
        reference = inherited.reference;
        inheritedDescription = tr("沿用第 %1 句头像")
            .arg(inherited.sourceRow + 1);
    }
    else
    {
        reference = portraitReferenceCombo->currentText().trimmed();
    }
    if (reference.isEmpty())
    {
        portraitPreview->clearImage(tr("请选择头像"));
        return;
    }
    for (const QString& candidate : portraitCandidates(reference))
    {
        AssetPreviewData preview;
        if (AssetPreviewLoader::load(assetsBasePath, candidate, &preview) &&
            preview.kind == AssetPreviewKind::Image)
        {
            portraitPreview->setSourceImage(preview.image);
            QString tooltip = tr("%1\n%2×%3，%4 帧")
                .arg(candidate)
                .arg(preview.imageWidth)
                .arg(preview.imageHeight)
                .arg(preview.frameCount);
            if (!inheritedDescription.isEmpty())
                tooltip.prepend(inheritedDescription + QLatin1Char('\n'));
            portraitPreview->setToolTip(tooltip);
            return;
        }
    }
    portraitPreview->clearImage(tr("未找到预览\n可继续保存引用"));
    portraitPreview->setToolTip(reference);
}

void DialogueEditorWindow::updateWindowTitle()
{
    const QString suffix = hasUnsavedChanges()
        ? QStringLiteral(" *") : QString();
    setWindowTitle(tr("对话 - %1%2").arg(displayName(), suffix));
}

void DialogueEditorWindow::updateActionStates()
{
    const bool opened = !filePath.isEmpty();
    saveAction->setEnabled(opened && hasUnsavedChanges());
    playtestAction->setEnabled(opened && !selectedSection.isEmpty());
    returnAction->setEnabled(hasCaller());
    callerHintLabel->setText(hasCaller()
        ? tr("完成后可返回打开这段对话的脚本位置。")
        : tr("从脚本或 NPC 进入时，可在这里返回调用位置。"));
}

bool DialogueEditorWindow::confirmSaveIfModified()
{
    commitPendingLineEditors();
    if (!hasUnsavedChanges())
        return true;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("保存更改"),
        tr("对话“%1”已修改，是否保存？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel)
        return false;
    return choice != QMessageBox::Save || saveFile();
}

QStringList DialogueEditorWindow::portraitCandidates(
    const QString& reference) const
{
    QString normalized = QDir::fromNativeSeparators(reference.trimmed());
    if (normalized.isEmpty())
        return {};
    QStringList candidates;
    auto append = [&candidates](const QString& candidate)
    {
        if (!candidate.isEmpty() &&
            !candidates.contains(candidate, Qt::CaseInsensitive))
        {
            candidates.append(candidate);
        }
    };
    if (normalized.startsWith(QStringLiteral("mpc/"), Qt::CaseInsensitive) ||
        normalized.startsWith(QStringLiteral("asf/"), Qt::CaseInsensitive))
    {
        append(normalized);
    }
    else
    {
        append(QStringLiteral("mpc/portrait/") + normalized);
        append(QStringLiteral("asf/portrait/") + normalized);
    }
    const QStringList originals = candidates;
    for (const QString& candidate : originals)
    {
        if (candidate.startsWith(QStringLiteral("mpc/"), Qt::CaseInsensitive))
        {
            append(replaceExtension(
                QStringLiteral("asf/") + candidate.mid(4),
                QStringLiteral(".asf")));
        }
        else if (candidate.startsWith(QStringLiteral("asf/"),
                                      Qt::CaseInsensitive))
        {
            append(replaceExtension(
                QStringLiteral("mpc/") + candidate.mid(4),
                QStringLiteral(".mpc")));
        }
    }
    return candidates;
}
