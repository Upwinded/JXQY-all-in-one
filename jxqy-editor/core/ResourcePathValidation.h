#pragma once

#include "../../src/File/ResourcePathSafety.h"

#include <QByteArray>
#include <QDir>
#include <QString>

#include <string>

namespace EditorResourcePath
{
// Editor manifests may omit an optional resource path. Non-empty values must
// remain relative and satisfy the same virtual-path rules as the runtime.
inline bool isSafeOptionalRelativeResourcePath(const QString& path)
{
    if (path.isEmpty())
        return true;

    if (path.startsWith('/') || path.startsWith('\\') ||
        QDir::isAbsolutePath(path))
    {
        return false;
    }

    const QByteArray utf8 = path.toUtf8();
    if (QString::fromUtf8(utf8.constData(), utf8.size()) != path)
        return false;

    return ResourcePathSafety::isSafeVirtualResourcePath(
        std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}
}
