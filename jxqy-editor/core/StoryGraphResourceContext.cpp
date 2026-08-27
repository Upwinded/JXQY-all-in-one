#include "StoryGraphResourceContext.h"

#include "EditorAssetPath.h"
#include "INIFileEditor.h"
#include "ScriptConverter.h"

#include "../../src/File/RootedResourceReader.h"
#include "../../src/Resource/ResourceCatalog.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QObject>

#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;

constexpr std::size_t MaximumMapNameIniBytes =
    1024 * 1024;

fs::path hostPath(const QString& path)
{
    const QByteArray utf8 = path.toUtf8();
    return fs::u8path(
        utf8.constData(),
        utf8.constData() + utf8.size());
}

QString hostPathText(const fs::path& path)
{
    const auto bytes = path.generic_u8string();
    return EditorAssetPath::normalizedAbsolutePath(
        QString::fromUtf8(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size())));
}

QString utf8Text(const std::string& text)
{
    return QString::fromUtf8(
        text.data(),
        static_cast<int>(text.size()));
}

StoryGraphContentRootKind graphRootKind(
    RuntimeResource::ContentRootKind kind)
{
    switch (kind)
    {
    case RuntimeResource::ContentRootKind::Active:
        return StoryGraphContentRootKind::Active;
    case RuntimeResource::ContentRootKind::DependencyId:
        return StoryGraphContentRootKind::DependencyId;
    case RuntimeResource::ContentRootKind::Common:
        return StoryGraphContentRootKind::Common;
    }
    return StoryGraphContentRootKind::DependencyId;
}

QString buildSelectionFingerprint(
    const QString& canonicalActiveResourcePackId,
    const QList<StoryGraphContentRoot>& roots)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("story-resource-selection-v1");
    const auto addField =
        [&hash](const QByteArray& value)
        {
            quint64 length =
                static_cast<quint64>(value.size());
            char encodedLength[8];
            for (int index = 7; index >= 0; --index)
            {
                encodedLength[index] =
                    static_cast<char>(length & 0xff);
                length >>= 8;
            }
            hash.addData(encodedLength, 8);
            hash.addData(value);
        };
    addField(canonicalActiveResourcePackId.toUtf8());
    for (const StoryGraphContentRoot& root : roots)
    {
        addField(root.portableRootKey.toUtf8());
        addField(
            QByteArray::number(
                static_cast<int>(root.kind)));
        addField(QByteArray::number(root.ordinal));
    }
    return QStringLiteral("story-selection-v1:") +
        QString::fromLatin1(hash.result().toHex());
}

QString readerStatusMessage(
    RootedResourceReader::Status status)
{
    switch (status)
    {
    case RootedResourceReader::Status::Success:
        return QStringLiteral(
            "story_graph.resource.success");
    case RootedResourceReader::Status::InvalidRoot:
        return QStringLiteral(
            "story_graph.resource.invalid_root");
    case RootedResourceReader::Status::UnsafeRelativePath:
        return QStringLiteral(
            "story_graph.resource.unsafe_path");
    case RootedResourceReader::Status::EscapesRoot:
        return QStringLiteral(
            "story_graph.resource.escapes_root");
    case RootedResourceReader::Status::NotFound:
        return QStringLiteral(
            "story_graph.resource.not_found");
    case RootedResourceReader::Status::NotRegularFile:
        return QStringLiteral(
            "story_graph.resource.not_regular_file");
    case RootedResourceReader::Status::TooLarge:
        return QStringLiteral(
            "story_graph.resource.file_too_large");
    case RootedResourceReader::Status::ReadFailed:
        return QStringLiteral(
            "story_graph.resource.read_failed");
    }
    return QStringLiteral(
        "story_graph.resource.read_failed");
}

QString mapBaseName(const QString& mapTarget)
{
    const qsizetype slash =
        mapTarget.lastIndexOf(QLatin1Char('/'));
    QString baseName = mapTarget.mid(slash + 1);
    const qsizetype dot =
        baseName.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        baseName.truncate(dot);
    return baseName;
}

