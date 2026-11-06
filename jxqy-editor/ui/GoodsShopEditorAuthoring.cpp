#include "GoodsShopEditorWindow.h"

#include "ContentReferenceDialog.h"
#include "../core/AuthoringMutationGate.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"
#include "../core/GameProfile.h"

#include <QApplication>
#include <QScopeGuard>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>

namespace
{
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

ContentKind GoodsShopEditorWindow::contentKind() const
{
    return kind == GoodsShopDocumentKind::Shop ? ContentKind::Shop : ContentKind::Goods;
}

void GoodsShopEditorWindow::setDocumentSnapshotsProvider(std::function<QList<DesktopRunDocumentSnapshot>()> provider)
{
    snapshotsProvider = std::move(provider);
}

void GoodsShopEditorWindow::setFileMutationPreflight(std::function<bool(const QStringList&, QString&)> preflight)
{
    mutationPreflight = std::move(preflight);
}

bool GoodsShopEditorWindow::authoringTarget(const QString& path, QString& error) const
{
    const QString directory = isShopFilePath(path) ? "ini/buy" : "ini/goods";
    if (assetsBasePath.isEmpty() || !QFileInfo(assetsBasePath).isDir() || !isSupportedFilePath(path) ||
        !EditorAssetPath::isLexicallyInside(QDir(assetsBasePath).filePath(directory), path) ||
        !EditorAssetPath::isInside(assetsBasePath, path))
    {
        error = tr("请先选择项目资源。文件必须位于当前项目的物品或商店目录。");
        return false;
    }
    const auto resolution = ResourcePackScanner::resolveContentRoots(assetsBasePath);
    if (!resolution.recoveryErrors.isEmpty())
    {
        error = resolution.recoveryErrors.join('\n');
        return false;
    }
    if (documentPathValidator && !documentPathValidator(filePath, path))
    {
        error = tr("目标文件已经在其它编辑页打开，请先处理该编辑页。");
        return false;
    }
    return true;
}

QByteArray GoodsShopEditorWindow::pendingDocumentBytes() const
{
    if (filePath.isEmpty() || refreshing) return currentDocumentBytes();
    auto value = [](QSpinBox* spin)
    {
        bool valid = false;
        const int typed = spin->locale().toInt(spin->cleanText(), &valid);
        return valid && spin->hasAcceptableInput() ? typed : spin->value();
    };
    if (kind == GoodsShopDocumentKind::Goods)
    {
        GoodsDocument result = goodsDocument;
        for (const auto field : {GoodsTextField::Name, GoodsTextField::Effect, GoodsTextField::Image, GoodsTextField::Icon})
            result.setText(field, goodsLineEdit(field)->text());
        result.setText(GoodsTextField::Introduction, goodsIntroductionEdit->toPlainText());
        for (const auto field : {GoodsIntegerField::Cost, GoodsIntegerField::SellPrice, GoodsIntegerField::Life,
            GoodsIntegerField::Thew, GoodsIntegerField::Mana, GoodsIntegerField::LifeMaximum,
            GoodsIntegerField::ThewMaximum, GoodsIntegerField::ManaMaximum, GoodsIntegerField::Attack,
            GoodsIntegerField::Defend, GoodsIntegerField::Evade})
        {
            auto* spin = goodsSpinBox(field);
            if (spin->isEnabled() && result.integer(field) != value(spin)) result.setInteger(field, value(spin));
        }
        return result.serializedBytes();
    }
    if (kind == GoodsShopDocumentKind::Shop)
    {
        ShopDocument result = shopDocument;
        if (result.buyPercent() != value(shopBuyPercentSpin)) result.setBuyPercent(value(shopBuyPercentSpin));
        if (result.recyclePercent() != value(shopRecyclePercentSpin)) result.setRecyclePercent(value(shopRecyclePercentSpin));
        return result.serializedBytes();
    }
    return {};
}

QString GoodsShopEditorWindow::createContentFile(ContentKind requestedKind, const QString& name,
    bool copyCurrent, QString* errorMessage)
{
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return QString(); };
    if (errorMessage) errorMessage->clear();
    const QString cleanName = name.trimmed();
    if ((requestedKind != ContentKind::Goods && requestedKind != ContentKind::Shop) ||
        cleanName.isEmpty() || cleanName.contains('\n') || cleanName.contains('\r'))
        return fail(tr("请填写单行名称。"));
    if (copyCurrent && (filePath.isEmpty() || requestedKind != contentKind()))
        return fail(tr("请先选择要复制的物品或商店。"));
    QByteArray bytes = copyCurrent ? pendingDocumentBytes() : ContentAuthoring::newDocument(requestedKind, cleanName);
    if (requestedKind == ContentKind::Goods)
    {
        GoodsDocument copy;
        if (!copy.load(bytes)) return fail(tr("无法读取物品内容。"));
        copy.setText(GoodsTextField::Name, cleanName);
        GoodsDocument check;
        if (!check.load(copy.serializedBytes()) || check.text(GoodsTextField::Name) != cleanName)
            return fail(tr("这个名称无法完整保存，请换一个名称。"));
        bytes = copy.serializedBytes();
    }
    const QString target = QDir(assetsBasePath).filePath(ContentAuthoring::directory(requestedKind) + '/' +
        ContentAuthoring::unusedFileName(requestedKind, assetsBasePath, cleanName));
    QString error;
    if (!authoringTarget(target, error) || !QDir().mkpath(QFileInfo(target).path()))
        return fail(error.isEmpty() ? tr("无法创建内容目录。") : error);
    {
        DurableFileTransaction transaction(assetsBasePath);
        if (!transaction.addBytesWriteChecked(target, bytes, false, {}, error) || !transaction.commit(error))
            return fail(error);
    }
    refreshCatalogLists();
    emit filesChanged({target});
    return target;
}

