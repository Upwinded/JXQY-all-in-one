#include "MenuPageContext.h"
#include "EditorAssetPath.h"
#include "GameProfile.h"
#include "INIFileEditor.h"
#include "../../src/Resource/ResourceCatalog.h"
#include "../../src/File/ResourcePathSafety.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <algorithm>

MenuPageContext MenuPageContext::resolve(const QString& activePackRoot)
{
    MenuPageContext result;
    if (activePackRoot.isEmpty()) return result;
    result.activeRoot = EditorAssetPath::normalizedAbsolutePath(activePackRoot);
    const auto roots = ResourcePackScanner::resolveContentRoots(result.activeRoot);
    result.recoveryErrors = roots.recoveryErrors;
    result.collectionRoot = roots.collectionRoot;
    const auto append = [&result](const QString& root)
    {
        if (root.isEmpty()) return;
        const QString normalized = EditorAssetPath::normalizedAbsolutePath(root);
        for (const auto& existing : result.readRoots)
            if (EditorAssetPath::logicalComparisonKey(existing) == EditorAssetPath::logicalComparisonKey(normalized)) return;
        result.readRoots.append(normalized);
    };
    for (const auto& pack : roots.availablePacks)
    {
        result.providers[EditorAssetPath::logicalComparisonKey(pack.rootPath)] = pack.profile.id;
        if (EditorAssetPath::logicalComparisonKey(pack.rootPath) != EditorAssetPath::logicalComparisonKey(result.activeRoot)) continue;
        const auto exact = RuntimeResource::resolveResourceCatalogEntrySelection(
            std::filesystem::u8path(roots.collectionRoot.toUtf8().constData()), pack.stableEntryKey.toStdString());
        if (!exact.succeeded())
        {
            result.problem = QString::fromStdString(exact.message);
            break;
        }
        result.manifest = exact.selection.activeManifest;
        for (const auto& content : exact.selection.orderedContentRoots)
            result.contentRoots.append(QString::fromStdString(content.root.u8string()));
        result.valid = ResourcePackScanner::hasManifest(result.activeRoot) && !result.manifest.resourceOnly;
        if (exact.selection.preferLocalUi) append(result.activeRoot);
        for (const auto& fallback : exact.selection.orderedUiFallbackRoots) append(QString::fromStdString(fallback.u8string()));
        if (!exact.selection.preferLocalUi) append(result.activeRoot);
        if (!exact.selection.commonResourceRoot.empty())
        {
            const QString common = QString::fromStdString(exact.selection.commonResourceRoot.u8string());
            append(common);
            result.providers[EditorAssetPath::logicalComparisonKey(common)] = "common";
        }
    }
    if (result.readRoots.isEmpty()) append(result.activeRoot);
    if (result.contentRoots.isEmpty()) result.contentRoots = result.readRoots;
    if (!result.recoveryErrors.isEmpty()) result.valid = false;
    const QString info = QString::fromStdString(result.manifest.teamInfoFile);
    if (result.valid && !info.isEmpty())
    {
        EditorAssetPath::resolveLogicalResourcePath(result.activeRoot, info, result.teamInfoPath);
        QFile file(result.teamInfoPath);
        QByteArray bytes;
        if (file.open(QIODevice::ReadOnly) && file.size() <= 64 * 1024) bytes = file.readAll();
        if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
        const bool visible = std::any_of(bytes.cbegin(), bytes.cend(), [](unsigned char value) { return value > 0x20; });
        if (!bytes.contains('\0') && visible && ResourcePathSafety::isValidUtf8(bytes.toStdString()))
            result.teamInfoText = QString::fromUtf8(bytes);
        else result.teamContentProblem = QCoreApplication::translate("MenuPageContext", "本包制作名单正文缺失、超过64KiB、为空或不是有效UTF-8：%1").arg(info);
    }
    const QString video = QString::fromStdString(result.manifest.titleTeamVideo);
    if (result.valid && !video.isEmpty())
    {
        QString path;
        if (EditorAssetPath::resolveLogicalResourcePath(result.activeRoot, "video/" + video, path) && QFileInfo(path).isFile())
            result.teamVideoPath = path;
        else if (result.teamInfoText.isEmpty() && result.teamContentProblem.isEmpty())
            result.teamContentProblem = QCoreApplication::translate("MenuPageContext", "本包制作名单视频缺失：%1").arg(video);
    }
    return result;
}

