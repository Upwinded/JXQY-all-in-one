#pragma once

#include <QString>
#include <QVector>

struct ApiInfo
{
    QString signature;
    QString description;
    QString tooltip;
};

QVector<ApiInfo> buildScriptApiList();
