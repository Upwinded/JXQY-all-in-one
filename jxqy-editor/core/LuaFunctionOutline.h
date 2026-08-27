#pragma once

#include <QList>
#include <QString>

struct LuaFunctionOutlineEntry
{
    QString name;
    int lineNumber = 0;
    bool isLocal = false;
};

class LuaFunctionOutline
{
public:
    static QList<LuaFunctionOutlineEntry> parse(const QString& sourceText);
};
