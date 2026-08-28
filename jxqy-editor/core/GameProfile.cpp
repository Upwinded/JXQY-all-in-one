#include "GameProfile.h"
#include "AuthoringMutationGate.h"
#include "DurableFileTransaction.h"
#include "EditorAssetPath.h"
#include "INIFileEditor.h"
#include "../../src/Resource/ResourceCatalog.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QFile>
#include <QStringList>
#include <QSet>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr char ManifestFileName[] = "game_profile.ini";

void setOrRemove(INIFileEditor& ini, const char* section, const char* key, const QString& value)
{
    if (value.isEmpty())
    {
        ini.removeKey(section, key);
    }
    else
    {
        ini.set(section, key, value.toUtf8().toStdString());
    }
}

void setOrRemove(
    INIFileEditor& ini,
    const char* section,
    const char* key,
    const std::string& value)
{
    if (value.empty())
    {
        ini.removeKey(section, key);
    }
    else
    {
        ini.set(section, key, value);
    }
}

GameProfile gameProfileFromManifest(
    const ResourceManifest& manifest,
    const QString& rootPath,
    const QString& manifestPath)
{
    GameProfile profile;
    profile.rootPath = rootPath;
    profile.manifestPath = manifestPath;
    profile.id = QString::fromStdString(manifest.id);
    profile.name = QString::fromStdString(manifest.name);
    profile.author = QString::fromStdString(manifest.author);
    profile.releaseMetadata = manifest.releaseMetadata;
    profile.type = manifest.type;
    profile.typeDefined = manifest.typeDefined;
    profile.useWav = manifest.useWav;
    profile.defeatedNpcExperienceMode =
        manifest.defeatedNpcExperienceMode;
    profile.defeatedNpcExperienceModeDefined =
        manifest.defeatedNpcExperienceModeDefined;
    profile.experienceMultiplier = manifest.experienceMultiplier;
    profile.experienceMultiplierDefined =
        manifest.experienceMultiplierDefined;
    profile.levelUpThresholdMode = manifest.levelUpThresholdMode;
    profile.levelUpThresholdModeDefined =
        manifest.levelUpThresholdModeDefined;
    profile.partnerFollowRadius = manifest.partnerFollowRadius;
    profile.partnerFollowRadiusDefined =
        manifest.partnerFollowRadiusDefined;
    profile.partnerFollowRunRadius = manifest.partnerFollowRunRadius;
    profile.partnerFollowRunRadiusDefined =
        manifest.partnerFollowRunRadiusDefined;
    profile.minimumMagicDamage = manifest.minimumMagicDamage;
    profile.minimumMagicDamageDefined =
        manifest.minimumMagicDamageDefined;
    profile.magicEffectCalculationMode =
        manifest.magicEffectCalculationMode;
    profile.magicEffectCalculationModeDefined =
        manifest.magicEffectCalculationModeDefined;
    profile.npcActionProfile = manifest.npcActionProfile;
    profile.npcActionProfileDefined = manifest.npcActionProfileDefined;
    profile.npcRuntimeProfile = manifest.npcRuntimeProfile;
    profile.npcRuntimeProfileDefined = manifest.npcRuntimeProfileDefined;
    profile.specialActionMode = manifest.specialActionMode;
    profile.specialActionModeDefined = manifest.specialActionModeDefined;
    profile.addLifeMode = manifest.addLifeMode;
    profile.addLifeModeDefined = manifest.addLifeModeDefined;
    profile.levelUpMessage =
        QString::fromStdString(manifest.levelUpMessage);
    for (const std::string& effect : manifest.levelUpRandomEffects)
    {
        profile.levelUpRandomEffects.append(
            QString::fromStdString(effect));
    }
    profile.levelUpMaleEffect =
        QString::fromStdString(manifest.levelUpMaleEffect);
    profile.levelUpFemaleEffect =
        QString::fromStdString(manifest.levelUpFemaleEffect);
    profile.dependencyId =
        QString::fromStdString(manifest.dependencyId);
    profile.resourceOnly = manifest.resourceOnly;
    profile.textEncodingConverted =
        manifest.textEncodingConverted;
    profile.uiBaseId = QString::fromStdString(manifest.uiBaseId);
    profile.uiProfile = QString::fromStdString(manifest.uiProfile);
    profile.preferLocalUi = manifest.preferLocalUi;
    for (const auto& feature : manifest.features)
    {
        profile.features.insert(
            QString::fromStdString(feature.first),
            feature.second);
    }
    profile.saveNamespace =
        QString::fromStdString(manifest.saveNamespace);
    for (const std::string& video : manifest.startupVideos)
    {
        profile.startupVideos.append(QString::fromStdString(video));
    }
    profile.titleMenu = QString::fromStdString(manifest.titleMenu);
    profile.titleNewYearMenu =
        QString::fromStdString(manifest.titleNewYearMenu);
    profile.titleMusic = QString::fromStdString(manifest.titleMusic);
    profile.titleTeamVideo =
        QString::fromStdString(manifest.titleTeamVideo);
    profile.teamInfoFile =
        QString::fromStdString(manifest.teamInfoFile);
    profile.newGameScript =
        QString::fromStdString(manifest.newGameScript);
    return profile;
}
}

