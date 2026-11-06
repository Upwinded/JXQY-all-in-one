#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "GoodsShopEditorWindow.h"
#include "DialogueEditorWindow.h"
#include "MagicEditorWindow.h"
#include "MapEditorWindow.h"
#include "NpcDataEditorWindow.h"
#include "ScriptEditorWindow.h"
#include "ProjectExplorerWidget.h"
#include "../core/EditorAssetPath.h"
#include "../core/GameProfile.h"
#include "../core/DurableFileTransaction.h"
#include "../core/ProjectManager.h"

#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QSet>
#include <QRegularExpression>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace
{
QString projectPath(const QString& root, const QString& path)
{
    for (const auto& entry : ResourcePackScanner::resolveContentRoots(root).roots)
    {
        if (entry.available && EditorAssetPath::isLexicallyInside(entry.rootPath, path))
            return QDir(root).filePath(QDir(entry.rootPath).relativeFilePath(path));
    }
    return path;
}
}

void MainWindow::onOpenMagicEditor()
{
    const QString root = activeAssetsPath();
    if (root.isEmpty() || !QFileInfo(root).isDir())
    {
        QMessageBox::information(this, tr("武功编辑器"), tr("请先打开项目并选择资源。"));
        return;
    }
    for (auto* subWindow : ui->mdiArea->subWindowList())
    {
        if (qobject_cast<MagicEditorWindow*>(subWindow->widget()))
        {
            ui->mdiArea->setActiveSubWindow(subWindow);
            return;
        }
    }
    auto* window = new MagicEditorWindow(this);
    window->setAssetsBasePath(root);
    auto* subWindow = createMdiSubWindow(window, tr("武功编辑器"));
    configureMagicDocumentWindow(window, subWindow);
    const auto resources = ContentAuthoring::resources(ContentKind::Magic, root);
    if (!resources.isEmpty())
        window->openFile(QDir(root).filePath("ini/magic/" + resources.first().fileName));
    window->show();
}

void MainWindow::onOpenGoodsShopEditor()
{
    const QString root = activeAssetsPath();
    if (root.isEmpty() || !QFileInfo(root).isDir())
    {
        QMessageBox::information(this, tr("物品与商店"), tr("请先打开项目并选择资源。"));
        return;
    }
    for (auto* subWindow : ui->mdiArea->subWindowList())
        if (qobject_cast<GoodsShopEditorWindow*>(subWindow->widget()))
        {
            ui->mdiArea->setActiveSubWindow(subWindow);
            return;
        }
    auto* window = new GoodsShopEditorWindow(this);
    window->setAssetsBasePath(root);
    auto* subWindow = createMdiSubWindow(window, tr("物品与商店"));
    configureGoodsShopDocumentWindow(window, subWindow);
    const auto resources = ContentAuthoring::resources(ContentKind::Goods, root);
    if (!resources.isEmpty()) window->openFile(QDir(root).filePath("ini/goods/" + resources.first().fileName));
    window->show();
}

void MainWindow::configureContentAuthoring(GoodsShopEditorWindow* owner)
{
    owner->setDocumentSnapshotsProvider([this]() { return desktopRunDocumentSnapshots(); });
    owner->setFileMutationPreflight([this, owner](const QStringList& paths, QString& error)
    {
        return prepareContentFileMutation(owner->currentFilePath(), paths, error);
    });
    connect(owner, &GoodsShopEditorWindow::filesChanged, this, [this, owner](const QStringList& paths)
    {
        refreshContentFileDocuments(owner, paths);
    });
    connect(owner, &GoodsShopEditorWindow::openReferenceRequested, this, &MainWindow::openContentReference);
    connect(owner, &GoodsShopEditorWindow::usageScriptRequested, this, [this, owner](const QString& source)
    {
        auto* window = new ScriptEditorWindow(this);
        auto* subWindow = createMdiSubWindow(window, tr("物品与商店脚本"));
        configureScriptDocumentWindow(window, subWindow);
        window->setSuggestedSavePath(QDir(activeAssetsPath()).filePath(
            "script/use-" + QFileInfo(owner->currentFilePath()).completeBaseName() + ".lua"));
        window->insertScriptCall(source);
        window->show();
    });
}

