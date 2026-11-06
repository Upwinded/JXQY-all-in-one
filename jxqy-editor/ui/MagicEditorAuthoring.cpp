#include "MagicEditorWindow.h"
#include "ContentReferenceDialog.h"

#include "../core/AuthoringMutationGate.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"
#include "../core/GameProfile.h"

#include <QApplication>
#include <QScopeGuard>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>

namespace
{
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString quoted(QString text)
{
    text.replace('\\', "\\\\");
    text.replace('"', "\\\"");
    return '"' + text + '"';
}
}

void MagicEditorWindow::setDocumentSnapshotsProvider(
    std::function<QList<DesktopRunDocumentSnapshot>()> provider)
{
    documentSnapshotsProvider = std::move(provider);
}

void MagicEditorWindow::setFileMutationPreflight(
    std::function<bool(const QStringList&, QString&)> preflight)
{
    fileMutationPreflight = std::move(preflight);
}

bool MagicEditorWindow::authoringTarget(const QString& target, QString& error) const
{
    const QString directory = QDir(assetsBasePath).filePath("ini/magic");
    const auto resolution = ResourcePackScanner::resolveContentRoots(assetsBasePath);
    if (assetsBasePath.isEmpty() || !QFileInfo(assetsBasePath).isDir() ||
        !isMagicFilePath(target) || !EditorAssetPath::isLexicallyInside(directory, target) ||
        !EditorAssetPath::isInside(assetsBasePath, target))
    {
        error = tr("请选择项目资源后再编辑武功。文件必须位于当前项目的武功目录。");
        return false;
    }
    if (!resolution.recoveryErrors.isEmpty())
    {
        error = resolution.recoveryErrors.join('\n');
        return false;
    }
    if (documentPathValidator && !documentPathValidator(filePath, target))
    {
        error = tr("目标文件已经在其它编辑页打开，请先处理该编辑页。");
        return false;
    }
    return true;
}

bool MagicEditorWindow::mutationReady(const QStringList& paths, QString& error) const
{
    return !fileMutationPreflight || fileMutationPreflight(paths, error);
}

QString MagicEditorWindow::createMagicFile(
    const QString& name, bool copyCurrent, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    QString error;
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || cleanName.contains('\n') || cleanName.contains('\r'))
    {
        if (errorMessage) *errorMessage = tr("请填写武功名称，不能包含换行。");
        return {};
    }
    if (copyCurrent && filePath.isEmpty())
    {
        if (errorMessage) *errorMessage = tr("请先选择要复制的武功。");
        return {};
    }
    if (copyCurrent && !linkedInputsValid())
    {
        if (errorMessage) *errorMessage = tr("请检查关联路径和参数：文件使用相对路径，数值使用非负整数，概率为0至100。当前输入已保留。");
        return {};
    }
    if (copyCurrent)
        commitPendingTextEditors();
    MagicDocument copy;
    if (!copy.load(copyCurrent ? document.serializedBytes() : ContentAuthoring::newDocument(ContentKind::Magic, cleanName), &error))
    {
        if (errorMessage) *errorMessage = error;
        return {};
    }
    copy.setText(MagicTextField::Name, cleanName);
    MagicDocument check;
    if (!check.load(copy.serializedBytes(), &error) || check.text(MagicTextField::Name) != cleanName)
    {
        if (errorMessage) *errorMessage = tr("这个名称无法完整保存，请换一个名称。");
        return {};
    }
    const QString target = QDir(assetsBasePath).filePath(
        "ini/magic/" + ContentAuthoring::unusedFileName(ContentKind::Magic, assetsBasePath, cleanName));
    if (!authoringTarget(target, error) || !QDir().mkpath(QFileInfo(target).path()))
    {
        if (errorMessage) *errorMessage = error.isEmpty() ? tr("无法创建武功目录。") : error;
        return {};
    }
    {
        DurableFileTransaction transaction(assetsBasePath);
        if (!transaction.addBytesWriteChecked(target, copy.serializedBytes(), false, {}, error) ||
            !transaction.commit(error))
        {
            if (errorMessage) *errorMessage = error;
            return {};
        }
    }
    refreshDocumentList();
    emit filesChanged({target});
    return target;
}