bool GameProfile::loadFromFile(const QString& filePath)
{
    INIFileEditor ini;
    if (!ini.loadFromFile(filePath.toStdString()))
    {
        return false;
    }

    const std::string manifestText = ini.saveToString();
    if (manifestText.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    ResourceManifest manifest;
    if (!manifest.loadFromBuffer(
            manifestText.data(),
            static_cast<int>(manifestText.size())))
    {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    GameProfile loadedProfile = gameProfileFromManifest(
        manifest,
        fileInfo.absolutePath(),
        filePath);

    // Runtime loading may decode a historical manifest encoding. Preserve the
    // exact release-field bytes so an untouched invalid UTF-8 value remains
    // blocked by the editor instead of being silently rewritten.
    loadedProfile.releaseMetadata.displayVersion =
        ini.get("Game", "Version", "");
    loadedProfile.releaseMetadata.releaseDate =
        ini.get("Release", "Date", "");
    loadedProfile.releaseMetadata.minimumEngineVersion =
        ini.get("Release", "MinimumEngineVersion", "");
    loadedProfile.releaseMetadata.coverPath =
        ini.get("Release", "Cover", "");
    loadedProfile.releaseMetadata.descriptionFilePath =
        ini.get("Release", "DescriptionFile", "");
    loadedProfile.releaseMetadata.installedArtifactCrc32 =
        ini.get("Release", "InstalledArtifactCrc32", "");
    loadedProfile.releaseMetadata.installedIncrementalArtifactCrc32 =
        ini.get("Release", "InstalledIncrementalArtifactCrc32", "");

    // ResourceManifest normalizes feature names for runtime lookups. Keep the
    // source spelling in the editor UI.
    loadedProfile.features.clear();
    for (const std::string& featureName : ini.getKeyNames("Features"))
    {
        loadedProfile.features.insert(
            QString::fromStdString(featureName),
            ini.getBoolean("Features", featureName, false));
    }

    *this = std::move(loadedProfile);
    return true;
}

bool GameProfile::saveToFile(const QString& filePath) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(filePath);
    if (!mutationLease)
        return false;

    QByteArray bytes;
    if (!prepareSaveBytes(filePath, bytes))
    {
        return false;
    }

    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly))
    {
        return false;
    }
    return output.write(bytes) == bytes.size() && output.commit();
}

