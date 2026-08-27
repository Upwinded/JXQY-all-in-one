#pragma once

#include <QStringList>

#include <cstdio>

class AssetCliRunner
{
public:
    static bool shouldHandle(const QStringList& arguments);
    static int run(const QStringList& arguments, FILE* stdoutFile, FILE* stderrFile);

private:
    static int printUsage(const QString& appName, FILE* outputFile, int exitCode);
};
