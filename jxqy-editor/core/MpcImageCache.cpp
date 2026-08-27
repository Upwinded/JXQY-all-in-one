#include "MpcImageCache.h"
#include "EditorAssetPath.h"
#include "Util.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileDevice>
#include <QFileInfo>

#include <algorithm>
#include <filesystem>

namespace
{
std::string normalizePathText(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty())
    {
        return path;
    }
    return std::filesystem::u8path(path).lexically_normal().generic_u8string();
}

std::string cacheKeyForPath(const std::string& path)
{
    std::string key = normalizePathText(path);
#if defined(_WIN32)
    for (char& character : key)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
#endif
    return key;
}

QString fromUtf8Path(const std::string& path)
{
    return QString::fromUtf8(
        path.data(), static_cast<qsizetype>(path.size()));
}

std::string normalizedResourceCachePath(
    const std::string& resourcePath)
{
    QString normalizedResourcePath;
    if (!EditorAssetPath::normalizeResourcePath(
            fromUtf8Path(resourcePath),
            normalizedResourcePath))
    {
        return resourcePath;
    }
    return normalizedResourcePath.toUtf8().toStdString();
}

struct MpcSourceState
{
    std::string resolvedLogicalPath;
    std::string physicalTargetHint;
    std::int64_t fileSize = -1;
    std::int64_t lastModifiedMilliseconds = -1;
    std::int64_t metadataChangedMilliseconds = -1;
    bool valid = false;
};

MpcSourceState inspectMpcSource(const std::string& resolvedPath)
{
    MpcSourceState state;
    const QString logicalPath =
        EditorAssetPath::normalizedAbsolutePath(
            fromUtf8Path(resolvedPath));
    QFileInfo fileInfo(logicalPath);
    fileInfo.refresh();
    if (!fileInfo.exists() || !fileInfo.isFile())
        return state;

    state.resolvedLogicalPath =
        logicalPath.toUtf8().toStdString();
    QString physicalTarget = fileInfo.canonicalFilePath();
    if (physicalTarget.isEmpty())
        physicalTarget = logicalPath;
    // The current physical target is only a cache invalidation hint. It never
    // authorizes or rejects a formal-resource read, and it is not the cache key.
    state.physicalTargetHint =
        EditorAssetPath::logicalComparisonKey(
            physicalTarget).toUtf8().toStdString();
    state.fileSize = fileInfo.size();
    state.lastModifiedMilliseconds =
        fileInfo.lastModified().toMSecsSinceEpoch();
    state.metadataChangedMilliseconds =
        fileInfo.fileTime(
            QFileDevice::FileMetadataChangeTime)
            .toMSecsSinceEpoch();
    state.valid = true;
    return state;
}

bool entryMatchesSource(const MpcCacheEntry& entry,
                        const MpcSourceState& state)
{
    return state.valid && entry.loaded &&
        entry.resolvedLogicalPath == state.resolvedLogicalPath &&
        entry.physicalTargetHint == state.physicalTargetHint &&
        entry.fileSize == state.fileSize &&
        entry.lastModifiedMilliseconds ==
            state.lastModifiedMilliseconds &&
        entry.metadataChangedMilliseconds ==
            state.metadataChangedMilliseconds;
}

void setEntrySourceState(MpcCacheEntry& entry,
                         const MpcSourceState& state)
{
    entry.resolvedLogicalPath = state.resolvedLogicalPath;
    entry.physicalTargetHint = state.physicalTargetHint;
    entry.fileSize = state.fileSize;
    entry.lastModifiedMilliseconds =
        state.lastModifiedMilliseconds;
    entry.metadataChangedMilliseconds =
        state.metadataChangedMilliseconds;
}
}

MpcImageCache::MpcImageCache()
{
}

MpcImageCache::~MpcImageCache()
{
    clearCache();
}

