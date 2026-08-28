#include "TestSupport.h"

#include "../core/GoodsShopDocument.h"
#include "../core/TranslationManager.h"
#include "../ui/GoodsShopEditorWindow.h"
#include "../ui/MpcPreviewLabel.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGroupBox>
#include <QImage>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUndoStack>

namespace
{
using namespace TestSupport;

QByteArray goodsFixture()
{
    return QByteArray::fromHex("efbbbf") +
        QStringLiteral(
            "[Init]\r\n"
            "Name=测试药品\r\n"
            "Kind=0\r\n"
            "Cost=140\r\n"
            "Intro=恢复内力\r\n"
            "Effect=气+160\r\n"
            "Image=drug.mpc\r\n"
            "Icon=drugs.mpc\r\n"
            "Life=0\r\n"
            "Mana=160\r\n"
            "Thew=0\r\n"
            "UnknownRule=keep-me\r\n")
            .toUtf8();
}

QByteArray shopFixture()
{
    return QByteArray::fromHex("efbbbf") +
        QStringLiteral(
            "// 保留商店说明\r\n"
            "[Head]\r\n"
            "Count=2\r\n"
            "UnknownHeader=keep-header\r\n"
            "\r\n"
            "[1]\r\n"
            "IniFile=first.ini\r\n"
            "Number=2\r\n"
            "RowTag=first-row\r\n"
            "\r\n"
            "[2]\r\n"
            "IniFile=second.ini\r\n"
            "Number=4\r\n"
            "RowTag=second-row\r\n"
            "\r\n"
            "[Extra]\r\n"
            "Future=preserve\r\n")
            .toUtf8();
}

bool testGoodsDocument()
{
    GoodsDocument document;
    QString error;
    bool ok = check(document.load(goodsFixture(), &error),
                    "load UTF-8 BOM goods fixture");
    ok = check(document.text(GoodsTextField::Name) ==
                   QStringLiteral("测试药品") &&
                   document.integer(GoodsIntegerField::Kind) == 0 &&
                   document.integer(GoodsIntegerField::Cost) == 140 &&
                   document.integer(GoodsIntegerField::Mana) == 160,
               "read author-facing goods fields") && ok;
    ok = check(document.hiddenFieldCount() == 1,
               "count hidden goods fields") && ok;

    document.setText(GoodsTextField::Name, QStringLiteral("测试药品·改"));
    document.setInteger(GoodsIntegerField::Cost, 175);
    const QByteArray bytes = document.serializedBytes();
    ok = check(bytes.startsWith(QByteArray::fromHex("efbbbf")) &&
                   bytes.count("[Init]") == 1 &&
                   bytes.contains(QStringLiteral("Name=测试药品·改").toUtf8()) &&
                   bytes.contains("Cost=175") &&
                   bytes.contains("UnknownRule=keep-me"),
               "edit BOM goods without duplicating its first section") && ok;

    QTemporaryDir directory;
    const QString path = QDir(directory.path()).filePath("ini/goods/test.ini");
    ok = check(directory.isValid() && QDir().mkpath(QFileInfo(path).path()) &&
                   document.saveFile(path, &error),
               "atomically save goods fixture") && ok;
    GoodsDocument reopened;
    ok = check(reopened.openFile(path, &error) &&
                   reopened.text(GoodsTextField::Name) ==
                       QStringLiteral("测试药品·改") &&
                   reopened.integer(GoodsIntegerField::Cost) == 175 &&
                   reopened.serializedBytes().contains("UnknownRule=keep-me"),
               "reopen saved goods and retain hidden content") && ok;
    return ok;
}

bool testShopDocument()
{
    ShopDocument document;
    QString error;
    bool ok = check(document.load(shopFixture(), &error),
                    "load UTF-8 BOM legacy Head shop fixture");
    ok = check(document.itemCount() == 2 &&
                   document.item(0).iniFile == QStringLiteral("first.ini") &&
                   document.item(1).number == 4 &&
                   !document.stockLimited() &&
                   document.buyPercent() == 100 &&
                   document.recyclePercent() == 100,
               "read shop order, quantities and runtime defaults") && ok;
    ok = check(document.hiddenFieldCount() == 4,
               "count hidden header, row and extra-section shop fields") && ok;

    document.setStockLimited(true);
    document.setBuyPercent(125);
    document.setRecyclePercent(60);
    ok = check(document.moveItem(1, 0) &&
                   document.setItemNumber(0, 7) &&
                   document.addItem(QStringLiteral("third.ini"), 3) &&
                   document.removeItem(1),
               "adjust shop order, stock and contents") && ok;
    const QByteArray bytes = document.serializedBytes();
    ok = check(bytes.startsWith(QByteArray::fromHex("efbbbf")) &&
                   bytes.contains("[Head]") &&
                   !bytes.contains("[Header]") &&
                   bytes.contains("Count=2") &&
                   bytes.contains("NumberValid=1") &&
                   bytes.contains("BuyPercent=125") &&
                   bytes.contains("RecyclePercent=60") &&
                   bytes.contains("IniFile=second.ini") &&
                   bytes.contains("Number=7") &&
                   bytes.contains("RowTag=second-row") &&
                   bytes.contains("IniFile=third.ini") &&
                   bytes.contains("UnknownHeader=keep-header") &&
                   bytes.contains("[Extra]\r\nFuture=preserve"),
               "preserve legacy spelling and hidden content while editing shop") && ok;

    QTemporaryDir directory;
    const QString path = QDir(directory.path()).filePath("ini/buy/test.ini");
    ok = check(directory.isValid() && QDir().mkpath(QFileInfo(path).path()) &&
                   document.saveFile(path, &error),
               "atomically save shop fixture") && ok;
    ShopDocument reopened;
    ok = check(reopened.openFile(path, &error) && reopened.itemCount() == 2 &&
                   reopened.item(0).iniFile == QStringLiteral("second.ini") &&
                   reopened.item(1).iniFile == QStringLiteral("third.ini") &&
                   reopened.stockLimited() && reopened.buyPercent() == 125 &&
                   reopened.recyclePercent() == 60,
               "reopen saved shop with edited order and pricing") && ok;
    return ok;
}

bool testGoodsShopEditorMinimumLoop()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               "create goods/shop editor temporary directory"))
    {
        return false;
    }

    const QString assetsRoot = QDir(directory.path()).filePath("assets");
    const QString goodsDirectory = QDir(assetsRoot).filePath("ini/goods");
    const QString shopDirectory = QDir(assetsRoot).filePath("ini/buy");
    const QString imageDirectory = QDir(assetsRoot).filePath("mpc/goods");
    if (!check(QDir().mkpath(goodsDirectory) &&
                   QDir().mkpath(shopDirectory) &&
                   QDir().mkpath(imageDirectory),
               "create goods/shop editor resource directories"))
    {
        return false;
    }

    const QString firstGoodsPath = QDir(goodsDirectory).filePath("first.ini");
    const QString secondGoodsPath = QDir(goodsDirectory).filePath("second.ini");
    const QString shopPath = QDir(shopDirectory).filePath("shop.ini");
    QImage image(48, 32, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(70, 150, 220));
    QImage icon(24, 24, QImage::Format_ARGB32_Premultiplied);
    icon.fill(QColor(220, 130, 60));
    const QByteArray secondGoods = QStringLiteral(
        "[Init]\r\n"
        "Name=第二件物品\r\n"
        "Kind=1\r\n"
        "Cost=300\r\n"
        "Intro=用于加入商店\r\n"
        "Effect=攻击+5\r\n"
        "Image=drug.mpc\r\n"
        "Icon=drugs.mpc\r\n"
        "Part=Hand\r\n"
        "Attack=5\r\n").toUtf8();
    const QByteArray shop = QStringLiteral(
        "[Header]\r\n"
        "Count=1\r\n"
        "FutureHeader=keep\r\n"
        "\r\n"
        "[1]\r\n"
        "IniFile=first.ini\r\n"
        "Number=2\r\n").toUtf8();
    if (!check(writeRawFile(firstGoodsPath, goodsFixture()) &&
                   writeRawFile(secondGoodsPath, secondGoods) &&
                   writeRawFile(shopPath, shop) &&
                   image.save(QDir(imageDirectory).filePath("drug.mpc"), "PNG") &&
                   icon.save(QDir(imageDirectory).filePath("drugs.mpc"), "PNG"),
               "write goods/shop editor fixtures"))
    {
        return false;
    }

    GoodsShopEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(1240, 860);
    window.setAssetsBasePath(assetsRoot);
    window.show();
    QApplication::processEvents();

    bool ok = check(window.openFile(firstGoodsPath),
                    "open goods through focused editor");
    QApplication::processEvents();
    auto* goodsList = window.findChild<QListWidget*>("goodsDocumentList");
    auto* shopList = window.findChild<QListWidget*>("shopDocumentList");
    auto* search = window.findChild<QLineEdit*>("goodsSearchEdit");
    auto* name = window.findChild<QLineEdit*>("goodsNameEdit");
    auto* intro = window.findChild<QPlainTextEdit*>("goodsIntroductionEdit");
    auto* kind = window.findChild<QComboBox*>("goodsKindCombo");
    auto* cost = window.findChild<QSpinBox*>("goodsCostSpin");
    auto* imagePreview = window.findChild<MpcPreviewLabel*>("goodsImagePreview");
    auto* iconPreview = window.findChild<MpcPreviewLabel*>("goodsIconPreview");
    auto* addToShop =
        window.findChild<QPushButton*>("shopAddSelectedGoodsButton");
    auto* undo = window.findChild<QUndoStack*>("goodsShopUndoStack");
    auto* playtest = window.findChild<QAction*>("goodsShopPlaytestAction");
    ok = check(goodsList && goodsList->count() == 2 &&
                   shopList && shopList->count() == 1 && search && name &&
                   intro && kind && cost && undo && playtest &&
                   imagePreview && iconPreview && addToShop,
               "expose shared goods/shop library and focused goods fields") && ok;
    window.resize(900, 650);
    QApplication::processEvents();
    QLayout* goodsCatalogLayout = goodsList ? goodsList->parentWidget()->layout()
                                            : nullptr;
    ok = check(addToShop && goodsCatalogLayout &&
                   goodsCatalogLayout->indexOf(addToShop) >= 0 &&
                   goodsCatalogLayout->indexOf(addToShop) <
                       goodsCatalogLayout->indexOf(goodsList),
               "keep add-to-shop action visible in a compact editing window") && ok;
    window.resize(1240, 860);
    QApplication::processEvents();
    ok = check(name && name->text() == QStringLiteral("测试药品") &&
                   kind && kind->currentData().toInt() == 0 &&
                   cost && cost->value() == 140 &&
                   imagePreview && !imagePreview->currentPixmapSize().isEmpty() &&
                   iconPreview && !iconPreview->currentPixmapSize().isEmpty(),
               "show goods type, price and real image previews") && ok;

    search->setText(QStringLiteral("第二件"));
    QApplication::processEvents();
    int visibleGoods = 0;
    for (int index = 0; index < goodsList->count(); ++index)
        visibleGoods += goodsList->item(index)->isHidden() ? 0 : 1;
    ok = check(visibleGoods == 1,
               "search goods by author-facing name") && ok;
    search->clear();

    bool playtestRequested = false;
    QObject::connect(&window, &GoodsShopEditorWindow::playtestRequested,
                     [&playtestRequested]() { playtestRequested = true; });
    name->setText(QStringLiteral("测试药品·未保存"));
    intro->setPlainText(QStringLiteral("仍在输入框中的说明"));
    playtest->trigger();
    QApplication::processEvents();
    DesktopRunDocumentSnapshot snapshot = window.desktopRunDocumentSnapshot();
    ok = check(playtestRequested && window.hasUnsavedChanges() &&
                   snapshot.type == ProjectDocumentType::Goods &&
                   snapshot.dirty && snapshot.serializationSupported &&
                   snapshot.bytes.contains(
                       QStringLiteral("Name=测试药品·未保存").toUtf8()) &&
                   snapshot.bytes.contains(
                       QStringLiteral("Intro=仍在输入框中的说明").toUtf8()) &&
                   snapshot.bytes.contains("UnknownRule=keep-me"),
               "flush focused goods fields into exact playtest snapshot") && ok;

    QString duplicatedPath;
    QObject::connect(&window,
                     &GoodsShopEditorWindow::openGoodsShopFileRequested,
                     [&duplicatedPath](const QString& path)
                     {
                         duplicatedPath = path;
                     });
    const QString copyPath = QDir(goodsDirectory).filePath("copy.ini");
    QString duplicateError;
    ok = check(window.duplicateCurrentGoodsTo(copyPath, &duplicateError) &&
                   QFileInfo::exists(copyPath) && duplicatedPath == copyPath,
               "copy the current in-memory goods into a new project file") && ok;
    GoodsDocument copied;
    ok = check(copied.openFile(copyPath) &&
                   copied.text(GoodsTextField::Name) ==
                       QStringLiteral("测试药品·未保存") &&
                   copied.serializedBytes().contains("UnknownRule=keep-me"),
               "copied goods includes pending edits and hidden content") && ok;
    ok = check(!window.duplicateCurrentGoodsTo(copyPath, &duplicateError),
               "never overwrite an existing goods file while copying") && ok;

    undo->undo();
    QApplication::processEvents();
    ok = check(name->text() == QStringLiteral("测试药品") &&
                   !window.hasUnsavedChanges(),
               "undo pending goods edits and return to clean source") && ok;

    ok = check(window.openFile(shopPath),
               "open shop in the same focused workspace") && ok;
    QApplication::processEvents();
    auto* table = window.findChild<QTableWidget*>("shopItemsTable");
    auto* limited = window.findChild<QCheckBox*>("shopStockLimitedCheck");
    auto* buyPercent = window.findChild<QSpinBox*>("shopBuyPercentSpin");
    auto* add = window.findChild<QPushButton*>("shopAddSelectedGoodsButton");
    auto* moveUp = window.findChild<QPushButton*>("shopMoveUpButton");
    auto* remove = window.findChild<QPushButton*>("shopRemoveButton");
    auto* shopIcon = window.findChild<MpcPreviewLabel*>("shopItemIconPreview");
    ok = check(table && table->rowCount() == 1 && limited &&
                   !limited->isChecked() && buyPercent &&
                   buyPercent->value() == 100 && add && moveUp && remove &&
                   shopIcon && !shopIcon->currentPixmapSize().isEmpty(),
               "show shop order, runtime defaults and selected goods preview") && ok;

    for (int index = 0; index < goodsList->count(); ++index)
    {
        if (goodsList->item(index)->data(Qt::UserRole).toString() ==
            secondGoodsPath)
        {
            goodsList->setCurrentRow(index);
            break;
        }
    }
    add->click();
    QApplication::processEvents();
    ok = check(table->rowCount() == 2 &&
                   table->item(1, 1)->text() == QStringLiteral("第二件物品"),
               "add a searched goods entry to the current shop") && ok;
    limited->click();
    QApplication::processEvents();
    table->item(1, 3)->setText(QStringLiteral("5"));
    QApplication::processEvents();
    table->selectRow(1);
    moveUp->click();
    QApplication::processEvents();
    snapshot = window.desktopRunDocumentSnapshot();
    ok = check(snapshot.type == ProjectDocumentType::Shop && snapshot.dirty &&
                   snapshot.bytes.contains("NumberValid=1") &&
                   snapshot.bytes.contains("IniFile=second.ini") &&
                   snapshot.bytes.contains("Number=5") &&
                   snapshot.bytes.indexOf("IniFile=second.ini") <
                       snapshot.bytes.indexOf("IniFile=first.ini") &&
                   snapshot.bytes.contains("FutureHeader=keep"),
               "edit finite stock and shop order without losing hidden fields") && ok;
    remove->click();
    QApplication::processEvents();
    ok = check(table->rowCount() == 1,
               "remove the selected goods from the shop") && ok;
    undo->undo();
    QApplication::processEvents();
    ok = check(table->rowCount() == 2,
               "undo a shop contents change") && ok;

    ok = check(TranslationManager::instance().setLanguage("en_US"),
               "switch goods/shop editor to English") && ok;
    QApplication::processEvents();
    auto* priceGroup = window.findChild<QGroupBox*>("goodsPriceGroup");
    ok = check(playtest->text() == QStringLiteral("Playtest Current Content") &&
                   table->horizontalHeaderItem(3)->text() ==
                       QStringLiteral("Stock") && priceGroup,
               "retranslate the open goods/shop workspace to English") && ok;
    ok = check(TranslationManager::instance().setLanguage("zh_TW"),
               "switch goods/shop editor to Traditional Chinese") && ok;
    QApplication::processEvents();
    ok = check(playtest->text() == QString::fromUtf8("試玩目前內容") &&
                   table->horizontalHeaderItem(3)->text() ==
                       QString::fromUtf8("庫存"),
               "retranslate the open goods/shop workspace to Traditional Chinese") && ok;
    ok = check(TranslationManager::instance().setLanguage("zh_CN"),
               "restore source language after goods/shop checks") && ok;
    QApplication::processEvents();

    ok = check(window.saveFile() && !window.hasUnsavedChanges(),
               "save the edited shop") && ok;
    ShopDocument reopenedShop;
    ok = check(reopenedShop.openFile(shopPath) &&
                   reopenedShop.itemCount() == 2 &&
                   reopenedShop.stockLimited() &&
                   reopenedShop.serializedBytes().contains("FutureHeader=keep"),
               "reopen saved shop with stock and hidden content") && ok;
    const QList<ProjectDocumentState> states = window.currentProjectDocuments();
    ok = check(states.size() == 1 &&
                   states.front().type == ProjectDocumentType::Shop &&
                   !states.front().dirty,
               "publish one clean shop document to shared registry") && ok;
    ok = check(GoodsShopEditorWindow::isGoodsFilePath(firstGoodsPath) &&
                   GoodsShopEditorWindow::isShopFilePath(shopPath) &&
                   !GoodsShopEditorWindow::isSupportedFilePath(
                       QDir(assetsRoot).filePath("ini/magic/test.ini")),
               "route only goods and buy INI files to focused workspace") && ok;
    window.hide();
    return ok;
}
}

int main(int argc, char* argv[])
{
#if !defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        qEnvironmentVariableIsEmpty("DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
#endif
    QApplication application(argc, argv);
    QTemporaryDir isolatedWorkingDirectory;
    QTemporaryDir isolatedSettingsDirectory;
    if (!isolatedWorkingDirectory.isValid() ||
        !isolatedSettingsDirectory.isValid() ||
        !QDir::setCurrent(isolatedWorkingDirectory.path()))
    {
        return 1;
    }
    application.setProperty(
        "configFilePath",
        isolatedSettingsDirectory.filePath("editor_config.ini"));
    TranslationManager::instance().initialize(application);

    bool ok = true;
    ok = testGoodsDocument() && ok;
    ok = testShopDocument() && ok;
    ok = testGoodsShopEditorMinimumLoop() && ok;
    return ok ? 0 : 1;
}
