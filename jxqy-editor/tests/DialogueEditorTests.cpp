#include "TestSupport.h"

#include "../core/DialogueDocument.h"
#include "../core/TranslationManager.h"
#include "../ui/DialogueEditorWindow.h"
#include "../ui/MainWindow.h"
#include "../ui/MpcPreviewLabel.h"
#include "../ui/NpcDataEditorWindow.h"
#include "../ui/ScriptEditorWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QUndoStack>


namespace
{
using namespace TestSupport;

QByteArray dialogueFixture()
{
    return QByteArray::fromHex("efbbbf") +
        QStringLiteral(
            "; 地图对话说明\r\n"
            "[name]\r\n"
            "1=测试地图\r\n"
            "\r\n"
            "[greeting]\r\n"
            "head1=hero.mpc\r\n"
            "1=飞云：你好<enter>少侠\r\n"
            "head2=\r\n"
            "2=旁白文字\r\n"
            "3=无头像变更\r\n"
            "（作者场景说明）\r\n"
            "FutureKey=keep-me\r\n"
            "\r\n"
            "[farewell]\r\n"
            "1=掌柜:一路顺风\r\n")
            .toUtf8();
}

bool testDialogueDocument()
{
    DialogueDocument document;
    QString error;
    bool ok = check(document.load(dialogueFixture(), &error),
                    "load UTF-8 BOM dialogue fixture");
    ok = check(document.sectionNames() ==
                   QStringList({QStringLiteral("greeting"),
                                QStringLiteral("farewell")}),
               "keep real dialogue order and hide the map-name section") && ok;
    const DialogueLine first = document.line(QStringLiteral("greeting"), 0);
    const DialogueLine second = document.line(QStringLiteral("greeting"), 1);
    const DialogueLine third = document.line(QStringLiteral("greeting"), 2);
    ok = check(first.speaker == QStringLiteral("飞云") &&
                   first.text == QStringLiteral("你好\n少侠") &&
                   first.portraitMode == DialoguePortraitMode::Reference &&
                   first.portraitReference == QStringLiteral("hero.mpc") &&
                   second.speaker.isEmpty() &&
                   second.portraitMode == DialoguePortraitMode::NoPortrait &&
                   third.portraitMode ==
                       DialoguePortraitMode::KeepPrevious,
               "present speaker, line breaks and all three portrait states") && ok;
    const DialogueResolvedPortrait inherited = document.resolvedPortrait(
        QStringLiteral("greeting"), 2);
    ok = check(inherited.reference == QStringLiteral("hero.mpc") &&
                   inherited.sourceRow == 0 && !inherited.explicitlyHidden,
               "resolve a missing head entry from the previous same-side line") && ok;
    ok = check(document.hiddenFieldCount() == 2,
               "count map metadata and unknown dialogue fields as hidden") && ok;

    ok = check(document.setLine(
                   QStringLiteral("greeting"), 0,
                   QStringLiteral("少侠"),
                   QStringLiteral("第一行\n第二行")) &&
                   document.setPortrait(
                       QStringLiteral("greeting"), 0,
                       DialoguePortraitMode::KeepPrevious),
               "edit one visible dialogue line") && ok;
    const QByteArray bytes = document.serializedBytes();
    ok = check(bytes.startsWith(QByteArray::fromHex("efbbbf")) &&
                   bytes.contains(
                       QStringLiteral("1=少侠：第一行<enter>第二行").toUtf8()) &&
                    !bytes.contains("head1=") &&
                    bytes.contains("head2=") &&
                    bytes.contains("FutureKey=keep-me") &&
                    bytes.contains(
                        QStringLiteral("（作者场景说明）").toUtf8()) &&
                    bytes.contains("; 地图对话说明"),
               "serialize visible edits while preserving hidden content") && ok;

    QTemporaryDir directory;
    const QString path = QDir(directory.path()).filePath(
        QStringLiteral("script/map/test/talk.txt"));
    ok = check(directory.isValid() &&
                   QDir().mkpath(QFileInfo(path).path()) &&
                   document.saveFile(path, &error),
               "atomically save dialogue fixture") && ok;
    DialogueDocument reopened;
    ok = check(reopened.openFile(path, &error) &&
                   reopened.line(QStringLiteral("greeting"), 0).speaker ==
                       QStringLiteral("少侠") &&
                   reopened.serializedBytes().contains("FutureKey=keep-me"),
               "reopen edited dialogue without losing hidden content") && ok;
    return ok;
}

bool testDialogueReferences()
{
    const QString script = QStringLiteral(
        "function main()\n"
        "    talk(\"greeting\")\n"
        "    mytalk(\"ignored\")\n"
        "    Talk('fare\\'well')\n"
        "end\n");
    const QVector<DialogueReference> references =
        DialogueDocument::findLiteralTalkReferences(script);
    bool ok = check(references.size() == 2 &&
                        references[0].section ==
                            QStringLiteral("greeting") &&
                        references[0].line == 2 &&
                        references[1].section ==
                            QStringLiteral("fare'well") &&
                        references[1].line == 4,
                    "find only direct literal talk calls with source locations");
    const int cursor = script.indexOf(QStringLiteral("greeting"));
    const std::optional<DialogueReference> atCursor =
        DialogueDocument::literalTalkReferenceAt(script, cursor);
    ok = check(atCursor &&
                   atCursor->section == QStringLiteral("greeting") &&
                   DialogueDocument::talkFileForScript(
                       QStringLiteral("C:/assets/script/map/test/npc.txt"))
                       .endsWith(QStringLiteral("/talk.txt")),
               "resolve the talk call under the cursor and sibling talk file") && ok;
    return ok;
}

bool testDialogueEditorMinimumLoop()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               "create dialogue editor temporary directory"))
    {
        return false;
    }
    const QString assetsRoot = QDir(directory.path()).filePath("assets");
    const QString talkPath = QDir(assetsRoot).filePath(
        "script/map/test/talk.txt");
    const QString scriptPath = QDir(assetsRoot).filePath(
        "script/map/test/npc.txt");
    const QString portraitDirectory = QDir(assetsRoot).filePath(
        "mpc/portrait");
    if (!check(QDir().mkpath(QFileInfo(talkPath).path()) &&
                   QDir().mkpath(portraitDirectory),
               "create dialogue editor resource directories"))
    {
        return false;
    }
    QImage portrait(64, 80, QImage::Format_ARGB32_Premultiplied);
    portrait.fill(QColor(80, 140, 210));
    if (!check(writeRawFile(talkPath, dialogueFixture()) &&
                   writeRawFile(scriptPath,
                                QByteArray("talk(\"greeting\")\n")) &&
                   portrait.save(
                       QDir(portraitDirectory).filePath("hero.mpc"), "PNG"),
               "write dialogue editor fixtures"))
    {
        return false;
    }

    DialogueEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(1180, 760);
    window.setAssetsBasePath(assetsRoot);
    window.show();
    QApplication::processEvents();

    bool ok = check(window.openFile(talkPath, QStringLiteral("greeting")),
                    "open requested dialogue section");
    QApplication::processEvents();
    auto* sections = window.findChild<QListWidget*>("dialogueSectionList");
    auto* lines = window.findChild<QListWidget*>("dialogueLineList");
    auto* search = window.findChild<QLineEdit*>("dialogueSearchEdit");
    auto* speaker = window.findChild<QLineEdit*>("dialogueSpeakerEdit");
    auto* text = window.findChild<QPlainTextEdit*>("dialogueTextEdit");
    auto* portraitMode =
        window.findChild<QComboBox*>("dialoguePortraitModeCombo");
    auto* portraitReference =
        window.findChild<QComboBox*>("dialoguePortraitReferenceCombo");
    auto* preview =
        window.findChild<MpcPreviewLabel*>("dialoguePortraitPreview");
    auto* undo = window.findChild<QUndoStack*>("dialogueUndoStack");
    auto* playtest = window.findChild<QAction*>("dialoguePlaytestAction");
    auto* returnAction = window.findChild<QAction*>("dialogueReturnAction");
    ok = check(sections && sections->count() == 2 && lines &&
                   lines->count() == 3 && search && speaker && text &&
                   portraitMode && portraitReference && preview && undo &&
                   playtest && returnAction &&
                   speaker->text() == QStringLiteral("飞云") &&
                   text->toPlainText() == QStringLiteral("你好\n少侠") &&
                   !preview->currentPixmapSize().isEmpty(),
               "show dialogue sections, author fields and real portrait preview") && ok;
    lines->setCurrentRow(2);
    QApplication::processEvents();
    ok = check(portraitMode->currentText().contains(
                       QStringLiteral("上上句")) &&
                   !preview->currentPixmapSize().isEmpty() &&
                   preview->toolTip().contains(QStringLiteral("第 1 句")),
               "preview the portrait inherited from the previous same-side line") && ok;
    lines->setCurrentRow(0);
    QApplication::processEvents();

    search->setText(QStringLiteral("一路顺风"));
    QApplication::processEvents();
    ok = check(sections->count() == 1 &&
                   sections->item(0)->data(Qt::UserRole).toString() ==
                       QStringLiteral("farewell"),
               "search dialogue by visible text") && ok;
    search->clear();
    window.selectSection(QStringLiteral("greeting"));
    QApplication::processEvents();

    speaker->setText(QStringLiteral("少侠"));
    text->setPlainText(QStringLiteral("改后第一行\n改后第二行"));
    const int referenceIndex = portraitMode->findData(
        static_cast<int>(DialoguePortraitMode::Reference));
    portraitMode->setCurrentIndex(referenceIndex);
    portraitReference->setCurrentText(QStringLiteral("hero.mpc"));
    bool playtestRequested = false;
    QObject::connect(&window, &DialogueEditorWindow::playtestRequested,
                     [&playtestRequested]() { playtestRequested = true; });
    playtest->trigger();
    DesktopRunDocumentSnapshot snapshot =
        window.desktopRunDocumentSnapshot();
    ok = check(playtestRequested && window.hasUnsavedChanges() &&
                   snapshot.type == ProjectDocumentType::Dialogue &&
                   snapshot.dirty && snapshot.serializationSupported &&
                   snapshot.bytes.contains(
                       QStringLiteral(
                           "1=少侠：改后第一行<enter>改后第二行")
                           .toUtf8()) &&
                   snapshot.bytes.contains("head1=hero.mpc") &&
                   snapshot.bytes.contains("FutureKey=keep-me"),
               "flush pending dialogue fields into the playtest snapshot") && ok;

    window.setCaller(scriptPath, 1, 1);
    QString returnedPath;
    int returnedLine = 0;
    QObject::connect(&window,
                     &DialogueEditorWindow::returnToCallerRequested,
                     [&](const QString& path, int line, int)
                     {
                         returnedPath = path;
                         returnedLine = line;
                     });
    returnAction->trigger();
    ok = check(returnedPath == QFileInfo(scriptPath).absoluteFilePath() &&
                   returnedLine == 1,
               "return to the exact script call location") && ok;

    undo->undo();
    QApplication::processEvents();
    ok = check(speaker->text() == QStringLiteral("飞云") &&
                   text->toPlainText() == QStringLiteral("你好\n少侠") &&
                   !window.hasUnsavedChanges(),
               "undo dialogue fields as one author action") && ok;
    ok = check(TranslationManager::instance().setLanguage("en_US"),
               "switch dialogue editor to English") && ok;
    QApplication::processEvents();
    ok = check(playtest->text() ==
                   QStringLiteral("Playtest Current Dialogue") &&
                   returnAction->text() ==
                       QStringLiteral("Return to Call Site"),
               "retranslate the open dialogue workspace to English") && ok;
    ok = check(TranslationManager::instance().setLanguage("zh_TW"),
               "switch dialogue editor to Traditional Chinese") && ok;
    QApplication::processEvents();
    ok = check(playtest->text() == QString::fromUtf8("試玩目前對話") &&
                   returnAction->text() ==
                       QString::fromUtf8("返回呼叫位置"),
               "retranslate the open dialogue workspace to Traditional Chinese") && ok;
    ok = check(TranslationManager::instance().setLanguage("zh_CN"),
               "restore source language after dialogue checks") && ok;
    QApplication::processEvents();
    speaker->setText(QStringLiteral("少侠"));
    snapshot = window.desktopRunDocumentSnapshot();
    ok = check(window.saveFile() && !window.hasUnsavedChanges(),
               "save the edited dialogue") && ok;
    DialogueDocument reopened;
    ok = check(reopened.openFile(talkPath) &&
                   reopened.line(QStringLiteral("greeting"), 0).speaker ==
                       QStringLiteral("少侠") &&
                   reopened.serializedBytes().contains("FutureKey=keep-me"),
               "reopen saved dialogue with hidden content") && ok;
    ok = check(DialogueEditorWindow::isDialogueFilePath(talkPath) &&
                   !DialogueEditorWindow::isDialogueFilePath(scriptPath),
               "route only map talk files to the dialogue editor") && ok;
    const QList<ProjectDocumentState> states =
        window.currentProjectDocuments();
    ok = check(states.size() == 1 &&
                   states.front().type == ProjectDocumentType::Dialogue &&
                   !states.front().dirty,
               "publish one clean dialogue document to shared registry") && ok;
    window.hide();
    return ok;
}

