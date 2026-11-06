#pragma once

#include "../core/ContentAuthoring.h"
#include <functional>

class QWidget;

void showContentReferences(QWidget* parent, const QString& root,
    const ContentReferenceReport& report,
    const std::function<void(const QString&, int)>& openReference);
