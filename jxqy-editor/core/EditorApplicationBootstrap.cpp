#include "EditorApplicationBootstrap.h"

#include "AssetCliRunner.h"
#include "BuildVersion.h"

#include <QCoreApplication>

void initializeEditorApplication(
    QCoreApplication& application,
    EditorApplicationSurface surface)
{
    application.setOrganizationName(
        QStringLiteral("JXQY"));
    application.setApplicationName(
        surface == EditorApplicationSurface::Graphical
        ? QStringLiteral("JXQY Editor")
        : QStringLiteral("jxqy-editor-cli"));
    application.setApplicationVersion(
        BuildVersion::editorProductVersion());
}

EditorCommandDispatchResult dispatchEditorAssetCommand(
    const QStringList& arguments,
    EditorCommandDispatchMode mode,
    std::FILE* standardOutput,
    std::FILE* standardError)
{
    EditorCommandDispatchResult result;
    result.handled =
        mode == EditorCommandDispatchMode::RequireCommand ||
        AssetCliRunner::shouldHandle(arguments);
    if (!result.handled)
        return result;
    result.exitCode = AssetCliRunner::run(
        arguments, standardOutput, standardError);
    return result;
}
