#include "FocusedContentPlaytest.h"

#include "EditorAssetPath.h"

#include <QFileInfo>
#include <QStringList>

#include <algorithm>

namespace
{
QString luaStringLiteral(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return QStringLiteral("\"") + value + QLatin1Char('"');
}

bool hasExpectedPrefix(
    const QString& virtualPath,
    const QString& prefix)
{
    return virtualPath.startsWith(
        prefix,
        Qt::CaseInsensitive);
}

QString targetFileName(
    const QString& virtualPath)
{
    return QFileInfo(virtualPath).fileName();
}

bool isObviouslyUnconfiguredScene(
    const ProjectScene& scene)
{
    // A newly created scene is initialized at (0, 0) with no NPC or object
    // data.  That is a valid editable draft, but it is not a useful focused
    // playtest baseline: the corner is commonly an empty MAP tile and there
    // is no world content against which to inspect the selected asset.
    return scene.playerPosition.isNull() &&
        scene.npcPath.trimmed().isEmpty() &&
        scene.objectPath.trimmed().isEmpty();
}
}

FocusedContentPlaytestBootstrap buildFocusedContentPlaytestBootstrap(
    const ProjectScene& baseScene,
    FocusedContentPlaytestKind kind,
    const QString& targetVirtualPath,
    int magicLevel,
    const QString& dialogueSection)
{
    FocusedContentPlaytestBootstrap result;
    result.scene = baseScene;

    QString normalizedMapPath;
    QString normalizedTargetPath;
    if (!EditorAssetPath::normalizeResourcePath(
            baseScene.mapPath,
            normalizedMapPath) ||
        !EditorAssetPath::normalizeResourcePath(
            targetVirtualPath,
            normalizedTargetPath))
    {
        result.diagnosticCode = QStringLiteral(
            "editor_run.focused_content.invalid_path");
        return result;
    }

    const QString mapName =
        QFileInfo(normalizedMapPath).completeBaseName();
    const QString targetName =
        targetFileName(normalizedTargetPath);
    if (mapName.isEmpty() || targetName.isEmpty())
    {
        result.diagnosticCode = QStringLiteral(
            "editor_run.focused_content.missing_name");
        return result;
    }

    QStringList statements;
    switch (kind)
    {
    case FocusedContentPlaytestKind::Magic:
        if (!hasExpectedPrefix(
                normalizedTargetPath,
                QStringLiteral("ini/magic/")))
        {
            result.diagnosticCode = QStringLiteral(
                "editor_run.focused_content.magic_path_mismatch");
            return result;
        }
        statements = {
            // This overlay is executed as raw Lua. Runtime APIs are
            // registered with lower-case global names; ordinary author
            // scripts only accept mixed-case spellings because conversion
            // normalizes them before execution.
            QStringLiteral("clearmagic()"),
            QStringLiteral("addmagic(%1)")
                .arg(luaStringLiteral(targetName)),
            QStringLiteral("setmagiclevel(%1, %2)")
                .arg(luaStringLiteral(targetName))
                .arg(std::clamp(magicLevel, 1, 10)),
            QStringLiteral("setplayerstate(1)")
        };
        break;
    case FocusedContentPlaytestKind::Goods:
        if (!hasExpectedPrefix(
                normalizedTargetPath,
                QStringLiteral("ini/goods/")))
        {
            result.diagnosticCode = QStringLiteral(
                "editor_run.focused_content.goods_path_mismatch");
            return result;
        }
        statements = {
            QStringLiteral("addgoods(%1)")
                .arg(luaStringLiteral(targetName))
        };
        break;
    case FocusedContentPlaytestKind::Shop:
        if (!hasExpectedPrefix(
                normalizedTargetPath,
                QStringLiteral("ini/buy/")))
        {
            result.diagnosticCode = QStringLiteral(
                "editor_run.focused_content.shop_path_mismatch");
            return result;
        }
        statements = {
            QStringLiteral("addmoney(1000000)"),
            QStringLiteral("buygoods(%1)")
                .arg(luaStringLiteral(targetName))
        };
        break;
    case FocusedContentPlaytestKind::Dialogue:
    {
        const QString expectedDialoguePath = QStringLiteral(
            "script/map/%1/talk.txt").arg(mapName);
        if (normalizedTargetPath.compare(
                expectedDialoguePath,
                Qt::CaseInsensitive) != 0 ||
            dialogueSection.trimmed().isEmpty())
        {
            result.diagnosticCode = QStringLiteral(
                "editor_run.focused_content.dialogue_scene_mismatch");
            return result;
        }
        statements = {
            QStringLiteral("talk(%1)")
                .arg(luaStringLiteral(dialogueSection))
        };
        break;
    }
    }

    if (isObviouslyUnconfiguredScene(baseScene))
    {
        result.diagnosticCode = QStringLiteral(
            "editor_run.focused_content.scene_not_configured");
        return result;
    }

    QString entryVirtualPath;
    if (!EditorAssetPath::normalizeResourcePath(
            QStringLiteral(
                "script/map/%1/__jxqy_editor_current__/focused-content.lua")
                .arg(mapName),
            entryVirtualPath))
    {
        result.diagnosticCode = QStringLiteral(
            "editor_run.focused_content.invalid_entry_path");
        return result;
    }

    result.scene.entryScriptPath = entryVirtualPath;
    DesktopRunDocumentSnapshot& snapshot =
        result.entryScriptSnapshot;
    snapshot.overlayVirtualPath = entryVirtualPath;
    snapshot.type = ProjectDocumentType::Script;
    snapshot.includeInOverlay = true;
    snapshot.serializationSupported = true;
    const QString source =
        QStringLiteral("-- UPEdit-JXQY focused playtest\n") +
        statements.join(QStringLiteral("\n")) +
        QLatin1Char('\n');
    snapshot.bytes = source.toUtf8();
    return result;
}
