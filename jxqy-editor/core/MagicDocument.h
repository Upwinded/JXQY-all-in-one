#pragma once

#include "INIFileEditor.h"

#include <QByteArray>
#include <QString>
#include <QVector>

enum class MagicTextField
{
    Name,
    Introduction,
    Image,
    Icon,
    FlyingImage,
    FlyingSound,
    VanishImage,
    VanishSound,
    SuperModeImage,
    SuperModeSound,
    ActionFile,
    UseActionFile,
    AttackFile
};

enum class MagicIntegerField
{
    MoveKind,
    Region,
    Speed,
    WaitFrame,
    LifeFrame,
    Effect,
    LifeCost,
    ThewCost,
    ManaCost,
    LevelUpExperience,
    AttackRadius,
    SpecialKind,
    AlphaBlend,
    FlyingLum,
    VanishLum
};

class MagicDocument
{
public:
    static constexpr int MaximumLevel = 10;

    bool load(const QByteArray& bytes, QString* errorMessage = nullptr);
    bool openFile(const QString& filePath, QString* errorMessage = nullptr);
    bool saveFile(const QString& filePath, QString* errorMessage = nullptr) const;

    QByteArray serializedBytes() const;
    QString text(MagicTextField field) const;
    void setText(MagicTextField field, const QString& value);

    int effectiveInteger(MagicIntegerField field, int level = 0) const;
    bool hasIntegerOverride(MagicIntegerField field, int level) const;
    void setInteger(MagicIntegerField field, int level, int value);
    void removeIntegerOverride(MagicIntegerField field, int level);

    QVector<int> authoredLevels() const;
    int hiddenFieldCount() const;

private:
    static const char* textKey(MagicTextField field);
    static const char* integerKey(MagicIntegerField field);
    static std::string sectionName(int level);

    INIFileEditor ini;
    bool loaded = false;
};
