#include "DialogueEditorWindow.h"
#include "EditorSaveConfirmation.h"

#include "ContentReferenceDialog.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"

#include <QApplication>
#include <QScopeGuard>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>

void DialogueEditorWindow::setDocumentSnapshotsProvider(std::function<QList<DesktopRunDocumentSnapshot>()> provider)
{
    snapshotsProvider = std::move(provider);
}

void DialogueEditorWindow::setFileMutationPreflight(std::function<bool(const QStringList&, QString&)> preflight)
{
    mutationPreflight = std::move(preflight);
}

QByteArray DialogueEditorWindow::pendingDocumentBytes() const
{
    if (refreshing || selectedSection.isEmpty() || selectedLineRow < 0) return document.serializedBytes();
    DialogueDocument copy = document;
    copy.setLine(selectedSection, selectedLineRow, speakerEdit->text(), textEdit->toPlainText());
    const auto mode = static_cast<DialoguePortraitMode>(portraitModeCombo->currentData().toInt());
    if (mode != DialoguePortraitMode::Reference || !portraitReferenceCombo->currentText().trimmed().isEmpty())
        copy.setPortrait(selectedSection, selectedLineRow, mode, portraitReferenceCombo->currentText());
    return copy.serializedBytes();
}

bool DialogueEditorWindow::writeDocument(const QByteArray& bytes, QString& error)
{
    if (assetsBasePath.isEmpty()) return document.saveFile(filePath, &error);
    if (!isDialogueFilePath(filePath) || !EditorAssetPath::isLexicallyInside(assetsBasePath, filePath) ||
        !EditorAssetPath::isInside(assetsBasePath, filePath) || !QDir().mkpath(QFileInfo(filePath).path()))
    {
        error = tr("无法写入当前项目的对话目录。");
        return false;
    }
    {
        DurableFileTransaction transaction(assetsBasePath);
        if (!EditorSaveConfirmation::prepareBytesWrite(this, transaction, filePath, bytes, loadedSourcePath == filePath, loadedBytes, error) ||
            !transaction.commit(error)) return false;
    }
    loadedSourcePath = filePath;
    loadedBytes = bytes;
    return true;
}

bool DialogueEditorWindow::createSection(const QString& name, bool copyCurrent, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    commitPendingLineEditors();
    const QByteArray before = document.serializedBytes();
    const bool added = !filePath.isEmpty() && (copyCurrent
        ? document.copySection(selectedSection, name) : document.addSection(name, tr("新台词")));
    if (!added)
    {
        if (errorMessage) *errorMessage = tr("段落名称不能为空、不能重复，也不能使用 name、方括号或换行。");
        return false;
    }
    const QByteArray after = document.serializedBytes();
    loadDocumentBytes(before);
    selectedSection = name.trimmed();
    selectedLineRow = 0;
    const QSignalBlocker blockSearch(searchEdit);
    searchEdit->clear();
    pushSnapshotChange(before, after, copyCurrent ? tr("复制段落") : tr("新建段落"));
    updateActionStates();
    emit documentStatesChanged();
    return true;
}

ContentReferenceReport DialogueEditorWindow::currentReferences() const
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto restoreCursor = qScopeGuard([]() { QApplication::restoreOverrideCursor(); });
    auto snapshots = snapshotsProvider ? snapshotsProvider() : QList<DesktopRunDocumentSnapshot>();
    snapshots.append(desktopRunDocumentSnapshot());
    return ContentAuthoring::references(ContentKind::Dialogue, assetsBasePath, selectedSection, snapshots,
        QDir(assetsBasePath).relativeFilePath(filePath));
}

