#include "AssetMigrationPolicy.h"

#include <QStringList>

namespace
{
std::size_t categoryIndex(LegacyImageCategory category)
{
    return static_cast<std::size_t>(category);
}
}

const QList<LegacyImageCategoryDefinition>&
LegacyImageMigrationPolicy::definitions()
{
    static const QList<LegacyImageCategoryDefinition> categoryDefinitions = {
        {LegacyImageCategory::Character, QStringLiteral("character"),
            QString::fromUtf8("角色"), QStringLiteral("Character"),
            LegacyImageMode::Convert, true, true, true},
        {LegacyImageCategory::Effect, QStringLiteral("effect"),
            QString::fromUtf8("武功效果"), QStringLiteral("Effect"),
            LegacyImageMode::Convert, true, true, true},
        {LegacyImageCategory::Object, QStringLiteral("object"),
            QString::fromUtf8("场景物件"), QStringLiteral("Object"),
            LegacyImageMode::Convert, true, true, true},
        {LegacyImageCategory::Ui, QStringLiteral("ui"),
            QString::fromUtf8("界面"), QStringLiteral("UI"),
            LegacyImageMode::Preserve, true, false, true},
        {LegacyImageCategory::Goods, QStringLiteral("goods"),
            QString::fromUtf8("物品"), QStringLiteral("Goods"),
            LegacyImageMode::Preserve, true, false, true},
        {LegacyImageCategory::Magic, QStringLiteral("magic"),
            QString::fromUtf8("武功图标"), QStringLiteral("Magic"),
            LegacyImageMode::Preserve, true, false, true},
        {LegacyImageCategory::Portrait, QStringLiteral("portrait"),
            QString::fromUtf8("头像"), QStringLiteral("Portrait"),
            LegacyImageMode::Preserve, true, false, true},
        {LegacyImageCategory::Interlude, QStringLiteral("interlude"),
            QString::fromUtf8("过场"), QStringLiteral("Interlude"),
            LegacyImageMode::Preserve, true, false, true},
        {LegacyImageCategory::Map, QStringLiteral("map"),
            QString::fromUtf8("地图图片"), QStringLiteral("Map images"),
            LegacyImageMode::Preserve, false, false, true},
        {LegacyImageCategory::Unknown, QStringLiteral("unknown"),
            QString::fromUtf8("未知类别"), QStringLiteral("Unknown"),
            LegacyImageMode::Preserve, false, false, true}
    };
    return categoryDefinitions;
}

const LegacyImageCategoryDefinition& LegacyImageMigrationPolicy::definition(
    LegacyImageCategory category)
{
    for (const LegacyImageCategoryDefinition& item : definitions())
    {
        if (item.category == category)
            return item;
    }
    return definitions().constLast();
}

std::optional<LegacyImageCategory>
LegacyImageMigrationPolicy::categoryFromId(const QString& id)
{
    const QString normalized = id.trimmed();
    for (const LegacyImageCategoryDefinition& item : definitions())
    {
        if (normalized.compare(item.id, Qt::CaseInsensitive) == 0)
            return item.category;
    }
    return std::nullopt;
}

std::optional<LegacyImageCategory>
LegacyImageMigrationPolicy::classifyRelativePath(const QString& relativePath)
{
    QString normalized = relativePath;
    normalized.replace('\\', '/');
    while (normalized.startsWith(QStringLiteral("./")))
        normalized.remove(0, 2);

    const QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty() ||
        (parts[0].compare(QStringLiteral("mpc"), Qt::CaseInsensitive) != 0 &&
         parts[0].compare(QStringLiteral("asf"), Qt::CaseInsensitive) != 0))
    {
        return std::nullopt;
    }

    // A category is a directory immediately below mpc/asf. A file directly
    // below either root has no category directory and is therefore unknown.
    if (parts.size() < 3)
        return LegacyImageCategory::Unknown;

    const QString firstDirectory = parts[1];
    for (const LegacyImageCategoryDefinition& item : definitions())
    {
        if (item.category != LegacyImageCategory::Unknown &&
            firstDirectory.compare(item.id, Qt::CaseInsensitive) == 0)
        {
            return item.category;
        }
    }
    return LegacyImageCategory::Unknown;
}

