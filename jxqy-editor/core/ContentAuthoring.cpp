#include "ContentAuthoring.h"
#include "MenuPageContext.h"

#include "EditorAssetPath.h"
#include "DurableFileTransaction.h"
#include "GameProfile.h"
#include "GoodsShopDocument.h"
#include "LuaLexer.h"
#include "MagicDocument.h"
#include "ProjectDocumentRegistry.h"

#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
QString canonical(ContentKind kind, QString value)
{
    value.replace('\\', '/');
    if (kind == ContentKind::Script) return QFileInfo(value).fileName().toCaseFolded();
    const QString prefix = ContentAuthoring::directory(kind) + '/';
    if (kind != ContentKind::Dialogue && value.startsWith(prefix, Qt::CaseInsensitive))
        value.remove(0, prefix.size());
    return value.toCaseFolded();
}

QList<ResourceContentRoot> rootsFor(const QString& root)
{
    return ResourcePackScanner::resolveContentRoots(root).roots;
}

QString virtualPath(const QString& root, const QString& path)
{
    for (const auto& entry : rootsFor(root))
    {
        if (entry.available && EditorAssetPath::isLexicallyInside(entry.rootPath, path))
            return QDir(entry.rootPath).relativeFilePath(path);
    }
    return {};
}

QString mapFromIni(const QString& text)
{
    QString section;
    for (const QString& line : text.split('\n'))
    {
        const auto header = QRegularExpression("^\\s*\\[([^\\]]+)\\]").match(line);
        if (header.hasMatch()) section = header.captured(1).trimmed();
        const auto map = QRegularExpression("^\\s*Map\\s*=\\s*(.*?)\\s*$", QRegularExpression::CaseInsensitiveOption).match(line);
        if (section.compare("Head", Qt::CaseInsensitive) == 0 && map.hasMatch() && !map.captured(1).isEmpty())
            return QFileInfo(QDir::fromNativeSeparators(map.captured(1))).completeBaseName();
    }
    return {};
}

void appendReference(
    ContentKind kind, ContentReferenceReport& report, const QString& targetName,
    const QString& path, const QString& source, const QString& value,
    int line, qsizetype offset, qsizetype length, const QString& context)
{
    if (canonical(kind, value) == canonical(kind, targetName))
        report.occurrences.append({path, source, line, offset, length, value, context});
}

