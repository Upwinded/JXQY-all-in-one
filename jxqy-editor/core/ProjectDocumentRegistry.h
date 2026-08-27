#pragma once

#include <QList>
#include <QString>

enum class ProjectDocumentType
{
    Script,
    Text,
    Map,
    NpcList,
    ObjectList,
    Menu,
    Image,
    Magic,
    Goods,
    Shop,
    Dialogue,
    NpcResource
};

struct ProjectDocumentState
{
    QString filePath;
    ProjectDocumentType type = ProjectDocumentType::Script;
    bool dirty = false;
};

class ProjectDocumentRegistry
{
public:
    // Document identity follows the logical path the author opened. It never
    // resolves symlinks or junctions, so two aliases to one physical file
    // remain two independently owned editor documents.
    static QString documentPathKey(const QString& filePath);

    bool registerDocument(const QString& filePath,
                          ProjectDocumentType type,
                          bool dirty = false);
    bool updateDocumentPath(const QString& currentFilePath,
                            const QString& newFilePath);
    bool updateDocumentState(const QString& filePath,
                             ProjectDocumentType type,
                             bool dirty);
    bool setDocumentDirty(const QString& filePath, bool dirty);
    bool unregisterDocument(const QString& filePath);

    bool contains(const QString& filePath) const;
    const ProjectDocumentState* findDocument(const QString& filePath) const;
    QList<ProjectDocumentState> documents() const;
    void clear();

private:
    int indexForPath(const QString& filePath) const;

    QList<ProjectDocumentState> m_documents;
};