void MpcImageCache::setAssetsBasePath(const std::string& path)
{
    std::string normalizedPath;
    if (!path.empty())
    {
        normalizedPath = normalizePathText(
            EditorAssetPath::normalizedAbsolutePath(
                fromUtf8Path(path)).toUtf8().toStdString());
    }
    if (!normalizedPath.empty() && normalizedPath.back() != '/')
        normalizedPath += "/";

    if (assetsBasePath != normalizedPath)
    {
        clearCache();
        assetsBasePath = normalizedPath;
    }
}

const std::string& MpcImageCache::getAssetsBasePath() const
{
    return assetsBasePath;
}

QImage MpcImageCache::getFrameImage(const std::string& mpcFileName, int frameIndex)
{
    PicFileEditor* editor = loadMpcFile(mpcFileName);
    if (!editor)
        return QImage();

    if (frameIndex < 0 || frameIndex >= editor->getFrameCount())
        return QImage();

    return editor->getFrameImage(frameIndex);
}

QImage MpcImageCache::getFrameImageByPath(const std::string& fullPath, int frameIndex)
{
    PicFileEditor* editor =
        loadMpcFileAtPath(fullPath, fullPath, fullPath);
    if (!editor)
        return QImage();

    if (frameIndex < 0 || frameIndex >= editor->getFrameCount())
        return QImage();

    return editor->getFrameImage(frameIndex);
}

int MpcImageCache::getFrameCount(const std::string& mpcFileName)
{
    PicFileEditor* editor = loadMpcFile(mpcFileName);
    if (!editor)
        return 0;
    return editor->getFrameCount();
}

int MpcImageCache::getDirection(const std::string& mpcFileName)
{
    PicFileEditor* editor = loadMpcFile(mpcFileName);
    if (!editor)
        return 0;
    return editor->getDirection();
}

int MpcImageCache::getInterval(const std::string& mpcFileName)
{
    PicFileEditor* editor = loadMpcFile(mpcFileName);
    if (!editor)
        return 0;
    return editor->getInterval();
}

bool MpcImageCache::getFrameOffset(const std::string& mpcFileName, int frameIndex,
                                   int& offsetX, int& offsetY)
{
    PicFileEditor* editor = loadMpcFile(mpcFileName);
    if (!editor || frameIndex < 0 || frameIndex >= editor->getFrameCount())
        return false;
    int32_t frameOffsetX = 0;
    int32_t frameOffsetY = 0;
    editor->getFrameOffset(frameIndex, &frameOffsetX, &frameOffsetY);
    offsetX = static_cast<int>(frameOffsetX);
    offsetY = static_cast<int>(frameOffsetY);
    return true;
}

bool MpcImageCache::isLoaded(const std::string& mpcFileName) const
{
    const std::string cachePath = assetsBasePath.empty()
        ? mpcFileName
        : normalizedResourceCachePath(mpcFileName);
    std::string key = cacheKeyForPath(cachePath);
    auto it = cache.find(key);
    return it != cache.end() &&
        entryMatchesSource(
            it->second,
            inspectMpcSource(
                it->second.resolvedLogicalPath));
}

void MpcImageCache::preload(const std::string& mpcFileName)
{
    loadMpcFile(mpcFileName);
}

void MpcImageCache::clearCache()
{
    cache.clear();
    accessOrder.clear();
}

void MpcImageCache::removeFromCache(const std::string& mpcFileName)
{
    const std::string cachePath = assetsBasePath.empty()
        ? mpcFileName
        : normalizedResourceCachePath(mpcFileName);
    std::string key = cacheKeyForPath(cachePath);
    cache.erase(key);
    auto it = std::find(accessOrder.begin(), accessOrder.end(), key);
    if (it != accessOrder.end())
        accessOrder.erase(it);
}

void MpcImageCache::setMaxCacheSize(int maxCount)
{
    maxCacheCount = std::max(1, maxCount);
    evictIfNeeded();
}

int MpcImageCache::getMaxCacheSize() const
{
    return maxCacheCount;
}

int MpcImageCache::getCurrentCacheSize() const
{
    return static_cast<int>(cache.size());
}