bool parseMapFolderMapping(
    const std::vector<std::uint8_t>& bytes,
    const QString& mapBase,
    QString& mappedFolder)
{
    if (bytes.empty())
        return false;

    std::string content(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size());
    if (!ScriptConverter::detectAndConvertEncoding(content))
        return false;

    INIFileEditor ini;
    if (!ini.loadFromString(content))
        return false;
    const QByteArray key = mapBase.toUtf8();
    const std::string value =
        ini.get(
            "Init",
            std::string(
                key.constData(),
                static_cast<std::size_t>(key.size())),
            {});
    if (value.empty())
        return false;

    mappedFolder = QString::fromUtf8(
        value.data(),
        static_cast<int>(value.size()));
    return !mappedFolder.isEmpty() &&
        !mappedFolder.contains(QChar::ReplacementCharacter);
}
}

struct StoryGraphResourceContext::Data
{
    struct ContentRootPath
    {
        RuntimeResource::ContentRootKind kind =
            RuntimeResource::ContentRootKind::Active;
        fs::path root;
        std::string resourcePackId;
    };

    std::vector<ContentRootPath> contentRoots;
    QList<StoryGraphContentRoot> graphRoots;
    QString collectionRoot;
    QString activeRoot;
    QString activeResourcePackId;
    QString fingerprint;
    qsizetype maximumSingleFileBytes = 0;
};

StoryGraphResourceContext::StoryGraphResourceContext() = default;

StoryGraphResourceContext::StoryGraphResourceContext(
    std::shared_ptr<const Data> data)
    : data(std::move(data))
{
}

StoryGraphResourceContext
StoryGraphResourceContext::resolve(
    const QString& assetsCollectionRoot,
    const QString& activeResourcePackId,
    qsizetype maximumSingleFileBytes,
    QString* diagnosticCode,
    QString* message,
    const QString& activeResourcePackEntryKey)
{
    if (diagnosticCode)
        diagnosticCode->clear();
    if (message)
        message->clear();

    if (maximumSingleFileBytes < 0 ||
        maximumSingleFileBytes ==
            (std::numeric_limits<qsizetype>::max)())
    {
        if (diagnosticCode)
        {
            *diagnosticCode =
                QStringLiteral(
                    "story_graph.resource.invalid_budget");
        }
        if (message)
        {
            *message =
                QObject::tr(
                    "剧情图资源单文件字节上限无效");
        }
        return {};
    }

    const bool selectByStableEntryKey =
        !activeResourcePackEntryKey.trimmed().isEmpty();
    const QByteArray requestedSelection =
        (selectByStableEntryKey
             ? activeResourcePackEntryKey.trimmed()
             : activeResourcePackId.trimmed()).toUtf8();
    const std::string_view requestedSelectionView(
        requestedSelection.constData(),
        static_cast<std::size_t>(
            requestedSelection.size()));
    const RuntimeResource::ExactSelectionResult resolved =
        selectByStableEntryKey
        ? RuntimeResource::resolveResourceCatalogEntrySelection(
              hostPath(assetsCollectionRoot),
              requestedSelectionView)
        : RuntimeResource::resolveExactResourceSelection(
              hostPath(assetsCollectionRoot),
              requestedSelectionView);
    if (!resolved.succeeded())
    {
        if (diagnosticCode)
            *diagnosticCode =
                utf8Text(resolved.diagnosticCode);
        if (message)
            *message = utf8Text(resolved.message);
        return {};
    }

    auto context = std::make_shared<Data>();
    context->collectionRoot =
        hostPathText(
            resolved.selection.assetsCollectionRoot);
    context->activeRoot =
        hostPathText(
            resolved.selection.activeResourceRoot);
    context->activeResourcePackId =
        utf8Text(
            resolved.selection.
                canonicalActiveResourcePackId);
    context->maximumSingleFileBytes =
        maximumSingleFileBytes;

    context->graphRoots.reserve(
        static_cast<qsizetype>(
            resolved.selection.
                orderedContentRoots.size()));
    context->contentRoots.reserve(
        resolved.selection.
            orderedContentRoots.size());
    int ordinal = 0;
    for (const RuntimeResource::ContentRoot& root :
         resolved.selection.orderedContentRoots)
    {
        context->contentRoots.push_back({
            root.kind,
            root.root,
            root.resourcePackId
        });

        StoryGraphContentRoot graphRoot;
        graphRoot.kind =
            graphRootKind(root.kind);
        graphRoot.ordinal = ordinal;
        QString rootIdentity =
            utf8Text(root.resourcePackId);
        if (root.kind ==
            RuntimeResource::ContentRootKind::Active)
        {
            rootIdentity =
                context->activeResourcePackId;
        }
        graphRoot.portableRootKey =
            makeStoryGraphPortableRootKey(
                graphRoot.kind,
                ordinal,
                rootIdentity);
        context->graphRoots.append(
            std::move(graphRoot));
        ++ordinal;
    }
    context->fingerprint =
        buildSelectionFingerprint(
            context->activeResourcePackId,
            context->graphRoots);
    return StoryGraphResourceContext(
        std::move(context));
}

