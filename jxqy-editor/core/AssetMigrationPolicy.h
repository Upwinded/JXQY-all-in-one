#pragma once

#include <QList>
#include <QString>

#include <array>
#include <optional>

enum class LegacyImageCategory
{
    Character = 0,
    Effect,
    Object,
    Ui,
    Goods,
    Magic,
    Portrait,
    Interlude,
    Map,
    Unknown,
    Count
};

enum class LegacyImageMode
{
    Convert,
    Preserve,
    Exclude
};

struct LegacyImageCategoryDefinition
{
    LegacyImageCategory category;
    QString id;
    QString displayNameZhCn;
    QString displayNameEn;
    LegacyImageMode defaultMode;
    bool allowsConversion;
    bool allowsTransparentCrop;
    bool entersOutput;
};

class LegacyImageMigrationPolicy
{
public:
    LegacyImageMigrationPolicy();

    static const QList<LegacyImageCategoryDefinition>& definitions();
    static const LegacyImageCategoryDefinition& definition(
        LegacyImageCategory category);
    static std::optional<LegacyImageCategory> categoryFromId(
        const QString& id);
    static std::optional<LegacyImageCategory> classifyRelativePath(
        const QString& relativePath);
    static QString modeId(LegacyImageMode mode);
    static std::optional<LegacyImageMode> modeFromId(const QString& id);

    LegacyImageMode mode(LegacyImageCategory category) const;
    bool setMode(LegacyImageCategory category, LegacyImageMode mode);
    void setAllConvertible(LegacyImageMode mode);

    bool cropTransparent() const;
    void setCropTransparent(bool enabled);
    bool hasTransparentCropEligibleConversion() const;
    bool shouldCrop(LegacyImageCategory category) const;
    bool effectiveCropTransparent() const;

    bool operator==(const LegacyImageMigrationPolicy& other) const;
    bool operator!=(const LegacyImageMigrationPolicy& other) const;

private:
    static constexpr std::size_t categoryCount =
        static_cast<std::size_t>(LegacyImageCategory::Count);
    std::array<LegacyImageMode, categoryCount> modes;
    bool cropTransparentEnabled = true;
};