QStringList MenuPageContext::pages()
{
    return {"goods", "equip", "state", "tooltip", "title", "titleteam", "system", "option", "saveload", "buysell", "yesno", "partnerhead", "magic", "xiulian", "memo", "dialog", "choose", "message", "timer", "systemnotice", "scriptmessages", "top", "bottom", "column", "mapthumbnail", "npcinfo", "joystick", "skills", "controllerhelp", "dicegame"};
}

QStringList MenuPageContext::menuPages(const QString& theme) const
{
    QStringList result;
    if (!valid) return result;
    for (const auto& page : pages())
    {
        if (page == "top" && !hudTopButtons(theme)) continue;
        if (page == "column" && hudColumnHost(theme) == "generic-submenu") continue;
        if (page == "titleteam" && manifest.teamInfoFile.empty() && manifest.titleTeamVideo.empty()) continue;
        if ((page == "xiulian" && !practiceAvailable()) || (page == "partnerhead" && !partnerHeadsAvailable())) continue;
        // Missing expected configurations remain visible; aliases share one actual menu.
        if ((page == "state" || page == "magic") && resource(page, theme) == resource("equip", theme)) continue;
        result.append(page);
    }
    return result;
}

QString MenuPageContext::menuPageForRole(const QString& page, const QString& theme) const
{
    if (!pages().contains(page) || (page == "xiulian" && !practiceAvailable())
        || (page == "partnerhead" && !partnerHeadsAvailable())) return {};
    if (!valid) return {};
    if ((page == "top" && !hudTopButtons(theme)) || (page == "column" && hudColumnHost(theme) == "generic-submenu")) return {};
    if (page == "titleteam" && manifest.teamInfoFile.empty() && manifest.titleTeamVideo.empty()) return {};
    if ((page == "state" || page == "magic") && resource(page, theme) == resource("equip", theme)) return "equip";
    return page;
}

QString MenuPageContext::readable(const QString& resource) const
{
    for (const QString& root : rootsFor(resource))
    {
        QString file;
        if (EditorAssetPath::resolveLogicalResourcePath(root, resource, file) && QFileInfo(file).isFile()) return file;
    }
    return {};
}

bool MenuPageContext::isUiResource(const QString& resource)
{
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(resource, normalized)) return false;
    normalized = normalized.toLower();
    // File.cpp::isUiResourcePath selects these domains before applying UI priority.
    for (const QString& prefix : {"ini/ui", "asf/ui", "mpc/ui", "bmp/ui", "image/ui", "sound/ui"})
        if (normalized == prefix || normalized.startsWith(prefix + "/")) return true;
    return false;
}

bool MenuPageContext::hasComponentData(const QString& resource) const
{
    QFile file(readable(resource));
    return file.open(QIODevice::ReadOnly) && !file.read(1).isEmpty();
}

const QStringList& MenuPageContext::rootsFor(const QString& resource) const
{
    return isUiResource(resource) ? readRoots : contentRoots;
}

QStringList MenuPageContext::allReadRoots() const
{
    QStringList result;
    for (const QString& root : readRoots + contentRoots)
    {
        bool present = false;
        for (const QString& existing : result)
            present |= EditorAssetPath::logicalComparisonKey(existing) == EditorAssetPath::logicalComparisonKey(root);
        if (!present) result.append(root);
    }
    return result;
}

bool MenuPageContext::qingyu(const QString& theme) const
{
    return theme == "qingyu" && !readable("ini/ui/qingyu/theme.ini").isEmpty()
        && !readable("asf/ui/qingyu/panel.png").isEmpty();
}

bool MenuPageContext::hudTopButtons(const QString& theme) const
{
    // Global::loadFeatureFlags closes integrated layouts over topButtonsLayout before applying Qingyu.
    return qingyu(theme) || manifest.isFeatureEnabled("stateequipintegratedlayout", manifest.uiProfile == "XJXQY")
        || manifest.isFeatureEnabled("topbuttonslayout", manifest.uiProfile == "YYCS" || manifest.uiProfile == "XJXQY");
}