bool StoryGraphResourceContext::isValid() const
{
    return data != nullptr &&
        !data->graphRoots.isEmpty() &&
        data->contentRoots.size() ==
            static_cast<std::size_t>(
                data->graphRoots.size());
}

void StoryGraphResourceContext::clear()
{
    data.reset();
}

QString StoryGraphResourceContext::assetsCollectionRoot() const
{
    return data ? data->collectionRoot : QString();
}

QString StoryGraphResourceContext::activeContentRoot() const
{
    return data ? data->activeRoot : QString();
}

QString
StoryGraphResourceContext::canonicalActiveResourcePackId() const
{
    return data ? data->activeResourcePackId : QString();
}

QString StoryGraphResourceContext::selectionFingerprint() const
{
    return data ? data->fingerprint : QString();
}

QList<StoryGraphContentRoot>
StoryGraphResourceContext::orderedContentRoots() const
{
    return data
        ? data->graphRoots
        : QList<StoryGraphContentRoot>();
}

StoryGraphReadResult
StoryGraphResourceContext::probeRegularFile(
    const StoryGraphContentRoot& root,
    const QString& strictVirtualPath) const
{
    StoryGraphReadResult result;
    if (!isValid())
    {
        result.status = StoryGraphReadStatus::Error;
        result.message =
            QStringLiteral(
                "story_graph.resource.context_unavailable");
        return result;
    }

    QString rejectionReason;
    if (!StoryGraphProjectResolver::
            isStrictRelativeVirtualPath(
                strictVirtualPath,
                &rejectionReason))
    {
        result.status =
            StoryGraphReadStatus::Rejected;
        result.message =
            QStringLiteral(
                "story_graph.resource.unsafe_path: ") +
            rejectionReason;
        return result;
    }

    if (root.ordinal < 0 ||
        root.ordinal >= data->graphRoots.size() ||
        data->graphRoots[root.ordinal].
                portableRootKey !=
            root.portableRootKey ||
        data->graphRoots[root.ordinal].kind !=
            root.kind)
    {
        result.status =
            StoryGraphReadStatus::Rejected;
        result.message =
            QStringLiteral(
                "story_graph.resource.root_identity_mismatch");
        return result;
    }

    const Data::ContentRootPath& nativeRoot =
        data->contentRoots[
            static_cast<std::size_t>(root.ordinal)];
    const QByteArray path = strictVirtualPath.toUtf8();
    const RootedResourceReader::ProbeResult probe =
        RootedResourceReader::probeRegularFileFromRoot(
            nativeRoot.root,
            std::string_view(
                path.constData(),
                static_cast<std::size_t>(path.size())));

    result.canonicalAbsolutePath =
        hostPathText(
            nativeRoot.root /
            fs::u8path(
                path.constData(),
                path.constData() + path.size()));
    result.message =
        readerStatusMessage(probe.status);
    switch (probe.status)
    {
    case RootedResourceReader::Status::Success:
        result.status = StoryGraphReadStatus::Found;
        break;
    case RootedResourceReader::Status::NotFound:
        result.status = StoryGraphReadStatus::Missing;
        break;
    case RootedResourceReader::Status::UnsafeRelativePath:
    case RootedResourceReader::Status::EscapesRoot:
    case RootedResourceReader::Status::NotRegularFile:
        result.status = StoryGraphReadStatus::Rejected;
        break;
    case RootedResourceReader::Status::InvalidRoot:
    case RootedResourceReader::Status::TooLarge:
    case RootedResourceReader::Status::ReadFailed:
        result.status = StoryGraphReadStatus::Error;
        break;
    }
    return result;
}