bool DialogueEditorWindow::renameSection(const QString& name, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return false; };
    DialogueDocument renamed;
    if (filePath.isEmpty() || !renamed.load(pendingDocumentBytes()) || !renamed.renameSection(selectedSection, name))
        return fail(tr("段落名称不能为空、不能重复，也不能使用 name、方括号或换行。"));
    auto report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，不能安全更新引用，请先查看引用。"));
    QStringList changed = report.fileBytes.keys();
    changed.append(filePath);
    QString error;
    if (mutationPreflight && !mutationPreflight(changed, error)) return fail(error);
    // Preflight may have saved open callers. Re-scan those exact saved bytes before publication.
    report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，未更新引用。"));
    changed = report.fileBytes.keys();
    changed.append(filePath);
    if (mutationPreflight && !mutationPreflight(changed, error)) return fail(error);
    if (assetsBasePath.isEmpty() || !EditorAssetPath::isInside(assetsBasePath, filePath) ||
        !QDir().mkpath(QFileInfo(filePath).path())) return fail(tr("无法写入当前项目的对话目录。"));
    {
        DurableFileTransaction transaction(assetsBasePath);
        for (auto file = report.fileBytes.cbegin(); file != report.fileBytes.cend(); ++file)
        {
            QList<ContentReferenceOccurrence> occurrences;
            for (const auto& occurrence : report.occurrences)
                if (occurrence.filePath == file.key()) occurrences.append(occurrence);
            const QByteArray bytes = ContentAuthoring::replaceReferences(ContentKind::Dialogue, file.value(), occurrences, name.trimmed());
            if (!transaction.addBytesWriteChecked(file.key(), bytes, QFileInfo(file.key()).isFile(),
                    report.diskBytes.value(file.key()), error)) return fail(error);
        }
        if (!transaction.addBytesWriteChecked(filePath, renamed.serializedBytes(), loadedSourcePath == filePath,
            loadedBytes, error) || !transaction.commit(error)) return fail(error);
    }
    document = std::move(renamed);
    loadedSourcePath = filePath;
    loadedBytes = document.serializedBytes();
    selectedSection = name.trimmed();
    undoStack->clear();
    refreshSectionList();
    updateWindowTitle();
    updateActionStates();
    emit documentStatesChanged();
    emit filesChanged(changed);
    return true;
}

bool DialogueEditorWindow::deleteSection(QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    const auto report = currentReferences();
    if (!report.occurrences.isEmpty() || !report.errors.isEmpty())
    {
        if (errorMessage) *errorMessage = tr("段落仍被引用，或部分文件无法读取，请先查看并处理引用。");
        return false;
    }
    commitPendingLineEditors();
    const QByteArray before = document.serializedBytes();
    if (!document.removeSection(selectedSection)) return false;
    const QByteArray after = document.serializedBytes();
    loadDocumentBytes(before);
    selectedSection.clear();
    selectedLineRow = -1;
    pushSnapshotChange(before, after, tr("删除段落"));
    updateActionStates();
    return true;
}

bool DialogueEditorWindow::applyLineOrder(const std::function<bool()>& mutation, int selectedRow, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    commitPendingLineEditors();
    const QByteArray before = document.serializedBytes();
    if (!document.canReorderLines(selectedSection) || !mutation())
    {
        if (errorMessage) *errorMessage = tr("无法调整台词。段落至少保留一句；含不连续编号的旧段落须先整理编号。");
        return false;
    }
    const QByteArray after = document.serializedBytes();
    loadDocumentBytes(before);
    selectedLineRow = selectedRow;
    pushSnapshotChange(before, after, tr("调整台词"));
    updateActionStates();
    return true;
}

bool DialogueEditorWindow::insertLine(int row, QString* errorMessage)
{
    return applyLineOrder([&]() { return document.insertLine(selectedSection, row, tr("新台词")); }, row, errorMessage);
}

bool DialogueEditorWindow::removeLine(int row, QString* errorMessage)
{
    return applyLineOrder([&]() { return document.removeLine(selectedSection, row); }, qMax(0, row - 1), errorMessage);
}

bool DialogueEditorWindow::moveLine(int from, int to, QString* errorMessage)
{
    return applyLineOrder([&]() { return document.moveLine(selectedSection, from, to); }, to, errorMessage);
}

void DialogueEditorWindow::showReferences()
{
    showContentReferences(this, assetsBasePath, currentReferences(), [this](const QString& path, int line)
    {
        emit openReferenceRequested(path, line);
    });
}

