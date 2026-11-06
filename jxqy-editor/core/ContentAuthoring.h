#pragma once

#include "DesktopRunDocumentSnapshot.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

enum class ContentKind
{
    Magic,
    Goods,
    Shop,
    Dialogue,
    Script
};

struct ContentResourceEntry
{
    QString name;
    QString fileName;
    QString sourcePath;
};

struct ContentFileEntry
{
    QString resourcePath;
    QString sourcePath;
    QString rootPath;
};

struct ContentReferenceOccurrence
{
    QString filePath;
    QString sourcePath;
    int line = 1;
    qsizetype offset = 0;
    qsizetype length = 0;
    QString value;
    QString context;
    // Null uses the containing document's map; a literal SetTrap map overrides it.
    QString scriptMap;
    QString scriptList;
};

struct ContentReferenceReport
{
    QList<ContentReferenceOccurrence> occurrences;
    QHash<QString, QByteArray> fileBytes;
    QHash<QString, QByteArray> diskBytes;
    QList<ContentReferenceOccurrence> dynamicReferences;
    QStringList errors;
};

namespace ContentAuthoring
{
QString directory(ContentKind kind);
QStringList mapNames(const QString& assetsRoot);
QString bindingScript(const QString& assetsRoot, const QString& mapName, const QString& reference);
QString createBindingScript(const QString& assetsRoot, const QString& mapName, const QString& name, QString& error);
QList<ContentResourceEntry> resources(ContentKind kind, const QString& assetsRoot);
QList<ContentFileEntry> files(const QString& assetsRoot,
    const QStringList& folders = {}, const QStringList& nameFilters = {}, QString* error = nullptr);
QString resolve(const QString& assetsRoot, const QString& resourcePath);
QString projectPath(const QString& assetsRoot, const QString& sourcePath);
QString fallback(const QString& assetsRoot, const QString& resourcePath);
QString referenceName(ContentKind kind, const QString& assetsRoot, const QString& filePath);
QString unusedFileName(ContentKind kind, const QString& assetsRoot, const QString& name);
QByteArray newDocument(ContentKind kind, const QString& name);
ContentReferenceReport references(
    ContentKind kind, const QString& assetsRoot, const QString& name,
    const QList<DesktopRunDocumentSnapshot>& snapshots = {},
    const QString& scopeFile = QString());
QByteArray replaceReferences(
    ContentKind kind, const QByteArray& bytes,
    const QList<ContentReferenceOccurrence>& occurrences,
    const QString& newName);
}
