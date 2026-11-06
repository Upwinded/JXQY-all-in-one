#include "ScriptEditorWindow.h"
#include "ui_ScriptEditorWindow.h"
#include "ContentReferenceDialog.h"
#include "../core/AuthoringMutationGate.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"

#include <QApplication>
#include <QScopeGuard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QToolButton>

namespace
{
QByteArray readScriptBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool validScriptName(const QString& name)
{
    return !name.isEmpty() && !name.startsWith('.') && !name.endsWith('.') && name.size() <= 120 &&
        !name.contains(QRegularExpression("[\\\\/:*?\"<>|\\r\\n]")) &&
        !QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\.|$)",
            QRegularExpression::CaseInsensitiveOption).match(name).hasMatch();
}
}

void ScriptEditorWindow::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path.isEmpty() ? QString() : EditorAssetPath::normalizedAbsolutePath(path);
    if (suggestedSavePath.isEmpty() && !assetsBasePath.isEmpty())
        suggestedSavePath = QDir(assetsBasePath).filePath("script/common/new-script.lua");
}

void ScriptEditorWindow::setDocumentSnapshotsProvider(std::function<QList<DesktopRunDocumentSnapshot>()> provider)
{
    snapshotsProvider = std::move(provider);
}

void ScriptEditorWindow::setFileMutationPreflight(std::function<bool(const QStringList&, QString&)> preflight)
{
    mutationPreflight = std::move(preflight);
}

QString ScriptEditorWindow::createContentFile(const QString& input, bool copyCurrent, QString* errorMessage)
{
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return QString(); };
    if (errorMessage) errorMessage->clear();
    QString name = input.normalized(QString::NormalizationForm_C).trimmed();
    if (!validScriptName(name)) return fail(tr("请输入单行名称，不包含目录或特殊字符。"));
    if (!name.endsWith(".lua", Qt::CaseInsensitive) && !name.endsWith(".txt", Qt::CaseInsensitive)) name += ".lua";
    if (assetsBasePath.isEmpty() || !QFileInfo(assetsBasePath).isDir()) return fail(tr("请先打开项目并选择资源。"));
    QString directory = QDir(assetsBasePath).filePath("script/common");
    if (copyCurrent && !ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName).isEmpty())
        directory = QFileInfo(ContentAuthoring::projectPath(assetsBasePath, currentFileName)).path();
    QSet<QString> names;
    const QString relativeDirectory = QDir(assetsBasePath).relativeFilePath(directory);
    const QStringList scope = relativeDirectory.split('/');
    for (const auto& entry : ContentAuthoring::resources(ContentKind::Script, assetsBasePath))
        if (scope.size() < 3 || scope[1].compare("map", Qt::CaseInsensitive) != 0 ||
            entry.fileName.startsWith("map/" + scope[2] + '/', Qt::CaseInsensitive) ||
            entry.fileName.startsWith("goods/", Qt::CaseInsensitive) || entry.fileName.startsWith("common/", Qt::CaseInsensitive))
            names.insert(QFileInfo(entry.fileName).fileName().toCaseFolded());
    const QFileInfo requested(name);
    const QString stem = requested.completeBaseName();
    const QString extension = requested.suffix();
    for (int suffix = 2; names.contains(name.toCaseFolded()) || QFileInfo(QDir(directory).filePath(name)).exists(); ++suffix)
        name = stem + '_' + QString::number(suffix) + '.' + extension;
    const QString target = QDir(directory).filePath(name);
    if (!EditorAssetPath::isInside(assetsBasePath, target) || !QDir().mkpath(directory))
        return fail(tr("无法创建项目脚本目录。"));
    QString error;
    DurableFileTransaction transaction(assetsBasePath);
    const QByteArray bytes = copyCurrent ? desktopRunDocumentSnapshot().bytes : QByteArray();
    if (!transaction.addBytesWriteChecked(target, bytes, false, {}, error) || !transaction.commit(error)) return fail(error);
    emit filesChanged({target});
    return target;
}

ContentReferenceReport ScriptEditorWindow::currentReferences() const
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto restoreCursor = qScopeGuard([]() { QApplication::restoreOverrideCursor(); });
    auto snapshots = snapshotsProvider ? snapshotsProvider() : QList<DesktopRunDocumentSnapshot>();
    snapshots.append(desktopRunDocumentSnapshot());
    return ContentAuthoring::references(ContentKind::Script, assetsBasePath,
        ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName), snapshots);
}

