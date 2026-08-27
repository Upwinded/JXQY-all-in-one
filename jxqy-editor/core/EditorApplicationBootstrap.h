#pragma once

#include <QStringList>

#include <cstdio>

class QCoreApplication;

enum class EditorApplicationSurface
{
    Graphical,
    CommandLine
};

enum class EditorCommandDispatchMode
{
    HandleWhenRequested,
    RequireCommand
};

struct EditorCommandDispatchResult
{
    bool handled = false;
    int exitCode = 0;
};

// GUI and CLI retain their required QApplication/QCoreApplication entrypoints,
// while identity and asset-command routing stay identical.
void initializeEditorApplication(
    QCoreApplication& application,
    EditorApplicationSurface surface);

EditorCommandDispatchResult dispatchEditorAssetCommand(
    const QStringList& arguments,
    EditorCommandDispatchMode mode,
    std::FILE* standardOutput,
    std::FILE* standardError);
