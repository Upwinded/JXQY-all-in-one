#pragma once

#include <QElapsedTimer>
#include <QString>

struct EditorPerformanceBenchmarkOptions
{
    QString mapPath;
    QString assetsPath;
    QString reportPath;
    int interactionIterations = 16;
    int windowWidth = 1280;
    int windowHeight = 826;
};

int runEditorPerformanceBenchmark(
    const EditorPerformanceBenchmarkOptions& options,
    const QElapsedTimer& processStartupClock);