void MainWindow::onOpenDialogueEditor()
{
    const QString root = activeAssetsPath();
    if (root.isEmpty() || !QFileInfo(root).isDir())
    {
        QMessageBox::information(this, tr("地图对话"), tr("请先打开项目并选择资源。"));
        return;
    }
    QStringList maps;
    QSet<QString> found;
    for (const auto& provider : ResourcePackScanner::resolveContentRoots(root).roots)
    {
        if (!provider.available) continue;
        for (const auto& file : QDir(QDir(provider.rootPath).filePath("map")).entryInfoList({"*.map"}, QDir::Files, QDir::Name))
        {
            const QString name = file.completeBaseName();
            if (!found.contains(name.toCaseFolded())) { found.insert(name.toCaseFolded()); maps.append(name); }
        }
    }
    maps.sort(Qt::CaseInsensitive);
    if (maps.isEmpty())
    {
        QMessageBox::information(this, tr("地图对话"), tr("项目还没有地图，请先创建或导入地图。"));
        return;
    }
    QString defaultMap;
    const auto& configuration = ProjectManager::instance().runtimeConfiguration();
    for (const auto& scene : configuration.scenes)
        if (scene.id == configuration.defaultSceneId) defaultMap = QFileInfo(scene.mapPath).completeBaseName();
    bool accepted = false;
    const QString map = QInputDialog::getItem(this, tr("地图对话"), tr("选择对话所属地图"), maps,
        qMax(0, maps.indexOf(defaultMap)), false, &accepted);
    if (!accepted) return;
    const QString relative = "script/map/" + map + "/talk.txt";
    const QString target = QDir(root).filePath(relative);
    if (ContentAuthoring::resolve(root, relative).isEmpty())
    {
        QString error;
        bool saved = QDir().mkpath(QFileInfo(target).path());
        if (saved)
        {
            DurableFileTransaction transaction(root);
            saved = transaction.addBytesWriteChecked(target, ContentAuthoring::newDocument(ContentKind::Dialogue, {}),
                false, {}, error) && transaction.commit(error);
        }
        if (!saved)
        {
            QMessageBox::warning(this, tr("无法创建地图对话"), error.isEmpty() ? tr("无法创建对话目录。") : error);
            return;
        }
    }
    openDialogueFile(target, {});
}

void MainWindow::configureContentAuthoring(DialogueEditorWindow* owner)
{
    owner->setDocumentSnapshotsProvider([this]() { return desktopRunDocumentSnapshots(); });
    owner->setFileMutationPreflight([this, owner](const QStringList& paths, QString& error)
    {
        return prepareContentFileMutation(owner->currentFilePath(), paths, error);
    });
    connect(owner, &DialogueEditorWindow::filesChanged, this, [this, owner](const QStringList& paths)
    {
        refreshContentFileDocuments(owner, paths);
    });
    connect(owner, &DialogueEditorWindow::openReferenceRequested, this, &MainWindow::openContentReference);
    connect(owner, &DialogueEditorWindow::usageScriptRequested, this, [this, owner](const QString& source)
    {
        auto* window = new ScriptEditorWindow(this);
        auto* subWindow = createMdiSubWindow(window, tr("对话调用脚本"));
        configureScriptDocumentWindow(window, subWindow);
        QString stem = owner->currentSectionName().normalized(QString::NormalizationForm_C);
        stem.replace(QRegularExpression("[^\\p{L}\\p{N}_-]+"), "_");
        if (stem.isEmpty()) stem = "dialogue";
        window->setSuggestedSavePath(QFileInfo(owner->currentFilePath()).dir().filePath("use-" + stem.left(60) + ".lua"));
        window->insertScriptCall(source);
        window->show();
    });
}

void MainWindow::configureContentAuthoring(MagicEditorWindow* owner)
{
    owner->setDocumentSnapshotsProvider([this]() { return desktopRunDocumentSnapshots(); });
    owner->setFileMutationPreflight([this, owner](const QStringList& paths, QString& error)
    {
        return prepareContentFileMutation(owner->currentFilePath(), paths, error);
    });
    connect(owner, &MagicEditorWindow::filesChanged, this, [this, owner](const QStringList& paths)
    {
        refreshContentFileDocuments(owner, paths);
    });
    connect(owner, &MagicEditorWindow::openReferenceRequested, this, [this](const QString& path, int line)
    {
        openContentReference(path, line);
    });
    connect(owner, &MagicEditorWindow::learningScriptRequested, this, [this, owner](const QString& source)
    {
        auto* window = new ScriptEditorWindow(this);
        auto* subWindow = createMdiSubWindow(window, tr("武功学习脚本"));
        configureScriptDocumentWindow(window, subWindow);
        window->setSuggestedSavePath(QDir(activeAssetsPath()).filePath(
            "script/learn-" + QFileInfo(owner->currentFilePath()).completeBaseName() + ".lua"));
        window->insertScriptCall(source);
        window->show();
    });
}

