#pragma once

#include <QString>
#include <functional>

class QLineEdit;
class QMimeData;

namespace AssetDragDrop
{

struct Payload
{
    QString sourceRoot;
    QString relativePath;
    QString absolutePath;
};

struct DropResult
{
    bool accepted = false;
    QString resourceReference;
    QString errorMessage;
};

using DropHandler = std::function<DropResult(const Payload&)>;

QString mimeType();

/// Creates a versioned payload for one existing regular file at a strict logical
/// path below sourceRoot. Formal descendant links are followed when reading.
/// Returns nullptr when the root/path pair is invalid.
QMimeData* createMimeData(const QString& sourceRoot,
                          const QString& relativePath);

/// Revalidates the logical root, strict relative path and file type from MIME data.
bool decodeMimeData(const QMimeData* mimeData, Payload& payload,
                    QString* errorMessage = nullptr);

/// Installs a type-aware drop target. The handler is evaluated without mutation
/// during drag feedback and once more on drop; only an accepted result is written.
void installLineEditTarget(QLineEdit* lineEdit, DropHandler handler);

}
