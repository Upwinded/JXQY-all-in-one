#include "GoodsShopDocument.h"

#include "AuthoringMutationGate.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <set>

namespace
{
const std::set<std::string>& visibleGoodsKeys()
{
    static const std::set<std::string> keys = {
        "name", "kind", "cost", "sellprice", "intro", "effect",
        "image", "icon", "part", "life", "thew", "mana",
        "lifemax", "thewmax", "manamax", "attack", "defend",
        "evade"};
    return keys;
}

const std::set<std::string>& visibleShopHeaderKeys()
{
    static const std::set<std::string> keys = {
        "count", "numbervalid", "buypercent", "recyclepercent"};
    return keys;
}

const std::set<std::string>& visibleShopItemKeys()
{
    static const std::set<std::string> keys = {"inifile", "number"};
    return keys;
}

bool writeAtomically(const QString& filePath, const QByteArray& bytes,
                     QString* errorMessage)
{
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
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        file.cancelWriting();
        return false;
    }
    return true;
}

bool readFileBytes(const QString& filePath, QByteArray& bytes,
                   QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = file.errorString();
        return false;
    }
    bytes = file.readAll();
    return true;
}

int clampedInteger(std::int64_t value)
{
    return static_cast<int>(std::clamp<std::int64_t>(
        value, std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}
}

bool GoodsDocument::load(const QByteArray& bytes, QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (bytes.isEmpty() || bytes.contains('\0') ||
        !ini.loadFromBuffer(bytes.constData(), bytes.size()) ||
        !ini.hasSection("Init"))
    {
        loaded = false;
        if (errorMessage)
            *errorMessage = QStringLiteral("物品文件必须是包含 [Init] 的可读 INI 文本。");
        return false;
    }
    loaded = true;
    return true;
}

bool GoodsDocument::openFile(const QString& filePath, QString* errorMessage)
{
    QByteArray bytes;
    return readFileBytes(filePath, bytes, errorMessage) &&
        load(bytes, errorMessage);
}

bool GoodsDocument::saveFile(const QString& filePath, QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    if (!loaded)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("尚未打开物品文件。");
        return false;
    }
    return writeAtomically(filePath, serializedBytes(), errorMessage);
}

QByteArray GoodsDocument::serializedBytes() const
{
    if (!loaded)
        return {};
    const std::string content = ini.saveToString();
    return QByteArray(content.data(), static_cast<qsizetype>(content.size()));
}

QString GoodsDocument::text(GoodsTextField field) const
{
    return QString::fromUtf8(ini.get("Init", textKey(field), ""));
}

void GoodsDocument::setText(GoodsTextField field, const QString& value)
{
    ini.set("Init", textKey(field), value.toUtf8().toStdString());
}

int GoodsDocument::integer(GoodsIntegerField field) const
{
    std::int64_t value = 0;
    return ini.tryGetInt64("Init", integerKey(field), value)
        ? clampedInteger(value) : defaultInteger(field);
}

void GoodsDocument::setInteger(GoodsIntegerField field, int value)
{
    ini.setInteger("Init", integerKey(field), value);
}

bool GoodsDocument::hasFixedInteger(GoodsIntegerField field) const
{
    const std::string value = ini.get("Init", integerKey(field), "");
    if (value.empty())
        return true;
    std::int64_t parsed = 0;
    return INIFileEditor::tryParseInt64(value, parsed);
}

int GoodsDocument::hiddenFieldCount() const
{
    int count = 0;
    for (const auto& sectionPair : ini.getIniMap().sections)
    {
        if (sectionPair.first != "init")
        {
            count += static_cast<int>(sectionPair.second.keys.size());
            continue;
        }
        for (const auto& keyPair : sectionPair.second.keys)
        {
            if (visibleGoodsKeys().find(keyPair.first) == visibleGoodsKeys().end())
                ++count;
        }
    }
    return count;
}