ContentReferenceReport MagicEditorWindow::currentReferences() const
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto restoreCursor = qScopeGuard([]() { QApplication::restoreOverrideCursor(); });
    auto snapshots = documentSnapshotsProvider
        ? documentSnapshotsProvider() : QList<DesktopRunDocumentSnapshot>();
    snapshots.append(desktopRunDocumentSnapshot());
    return ContentAuthoring::references(ContentKind::Magic, assetsBasePath,
        ContentAuthoring::referenceName(ContentKind::Magic, assetsBasePath, filePath), snapshots);
}

bool MagicEditorWindow::renameMagicFile(const QString& newFileName, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    commitPendingTextEditors();
    QString error;
    auto fail = [&](const QString& message)
    {
        if (errorMessage) *errorMessage = message;
        return false;
    };
    QString name = newFileName.normalized(QString::NormalizationForm_C).trimmed().toLower();
    if (!name.endsWith(".ini", Qt::CaseInsensitive)) name += ".ini";
    if (name.contains(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\r\\n]"))) ||
        name == ".ini" || name.startsWith('.') || name.size() > 120 ||
        QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])\\.", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
        return fail(tr("请输入有效的武功文件名，不包含目录或特殊字符。"));
    const QString target = QDir(QFileInfo(filePath).path()).filePath(name);
    if (!authoringTarget(target, error)) return fail(error);
    const QString newName = QDir(QDir(assetsBasePath).filePath("ini/magic")).relativeFilePath(target);
    if (ProjectDocumentRegistry::documentPathKey(target) == ProjectDocumentRegistry::documentPathKey(filePath))
        return fail(tr("新文件名与当前文件名相同。"));
    for (const auto& resource : ContentAuthoring::resources(ContentKind::Magic, assetsBasePath))
        if (resource.fileName.compare(newName, Qt::CaseInsensitive) == 0)
            return fail(tr("这个文件名已经存在，请换一个名称。"));
    if (loadedSourcePath != filePath || readBytes(filePath) != loadedBytes)
        return fail(tr("当前文件来自基础资源或已被外部修改。请复制为新武功，或重新打开后再操作。"));
    auto report = currentReferences();
    if (!report.errors.isEmpty())
        return fail(tr("部分文件无法按当前文本格式读取，不能安全更新引用：\n%1").arg(report.errors.join('\n')));
    QStringList changed = report.fileBytes.keys();
    changed.append(filePath);
    if (!mutationReady(changed, error)) return fail(error);
    // Preflight may have saved open callers. Re-scan those exact saved bytes before publication.
    report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，未更新引用。"));
    changed = report.fileBytes.keys();
    changed.append(filePath);
    if (!mutationReady(changed, error)) return fail(error);
    QByteArray renamedBytes = document.serializedBytes();
    {
        DurableFileTransaction transaction(assetsBasePath);
        for (auto file = report.fileBytes.cbegin(); file != report.fileBytes.cend(); ++file)
        {
            QList<ContentReferenceOccurrence> occurrences;
            for (const auto& occurrence : report.occurrences)
                if (occurrence.filePath == file.key()) occurrences.append(occurrence);
            const QByteArray bytes = ContentAuthoring::replaceReferences(ContentKind::Magic, file.value(), occurrences, newName);
            if (file.key() == filePath) renamedBytes = bytes;
            else if (!transaction.addBytesWriteChecked(file.key(), bytes,
                QFileInfo(file.key()).isFile(), report.diskBytes.value(file.key()), error))
                return fail(error);
        }
        if (!transaction.addBytesWriteChecked(target, renamedBytes, false, {}, error) ||
            !transaction.addRemovalChecked(filePath, loadedBytes, error) || !transaction.commit(error))
            return fail(error);
    }
    filePath.clear();
    undoStack->clear();
    if (!openFile(target)) return fail(tr("文件已改名，请重新打开武功列表。"));
    changed.append(target);
    emit filesChanged(changed);
    return true;
}