void DialogueEditorWindow::setupAuthoringActions()
{
    newSectionAction = toolBar->addAction(QString());
    newSectionAction->setObjectName("dialogueNewSectionAction");
    copySectionAction = toolBar->addAction(QString());
    copySectionAction->setObjectName("dialogueCopySectionAction");
    auto* menu = new QMenu(this);
    usageScriptAction = menu->addAction(QString());
    referencesAction = menu->addAction(QString());
    menu->addSeparator();
    renameSectionAction = menu->addAction(QString());
    deleteSectionAction = menu->addAction(QString());
    auto* more = new QToolButton(this);
    more->setObjectName("dialogueMoreButton");
    more->setPopupMode(QToolButton::InstantPopup);
    more->setMenu(menu);
    toolBar->addSeparator();
    auto* lines = findChild<QToolBar*>("dialogueLineToolBar");
    insertLineAction = lines->addAction(QString());
    insertLineAction->setObjectName("dialogueInsertLineAction");
    removeLineAction = lines->addAction(QString());
    removeLineAction->setObjectName("dialogueRemoveLineAction");
    moveLineUpAction = lines->addAction(QString());
    moveLineUpAction->setObjectName("dialogueMoveLineUpAction");
    moveLineDownAction = lines->addAction(QString());
    moveLineDownAction->setObjectName("dialogueMoveLineDownAction");
    connect(newSectionAction, &QAction::triggered, this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("新建段落"), tr("段落名称（供脚本选择）"), QLineEdit::Normal, {}, &accepted);
        QString error;
        if (accepted && !createSection(name, false, &error)) QMessageBox::warning(this, tr("无法新建段落"), error);
    });
    connect(copySectionAction, &QAction::triggered, this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("复制段落"), tr("新段落名称"), QLineEdit::Normal,
            selectedSection + tr("副本"), &accepted);
        QString error;
        if (accepted && !createSection(name, true, &error)) QMessageBox::warning(this, tr("无法复制段落"), error);
    });
    connect(renameSectionAction, &QAction::triggered, this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("重命名段落"), tr("新段落名称"), QLineEdit::Normal, selectedSection, &accepted);
        if (!accepted) return;
        const auto report = currentReferences();
        QMessageBox confirm(QMessageBox::Question, tr("确认重命名"),
            tr("将保存对话，并更新同地图下 %1 处明确调用。公共脚本及动态调用须先检查，不会自动改写。是否继续？").arg(report.occurrences.size()),
            QMessageBox::Yes | QMessageBox::No, this);
        QStringList locations;
        for (const auto& occurrence : report.occurrences)
            locations.append(QStringLiteral("%1:%2").arg(QDir(assetsBasePath).relativeFilePath(occurrence.filePath)).arg(occurrence.line));
        confirm.setDetailedText(locations.join('\n'));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        QString error;
        if (!renameSection(name, &error) && !error.isEmpty()) QMessageBox::warning(this, tr("无法重命名段落"), error);
    });
    connect(deleteSectionAction, &QAction::triggered, this, [this]()
    {
        const auto report = currentReferences();
        if (!report.occurrences.isEmpty()) { showReferences(); return; }
        QString message = tr("删除当前段落及其全部台词，可通过撤销恢复。是否继续？");
        if (!report.dynamicReferences.isEmpty()) message += tr("\n公共脚本或动态调用可能使用此段落，请先查看引用。");
        if (QMessageBox::question(this, tr("删除段落"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!deleteSection(&error)) QMessageBox::warning(this, tr("无法删除段落"), error);
    });
    connect(referencesAction, &QAction::triggered, this, &DialogueEditorWindow::showReferences);
    connect(usageScriptAction, &QAction::triggered, this, [this]()
    {
        QString section = selectedSection;
        section.replace('\\', "\\\\");
        section.replace('"', "\\\"");
        emit usageScriptRequested(QStringLiteral("talk(\"%1\")\n").arg(section));
    });
    auto runLineAction = [this](const std::function<bool(QString*)>& operation)
    {
        QString error;
        if (!operation(&error)) QMessageBox::warning(this, tr("无法调整台词"), error);
    };
    connect(insertLineAction, &QAction::triggered, this, [this, runLineAction]()
    { runLineAction([this](QString* error) { return insertLine(qMax(0, selectedLineRow + 1), error); }); });
    connect(removeLineAction, &QAction::triggered, this, [this, runLineAction]()
    { runLineAction([this](QString* error) { return removeLine(selectedLineRow, error); }); });
    connect(moveLineUpAction, &QAction::triggered, this, [this, runLineAction]()
    { runLineAction([this](QString* error) { return moveLine(selectedLineRow, selectedLineRow - 1, error); }); });
    connect(moveLineDownAction, &QAction::triggered, this, [this, runLineAction]()
    { runLineAction([this](QString* error) { return moveLine(selectedLineRow, selectedLineRow + 1, error); }); });
}

void DialogueEditorWindow::retranslateAuthoringActions()
{
    if (!newSectionAction) return;
    newSectionAction->setText(tr("新建段落"));
    copySectionAction->setText(tr("复制段落"));
    usageScriptAction->setText(tr("创建调用脚本"));
    referencesAction->setText(tr("查看引用…"));
    renameSectionAction->setText(tr("重命名段落…"));
    deleteSectionAction->setText(tr("删除段落…"));
    if (auto* more = findChild<QToolButton*>("dialogueMoreButton")) more->setText(tr("更多操作…"));
    insertLineAction->setText(tr("插入台词"));
    removeLineAction->setText(tr("删除"));
    removeLineAction->setToolTip(tr("删除当前一句；段落至少保留一句，可通过撤销恢复。"));
    moveLineUpAction->setText(tr("上移"));
    moveLineDownAction->setText(tr("下移"));
}
