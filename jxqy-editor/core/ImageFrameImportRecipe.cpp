#include "ImageFrameImportRecipe.h"

#include "DurableFileTransaction.h"
#include "EditorAssetPath.h"
#include "ImageFrameSequence.h"
#include "Util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <string>

namespace
{
constexpr auto RecipeFormat = "jxqy-editor.image-frame-import-recipe";
constexpr int RecipeVersion = 1;

void clearFailure(ImageFrameImportRecipeFailure* failure)
{
    if (failure)
        *failure = {};
}

bool fail(
    ImageFrameImportRecipeError error,
    ImageFrameImportRecipeFailure* failure,
    const QString& detail = QString(),
    int frameIndex = -1,
    ImageFrameImportError importError = ImageFrameImportError::None)
{
    if (failure)
    {
        failure->error = error;
        failure->frameIndex = frameIndex;
        failure->importError = importError;
        failure->detail = detail;
    }
    return false;
}

bool readInteger(
    const QJsonObject& object,
    const QString& key,
    qint64 minimum,
    qint64 maximum,
    qint64* value)
{
    if (value == nullptr)
        return false;
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
        return false;
    const double number = jsonValue.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(minimum) ||
        number > static_cast<double>(maximum))
    {
        return false;
    }
    *value = static_cast<qint64>(number);
    return true;
}

bool normalizeRelativePngPath(
    const QString& input,
    QString* normalizedPath)
{
    if (normalizedPath == nullptr || input.isEmpty() ||
        input.contains(QChar::Null))
    {
        return false;
    }

    QString path = QDir::fromNativeSeparators(input);
    if (path.startsWith(QStringLiteral("//")) || path.contains(':') ||
        QDir::isAbsolutePath(path))
    {
        return false;
    }
    path = QDir::cleanPath(path);
    if (path.isEmpty() || path == QStringLiteral(".") ||
        QDir::isAbsolutePath(path) ||
        QFileInfo(path).suffix().compare(
            QStringLiteral("png"), Qt::CaseInsensitive) != 0)
    {
        return false;
    }
    *normalizedPath = path;
    return true;
}

bool recoverDirectory(
    const QString& directory,
    ImageFrameImportRecipeFailure* failure)
{
    QStringList recoveryErrors;
    if (!DurableFileTransaction::recoverPending(directory, recoveryErrors))
    {
        return fail(
            ImageFrameImportRecipeError::TransactionRecoveryFailed,
            failure,
            recoveryErrors.join('\n'));
    }
    return true;
}

bool readStrictUtf8Json(
    const QString& filePath,
    QByteArray* jsonBytes,
    ImageFrameImportRecipeFailure* failure)
{
    if (jsonBytes == nullptr)
        return false;
    jsonBytes->clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return fail(
            ImageFrameImportRecipeError::ReadFailed,
            failure,
            file.errorString());
    }
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        return fail(
            ImageFrameImportRecipeError::ReadFailed,
            failure,
            file.errorString());
    }
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    const std::string content(
        bytes.constData(), static_cast<size_t>(bytes.size()));
    if (!Util::isUtf8(
            reinterpret_cast<const uint8_t*>(content.data()),
            content.size()))
    {
        return fail(ImageFrameImportRecipeError::InvalidUtf8, failure);
    }
    *jsonBytes = std::move(bytes);
    return true;
}

QString normalizedRecipePath(const QString& recipeFilePath)
{
    return EditorAssetPath::normalizedAbsolutePath(recipeFilePath);
}
}

QString ImageFrameImportRecipe::formatIdentifier()
{
    return QString::fromLatin1(RecipeFormat);
}

int ImageFrameImportRecipe::currentVersion()
{
    return RecipeVersion;
}

