#include "ProjectExplorerWidget.h"

#include "AssetBrowser.h"
#include "../core/EditorAssetPath.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

ProjectExplorerWidget::ProjectExplorerWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("projectExplorerWidget"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("projectExplorerTabs"));
    layout->addWidget(m_tabs);

    auto* filesPage = new QWidget(m_tabs);
    auto* filesLayout = new QVBoxLayout(filesPage);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    m_emptyProjectLabel = new QLabel(filesPage);
    m_emptyProjectLabel->setObjectName(QStringLiteral("projectExplorerEmptyLabel"));
    m_emptyProjectLabel->setAlignment(Qt::AlignCenter);
    m_emptyProjectLabel->setWordWrap(true);
    filesLayout->addWidget(m_emptyProjectLabel, 1);
    m_assetBrowser = new AssetBrowser(filesPage);
    m_assetBrowser->setObjectName(QStringLiteral("projectFileBrowser"));
    filesLayout->addWidget(m_assetBrowser, 1);

    m_documentTree = new QTreeWidget(m_tabs);
    m_documentTree->setObjectName(QStringLiteral("projectDocumentTree"));
    m_documentTree->setRootIsDecorated(false);
    m_documentTree->setAlternatingRowColors(true);
    m_documentTree->setColumnCount(3);
    m_documentTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_documentTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_documentTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    m_tabs->addTab(filesPage, QString());
    m_tabs->addTab(m_documentTree, QString());

    connect(m_assetBrowser, &AssetBrowser::fileDoubleClicked,
        this, &ProjectExplorerWidget::onRelativeFileDoubleClicked);
    connect(m_documentTree, &QTreeWidget::itemDoubleClicked,
        this, &ProjectExplorerWidget::onDocumentItemDoubleClicked);

    setContentRoot(QString());
    retranslateUi();
}

void ProjectExplorerWidget::setContentRoot(const QString& path)
{
    const QString normalizedPath = path.trimmed().isEmpty()
        ? QString() : EditorAssetPath::normalizedAbsolutePath(path);
    m_contentRoot = QFileInfo(normalizedPath).isDir()
        ? normalizedPath : QString();
    m_emptyProjectLabel->setVisible(m_contentRoot.isEmpty());
    m_assetBrowser->setVisible(!m_contentRoot.isEmpty());
    m_assetBrowser->setAssetsBasePath(m_contentRoot);
    rebuildDocumentItems();
}

QString ProjectExplorerWidget::contentRoot() const
{
    return m_contentRoot;
}

void ProjectExplorerWidget::setDocuments(
    const QList<ProjectDocumentState>& documents)
{
    m_documents = documents;
    rebuildDocumentItems();
}

void ProjectExplorerWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void ProjectExplorerWidget::retranslateUi()
{
    m_tabs->setTabText(0, tr("项目文件"));
    m_tabs->setTabText(1, tr("已打开文档"));
    m_emptyProjectLabel->setText(
        tr("请先打开项目并配置可编辑资源目录。"));
    m_documentTree->setHeaderLabels(
        {tr("文档"), tr("类型"), tr("状态")});
    rebuildDocumentItems();
}

void ProjectExplorerWidget::rebuildDocumentItems()
{
    m_documentTree->clear();
    for (const ProjectDocumentState& document : m_documents)
    {
        QString displayedPath = document.filePath;
        if (!m_contentRoot.isEmpty() &&
            EditorAssetPath::isLexicallyInside(
                m_contentRoot,
                document.filePath))
        {
            displayedPath = QDir(m_contentRoot).relativeFilePath(document.filePath);
        }
        displayedPath.replace('\\', '/');

        auto* item = new QTreeWidgetItem(m_documentTree);
        item->setText(0, displayedPath);
        item->setText(1, documentTypeText(document.type));
        item->setText(2, document.dirty ? tr("已修改") : tr("已保存"));
        item->setToolTip(0, document.filePath);
        item->setData(0, Qt::UserRole, document.filePath);
        if (document.dirty)
            item->setText(0, item->text(0) + QStringLiteral(" *"));
    }
}

QString ProjectExplorerWidget::documentTypeText(ProjectDocumentType type) const
{
    switch (type)
    {
    case ProjectDocumentType::Script:
        return tr("脚本");
    case ProjectDocumentType::Text:
        return tr("文本");
    case ProjectDocumentType::Map:
        return tr("地图");
    case ProjectDocumentType::NpcList:
        return tr("NPC 列表");
    case ProjectDocumentType::ObjectList:
        return tr("物体列表");
    case ProjectDocumentType::Menu:
        return tr("菜单");
    case ProjectDocumentType::Image:
        return tr("图片");
    case ProjectDocumentType::Magic:
        return tr("武功");
    case ProjectDocumentType::Goods:
        return tr("物品");
    case ProjectDocumentType::Shop:
        return tr("商店");
    case ProjectDocumentType::Dialogue:
        return tr("对话");
    case ProjectDocumentType::NpcResource:
        return tr("NPC 资源");
    }
    return QString();
}

void ProjectExplorerWidget::onRelativeFileDoubleClicked(
    const QString& relativePath)
{
    QString normalizedRelativePath;
    if (!EditorAssetPath::normalizeResourcePath(
            relativePath,
            normalizedRelativePath))
    {
        return;
    }
    const QString absolutePath =
        EditorAssetPath::normalizedAbsolutePath(
            QDir(m_contentRoot).filePath(
                normalizedRelativePath));
    if (EditorAssetPath::isLexicallyInside(
            m_contentRoot,
            absolutePath))
    {
        emit fileOpenRequested(absolutePath);
    }
}

void ProjectExplorerWidget::onDocumentItemDoubleClicked(
    QTreeWidgetItem* item, int)
{
    if (!item)
        return;
    const QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty())
        emit documentActivationRequested(filePath);
}