bool ScriptEditorWindow::renameContentFile(const QString& input, QString* errorMessage)
{
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return false; };
    if (errorMessage) errorMessage->clear();
    QString name = input.normalized(QString::NormalizationForm_C).trimmed();
    if (!validScriptName(name)) return fail(tr("请输入有效的文件名，不包含目录或特殊字符。"));
    if (!name.endsWith(".lua", Qt::CaseInsensitive) && !name.endsWith(".txt", Qt::CaseInsensitive))
        name += '.' + QFileInfo(currentFileName).suffix();
    if (ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName).isEmpty() ||
        !EditorAssetPath::isInside(assetsBasePath, currentFileName) ||
        loadedSourcePath != currentFileName || readScriptBytes(currentFileName) != loadedSourceBytes)
        return fail(tr("只能改名当前项目中未被外部修改的脚本；基础脚本请先复制。"));
    const QString oldReference = ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName);
    const QStringList scope = oldReference.split('/');
    const bool mapLocal = scope.size() >= 3 && scope[0].compare("map", Qt::CaseInsensitive) == 0;
    for (const auto& entry : ContentAuthoring::resources(ContentKind::Script, assetsBasePath))
    {
        const QString existing = QFileInfo(entry.fileName).fileName();
        if (existing.compare(name, Qt::CaseInsensitive) == 0 && (!mapLocal ||
            entry.fileName.startsWith("map/" + scope[1] + '/', Qt::CaseInsensitive) ||
            entry.fileName.startsWith("common/", Qt::CaseInsensitive) || entry.fileName.startsWith("goods/", Qt::CaseInsensitive)))
            return fail(tr("这个脚本名在当前调用范围内已经存在，请换一个名称。"));
    }
    const QString target = QFileInfo(currentFileName).dir().filePath(name);
    if (documentPathValidator && !documentPathValidator(target)) return fail(tr("目标文件已在其它编辑页中打开。"));
    auto report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，请先查看引用。"));
    QStringList changed = report.fileBytes.keys();
    changed.append(currentFileName);
    QString error;
    if (mutationPreflight && !mutationPreflight(changed, error)) return fail(error);
    // Preflight may have saved open callers. Re-scan those exact saved bytes before publication.
    report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，未更新引用。"));
    changed = report.fileBytes.keys();
    changed.append(currentFileName);
    if (mutationPreflight && !mutationPreflight(changed, error)) return fail(error);
    QByteArray bytes = desktopRunDocumentSnapshot().bytes;
    {
        DurableFileTransaction transaction(assetsBasePath);
        for (auto file = report.fileBytes.cbegin(); file != report.fileBytes.cend(); ++file)
        {
            QList<ContentReferenceOccurrence> occurrences;
            for (const auto& occurrence : report.occurrences)
                if (occurrence.filePath == file.key()) occurrences.append(occurrence);
            const QByteArray replaced = ContentAuthoring::replaceReferences(ContentKind::Script, file.value(), occurrences, name);
            if (file.key() == currentFileName) bytes = replaced;
            else if (!transaction.addBytesWriteChecked(file.key(), replaced,
                QFileInfo(file.key()).isFile(), report.diskBytes.value(file.key()), error)) return fail(error);
        }
        if (!transaction.addBytesWriteChecked(target, bytes, false, {}, error) ||
            !transaction.addRemovalChecked(currentFileName, loadedSourceBytes, error) || !transaction.commit(error)) return fail(error);
    }
    editor->document()->setModified(false);
    if (!openFile(target)) return fail(tr("文件已改名，请重新打开。"));
    changed.append(target);
    emit filesChanged(changed);
    return true;
}