bool ImageFrameImportRecipe::load(
    const QString& recipeFilePath,
    std::vector<ImageFrameImportRequest>* requests,
    ImageFrameImportRecipeFailure* failure,
    std::shared_ptr<DurableFileRecoveredReadLock>* coherentRead)
{
    clearFailure(failure);
    if (coherentRead)
        coherentRead->reset();
    if (requests == nullptr)
        return false;
    requests->clear();

    const QString normalizedPath = normalizedRecipePath(recipeFilePath);
    const QString recipeDirectory = QFileInfo(normalizedPath).absolutePath();
    if (recipeFilePath.trimmed().isEmpty() ||
        !QFileInfo(recipeDirectory).isDir())
    {
        return fail(
            ImageFrameImportRecipeError::InvalidRecipePath,
            failure,
            normalizedPath);
    }
    QStringList recoveryErrors;
    auto readLock = DurableFileTransaction::acquireRecoveredReadLock(
        recipeDirectory, recoveryErrors);
    if (!readLock)
    {
        return fail(
            ImageFrameImportRecipeError::TransactionRecoveryFailed,
            failure,
            recoveryErrors.join('\n'));
    }
    const QFileInfo recipeInfo(normalizedPath);
    if (!recipeInfo.exists() || !recipeInfo.isFile() ||
        !recipeInfo.isReadable())
    {
        return fail(
            ImageFrameImportRecipeError::InvalidRecipePath,
            failure,
            normalizedPath);
    }

    QByteArray jsonBytes;
    if (!readStrictUtf8Json(normalizedPath, &jsonBytes, failure))
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject())
    {
        return fail(
            ImageFrameImportRecipeError::InvalidJson,
            failure,
            parseError.errorString());
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("format")).isString() ||
        root.value(QStringLiteral("format")).toString() !=
            formatIdentifier())
    {
        return fail(
            ImageFrameImportRecipeError::UnsupportedFormat, failure);
    }
    qint64 version = 0;
    if (!readInteger(
            root,
            QStringLiteral("version"),
            1,
            (std::numeric_limits<int>::max)(),
            &version))
    {
        return fail(ImageFrameImportRecipeError::InvalidJson, failure);
    }
    if (version != RecipeVersion)
    {
        return fail(
            ImageFrameImportRecipeError::UnsupportedVersion,
            failure,
            QString::number(version));
    }
    if (!root.value(QStringLiteral("frames")).isArray())
    {
        return fail(ImageFrameImportRecipeError::InvalidFrames, failure);
    }
    const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
    if (frames.isEmpty() || frames.size() > MaximumImageFrameCount)
    {
        return fail(ImageFrameImportRecipeError::InvalidFrames, failure);
    }

    std::vector<ImageFrameImportRequest> parsedRequests;
    parsedRequests.reserve(static_cast<size_t>(frames.size()));
    for (qsizetype index = 0; index < frames.size(); ++index)
    {
        if (!frames.at(index).isObject())
        {
            return fail(
                ImageFrameImportRecipeError::InvalidFrames,
                failure,
                QString(),
                static_cast<int>(index));
        }
        const QJsonObject frame = frames.at(index).toObject();
        if (!frame.value(QStringLiteral("file")).isString())
        {
            return fail(
                ImageFrameImportRecipeError::InvalidFramePath,
                failure,
                QString(),
                static_cast<int>(index));
        }
        QString relativePath;
        if (!normalizeRelativePngPath(
                frame.value(QStringLiteral("file")).toString(),
                &relativePath))
        {
            return fail(
                ImageFrameImportRecipeError::InvalidFramePath,
                failure,
                frame.value(QStringLiteral("file")).toString(),
                static_cast<int>(index));
        }
        qint64 xOffset = 0;
        qint64 yOffset = 0;
        if (!readInteger(
                frame,
                QStringLiteral("xOffset"),
                (std::numeric_limits<int32_t>::min)(),
                (std::numeric_limits<int32_t>::max)(),
                &xOffset) ||
            !readInteger(
                frame,
                QStringLiteral("yOffset"),
                (std::numeric_limits<int32_t>::min)(),
                (std::numeric_limits<int32_t>::max)(),
                &yOffset))
        {
            return fail(
                ImageFrameImportRecipeError::InvalidOffset,
                failure,
                QString(),
                static_cast<int>(index));
        }
        parsedRequests.push_back({
            EditorAssetPath::normalizedAbsolutePath(
                QDir(recipeDirectory).filePath(relativePath)),
            static_cast<int32_t>(xOffset),
            static_cast<int32_t>(yOffset)});
    }

    for (const ImageFrameImportRequest& request : parsedRequests)
    {
        if (!readLock->addRecoveredReadRoot(
                QFileInfo(request.filePath).absolutePath(),
                recoveryErrors))
        {
            return fail(
                ImageFrameImportRecipeError::TransactionRecoveryFailed,
                failure,
                recoveryErrors.isEmpty()
                    ? request.filePath
                    : recoveryErrors.join('\n'));
        }
    }

    *requests = std::move(parsedRequests);
    if (coherentRead)
        *coherentRead = std::move(readLock);
    return true;
}

