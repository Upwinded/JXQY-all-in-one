#pragma once

#include "INIFileEditor.h"

#include <QByteArray>
#include <QString>
#include <QVector>

enum class GoodsTextField
{
    Name,
    Introduction,
    Effect,
    Image,
    Icon,
    EquipmentPart
};

enum class GoodsIntegerField
{
    Kind,
    Cost,
    SellPrice,
    Life,
    Thew,
    Mana,
    LifeMaximum,
    ThewMaximum,
    ManaMaximum,
    Attack,
    Defend,
    Evade
};

class GoodsDocument
{
public:
    bool load(const QByteArray& bytes, QString* errorMessage = nullptr);
    bool openFile(const QString& filePath, QString* errorMessage = nullptr);
    bool saveFile(const QString& filePath, QString* errorMessage = nullptr) const;

    QByteArray serializedBytes() const;
    QString text(GoodsTextField field) const;
    void setText(GoodsTextField field, const QString& value);
    int integer(GoodsIntegerField field) const;
    void setInteger(GoodsIntegerField field, int value);
    bool hasFixedInteger(GoodsIntegerField field) const;
    int hiddenFieldCount() const;

private:
    static const char* textKey(GoodsTextField field);
    static const char* integerKey(GoodsIntegerField field);
    static int defaultInteger(GoodsIntegerField field);

    INIFileEditor ini;
    bool loaded = false;
};

struct ShopDocumentItem
{
    QString iniFile;
    int number = 0;
};

class ShopDocument
{
public:
    static constexpr int MaximumItems = 81;

    bool load(const QByteArray& bytes, QString* errorMessage = nullptr);
    bool openFile(const QString& filePath, QString* errorMessage = nullptr);
    bool saveFile(const QString& filePath, QString* errorMessage = nullptr) const;

    QByteArray serializedBytes() const;
    int itemCount() const;
    ShopDocumentItem item(int index) const;
    bool stockLimited() const;
    int buyPercent() const;
    int recyclePercent() const;

    void setStockLimited(bool limited);
    void setBuyPercent(int percent);
    void setRecyclePercent(int percent);
    bool addItem(const QString& iniFile, int number = 1);
    bool removeItem(int index);
    bool moveItem(int from, int to);
    bool setItemFile(int index, const QString& iniFile);
    bool setItemNumber(int index, int number);
    int hiddenFieldCount() const;

private:
    QVector<IniSection> itemSections() const;
    void writeItemSections(const QVector<IniSection>& sections);
    const char* headerSectionName() const;

    INIFileEditor ini;
    bool loaded = false;
    bool usesLegacyHead = false;
};
