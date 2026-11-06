#pragma once

#include "../core/EditorFileState.h"

class QWidget;

namespace EditorSaveConfirmation
{
bool confirmOverwrite(QWidget* parent, const QStringList& changedPaths);
EditorFileState::OverwriteConfirmation forWindow(QWidget* parent);
bool prepareBytesWrite(QWidget* parent, DurableFileTransaction& transaction,
    const QString& path, const QByteArray& bytes,
    bool expectedExists, const QByteArray& expectedBytes, QString& error);
}