bool MagicEditorWindow::deleteMagicFile(QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    commitPendingTextEditors();
    QString error;
    auto fail = [&](const QString& message)
    {
        if (errorMessage) *errorMessage = message;
        return false;
    };
    if (!authoringTarget(filePath, error) || loadedSourcePath != filePath)
        return fail(error.isEmpty() ? tr("只能删除当前项目中的武功文件。") : error);
    if (readBytes(filePath) != loadedBytes) return fail(tr("文件已被外部修改，请重新打开后再删除。"));
    const auto report = currentReferences();
    const bool restoresBase = !ContentAuthoring::fallback(assetsBasePath,
        "ini/magic/" + ContentAuthoring::referenceName(ContentKind::Magic, assetsBasePath, filePath)).isEmpty();
    for (const auto& occurrence : report.occurrences)
        if (!restoresBase && occurrence.filePath != filePath)
            return fail(tr("这个武功仍被使用：\n%1:%2\n请先修改引用，再删除。")
                .arg(occurrence.filePath).arg(occurrence.line));
    if (!report.errors.isEmpty()) return fail(tr("有文件无法读取，请先处理引用检查中的问题。"));
    if (!mutationReady({filePath}, error)) return fail(error);
    auto lease = AuthoringMutationGate::instance().acquireMutationLeaseForPath(filePath);
    if (!lease) return fail(tr("资源正在保存，请稍后重试。"));
    if (readBytes(filePath) != loadedBytes) return fail(tr("文件已被外部修改，请重新打开后再删除。"));
    if (!QFile::moveToTrash(filePath)) return fail(tr("无法移入系统回收站，原文件已保留。"));
    lease.release();
    const QString oldPath = filePath;
    document = MagicDocument();
    filePath.clear();
    loadedSourcePath.clear();
    loadedBytes.clear();
    undoStack->clear();
    refreshDocumentList();
    refreshFromDocument();
    if (restoresBase) openFile(oldPath);
    emit documentStatesChanged();
    emit filesChanged({oldPath});
    return true;
}