QString MenuPageContext::hudColumnHost(const QString& theme) const
{
    if (hudTopButtons(theme)) return "independent";
    INIFileEditor bottom;
    bottom.loadFromFile(readable(themedResource("ini/ui/bottom/bottom.menu.ini", qingyu(theme))).toStdString());
    // ConfigDrivenPanel creates a generic panel even when its explicit submenu definition is missing.
    return bottom.get("submenu1", "file").empty() ? "bottom-fallback" : "generic-submenu";
}

QString MenuPageContext::resource(const QString& page, const QString& theme, bool atTitle) const
{
    if (!valid || !pages().contains(page)) return {};
    if (page == "dicegame") return "ini/ui/dicegame/window.ini";
    if (page == "controllerhelp") return "ini/ui/controller_help/window.ini";
    if (page == "npcinfo") return "ini/ui/npcinfo/window.ini";
    if (page == "joystick") return themedResource("ini/ui/mobile/joystick/joystick.menu.ini", qingyu(theme));
    if (page == "skills") return themedResource("ini/ui/mobile/skills/skills.menu.ini", qingyu(theme));
    if (page == "mapthumbnail") return themedResource(mapThumbnailBaseResource(), qingyu(theme));
    if (page == "titleteam") return "ini/ui/titleteam/titleteam.menu.ini";
    if (page == "systemnotice" || page == "scriptmessages") return "ini/ui/" + page + "/" + page + ".menu.ini";
    // SaveLoad has no GameManager on the title screen and checks only the candidate file.
    const bool themed = page != "title" && (page == "saveload" && atTitle ? theme == "qingyu" : qingyu(theme));
    const auto candidate = [this, themed](const QString& resource)
    {
        return themedResource(resource, themed);
    };
    const auto components = [this, &candidate](const QString& resource)
    {
        QMap<QString, QString> result;
        const QString path = readable(candidate(resource));
        if (path.isEmpty()) return result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return result;
        INIFileEditor ini;
        ini.loadFromString(file.readAll().toStdString(), INIFileEditor::UnrecognizedLinePolicy::Preserve);
        for (int index = 1;; ++index)
        {
            const std::string section = "component" + std::to_string(index);
            const std::string type = ini.get(section, "type");
            if (type.empty()) break;
            result[QString::fromStdString(ini.get(section, "name"))] = QString::fromStdString(type);
        }
        return result;
    };
    QString document = page;
    if (page == "state" || page == "magic")
    {
        const auto equipment = components("ini/ui/equip/equip.menu.ini");
        const bool hasState = equipment.value("labLife") == "Label" || equipment.value("labThew") == "Label"
            || equipment.value("labMana") == "Label";
        const bool topButtons = themed || manifest.isFeatureEnabled("topbuttonslayout", manifest.uiProfile != "JXQY2");
        const bool integrated = themed || manifest.isFeatureEnabled("stateequipintegratedlayout", manifest.uiProfile == "XJXQY");
        const bool combined = hasState && (integrated || components("ini/ui/top/top.menu.ini").value("stateBtn") != "CheckBox"
            || (topButtons && components("ini/ui/bottom/bottom.menu.ini").value("stateBtn") != "CheckBox"));
        const bool magicInEquip = !themed && integrated && manifest.isFeatureEnabled("magicbuttonopensintegratedequip", manifest.uiProfile == "XJXQY");
        if (combined && (page == "state" || magicInEquip)) document = "equip";
    }
    QString resource = "ini/ui/" + document + "/" + document + ".menu.ini";
    if (page == "message") resource = "ini/ui/message/msgbox.menu.ini";
    if (page == "title" && !manifest.titleMenu.empty()) resource = QString::fromStdString(manifest.titleMenu);
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(resource, normalized)) return {};
    if (!EditorAssetPath::normalizeResourcePath(candidate(normalized), normalized)) return {};
    return normalized;
}

QString MenuPageContext::mapThumbnailBaseResource() const
{
    const QString candidate = "ini/ui/littlemap/littlemap.menu.ini";
    // MenuResource chooses the profile candidate before ConfigDrivenPanel applies Qingyu.
    if ((manifest.uiProfile == "YYCS" || manifest.uiProfile == "XJXQY") && !readable(candidate).isEmpty()) return candidate;
    return "ini/ui/mapthumbnail/mapthumbnail.menu.ini";
}