bool ImageFrameImportRecipe::save(
    const QString& recipeFilePath,
    const std::vector<ImageFrameImportRequest>& requests,
    ImageFrameImportRecipeFailure* failure)
{
    clearFailure(failure);
    const QString normalizedPath = normalizedRecipePath(recipeFilePath);
    const QFileInfo recipeInfo(normalizedPath);
    const QString recipeDirectory = recipeInfo.absolutePath();
    if (recipeFilePath.trimmed().isEmpty() ||
        !QFileInfo(recipeDirectory).isDir() ||
        (recipeInfo.exists() && !recipeInfo.isFile()))
    {
        return fail(
            ImageFrameImportRecipeError::InvalidRecipePath,
            failure,
            normalizedPath);
    }
    if (requests.empty() || requests.size() > MaximumImageFrameCount)
    {
        return fail(ImageFrameImportRecipeError::InvalidFrames, failure);
    }
    if (!recoverDirectory(recipeDirectory, failure))
        return false;

    QJsonArray frames;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        const ImageFrameImportProbe probe = ImageFrameImport::probeFile(
            requests[index].filePath);
        if (!probe.isValid())
        {
            return fail(
                ImageFrameImportRecipeError::FrameFileInvalid,
                failure,
                requests[index].filePath,
                static_cast<int>(index),
                probe.error);
        }
        QString relativePath = QDir(recipeDirectory).relativeFilePath(
            probe.normalizedPath);
        relativePath = QDir::fromNativeSeparators(relativePath);
        if (!normalizeRelativePngPath(relativePath, &relativePath))
        {
            return fail(
                ImageFrameImportRecipeError::InvalidFramePath,
                failure,
                requests[index].filePath,
                static_cast<int>(index));
        }

        QJsonObject frame;
        frame.insert(QStringLiteral("file"), relativePath);
        frame.insert(
            QStringLiteral("xOffset"), requests[index].xOffset);
        frame.insert(
            QStringLiteral("yOffset"), requests[index].yOffset);
        frames.append(frame);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), formatIdentifier());
    root.insert(QStringLiteral("version"), RecipeVersion);
    root.insert(QStringLiteral("frames"), frames);
    const QByteArray jsonBytes = QJsonDocument(root).toJson(
        QJsonDocument::Indented);
    if (jsonBytes.isEmpty())
        return fail(ImageFrameImportRecipeError::InvalidJson, failure);

    DurableFileTransaction transaction(recipeDirectory);
    QString transactionMessage;
    if (!transaction.addBytesWrite(
            normalizedPath, jsonBytes, transactionMessage) ||
        !transaction.commit(transactionMessage))
    {
        return fail(
            ImageFrameImportRecipeError::TransactionFailed,
            failure,
            transactionMessage);
    }
    if (failure)
        failure->warning = transactionMessage;
    return true;
}