bool GameProfile::prepareSaveBytes(
    const QString& filePath,
    QByteArray& bytes,
    QByteArray* sourceBytes,
    bool* sourceExists) const
{
    bytes.clear();
    if (sourceBytes)
    {
        sourceBytes->clear();
    }
    if (sourceExists)
    {
        *sourceExists = false;
    }
    INIFileEditor ini;
    QString sourcePath = manifestPath;
    if (!sourcePath.isEmpty() && !QFileInfo::exists(sourcePath))
    {
        return false;
    }
    if (sourcePath.isEmpty() && QFileInfo::exists(filePath))
    {
        sourcePath = filePath;
    }
    if (!sourcePath.isEmpty() && QFileInfo::exists(sourcePath))
    {
        QFile sourceFile(sourcePath);
        if (!QFileInfo(sourcePath).isFile() ||
            !sourceFile.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const QByteArray originalBytes = sourceFile.readAll();
        if (sourceFile.error() != QFileDevice::NoError)
        {
            return false;
        }
        const std::string originalText(
            originalBytes.constData(),
            static_cast<std::size_t>(originalBytes.size()));
        if (!ini.loadFromString(originalText))
        {
            return false;
        }
        if (sourceBytes)
        {
            *sourceBytes = originalBytes;
        }
        if (sourceExists)
        {
            *sourceExists = true;
        }
    }

    ini.set("Game", "Id", id.toUtf8().toStdString());
    ini.set("Game", "Name", name.toUtf8().toStdString());
    setOrRemove(ini, "Game", "Author", author.trimmed());
    setOrRemove(
        ini, "Game", "Version", releaseMetadata.displayVersion);
    if (typeDefined)
    {
        ini.setInteger("Game", "Type", type);
    }
    else
    {
        ini.removeKey("Game", "Type");
    }
    ini.setBoolean("Game", "UseWav", useWav);

    if (defeatedNpcExperienceModeDefined)
    {
        ini.set(
            "Experience",
            "DefeatedNpcExperienceMode",
            defeatedNpcExperienceMode ==
                    DefeatedNpcExperienceMode::StoredExperience
                ? "StoredExperience"
                : "LevelProductWithBonus");
    }
    else
    {
        ini.removeKey("Experience", "DefeatedNpcExperienceMode");
    }
    if (experienceMultiplierDefined)
    {
        ini.set(
            "Experience",
            "ExperienceMultiplier",
            QString::number(experienceMultiplier, 'g', 15).
                toStdString());
    }
    else
    {
        ini.removeKey("Experience", "ExperienceMultiplier");
    }
    if (levelUpThresholdModeDefined)
    {
        ini.set(
            "Experience",
            "LevelUpThresholdMode",
            levelUpThresholdMode == LevelUpThresholdMode::GreaterThan
                ? "GreaterThan"
                : "GreaterThanOrEqual");
    }
    else
    {
        ini.removeKey("Experience", "LevelUpThresholdMode");
    }
    if (partnerFollowRadiusDefined)
    {
        ini.setInteger(
            "Gameplay", "PartnerFollowRadius", partnerFollowRadius);
    }
    else
    {
        ini.removeKey("Gameplay", "PartnerFollowRadius");
    }
    if (partnerFollowRunRadiusDefined)
    {
        ini.setInteger(
            "Gameplay", "PartnerFollowRunRadius", partnerFollowRunRadius);
    }
    else
    {
        ini.removeKey("Gameplay", "PartnerFollowRunRadius");
    }
    if (minimumMagicDamageDefined)
    {
        ini.setInteger(
            "Combat", "MinimumMagicDamage", minimumMagicDamage);
    }
    else
    {
        ini.removeKey("Combat", "MinimumMagicDamage");
    }
    if (magicEffectCalculationModeDefined)
    {
        ini.set(
            "Combat",
            "MagicEffectCalculationMode",
            magicEffectCalculationMode ==
                    MagicEffectCalculationMode::AddToAttack
                ? "AddToAttack"
                : "ReplaceAttack");
    }
    else
    {
        ini.removeKey("Combat", "MagicEffectCalculationMode");
    }
    if (npcActionProfileDefined)
    {
        const char* value = "Legacy";
        if (npcActionProfile == ScriptNpcActionProfile::Yycs)
        {
            value = "YYCS";
        }
        else if (npcActionProfile == ScriptNpcActionProfile::Xjxqy)
        {
            value = "XJXQY";
        }
        ini.set("Script", "NpcActionProfile", value);
    }
    else
    {
        ini.removeKey("Script", "NpcActionProfile");
    }
    if (npcRuntimeProfileDefined)
    {
        ini.set(
            "Script",
            "NpcRuntimeProfile",
            npcRuntimeProfile == ScriptNpcRuntimeProfile::Trilogy
                ? "Trilogy"
                : "Legacy");
    }
    else
    {
        ini.removeKey("Script", "NpcRuntimeProfile");
    }
    if (specialActionModeDefined)
    {
        ini.set(
            "Script",
            "SpecialActionMode",
            specialActionMode == ScriptSpecialActionMode::Overlay
                ? "Overlay"
                : "Replace");
    }
    else
    {
        ini.removeKey("Script", "SpecialActionMode");
    }
    if (addLifeModeDefined)
    {
        ini.set(
            "Script",
            "AddLifeMode",
            addLifeMode == ScriptAddLifeMode::DirectClamp
                ? "DirectClamp"
                : "PlayerRules");
    }
    else
    {
        ini.removeKey("Script", "AddLifeMode");
    }
    ini.set(
        "LevelUp",
        "Message",
        levelUpMessage.toUtf8().toStdString());
    setOrRemove(
        ini,
        "LevelUp",
        "RandomEffects",
        levelUpRandomEffects.join(','));
    setOrRemove(
        ini, "LevelUp", "MaleEffect", levelUpMaleEffect);
    setOrRemove(
        ini, "LevelUp", "FemaleEffect", levelUpFemaleEffect);
    setOrRemove(ini, "Resource", "DependencyId", dependencyId);
    ini.removeKey("Resource", "DependencyPath");
    ini.removeKey("Resource", "CommonPath");
    if (resourceOnly)
        ini.setBoolean("Resource", "ResourceOnly", true);
    else
        ini.removeKey("Resource", "ResourceOnly");
    ini.setBoolean(
        "Resource", "TextEncodingConverted", textEncodingConverted);

    setOrRemove(ini, "UI", "BaseId", uiBaseId);
    setOrRemove(ini, "UI", "Profile", uiProfile);
    if (!uiBaseId.isEmpty() || !uiProfile.isEmpty() || !preferLocalUi || ini.hasKey("UI", "PreferLocal"))
    {
        ini.setBoolean("UI", "PreferLocal", preferLocalUi);
    }

    QSet<QString> currentFeatureNames;
    for (auto feature = features.cbegin(); feature != features.cend(); ++feature)
    {
        currentFeatureNames.insert(feature.key().toLower());
    }
    for (const std::string& existingFeature : ini.getKeyNames("Features"))
    {
        if (!currentFeatureNames.contains(QString::fromStdString(existingFeature).toLower()))
        {
            ini.removeKey("Features", existingFeature);
        }
    }
    for (auto feature = features.cbegin(); feature != features.cend(); ++feature)
    {
        ini.setBoolean("Features", feature.key().toUtf8().toStdString(), feature.value());
    }

    setOrRemove(ini, "Save", "Namespace", saveNamespace);
    setOrRemove(
        ini, "Release", "Date", releaseMetadata.releaseDate);
    ini.removeKey("Release", "PackageId");
    ini.removeKey("Release", "PackageVersion");
    setOrRemove(
        ini,
        "Release",
        "MinimumEngineVersion",
        releaseMetadata.minimumEngineVersion);
    setOrRemove(
        ini, "Release", "Cover", releaseMetadata.coverPath);
    setOrRemove(
        ini,
        "Release",
        "DescriptionFile",
        releaseMetadata.descriptionFilePath);
    setOrRemove(
        ini,
        "Release",
        "InstalledArtifactCrc32",
        releaseMetadata.installedArtifactCrc32);
    setOrRemove(
        ini,
        "Release",
        "InstalledIncrementalArtifactCrc32",
        releaseMetadata.installedIncrementalArtifactCrc32);
    ini.set("Startup", "Videos", videosToString().toUtf8().toStdString());
    ini.set("Title", "Menu", titleMenu.toUtf8().toStdString());
    setOrRemove(ini, "Title", "NewYearMenu", titleNewYearMenu);
    ini.set("Title", "Music", titleMusic.toUtf8().toStdString());
    ini.set("Title", "TeamVideo", titleTeamVideo.toUtf8().toStdString());
    setOrRemove(ini, "Team", "InfoFile", teamInfoFile);
    ini.set("NewGame", "Script", newGameScript.toUtf8().toStdString());

    const std::string content = ini.saveToString();
    bytes = QByteArray(
        content.data(), static_cast<qsizetype>(content.size()));
    return true;
}

bool GameProfile::save() const
{
    if (manifestPath.isEmpty())
    {
        return false;
    }
    return saveToFile(manifestPath);
}

GameProfile GameProfile::createDefault()
{
    GameProfile profile;
    profile.id = "JXQY2";
    profile.name = QString::fromUtf8("剑侠情缘二");
    profile.author = "";
    profile.releaseMetadata = {};
    profile.type = 0;
    profile.typeDefined = true;
    profile.useWav = false;
    profile.defeatedNpcExperienceMode =
        DefeatedNpcExperienceMode::StoredExperience;
    profile.defeatedNpcExperienceModeDefined = true;
    profile.experienceMultiplier = 3.0;
    profile.experienceMultiplierDefined = true;
    profile.levelUpThresholdMode =
        LevelUpThresholdMode::GreaterThanOrEqual;
    profile.levelUpThresholdModeDefined = true;
    profile.partnerFollowRadius = 1;
    profile.partnerFollowRadiusDefined = true;
    profile.partnerFollowRunRadius = 5;
    profile.partnerFollowRunRadiusDefined = true;
    profile.minimumMagicDamage = 10;
    profile.minimumMagicDamageDefined = true;
    profile.magicEffectCalculationMode =
        MagicEffectCalculationMode::ReplaceAttack;
    profile.magicEffectCalculationModeDefined = true;
    profile.npcActionProfile = ScriptNpcActionProfile::Legacy;
    profile.npcActionProfileDefined = true;
    profile.npcRuntimeProfile = ScriptNpcRuntimeProfile::Legacy;
    profile.npcRuntimeProfileDefined = true;
    profile.specialActionMode = ScriptSpecialActionMode::Replace;
    profile.specialActionModeDefined = true;
    profile.addLifeMode = ScriptAddLifeMode::PlayerRules;
    profile.addLifeModeDefined = true;
    profile.levelUpMessage =
        QString::fromUtf8("{name}的等级得到提升！");
    profile.levelUpRandomEffects.clear();
    profile.levelUpMaleEffect.clear();
    profile.levelUpFemaleEffect.clear();
    profile.dependencyId = "";
    profile.resourceOnly = false;
    profile.textEncodingConverted = true;
    profile.uiBaseId = "";
    profile.uiProfile = "JXQY2";
    profile.preferLocalUi = true;
    profile.features.clear();
    profile.features.insert("MagicTriggerAtAnimationEnd", false);
    profile.features.insert("LumAsBrightness", true);
    profile.features.insert("AmbientLumOverlay", true);
    profile.features.insert("RainSceneTint", false);
    profile.saveNamespace = "";
    profile.startupVideos = QStringList{ "logo.avi", "title.avi" };
    profile.titleMenu = "ini\\ui\\title\\title.menu.ini";
    profile.titleNewYearMenu = "";
    profile.titleMusic = "ks64.mp3";
    profile.titleTeamVideo = "team.avi";
    profile.teamInfoFile = "";
    profile.newGameScript = "newgame.txt";
    return profile;
}

bool GameProfile::isValid() const
{
    return !id.trimmed().isEmpty();
}

QString GameProfile::videosToString() const
{
    return startupVideos.join(",");
}

bool ResourcePackScanner::hasManifest(const QString& dirPath)
{
    if (dirPath.isEmpty())
    {
        return false;
    }
    QDir dir(dirPath);
    return dir.exists(QString::fromLatin1(ManifestFileName));
}

QString ResourcePackScanner::manifestPath(const QString& dirPath)
{
    QDir dir(dirPath);
    return dir.absoluteFilePath(QString::fromLatin1(ManifestFileName));
}

namespace
{
QList<ResourcePackInfo> scanPacksUnlocked(
    const QString& assetsPath)
{
    QList<ResourcePackInfo> result;
    const QString normalizedRoot =
        EditorAssetPath::normalizedAbsolutePath(assetsPath);
    if (normalizedRoot.isEmpty())
    {
        return result;
    }

    const auto catalogResult =
        RuntimeResource::loadResourceCatalogSnapshot(
            std::filesystem::u8path(
                normalizedRoot.toUtf8().constData()));
    if (!catalogResult.succeeded())
    {
        return result;
    }

    const auto& snapshot = catalogResult.snapshot;
    for (const auto& entry : snapshot.entries)
    {
        ResourcePackInfo pack;
        pack.stableEntryKey =
            QString::fromStdString(entry.stableKey);
        pack.rootPath = EditorAssetPath::normalizedAbsolutePath(
            QString::fromStdString(entry.root.u8string()));
        pack.manifestPath = entry.manifestPath.empty()
            ? QDir(pack.rootPath).filePath(
                QString::fromLatin1(ManifestFileName))
            : EditorAssetPath::normalizedAbsolutePath(
                QString::fromStdString(
                    entry.manifestPath.u8string()));
        pack.effectiveSaveNamespace =
            QString::fromStdString(
                entry.effectiveSaveNamespace);
        pack.saveNamespaceAdjusted =
            entry.saveNamespaceAdjusted;
        pack.profile = gameProfileFromManifest(
            entry.manifest,
            pack.rootPath,
            pack.manifestPath);
        for (const auto& diagnostic : snapshot.diagnostics)
        {
            if (diagnostic.stableEntryKey.empty() ||
                QString::fromStdString(
                    diagnostic.stableEntryKey)
                    .compare(
                        pack.stableEntryKey,
                        Qt::CaseInsensitive) == 0)
            {
                pack.catalogDiagnostics.append(
                    QString::fromStdString(
                        diagnostic.message));
            }
        }
        result.append(pack);
    }
    return result;
}
}

QList<ResourcePackInfo> ResourcePackScanner::scanPacks(
    const QString& assetsPath)
{
    return scanPacksUnlocked(assetsPath);
}

QList<ResourcePackInfo> ResourcePackScanner::scanPacks(
    const QString& assetsPath,
    QStringList& recoveryErrors)
{
    recoveryErrors.clear();
    if (assetsPath.isEmpty())
    {
        return {};
    }
    const auto readLock =
        DurableFileTransaction::acquireRecoveredReadLock(
            assetsPath, recoveryErrors);
    if (!readLock)
    {
        return {};
    }
    return scanPacksUnlocked(assetsPath);
}

ResourcePackSelection ResourcePackScanner::resolveActivePack(
    const QString& assetsPath,
    const QString& requestedPackId,
    const QString& requestedPackEntryKey)
{
    ResourcePackSelection selection;
    if (assetsPath.trimmed().isEmpty())
        return selection;

    selection.collectionRoot = EditorAssetPath::normalizedAbsolutePath(assetsPath);
    const QDir collectionDirectory(selection.collectionRoot);
    if (!collectionDirectory.exists())
    {
        selection.status = ResourcePackSelectionStatus::InvalidAssetsRoot;
        return selection;
    }

    const auto readLock =
        DurableFileTransaction::acquireRecoveredReadLock(
            selection.collectionRoot, selection.recoveryErrors);
    if (!readLock)
    {
        selection.status = ResourcePackSelectionStatus::RecoveryFailed;
        return selection;
    }
    selection.availablePacks =
        scanPacksUnlocked(selection.collectionRoot);
    if (selection.availablePacks.isEmpty())
    {
        // A directory without a valid game_profile.ini remains an authoring
        // root. It is not advertised as a runnable pack, and editor-run will
        // expose only explicitly captured overlay content.
        selection.activeRoot = selection.collectionRoot;
        return selection;
    }

    int activeIndex = -1;
    const QString requestedId = requestedPackId.trimmed();
    const QString requestedEntryKey =
        requestedPackEntryKey.trimmed();
    if (requestedId.isEmpty() && requestedEntryKey.isEmpty())
    {
        if (selection.availablePacks.size() != 1)
        {
            selection.status = ResourcePackSelectionStatus::SelectionRequired;
            return selection;
        }
        activeIndex = 0;
    }
    else
    {
        if (!requestedEntryKey.isEmpty())
        {
            for (int i = 0;
                 i < selection.availablePacks.size();
                 ++i)
            {
                if (selection.availablePacks[i].stableEntryKey.compare(
                        requestedEntryKey,
                        Qt::CaseInsensitive) == 0)
                {
                    activeIndex = i;
                    break;
                }
            }
            if (activeIndex >= 0 && !requestedId.isEmpty() &&
                selection.availablePacks[activeIndex].profile.id.trimmed().compare(
                    requestedId, Qt::CaseInsensitive) != 0)
            {
                activeIndex = -1;
            }
        }

        if (activeIndex < 0 && !requestedId.isEmpty())
        {
            int matchingIdCount = 0;
            for (int i = 0;
                i < selection.availablePacks.size();
                ++i)
            {
                if (selection.availablePacks[i].profile.id.compare(
                        requestedId,
                        Qt::CaseInsensitive) == 0)
                {
                    activeIndex = i;
                    matchingIdCount++;
                }
            }
            if (matchingIdCount > 1)
            {
                selection.status =
                    ResourcePackSelectionStatus::ResourceIdConflict;
                return selection;
            }
        }

        if (activeIndex < 0)
        {
            selection.status = ResourcePackSelectionStatus::ActivePackNotFound;
            return selection;
        }
    }

    const QString activeId =
        selection.availablePacks[activeIndex].profile.id.trimmed();
    const int activeIdOwners = std::count_if(
        selection.availablePacks.cbegin(),
        selection.availablePacks.cend(),
        [&activeId](const ResourcePackInfo& pack)
        {
            return pack.profile.id.trimmed().compare(
                activeId, Qt::CaseInsensitive) == 0;
        });
    if (activeIdOwners > 1)
    {
        selection.status =
            ResourcePackSelectionStatus::ResourceIdConflict;
        return selection;
    }

    selection.activePack = selection.availablePacks[activeIndex];
    selection.activeRoot = EditorAssetPath::normalizedAbsolutePath(
        selection.activePack.rootPath);
    selection.activeResourcePackId = selection.activePack.profile.id.trimmed();
    selection.activeResourcePackEntryKey =
        selection.activePack.stableEntryKey;
    return selection;
}

namespace
{
ResourceContentRootResolution resolveContentRootsImpl(
    const QString& activeAssetsPath)
{
    ResourceContentRootResolution resolution;
    if (activeAssetsPath.trimmed().isEmpty())
        return resolution;

    const QString activeRoot = EditorAssetPath::normalizedAbsolutePath(activeAssetsPath);
    if (activeRoot.isEmpty())
        return resolution;

    QStringList recoveryErrors;
    if (!DurableFileTransaction::recoverPending(
            activeRoot, recoveryErrors))
    {
        resolution.recoveryErrors = recoveryErrors;
        return resolution;
    }

    auto rootKey = [](const QString& path)
    {
        return EditorAssetPath::logicalComparisonKey(
            EditorAssetPath::normalizedAbsolutePath(path));
    };
    QString collectionRoot = activeRoot;
    QList<ResourcePackInfo> packs;
    QString ancestor = QFileInfo(activeRoot).dir().absolutePath();
    while (!ancestor.isEmpty())
    {
        const bool hasIndex = QFileInfo::exists(
            QDir(ancestor).filePath(QStringLiteral("resources.ini")));
        const bool hasPendingTransactions = QFileInfo::exists(
            DurableFileTransaction::transactionStorePath(ancestor));
        if (hasIndex || hasPendingTransactions)
        {
            QList<ResourcePackInfo> candidatePacks =
                ResourcePackScanner::scanPacks(
                    ancestor, recoveryErrors);
            if (!recoveryErrors.isEmpty())
            {
                resolution.recoveryErrors = recoveryErrors;
                return resolution;
            }
            const bool containsActiveRoot = std::any_of(
                candidatePacks.cbegin(),
                candidatePacks.cend(),
                [&rootKey, &activeRoot](const ResourcePackInfo& pack)
                {
                    return rootKey(pack.rootPath) ==
                        rootKey(activeRoot);
                });
            if (containsActiveRoot)
            {
                collectionRoot = ancestor;
                packs = std::move(candidatePacks);
                break;
            }
        }
        const QString parent =
            QFileInfo(ancestor).dir().absolutePath();
        if (rootKey(parent) == rootKey(ancestor))
        {
            break;
        }
        ancestor = parent;
    }
    if (packs.isEmpty())
    {
        packs = ResourcePackScanner::scanPacks(
            collectionRoot, recoveryErrors);
        if (!recoveryErrors.isEmpty())
        {
            resolution.recoveryErrors = recoveryErrors;
            return resolution;
        }
    }
    resolution.collectionRoot = collectionRoot;
    resolution.availablePacks = packs;

    auto findPackByRoot = [&packs, &rootKey](const QString& root) -> int
    {
        const QString key = rootKey(root);
        for (int i = 0; i < packs.size(); ++i)
        {
            if (rootKey(packs[i].rootPath) == key)
                return i;
        }
        return -1;
    };

    const int activeIndex = findPackByRoot(activeRoot);
    if (activeIndex < 0)
    {
        const QDir activeDirectory(activeRoot);
        const bool hasDeclaredResourceConfiguration =
            QFileInfo::exists(
                activeDirectory.filePath(
                    QStringLiteral("resources.ini"))) ||
            QFileInfo::exists(
                activeDirectory.filePath(
                    QString::fromLatin1(
                        ManifestFileName)));
        if (packs.isEmpty() &&
            activeDirectory.exists() &&
            !hasDeclaredResourceConfiguration)
        {
            ResourceContentRoot localRoot;
            localRoot.rootPath = activeRoot;
            localRoot.kind =
                ResourceContentRoot::Kind::Local;
            localRoot.available = true;
            resolution.roots.append(localRoot);
        }
        return resolution;
    }

    const auto readLock =
        DurableFileTransaction::acquireRecoveredReadLock(
            collectionRoot, recoveryErrors);
    if (!readLock)
    {
        resolution.recoveryErrors = recoveryErrors;
        return resolution;
    }

    const RuntimeResource::ExactSelectionResult exact =
        RuntimeResource::resolveResourceCatalogEntrySelection(
            std::filesystem::u8path(
                collectionRoot.toUtf8().constData()),
            packs[activeIndex].stableEntryKey.toStdString());

    auto appendDiagnostic =
        [&resolution](
            const RuntimeResource::CatalogDiagnostic& diagnostic)
        {
            const QString code =
                QString::fromStdString(diagnostic.code);
            const QString message =
                QString::fromStdString(diagnostic.message);
            resolution.catalogDiagnostics.append(
                code.isEmpty()
                    ? message
                    : code + QStringLiteral(": ") + message);

            if (diagnostic.code ==
                    "resource.catalog.dependency_not_found")
            {
                const QString dependencyId =
                    QString::fromStdString(
                        diagnostic.resourcePackId);
                if (!dependencyId.isEmpty() &&
                    !resolution.missingDependencyIds.contains(
                        dependencyId, Qt::CaseInsensitive))
                {
                    resolution.missingDependencyIds.append(
                        dependencyId);
                }
            }
            if (diagnostic.code ==
                    "resource.catalog.common_root_unavailable")
            {
                const QString path =
                    EditorAssetPath::normalizedAbsolutePath(
                        QString::fromStdString(
                            diagnostic.hostPath.u8string()));
                if (!path.isEmpty() &&
                    !resolution.missingPaths.contains(path))
                {
                    resolution.missingPaths.append(path);
                }
            }
        };
    for (const auto& diagnostic : exact.diagnostics)
    {
        appendDiagnostic(diagnostic);
    }
    if (!exact.succeeded())
    {
        if (!exact.diagnosticCode.empty() ||
            !exact.message.empty())
        {
            resolution.catalogDiagnostics.append(
                QString::fromStdString(
                    exact.diagnosticCode +
                    (exact.diagnosticCode.empty() ||
                        exact.message.empty()
                        ? std::string()
                        : ": ") +
                    exact.message));
        }
        return resolution;
    }

    auto kindFor =
        [](RuntimeResource::ContentRootKind kind)
        {
            switch (kind)
            {
            case RuntimeResource::ContentRootKind::Active:
                return ResourceContentRoot::Kind::Local;
            case RuntimeResource::ContentRootKind::DependencyId:
                return ResourceContentRoot::Kind::DependencyId;
            case RuntimeResource::ContentRootKind::Common:
                return ResourceContentRoot::Kind::Common;
            }
            return ResourceContentRoot::Kind::DependencyId;
        };

    for (const RuntimeResource::ContentRoot& contentRoot :
        exact.selection.orderedContentRoots)
    {
        ResourceContentRoot root;
        root.rootPath =
            EditorAssetPath::normalizedAbsolutePath(
                QString::fromStdString(
                    contentRoot.root.u8string()));
        root.id =
            QString::fromStdString(
                contentRoot.resourcePackId);
        root.kind = kindFor(contentRoot.kind);
        root.available = QDir(root.rootPath).exists();
        const int packIndex = findPackByRoot(root.rootPath);
        if (packIndex >= 0)
        {
            root.name = packs[packIndex].profile.name;
            if (root.id.isEmpty())
            {
                root.id = packs[packIndex].profile.id;
            }
        }
        resolution.roots.append(root);
    }

    return resolution;
}
}

ResourceContentRootResolution ResourcePackScanner::resolveContentRoots(
    const QString& activeAssetsPath)
{
    return resolveContentRootsImpl(activeAssetsPath);
}