QString MenuPageContext::mapThumbnailLayout(const QString& theme) const
{
    if (qingyu(theme)) return "qingyu";
    if (manifest.uiProfile == "YYCS") return "yycs-derived";
    if (manifest.uiProfile == "XJXQY") return "xjxqy-expanded";
    return "configured";
}

QString MenuPageContext::themedResource(const QString& resource, bool themed) const
{
    if (themed && resource.startsWith("ini/ui/") && !resource.startsWith("ini/ui/qingyu/"))
    {
        const QString alternative = "ini/ui/qingyu/" + resource.mid(7);
        if (!readable(alternative).isEmpty()) return alternative;
    }
    return resource;
}

QString MenuPageContext::source(const QString& page, const QString& theme, bool atTitle) const
{
    const QString entry = resource(page, theme, atTitle);
    return entry.isEmpty() ? QString() : readable(entry);
}

bool MenuPageContext::partnerHeadsAvailable() const
{
    return valid && manifest.isFeatureEnabled("partnerheadmenu", manifest.uiProfile != "JXQY2");
}

bool MenuPageContext::practiceAvailable() const
{
    return valid && !manifest.isFeatureEnabled("practicemenudisabled", manifest.uiProfile == "XJXQY");
}

bool MenuPageContext::canLoad(const QString& page, const QString& theme, bool atTitle) const
{
    if (page == "npcinfo" || page == "controllerhelp") return valid;
    if ((page == "systemnotice" || page == "scriptmessages") && source(page, theme).isEmpty()) return valid;
    if (page == "titleteam" && source(page, theme).isEmpty())
        return valid && (!manifest.teamInfoFile.empty() || !manifest.titleTeamVideo.empty());
    if (page == "yesno" && yesNoFallback(theme))
        return hasComponentData("ini/ui/yesno/window.ini") && hasComponentData("ini/ui/yesno/btnyes.ini")
            && hasComponentData("ini/ui/yesno/btnno.ini") && hasComponentData("ini/ui/yesno/label.ini");
    if (page == "xiulian" && !practiceAvailable()) return false;
    if (page == "partnerhead" && source(page, theme).isEmpty()) return valid;
    const QString path = source(page, theme, atTitle);
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    INIFileEditor ini;
    ini.loadFromString(file.readAll().toStdString(), INIFileEditor::UnrecognizedLinePolicy::Preserve);
    if (page == "dicegame") return ini.hasSection("Window");
    return ini.hasSection("menu");
}

bool MenuPageContext::yesNoFallback(const QString& theme) const
{
    QFile file(source("yesno", theme));
    if (!file.open(QIODevice::ReadOnly)) return true;
    INIFileEditor ini;
    ini.loadFromString(file.readAll().toStdString(), INIFileEditor::UnrecognizedLinePolicy::Preserve);
    QMap<QString, QString> types;
    const QStringList buttons = {"Button", "TextButton", "FlatTextButton", "ChooseTextButton", "CheckBox", "RoundButton", "DragButton", "DragRoundButton", "Joystick"};
    const QStringList others = {"Label", "ImageContainer", "ColumnImage", "TransImage", "Item", "TalkLabel", "Scrollbar", "FlatScrollbar", "ListBox", "MemoText", "VideoPlayer", "FadeMask"};
    for (int i = 1;; ++i)
    {
        const auto section = "component" + std::to_string(i);
        const auto type = ini.get(section, "type");
        if (type.empty()) break;
        const auto name = QString::fromStdString(ini.get(section, "name"));
        const auto componentType = QString::fromStdString(type);
        if (name != "yes" && name != "no" && name != "label") continue;
        // ConfigDrivenPanel updates its name map only after successful creation.
        if ((buttons.contains(componentType) || others.contains(componentType))
            && hasComponentData(QString::fromStdString(ini.get(section, "file")))) types[name] = componentType;
    }
    return !buttons.contains(types.value("yes")) || !buttons.contains(types.value("no")) || types.value("label") != "Label";
}
