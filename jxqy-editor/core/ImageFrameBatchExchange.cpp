#include "ImageFrameBatchExchange.h"

#include "AuthoringMutationGate.h"
#include "DurableFileTransaction.h"

#include <QBuffer>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
constexpr auto ManifestFormat = "jxqy-editor.image-frame-batch";
constexpr int ManifestVersion = 1;
constexpr auto FingerprintDomain = "jxqy-editor.image-frame-document.v1";

struct ParsedManifestFrame
{
    int index = -1;
    QString fileName;
    int width = 0;
    int height = 0;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    QString exportedSha256;
};

struct ParsedManifest
{
    ImageFrameBatchScope scope = ImageFrameBatchScope::Selected;
    QString documentName;
    int frameCount = 0;
    int directionCount = 0;
    int intervalMilliseconds = 0;
    QString documentFingerprintSha256;
    std::vector<ParsedManifestFrame> frames;
};

void clearFailure(ImageFrameBatchFailure* failure)
{
    if (failure)
        *failure = {};
}

bool fail(
    ImageFrameBatchError error,
    ImageFrameBatchFailure* failure,
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

void addInt32(QCryptographicHash& hash, int32_t value)
{
    const quint32 encoded = qToLittleEndian(
        static_cast<quint32>(value));
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(&encoded),
        static_cast<qsizetype>(sizeof(encoded))));
}