void scanIni(
    ContentKind kind, ContentReferenceReport& report, const QString& targetName,
    const QString& path, const QString& source, const QString& text)
{
    // These fields load magic files, rather than comparing a display name.
    static const QSet<QString> magicFields = {
        "attackfile", "flymagic", "explodemagicfile", "parasiticmagic",
        "randmagicfile", "secondmagicfile", "magicwhennewpos",
        "magictousewhenkillenemy", "bounceflyendmagic", "changemagic",
        "jumpendmagic", "replacemagic", "usereplacemagic",
        "specialkind9replaceflyini", "specialkind9replaceflyini2",
        "flyini", "flyini2", "flyinis", "magictousewhenbeattacked", "magicini",
        "magiciniwhenuse", "currentusemagicfile", "currentusemagictalentfile"};
    static const QSet<QString> goodsFields = {"goodsname", "headequip", "neckequip", "bodyequip",
        "backequip", "handequip", "wristequip", "footequip", "backgroundtextureequip", "baggoods"};
    static const QSet<QString> scriptFields = {"scriptfile", "scriptfileright", "timerscriptfile", "deathscript"};
    const QRegularExpression assignment(QStringLiteral("^\\s*([^;#\\[=]+?)\\s*=\\s*(.*?)\\s*$"));
    qsizetype offset = 0;
    int lineNumber = 1;
    QString section;
    for (const QString& line : text.split('\n'))
    {
        const auto header = QRegularExpression(QStringLiteral("^\\s*\\[([^\\]]+)\\]")).match(line);
        if (header.hasMatch()) section = header.captured(1).trimmed().toCaseFolded();
        const auto match = assignment.match(line);
        if (match.hasMatch())
        {
            const QString key = match.captured(1).trimmed().toCaseFolded();
            const bool levelUpEffects = kind == ContentKind::Magic && section == QStringLiteral("levelup") && key == QStringLiteral("randomeffects");
            const bool matchingField = kind == ContentKind::Script ? scriptFields.contains(key)
                : kind == ContentKind::Magic ? magicFields.contains(key)
                : kind == ContentKind::Shop ? key == "buyinifile"
                : goodsFields.contains(key) || (key == "inifile" && path.contains("/ini/buy/", Qt::CaseInsensitive));
            if (matchingField || levelUpEffects)
            {
                QString value = match.captured(2);
                const auto comment = QRegularExpression(QStringLiteral("\\s+;")).match(value);
                if (comment.hasMatch()) value = value.left(comment.capturedStart()).trimmed();
                const qsizetype start = offset + match.capturedStart(2);
                appendReference(kind, report, targetName, path, source, value,
                    lineNumber, start, value.size(), match.captured(1).trimmed());
                const bool goods = path.contains("/ini/goods/", Qt::CaseInsensitive);
                const bool list = key == QStringLiteral("baggoods") || key == QStringLiteral("flyinis") ||
                    (!goods && key == QStringLiteral("replacemagic"));
                const bool random = levelUpEffects || (goods && value.contains(','));
                if (list || random)
                {
                    const QRegularExpression item(random
                        ? QStringLiteral("(?:^|,)\\s*([^,\\[]+?)\\s*(?=\\[|,|$)")
                        : key == "baggoods" ? QStringLiteral("(?:^|[;|])\\s*([^;|:]+?)\\s*(?=[:;|]|$)")
                        : QStringLiteral("(?:^|[;；])\\s*([^;；:：]+?)\\s*(?=[:：;；]|$)"));
                    auto matches = item.globalMatch(value);
                    while (matches.hasNext())
                    {
                        const auto part = matches.next();
                        if (part.captured(1) != value)
                            appendReference(kind, report, targetName, path, source,
                                part.captured(1), lineNumber,
                                start + part.capturedStart(1), part.capturedLength(1), key);
                    }
                }
            }
        }
        offset += line.size() + 1;
        ++lineNumber;
    }
}

