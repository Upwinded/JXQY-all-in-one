#include "NpcDataEditorWindow.h"
#include "ui_NpcDataEditorWindow.h"
#include "../core/ContentAuthoring.h"
#include "../core/DurableFileTransaction.h"
#include "../core/EditorAssetPath.h"

#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QToolButton>

QString NpcDataEditorWindow::createContentFile(ProjectDocumentType type, const QString& input,
    const QString& mapName, QString* errorMessage)
{
    auto fail = [&](const QString& message) { if (errorMessage) *errorMessage = message; return QString(); };
    if (errorMessage) errorMessage->clear();
    if (type != ProjectDocumentType::NpcList && type != ProjectDocumentType::ObjectList && type != ProjectDocumentType::NpcResource)
        return fail(tr("请选择NPC列表、物体列表或动作资源。"));
    const QString name = input.normalized(QString::NormalizationForm_C).trimmed();
    if (name.isEmpty() || name.startsWith('.') || name.endsWith('.') || name.size() > 100 ||
        name.contains(QRegularExpression("[\\\\/:*?\"<>|\\r\\n]")) ||
        QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
        return fail(tr("请输入单行名称，不包含目录或特殊字符。"));
    const bool resource = type == ProjectDocumentType::NpcResource;
    const QString map = QFileInfo(QDir::fromNativeSeparators(mapName)).completeBaseName();
    if (!resource && !ContentAuthoring::mapNames(assetsBasePath).contains(map, Qt::CaseInsensitive))
        return fail(tr("请先选择列表所属的地图。"));
    const QString suffix = resource ? ".ini" : type == ProjectDocumentType::NpcList ? ".npc" : ".obj";
    const QString relative = (resource ? "ini/npcres/" : "ini/save/") + name + suffix;
    const QString target = QDir(assetsBasePath).filePath(relative);
    if (!ContentAuthoring::resolve(assetsBasePath, relative).isEmpty()) return fail(tr("这个文件名已存在，请换一个名称。"));
    if (!EditorAssetPath::isInside(assetsBasePath, target) || !QDir().mkpath(QFileInfo(target).path()))
        return fail(tr("无法创建项目内容目录。"));
    const QByteArray bytes = resource ? QByteArray("[Stand]\r\nImage=\r\n")
        : QString("[Head]\r\nMap=%1.map\r\nCount=0\r\n").arg(map).toUtf8();
    QString error;
    DurableFileTransaction transaction(assetsBasePath);
    if (!transaction.addBytesWriteChecked(target, bytes, false, {}, error) || !transaction.commit(error)) return fail(error);
    return target;
}

void NpcDataEditorWindow::setupAuthoringActions()
{
    auto* create = new QToolButton(this);
    create->setObjectName("newEntityDocumentButton");
    create->setText(tr("新建文件"));
    create->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(create);
    create->setMenu(menu);
    ui->npcToolBar->insertWidget(ui->actionOpen, create);
    for (auto type : {ProjectDocumentType::NpcList, ProjectDocumentType::ObjectList, ProjectDocumentType::NpcResource})
    {
        const QString title = type == ProjectDocumentType::NpcList ? tr("NPC列表…")
            : type == ProjectDocumentType::ObjectList ? tr("物体列表…") : tr("NPC动作资源…");
        auto* action = menu->addAction(title);
        connect(action, &QAction::triggered, this, [this, type, title]()
        {
            bool accepted = false;
            const QString name = QInputDialog::getText(this, title, tr("文件名称"), QLineEdit::Normal, {}, &accepted);
            if (!accepted) return;
            QString map;
            if (type != ProjectDocumentType::NpcResource)
            {
                const auto maps = ContentAuthoring::mapNames(assetsBasePath);
                if (maps.isEmpty()) { QMessageBox::information(this, title, tr("请先创建或导入地图。")); return; }
                map = QInputDialog::getItem(this, title, tr("所属地图"), maps, 0, false, &accepted);
                if (!accepted) return;
            }
            QString error;
            const QString path = createContentFile(type, name, map, &error);
            if (path.isEmpty()) QMessageBox::warning(this, tr("无法新建"), error);
            else emit openContentFileRequested(path);
        });
    }
    ui->actionSaveAs->setToolTip(tr("另存为独立文件；保留条目、未知字段和原有素材引用。"));
}
