#include "ProjectDocumentRegistry.h"

#include "EditorAssetPath.h"

namespace
{
QString normalizeDocumentPath(const QString& filePath)
{
    if (filePath.trimmed().isEmpty())
        return QString();
    return EditorAssetPath::normalizedAbsolutePath(filePath);
}
}

QString ProjectDocumentRegistry::documentPathKey(
    const QString& filePath)
{
    QString key = normalizeDocumentPath(filePath);
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return key;
}

bool ProjectDocumentRegistry::registerDocument(
    const QString& filePath, ProjectDocumentType type, bool dirty)
{
    const QString normalizedPath = normalizeDocumentPath(filePath);
    if (normalizedPath.isEmpty() || indexForPath(normalizedPath) >= 0)
        return false;

    m_documents.append({normalizedPath, type, dirty});
    return true;
}

bool ProjectDocumentRegistry::updateDocumentPath(
    const QString& currentFilePath, const QString& newFilePath)
{
    const int currentIndex = indexForPath(currentFilePath);
    const QString normalizedNewPath = normalizeDocumentPath(newFilePath);
    if (currentIndex < 0 || normalizedNewPath.isEmpty())
        return false;

    const int existingIndex = indexForPath(normalizedNewPath);
    if (existingIndex >= 0 && existingIndex != currentIndex)
        return false;

    m_documents[currentIndex].filePath = normalizedNewPath;
    return true;
}

bool ProjectDocumentRegistry::setDocumentDirty(
    const QString& filePath, bool dirty)
{
    const int index = indexForPath(filePath);
    if (index < 0)
        return false;
    m_documents[index].dirty = dirty;
    return true;
}

bool ProjectDocumentRegistry::updateDocumentState(
    const QString& filePath, ProjectDocumentType type, bool dirty)
{
    const int index = indexForPath(filePath);
    if (index < 0)
        return false;
    m_documents[index].type = type;
    m_documents[index].dirty = dirty;
    return true;
}

bool ProjectDocumentRegistry::unregisterDocument(const QString& filePath)
{
    const int index = indexForPath(filePath);
    if (index < 0)
        return false;
    m_documents.removeAt(index);
    return true;
}

bool ProjectDocumentRegistry::contains(const QString& filePath) const
{
    return indexForPath(filePath) >= 0;
}

const ProjectDocumentState* ProjectDocumentRegistry::findDocument(
    const QString& filePath) const
{
    const int index = indexForPath(filePath);
    return index < 0 ? nullptr : &m_documents[index];
}

QList<ProjectDocumentState> ProjectDocumentRegistry::documents() const
{
    return m_documents;
}

void ProjectDocumentRegistry::clear()
{
    m_documents.clear();
}

int ProjectDocumentRegistry::indexForPath(const QString& filePath) const
{
    const QString key = documentPathKey(filePath);
    if (key.isEmpty())
        return -1;

    for (int index = 0; index < m_documents.size(); ++index)
    {
        if (documentPathKey(m_documents[index].filePath) == key)
        {
            return index;
        }
    }
    return -1;
}
