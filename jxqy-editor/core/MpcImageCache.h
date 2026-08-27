#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <QImage>
#include "PicFileEditor.h"

struct MpcCacheEntry
{
    std::string fileName;
    std::string resolvedLogicalPath;
    std::string physicalTargetHint;
    std::int64_t fileSize = -1;
    std::int64_t lastModifiedMilliseconds = -1;
    std::int64_t metadataChangedMilliseconds = -1;
    std::unique_ptr<PicFileEditor> editor;
    bool loaded = false;
};

class MpcImageCache
{
public:
    MpcImageCache();
    ~MpcImageCache();

    void setAssetsBasePath(const std::string& path);
    const std::string& getAssetsBasePath() const;

    QImage getFrameImage(const std::string& mpcFileName, int frameIndex);
    QImage getFrameImageByPath(const std::string& fullPath, int frameIndex);

    int getFrameCount(const std::string& mpcFileName);
    int getDirection(const std::string& mpcFileName);
    int getInterval(const std::string& mpcFileName);
    bool getFrameOffset(const std::string& mpcFileName, int frameIndex,
                        int& offsetX, int& offsetY);

    bool isLoaded(const std::string& mpcFileName) const;
    void preload(const std::string& mpcFileName);

    void clearCache();
    void removeFromCache(const std::string& mpcFileName);

    void setMaxCacheSize(int maxCount);
    int getMaxCacheSize() const;
    int getCurrentCacheSize() const;

    // WARNING: The returned pointer is owned by the cache and may be invalidated
    // when other files are loaded (LRU eviction). Callers must NOT store this
    // pointer long-term. Use getFrameImage() or getFrameImageByPath() instead
    // for safe one-shot access. If you need persistent access, copy the data
    // immediately after calling this function.
    PicFileEditor* getPicFileEditor(const std::string& mpcFileName);

private:
    PicFileEditor* loadMpcFile(const std::string& mpcFileName);
    PicFileEditor* loadMpcFileAtPath(
        const std::string& cachePath,
        const std::string& displayName,
        const std::string& resolvedPath);
    std::string resolveFilePath(const std::string& relativePath) const;
    void evictIfNeeded();

    std::map<std::string, MpcCacheEntry> cache;
    std::vector<std::string> accessOrder;
    std::string assetsBasePath;
    int maxCacheCount = 64;
};