bool ScriptEditorWindow::deleteContentFile(QString* errorMessage)
{
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return false; };
    if (errorMessage) errorMessage->clear();
    if (ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName).isEmpty() ||
        !EditorAssetPath::isInside(assetsBasePath, currentFileName) || loadedSourcePath != currentFileName ||
        !QFileInfo(currentFileName).isFile() || readScriptBytes(currentFileName) != loadedSourceBytes)
        return fail(tr("只能删除当前项目中未被外部修改的脚本。"));
    const auto report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，请先查看引用。"));
    const bool restoresBase = !ContentAuthoring::fallback(assetsBasePath,
        "script/" + ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName)).isEmpty();
    for (const auto& occurrence : report.occurrences)
        if (!restoresBase && occurrence.filePath != currentFileName) return fail(tr("这个脚本仍被使用，请先修改引用。"));
    for (const auto& occurrence : report.dynamicReferences)
        if (occurrence.context == "script — duplicate filename") return fail(tr("同名脚本的调用地图尚未确定，请先处理引用。"));
    QString error;
    if (mutationPreflight && !mutationPreflight({currentFileName}, error)) return fail(error);
    auto lease = AuthoringMutationGate::instance().acquireMutationLeaseForPath(currentFileName);
    if (!lease || readScriptBytes(currentFileName) != loadedSourceBytes) return fail(tr("文件正在保存或已被外部修改，请重新打开。"));
    if (!QFile::moveToTrash(currentFileName)) return fail(tr("无法移入系统回收站，原文件已保留。"));
    lease.release();
    const QString previous = currentFileName;
    currentFileName.clear();
    loadedSourcePath.clear();
    loadedSourceBytes.clear();
    editor->clear();
    editor->document()->setModified(false);
    updateWindowTitle();
    emit documentStateChanged({}, false);
    emit filesChanged({previous});
    return true;
}

void ScriptEditorWindow::showReferences()
{
    showContentReferences(this, assetsBasePath, currentReferences(), [this](const QString& path, int line)
    {
        emit openReferenceRequested(path, line);
    });
}

void ScriptEditorWindow::setupAuthoringActions()
{
    if (!isRunnableScript()) return;
    auto* create = new QPushButton(tr("新建"), this);
    create->setObjectName("newScriptButton");
    ui->toolbarLayout->insertWidget(0, create);
    auto* copy = new QPushButton(tr("复制"), this);
    copy->setObjectName("copyScriptButton");
    ui->toolbarLayout->insertWidget(2, copy);
    for (auto* button : {create, copy})
        connect(button, &QPushButton::clicked, this, [this, button, copy]()
        {
            bool accepted = false;
            const QString name = QInputDialog::getText(this, button->text(), tr("脚本名称"), QLineEdit::Normal,
                button == copy ? QFileInfo(currentFileName).completeBaseName() + tr("副本") : QString(), &accepted);
            if (!accepted) return;
            QString error;
            const QString path = createContentFile(name, button == copy, &error);
            if (path.isEmpty()) QMessageBox::warning(this, tr("无法创建脚本"), error);
            else emit openScriptFileRequested(path);
        });
    auto* more = new QToolButton(this);
    more->setObjectName("scriptMoreButton");
    more->setText(tr("更多操作…"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more);
    more->setMenu(menu);
    ui->toolbarLayout->insertWidget(ui->toolbarLayout->count() - 1, more);
    menu->addAction(tr("查看引用…"), this, &ScriptEditorWindow::showReferences);
    menu->addAction(tr("重命名文件…"), this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("重命名脚本"), tr("新文件名"), QLineEdit::Normal,
            QFileInfo(currentFileName).fileName(), &accepted);
        if (!accepted) return;
        const auto report = currentReferences();
        if (QMessageBox::question(this, tr("确认重命名"), tr("将保存当前脚本并更新 %1 处明确引用。动态调用需自行确认。是否继续？")
            .arg(report.occurrences.size()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!renameContentFile(name, &error) && !error.isEmpty()) QMessageBox::warning(this, tr("无法重命名"), error);
    });
    menu->addAction(tr("删除脚本文件…"), this, [this]()
    {
        const auto report = currentReferences();
        const bool restoresBase = !ContentAuthoring::fallback(assetsBasePath,
            "script/" + ContentAuthoring::referenceName(ContentKind::Script, assetsBasePath, currentFileName)).isEmpty();
        for (const auto& occurrence : report.occurrences)
            if (!restoresBase && occurrence.filePath != currentFileName) { showReferences(); return; }
        QString message = tr("将当前脚本移入系统回收站，并放弃未保存修改。是否继续？");
        if (restoresBase)
            message += '\n' + tr("删除后会重新使用基础资源中的同名脚本。");
        if (!report.dynamicReferences.isEmpty()) message += '\n' + tr("有动态调用需要自行检查，请先查看引用。");
        if (QMessageBox::question(this, tr("删除脚本文件"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!deleteContentFile(&error)) QMessageBox::warning(this, tr("无法删除"), error);
    });
    connect(menu, &QMenu::aboutToShow, this, [this, menu]()
    {
        for (auto* action : menu->actions()) action->setEnabled(!currentFileName.isEmpty());
    });
}