ContentReferenceReport GoodsShopEditorWindow::currentReferences() const
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto restoreCursor = qScopeGuard([]() { QApplication::restoreOverrideCursor(); });
    auto snapshots = snapshotsProvider ? snapshotsProvider() : QList<DesktopRunDocumentSnapshot>();
    snapshots.append(desktopRunDocumentSnapshot());
    return ContentAuthoring::references(contentKind(), assetsBasePath,
        ContentAuthoring::referenceName(contentKind(), assetsBasePath, filePath), snapshots);
}

bool GoodsShopEditorWindow::renameContentFile(const QString& newFileName, QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return false; };
    QString name = newFileName.normalized(QString::NormalizationForm_C).trimmed();
    if (!name.endsWith(".ini", Qt::CaseInsensitive)) name += ".ini";
    if (name.contains(QRegularExpression("[\\\\/:*?\"<>|\\r\\n]")) || name == ".ini" || name.startsWith('.') || name.size() > 120 ||
        QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])\\.", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
        return fail(tr("请输入有效的文件名，不包含目录或特殊字符。"));
    const QString target = QDir(QFileInfo(filePath).path()).filePath(name);
    QString error;
    if (!authoringTarget(target, error)) return fail(error);
    if (ProjectDocumentRegistry::documentPathKey(target) == ProjectDocumentRegistry::documentPathKey(filePath))
        return fail(tr("新文件名与当前文件名相同。"));
    const QString newReference = ContentAuthoring::referenceName(contentKind(), assetsBasePath, target);
    if (!ContentAuthoring::resolve(assetsBasePath, ContentAuthoring::directory(contentKind()) + '/' + newReference).isEmpty())
        return fail(tr("这个文件名已经存在，请换一个名称。"));
    if (loadedSourcePath != filePath || readBytes(filePath) != loadedBytes)
        return fail(tr("文件来自基础资源或已被外部修改，请复制为新文件，或重新打开后再操作。"));
    auto report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，不能安全更新引用，请先查看引用。"));
    QStringList changed = report.fileBytes.keys();
    changed.append(filePath);
    if (mutationPreflight && !mutationPreflight(changed, error)) return fail(error);
    // Preflight may have saved open callers. Re-scan those exact saved bytes before publication.
    report = currentReferences();
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，未更新引用。"));
    changed = report.fileBytes.keys();
    changed.append(filePath);
    if (mutationPreflight && !mutationPreflight(changed, error)) return fail(error);
    QByteArray renamedBytes = pendingDocumentBytes();
    {
        DurableFileTransaction transaction(assetsBasePath);
        for (auto file = report.fileBytes.cbegin(); file != report.fileBytes.cend(); ++file)
        {
            QList<ContentReferenceOccurrence> occurrences;
            for (const auto& occurrence : report.occurrences)
                if (occurrence.filePath == file.key()) occurrences.append(occurrence);
            const QByteArray bytes = ContentAuthoring::replaceReferences(contentKind(), file.value(), occurrences, newReference);
            if (file.key() == filePath) renamedBytes = bytes;
            else if (!transaction.addBytesWriteChecked(file.key(), bytes,
                QFileInfo(file.key()).isFile(), report.diskBytes.value(file.key()), error)) return fail(error);
        }
        if (!transaction.addBytesWriteChecked(target, renamedBytes, false, {}, error) ||
            !transaction.addRemovalChecked(filePath, loadedBytes, error) || !transaction.commit(error)) return fail(error);
    }
    filePath.clear();
    undoStack->clear();
    if (!openFile(target)) return fail(tr("文件已改名，请重新打开列表。"));
    changed.append(target);
    emit filesChanged(changed);
    return true;
}