bool canonicalPng(const QImage& source, QImage* normalized, QByteArray* bytes)
{
    if (normalized == nullptr || bytes == nullptr || source.isNull())
        return false;
    *normalized = source.convertToFormat(QImage::Format_ARGB32);
    bytes->clear();
    QBuffer buffer(bytes);
    return buffer.open(QIODevice::WriteOnly) &&
           normalized->save(&buffer, "PNG") && !bytes->isEmpty();
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool isLowerSha256(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(value).hasMatch();
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

bool parseManifest(
    const QByteArray& bytes,
    ParsedManifest* parsed,
    ImageFrameBatchFailure* failure)
{
    if (parsed == nullptr)
        return fail(ImageFrameBatchError::InvalidManifest, failure);
    *parsed = {};

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return fail(
            ImageFrameBatchError::ManifestParseFailed,
            failure,
            parseError.errorString());
    }

    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("format")).isString() ||
        root.value(QStringLiteral("format")).toString() !=
            QString::fromLatin1(ManifestFormat))
    {
        return fail(ImageFrameBatchError::UnsupportedFormat, failure);
    }
    qint64 version = 0;
    if (!readInteger(root, QStringLiteral("version"), 1,
                     (std::numeric_limits<int>::max)(), &version))
    {
        return fail(ImageFrameBatchError::InvalidManifest, failure);
    }
    if (version != ManifestVersion)
        return fail(ImageFrameBatchError::UnsupportedVersion, failure);

    const QString scope = root.value(QStringLiteral("scope")).toString();
    if (scope == QStringLiteral("selected"))
        parsed->scope = ImageFrameBatchScope::Selected;
    else if (scope == QStringLiteral("all"))
        parsed->scope = ImageFrameBatchScope::All;
    else
        return fail(ImageFrameBatchError::InvalidManifest, failure);

    if (!root.value(QStringLiteral("source")).isObject() ||
        !root.value(QStringLiteral("frames")).isArray())
    {
        return fail(ImageFrameBatchError::InvalidManifest, failure);
    }
    const QJsonObject source = root.value(QStringLiteral("source")).toObject();
    if (!source.value(QStringLiteral("documentName")).isString() ||
        source.value(QStringLiteral("documentName")).toString().isEmpty())
    {
        return fail(ImageFrameBatchError::InvalidManifest, failure);
    }
    parsed->documentName =
        source.value(QStringLiteral("documentName")).toString();

    qint64 frameCount = 0;
    qint64 directionCount = 0;
    qint64 interval = 0;
    if (!readInteger(source, QStringLiteral("frameCount"), 1,
                     MaximumImageFrameCount, &frameCount) ||
        !readInteger(source, QStringLiteral("directionCount"), 0, 255,
                     &directionCount) ||
        !readInteger(source, QStringLiteral("intervalMilliseconds"), 0,
                     99999, &interval))
    {
        return fail(ImageFrameBatchError::InvalidManifest, failure);
    }
    parsed->frameCount = static_cast<int>(frameCount);
    parsed->directionCount = static_cast<int>(directionCount);
    parsed->intervalMilliseconds = static_cast<int>(interval);
    parsed->documentFingerprintSha256 = source.value(
        QStringLiteral("documentFingerprintSha256")).toString();
    if (!isLowerSha256(parsed->documentFingerprintSha256))
        return fail(ImageFrameBatchError::InvalidManifest, failure);

    const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
    if (frames.isEmpty() || frames.size() > parsed->frameCount)
        return fail(ImageFrameBatchError::InvalidManifest, failure);

    int previousIndex = -1;
    parsed->frames.reserve(frames.size());
    for (const QJsonValue& value : frames)
    {
        if (!value.isObject())
            return fail(ImageFrameBatchError::InvalidManifest, failure);
        const QJsonObject object = value.toObject();
        qint64 index = 0;
        qint64 width = 0;
        qint64 height = 0;
        qint64 xOffset = 0;
        qint64 yOffset = 0;
        if (!readInteger(object, QStringLiteral("index"), 0,
                         parsed->frameCount - 1, &index) ||
            !readInteger(object, QStringLiteral("width"), 1,
                         (std::numeric_limits<int>::max)(), &width) ||
            !readInteger(object, QStringLiteral("height"), 1,
                         (std::numeric_limits<int>::max)(), &height) ||
            !readInteger(object, QStringLiteral("xOffset"),
                         (std::numeric_limits<int32_t>::min)(),
                         (std::numeric_limits<int32_t>::max)(), &xOffset) ||
            !readInteger(object, QStringLiteral("yOffset"),
                         (std::numeric_limits<int32_t>::min)(),
                         (std::numeric_limits<int32_t>::max)(), &yOffset))
        {
            return fail(ImageFrameBatchError::InvalidManifest, failure);
        }
        if (index <= previousIndex)
            return fail(ImageFrameBatchError::InvalidManifest, failure);

        ParsedManifestFrame frame;
        frame.index = static_cast<int>(index);
        frame.fileName = object.value(QStringLiteral("file")).toString();
        frame.width = static_cast<int>(width);
        frame.height = static_cast<int>(height);
        frame.xOffset = static_cast<int32_t>(xOffset);
        frame.yOffset = static_cast<int32_t>(yOffset);
        frame.exportedSha256 =
            object.value(QStringLiteral("exportedSha256")).toString();
        if (frame.fileName != ImageFrameBatchExchange::frameFileName(frame.index))
        {
            return fail(
                ImageFrameBatchError::UnsafeFrameFileName,
                failure,
                frame.fileName,
                frame.index);
        }
        if (!isLowerSha256(frame.exportedSha256))
            return fail(ImageFrameBatchError::InvalidManifest, failure);
        parsed->frames.push_back(std::move(frame));
        previousIndex = static_cast<int>(index);
    }

    if (parsed->scope == ImageFrameBatchScope::All)
    {
        if (static_cast<int>(parsed->frames.size()) != parsed->frameCount)
            return fail(ImageFrameBatchError::InvalidManifest, failure);
        for (int index = 0; index < parsed->frameCount; index++)
        {
            if (parsed->frames[static_cast<size_t>(index)].index != index)
                return fail(ImageFrameBatchError::InvalidManifest, failure);
        }
    }
    return true;
}

bool readFile(const QString& filePath, QByteArray* bytes)
{
    if (bytes == nullptr)
        return false;
    bytes->clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *bytes = file.readAll();
    return file.error() == QFileDevice::NoError;
}

