#include "MagicDocument.h"

#include "AuthoringMutationGate.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <set>

namespace
{
const std::set<std::string>& visibleInitKeys()
{
    static const std::set<std::string> keys = {
        "name", "intro", "image", "icon", "flyingimage",
        "flyingsound", "vanishimage", "vanishsound", "movekind",
        "region", "speed", "waitframe", "lifeframe", "effect",
        "lifecost", "thewcost", "manacost", "levelupexp",
        "attackradius", "specialkind", "alphablend", "flyinglum",
        "vanishlum", "supermodeimage", "supermodesound", "actionfile",
        "useactionfile", "attackfile"};
    return keys;
}

const std::set<std::string>& visibleLevelKeys()
{
    static const std::set<std::string> keys = {
        "movekind", "region", "effect", "lifecost", "thewcost",
        "manacost", "levelupexp", "attackradius", "speed", "waitframe",
        "lifeframe", "specialkind", "alphablend", "flyinglum",
        "vanishlum"};
    return keys;
}
}

bool MagicDocument::load(const QByteArray& bytes, QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (bytes.isEmpty() || bytes.contains('\0') ||
        !ini.loadFromBuffer(bytes.constData(), bytes.size()) ||
        !ini.hasSection("Init"))
    {
        loaded = false;
        if (errorMessage)
            *errorMessage = QStringLiteral("武功文件必须是包含 [Init] 的可读 INI 文本。");
        return false;
    }
    loaded = true;
    return true;
}

bool MagicDocument::openFile(const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    return load(file.readAll(), errorMessage);
}

bool MagicDocument::saveFile(const QString& filePath, QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    if (!loaded)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("尚未打开武功文件。");
        return false;
    }

    auto mutationLease = AuthoringMutationGate::instance().
        acquireMutationLeaseForPath(filePath);
    if (!mutationLease)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("资源正在更新或进行其他写入。");
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    const QByteArray bytes = serializedBytes();
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        file.cancelWriting();
        return false;
    }
    return true;
}

QByteArray MagicDocument::serializedBytes() const
{
    if (!loaded)
        return {};
    const std::string content = ini.saveToString();
    return QByteArray(content.data(), static_cast<qsizetype>(content.size()));
}

QString MagicDocument::text(MagicTextField field) const
{
    return QString::fromUtf8(ini.get("Init", textKey(field), ""));
}

void MagicDocument::setText(MagicTextField field, const QString& value)
{
    ini.set("Init", textKey(field), value.toUtf8().toStdString());
}

int MagicDocument::effectiveInteger(MagicIntegerField field, int level) const
{
    level = std::clamp(level, 0, MaximumLevel);
    std::int64_t value = 0;
    for (int currentLevel = 0; currentLevel <= level; ++currentLevel)
    {
        const std::string section = sectionName(currentLevel);
        std::int64_t candidate = 0;
        if (ini.tryGetInt64(section, integerKey(field), candidate))
            value = candidate;
    }
    return static_cast<int>(std::clamp<std::int64_t>(
        value, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}

bool MagicDocument::hasIntegerOverride(MagicIntegerField field, int level) const
{
    return ini.hasKey(sectionName(std::clamp(level, 0, MaximumLevel)),
                      integerKey(field));
}

void MagicDocument::setInteger(MagicIntegerField field, int level, int value)
{
    ini.setInteger(sectionName(std::clamp(level, 0, MaximumLevel)),
                   integerKey(field), value);
}

void MagicDocument::removeIntegerOverride(MagicIntegerField field, int level)
{
    ini.removeKey(sectionName(std::clamp(level, 0, MaximumLevel)),
                  integerKey(field));
}

QVector<int> MagicDocument::authoredLevels() const
{
    QVector<int> levels;
    for (int level = 1; level <= MaximumLevel; ++level)
    {
        if (ini.hasSection(sectionName(level)))
            levels.append(level);
    }
    return levels;
}

int MagicDocument::hiddenFieldCount() const
{
    int count = 0;
    for (const auto& sectionPair : ini.getIniMap().sections)
    {
        const std::string& section = sectionPair.first;
        const std::set<std::string>* visible = nullptr;
        if (section == "init")
        {
            visible = &visibleInitKeys();
        }
        else if (section.rfind("level", 0) == 0)
        {
            visible = &visibleLevelKeys();
        }
        if (!visible)
        {
            count += static_cast<int>(sectionPair.second.keys.size());
            continue;
        }
        for (const auto& keyPair : sectionPair.second.keys)
        {
            if (visible->find(keyPair.first) == visible->end())
                ++count;
        }
    }
    return count;
}

const char* MagicDocument::textKey(MagicTextField field)
{
    switch (field)
    {
    case MagicTextField::Name: return "Name";
    case MagicTextField::Introduction: return "Intro";
    case MagicTextField::Image: return "Image";
    case MagicTextField::Icon: return "Icon";
    case MagicTextField::FlyingImage: return "FlyingImage";
    case MagicTextField::FlyingSound: return "FlyingSound";
    case MagicTextField::VanishImage: return "VanishImage";
    case MagicTextField::VanishSound: return "VanishSound";
    case MagicTextField::SuperModeImage: return "SuperModeImage";
    case MagicTextField::SuperModeSound: return "SuperModeSound";
    case MagicTextField::ActionFile: return "ActionFile";
    case MagicTextField::UseActionFile: return "UseActionFile";
    case MagicTextField::AttackFile: return "AttackFile";
    }
    return "";
}

const char* MagicDocument::integerKey(MagicIntegerField field)
{
    switch (field)
    {
    case MagicIntegerField::MoveKind: return "MoveKind";
    case MagicIntegerField::Region: return "Region";
    case MagicIntegerField::Speed: return "Speed";
    case MagicIntegerField::WaitFrame: return "WaitFrame";
    case MagicIntegerField::LifeFrame: return "LifeFrame";
    case MagicIntegerField::Effect: return "Effect";
    case MagicIntegerField::LifeCost: return "LifeCost";
    case MagicIntegerField::ThewCost: return "ThewCost";
    case MagicIntegerField::ManaCost: return "ManaCost";
    case MagicIntegerField::LevelUpExperience: return "LevelupExp";
    case MagicIntegerField::AttackRadius: return "AttackRadius";
    case MagicIntegerField::SpecialKind: return "SpecialKind";
    case MagicIntegerField::AlphaBlend: return "AlphaBlend";
    case MagicIntegerField::FlyingLum: return "FlyingLum";
    case MagicIntegerField::VanishLum: return "VanishLum";
    }
    return "";
}

std::string MagicDocument::sectionName(int level)
{
    return level <= 0 ? "Init" : "Level" + std::to_string(level);
}