bool GoodsShopEditorWindow::deleteContentFile(QString* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return false; };
    QString error;
    if (!authoringTarget(filePath, error) || loadedSourcePath != filePath)
        return fail(error.isEmpty() ? tr("只能删除当前项目中的文件。") : error);
    if (readBytes(filePath) != loadedBytes) return fail(tr("文件已被外部修改，请重新打开后再删除。"));
    const bool restoresBase = !ContentAuthoring::fallback(assetsBasePath,
        ContentAuthoring::directory(contentKind()) + '/' + ContentAuthoring::referenceName(contentKind(), assetsBasePath, filePath)).isEmpty();
    const auto report = currentReferences();
    for (const auto& occurrence : report.occurrences)
        if (!restoresBase && occurrence.filePath != filePath)
            return fail(tr("这个文件仍被使用，请先修改引用。"));
    if (!report.errors.isEmpty()) return fail(tr("部分文件无法读取，请先处理引用检查中的问题。"));
    if (mutationPreflight && !mutationPreflight({filePath}, error)) return fail(error);
    auto lease = AuthoringMutationGate::instance().acquireMutationLeaseForPath(filePath);
    if (!lease) return fail(tr("资源正在保存，请稍后重试。"));
    if (readBytes(filePath) != loadedBytes) return fail(tr("文件已被外部修改，请重新打开后再删除。"));
    if (!QFile::moveToTrash(filePath)) return fail(tr("无法移入系统回收站，原文件已保留。"));
    lease.release();
    const QString previous = filePath;
    filePath.clear();
    loadedSourcePath.clear();
    loadedBytes.clear();
    kind = GoodsShopDocumentKind::None;
    goodsDocument = GoodsDocument();
    shopDocument = ShopDocument();
    undoStack->clear();
    refreshCatalogLists();
    refreshFromDocument();
    if (restoresBase) openFile(previous);
    emit documentStatesChanged();
    emit filesChanged({previous});
    return true;
}

void GoodsShopEditorWindow::showReferences()
{
    showContentReferences(this, assetsBasePath, currentReferences(), [this](const QString& path, int line)
    {
        emit openReferenceRequested(path, line);
    });
}