bool MainWindow::prepareContentFileMutation(const QString& ownerPath, const QStringList& paths, QString& error)
{
    QSet<QString> affected;
    for (const auto& path : paths) affected.insert(ProjectDocumentRegistry::documentPathKey(path));
    const QString root = activeAssetsPath();
    const QString ownerKey = ProjectDocumentRegistry::documentPathKey(ownerPath);
    auto needsSave = [&](const QString& path, bool dirty)
    {
        const QString key = ProjectDocumentRegistry::documentPathKey(projectPath(root, path));
        return dirty && key != ownerKey && affected.contains(key);
    };
    QList<std::function<bool()>> saves;
    QStringList names;
    for (auto* subWindow : ui->mdiArea->subWindowList())
    {
        QWidget* widget = subWindow->widget();
        if (auto* script = qobject_cast<ScriptEditorWindow*>(widget))
        {
            if (needsSave(script->currentFilePath(), script->hasUnsavedChanges()) ||
                (script->isRunnableScript() && script->currentFilePath().isEmpty() && script->hasUnsavedChanges()))
            {
                names.append(script->currentFilePath().isEmpty() ? tr("未命名脚本") : QFileInfo(script->currentFilePath()).fileName());
                saves.append([script]() { return script->saveFile(); });
            }
        }
        else if (auto* map = qobject_cast<MapEditorWindow*>(widget))
        {
            for (const auto& state : map->currentProjectDocuments())
                if (needsSave(state.filePath, state.dirty))
                {
                    names.append(QFileInfo(state.filePath).fileName());
                    saves.append([map, path = state.filePath]() { return map->saveContentDocument(path); });
                }
        }
        else if (auto* npc = qobject_cast<NpcDataEditorWindow*>(widget))
        {
            for (const auto& state : npc->currentProjectDocuments())
                if (needsSave(state.filePath, state.dirty))
                {
                    names.append(QFileInfo(state.filePath).fileName());
                    saves.append([npc, path = state.filePath]() { return npc->saveContentDocument(path); });
                }
        }
        else if (auto* magic = qobject_cast<MagicEditorWindow*>(widget))
        {
            if (needsSave(magic->currentFilePath(), magic->hasUnsavedChanges()))
            { names.append(QFileInfo(magic->currentFilePath()).fileName()); saves.append([magic]() { return magic->saveFile(); }); }
        }
        else if (auto* goods = qobject_cast<GoodsShopEditorWindow*>(widget))
        {
            if (needsSave(goods->currentFilePath(), goods->hasUnsavedChanges()))
            { names.append(QFileInfo(goods->currentFilePath()).fileName()); saves.append([goods]() { return goods->saveFile(); }); }
        }
        else if (auto* dialogue = qobject_cast<DialogueEditorWindow*>(widget))
        {
            if (needsSave(dialogue->currentFilePath(), dialogue->hasUnsavedChanges()))
            { names.append(QFileInfo(dialogue->currentFilePath()).fileName()); saves.append([dialogue]() { return dialogue->saveFile(); }); }
        }
    }
    if (!saves.isEmpty())
    {
        QMessageBox prompt(QMessageBox::Question, tr("保存相关修改"),
            tr("下面的内容需要先保存，再更新引用：\n%1").arg(names.join('\n')), QMessageBox::NoButton, this);
        auto* save = prompt.addButton(tr("保存并继续"), QMessageBox::AcceptRole);
        prompt.addButton(QMessageBox::Cancel);
        prompt.exec();
        if (prompt.clickedButton() != save) return false;
        for (const auto& saveDocument : saves)
            if (!saveDocument())
            {
                error = tr("相关内容尚未保存，未更改文件名和引用。请先处理保存问题。");
                return false;
            }
    }
    return true;
}