bool testMainWindowDialogueEntryPoints()
{
    QTemporaryDir directory;
    if (!check(directory.isValid(),
               "create main-window dialogue temporary directory"))
    {
        return false;
    }
    const QString assetsRoot = QDir(directory.path()).filePath("assets");
    const QString talkPath = QDir(assetsRoot).filePath(
        "script/map/test/talk.txt");
    const QString scriptPath = QDir(assetsRoot).filePath(
        "script/map/test/npc.txt");
    const QString npcPath = QDir(assetsRoot).filePath(
        "ini/save/test.npc");
    if (!check(QDir().mkpath(QFileInfo(talkPath).path()) &&
                   QDir().mkpath(QFileInfo(npcPath).path()) &&
                   writeRawFile(talkPath, dialogueFixture()) &&
                   writeRawFile(scriptPath,
                                QByteArray("talk(\"greeting\")\n")) &&
                   writeRawFile(
                       npcPath,
                       QStringLiteral(
                           "[Head]\r\n"
                           "Count=1\r\n"
                           "Map=test.map\r\n"
                           "[NPC000]\r\n"
                           "Name=测试 NPC\r\n"
                           "ScriptFile=npc.txt\r\n")
                           .toUtf8()),
               "write main-window dialogue entry fixtures"))
    {
        return false;
    }

    MainWindow window(nullptr);
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(1280, 820);
    window.show();
    QApplication::processEvents();
    bool ok = check(window.requestAssetsPathChange(assetsRoot),
                    "set the active assets root for dialogue entry tests");
    ok = check(window.openStartupFileArguments({scriptPath}),
               "open the NPC script through main-window routing") && ok;
    QApplication::processEvents();
    ScriptEditorWindow* scriptWindow =
        window.findChild<ScriptEditorWindow*>();
    QPushButton* dialogueButton = scriptWindow
        ? scriptWindow->findChild<QPushButton*>(
              "editCurrentDialogueButton")
        : nullptr;
    ok = check(scriptWindow && dialogueButton,
               "expose the dialogue entry in a runnable script editor") && ok;
    if (dialogueButton)
        dialogueButton->click();
    QApplication::processEvents();
    DialogueEditorWindow* dialogueWindow =
        window.findChild<DialogueEditorWindow*>();
    ok = check(dialogueWindow &&
                   dialogueWindow->currentFilePath() ==
                       QFileInfo(talkPath).absoluteFilePath() &&
                   dialogueWindow->currentSectionName() ==
                       QStringLiteral("greeting") &&
                   dialogueWindow->hasCaller(),
               "open the exact talk section from the script cursor") && ok;

    ok = check(window.openStartupFileArguments({npcPath}),
               "open an NPC list through main-window routing") && ok;
    QApplication::processEvents();
    NpcDataEditorWindow* npcWindow = nullptr;
    const QList<NpcDataEditorWindow*> npcWindows =
        window.findChildren<NpcDataEditorWindow*>();
    if (!npcWindows.isEmpty())
        npcWindow = npcWindows.constLast();
    QAction* npcDialogueAction = npcWindow
        ? npcWindow->findChild<QAction*>("editDialogueFromNpcAction")
        : nullptr;
    ok = check(npcWindow && npcDialogueAction &&
                   npcDialogueAction->isEnabled(),
               "enable dialogue editing for the selected scripted NPC") && ok;
    if (npcDialogueAction)
        npcDialogueAction->trigger();
    QApplication::processEvents();
    const QList<DialogueEditorWindow*> dialogueWindows =
        window.findChildren<DialogueEditorWindow*>();
    ok = check(dialogueWindows.size() == 1 &&
                   dialogueWindows.front()->currentSectionName() ==
                       QStringLiteral("greeting") &&
                   dialogueWindows.front()->hasCaller(),
               "reuse the same dialogue workspace from the NPC entry") && ok;
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
    ok = testDialogueDocument() && ok;
    ok = testDialogueReferences() && ok;
    ok = testDialogueEditorMinimumLoop() && ok;
    ok = testMainWindowDialogueEntryPoints() && ok;
    return ok ? 0 : 1;
}
