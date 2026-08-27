#include "SaveEditContext.h"

#include "EditorSettings.h"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

namespace
{
constexpr char SettingsKey[] = "saveEditContext/target";
constexpr char DefaultTargetId[] = "new_game";
}

SaveEditContext& SaveEditContext::instance()
{
    static SaveEditContext context;
    return context;
}

SaveEditContext::SaveEditContext()
{
    availableTargets.append({"new_game", QString(), "ini/save", false});
    for (int slot = 1; slot <= 7; slot++)
    {
        availableTargets.append({
            QString("slot_%1").arg(slot),
            QString(),
            QString("save/rpg%1").arg(slot),
            false
        });
    }
    availableTargets.append({
        "temporary",
        QString(),
        "save/game",
        true
    });
    refreshDisplayNames();
    reload();
}

const QList<SaveEditTarget>& SaveEditContext::targets() const
{
    refreshDisplayNames();
    return availableTargets;
}

int SaveEditContext::currentIndex() const
{
    return selectedIndex;
}

SaveEditTarget SaveEditContext::currentTarget() const
{
    refreshDisplayNames();
    return availableTargets.value(selectedIndex, availableTargets.first());
}

bool SaveEditContext::setCurrentIndex(int index)
{
    if (index < 0 || index >= availableTargets.size())
        return false;

    selectedIndex = index;
    QSettings settings = EditorSettings::create();
    settings.setValue(SettingsKey, currentTarget().id);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool SaveEditContext::setCurrentId(const QString& id)
{
    int index = findTargetIndex(id);
    return index >= 0 && setCurrentIndex(index);
}

QString SaveEditContext::resolveFilePath(
    const QString& assetsBasePath,
    const QString& fileName,
    QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();

    QFileInfo baseInfo(assetsBasePath);
    if (assetsBasePath.trimmed().isEmpty() || !baseInfo.exists() || !baseInfo.isDir())
    {
        if (errorMessage)
        {
            *errorMessage = QCoreApplication::translate(
                "SaveEditContext", "素材根目录无效，无法解析存档编辑路径：%1")
                .arg(assetsBasePath.isEmpty() ? QCoreApplication::translate("SaveEditContext", "（未设置）")
                                              : assetsBasePath);
        }
        return QString();
    }

    QString normalizedFileName = fileName;
    normalizedFileName.replace('\\', '/');
    QString cleanFileName = QDir::cleanPath(normalizedFileName);
    if (cleanFileName.isEmpty() || cleanFileName == "." ||
        QDir::isAbsolutePath(cleanFileName) || cleanFileName.startsWith("../") ||
        cleanFileName.contains("/../"))
    {
        if (errorMessage)
            *errorMessage = QCoreApplication::translate(
                "SaveEditContext", "存档文件名无效：%1").arg(fileName);
        return QString();
    }

    QDir baseDirectory(baseInfo.absoluteFilePath());
    QString relativePath = currentTarget().relativeDirectory + "/" + cleanFileName;
    return QDir::cleanPath(baseDirectory.absoluteFilePath(relativePath));
}

void SaveEditContext::reload()
{
    QSettings settings = EditorSettings::create();
    QString targetId = settings.value(SettingsKey, DefaultTargetId).toString();
    int index = findTargetIndex(targetId);
    selectedIndex = index >= 0 ? index : 0;
}

int SaveEditContext::findTargetIndex(const QString& id) const
{
    for (int index = 0; index < availableTargets.size(); index++)
    {
        if (availableTargets[index].id == id)
            return index;
    }
    return -1;
}

void SaveEditContext::refreshDisplayNames() const
{
    if (availableTargets.size() != 9)
        return;

    availableTargets[0].displayName = QCoreApplication::translate(
        "SaveEditContext", "新游戏模板（ini/save）");
    for (int slot = 1; slot <= 7; slot++)
    {
        availableTargets[slot].displayName = QCoreApplication::translate(
            "SaveEditContext", "存档槽 %1（save/rpg%1）").arg(slot);
    }
    availableTargets[8].displayName = QCoreApplication::translate(
        "SaveEditContext", "临时运行存档（save/game，高风险）");
}