void MainWindow::refreshContentFileDocuments(QWidget* owner, const QStringList& paths)
{
        QSet<QString> affected;
        for (const auto& path : paths) affected.insert(ProjectDocumentRegistry::documentPathKey(path));
        const QString root = activeAssetsPath();
        for (auto* subWindow : ui->mdiArea->subWindowList())
        {
            QWidget* widget = subWindow->widget();
            if (widget == owner) continue;
            auto changedPath = [&](const QString& path)
            {
                const QString mapped = projectPath(root, path);
                return affected.contains(ProjectDocumentRegistry::documentPathKey(mapped)) && QFileInfo(mapped).isFile()
                    ? mapped : QString();
            };
            if (auto* script = qobject_cast<ScriptEditorWindow*>(widget))
            {
                const QString path = changedPath(script->currentFilePath());
                if (!path.isEmpty() && !script->hasUnsavedChanges())
                {
                    const auto view = script->editingViewState();
                    script->openFile(path);
                    script->restoreEditingViewState(view);
                }
            }
            else if (auto* magic = qobject_cast<MagicEditorWindow*>(widget))
            {
                const QString path = changedPath(magic->currentFilePath());
                if (!path.isEmpty()) magic->openFile(path);
                magic->setAssetsBasePath(root);
            }
            else if (auto* goods = qobject_cast<GoodsShopEditorWindow*>(widget))
            {
                const QString path = changedPath(goods->currentFilePath());
                if (!path.isEmpty()) goods->openFile(path);
                goods->setAssetsBasePath(root);
            }
            else if (auto* map = qobject_cast<MapEditorWindow*>(widget))
            {
                for (const auto& state : map->currentProjectDocuments())
                {
                    const QString path = changedPath(state.filePath);
                    if (!path.isEmpty()) map->reloadContentDocument(path);
                }
            }
            else if (auto* npc = qobject_cast<NpcDataEditorWindow*>(widget))
            {
                for (const auto& state : npc->currentProjectDocuments())
                {
                    const QString path = changedPath(state.filePath);
                    if (path.isEmpty()) continue;
                    npc->reloadContentDocument(path);
                }
            }
        }
        updateProjectDocumentSession();
        if (projectExplorerWidget && !paths.isEmpty())
            projectExplorerWidget->revealFile(paths.last());
}

void MainWindow::openContentReference(const QString& path, int line)
{
        if (openFileByType(path))
        {
            if (auto* subWindow = ui->mdiArea->activeSubWindow())
                if (auto* script = qobject_cast<ScriptEditorWindow*>(subWindow->widget())) script->goToLine(line);
        }
}

void MainWindow::editBoundScript(const QString& path, QWidget* propertyDialog)
{
    const QString target = projectPath(activeAssetsPath(), path);
    for (auto* window : ui->mdiArea->subWindowList())
    {
        auto* script = qobject_cast<ScriptEditorWindow*>(window->widget());
        if (!script || ProjectDocumentRegistry::documentPathKey(projectPath(activeAssetsPath(), script->currentFilePath())) !=
                ProjectDocumentRegistry::documentPathKey(target) || !script->hasUnsavedChanges())
            continue;
        if (QMessageBox::question(propertyDialog, tr("保存脚本修改"),
                tr("这个脚本已在其它编辑页修改。先保存，再从属性窗口继续编辑？"),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save) != QMessageBox::Save || !script->saveFile())
            return;
    }
    class BoundScriptDialog : public QDialog
    {
    public:
        explicit BoundScriptDialog(QWidget* parent) : QDialog(parent) {}
        ScriptEditorWindow* editor = nullptr;
    protected:
        void done(int result) override
        {
            if (editor && editor->confirmSaveIfModified() == ScriptEditorWindow::SaveConfirmResult::Cancelled)
                return;
            QDialog::done(result);
        }
    };
    BoundScriptDialog dialog(propertyDialog);
    dialog.setWindowTitle(tr("编辑脚本 — %1").arg(QFileInfo(path).fileName()));
    dialog.resize(960, 680);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("脚本单独保存；返回后可继续编辑实体属性。"), &dialog));
    auto* script = new ScriptEditorWindow(&dialog);
    dialog.editor = script;
    script->setAssetsBasePath(activeAssetsPath());
    script->setSingleFileEditing();
    if (!script->openFile(path))
        return;
    layout->addWidget(script, 1);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* back = buttons->addButton(tr("返回属性"), QDialogButtonBox::AcceptRole);
    back->setObjectName("returnToEntityButton");
    back->setAutoDefault(false);
    connect(back, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
    refreshContentFileDocuments(nullptr, {target});
}