void scanLua(
    ContentKind kind, ContentReferenceReport& report, const QString& targetName,
    const QString& path, const QString& source, const QString& text)
{
    const auto lexed = LuaLexer::lex(text, path);
    QList<LuaToken> tokens;
    for (const auto& token : lexed.tokens)
        if (LuaLexer::isSignificant(token)) tokens.append(token);
    const QHash<QString, int> magicArguments = {
        {"addmagic", 0}, {"addtalent", 0}, {"delmagic", 0}, {"deletemagic", 0},
        {"setmagiclevel", 0}, {"getmagiclevel", 0}, {"addmagicexp", 0},
        {"getplayermagiclevel", 0}, {"getmagicstate", 0},
        {"setplayermagictousewhenbeattacked", 0}, {"setplayermagicwhenattacked", 0},
        {"setplayermagictousewhenbeatacked", 0},
        {"setnpcmagictousewhenbeattacked", 1}, {"setnpcmagicwhenattacked", 1},
        {"setnpcmagictousewhenbeatacked", 1},
        {"movemagic", 0}, {"usemagic", 0},
        {"addnpcmagic", 1}, {"npcusemagic", 1}, {"addonemagic", 1}, {"addonemogic", 1}};
    const QHash<QString, int> goodsArguments = {{"addgoods", 0}, {"delgoods", 0}, {"removegoods", 0},
        {"getgoodsnum", 0}, {"getgoodsmun", 0}, {"getgoodscountbyfile", 0}, {"getgoodsstate", 0}};
    const QHash<QString, int> shopArguments = {{"buygoods", 0}, {"buygoodsonly", 0}, {"sellgoods", 0},
        {"addrandgoods", 0}, {"addrandomgoods", 0}};
    const QHash<QString, int> dialogueArguments = {{"talk", 0}};
    const QHash<QString, int> scriptArguments = {{"runscript", 0}, {"runscirpt", 0}, {"runparallelscript", 0},
        {"setnpcscript", 1}, {"setobjscript", 1}, {"setnpcdeathscript", 1}, {"setallnpcscript", 1},
        {"setallnpcdeathscript", 1}, {"setnpcclickscript", 1}, {"settimescript", 1}, {"randrun", 1}, {"showstealwin", 1}};
    const auto& arguments = kind == ContentKind::Magic ? magicArguments
        : kind == ContentKind::Goods ? goodsArguments
        : kind == ContentKind::Shop ? shopArguments
        : kind == ContentKind::Script ? scriptArguments : dialogueArguments;
    for (int index = 0; index + 1 < tokens.size(); ++index)
    {
        const auto& call = tokens[index];
        const QString name = call.text.toCaseFolded();
        if (call.kind != LuaTokenKind::Identifier || tokens[index + 1].text != "(" ||
            (index > 0 && (tokens[index - 1].text == "." || tokens[index - 1].text == ":" ||
                          tokens[index - 1].text == "function")))
            continue;
        QList<QList<LuaToken>> args{QList<LuaToken>()};
        int depth = 1;
        int cursor = index + 2;
        for (; cursor < tokens.size() && depth > 0; ++cursor)
        {
            const auto& token = tokens[cursor];
            if (token.kind == LuaTokenKind::Symbol)
            {
                if (token.text == "(" || token.text == "{" || token.text == "[") ++depth;
                if (token.text == ")" || token.text == "}" || token.text == "]") --depth;
                if (depth == 1 && token.text == ",")
                {
                    args.append(QList<LuaToken>());
                    continue;
                }
            }
            if (depth > 0) args.last().append(token);
        }
        const bool displayNameOrFile = kind == ContentKind::Goods && name == "showgivegoodswin";
        int argument = displayNameOrFile ? 0 : arguments.value(name, -1);
        if (kind == ContentKind::Script && name == "settrap") argument = args.size() > 2 ? 2 : 1;
        if (kind == ContentKind::Magic && name == "setnpcmagicfile") argument = args.size() > 1 ? 1 : 0;
        if (argument < 0 || argument >= args.size() || depth != 0)
            continue;
        QList<int> selectedArguments{argument};
        if (kind == ContentKind::Script && (name == "randrun" || name == "showstealwin") && args.size() > 2)
            selectedArguments.append(2);
        for (int selectedArgument : selectedArguments)
        {
            const auto& value = args[selectedArgument];
            if (value.size() == 1 && value[0].complete &&
                (value[0].kind == LuaTokenKind::String || value[0].kind == LuaTokenKind::LongString))
            {
                const auto& literal = value[0];
                const qsizetype previous = report.occurrences.size();
                appendReference(kind, report, targetName, path, source, literal.decodedText,
                    literal.range.start.line, literal.range.start.utf16Offset,
                    literal.range.end.utf16Offset - literal.range.start.utf16Offset, name);
                if (kind == ContentKind::Script && name == "settrap" && args.size() > 2 && report.occurrences.size() > previous)
                {
                    const auto& map = args[0];
                    if (map.size() == 1 && map[0].complete &&
                        (map[0].kind == LuaTokenKind::String || map[0].kind == LuaTokenKind::LongString))
                    {
                        if (!map[0].decodedText.isEmpty()) report.occurrences.last().scriptMap = map[0].decodedText;
                    }
                    else
                    {
                        auto occurrence = report.occurrences.takeLast();
                        occurrence.context = QStringLiteral("script — caller map");
                        report.dynamicReferences.append(occurrence);
                    }
                }
                if (kind == ContentKind::Script && args.size() > 2 && report.occurrences.size() > previous &&
                    (name == "setnpcscript" || name == "setnpcdeathscript" || name == "setobjscript"))
                {
                    const auto& list = args[2];
                    if (list.size() == 1 && list[0].complete &&
                        (list[0].kind == LuaTokenKind::String || list[0].kind == LuaTokenKind::LongString))
                        report.occurrences.last().scriptList = QStringLiteral("") + list[0].decodedText;
                    else
                    {
                        auto occurrence = report.occurrences.takeLast();
                        occurrence.context = QStringLiteral("script — caller map");
                        report.dynamicReferences.append(occurrence);
                    }
                }
                if (displayNameOrFile && report.occurrences.size() > previous)
                    report.dynamicReferences.append(report.occurrences.takeLast());
            }
            else
            {
                report.dynamicReferences.append({path, source, call.range.start.line,
                    -1, 0, {}, call.text});
            }
        }
    }
}
}

