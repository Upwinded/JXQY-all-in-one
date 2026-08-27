#include "AssetDragDrop.h"

#include "../core/EditorAssetPath.h"

#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMimeData>
#include <QToolTip>

namespace AssetDragDrop
{
namespace
{
constexpr int PayloadVersion = 1;
const QString MimeType = QStringLiteral(
    "application/x-jxqy-editor-asset-reference");
[[maybe_unused]] const char* const TranslationSources[] = {
    QT_TRANSLATE_NOOP("AssetDragDrop", "该资源不能用于此字段。"),
    QT_TRANSLATE_NOOP("AssetDragDrop", "拖放数据不是编辑器资源引用。"),
    QT_TRANSLATE_NOOP("AssetDragDrop", "拖放资源数据格式无效。"),
    QT_TRANSLATE_NOOP("AssetDragDrop", "拖放资源数据版本不受支持。"),
    QT_TRANSLATE_NOOP("AssetDragDrop", "拖放资源的来源目录无效。"),
    QT_TRANSLATE_NOOP("AssetDragDrop", "拖放资源已越过来源目录。"),
    QT_TRANSLATE_NOOP("AssetDragDrop", "拖放资源不存在或不是普通文件。")
};

QString translated(const char* source)
{
    return QCoreApplication::translate("AssetDragDrop", source);
}

class LineEditDropTarget final : public QObject
{
public:
    LineEditDropTarget(QLineEdit* lineEdit, DropHandler handler)
        : QObject(lineEdit)
        , lineEdit(lineEdit)
        , handler(std::move(handler))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != lineEdit || !lineEdit->isEnabled() ||
            lineEdit->isReadOnly())
        {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::DragEnter)
        {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            QString errorMessage;
            if (canAccept(dragEvent->mimeData(), &errorMessage))
            {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->accept();
            }
            else
            {
                if (dragEvent->mimeData() &&
                    dragEvent->mimeData()->hasFormat(MimeType) &&
                    !errorMessage.isEmpty())
                {
                    showRejection(dragEvent->position().toPoint(), errorMessage);
                }
                dragEvent->ignore();
            }
            return true;
        }
        if (event->type() == QEvent::DragMove)
        {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (canAccept(dragEvent->mimeData()))
            {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->accept();
            }
            else
            {
                dragEvent->ignore();
            }
            return true;
        }
        if (event->type() != QEvent::Drop)
            return QObject::eventFilter(watched, event);

        auto* dropEvent = static_cast<QDropEvent*>(event);
        Payload payload;
        QString errorMessage;
        if (!decodeMimeData(dropEvent->mimeData(), payload, &errorMessage))
        {
            showRejection(dropEvent->position().toPoint(), errorMessage);
            dropEvent->ignore();
            return true;
        }

        const DropResult result = handler ? handler(payload) : DropResult{};
        if (!result.accepted)
        {
            showRejection(dropEvent->position().toPoint(),
                result.errorMessage.isEmpty()
                    ? translated("该资源不能用于此字段。")
                    : result.errorMessage);
            dropEvent->ignore();
            return true;
        }

        if (lineEdit->text() != result.resourceReference)
            lineEdit->setText(result.resourceReference);
        dropEvent->setDropAction(Qt::CopyAction);
        dropEvent->accept();
        return true;
    }

private:
    bool canAccept(const QMimeData* mimeData,
                   QString* errorMessage = nullptr) const
    {
        Payload payload;
        QString decodeError;
        if (!decodeMimeData(mimeData, payload, &decodeError))
        {
            if (errorMessage)
                *errorMessage = decodeError;
            return false;
        }
        const DropResult result = handler ? handler(payload) : DropResult{};
        if (!result.accepted && errorMessage)
        {
            *errorMessage = result.errorMessage.isEmpty()
                ? translated("该资源不能用于此字段。")
                : result.errorMessage;
        }
        return result.accepted;
    }

    void showRejection(const QPoint& localPosition,
                       const QString& message) const
    {
        QToolTip::showText(lineEdit->mapToGlobal(localPosition), message,
                           lineEdit);
    }

    QLineEdit* lineEdit = nullptr;
    DropHandler handler;
};
}

QString mimeType()
{
    return MimeType;
}

QMimeData* createMimeData(const QString& sourceRoot,
                          const QString& relativePath)
{
    const QString normalizedRoot =
        EditorAssetPath::normalizedAbsolutePath(sourceRoot);
    QString normalizedRelative;
    QString absolutePath;
    if (sourceRoot.trimmed().isEmpty() || !QDir(normalizedRoot).exists() ||
        !EditorAssetPath::normalizeResourcePath(relativePath,
                                                normalizedRelative) ||
        !EditorAssetPath::resolveLogicalResourcePath(
            normalizedRoot, normalizedRelative, absolutePath))
    {
        return nullptr;
    }

    const QFileInfo fileInfo(absolutePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
        return nullptr;

    QJsonObject object;
    object.insert(QStringLiteral("version"), PayloadVersion);
    object.insert(QStringLiteral("sourceRoot"), normalizedRoot);
    object.insert(QStringLiteral("relativePath"), normalizedRelative);

    auto* mimeData = new QMimeData;
    mimeData->setData(MimeType,
        QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mimeData;
}

bool decodeMimeData(const QMimeData* mimeData, Payload& payload,
                    QString* errorMessage)
{
    payload = {};
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!mimeData || !mimeData->hasFormat(MimeType))
        return fail(translated("拖放数据不是编辑器资源引用。"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        mimeData->data(MimeType), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject())
    {
        return fail(translated("拖放资源数据格式无效。"));
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt(-1) != PayloadVersion)
        return fail(translated("拖放资源数据版本不受支持。"));

    const QString sourceRoot =
        object.value(QStringLiteral("sourceRoot")).toString();
    const QString relativePath =
        object.value(QStringLiteral("relativePath")).toString();
    if (sourceRoot.trimmed().isEmpty() || !QDir(sourceRoot).exists())
        return fail(translated("拖放资源的来源目录无效。"));

    QString normalizedRelative;
    QString absolutePath;
    if (!EditorAssetPath::normalizeResourcePath(relativePath,
                                                normalizedRelative) ||
        !EditorAssetPath::resolveLogicalResourcePath(
            sourceRoot, normalizedRelative, absolutePath))
    {
        return fail(translated("拖放资源已越过来源目录。"));
    }

    const QFileInfo fileInfo(absolutePath);
    if (!fileInfo.exists() || !fileInfo.isFile())
        return fail(translated("拖放资源不存在或不是普通文件。"));

    payload.sourceRoot = EditorAssetPath::normalizedAbsolutePath(sourceRoot);
    payload.relativePath = normalizedRelative;
    payload.absolutePath = absolutePath;
    if (errorMessage)
        errorMessage->clear();
    return true;
}

void installLineEditTarget(QLineEdit* lineEdit, DropHandler handler)
{
    if (!lineEdit)
        return;
    lineEdit->setAcceptDrops(true);
    lineEdit->installEventFilter(
        new LineEditDropTarget(lineEdit, std::move(handler)));
}

}