void GoodsShopEditorWindow::setupAuthoringActions()
{
    auto* menu = new QMenu(this);
    newGoodsAction = menu->addAction(tr("新建物品…"));
    newGoodsAction->setObjectName("newGoodsAction");
    newShopAction = menu->addAction(tr("新建商店…"));
    newShopAction->setObjectName("newShopAction");
    auto* button = new QToolButton(this);
    button->setObjectName("contentNewButton");
    button->setText(tr("新建"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(menu);
    toolBar->addWidget(button);
    duplicateAction = toolBar->addAction(tr("复制"));
    duplicateAction->setObjectName("goodsDuplicateAction");
    toolBar->addSeparator();
    for (auto* action : {newGoodsAction, newShopAction})
        connect(action, &QAction::triggered, this, [this, action]()
        {
            const auto requested = action == newGoodsAction ? ContentKind::Goods : ContentKind::Shop;
            bool accepted = false;
            const QString name = QInputDialog::getText(this, action->text(), tr("名称"), QLineEdit::Normal, {}, &accepted);
            if (!accepted) return;
            QString error;
            const QString target = createContentFile(requested, name, false, &error);
            if (target.isEmpty()) QMessageBox::warning(this, tr("无法新建"), error);
            else if (filePath.isEmpty()) openFile(target);
            else emit openGoodsShopFileRequested(target);
        });
}

void GoodsShopEditorWindow::setupAuthoringMenu()
{
    auto* menu = new QMenu(this);
    usageScriptAction = menu->addAction(tr("创建使用脚本"));
    referencesAction = menu->addAction(tr("查看引用…"));
    menu->addSeparator();
    renameAction = menu->addAction(tr("重命名文件…"));
    deleteAction = menu->addAction(tr("删除文件…"));
    deleteAction->setObjectName("contentDeleteAction");
    auto* more = new QToolButton(this);
    more->setObjectName("contentMoreButton");
    more->setText(tr("更多操作…"));
    more->setPopupMode(QToolButton::InstantPopup);
    more->setMenu(menu);
    toolBar->addSeparator();
    toolBar->addWidget(more);
    connect(referencesAction, &QAction::triggered, this, &GoodsShopEditorWindow::showReferences);
    connect(usageScriptAction, &QAction::triggered, this, [this]()
    {
        QString reference = ContentAuthoring::referenceName(contentKind(), assetsBasePath, filePath);
        reference.replace('\\', "\\\\");
        reference.replace('"', "\\\"");
        emit usageScriptRequested(QStringLiteral("%1(\"%2\")\n")
            .arg(kind == GoodsShopDocumentKind::Shop ? "buygoods" : "addgoods", reference));
    });
    connect(renameAction, &QAction::triggered, this, [this]()
    {
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("重命名文件"),
            tr("新文件名（已识别的引用会一起更新）"), QLineEdit::Normal, QFileInfo(filePath).fileName(), &accepted);
        if (!accepted) return;
        const auto report = currentReferences();
        QMessageBox confirm(QMessageBox::Question, tr("确认重命名"),
            tr("将更新 %1 处明确引用。动态调用不会自动改写，是否继续？").arg(report.occurrences.size()),
            QMessageBox::Yes | QMessageBox::No, this);
        QStringList locations;
        for (const auto& occurrence : report.occurrences)
            locations.append(QStringLiteral("%1:%2  %3").arg(QDir(assetsBasePath).relativeFilePath(occurrence.filePath))
                .arg(occurrence.line).arg(occurrence.context));
        confirm.setDetailedText(locations.join('\n'));
        confirm.setDefaultButton(QMessageBox::No);
        if (confirm.exec() != QMessageBox::Yes) return;
        QString error;
        if (!renameContentFile(name, &error) && !error.isEmpty()) QMessageBox::warning(this, tr("无法重命名"), error);
    });
    connect(deleteAction, &QAction::triggered, this, [this]()
    {
        const auto report = currentReferences();
        const bool restoresBase = !ContentAuthoring::fallback(assetsBasePath, ContentAuthoring::directory(contentKind()) + '/' +
            ContentAuthoring::referenceName(contentKind(), assetsBasePath, filePath)).isEmpty();
        for (const auto& occurrence : report.occurrences)
            if (!restoresBase && occurrence.filePath != filePath) { showReferences(); return; }
        QString message = tr("将“%1”移入系统回收站，并放弃此文件的未保存修改。是否继续？").arg(displayName());
        if (restoresBase) message += tr("\n基础资源中的同名文件会重新生效。");
        if (!report.dynamicReferences.isEmpty()) message += tr("\n有动态调用需要自行检查，请先查看引用。");
        if (QMessageBox::question(this, tr("删除文件"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!deleteContentFile(&error)) QMessageBox::warning(this, tr("无法删除"), error);
    });
}

void GoodsShopEditorWindow::retranslateAuthoringActions()
{
    if (!newGoodsAction) return;
    newGoodsAction->setText(tr("新建物品…"));
    newShopAction->setText(tr("新建商店…"));
    duplicateAction->setText(tr("复制"));
    if (auto* button = findChild<QToolButton*>("contentNewButton")) button->setText(tr("新建"));
    if (auto* button = findChild<QToolButton*>("contentMoreButton")) button->setText(tr("更多操作…"));
    if (!renameAction) return;
    usageScriptAction->setText(tr("创建使用脚本"));
    referencesAction->setText(tr("查看引用…"));
    renameAction->setText(tr("重命名文件…"));
    deleteAction->setText(kind == GoodsShopDocumentKind::Shop ? tr("删除商店文件…") : tr("删除物品文件…"));
}