QString LegacyImageMigrationPolicy::modeId(LegacyImageMode mode)
{
    switch (mode)
    {
    case LegacyImageMode::Convert:
        return QStringLiteral("convert");
    case LegacyImageMode::Preserve:
        return QStringLiteral("preserve");
    case LegacyImageMode::Exclude:
        return QStringLiteral("exclude");
    }
    return QStringLiteral("exclude");
}

std::optional<LegacyImageMode> LegacyImageMigrationPolicy::modeFromId(
    const QString& id)
{
    if (id == QStringLiteral("convert"))
        return LegacyImageMode::Convert;
    if (id == QStringLiteral("preserve"))
        return LegacyImageMode::Preserve;
    if (id == QStringLiteral("exclude"))
        return LegacyImageMode::Exclude;
    return std::nullopt;
}

LegacyImageMigrationPolicy::LegacyImageMigrationPolicy()
{
    modes.fill(LegacyImageMode::Exclude);
    for (const LegacyImageCategoryDefinition& item : definitions())
        modes[categoryIndex(item.category)] = item.defaultMode;
}

LegacyImageMode LegacyImageMigrationPolicy::mode(
    LegacyImageCategory category) const
{
    const std::size_t index = categoryIndex(category);
    if (index >= modes.size())
        return LegacyImageMode::Exclude;
    return modes[index];
}

bool LegacyImageMigrationPolicy::setMode(
    LegacyImageCategory category, LegacyImageMode requestedMode)
{
    if (categoryIndex(category) >= modes.size())
        return false;
    const LegacyImageCategoryDefinition& item = definition(category);
    const bool accepted = item.allowsConversion
        ? requestedMode != LegacyImageMode::Exclude
        : requestedMode == item.defaultMode;
    if (!accepted)
        return false;

    modes[categoryIndex(category)] = requestedMode;
    return true;
}

void LegacyImageMigrationPolicy::setAllConvertible(LegacyImageMode mode)
{
    if (mode == LegacyImageMode::Exclude)
        return;
    for (const LegacyImageCategoryDefinition& item : definitions())
    {
        if (item.allowsConversion)
            setMode(item.category, mode);
    }
}

bool LegacyImageMigrationPolicy::cropTransparent() const
{
    return cropTransparentEnabled;
}

void LegacyImageMigrationPolicy::setCropTransparent(bool enabled)
{
    cropTransparentEnabled = enabled;
}

bool LegacyImageMigrationPolicy::hasTransparentCropEligibleConversion() const
{
    for (const LegacyImageCategoryDefinition& item : definitions())
    {
        if (item.allowsTransparentCrop &&
            mode(item.category) == LegacyImageMode::Convert)
        {
            return true;
        }
    }
    return false;
}

bool LegacyImageMigrationPolicy::shouldCrop(
    LegacyImageCategory category) const
{
    const LegacyImageCategoryDefinition& item = definition(category);
    return cropTransparentEnabled && item.allowsTransparentCrop &&
        mode(category) == LegacyImageMode::Convert;
}

bool LegacyImageMigrationPolicy::effectiveCropTransparent() const
{
    return cropTransparentEnabled &&
        hasTransparentCropEligibleConversion();
}

bool LegacyImageMigrationPolicy::operator==(
    const LegacyImageMigrationPolicy& other) const
{
    return modes == other.modes &&
        cropTransparentEnabled == other.cropTransparentEnabled;
}

bool LegacyImageMigrationPolicy::operator!=(
    const LegacyImageMigrationPolicy& other) const
{
    return !(*this == other);
}