const char* GoodsDocument::textKey(GoodsTextField field)
{
    switch (field)
    {
    case GoodsTextField::Name: return "Name";
    case GoodsTextField::Introduction: return "Intro";
    case GoodsTextField::Effect: return "Effect";
    case GoodsTextField::Image: return "Image";
    case GoodsTextField::Icon: return "Icon";
    case GoodsTextField::EquipmentPart: return "Part";
    }
    return "";
}

const char* GoodsDocument::integerKey(GoodsIntegerField field)
{
    switch (field)
    {
    case GoodsIntegerField::Kind: return "Kind";
    case GoodsIntegerField::Cost: return "Cost";
    case GoodsIntegerField::SellPrice: return "SellPrice";
    case GoodsIntegerField::Life: return "Life";
    case GoodsIntegerField::Thew: return "Thew";
    case GoodsIntegerField::Mana: return "Mana";
    case GoodsIntegerField::LifeMaximum: return "LifeMax";
    case GoodsIntegerField::ThewMaximum: return "ThewMax";
    case GoodsIntegerField::ManaMaximum: return "ManaMax";
    case GoodsIntegerField::Attack: return "Attack";
    case GoodsIntegerField::Defend: return "Defend";
    case GoodsIntegerField::Evade: return "Evade";
    }
    return "";
}

int GoodsDocument::defaultInteger(GoodsIntegerField field)
{
    return field == GoodsIntegerField::Kind ? 2 : 0;
}

bool ShopDocument::load(const QByteArray& bytes, QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (bytes.isEmpty() || bytes.contains('\0') ||
        !ini.loadFromBuffer(bytes.constData(), bytes.size()) ||
        (!ini.hasSection("Header") && !ini.hasSection("Head")))
    {
        loaded = false;
        if (errorMessage)
            *errorMessage = QStringLiteral("商店货单必须是包含 [Header] 或 [Head] 的可读 INI 文本。");
        return false;
    }

    usesLegacyHead = !ini.hasSection("Header") && ini.hasSection("Head");
    std::int64_t count = 0;
    const std::string rawCount = ini.get(headerSectionName(), "Count", "0");
    if (!INIFileEditor::tryParseInt64(rawCount, count) ||
        count < 0 || count > MaximumItems)
    {
        loaded = false;
        if (errorMessage)
            *errorMessage = QStringLiteral("商店货单的物品数量必须在 0～81 之间。");
        return false;
    }
    loaded = true;
    return true;
}

bool ShopDocument::openFile(const QString& filePath, QString* errorMessage)
{
    QByteArray bytes;
    return readFileBytes(filePath, bytes, errorMessage) &&
        load(bytes, errorMessage);
}

bool ShopDocument::saveFile(const QString& filePath, QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    if (!loaded)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("尚未打开商店货单。");
        return false;
    }
    return writeAtomically(filePath, serializedBytes(), errorMessage);
}

QByteArray ShopDocument::serializedBytes() const
{
    if (!loaded)
        return {};
    const std::string content = ini.saveToString();
    return QByteArray(content.data(), static_cast<qsizetype>(content.size()));
}

int ShopDocument::itemCount() const
{
    if (!loaded)
        return 0;
    return static_cast<int>(ini.getInteger(headerSectionName(), "Count", 0));
}

ShopDocumentItem ShopDocument::item(int index) const
{
    ShopDocumentItem result;
    if (index < 0 || index >= itemCount())
        return result;
    const std::string section = std::to_string(index + 1);
    result.iniFile = QString::fromUtf8(ini.get(section, "IniFile", ""));
    result.number = std::max(0, clampedInteger(
        ini.getInt64(section, "Number", 0)));
    return result;
}

bool ShopDocument::stockLimited() const
{
    return ini.getInteger(headerSectionName(), "NumberValid", 0) != 0;
}

int ShopDocument::buyPercent() const
{
    return std::max(0, clampedInteger(
        ini.getInt64(headerSectionName(), "BuyPercent", 100)));
}

int ShopDocument::recyclePercent() const
{
    return std::max(0, clampedInteger(
        ini.getInt64(headerSectionName(), "RecyclePercent", 100)));
}