StoryGraphReadResult StoryGraphResourceContext::read(
    const StoryGraphContentRoot& root,
    const QString& strictVirtualPath) const
{
    StoryGraphReadResult result;
    if (!isValid())
    {
        result.status = StoryGraphReadStatus::Error;
        result.message =
            QStringLiteral(
                "story_graph.resource.context_unavailable");
        return result;
    }

    QString rejectionReason;
    if (!StoryGraphProjectResolver::
            isStrictRelativeVirtualPath(
                strictVirtualPath,
                &rejectionReason))
    {
        result.status =
            StoryGraphReadStatus::Rejected;
        result.message =
            QStringLiteral(
                "story_graph.resource.unsafe_path: ") +
            rejectionReason;
        return result;
    }

    if (root.ordinal < 0 ||
        root.ordinal >= data->graphRoots.size() ||
        data->graphRoots[root.ordinal].
                portableRootKey !=
            root.portableRootKey ||
        data->graphRoots[root.ordinal].kind !=
            root.kind)
    {
        result.status =
            StoryGraphReadStatus::Rejected;
        result.message =
            QStringLiteral(
                "story_graph.resource.root_identity_mismatch");
        return result;
    }

    const Data::ContentRootPath& nativeRoot =
        data->contentRoots[
            static_cast<std::size_t>(root.ordinal)];
    const QByteArray path = strictVirtualPath.toUtf8();
    const std::size_t readLimit =
        static_cast<std::size_t>(
            data->maximumSingleFileBytes);
    const RootedResourceReader::Result read =
        RootedResourceReader::readBoundedFileFromRoot(
            nativeRoot.root,
            std::string_view(
                path.constData(),
                static_cast<std::size_t>(path.size())),
            readLimit);

    result.canonicalAbsolutePath =
        hostPathText(
            nativeRoot.root /
            fs::u8path(
                path.constData(),
                path.constData() + path.size()));
    result.message =
        readerStatusMessage(read.status);
    switch (read.status)
    {
    case RootedResourceReader::Status::Success:
        result.status = StoryGraphReadStatus::Found;
        result.utf8Bytes = QByteArray(
            reinterpret_cast<const char*>(
                read.bytes.data()),
            static_cast<qsizetype>(
                read.bytes.size()));
        break;
    case RootedResourceReader::Status::NotFound:
        result.status = StoryGraphReadStatus::Missing;
        break;
    case RootedResourceReader::Status::UnsafeRelativePath:
    case RootedResourceReader::Status::EscapesRoot:
    case RootedResourceReader::Status::NotRegularFile:
        result.status = StoryGraphReadStatus::Rejected;
        break;
    case RootedResourceReader::Status::TooLarge:
        // Preserve the budget distinction without retaining or analyzing the
        // oversized payload. This is a synthetic size marker, not file
        // content. The resolver observes a Found byte count just beyond its
        // configured single-file budget, publishes BudgetExceeded, and drops
        // the marker before caching.
        result.status = StoryGraphReadStatus::Found;
        result.utf8Bytes =
            QByteArray(
                data->maximumSingleFileBytes + 1,
                '\0');
        break;
    case RootedResourceReader::Status::InvalidRoot:
    case RootedResourceReader::Status::ReadFailed:
        result.status = StoryGraphReadStatus::Error;
        break;
    }
    return result;
}

