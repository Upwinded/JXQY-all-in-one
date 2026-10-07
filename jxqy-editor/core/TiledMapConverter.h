#pragma once

#include <QString>
#include <QStringList>

// MG image-collection TMX -> existing MAP V3 + IMG frames. The caller owns
// staging/publication; original map filenames (including .tmx) remain valid.
class TiledMapConverter
{
public:
    static bool convert(const QString& sourcePath, const QString& sourceRoot,
        const QString& outputRoot, const QString& relativePath,
        QStringList& generatedFiles, QStringList& warnings, QString& error);
};
