#include "TestSupport.h"

#include "../core/MagicDocument.h"
#include "../core/TranslationManager.h"
#include "../ui/MagicEditorWindow.h"
#include "../ui/MagicRangePreview.h"
#include "../ui/MpcPreviewLabel.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUndoStack>

namespace
{
using namespace TestSupport;

QByteArray magicFixture(const QString& name)
{
    return QStringLiteral(
        "; 保留这条注释\r\n"
        "[Init]\r\n"
        "Name=%1\r\n"
        "Intro=用于编辑器闭环测试\r\n"
        "MoveKind=11\r\n"
        "Region=2\r\n"
        "Icon=icon.png\r\n"
        "FlyingImage=fly.mpc\r\n"
        "VanishImage=hit.mpc\r\n"
        "SuperModeImage=super.png\r\n"
        "SuperModeSound=super.wav\r\n"
        "ActionFile=action.png\r\n"
        "UseActionFile=use-action.png\r\n"
        "AttackFile=second.ini\r\n"
        "Speed=8\r\n"
        "WaitFrame=4\r\n"
        "LifeFrame=40\r\n"
        "SpecialKind=2\r\n"
        "AlphaBlend=1\r\n"
        "FlyingLum=15\r\n"
        "VanishLum=24\r\n"
        "UnknownFlag=keep-me\r\n"
        "\r\n"
        "[Level1]\r\n"
        "Effect=10\r\n"
        "ManaCost=3\r\n"
        "AttackRadius=2\r\n"
        "HiddenLevelValue=level-secret\r\n"
        "\r\n"
        "[Level2]\r\n"
        "ManaCost=4\r\n"
        "MoveKind=10\r\n"
        "Speed=12\r\n"
        "SpecialKind=3\r\n"
        "\r\n"
        "[Extra]\r\n"
        "Foo=Bar\r\n")
        .arg(name)
        .toUtf8();
}

bool testMagicDocumentRoundTrip()
{
    MagicDocument document;
    QString error;
    bool ok = check(document.load(magicFixture(QStringLiteral("测试武功")), &error),
                    "load magic INI fixture");
    ok = check(document.text(MagicTextField::Name) == QStringLiteral("测试武功"),
               "read author-facing magic name") && ok;
    ok = check(document.effectiveInteger(MagicIntegerField::Effect, 2) == 10 &&
                   document.effectiveInteger(MagicIntegerField::ManaCost, 2) == 4 &&
                   document.effectiveInteger(MagicIntegerField::Speed, 2) == 12 &&
                   document.effectiveInteger(MagicIntegerField::VanishLum, 2) == 24,
               "inherit missing level values and keep explicit overrides") && ok;
    ok = check(document.text(MagicTextField::SuperModeImage) ==
                       QStringLiteral("super.png") &&
                   document.text(MagicTextField::ActionFile) ==
                       QStringLiteral("action.png") &&
                   document.text(MagicTextField::AttackFile) ==
                       QStringLiteral("second.ini"),
               "expose full-screen, action and practice attack resources") && ok;
    ok = check(!document.hasIntegerOverride(MagicIntegerField::Effect, 2) &&
                   document.authoredLevels() == QVector<int>({1, 2}),
               "report authored magic levels") && ok;
    ok = check(document.hiddenFieldCount() == 3,
               "count advanced fields hidden from the focused editor") && ok;

    document.setText(MagicTextField::Name, QStringLiteral("修改后的武功"));
    document.setInteger(MagicIntegerField::Effect, 2, 77);
    const QByteArray serialized = document.serializedBytes();
    ok = check(serialized.contains("; 保留这条注释") &&
                   serialized.contains("UnknownFlag=keep-me") &&
                   serialized.contains("HiddenLevelValue=level-secret") &&
                   serialized.contains("[Extra]\r\nFoo=Bar") &&
                   serialized.contains(QStringLiteral("Name=修改后的武功").toUtf8()) &&
                   serialized.contains("Effect=77"),
               "preserve comments and hidden fields while changing visible fields") && ok;

    QTemporaryDir directory;
    const QString path = QDir(directory.path()).filePath("ini/magic/test.ini");
    ok = check(directory.isValid() && QDir().mkpath(QFileInfo(path).path()),
               "create magic document save directory") && ok;
    ok = check(document.saveFile(path, &error),
               "atomically save edited magic document") && ok;
    MagicDocument reopened;
    ok = check(reopened.openFile(path, &error) &&
                   reopened.text(MagicTextField::Name) ==
                       QStringLiteral("修改后的武功") &&
                   reopened.effectiveInteger(MagicIntegerField::Effect, 2) == 77 &&
                   reopened.serializedBytes().contains("UnknownFlag=keep-me"),
               "reopen saved magic document without losing advanced content") && ok;
    return ok;
}

bool testMagicRangePreviewRegionShapes()
{
    auto affected = [](int region, int level, int x, int y)
    {
        return MagicRangePreview::affectsPreviewCell(
            11, region, level, x, y);
    };

    bool ok = check(
        affected(3, 1, 3, 4) && !affected(3, 1, 4, 4) &&
            !affected(3, 1, 2, 1) &&
            affected(3, 4, 5, -4) && !affected(3, 4, 6, 0),
        "wave preview keeps five-tile rows and grows from three to five rows");
    ok = check(
        affected(4, 1, 1, 0) && !affected(4, 1, 1, 1) &&
            affected(4, 1, 3, 4) && !affected(4, 1, 4, 0) &&
            affected(4, 4, 5, 8),
        "triangle preview expands by the selected level range") && ok;
    ok = check(
        affected(5, 1, 1, 0) && affected(5, 1, 2, -2) &&
            !affected(5, 1, 2, -3) &&
            affected(5, 4, 3, 4) && !affected(5, 4, 3, 0),
        "V-type preview grows two arms with the selected level") && ok;
    ok = check(
        affected(1, 1, 3, 2) && affected(1, 1, 4, 0) &&
            affected(1, 1, 2, 0) && affected(1, 1, 3, -2) &&
            !affected(1, 1, 4, 1) &&
            affected(1, 4, 3, 4) && affected(1, 4, 5, 0) &&
            affected(1, 4, 1, 0) && affected(1, 4, 3, -4),
        "square preview grows from three by three to five by five") && ok;
    ok = check(
        affected(2, 1, -2, 3) && !affected(2, 1, -2, 4) &&
            affected(2, 4, 2, 5),
        "cross preview arm length follows the selected level") && ok;
    return ok;
}

bool testMagicRangePreviewMovementShapes()
{
    auto affected = [](int moveKind, int level, int x, int y)
    {
        return MagicRangePreview::affectsPreviewCell(
            moveKind, 0, level, x, y);
    };

    bool ok = check(
        affected(13, 1, 0, 0) && !affected(13, 1, 1, 0) &&
            !affected(13, 1, 0, 1),
        "self magic preview affects only the caster cell");
    ok = check(
        affected(1, 1, 4, 0) && !affected(1, 1, 1, 0) &&
            !affected(1, 1, 2, 0) && !affected(1, 1, 3, 0),
        "point magic previews only its target tile") && ok;
    ok = check(
        affected(2, 1, 1, 0) && affected(2, 1, 4, 0) &&
            !affected(2, 1, 2, 0) && !affected(2, 1, 3, 0),
        "flying magic previews start and target without filling its route") && ok;
    for (const int moveKind : {3, 16, 17})
    {
        ok = check(
            affected(moveKind, 1, 1, 0) &&
                affected(moveKind, 1, 4, 0) &&
                !affected(moveKind, 1, 2, 0),
            "travel variants preview endpoints without filling their routes") && ok;
    }
    ok = check(
        affected(4, 1, 3, 0) && affected(4, 1, -3, 0) &&
            affected(4, 1, 0, 6) && !affected(4, 1, 1, 0),
        "circle magic previews radial destinations without filled spokes") && ok;
    for (const int moveKind : {5, 6})
    {
        ok = check(
            affected(moveKind, 1, 3, 0) &&
                affected(moveKind, 1, -3, 0) &&
                !affected(moveKind, 1, 1, 0),
            "timed circle variants keep the same radial destinations") && ok;
    }
    ok = check(
        affected(7, 1, 4, -2) && affected(7, 1, 4, 0) &&
            affected(7, 1, 4, 2) && !affected(7, 1, 3, 0) &&
            affected(7, 4, 4, -4) && affected(7, 4, 4, 4),
        "sector preview shows level-based projectile destinations") && ok;
    ok = check(
        affected(8, 1, 4, -2) && affected(8, 1, 4, 0) &&
            affected(8, 1, 4, 2) && !affected(8, 1, 3, 0),
        "random sector keeps the same projectile destinations") && ok;
    ok = check(
        affected(10, 1, 0, 2) && affected(10, 1, 4, -2) &&
            !affected(10, 1, 2, 2) && !affected(10, 1, 2, 4),
        "level-one moving line previews its three-unit start and end walls") && ok;
    ok = check(
        affected(10, 3, 0, 6) && affected(10, 3, 4, -6) &&
            !affected(10, 3, 2, 6) && !affected(10, 3, 2, 8),
        "moving-line preview width follows the selected level") && ok;
    ok = check(
        affected(10, 10, 0, 20) && affected(10, 10, 4, -20) &&
            !affected(10, 10, 2, 20) && !affected(10, 10, 2, 22),
        "high-level moving line is not clipped to four cells") && ok;
    ok = check(
        affected(24, 2, 1, -2) && affected(24, 2, 1, 2) &&
            affected(24, 2, 5, -2) && affected(24, 2, 5, 2) &&
            !affected(24, 2, 3, -2),
        "V-move preview shows only the start and later formations") && ok;
    ok = check(
        affected(19, 1, 0, 0) && affected(19, 1, 3, 0) &&
            !affected(19, 1, 4, 0),
        "trail preview marks positions left behind by the moving caster") && ok;
    ok = check(
        affected(20, 1, 4, 0) && !affected(20, 1, 2, 0),
        "transport preview marks the actor destination without a fake path") && ok;
    ok = check(
        affected(21, 1, 4, 0) && !affected(21, 1, 2, 0),
        "control preview marks the selected target without a projectile path") && ok;
    ok = check(
        affected(22, 1, 4, 0) && !affected(22, 1, 1, 0),
        "summon preview marks only the summon position") && ok;
    ok = check(
        affected(23, 1, 0, 0) && !affected(23, 1, 1, 0),
        "time-stop preview starts from the caster rather than a surrounding area") && ok;
    ok = check(
        !affected(0, 1, 0, 0) && !affected(0, 1, 1, 0),
        "undefined move kind does not invent an affected range") && ok;
    return ok;
}

bool testMagicEditorMinimumLoop()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(), "create magic editor temporary directory"))
        return false;

    const QString assetsRoot = QDir(directory.path()).filePath("assets");
    const QString magicDirectory = QDir(assetsRoot).filePath("ini/magic");
    const QString imageDirectory = QDir(assetsRoot).filePath("mpc/magic");
    const QString effectDirectory = QDir(assetsRoot).filePath("mpc/effect");
    const QString characterDirectory =
        QDir(assetsRoot).filePath("mpc/character");
    if (!check(QDir().mkpath(magicDirectory) &&
                   QDir().mkpath(imageDirectory) &&
                   QDir().mkpath(effectDirectory) &&
                   QDir().mkpath(characterDirectory),
               "create magic editor resource directories"))
    {
        return false;
    }

    const QString firstPath = QDir(magicDirectory).filePath("first.ini");
    const QString secondPath = QDir(magicDirectory).filePath("second.ini");
    QImage icon(32, 32, QImage::Format_ARGB32_Premultiplied);
    icon.fill(QColor(60, 150, 230));
    QImage effect(48, 24, QImage::Format_ARGB32_Premultiplied);
    effect.fill(QColor(230, 120, 60));
    QImage effectSecondFrame(48, 24, QImage::Format_ARGB32_Premultiplied);
    effectSecondFrame.fill(QColor(255, 210, 80));
    const std::vector<uint8_t> animatedEffectBytes =
        buildMpcFileFromImages({effect, effectSecondFrame}, 1, 20, 0);
    if (!check(writeRawFile(firstPath, magicFixture(QStringLiteral("第一式"))) &&
                   writeRawFile(secondPath, magicFixture(QStringLiteral("第二式"))) &&
                   icon.save(QDir(imageDirectory).filePath("icon.png")) &&
                   effect.save(QDir(effectDirectory).filePath("super.png")) &&
                   icon.save(QDir(characterDirectory).filePath("action.png")) &&
                   effect.save(QDir(characterDirectory).filePath("use-action.png")) &&
                   writeRawFile(
                       QDir(effectDirectory).filePath("fly.mpc"),
                       QByteArray(reinterpret_cast<const char*>(
                                      animatedEffectBytes.data()),
                                  static_cast<qsizetype>(
                                      animatedEffectBytes.size()))) &&
                   writeRawFile(
                       QDir(effectDirectory).filePath("hit.mpc"),
                       QByteArray(reinterpret_cast<const char*>(
                                      animatedEffectBytes.data()),
                                  static_cast<qsizetype>(
                                      animatedEffectBytes.size()))),
               "write magic editor fixtures"))
    {
        return false;
    }

    MagicEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(1180, 820);
    window.setAssetsBasePath(assetsRoot);
    window.show();
    QApplication::processEvents();

    bool ok = check(window.openFile(firstPath), "open magic through focused editor");
    QApplication::processEvents();
    auto* list = window.findChild<QListWidget*>("magicDocumentList");
    auto* search = window.findChild<QLineEdit*>("magicSearchEdit");
    auto* name = window.findChild<QLineEdit*>("magicNameEdit");
    auto* intro = window.findChild<QPlainTextEdit*>("magicIntroductionEdit");
    auto* moveKind = window.findChild<QComboBox*>("magicMoveKindCombo");
    auto* specialKind =
        window.findChild<QComboBox*>("magicSpecialKindCombo");
    auto* alphaBlend =
        window.findChild<QComboBox*>("magicAlphaBlendCombo");
    auto* previewLevel =
        window.findChild<QSpinBox*>("magicPreviewLevelSpin");
    auto* speed = window.findChild<QSpinBox*>("magicSpeedSpin");
    auto* waitFrame = window.findChild<QSpinBox*>("magicWaitFrameSpin");
    auto* lifeFrame = window.findChild<QSpinBox*>("magicLifeFrameSpin");
    auto* flyingLum = window.findChild<QSpinBox*>("magicFlyingLumSpin");
    auto* vanishLum = window.findChild<QSpinBox*>("magicVanishLumSpin");
    auto* levelEffect =
        window.findChild<QLabel*>("magicSelectedLevelEffectLabel");
    auto* table = window.findChild<QTableWidget*>("magicLevelTable");
    auto* undo = window.findChild<QUndoStack*>("magicUndoStack");
    auto* playtest = window.findChild<QAction*>("magicPlaytestAction");
    auto* iconPreview = window.findChild<MpcPreviewLabel*>("magicIconPreview");
    auto* flyingPreview =
        window.findChild<MpcPreviewLabel*>("magicFlyingImagePreview");
    auto* vanishPreview =
        window.findChild<MpcPreviewLabel*>("magicVanishImagePreview");
    auto* superModePreview =
        window.findChild<MpcPreviewLabel*>("magicSuperModeImagePreview");
    auto* attackFile =
        window.findChild<QComboBox*>("magicAttackFileCombo");
    ok = check(list && list->count() == 2 && search && name && intro &&
                   moveKind && specialKind && alphaBlend && previewLevel &&
                   speed && waitFrame && lifeFrame && flyingLum && vanishLum &&
                   levelEffect && table && undo && playtest && iconPreview &&
                   flyingPreview && vanishPreview && superModePreview && attackFile,
               "expose searchable list, authoring fields, levels, previews and actions") && ok;
    ok = check(name && name->text() == QStringLiteral("第一式") &&
                   table && table->item(1, 1)->text() == QStringLiteral("10") &&
                   table->item(1, 4)->text() == QStringLiteral("4"),
               "show identity and inherited level values") && ok;
    ok = check(moveKind &&
                   moveKind->itemText(moveKind->findData(10)) ==
                       QStringLiteral("直线移动") &&
                   moveKind->itemText(moveKind->findData(13)) ==
                       QStringLiteral("对自身"),
               "describe moving-line and self effects in author-facing terms") && ok;
    ok = check(previewLevel && previewLevel->value() == 1 &&
                   levelEffect && levelEffect->text() == QStringLiteral("10") &&
                   moveKind && moveKind->currentData().toInt() == 11 &&
                   specialKind && specialKind->currentData().toInt() == 2 &&
                   alphaBlend && alphaBlend->currentData().toInt() == 1 &&
                   speed && speed->value() == 8 && waitFrame->value() == 4 &&
                   lifeFrame->value() == 40 && flyingLum->value() == 15 &&
                   vanishLum->value() == 24,
                "show the selected level's effective action and visual behavior") && ok;
    ok = check(iconPreview && !iconPreview->currentPixmapSize().isEmpty() &&
                   superModePreview && !superModePreview->currentPixmapSize().isEmpty(),
                "preview referenced static magic images") && ok;
    ok = check(flyingPreview && !flyingPreview->currentPixmapSize().isEmpty() &&
                   flyingPreview->isAnimationPlaying(),
                "continuously play the flight effect preview") && ok;
    ok = check(vanishPreview && !vanishPreview->currentPixmapSize().isEmpty() &&
                   vanishPreview->isAnimationPlaying(),
                "continuously play the hit effect preview") && ok;
    ok = check(attackFile && attackFile->currentText() ==
                       QStringLiteral("second.ini") &&
                   attackFile->findText(QStringLiteral("first.ini")) >= 0,
               "offer existing magic files for the practice attack effect") && ok;

    auto* resourcesGroup =
        window.findChild<QGroupBox*>("magicResourcesGroup");
    ok = check(TranslationManager::instance().setLanguage("en_US"),
               "switch magic editor to English") && ok;
    QApplication::processEvents();
    ok = check(playtest->text() == QStringLiteral("Playtest Current Skill") &&
                   resourcesGroup &&
                   resourcesGroup->title() == QStringLiteral("Images and Sounds") &&
                   moveKind->itemText(moveKind->findData(10)) ==
                       QStringLiteral("Moving Line") &&
                   moveKind->itemText(moveKind->findData(13)) ==
                       QStringLiteral("Self") &&
                   table->horizontalHeaderItem(6)->text() ==
                       QStringLiteral("Attack Distance"),
               "retranslate the open magic editor without losing state") && ok;
    ok = check(TranslationManager::instance().setLanguage("zh_TW"),
               "switch magic editor to Traditional Chinese") && ok;
    QApplication::processEvents();
    ok = check(playtest->text() == QString::fromUtf8("試玩目前武功") &&
                   resourcesGroup->title() == QString::fromUtf8("圖片與聲音") &&
                   moveKind->itemText(moveKind->findData(10)) ==
                       QString::fromUtf8("直線移動") &&
                   moveKind->itemText(moveKind->findData(13)) ==
                       QString::fromUtf8("對自身") &&
                   table->horizontalHeaderItem(6)->text() ==
                       QString::fromUtf8("攻擊距離"),
               "translate the open magic editor to Traditional Chinese") && ok;
    ok = check(TranslationManager::instance().setLanguage("zh_CN"),
               "restore source language after translation checks") && ok;
    QApplication::processEvents();

    previewLevel->setValue(2);
    QApplication::processEvents();
    ok = check(table->currentRow() == 1 && window.selectedLevel() == 2 &&
                   moveKind->currentData().toInt() == 10 &&
                   speed->value() == 12 &&
                   specialKind->currentData().toInt() == 3,
                "switch level beside the preview and show its effective behavior") && ok;
    table->setCurrentCell(0, 1);
    QApplication::processEvents();
    ok = check(previewLevel->value() == 1 && window.selectedLevel() == 1 &&
                   moveKind->currentData().toInt() == 11,
                "keep the level table and nearby preview selector synchronized") && ok;

    speed->setValue(33);
    QApplication::processEvents();
    ok = check(window.desktopRunDocumentSnapshot().bytes.contains("Speed=33"),
               "edit the selected level's movement and presentation values") && ok;

    search->setText(QStringLiteral("第二式"));
    QApplication::processEvents();
    int visibleItems = 0;
    for (int index = 0; index < list->count(); ++index)
        visibleItems += list->item(index)->isHidden() ? 0 : 1;
    ok = check(visibleItems == 1,
               "filter magic list by author-facing name") && ok;
    search->clear();

    bool playtestRequested = false;
    QObject::connect(&window, &MagicEditorWindow::playtestRequested,
                     [&playtestRequested]() { playtestRequested = true; });
    name->setText(QStringLiteral("第一式·改"));
    intro->setPlainText(QStringLiteral("尚未离开输入框的修改"));
    playtest->trigger();
    QApplication::processEvents();
    const DesktopRunDocumentSnapshot dirtySnapshot =
        window.desktopRunDocumentSnapshot();
    ok = check(playtestRequested && window.hasUnsavedChanges() &&
                   dirtySnapshot.type == ProjectDocumentType::Magic &&
                   dirtySnapshot.dirty && dirtySnapshot.serializationSupported &&
                   dirtySnapshot.bytes.contains(
                       QStringLiteral("Name=第一式·改").toUtf8()) &&
                   dirtySnapshot.bytes.contains(
                       QStringLiteral("Intro=尚未离开输入框的修改").toUtf8()) &&
                   dirtySnapshot.bytes.contains("UnknownFlag=keep-me"),
               "flush focused text fields into the exact playtest overlay snapshot") && ok;

    undo->undo();
    ok = check(name->text() == QStringLiteral("第一式"),
               "undo grouped pending text changes") && ok;
    undo->redo();
    ok = check(name->text() == QStringLiteral("第一式·改"),
               "redo grouped pending text changes") && ok;

    table->item(1, 1)->setText(QStringLiteral("77"));
    QApplication::processEvents();
    ok = check(window.desktopRunDocumentSnapshot().bytes.contains("Effect=77"),
               "edit a level-specific magic parameter") && ok;
    ok = check(window.saveFile() && !window.hasUnsavedChanges(),
               "save the current magic without a separate technical workflow") && ok;

    MagicDocument reopened;
    QString error;
    ok = check(reopened.openFile(firstPath, &error) &&
                   reopened.text(MagicTextField::Name) ==
                       QStringLiteral("第一式·改") &&
                   reopened.effectiveInteger(MagicIntegerField::Effect, 2) == 77 &&
                   reopened.serializedBytes().contains("UnknownFlag=keep-me"),
               "reopen the editor save and retain hidden fields") && ok;
    const QList<ProjectDocumentState> states = window.currentProjectDocuments();
    ok = check(states.size() == 1 &&
                   states.front().type == ProjectDocumentType::Magic &&
                   !states.front().dirty,
               "publish one clean magic document to the shared workspace registry") && ok;
    ok = check(MagicEditorWindow::isMagicFilePath(firstPath) &&
                   !MagicEditorWindow::isMagicFilePath(
                       QDir(assetsRoot).filePath("ini/npc/first.ini")),
               "route only ini/magic files to the magic editor") && ok;

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
    ok = testMagicDocumentRoundTrip() && ok;
    ok = testMagicRangePreviewRegionShapes() && ok;
    ok = testMagicRangePreviewMovementShapes() && ok;
    ok = testMagicEditorMinimumLoop() && ok;
    return ok ? 0 : 1;
}
