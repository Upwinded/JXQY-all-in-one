#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

enum class AssetPreviewKind
{
    None,
    Image,
    Audio,
    Text
};

enum class AssetPreviewError
{
    None,
    InvalidRoot,
    InvalidPath,
    MissingFile,
    NotRegularFile,
    FileNotReadable,
    UnsupportedType,
    ImageDecodeFailed,
    TextReadFailed,
    BinaryText,
    TextEncodingFailed
};

struct AssetPreviewFailure
{
    AssetPreviewError error = AssetPreviewError::None;
};

struct AssetPreviewData
{
    AssetPreviewKind kind = AssetPreviewKind::None;
    QString absolutePath;
    QString relativePath;
    QString suffix;
    QString formatName;
    qint64 fileSize = 0;

    QImage image;
    int imageWidth = 0;
    int imageHeight = 0;
    int frameCount = 0;
    int directions = 0;
    int intervalMilliseconds = 0;
    int xOffset = 0;
    int yOffset = 0;

    QString text;
    QString textEncoding;
    bool textTruncated = false;
};

struct AssetAnimationPreviewData
{
    AssetPreviewData preview;
    QVector<QImage> frames;
};

class AssetPreviewLoader
{
public:
    static constexpr qint64 MaximumTextPreviewBytes = 256 * 1024;

    static QStringList imageSuffixes();
    static QStringList audioSuffixes();
    static QStringList textSuffixes();

    static bool load(
        const QString& assetsRoot,
        const QString& relativePath,
        AssetPreviewData* preview,
        AssetPreviewFailure* failure = nullptr);
    static bool loadAnimation(
        const QString& assetsRoot,
        const QString& relativePath,
        AssetAnimationPreviewData* animation,
        AssetPreviewFailure* failure = nullptr);
};
