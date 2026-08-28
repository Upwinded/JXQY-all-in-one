#include "../core/ProjectManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeJson(const QString& path, const QJsonObject& root)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes =
        QJsonDocument(root).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() &&
        file.flush();
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

bool hasNoLocalProjectFields(const QJsonObject& root)
{
    return !root.contains(QStringLiteral("theme")) &&
        !root.contains(QStringLiteral("windowPlacement")) &&
        !root.contains(QStringLiteral("windowGeometry")) &&
        !root.contains(QStringLiteral("windowState")) &&
        !root.contains(QStringLiteral("recentFiles")) &&
        !root.contains(QStringLiteral("documentSession"));
}

ProjectDocumentSessionState sampleSession()
{
    ProjectDocumentSessionState session;
    ProjectSessionWindowState scriptWindow;
    scriptWindow.type = ProjectSessionWindowType::Script;
    scriptWindow.primaryPath = QStringLiteral("script/local-state.lua");
    session.windows.append(scriptWindow);
    ProjectSessionWindowState magicWindow;
    magicWindow.type = ProjectSessionWindowType::Magic;
    magicWindow.primaryPath = QStringLiteral("ini/magic/local-state.ini");
    session.windows.append(magicWindow);
    ProjectSessionWindowState goodsWindow;
    goodsWindow.type = ProjectSessionWindowType::Goods;
    goodsWindow.primaryPath = QStringLiteral("ini/goods/local-state.ini");
    session.windows.append(goodsWindow);
    ProjectSessionWindowState shopWindow;
    shopWindow.type = ProjectSessionWindowType::Shop;
    shopWindow.primaryPath = QStringLiteral("ini/buy/local-state.ini");
    session.windows.append(shopWindow);
    ProjectSessionWindowState dialogueWindow;
    dialogueWindow.type = ProjectSessionWindowType::Dialogue;
    dialogueWindow.primaryPath =
        QStringLiteral("script/map/test/talk.txt");
    session.windows.append(dialogueWindow);
    session.activeWindowPath = dialogueWindow.primaryPath;
    return session;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("JXQY"));
    QCoreApplication::setApplicationName(
        QStringLiteral("ProjectLocalStateTests"));

    QTemporaryDir temporaryDirectory;
    bool ok = check(
        temporaryDirectory.isValid(),
        "create isolated project local-state directory");
    if (!ok)
        return 1;

    const QDir root(temporaryDirectory.path());
    application.setProperty(
        "configFilePath",
        root.filePath(QStringLiteral("editor_config.ini")));

    ProjectManager& manager = ProjectManager::instance();
    const QString projectPath =
        root.filePath(QStringLiteral("shared.jxqyproj"));
    ok = check(
        manager.newProject(projectPath),
        "create shared project") && ok;
    manager.setEditableAssetsRoot(
        root.filePath(QStringLiteral("assets")));
    manager.setActiveResourcePackId(
        QStringLiteral("LOCAL_STATE_TEST"));
    ok = check(
        manager.saveProject() &&
            !manager.hasUnsavedChanges(),
        "save shared project fields") && ok;

    const QRect placement(50, 70, 1280, 720);
    const QByteArray windowState =
        QByteArray::fromHex("0010aaff");
    manager.setTheme(QStringLiteral("light"));
    manager.setWindowPlacement(
        placement, WindowDisplayMode::Maximized);
    manager.setWindowState(windowState);
    manager.addRecentFile(
        QStringLiteral("script/local-state.lua"));
    manager.setDocumentSession(sampleSession());
    ok = check(
        !manager.hasUnsavedChanges(),
        "local UI state does not dirty shared project data") && ok;

    const QJsonObject savedSharedRoot =
        readJson(projectPath);
    ok = check(
        hasNoLocalProjectFields(savedSharedRoot),
        ".jxqyproj omits every machine-local UI field") && ok;
    ok = check(
        manager.closeProject() &&
            manager.openProject(projectPath) &&
            manager.theme() == QStringLiteral("light") &&
            manager.hasWindowPlacement() &&
            manager.windowGeometry() == placement &&
            manager.windowMode() ==
                WindowDisplayMode::Maximized &&
            manager.windowState() == windowState &&
            manager.recentFiles() ==
                QStringList{
                    QStringLiteral(
                        "script/local-state.lua")} &&
            manager.documentSession() ==
                sampleSession(),
        "local UI state round-trips through user settings") && ok;

    const QString copiedProjectPath =
        root.filePath(QStringLiteral("copied.jxqyproj"));
    ok = check(
        manager.closeProject() &&
            QFile::copy(projectPath, copiedProjectPath) &&
            manager.openProject(copiedProjectPath) &&
            manager.theme() == QStringLiteral("dark") &&
            !manager.hasWindowPlacement() &&
            manager.windowState().isEmpty() &&
            manager.recentFiles().isEmpty() &&
            manager.documentSession().windows.isEmpty(),
        "copying shared project data does not copy machine-local state") &&
        ok;

    const QString legacyProjectPath =
        root.filePath(QStringLiteral("legacy.jxqyproj"));
    QJsonObject legacyGeometry;
    legacyGeometry[QStringLiteral("x")] = 5;
    legacyGeometry[QStringLiteral("y")] = 9;
    legacyGeometry[QStringLiteral("width")] = 800;
    legacyGeometry[QStringLiteral("height")] = 600;
    QJsonObject legacySession;
    legacySession[QStringLiteral("version")] =
        ProjectDocumentSessionState::schemaVersion;
    QJsonObject legacyWindow;
    legacyWindow[QStringLiteral("type")] =
        QStringLiteral("script");
    legacyWindow[QStringLiteral("path")] =
        QStringLiteral("script/legacy.lua");
    legacySession[QStringLiteral("windows")] =
        QJsonArray{legacyWindow};
    legacySession[QStringLiteral("activeWindow")] =
        QStringLiteral("script/legacy.lua");
    QJsonObject legacyRoot;
    legacyRoot[QStringLiteral("assetsPath")] =
        QStringLiteral("assets");
    legacyRoot[QStringLiteral("theme")] =
        QStringLiteral("light");
    legacyRoot[QStringLiteral("windowGeometry")] =
        legacyGeometry;
    legacyRoot[QStringLiteral("windowState")] =
        QString::fromUtf8(windowState.toBase64());
    legacyRoot[QStringLiteral("recentFiles")] =
        QJsonArray{
            QStringLiteral("script/legacy.lua")};
    legacyRoot[QStringLiteral("documentSession")] =
        legacySession;
    ok = check(
        manager.closeProject() &&
            writeJson(legacyProjectPath, legacyRoot) &&
            manager.openProject(legacyProjectPath) &&
            manager.theme() == QStringLiteral("light") &&
            manager.windowGeometry() ==
                QRect(5, 9, 800, 600) &&
            manager.recentFiles() ==
                QStringList{
                    QStringLiteral("script/legacy.lua")} &&
            manager.documentSession().windows.size() == 1 &&
            manager.hasUnsavedChanges(),
        "legacy project-local fields remain a migration input") && ok;
    ok = check(
        manager.saveProject() &&
            hasNoLocalProjectFields(
                readJson(legacyProjectPath)) &&
            manager.closeProject() &&
            manager.openProject(legacyProjectPath) &&
            manager.theme() == QStringLiteral("light") &&
            manager.windowGeometry() ==
                QRect(5, 9, 800, 600) &&
            manager.documentSession().windows.size() == 1,
        "legacy local state moves to user settings and is stripped from the project") &&
        ok;

    const QString malformedLocalPath =
        root.filePath(QStringLiteral("malformed-local.jxqyproj"));
    QJsonObject malformedLocalRoot =
        readJson(projectPath);
    malformedLocalRoot[QStringLiteral("theme")] =
        QJsonObject();
    malformedLocalRoot[QStringLiteral("windowState")] =
        42;
    malformedLocalRoot[QStringLiteral("recentFiles")] =
        QStringLiteral("invalid");
    malformedLocalRoot[QStringLiteral("documentSession")] =
        QStringLiteral("invalid");
    ok = check(
        manager.closeProject() &&
            writeJson(
                malformedLocalPath,
                malformedLocalRoot) &&
            manager.openProject(malformedLocalPath) &&
            manager.theme() == QStringLiteral("dark") &&
            manager.windowState().isEmpty() &&
            manager.recentFiles().isEmpty() &&
            manager.documentSession().windows.isEmpty(),
        "malformed legacy local fields do not block shared project loading") &&
        ok;

    if (manager.isProjectOpen())
        ok = check(
            manager.closeProject(),
            "close final project local-state fixture") && ok;
    return ok ? 0 : 1;
}
