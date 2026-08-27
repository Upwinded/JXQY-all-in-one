#pragma once

#include "ImageFrameImport.h"

#include <QString>

#include <memory>
#include <vector>

class DurableFileRecoveredReadLock;

enum class ImageFrameImportRecipeError
{
    None,
    InvalidRecipePath,
    TransactionRecoveryFailed,
    ReadFailed,
    InvalidUtf8,
    InvalidJson,
    UnsupportedFormat,
    UnsupportedVersion,
    InvalidFrames,
    InvalidFramePath,
    InvalidOffset,
    FrameFileInvalid,
    TransactionFailed
};

struct ImageFrameImportRecipeFailure
{
    ImageFrameImportRecipeError error = ImageFrameImportRecipeError::None;
    int frameIndex = -1;
    ImageFrameImportError importError = ImageFrameImportError::None;
    QString detail;
    QString warning;
};

class ImageFrameImportRecipe
{
public:
    static QString formatIdentifier();
    static int currentVersion();

    static bool load(
        const QString& recipeFilePath,
        std::vector<ImageFrameImportRequest>* requests,
        ImageFrameImportRecipeFailure* failure = nullptr,
        std::shared_ptr<DurableFileRecoveredReadLock>* coherentRead = nullptr);

    static bool save(
        const QString& recipeFilePath,
        const std::vector<ImageFrameImportRequest>& requests,
        ImageFrameImportRecipeFailure* failure = nullptr);
};