QString ContentAuthoring::directory(ContentKind kind)
{
    return kind == ContentKind::Magic ? QStringLiteral("ini/magic")
        : kind == ContentKind::Goods ? QStringLiteral("ini/goods")
        : kind == ContentKind::Shop ? QStringLiteral("ini/buy")
        : kind == ContentKind::Script ? QStringLiteral("script") : QStringLiteral("script/map");
}

QStringList ContentAuthoring::mapNames(const QString& assetsRoot)
{
    QStringList maps;
    QSet<QString> found;
    for (const auto& root : rootsFor(assetsRoot))
    {
        if (!root.available) continue;
        for (const auto& file : QDir(QDir(root.rootPath).filePath("map")).entryInfoList({"*.map"}, QDir::Files, QDir::Name))
        {
            const QString name = file.completeBaseName();
            if (!found.contains(name.toCaseFolded()))
            {
                found.insert(name.toCaseFolded());
                maps.append(name);
            }
        }
    }
    maps.sort(Qt::CaseInsensitive);
    return maps;
}

QString ContentAuthoring::bindingScript(const QString& assetsRoot, const QString& mapName, const QString& reference)
{
    QString value = reference.trimmed();
    value.replace('\\', '/');
    if (value.isEmpty() || QDir::isAbsolutePath(value) || value.split('/').contains("..")) return {};
    QStringList candidates;
    const QString map = QFileInfo(QDir::fromNativeSeparators(mapName)).completeBaseName();
    if (!map.isEmpty()) candidates.append("script/map/" + map + '/' + value);
    candidates.append("script/goods/" + value);
    candidates.append("script/common/" + value);
    for (const auto& candidate : candidates)
    {
        const QString source = resolve(assetsRoot, candidate);
        if (!source.isEmpty()) return source;
    }
    return {};
}

