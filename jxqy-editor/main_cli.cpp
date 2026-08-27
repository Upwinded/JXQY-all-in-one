#include "core/EditorApplicationBootstrap.h"

#include <QCoreApplication>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    initializeEditorApplication(
        application,
        EditorApplicationSurface::CommandLine);
    const EditorCommandDispatchResult commandDispatch =
        dispatchEditorAssetCommand(
            application.arguments(),
            EditorCommandDispatchMode::RequireCommand,
            stdout,
            stderr);
    return commandDispatch.exitCode;
}
