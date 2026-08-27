#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

namespace EditorAssetPath
{
inline Qt::CaseSensitivity caseSensitivity(const QString& path = QString())
{
#ifdef Q_OS_WIN
    Q_UNUSED(path);
    return Qt::CaseInsensitive;
#elif defined(Q_OS_MACOS)
    QString probe = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!QFileInfo::exists(probe))
    {
        const QString parent = QFileInfo(probe).dir().absolutePath();
        if (parent == probe)
        {
            break;
        }
        probe = parent;
    }
    const QByteArray nativePath = QFile::encodeName(probe);
    const long isCaseSensitive =
        ::pathconf(nativePath.constData(), _PC_CASE_SENSITIVE);
    return isCaseSensitive == 0
        ? Qt::CaseInsensitive
        : Qt::CaseSensitive;
#else
    Q_UNUSED(path);
    return Qt::CaseSensitive;
#endif
}

inline QString normalizedAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

inline bool isLexicallyInside(const QString& basePath, const QString& candidatePath)
{
    if (basePath.isEmpty() || candidatePath.isEmpty())
        return false;

    const QString base = normalizedAbsolutePath(basePath);
    const QString candidate = normalizedAbsolutePath(candidatePath);
    const Qt::CaseSensitivity sensitivity = caseSensitivity(base);
    if (candidate.compare(base, sensitivity) == 0)
        return true;

    QString childPrefix = base;
    if (!childPrefix.endsWith('/') &&
        !childPrefix.endsWith(QDir::separator()))
    {
        childPrefix += '/';
    }
    return candidate.startsWith(childPrefix, sensitivity);
}

inline bool isInside(const QString& basePath, const QString& candidatePath)
{
    if (!isLexicallyInside(basePath, candidatePath))
        return false;

    const QString base = normalizedAbsolutePath(basePath);
    const QString candidate = normalizedAbsolutePath(candidatePath);
    QString canonicalBase = QFileInfo(base).canonicalFilePath();
    if (canonicalBase.isEmpty())
        canonicalBase = base;

    // Resolve the deepest existing ancestor. This rejects a future target
    // routed outside the pack through an existing junction or symlink.
    QString probe = candidate;
    while (!QFileInfo::exists(probe))
    {
        const QString parent = QFileInfo(probe).dir().absolutePath();
        if (parent == probe)
            break;
        probe = parent;
    }

    QString canonicalProbe = QFileInfo(probe).canonicalFilePath();
    if (canonicalProbe.isEmpty())
        canonicalProbe = normalizedAbsolutePath(probe);
    return isLexicallyInside(canonicalBase, canonicalProbe);
}

inline bool normalizeResourcePath(const QString& input, QString& normalized)
{
    normalized = input.trimmed();
    normalized.replace('\\', '/');
    if (normalized.startsWith("//") || normalized.contains(':'))
        return false;

    // Historical game resource names may begin with one separator, but they
    // are still relative to a resource pack rather than host absolute paths.
    while (normalized.startsWith('/'))
        normalized.remove(0, 1);
    normalized = QDir::cleanPath(normalized);
    if (normalized.isEmpty() || normalized == "." ||
        QDir::isAbsolutePath(normalized))
    {
        return false;
    }

    const QStringList segments = normalized.split('/', Qt::KeepEmptyParts);
    for (const QString& segment : segments)
    {
        if (segment == "..")
            return false;
    }

    for (int index = 0; index < normalized.size(); ++index)
    {
        const ushort character = normalized.at(index).unicode();
        if (character >= 'A' && character <= 'Z')
        {
            normalized[index] = QChar(
                static_cast<ushort>(character + ('a' - 'A')));
        }
    }
    return true;
}

// Formal resource reads and references use the canonical lowercase logical
// path. A descendant symlink/junction may still resolve outside rootPath,
// while ".." and host-absolute virtual paths remain invalid.
// Do not use these helpers to authorize an editor-managed write target.
inline QString logicalComparisonKey(const QString& path)
{
    QString key = normalizedAbsolutePath(path);
    if (caseSensitivity(key) == Qt::CaseInsensitive)
        key = key.toLower();
    return key;
}

inline bool makeLogicalResourceRelativePath(const QString& rootPath,
                                            const QString& candidatePath,
                                            QString& relativePath)
{
    relativePath.clear();
    if (!isLexicallyInside(rootPath, candidatePath))
        return false;

    const QString root = normalizedAbsolutePath(rootPath);
    const QString candidate = normalizedAbsolutePath(candidatePath);
    return normalizeResourcePath(
        QDir(root).relativeFilePath(candidate), relativePath);
}

inline bool resolveLogicalResourcePath(const QString& rootPath,
                                       const QString& resourcePath,
                                       QString& absolutePath)
{
    QString normalizedResource;
    if (rootPath.isEmpty() ||
        !normalizeResourcePath(resourcePath, normalizedResource))
    {
        absolutePath.clear();
        return false;
    }

    const QString root = normalizedAbsolutePath(rootPath);
    const QString candidate = normalizedAbsolutePath(
        QDir(root).filePath(normalizedResource));
    if (!isLexicallyInside(root, candidate))
    {
        absolutePath.clear();
        return false;
    }
    absolutePath = candidate;
    return true;
}

inline bool resolveInside(const QString& rootPath, const QString& resourcePath,
                          QString& absolutePath)
{
    QString normalizedResource;
    if (rootPath.isEmpty() ||
        !normalizeResourcePath(resourcePath, normalizedResource))
    {
        absolutePath.clear();
        return false;
    }

    const QString candidate = normalizedAbsolutePath(
        QDir(rootPath).filePath(normalizedResource));
    if (!isInside(rootPath, candidate))
    {
        absolutePath.clear();
        return false;
    }
    absolutePath = candidate;
    return true;
}

inline QString comparisonKey(const QString& path)
{
    QString key = normalizedAbsolutePath(path);
    QString probe = key;
    QStringList missingSegments;
    while (!QFileInfo::exists(probe))
    {
        const QFileInfo probeInfo(probe);
        const QString segment = probeInfo.fileName();
        const QString parent = probeInfo.dir().absolutePath();
        if (!segment.isEmpty())
        {
            missingSegments.prepend(segment);
        }
        if (parent == probe)
        {
            break;
        }
        probe = parent;
    }
    const QString canonicalProbe =
        QFileInfo(probe).canonicalFilePath();
    if (!canonicalProbe.isEmpty())
    {
        key = canonicalProbe;
        for (const QString& segment : missingSegments)
        {
            key = QDir(key).filePath(segment);
        }
        key = QDir::cleanPath(key);
    }
    if (caseSensitivity(probe) == Qt::CaseInsensitive)
    {
        key = key.toLower();
    }
    return key;
}
}