bool recoverDirectory(
    const QString& directory,
    ImageFrameBatchFailure* failure)
{
    QStringList recoveryErrors;
    if (!DurableFileTransaction::recoverPending(directory, recoveryErrors))
    {
        return fail(
            ImageFrameBatchError::TransactionRecoveryFailed,
            failure,
            recoveryErrors.join('\n'));
    }
    return true;
}
}

QString ImageFrameBatchExchange::manifestFileName()
{
    return QStringLiteral("jxqy-image-frames.json");
}

QString ImageFrameBatchExchange::frameFileName(int index)
{
    if (index < 0 || index >= MaximumImageFrameCount)
        return QString();
    return QStringLiteral("frame_%1.png").arg(index, 4, 10, QChar('0'));
}

QString ImageFrameBatchExchange::documentFingerprintSha256(
    const ImageFrameBatchDocument& document)
{
    if (document.frames.empty() ||
        document.frames.size() > MaximumImageFrameCount ||
        document.directionCount < 0 || document.directionCount > 255 ||
        document.intervalMilliseconds < 0 ||
        document.intervalMilliseconds > 99999)
    {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(
        FingerprintDomain, sizeof(FingerprintDomain) - 1));
    addInt32(hash, document.directionCount);
    addInt32(hash, document.intervalMilliseconds);
    addInt32(hash, static_cast<int32_t>(document.frames.size()));
    for (int index = 0;
         index < static_cast<int>(document.frames.size());
         index++)
    {
        const ImageFrameData& frame = document.frames[static_cast<size_t>(index)];
        if (frame.decodedImage.isNull())
            return QString();
        const QImage image = frame.decodedImage.convertToFormat(
            QImage::Format_RGBA8888);
        if (image.isNull() || image.width() <= 0 || image.height() <= 0)
            return QString();
        addInt32(hash, index);
        addInt32(hash, image.width());
        addInt32(hash, image.height());
        addInt32(hash, frame.xOffset);
        addInt32(hash, frame.yOffset);
        addInt32(hash, frame.reserved);
        const qsizetype rowBytes = static_cast<qsizetype>(image.width()) * 4;
        for (int row = 0; row < image.height(); row++)
        {
            hash.addData(QByteArrayView(
                reinterpret_cast<const char*>(image.constScanLine(row)),
                rowBytes));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool ImageFrameBatchExchange::prepareExport(
    const ImageFrameBatchDocument& document,
    ImageFrameBatchScope scope,
    const std::vector<int>& selectedIndices,
    ImageFrameBatchPreparedExport* preparedExport,
    ImageFrameBatchFailure* failure)
{
    clearFailure(failure);
    if (preparedExport == nullptr)
        return fail(ImageFrameBatchError::InvalidDocument, failure);
    *preparedExport = {};

    const QString fingerprint = documentFingerprintSha256(document);
    if (document.documentName.isEmpty() || fingerprint.isEmpty())
        return fail(ImageFrameBatchError::InvalidDocument, failure);

    std::vector<int> indices;
    if (scope == ImageFrameBatchScope::All)
    {
        indices.reserve(document.frames.size());
        for (int index = 0;
             index < static_cast<int>(document.frames.size());
             index++)
        {
            indices.push_back(index);
        }
    }
    else
    {
        if (selectedIndices.empty())
            return fail(ImageFrameBatchError::EmptySelection, failure);
        indices = selectedIndices;
        std::sort(indices.begin(), indices.end());
        for (size_t position = 0; position < indices.size(); position++)
        {
            if (indices[position] < 0 ||
                indices[position] >= static_cast<int>(document.frames.size()))
            {
                return fail(
                    ImageFrameBatchError::IndexOutOfRange,
                    failure,
                    QString(),
                    indices[position]);
            }
            if (position > 0 && indices[position] == indices[position - 1])
            {
                return fail(
                    ImageFrameBatchError::DuplicateIndex,
                    failure,
                    QString(),
                    indices[position]);
            }
        }
    }

    QJsonArray jsonFrames;
    std::vector<ImageFrameBatchExportFile> files;
    files.reserve(indices.size());
    for (int index : indices)
    {
        const ImageFrameData& source = document.frames[static_cast<size_t>(index)];
        QImage normalized;
        QByteArray pngBytes;
        if (!canonicalPng(source.decodedImage, &normalized, &pngBytes))
        {
            return fail(
                ImageFrameBatchError::EncodeFailed,
                failure,
                QString(),
                index);
        }

        ImageFrameBatchExportFile file;
        file.index = index;
        file.fileName = frameFileName(index);
        file.pngBytes = pngBytes;
        files.push_back(file);

        QJsonObject jsonFrame;
        jsonFrame.insert(QStringLiteral("index"), index);
        jsonFrame.insert(QStringLiteral("file"), file.fileName);
        jsonFrame.insert(QStringLiteral("width"), normalized.width());
        jsonFrame.insert(QStringLiteral("height"), normalized.height());
        jsonFrame.insert(QStringLiteral("xOffset"), source.xOffset);
        jsonFrame.insert(QStringLiteral("yOffset"), source.yOffset);
        jsonFrame.insert(QStringLiteral("exportedSha256"), sha256(pngBytes));
        jsonFrames.append(jsonFrame);
    }

    QJsonObject source;
    source.insert(QStringLiteral("documentName"), document.documentName);
    source.insert(QStringLiteral("frameCount"),
                  static_cast<int>(document.frames.size()));
    source.insert(QStringLiteral("directionCount"), document.directionCount);
    source.insert(
        QStringLiteral("intervalMilliseconds"), document.intervalMilliseconds);
    source.insert(
        QStringLiteral("documentFingerprintSha256"), fingerprint);

    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(ManifestFormat));
    root.insert(QStringLiteral("version"), ManifestVersion);
    root.insert(
        QStringLiteral("scope"),
        scope == ImageFrameBatchScope::All
            ? QStringLiteral("all") : QStringLiteral("selected"));
    root.insert(QStringLiteral("source"), source);
    root.insert(QStringLiteral("frames"), jsonFrames);

    preparedExport->manifestBytes =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    preparedExport->documentFingerprintSha256 = fingerprint;
    preparedExport->files = std::move(files);
    return true;
}

bool ImageFrameBatchExchange::publishExport(
    const QString& outputDirectory,
    const ImageFrameBatchPreparedExport& preparedExport,
    bool replaceExistingBatch,
    ImageFrameBatchFailure* failure)
{
    clearFailure(failure);
    const QFileInfo directoryInfo(outputDirectory);
    if (!directoryInfo.exists() || !directoryInfo.isDir() ||
        !directoryInfo.isReadable() || !directoryInfo.isWritable() ||
        preparedExport.files.empty() || preparedExport.manifestBytes.isEmpty())
    {
        return fail(ImageFrameBatchError::InvalidOutputDirectory, failure);
    }
    const QString directory = QDir::cleanPath(directoryInfo.absoluteFilePath());
    if (AuthoringMutationGate::wouldReplaceResourceCollection(directory))
    {
        return fail(
            ImageFrameBatchError::InvalidOutputDirectory,
            failure,
            directory);
    }
    if (!recoverDirectory(directory, failure))
        return false;
    auto mutationLease = AuthoringMutationGate::instance().
        acquireMutationLeaseForPath(directory);
    if (!mutationLease)
    {
        return fail(
            ImageFrameBatchError::TransactionFailed,
            failure,
            directory);
    }

    ParsedManifest newManifest;
    if (!parseManifest(preparedExport.manifestBytes, &newManifest, failure) ||
        newManifest.documentFingerprintSha256 !=
            preparedExport.documentFingerprintSha256 ||
        newManifest.frames.size() != preparedExport.files.size())
    {
        if (!failure || failure->error == ImageFrameBatchError::None)
            fail(ImageFrameBatchError::InvalidManifest, failure);
        return false;
    }
    for (size_t position = 0;
         position < preparedExport.files.size();
         position++)
    {
        const ImageFrameBatchExportFile& file = preparedExport.files[position];
        const ParsedManifestFrame& manifestFrame = newManifest.frames[position];
        if (file.index != manifestFrame.index ||
            file.fileName != manifestFrame.fileName ||
            sha256(file.pngBytes) != manifestFrame.exportedSha256)
        {
            return fail(ImageFrameBatchError::InvalidManifest, failure);
        }
    }

    const QString manifestPath = QDir(directory).filePath(manifestFileName());
    ParsedManifest oldManifest;
    bool hasOldManifest = QFileInfo::exists(manifestPath);
    if (hasOldManifest)
    {
        QByteArray oldBytes;
        if (!readFile(manifestPath, &oldBytes))
        {
            return fail(
                ImageFrameBatchError::ManifestReadFailed,
                failure,
                manifestPath);
        }
        ImageFrameBatchFailure oldFailure;
        if (!parseManifest(oldBytes, &oldManifest, &oldFailure))
        {
            return fail(
                oldFailure.error,
                failure,
                oldFailure.detail,
                oldFailure.frameIndex,
                oldFailure.importError);
        }
        if (!replaceExistingBatch)
        {
            return fail(
                ImageFrameBatchError::ReplacementConfirmationRequired,
                failure,
                manifestPath);
        }
    }

    std::set<QString> oldManagedFiles;
    for (const ParsedManifestFrame& frame : oldManifest.frames)
        oldManagedFiles.insert(frame.fileName);

    std::set<QString> newManagedFiles;
    for (const ImageFrameBatchExportFile& file : preparedExport.files)
    {
        if (file.index < 0 || file.fileName != frameFileName(file.index) ||
            file.pngBytes.isEmpty() || !newManagedFiles.insert(file.fileName).second)
        {
            return fail(ImageFrameBatchError::InvalidManifest, failure);
        }
        const QString targetPath = QDir(directory).filePath(file.fileName);
        if (QFileInfo::exists(targetPath) &&
            oldManagedFiles.find(file.fileName) == oldManagedFiles.end())
        {
            return fail(
                ImageFrameBatchError::TargetCollision,
                failure,
                targetPath,
                file.index);
        }
    }

    DurableFileTransaction transaction(directory);
    QString transactionMessage;
    for (const ImageFrameBatchExportFile& file : preparedExport.files)
    {
        if (!transaction.addBytesWrite(
                QDir(directory).filePath(file.fileName),
                file.pngBytes,
                transactionMessage))
        {
            return fail(
                ImageFrameBatchError::TransactionFailed,
                failure,
                transactionMessage,
                file.index);
        }
    }
    for (const QString& oldFileName : oldManagedFiles)
    {
        if (newManagedFiles.find(oldFileName) != newManagedFiles.end())
            continue;
        const QString oldPath = QDir(directory).filePath(oldFileName);
        if (QFileInfo::exists(oldPath) &&
            !transaction.addRemoval(oldPath, transactionMessage))
        {
            return fail(
                ImageFrameBatchError::TransactionFailed,
                failure,
                transactionMessage);
        }
    }
    if (!transaction.addBytesWrite(
            manifestPath, preparedExport.manifestBytes, transactionMessage) ||
        !transaction.commit(transactionMessage))
    {
        return fail(
            ImageFrameBatchError::TransactionFailed,
            failure,
            transactionMessage);
    }
    if (failure)
        failure->detail = transactionMessage;
    return true;
}

bool ImageFrameBatchExchange::prepareReimport(
    const QString& manifestPath,
    const ImageFrameBatchDocument& currentDocument,
    ImageFrameSequenceEdit* edit,
    ImageFrameBatchFailure* failure)
{
    clearFailure(failure);
    if (edit == nullptr)
        return fail(ImageFrameBatchError::InvalidDocument, failure);
    *edit = {};

    const QString directory = QFileInfo(manifestPath).absolutePath();
    QStringList recoveryErrors;
    const auto coherentRead =
        DurableFileTransaction::acquireRecoveredReadLock(
            directory, recoveryErrors);
    if (!coherentRead)
    {
        return fail(
            ImageFrameBatchError::TransactionRecoveryFailed,
            failure,
            recoveryErrors.join('\n'));
    }

    const QFileInfo manifestInfo(manifestPath);
    if (!manifestInfo.exists() || !manifestInfo.isFile() ||
        !manifestInfo.isReadable() ||
        manifestInfo.fileName() != manifestFileName())
    {
        return fail(
            ImageFrameBatchError::ManifestReadFailed,
            failure,
            manifestPath);
    }
    QByteArray manifestBytes;
    if (!readFile(manifestPath, &manifestBytes))
    {
        return fail(
            ImageFrameBatchError::ManifestReadFailed,
            failure,
            manifestPath);
    }
    ParsedManifest manifest;
    if (!parseManifest(manifestBytes, &manifest, failure))
        return false;

    const QString currentFingerprint =
        documentFingerprintSha256(currentDocument);
    if (currentFingerprint.isEmpty() ||
        manifest.frameCount != static_cast<int>(currentDocument.frames.size()) ||
        manifest.directionCount != currentDocument.directionCount ||
        manifest.intervalMilliseconds != currentDocument.intervalMilliseconds ||
        manifest.documentFingerprintSha256 != currentFingerprint)
    {
        return fail(ImageFrameBatchError::DocumentChanged, failure);
    }

    std::vector<ImageFrameImportRequest> importRequests;
    importRequests.reserve(manifest.frames.size());
    for (const ParsedManifestFrame& manifestFrame : manifest.frames)
    {
        importRequests.push_back({
            QDir(directory).filePath(manifestFrame.fileName),
            manifestFrame.xOffset,
            manifestFrame.yOffset});
    }
    std::vector<ImageFrameData> importedFrames;
    int failedImportIndex = -1;
    ImageFrameImportError importError = ImageFrameImportError::None;
    if (!ImageFrameImport::prepareRequests(
            importRequests,
            &importedFrames,
            &failedImportIndex,
            &importError))
    {
        const QString failedPath = failedImportIndex >= 0
            ? importRequests[static_cast<size_t>(failedImportIndex)].filePath
            : manifestPath;
        const int failedFrameIndex = failedImportIndex >= 0
            ? manifest.frames[static_cast<size_t>(failedImportIndex)].index
            : -1;
        return fail(
            ImageFrameBatchError::FrameFileInvalid,
            failure,
            failedPath,
            failedFrameIndex,
            importError);
    }

    std::vector<ImageFrameData> replacementFrames = currentDocument.frames;
    std::vector<int> selectedIndices;
    selectedIndices.reserve(manifest.frames.size());
    bool changed = false;
    for (size_t importIndex = 0;
         importIndex < manifest.frames.size(); ++importIndex)
    {
        const ParsedManifestFrame& manifestFrame =
            manifest.frames[importIndex];
        ImageFrameData importedFrame = std::move(
            importedFrames[importIndex]);

        const ImageFrameData& current = currentDocument.frames[
            static_cast<size_t>(manifestFrame.index)];
        const bool pixelsDiffer =
            current.decodedImage.convertToFormat(QImage::Format_ARGB32) !=
            importedFrame.decodedImage;
        const bool offsetDiffers = current.xOffset != importedFrame.xOffset ||
                                   current.yOffset != importedFrame.yOffset;
        if (pixelsDiffer || offsetDiffers)
        {
            if (!pixelsDiffer)
            {
                const int32_t newXOffset = importedFrame.xOffset;
                const int32_t newYOffset = importedFrame.yOffset;
                importedFrame = current;
                importedFrame.xOffset = newXOffset;
                importedFrame.yOffset = newYOffset;
            }
            importedFrame.reserved = current.reserved;
            replacementFrames[static_cast<size_t>(manifestFrame.index)] =
                std::move(importedFrame);
            changed = true;
        }
        selectedIndices.push_back(manifestFrame.index);
    }

    if (!changed)
        return true;
    edit->frames = std::move(replacementFrames);
    edit->selectedIndices = std::move(selectedIndices);
    edit->currentIndex = edit->selectedIndices.front();
    edit->changed = true;
    return true;
}
