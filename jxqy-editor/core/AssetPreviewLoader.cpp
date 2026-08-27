#include "AssetPreviewLoader.h"

#include "EditorAssetPath.h"
#include "LegacyTextDecoder.h"
#include "PicFileEditor.h"
#include "Util.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>
#include <string>

namespace
{
void setFailure(AssetPreviewFailure* failure, AssetPreviewError error)
{
    if (failure != nullptr)
        failure->error = error;
}

bool containsSuffix(const QStringList& suffixes, const QString& suffix)
{
    return suffixes.contains(suffix, Qt::CaseInsensitive);
}

QString legacyImageFormatName(PicType type)
{
    switch (type)
    {
    case PicType::Mpc:
        return QStringLiteral("MPC File Ver2.0");
    case PicType::Shd:
        return QStringLiteral("SHD File Ver2.0");
    case PicType::Pic:
        return QStringLiteral("PIC File Ver1.0");
    case PicType::Asf100:
        return QStringLiteral("ASF 1.00");
    case PicType::Asf101:
        return QStringLiteral("ASF 1.01");
    case PicType::Imp:
        return QStringLiteral("IMG File Ver1.0");
    case PicType::Img:
        return QStringLiteral("PNG");
    case PicType::Rle8:
        return QStringLiteral("RLE8");
    case PicType::None:
        break;
    }
    return QString();
}

bool loadLegacyImage(
    const QString& absolutePath,
    AssetPreviewData* preview)
{
    PicFileEditor editor;
    if (!editor.loadFromFile(absolutePath.toUtf8().toStdString()))
        return false;

    QImage image = editor.getFrameImage(0);
    if (image.isNull())
        return false;

    int32_t xOffset = 0;
    int32_t yOffset = 0;
    editor.getFrameOffset(0, &xOffset, &yOffset);
    PicFileData* fileData = editor.getPicFileData();
    preview->formatName = fileData
        ? legacyImageFormatName(fileData->picType) : QString();
    preview->image = std::move(image);
    preview->imageWidth = preview->image.width();
    preview->imageHeight = preview->image.height();
    preview->frameCount = editor.getFrameCount();
    preview->directions = (std::max)(1, editor.getDirection());
    preview->intervalMilliseconds = editor.getInterval();
    if (fileData && fileData->picType == PicType::Imp)
    {
        // IMG File Ver1.0 stores animation metadata in mpcFileHead, while the
        // generic PicFileEditor getters intentionally return static-image
        // defaults for PicType::Imp. The browser needs the persisted details.
        preview->directions = (std::max)(1, fileData->mpcFileHead.directions);
        preview->intervalMilliseconds = fileData->mpcFileHead.interval;
    }
    preview->xOffset = xOffset;
    preview->yOffset = yOffset;
    return true;
}

bool loadCommonImage(
    const QString& absolutePath,
    AssetPreviewData* preview)
{
    QImageReader reader(absolutePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return false;

    QByteArray format = reader.format();
    preview->formatName = format.isEmpty()
        ? preview->suffix.toUpper()
        : QString::fromLatin1(format).toUpper();
    preview->image = std::move(image);
    preview->imageWidth = preview->image.width();
    preview->imageHeight = preview->image.height();
    preview->frameCount = (std::max)(1, reader.imageCount());
    preview->directions = 1;
    preview->intervalMilliseconds = 0;
    preview->xOffset = 0;
    preview->yOffset = 0;
    return true;
}

bool decodeTextPreview(
    const QByteArray& sourceBytes,
    bool truncated,
    QString* text,
    QString* encoding)
{
    if (text == nullptr || encoding == nullptr)
        return false;

    const std::string source(
        sourceBytes.constData(),
        static_cast<std::size_t>(sourceBytes.size()));
    std::string converted;
    DecodedTextEncoding detected = DecodedTextEncoding::Utf8;
    if (!LegacyTextDecoder::decodeToUtf8(
            source,
            LegacyTextEncoding::Auto,
            converted,
            &detected,
            truncated))
    {
        return false;
    }
    *text = QString::fromUtf8(
        converted.data(), static_cast<qsizetype>(converted.size()));
    *encoding = QString::fromLatin1(
        LegacyTextDecoder::encodingName(detected));
    return true;
}

bool loadText(
    const QString& absolutePath,
    AssetPreviewData* preview,
    AssetPreviewFailure* failure)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        setFailure(failure, AssetPreviewError::TextReadFailed);
        return false;
    }

    QByteArray bytes = file.read(AssetPreviewLoader::MaximumTextPreviewBytes);
    if (bytes.contains('\0'))
    {
        setFailure(failure, AssetPreviewError::BinaryText);
        return false;
    }

    preview->textTruncated = preview->fileSize > bytes.size();
    if (!decodeTextPreview(
            bytes,
            preview->textTruncated,
            &preview->text,
            &preview->textEncoding))
    {
        setFailure(failure, AssetPreviewError::TextEncodingFailed);
        return false;
    }
    preview->formatName = preview->textEncoding;
    return true;
}
}

QStringList AssetPreviewLoader::imageSuffixes()
{
    return {
        QStringLiteral("mpc"), QStringLiteral("shd"),
        QStringLiteral("asf"), QStringLiteral("imp"),
        QStringLiteral("img"), QStringLiteral("pic"),
        QStringLiteral("png"), QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("bmp"),
        QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("tga")};
}