StoryGraphMapFolderResolution
StoryGraphResourceContext::resolveMapFolder(
    const QString& strictMapTarget) const
{
    StoryGraphMapFolderResolution result;
    if (!isValid())
    {
        result.status =
            StoryGraphMapFolderResolutionStatus::Error;
        result.message =
            QStringLiteral(
                "story_graph.resource.context_unavailable");
        return result;
    }

    QString rejectionReason;
    if (!StoryGraphProjectResolver::
            isStrictRelativeVirtualPath(
                strictMapTarget,
                &rejectionReason))
    {
        result.status =
            StoryGraphMapFolderResolutionStatus::Rejected;
        result.message = rejectionReason;
        return result;
    }

    const QString mapVirtualPath =
        QStringLiteral("map/") + strictMapTarget;
    const QByteArray mapPath =
        mapVirtualPath.toUtf8();
    bool mapFound = false;
    RootedResourceReader::Status firstMapFailure =
        RootedResourceReader::Status::NotFound;
    for (const Data::ContentRootPath& root :
         data->contentRoots)
    {
        const RootedResourceReader::ProbeResult probe =
            RootedResourceReader::
                probeRegularFileFromRoot(
                    root.root,
                    std::string_view(
                        mapPath.constData(),
                        static_cast<std::size_t>(
                            mapPath.size())));
        if (!probe.succeeded())
        {
            if (probe.status !=
                    RootedResourceReader::Status::NotFound &&
                firstMapFailure ==
                    RootedResourceReader::Status::NotFound)
            {
                firstMapFailure = probe.status;
            }
            continue;
        }
        mapFound = true;
        break;
    }
    if (!mapFound)
    {
        if (firstMapFailure ==
            RootedResourceReader::Status::NotFound)
        {
            result.status =
                StoryGraphMapFolderResolutionStatus::Missing;
            result.message =
                QStringLiteral(
                    "story_graph.resource.map_not_found");
        }
        else
        {
            result.status =
                firstMapFailure ==
                        RootedResourceReader::Status::
                            UnsafeRelativePath ||
                    firstMapFailure ==
                        RootedResourceReader::Status::
                            EscapesRoot ||
                    firstMapFailure ==
                        RootedResourceReader::Status::
                            NotRegularFile
                ? StoryGraphMapFolderResolutionStatus::
                      Rejected
                : StoryGraphMapFolderResolutionStatus::
                      Error;
            result.message =
                readerStatusMessage(firstMapFailure);
        }
        return result;
    }

    QString folder = mapBaseName(strictMapTarget);
    const QByteArray mapNamePath(
        "ini/map/mapname.ini");
    for (const Data::ContentRootPath& root :
         data->contentRoots)
    {
        const RootedResourceReader::Result read =
            RootedResourceReader::
                readBoundedFileFromRoot(
                    root.root,
                    std::string_view(
                        mapNamePath.constData(),
                        static_cast<std::size_t>(
                            mapNamePath.size())),
                    MaximumMapNameIniBytes);
        if (!read.succeeded())
            continue;
        QString mappedFolder;
        if (parseMapFolderMapping(
                read.bytes,
                folder,
                mappedFolder))
        {
            folder = mappedFolder;
        }
        // Runtime selects the first whole mapname.ini file. Parse failure does
        // not merge keys from a later root; it keeps the basename fallback.
        break;
    }

    if (folder.isEmpty())
    {
        result.status =
            StoryGraphMapFolderResolutionStatus::Error;
        result.message =
            QStringLiteral(
                "story_graph.resource.map_folder_empty");
        return result;
    }

    result.status =
        StoryGraphMapFolderResolutionStatus::Resolved;
    result.effectiveMapFolder = folder;
    return result;
}
