#pragma once

#include "DesktopRunDocumentSnapshot.h"
#include "ProjectRuntimeConfiguration.h"

#include <QString>

enum class FocusedContentPlaytestKind
{
    Magic,
    Goods,
    Shop,
    Dialogue
};

struct FocusedContentPlaytestBootstrap
{
    ProjectScene scene;
    DesktopRunDocumentSnapshot entryScriptSnapshot;
    QString diagnosticCode;

    bool succeeded() const
    {
        return diagnosticCode.isEmpty() &&
            !scene.entryScriptPath.isEmpty() &&
            entryScriptSnapshot.includeInOverlay &&
            entryScriptSnapshot.serializationSupported;
    }
};

// Builds a private, target-specific entry script for a focused playtest. The
// script is materialized only in the desktop-run overlay and never changes the
// saved scene, formal resources, or formal save data.
FocusedContentPlaytestBootstrap buildFocusedContentPlaytestBootstrap(
    const ProjectScene& baseScene,
    FocusedContentPlaytestKind kind,
    const QString& targetVirtualPath,
    int magicLevel = 1,
    const QString& dialogueSection = QString());