QStringList AssetPreviewLoader::audioSuffixes()
{
    return {
        QStringLiteral("wav"), QStringLiteral("mp3"),
        QStringLiteral("ogg")};
}

QStringList AssetPreviewLoader::textSuffixes()
{
    return {
        QStringLiteral("txt"), QStringLiteral("lua"),
        QStringLiteral("ini"), QStringLiteral("json"),
        QStringLiteral("xml"), QStringLiteral("cfg"),
        QStringLiteral("csv"), QStringLiteral("md"),
        QStringLiteral("log")};
}

bool AssetPreviewLoader::load(
    const QString& assetsRoot,
    const QString& relativePath,
    AssetPreviewData* preview,
    AssetPreviewFailure* failure)
{
    if (preview == nullptr)
    {
        setFailure(failure, AssetPreviewError::InvalidPath);
        return false;
    }
    *preview = {};
    if (failure != nullptr)
        *failure = {};

    const QFileInfo rootInfo(assetsRoot);
    if (assetsRoot.trimmed().isEmpty() ||
        !rootInfo.exists() || !rootInfo.isDir())
    {
        setFailure(failure, AssetPreviewError::InvalidRoot);
        return false;
    }

    QString normalizedRelativePath;
    QString absolutePath;
    if (!EditorAssetPath::normalizeResourcePath(
            relativePath, normalizedRelativePath) ||
        !EditorAssetPath::resolveLogicalResourcePath(
            assetsRoot, normalizedRelativePath, absolutePath))
    {
        setFailure(failure, AssetPreviewError::InvalidPath);
        return false;
    }

    const QFileInfo fileInfo(absolutePath);
    if (!fileInfo.exists())
    {
        setFailure(failure, AssetPreviewError::MissingFile);
        return false;
    }
    if (!fileInfo.isFile())
    {
        setFailure(failure, AssetPreviewError::NotRegularFile);
        return false;
    }
    if (!fileInfo.isReadable())
    {
        setFailure(failure, AssetPreviewError::FileNotReadable);
        return false;
    }

    AssetPreviewData result;
    result.absolutePath = EditorAssetPath::normalizedAbsolutePath(absolutePath);
    result.relativePath = normalizedRelativePath;
    result.suffix = fileInfo.suffix().toLower();
    result.fileSize = fileInfo.size();

    if (containsSuffix(imageSuffixes(), result.suffix))
    {
        result.kind = AssetPreviewKind::Image;
        const bool legacy = containsSuffix(
            {QStringLiteral("mpc"), QStringLiteral("shd"),
             QStringLiteral("asf"), QStringLiteral("imp"),
             QStringLiteral("img"), QStringLiteral("pic")},
            result.suffix);
        const bool loaded = legacy
            ? loadLegacyImage(result.absolutePath, &result)
            : loadCommonImage(result.absolutePath, &result);
        if (!loaded)
        {
            setFailure(failure, AssetPreviewError::ImageDecodeFailed);
            return false;
        }
    }
    else if (containsSuffix(audioSuffixes(), result.suffix))
    {
        result.kind = AssetPreviewKind::Audio;
        result.formatName = result.suffix.toUpper();
    }
    else if (containsSuffix(textSuffixes(), result.suffix))
    {
        result.kind = AssetPreviewKind::Text;
        if (!loadText(result.absolutePath, &result, failure))
            return false;
    }
    else
    {
        setFailure(failure, AssetPreviewError::UnsupportedType);
        return false;
    }

    *preview = std::move(result);
    return true;
}

bool AssetPreviewLoader::loadAnimation(
    const QString& assetsRoot,
    const QString& relativePath,
    AssetAnimationPreviewData* animation,
    AssetPreviewFailure* failure)
{
    if (animation == nullptr)
    {
        setFailure(failure, AssetPreviewError::InvalidPath);
        return false;
    }
    *animation = {};

    AssetPreviewData preview;
    if (!load(assetsRoot, relativePath, &preview, failure) ||
        preview.kind != AssetPreviewKind::Image)
    {
        return false;
    }

    QVector<QImage> frames;
    const bool legacy = containsSuffix(
        {QStringLiteral("mpc"), QStringLiteral("shd"),
         QStringLiteral("asf"), QStringLiteral("imp"),
         QStringLiteral("img"), QStringLiteral("pic")},
        preview.suffix);
    if (legacy)
    {
        PicFileEditor editor;
        if (!editor.loadFromFile(preview.absolutePath.toUtf8().toStdString()))
        {
            setFailure(failure, AssetPreviewError::ImageDecodeFailed);
            return false;
        }
        const int count = editor.getFrameCount();
        frames.reserve((std::max)(0, count));
        for (int frameIndex = 0; frameIndex < count; ++frameIndex)
        {
            QImage frame = editor.getFrameImage(frameIndex);
            if (frame.isNull())
            {
                setFailure(failure, AssetPreviewError::ImageDecodeFailed);
                return false;
            }
            frames.append(std::move(frame));
        }
    }
    else
    {
        QImageReader reader(preview.absolutePath);
        reader.setAutoTransform(true);
        do
        {
            QImage frame = reader.read();
            if (frame.isNull())
                break;
            frames.append(std::move(frame));
        }
        while (reader.jumpToNextImage());
    }

    if (frames.isEmpty())
    {
        frames.append(preview.image);
    }
    animation->preview = std::move(preview);
    animation->frames = std::move(frames);
    return true;
}