void MagicEditorWindow::setupAuthoringActions()
{
    newAction = toolBar->addAction(QString());
    newAction->setObjectName("magicNewAction");
    duplicateAction = toolBar->addAction(QString());
    duplicateAction->setObjectName("magicDuplicateAction");
    toolBar->addSeparator();
    renameAction = new QAction(this);
    renameAction->setObjectName("magicRenameAction");
    deleteAction = new QAction(this);
    deleteAction->setObjectName("magicDeleteAction");
    referencesAction = new QAction(this);
    referencesAction->setObjectName("magicReferencesAction");
    learningScriptAction = new QAction(this);
    learningScriptAction->setObjectName("magicLearningScriptAction");
    documentList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(documentList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& point)
    {
        if (auto* item = documentList->itemAt(point))
        {
            const QString path = item->data(Qt::UserRole).toString();
            if (path != filePath && !openFile(path)) return;
        }
        QMenu menu(this);
        menu.addAction(newAction);
        menu.addAction(duplicateAction);
        menu.addSeparator();
        menu.addAction(learningScriptAction);
        menu.addAction(referencesAction);
        menu.addSeparator();
        menu.addAction(renameAction);
        menu.addAction(deleteAction);
        menu.exec(documentList->viewport()->mapToGlobal(point));
    });
    connect(newAction, &QAction::triggered, this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("新建武功"), tr("武功名称"), QLineEdit::Normal, {}, &accepted);
        if (!accepted) return;
        QString error;
        const QString path = createMagicFile(name, false, &error);
        if (path.isEmpty()) QMessageBox::warning(this, tr("无法新建武功"), error);
        else if (filePath.isEmpty()) openFile(path);
        else emit openMagicFileRequested(path);
    });
    connect(duplicateAction, &QAction::triggered, this, [this]()
    {
        commitPendingTextEditors();
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("复制武功"), tr("新武功名称"), QLineEdit::Normal,
            displayName() + tr("副本"), &accepted);
        if (!accepted) return;
        QString error;
        const QString path = createMagicFile(name, true, &error);
        if (path.isEmpty()) QMessageBox::warning(this, tr("无法复制武功"), error);
        else emit openMagicFileRequested(path);
    });
    connect(referencesAction, &QAction::triggered, this, &MagicEditorWindow::showReferences);
    connect(learningScriptAction, &QAction::triggered, this, [this]()
    {
        const QString name = ContentAuthoring::referenceName(ContentKind::Magic, assetsBasePath, filePath);
        const QString source = QStringLiteral("addmagic(%1)\nsetmagiclevel(%1, %2)\n")
            .arg(quoted(name)).arg(selectedLevel());
        emit learningScriptRequested(source);
    });
    connect(renameAction, &QAction::triggered, this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("重命名武功文件"),
            tr("新文件名（显示名称在右侧属性中修改；已识别的引用会一起更新）"),
            QLineEdit::Normal, QFileInfo(filePath).fileName(), &accepted);
        if (!accepted) return;
        const auto report = currentReferences();
        QString message = tr("将更新 %1 处明确引用。是否继续？").arg(report.occurrences.size());
        if (!report.dynamicReferences.isEmpty()) message += tr("\n另有 %1 处动态武功调用需自行检查，不会自动改写。").arg(report.dynamicReferences.size());
        QMessageBox confirmation(QMessageBox::Question, tr("确认重命名"), message,
            QMessageBox::Yes | QMessageBox::No, this);
        QStringList locations;
        for (const auto& occurrence : report.occurrences)
            locations.append(QStringLiteral("%1:%2  %3")
                .arg(QDir(assetsBasePath).relativeFilePath(occurrence.filePath))
                .arg(occurrence.line).arg(occurrence.context));
        confirmation.setDetailedText(locations.join('\n'));
        confirmation.setDefaultButton(QMessageBox::No);
        if (confirmation.exec() != QMessageBox::Yes) return;
        QString error;
        if (!renameMagicFile(name, &error) && !error.isEmpty()) QMessageBox::warning(this, tr("无法重命名"), error);
    });
    connect(deleteAction, &QAction::triggered, this, [this]()
    {
        const auto report = currentReferences();
        const bool restoresBase = !ContentAuthoring::fallback(assetsBasePath,
            "ini/magic/" + ContentAuthoring::referenceName(ContentKind::Magic, assetsBasePath, filePath)).isEmpty();
        for (const auto& occurrence : report.occurrences)
        {
            if (!restoresBase && occurrence.filePath != filePath)
            {
                showReferences();
                return;
            }
        }
        QString message = tr("将“%1”移入系统回收站。未保存的修改也会放弃。是否继续？").arg(displayName());
        if (restoresBase) message += tr("\n删除的是当前项目版本；基础资源中的同名武功会重新生效。");
        if (!report.dynamicReferences.isEmpty()) message += tr("\n另有 %1 处动态武功调用需自行检查，不会自动改写。").arg(report.dynamicReferences.size());
        if (QMessageBox::question(this, tr("删除武功"), message, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        QString error;
        if (!deleteMagicFile(&error)) QMessageBox::warning(this, tr("无法删除武功"), error);
    });
    retranslateAuthoringActions();
}

void MagicEditorWindow::retranslateAuthoringActions()
{
    if (!newAction) return;
    newAction->setText(tr("新建"));
    duplicateAction->setText(tr("复制"));
    renameAction->setText(tr("重命名文件…"));
    deleteAction->setText(tr("删除武功…"));
    referencesAction->setText(tr("查看引用…"));
    learningScriptAction->setText(tr("创建学习脚本"));
    if (moreButton) moreButton->setText(tr("更多操作…"));
}

void MagicEditorWindow::setupAuthoringMenu()
{
    auto* menu = new QMenu(this);
    menu->addAction(learningScriptAction);
    menu->addAction(referencesAction);
    menu->addSeparator();
    menu->addAction(renameAction);
    menu->addAction(deleteAction);
    moreButton = new QToolButton(this);
    moreButton->setObjectName("magicMoreButton");
    moreButton->setPopupMode(QToolButton::InstantPopup);
    moreButton->setMenu(menu);
    toolBar->addSeparator();
    toolBar->addWidget(moreButton);
    retranslateAuthoringActions();
}

void MagicEditorWindow::showReferences()
{
    showContentReferences(this, assetsBasePath, currentReferences(), [this](const QString& path, int line)
    {
        emit openReferenceRequested(path, line);
    });
}