void ShopDocument::setStockLimited(bool limited)
{
    if (limited)
        ini.setInteger(headerSectionName(), "NumberValid", 1);
    else
        ini.removeKey(headerSectionName(), "NumberValid");
}

void ShopDocument::setBuyPercent(int percent)
{
    percent = std::max(0, percent);
    if (percent == 100)
        ini.removeKey(headerSectionName(), "BuyPercent");
    else
        ini.setInteger(headerSectionName(), "BuyPercent", percent);
}

void ShopDocument::setRecyclePercent(int percent)
{
    percent = std::max(0, percent);
    if (percent == 100)
        ini.removeKey(headerSectionName(), "RecyclePercent");
    else
        ini.setInteger(headerSectionName(), "RecyclePercent", percent);
}

bool ShopDocument::addItem(const QString& iniFile, int number)
{
    const int count = itemCount();
    if (!loaded || count >= MaximumItems || iniFile.trimmed().isEmpty())
        return false;
    const std::string section = std::to_string(count + 1);
    ini.set(section, "IniFile", iniFile.trimmed().toUtf8().toStdString());
    ini.setInteger(section, "Number", std::max(0, number));
    ini.setInteger(headerSectionName(), "Count", count + 1);
    return true;
}

bool ShopDocument::removeItem(int index)
{
    QVector<IniSection> sections = itemSections();
    if (index < 0 || index >= sections.size())
        return false;
    sections.removeAt(index);
    writeItemSections(sections);
    return true;
}

bool ShopDocument::moveItem(int from, int to)
{
    QVector<IniSection> sections = itemSections();
    if (from < 0 || from >= sections.size() ||
        to < 0 || to >= sections.size() || from == to)
    {
        return false;
    }
    sections.move(from, to);
    writeItemSections(sections);
    return true;
}

bool ShopDocument::setItemFile(int index, const QString& iniFile)
{
    if (index < 0 || index >= itemCount() || iniFile.trimmed().isEmpty())
        return false;
    ini.set(std::to_string(index + 1), "IniFile",
            iniFile.trimmed().toUtf8().toStdString());
    return true;
}

bool ShopDocument::setItemNumber(int index, int number)
{
    if (index < 0 || index >= itemCount())
        return false;
    ini.setInteger(std::to_string(index + 1), "Number",
                   std::max(0, number));
    return true;
}

int ShopDocument::hiddenFieldCount() const
{
    int count = 0;
    const std::string header = usesLegacyHead ? "head" : "header";
    const int visibleCount = itemCount();
    for (const auto& sectionPair : ini.getIniMap().sections)
    {
        const std::set<std::string>* visible = nullptr;
        if (sectionPair.first == header)
        {
            visible = &visibleShopHeaderKeys();
        }
        else
        {
            std::int64_t number = 0;
            if (INIFileEditor::tryParseInt64(sectionPair.first, number) &&
                number >= 1 && number <= visibleCount)
            {
                visible = &visibleShopItemKeys();
            }
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

QVector<IniSection> ShopDocument::itemSections() const
{
    QVector<IniSection> sections;
    const IniMap& map = ini.getIniMap();
    for (int index = 0; index < itemCount(); ++index)
    {
        const std::string name = std::to_string(index + 1);
        const auto found = map.sections.find(name);
        if (found != map.sections.end())
        {
            sections.append(found->second);
        }
        else
        {
            IniSection section;
            section.originalName = name;
            sections.append(section);
        }
    }
    return sections;
}

void ShopDocument::writeItemSections(const QVector<IniSection>& sections)
{
    const int oldCount = itemCount();
    for (int index = 0; index < oldCount; ++index)
        ini.removeSection(std::to_string(index + 1));

    IniMap& map = ini.getIniMapRef();
    for (int index = 0; index < sections.size(); ++index)
    {
        IniSection section = sections[index];
        section.originalName = std::to_string(index + 1);
        map.sections[section.originalName] = std::move(section);
    }
    ini.setInteger(headerSectionName(), "Count", sections.size());
}

const char* ShopDocument::headerSectionName() const
{
    return usesLegacyHead ? "Head" : "Header";
}
