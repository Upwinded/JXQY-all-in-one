#pragma once

#include "LuaLexer.h"
#include "StoryGraphModel.h"

#include <QByteArray>
#include <QString>

#include <functional>

struct StoryGraphSourceSnapshot
{
    StoryGraphSourceIdentity identity;
    QByteArray utf8Bytes;
};

struct StoryGraphAnalysisRequest
{
    StoryGraphSourceSnapshot source;
    quint64 analysisGeneration = 0;
    QString effectiveMapFolder;
    bool effectiveMapFolderKnown = false;
    bool includeUnknownCalls = true;
};

class StoryGraphAnalyzer
{
public:
    using CancelCallback = std::function<bool()>;

    static StoryGraphDocumentResult analyze(
        const StoryGraphAnalysisRequest& request,
        const CancelCallback& cancelCallback =
            CancelCallback());
};
