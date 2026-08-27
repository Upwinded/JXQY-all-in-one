#pragma once

#include "../core/ProjectDocumentRegistry.h"

#include <QWidget>

class AssetBrowser;
class QLabel;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

class ProjectExplorerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectExplorerWidget(QWidget* parent = nullptr);

    void setContentRoot(const QString& path);
    QString contentRoot() const;
    void setDocuments(const QList<ProjectDocumentState>& documents);

signals:
    void fileOpenRequested(const QString& filePath);
    void documentActivationRequested(const QString& filePath);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void rebuildDocumentItems();
    QString documentTypeText(ProjectDocumentType type) const;
    void onRelativeFileDoubleClicked(const QString& relativePath);
    void onDocumentItemDoubleClicked(QTreeWidgetItem* item, int column);

    QTabWidget* m_tabs = nullptr;
    QLabel* m_emptyProjectLabel = nullptr;
    AssetBrowser* m_assetBrowser = nullptr;
    QTreeWidget* m_documentTree = nullptr;
    QString m_contentRoot;
    QList<ProjectDocumentState> m_documents;
};