PicFileEditor* MpcImageCache::getPicFileEditor(const std::string& mpcFileName)
{
    return loadMpcFile(mpcFileName);
}

PicFileEditor* MpcImageCache::loadMpcFile(const std::string& mpcFileName)
{
    const std::string cachePath = assetsBasePath.empty()
        ? mpcFileName
        : normalizedResourceCachePath(mpcFileName);

    std::string fullPath = resolveFilePath(mpcFileName);
    if (fullPath.empty())
    {
        removeFromCache(cachePath);
        return nullptr;
    }

    return loadMpcFileAtPath(
        cachePath, mpcFileName, fullPath);
}

PicFileEditor* MpcImageCache::loadMpcFileAtPath(
    const std::string& cachePath,
    const std::string& displayName,
    const std::string& resolvedPath)
{
    const std::string key = cacheKeyForPath(cachePath);
    const MpcSourceState sourceState =
        inspectMpcSource(resolvedPath);
    auto it = cache.find(key);
    if (it != cache.end() &&
        entryMatchesSource(it->second, sourceState))
    {
        auto orderIt =
            std::find(accessOrder.begin(), accessOrder.end(), key);
        if (orderIt != accessOrder.end())
        {
            accessOrder.erase(orderIt);
            accessOrder.push_back(key);
        }
        return it->second.editor.get();
    }

    if (it != cache.end())
    {
        cache.erase(it);
        auto orderIt =
            std::find(accessOrder.begin(), accessOrder.end(), key);
        if (orderIt != accessOrder.end())
            accessOrder.erase(orderIt);
    }
    if (!sourceState.valid)
        return nullptr;

    MpcCacheEntry entry;
    entry.fileName = displayName;
    setEntrySourceState(entry, sourceState);
    entry.editor = std::make_unique<PicFileEditor>();
    entry.loaded = entry.editor->loadFromFile(resolvedPath);
    if (!entry.loaded)
        return nullptr;

    cache[key] = std::move(entry);
    accessOrder.push_back(key);
    evictIfNeeded();

    auto newIt = cache.find(key);
    return newIt != cache.end() ? newIt->second.editor.get() : nullptr;
}

std::string MpcImageCache::resolveFilePath(const std::string& relativePath) const
{
    if (relativePath.empty())
        return "";

    const std::string normalizedRelativePath = normalizePathText(relativePath);
    const std::filesystem::path requestedPath = std::filesystem::u8path(normalizedRelativePath);
    if (!assetsBasePath.empty())
    {
        QString absolutePath;
        if (!EditorAssetPath::resolveLogicalResourcePath(
                fromUtf8Path(assetsBasePath),
                fromUtf8Path(relativePath),
                absolutePath))
        {
            return "";
        }

        const QFileInfo fileInfo(absolutePath);
        if (!fileInfo.exists() || !fileInfo.isFile())
            return "";
        return absolutePath.toUtf8().toStdString();
    }

    // A cache without an assets root retains the historical relative-path
    // behavior, but still reject absolute/drive-qualified paths and parent
    // traversal. Callers needing an explicit absolute file must use the
    // separate getFrameImageByPath API.
    if (requestedPath.is_absolute() || requestedPath.has_root_name() ||
        normalizedRelativePath.find(':') != std::string::npos)
    {
        return "";
    }
    const std::filesystem::path normalizedPath = requestedPath.lexically_normal();
    for (const std::filesystem::path& component : normalizedPath)
    {
        if (component == "..")
            return "";
    }
    const std::string safeRelativePath = normalizedPath.generic_u8string();
    if (Util::fileExistsUtf8(safeRelativePath))
        return safeRelativePath;

    return "";
}

void MpcImageCache::evictIfNeeded()
{
    while (static_cast<int>(cache.size()) > maxCacheCount && !accessOrder.empty())
    {
        std::string oldestKey = accessOrder.front();
        accessOrder.erase(accessOrder.begin());
        cache.erase(oldestKey);
    }
}