QString ContentAuthoring::createBindingScript(const QString& assetsRoot, const QString& mapName, const QString& input, QString& error)
{
    error.clear();
    QString name = input.normalized(QString::NormalizationForm_C).trimmed();
    if (name.isEmpty() || name.startsWith('.') || name.endsWith('.') || name.size() > 120 ||
        name.contains(QRegularExpression("[\\\\/:*?\"<>|\\r\\n]")) ||
        QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
    {
        error = QCoreApplication::translate("ContentAuthoring", "请输入有效脚本名称，不包含目录或特殊字符。");
        return {};
    }
    if (!name.endsWith(".lua", Qt::CaseInsensitive)) name += ".lua";
    if (!bindingScript(assetsRoot, mapName, name).isEmpty())
    {
        error = QCoreApplication::translate("ContentAuthoring", "这个脚本名已被使用，请换一个名称。");
        return {};
    }
    const QString map = QFileInfo(QDir::fromNativeSeparators(mapName)).completeBaseName();
    const QString relative = map.isEmpty() ? "script/common/" + name : "script/map/" + map + '/' + name;
    const QString target = QDir(assetsRoot).filePath(relative);
    if (!QFileInfo(assetsRoot).isDir() || !EditorAssetPath::isInside(assetsRoot, target) || !QDir().mkpath(QFileInfo(target).path()))
    {
        error = QCoreApplication::translate("ContentAuthoring", "无法创建项目脚本目录。");
        return {};
    }
    DurableFileTransaction transaction(assetsRoot);
    return transaction.addBytesWriteChecked(target, {}, false, {}, error) && transaction.commit(error) ? target : QString();
}

QList<ContentResourceEntry> ContentAuthoring::resources(ContentKind kind, const QString& assetsRoot)
{
    QList<ContentResourceEntry> result;
    QSet<QString> found;
    for (const auto& root : rootsFor(assetsRoot))
    {
        if (!root.available) continue;
        const QString directory = QDir(root.rootPath).filePath(ContentAuthoring::directory(kind));
        QDirIterator files(directory, kind == ContentKind::Dialogue ? QStringList{"talk.txt"}
            : kind == ContentKind::Script ? QStringList{"*.lua", "*.txt", "*.script"} : QStringList{"*.ini"},
            QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            const QString source = files.next();
            const QString name = QDir(directory).relativeFilePath(source);
            if (kind == ContentKind::Script && QFileInfo(name).fileName().compare("talk.txt", Qt::CaseInsensitive) == 0) continue;
            if (found.contains(name.toCaseFolded())) continue;
            found.insert(name.toCaseFolded());
            QFile input(source);
            const bool readable = input.open(QIODevice::ReadOnly);
            const QByteArray bytes = readable ? input.readAll() : QByteArray();
            if ((kind == ContentKind::Magic || kind == ContentKind::Goods) && readable && !QRegularExpression("^\\s*\\[Init\\]",
                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption)
                    .match(QString::fromUtf8(bytes)).hasMatch())
                continue;
            QString displayName;
            if (kind == ContentKind::Magic)
            {
                MagicDocument document;
                if (readable && document.load(bytes)) displayName = document.text(MagicTextField::Name);
            }
            else if (kind == ContentKind::Goods)
            {
                GoodsDocument document;
                if (readable && document.load(bytes)) displayName = document.text(GoodsTextField::Name);
            }
            else if (kind == ContentKind::Dialogue) displayName = QFileInfo(source).dir().dirName();
            result.append({displayName, name, source});
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& first, const auto& second)
    {
        return first.fileName.compare(second.fileName, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<ContentFileEntry> ContentAuthoring::files(const QString& assetsRoot,
    const QStringList& folders, const QStringList& nameFilters, QString* error)
{
    if (error) error->clear();
    const auto context = MenuPageContext::resolve(assetsRoot);
    if (!context.recoveryErrors.isEmpty())
    {
        if (error) *error = context.recoveryErrors.join('\n');
        return {};
    }
    QList<ContentFileEntry> result;
    QSet<QString> seen;
    const QStringList paths = folders.isEmpty() ? QStringList{QString()} : folders;
    for (const QString& root : context.allReadRoots())
    {
        for (const QString& folder : paths)
        {
            QString start = root;
            if (!folder.isEmpty() && !EditorAssetPath::resolveLogicalResourcePath(root, folder, start)) continue;
            QDirIterator iterator(start, QDir::Files | QDir::NoDotAndDotDot,
                QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
            while (iterator.hasNext())
            {
                const QString path = iterator.next();
                const QString relative = QDir(root).relativeFilePath(path);
                const auto parts = relative.split('/');
                if (parts.contains(".jxqy_editor", Qt::CaseInsensitive) || parts.contains(".git", Qt::CaseInsensitive)) continue;
                const QString name = QFileInfo(path).fileName();
                if (name.startsWith(".jxqy_", Qt::CaseInsensitive) || name == ".gitignore" || name == ".gitattributes" || name == ".gitmodules") continue;
                if (!nameFilters.isEmpty() && !QDir::match(nameFilters, QFileInfo(path).fileName())) continue;
                const QString key = relative.toCaseFolded();
                if (seen.contains(key)) continue;
                const QString source = context.readable(relative);
                if (source.isEmpty()) continue;
                seen.insert(key);
                QString sourceRoot;
                for (const QString& candidate : context.rootsFor(relative))
                    if (EditorAssetPath::isLexicallyInside(candidate, source)) { sourceRoot = candidate; break; }
                result.append({relative, source, sourceRoot});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
    { return a.resourcePath.compare(b.resourcePath, Qt::CaseInsensitive) < 0; });
    return result;
}

QString ContentAuthoring::resolve(const QString& assetsRoot, const QString& resourcePath)
{
    for (const auto& root : rootsFor(assetsRoot))
    {
        QString path;
        if (root.available && EditorAssetPath::resolveLogicalResourcePath(root.rootPath, resourcePath, path) &&
            QFileInfo(path).isFile()) return path;
    }
    return {};
}

QString ContentAuthoring::referenceName(ContentKind kind, const QString& assetsRoot, const QString& filePath)
{
    if (assetsRoot.isEmpty() || filePath.isEmpty()) return {};
    QString relative = virtualPath(assetsRoot, filePath);
    const QString prefix = directory(kind) + '/';
    if (relative.startsWith(prefix, Qt::CaseInsensitive)) relative.remove(0, prefix.size());
    else relative.clear();
    return relative;
}

QString ContentAuthoring::projectPath(const QString& assetsRoot, const QString& sourcePath)
{
    if (assetsRoot.isEmpty() || sourcePath.isEmpty()) return sourcePath;
    const QString relative = virtualPath(assetsRoot, sourcePath);
    QString target;
    return !relative.isEmpty() && EditorAssetPath::resolveLogicalResourcePath(assetsRoot, relative, target)
        ? target : sourcePath;
}

QString ContentAuthoring::fallback(const QString& assetsRoot, const QString& resourcePath)
{
    for (const auto& root : rootsFor(assetsRoot))
    {
        if (!root.available || ProjectDocumentRegistry::documentPathKey(root.rootPath) ==
            ProjectDocumentRegistry::documentPathKey(assetsRoot)) continue;
        QString path;
        if (EditorAssetPath::resolveLogicalResourcePath(root.rootPath, resourcePath, path) && QFileInfo(path).isFile())
            return path;
    }
    return {};
}

QString ContentAuthoring::unusedFileName(ContentKind kind, const QString& assetsRoot, const QString& name)
{
    QString base = name.normalized(QString::NormalizationForm_C).trimmed().toLower();
    base.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]+")), "_");
    if (base.isEmpty() || QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])$", QRegularExpression::CaseInsensitiveOption).match(base).hasMatch())
        base = kind == ContentKind::Magic ? "magic" : kind == ContentKind::Goods ? "goods" : "shop";
    base = base.left(60);
    QSet<QString> used;
    for (const auto& entry : resources(kind, assetsRoot)) used.insert(canonical(kind, entry.fileName));
    QString candidate = base + ".ini";
    for (int suffix = 2; used.contains(canonical(kind, candidate)) ||
             !resolve(assetsRoot, directory(kind) + '/' + candidate).isEmpty(); ++suffix)
        candidate = base + "_" + QString::number(suffix) + ".ini";
    return candidate;
}

QByteArray ContentAuthoring::newDocument(ContentKind kind, const QString& name)
{
    if (kind == ContentKind::Shop)
        return "[Head]\r\nCount=0\r\n";
    if (kind == ContentKind::Dialogue)
        return "[Name]\r\n";
    if (kind == ContentKind::Goods)
    {
        GoodsDocument document;
        document.load("[Init]\r\nName=\r\nKind=0\r\nCost=100\r\nLife=100\r\n");
        document.setText(GoodsTextField::Name, name);
        return document.serializedBytes();
    }
    MagicDocument document;
    document.load("[Init]\r\nName=\r\nMoveKind=2\r\nSpeed=8\r\nLifeFrame=40\r\nAttackRadius=6\r\nEffect=10\r\n");
    document.setText(MagicTextField::Name, name);
    return document.serializedBytes();
}

ContentReferenceReport ContentAuthoring::references(
    ContentKind kind, const QString& assetsRoot, const QString& name,
    const QList<DesktopRunDocumentSnapshot>& snapshots, const QString& scopeFile)
{
    ContentReferenceReport report;
    if (kind == ContentKind::Dialogue && scopeFile.isEmpty())
    {
        report.errors.append(QStringLiteral("Talk file context is required."));
        return report;
    }
    QHash<QString, QString> files;
    QSet<QString> virtualNames;
    const auto resolution = ResourcePackScanner::resolveContentRoots(assetsRoot);
    if (!resolution.missingDependencyIds.isEmpty())
        report.errors.append(QStringLiteral("game_profile.ini — DependencyId: ") + resolution.missingDependencyIds.join(", "));
    report.errors.append(resolution.recoveryErrors);
    for (const auto& root : resolution.roots)
    {
        if (!root.available) continue;
        const QString profile = QDir(root.rootPath).filePath("game_profile.ini");
        if (!virtualNames.contains("game_profile.ini") && QFileInfo(profile).isFile())
        {
            virtualNames.insert("game_profile.ini");
            files.insert(QDir(assetsRoot).filePath("game_profile.ini"), profile);
        }
        for (const QString& directory : {QStringLiteral("ini"), QStringLiteral("script"),
                                        QStringLiteral("npc"), QStringLiteral("obj")})
        {
            QDirIterator iterator(QDir(root.rootPath).filePath(directory),
                {"*.ini", "*.lua", "*.txt", "*.script", "*.npc", "*.obj"}, QDir::Files,
                QDirIterator::Subdirectories);
            while (iterator.hasNext())
            {
                const QString source = iterator.next();
                const QString relative = QDir(root.rootPath).relativeFilePath(source);
                if (virtualNames.contains(relative.toCaseFolded())) continue;
                virtualNames.insert(relative.toCaseFolded());
                files.insert(QDir(assetsRoot).filePath(relative), source);
            }
        }
    }
    QHash<QString, QByteArray> overlays;
    for (const auto& snapshot : snapshots)
    {
        if (!snapshot.serializationSupported || snapshot.filePath.isEmpty()) continue;
        const QString relative = virtualPath(assetsRoot, snapshot.filePath);
        if (relative.isEmpty()) continue;
        const QString path = QDir(assetsRoot).filePath(relative);
        overlays.insert(ProjectDocumentRegistry::documentPathKey(path), snapshot.bytes);
        if (!files.contains(path)) files.insert(path, snapshot.filePath);
    }
    for (auto file = files.cbegin(); file != files.cend(); ++file)
    {
        const QString relative = QDir(assetsRoot).relativeFilePath(file.key());
        const bool scriptFile = file.key().endsWith(".lua", Qt::CaseInsensitive) || file.key().endsWith(".script", Qt::CaseInsensitive) ||
            (relative.startsWith("script/", Qt::CaseInsensitive) && file.key().endsWith(".txt", Qt::CaseInsensitive) &&
             QFileInfo(file.key()).fileName().compare("talk.txt", Qt::CaseInsensitive) != 0);
        const QString scopeDirectory = QFileInfo(scopeFile).path() + '/';
        const bool sameMap = relative.startsWith(scopeDirectory, Qt::CaseInsensitive);
        if (kind == ContentKind::Dialogue && (!scriptFile ||
            (relative.startsWith("script/map/", Qt::CaseInsensitive) && !sameMap))) continue;
        QFile input(file.value());
        QByteArray disk;
        const bool readable = input.open(QIODevice::ReadOnly);
        if (readable) disk = input.readAll();
        if (!readable && !overlays.contains(ProjectDocumentRegistry::documentPathKey(file.key())))
        {
            report.errors.append(file.key());
            continue;
        }
        const QByteArray bytes = overlays.value(ProjectDocumentRegistry::documentPathKey(file.key()), disk);
        if (bytes.isEmpty() || bytes.contains('\0')) continue;
        const QByteArray textBytes = bytes.startsWith("\xEF\xBB\xBF") ? bytes.mid(3) : bytes;
        const QString text = QString::fromUtf8(textBytes);
        if (text.toUtf8() != textBytes)
        {
            report.errors.append(file.key());
            continue;
        }
        const qsizetype previous = report.occurrences.size();
        if (scriptFile)
            scanLua(kind, report, name, file.key(), file.value(), text);
        else scanIni(kind, report, name, file.key(), file.value(), text);
        if (kind == ContentKind::Dialogue && !sameMap)
            while (report.occurrences.size() > previous)
            {
                auto occurrence = report.occurrences.takeLast();
                occurrence.context = QStringLiteral("talk — caller map");
                report.dynamicReferences.append(occurrence);
            }
        if (kind == ContentKind::Script && report.occurrences.size() > previous)
        {
            QString documentMap;
            const QStringList parts = relative.split('/');
            if (parts.size() >= 4 && parts[0].compare("script", Qt::CaseInsensitive) == 0 &&
                parts[1].compare("map", Qt::CaseInsensitive) == 0)
                documentMap = parts[2];
            else if (!scriptFile)
                documentMap = mapFromIni(text);
            QString target = ("script/" + QDir::fromNativeSeparators(name)).toCaseFolded();
            if (!name.contains('/') && !name.contains('\\'))
            {
                QStringList matches;
                for (const QString& candidate : virtualNames)
                    if (candidate.startsWith("script/") && QFileInfo(candidate).fileName() == name.toCaseFolded())
                        matches.append(candidate);
                if (matches.size() == 1) target = matches.first();
            }
            for (qsizetype index = report.occurrences.size() - 1; index >= previous; --index)
            {
                auto occurrence = report.occurrences[index];
                QString map = occurrence.scriptMap.isNull() ? documentMap
                    : QFileInfo(QDir::fromNativeSeparators(occurrence.scriptMap)).completeBaseName();
                if (!occurrence.scriptList.isNull())
                {
                    map = {};
                    const QString listPath = "ini/save/" + QDir::fromNativeSeparators(occurrence.scriptList);
                    for (auto list = files.cbegin(); list != files.cend(); ++list)
                        if (QDir(assetsRoot).relativeFilePath(list.key()).compare(listPath, Qt::CaseInsensitive) == 0)
                        {
                            QFile input(list.value());
                            const QByteArray disk = input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
                            map = mapFromIni(QString::fromUtf8(overlays.value(ProjectDocumentRegistry::documentPathKey(list.key()), disk)));
                            break;
                        }
                }
                QString value = QDir::fromNativeSeparators(occurrence.value);
                QString resolved;
                const bool unknownList = !occurrence.scriptList.isNull() && map.isNull();
                bool uncertain = unknownList || QDir::isAbsolutePath(value) || value.split('/').contains("..");
                if (!uncertain && !map.isNull())
                {
                    QStringList candidates;
                    if (!map.isEmpty()) candidates.append("script/map/" + map + '/' + value);
                    candidates.append("script/goods/" + value);
                    candidates.append("script/common/" + value);
                    for (const QString& candidate : candidates)
                        if (virtualNames.contains(candidate.toCaseFolded())) { resolved = candidate.toCaseFolded(); break; }
                }
                else if (!uncertain)
                {
                    QStringList candidates;
                    for (const QString& candidate : virtualNames)
                        if (candidate.startsWith("script/") && candidate.endsWith('/' + value.toCaseFolded())) candidates.append(candidate);
                    uncertain = candidates.size() > 1;
                    if (candidates.size() == 1) resolved = candidates.first();
                }
                if (uncertain)
                {
                    report.occurrences.removeAt(index);
                    occurrence.context = unknownList ? QStringLiteral("script — caller map") : QStringLiteral("script — duplicate filename");
                    report.dynamicReferences.append(occurrence);
                }
                else if (resolved != target)
                    report.occurrences.removeAt(index);
            }
        }
        if (report.occurrences.size() != previous)
        {
            report.fileBytes.insert(file.key(), bytes);
            report.diskBytes.insert(file.key(), disk);
        }
    }
    return report;
}

QByteArray ContentAuthoring::replaceReferences(
    ContentKind kind, const QByteArray& bytes, const QList<ContentReferenceOccurrence>& occurrences,
    const QString& newName)
{
    QString text = QString::fromUtf8(bytes);
    auto sorted = occurrences;
    std::sort(sorted.begin(), sorted.end(), [](const auto& first, const auto& second)
    {
        return first.offset > second.offset;
    });
    qsizetype previous = text.size();
    for (const auto& occurrence : sorted)
    {
        if (occurrence.offset < 0 || occurrence.offset + occurrence.length > previous) continue;
        QString replacement = newName;
        if (kind == ContentKind::Script)
        {
            QString value = occurrence.value;
            value.replace('\\', '/');
            const QString prefix = QFileInfo(value).path();
            replacement = prefix == "." ? QFileInfo(newName).fileName() : prefix + '/' + QFileInfo(newName).fileName();
        }
        if (kind != ContentKind::Dialogue && QDir::fromNativeSeparators(occurrence.value).startsWith(directory(kind) + '/', Qt::CaseInsensitive))
            replacement.prepend(directory(kind) + '/');
        const QString old = text.mid(occurrence.offset, occurrence.length);
        if (old != occurrence.value)
        {
            replacement.replace('\\', "\\\\");
            replacement.replace('"', "\\\"");
            replacement = '"' + replacement + '"';
        }
        text.replace(occurrence.offset, occurrence.length, replacement);
        previous = occurrence.offset;
    }
    return (bytes.startsWith("\xEF\xBB\xBF") ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + text.toUtf8();
}
