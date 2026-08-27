#include "BuildVersion.h"

#include "JxqyEditorBuildVersion.h"
#include "JxqyEngineVersion.h"

namespace BuildVersion
{
QString editorProductVersion()
{
    return QString::fromLatin1(JXQY_EDITOR_PRODUCT_VERSION_STRING);
}

QString engineVersion()
{
    return QString::fromLatin1(JxqyBuildVersion::EngineVersion);
}
}
