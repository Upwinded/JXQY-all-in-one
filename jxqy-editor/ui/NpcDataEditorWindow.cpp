#include "NpcDataEditorWindow.h"
#include "../core/AuthoringMutationGate.h"
#include "ui_NpcDataEditorWindow.h"
#include "AssetDragDrop.h"
#include "FilePickerHelper.h"
#include "MpcPreviewLabel.h"
#include "../core/INIFileEditor.h"
#include "../core/MpcImageCache.h"
#include "../core/ProjectManager.h"
#include "../core/ScriptConverter.h"
#include "../core/EditorAssetPath.h"
#include "../core/DurableFileTransaction.h"

#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QUuid>
#include <vector>

static QByteArray textLinesToUtf8Bytes(const QStringList& lines)
{
    QByteArray result;
    for (const QString& line : lines)
    {
        QString normalizedLine = line;
        while (normalizedLine.endsWith('\n') ||
               normalizedLine.endsWith('\r'))
        {
            normalizedLine.chop(1);
        }
        result.append(normalizedLine.toUtf8());
        result.append('\n');
    }
    return result;
}

static bool writeTextLinesAtomically(const QString& fileName, const QStringList& lines)
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
    if (!mutationLease)
        return false;

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QByteArray bytes = textLinesToUtf8Bytes(lines);
    if (file.write(bytes) != bytes.size())
    {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

static bool writeBytesAtomically(const QString& fileName, const QByteArray& data)
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fileName);
    if (!mutationLease)
        return false;

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(data) != data.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

static const QStringList& canonicalNpcResourceSections()
{
    static const QStringList sections = {
        QStringLiteral("Stand"), QStringLiteral("Stand1"),
        QStringLiteral("Walk"), QStringLiteral("Run"), QStringLiteral("Jump"),
        QStringLiteral("Attack"), QStringLiteral("Attack1"),
        QStringLiteral("Attack2"), QStringLiteral("Magic"),
        QStringLiteral("Hurt"), QStringLiteral("Death"), QStringLiteral("Sit"),
        QStringLiteral("FightStand"), QStringLiteral("FightWalk"),
        QStringLiteral("FightRun"), QStringLiteral("FightJump")
    };
    return sections;
}

static bool isKnownNpcResourceSection(const QString& section)
{
    for (const QString& known : canonicalNpcResourceSections())
    {
        if (section.compare(known, Qt::CaseInsensitive) == 0)
            return true;
    }
    const QString lower = section.toLower();
    return lower == QStringLiteral("astand") ||
           lower == QStringLiteral("awalk") ||
           lower == QStringLiteral("arun") ||
           lower == QStringLiteral("ajump");
}

static bool validateNpcResourceRawShape(const QStringList& lines,
                                        QStringList& sectionOrder,
                                        QString& errorMessage)
{
    sectionOrder.clear();
    QSet<QString> sections;
    QMap<QString, QSet<QString>> keysBySection;
    QString currentSection;
    bool hasRecognizedShape = false;

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const QString trimmed = lines[lineIndex].trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') ||
            trimmed.startsWith('#') || trimmed.startsWith("//"))
        {
            continue;
        }
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
        {
            const QString section =
                trimmed.mid(1, trimmed.size() - 2).trimmed();
            if (section.isEmpty())
            {
                errorMessage = NpcDataEditorWindow::tr(
                    "第 %1 行的段名为空。").arg(lineIndex + 1);
                return false;
            }
            const QString lowerSection = section.toLower();
            if (sections.contains(lowerSection))
            {
                errorMessage = NpcDataEditorWindow::tr(
                    "段 [%1] 重复，保存会丢失前一段内容。").arg(section);
                return false;
            }
            if (sections.size() >= 256)
            {
                errorMessage = NpcDataEditorWindow::tr(
                    "NPC 资源文件最多允许 256 个动作段。");
                return false;
            }
            sections.insert(lowerSection);
            keysBySection.insert(lowerSection, {});
            sectionOrder.append(section);
            currentSection = lowerSection;
            hasRecognizedShape = hasRecognizedShape ||
                isKnownNpcResourceSection(section);
            continue;
        }

        const int equal = trimmed.indexOf('=');
        const int colon = trimmed.indexOf(':');
        int separator = -1;
        if (equal >= 0 && colon >= 0)
            separator = qMin(equal, colon);
        else
            separator = qMax(equal, colon);
        if (separator <= 0 || currentSection.isEmpty())
        {
            errorMessage = NpcDataEditorWindow::tr(
                "第 %1 行不是段内的 key=value：%2")
                .arg(lineIndex + 1)
                .arg(trimmed);
            return false;
        }

        const QString key = trimmed.left(separator).trimmed();
        const QString lowerKey = key.toLower();
        if (key.isEmpty() || keysBySection[currentSection].contains(lowerKey))
        {
            errorMessage = NpcDataEditorWindow::tr(
                "[%1] 中的键 %2 重复或为空。")
                .arg(sectionOrder.value(sectionOrder.size() - 1), key);
            return false;
        }
        if (keysBySection[currentSection].size() >= 256)
        {
            errorMessage = NpcDataEditorWindow::tr(
                "单个动作段最多允许 256 个字段。");
            return false;
        }
        keysBySection[currentSection].insert(lowerKey);
        hasRecognizedShape = hasRecognizedShape ||
            lowerKey == QStringLiteral("image") ||
            lowerKey == QStringLiteral("shade") ||
            lowerKey == QStringLiteral("sound");
    }

    if (sectionOrder.isEmpty() || !hasRecognizedShape)
    {
        errorMessage = NpcDataEditorWindow::tr(
            "文件不包含可识别的 NPC 动作资源段或 Image/Shade/Sound 字段。");
        return false;
    }
    return true;
}

static bool normalizeNpcResourceAssetReference(const QString& input,
                                               QString& output)
{
    output.clear();
    if (input.trimmed().isEmpty())
        return true;
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(input, normalized))
        return false;
    normalized.replace('/', '\\');
    output = normalized;
    return true;
}

static bool pathsReferToSameTarget(const QString& firstPath, const QString& secondPath)
{
    if (firstPath.isEmpty() || secondPath.isEmpty())
        return false;

    auto normalizedTarget = [](const QString& path)
    {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    };

#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return normalizedTarget(firstPath).compare(normalizedTarget(secondPath), sensitivity) == 0;
}

static QString transactionRootForTargets(const QString& assetsBasePath,
                                         const QStringList& targetPaths)
{
    if (targetPaths.isEmpty())
        return QString();

    QString candidate = !assetsBasePath.isEmpty() && QFileInfo(assetsBasePath).isDir()
        ? EditorAssetPath::normalizedAbsolutePath(assetsBasePath)
        : QFileInfo(targetPaths.first()).absolutePath();
    while (!candidate.isEmpty())
    {
        bool containsAll = true;
        for (const QString& targetPath : targetPaths)
        {
            if (!EditorAssetPath::isInside(candidate, targetPath))
            {
                containsAll = false;
                break;
            }
        }
        if (containsAll)
            return candidate;
        QDir parent(candidate);
        if (!parent.cdUp() || parent.absolutePath() == candidate)
            break;
        candidate = parent.absolutePath();
    }
    return QString();
}

static QStringList splitUtf8TextLines(const std::string& utf8Text)
{
    const QString text = QString::fromUtf8(
        utf8Text.data(), static_cast<qsizetype>(utf8Text.size()));
    QStringList lines;
    qsizetype lineStart = 0;
    while (lineStart < text.size())
    {
        const qsizetype newline = text.indexOf('\n', lineStart);
        if (newline < 0)
        {
            lines.append(text.mid(lineStart));
            break;
        }
        lines.append(text.mid(lineStart, newline - lineStart + 1));
        lineStart = newline + 1;
    }
    return lines;
}

static bool readIniTextAsUtf8(const QString& fileName,
                              std::string& utf8Text,
                              QStringList& lines,
                              QString& errorMessage)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        errorMessage = NpcDataEditorWindow::tr("无法读取文件: %1").arg(fileName);
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError)
    {
        errorMessage = NpcDataEditorWindow::tr("读取文件失败: %1").arg(fileName);
        return false;
    }

    utf8Text.assign(bytes.constData(), static_cast<size_t>(bytes.size()));
    if (!ScriptConverter::detectAndConvertEncoding(utf8Text))
    {
        errorMessage = NpcDataEditorWindow::tr(
            "文件既不是有效 UTF-8，也无法按 GBK/GB18030 解码: %1").arg(fileName);
        return false;
    }

    lines = splitUtf8TextLines(utf8Text);
    return true;
}

static bool validateEntityListShape(const INIFileEditor& ini,
                                    const QStringList& sourceLines,
                                    const QString& sectionPrefix,
                                    int& count,
                                    QString& errorMessage)
{
    std::string headSection;
    for (const std::string& section : ini.getSectionNames())
    {
        if (QString::fromStdString(section).trimmed().compare("Head", Qt::CaseInsensitive) == 0)
        {
            headSection = section;
            break;
        }
    }
    if (headSection.empty())
    {
        errorMessage = NpcDataEditorWindow::tr("缺少 [Head] 段。");
        return false;
    }

    std::string countKey;
    for (const std::string& key : ini.getKeyNames(headSection))
    {
        if (QString::fromStdString(key).compare("Count", Qt::CaseInsensitive) == 0)
        {
            countKey = key;
            break;
        }
    }
    if (countKey.empty())
    {
        errorMessage = NpcDataEditorWindow::tr("[Head] 段缺少 Count。");
        return false;
    }

    bool countOk = false;
    const qlonglong parsedCount = QString::fromStdString(
        ini.get(headSection, countKey)).trimmed().toLongLong(&countOk);
    if (!countOk || parsedCount < 0 || parsedCount > 100000)
    {
        errorMessage = NpcDataEditorWindow::tr("Count 必须是 0 到 100000 之间的整数。");
        return false;
    }
    count = static_cast<int>(parsedCount);

    QSet<int> sectionIds;
    for (const std::string& section : ini.getSectionNames())
    {
        const QString sectionName = QString::fromStdString(section).trimmed();
        if (!sectionName.startsWith(sectionPrefix, Qt::CaseInsensitive))
            continue;

        const QString suffix = sectionName.mid(sectionPrefix.size());
        if (suffix.isEmpty())
            continue;
        bool allDigits = true;
        for (const QChar character : suffix)
        {
            if (!character.isDigit())
            {
                allDigits = false;
                break;
            }
        }
        if (!allDigits)
            continue;

        bool idOk = false;
        const int sectionId = suffix.toInt(&idOk);
        if (idOk)
            sectionIds.insert(sectionId);
    }

    int rawHeadCount = 0;
    QSet<int> rawSectionIds;
    for (const QString& line : sourceLines)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith('[') || !trimmed.endsWith(']'))
            continue;

        const QString sectionName = trimmed.mid(1, trimmed.size() - 2).trimmed();
        if (sectionName.compare("Head", Qt::CaseInsensitive) == 0)
        {
            rawHeadCount++;
            continue;
        }
        if (!sectionName.startsWith(sectionPrefix, Qt::CaseInsensitive))
            continue;

        const QString suffix = sectionName.mid(sectionPrefix.size());
        bool idOk = !suffix.isEmpty();
        for (const QChar character : suffix)
            idOk = idOk && character.isDigit();
        if (!idOk)
            continue;

        const int sectionId = suffix.toInt(&idOk);
        if (!idOk)
            continue;
        if (rawSectionIds.contains(sectionId))
        {
            errorMessage = NpcDataEditorWindow::tr(
                "%1%2 段重复，已拒绝覆盖前一个同名实体。")
                .arg(sectionPrefix.toUpper())
                .arg(sectionId, 3, 10, QChar('0'));
            return false;
        }
        rawSectionIds.insert(sectionId);
    }
    if (rawHeadCount != 1)
    {
        errorMessage = NpcDataEditorWindow::tr(
            "文件必须且只能包含一个 [Head] 段，当前为 %1 个。")
            .arg(rawHeadCount);
        return false;
    }

    bool contiguous = sectionIds.size() == count;
    for (int i = 0; contiguous && i < count; i++)
        contiguous = sectionIds.contains(i);
    if (!contiguous)
    {
        errorMessage = NpcDataEditorWindow::tr(
            "[Head].Count=%1，但 %2 编号段不是从 000 起连续编号；"
            "运行时只读取 000..Count-1，已拒绝静默重排。")
            .arg(count)
            .arg(sectionPrefix.toUpper());
        return false;
    }

    return true;
}

static void selectComboValuePreservingUnknown(QComboBox* combo, const QString& rawValue)
{
    bool isInteger = false;
    const int integerValue = rawValue.toInt(&isInteger);
    int index = isInteger ? combo->findData(integerValue) : -1;
    if (index < 0)
        index = combo->findData(rawValue);

    if (index < 0)
    {
        combo->addItem(
            NpcDataEditorWindow::tr("%1 - 自定义值").arg(rawValue), rawValue);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

static void setSpinBoxValuePreservingRange(QSpinBox* spinBox,
                                            const QString& rawValue,
                                            int defaultValue)
{
    bool isInteger = false;
    int value = rawValue.toInt(&isInteger);
    if (!isInteger)
        value = defaultValue;

    if (value < spinBox->minimum())
        spinBox->setMinimum(value);
    if (value > spinBox->maximum())
        spinBox->setMaximum(value);
    spinBox->setValue(value);
}

static QMap<QString, QString> createNpcKeyCaseMap()
{
    QMap<QString, QString> map;
    map["name"] = "Name";
    map["kind"] = "Kind";
    map["npcini"] = "NPCIni";
    map["dir"] = "Dir";
    map["mapx"] = "MapX";
    map["mapy"] = "MapY";
    map["action"] = "Action";
    map["walkspeed"] = "WalkSpeed";
    map["standspeed"] = "StandSpeed";
    map["pathfinder"] = "PathFinder";
    map["dialogradius"] = "DialogRadius";
    map["scriptfile"] = "ScriptFile";
    map["state"] = "State";
    map["relation"] = "Relation";
    map["life"] = "Life";
    map["lifemax"] = "LifeMax";
    map["thew"] = "Thew";
    map["thewmax"] = "ThewMax";
    map["mana"] = "Mana";
    map["manamax"] = "ManaMax";
    map["attack"] = "Attack";
    map["defend"] = "Defend";
    map["defence"] = "Defend";
    map["evade"] = "Evade";
    map["duck"] = "Duck";
    map["exp"] = "Exp";
    map["levelupexp"] = "LevelUpExp";
    map["level"] = "Level";
    map["attacklevel"] = "AttackLevel";
    map["magiclevel"] = "MagicLevel";
    map["lum"] = "Lum";
    map["visionradius"] = "VisionRadius";
    map["attackradius"] = "AttackRadius";
    map["bodyini"] = "BodyIni";
    map["flyini"] = "FlyIni";
    map["flyini2"] = "FlyIni2";
    map["flyinis"] = "FlyInis";
    map["magicini"] = "MagicIni";
    map["deathscript"] = "DeathScript";
    return map;
}

static QMap<QString, QString> createObjectKeyCaseMap()
{
    QMap<QString, QString> map;
    map["objname"] = "ObjName";
    map["name"] = "ObjName";
    map["objfile"] = "ObjFile";
    map["scriptfile"] = "ScriptFile";
    map["wavfile"] = "WavFile";
    map["kind"] = "Kind";
    map["dir"] = "Dir";
    map["mapx"] = "MapX";
    map["mapy"] = "MapY";
    map["offsetx"] = "OffsetX";
    map["offsety"] = "OffsetY";
    map["offx"] = "OffsetX";
    map["offy"] = "OffsetY";
    map["lum"] = "Lum";
    map["damage"] = "Damage";
    map["frame"] = "Frame";
    map["state"] = "State";
    map["actiontime"] = "ActionTime";
    return map;
}

static const QMap<QString, QString>& npcKeyCaseMap()
{
    static const QMap<QString, QString> map = createNpcKeyCaseMap();
    return map;
}

static const QMap<QString, QString>& objectKeyCaseMap()
{
    static const QMap<QString, QString> map = createObjectKeyCaseMap();
    return map;
}

NpcDataEditorWindow::NpcDataEditorWindow(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::NpcDataEditorWindow)
{
    ui->setupUi(this);

    initializeCombos();
    npcResourceDocument = std::make_unique<INIFileEditor>();
    npcResourceImageCache = std::make_unique<MpcImageCache>();
    setupNpcResourceEditor();

    connect(ui->actionOpen, &QAction::triggered, this, &NpcDataEditorWindow::onOpen);
    connect(ui->actionSave, &QAction::triggered, this, &NpcDataEditorWindow::onSave);
    connect(ui->actionSaveAs, &QAction::triggered, this, &NpcDataEditorWindow::onSaveAs);
    connect(ui->actionAddEntry, &QAction::triggered, this, &NpcDataEditorWindow::onAddEntry);
    connect(ui->actionRemoveEntry, &QAction::triggered, this, &NpcDataEditorWindow::onRemoveEntry);
    connect(ui->actionMoveUp, &QAction::triggered, this, &NpcDataEditorWindow::onMoveUp);
    connect(ui->actionMoveDown, &QAction::triggered, this, &NpcDataEditorWindow::onMoveDown);
    connect(ui->actionDuplicateEntry, &QAction::triggered, this, &NpcDataEditorWindow::onDuplicateEntry);

    connect(ui->npcListWidget, &QListWidget::currentRowChanged, this, &NpcDataEditorWindow::onNpcSelectionChanged);
    connect(ui->objectListWidget, &QListWidget::currentRowChanged, this, &NpcDataEditorWindow::onObjectSelectionChanged);
    connect(npcResourceSectionList, &QListWidget::currentRowChanged,
        this, &NpcDataEditorWindow::onNpcResourceSelectionChanged);
    connect(ui->entityTabWidget, &QTabWidget::currentChanged, this, &NpcDataEditorWindow::onEntityTabChanged);

    connect(ui->nameEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->resourceIniEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->scriptEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->bodyIniEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->flyIniEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->flyIni2Edit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->flyInisEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->magicIniEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->deathScriptEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->actionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->pathFinderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->relationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->lumCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->dirSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->mapXSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->mapYSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->walkSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->standSpeedSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->dialogRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->visionRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->attackRadiusSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->lifeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->lifeMaxSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->thewSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->thewMaxSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->manaSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->manaMaxSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->attackSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->defendSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->evadeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->duckSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->expSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->levelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->levelUpExpSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->attackLevelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->magicLevelSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);
    connect(ui->stateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onNpcPropertyChanged);

    connect(ui->objNameEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objFileEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objScriptEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->wavFileEdit, &QLineEdit::textChanged, this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objKindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objLumCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objStateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objDirSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objMapXSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->objMapYSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->offsetXSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->offsetYSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->damageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->frameSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &NpcDataEditorWindow::onObjectPropertyChanged);
    connect(ui->actionTimeEdit, &QLineEdit::textChanged,
        this, &NpcDataEditorWindow::onObjectPropertyChanged);

    ui->npcToolBar->addAction(ui->actionOpen);
    ui->npcToolBar->addAction(ui->actionSave);
    ui->npcToolBar->addAction(ui->actionSaveAs);
    ui->npcToolBar->addSeparator();
    ui->npcToolBar->addAction(ui->actionAddEntry);
    ui->npcToolBar->addAction(ui->actionRemoveEntry);
    ui->npcToolBar->addAction(ui->actionMoveUp);
    ui->npcToolBar->addAction(ui->actionMoveDown);
    ui->npcToolBar->addSeparator();
    ui->npcToolBar->addAction(ui->actionDuplicateEntry);
    editDialogueAction = new QAction(this);
    editDialogueAction->setObjectName(
        QStringLiteral("editDialogueFromNpcAction"));
    editDialogueAction->setText(tr("编辑对话"));
    editDialogueAction->setToolTip(
        tr("打开当前 NPC 脚本引用的对话"));
    ui->npcToolBar->addSeparator();
    ui->npcToolBar->addAction(editDialogueAction);
    connect(editDialogueAction, &QAction::triggered,
            this, &NpcDataEditorWindow::onEditDialogueFromNpc);

    // NPC 资源/脚本/INI 字段文件选择按钮
    const QString iniFilter = tr("INI Files (*.ini);;All Files (*)");
    const QString scriptFilter = tr("Script Files (*.txt *.ini *.lua *.script);;All Files (*)");
    const QString objFileFilter = tr("OBJ Files (*.obj *_obj.ini);;INI Files (*.ini);;All Files (*)");
    const QString wavFilter = tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)");

    auto getAssetsPath = [this]() -> QString { return assetsBasePath; };
    FilePickerHelper::addEntityResourcePickerButton(ui->resourceIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::NpcIni, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->scriptEdit, scriptFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->bodyIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::BodyIni, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->flyIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::FlyIni, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->flyIni2Edit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::FlyIni, this);
    FilePickerHelper::addEntityResourceAppendPickerButton(ui->flyInisEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::FlyInis, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->magicIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::MagicIni, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->deathScriptEdit, scriptFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile, this);

    // OBJ 资源/脚本/音效字段文件选择按钮
    FilePickerHelper::addEntityResourcePickerButton(ui->objFileEdit, objFileFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ObjFile, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->objScriptEdit, scriptFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile, this);
    FilePickerHelper::addEntityResourcePickerButton(ui->wavFileEdit, wavFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::WavFile, this);

    ui->propertyTabWidget->setEnabled(false);
    updatePropertyTabVisibility();
}

NpcDataEditorWindow::~NpcDataEditorWindow()
{
    delete ui;
}

QList<DesktopRunDocumentSnapshot>
NpcDataEditorWindow::desktopRunDocumentSnapshots() const
{
    QList<DesktopRunDocumentSnapshot> snapshots;
    auto absoluteDocumentPath = [](const QString& path)
    {
        return path.isEmpty()
            ? QString()
            : EditorAssetPath::normalizedAbsolutePath(path);
    };

    if (isNpcFile)
    {
        DesktopRunDocumentSnapshot snapshot;
        snapshot.filePath = absoluteDocumentPath(currentNpcFilePath);
        snapshot.type = ProjectDocumentType::NpcList;
        snapshot.dirty = hasNpcUnsavedChanges;
        QList<NpcData> normalizedEntries;
        int invalidRow = -1;
        QString invalidFieldKey;
        QString invalidValue;
        if (normalizedNpcEntriesForSave(
                npcEntries,
                normalizedEntries,
                invalidRow,
                invalidFieldKey,
                invalidValue))
        {
            snapshot.serializationSupported = true;
            snapshot.bytes = textLinesToUtf8Bytes(
                serializeNpcLinesPreserving(normalizedEntries));
        }
        else
        {
            snapshot.diagnosticCode =
                QStringLiteral(
                    "editor_run.overlay.npc_list_snapshot_invalid");
        }
        snapshots.append(std::move(snapshot));
    }

    if (isObjectFile)
    {
        DesktopRunDocumentSnapshot snapshot;
        snapshot.filePath = absoluteDocumentPath(currentObjectFilePath);
        snapshot.type = ProjectDocumentType::ObjectList;
        snapshot.dirty = hasObjectUnsavedChanges;
        QList<ObjectData> normalizedEntries;
        int invalidRow = -1;
        QString invalidFieldKey;
        QString invalidValue;
        if (normalizedObjectEntriesForSave(
                objectEntries,
                normalizedEntries,
                invalidRow,
                invalidFieldKey,
                invalidValue))
        {
            snapshot.serializationSupported = true;
            snapshot.bytes = textLinesToUtf8Bytes(
                serializeObjectLinesPreserving(normalizedEntries));
        }
        else
        {
            snapshot.diagnosticCode =
                QStringLiteral(
                    "editor_run.overlay.object_list_snapshot_invalid");
        }
        snapshots.append(std::move(snapshot));
    }

    if (isNpcResourceFile)
    {
        DesktopRunDocumentSnapshot snapshot;
        snapshot.filePath =
            absoluteDocumentPath(currentNpcResourceFilePath);
        snapshot.type = ProjectDocumentType::NpcResource;
        snapshot.dirty = hasNpcResourceUnsavedChanges;
        snapshot.diagnosticCode =
            QStringLiteral(
                "editor_run.overlay.npc_resource_snapshot_unsupported");
        snapshots.append(std::move(snapshot));
    }

    return snapshots;
}

QList<ProjectDocumentState> NpcDataEditorWindow::currentProjectDocuments() const
{
    QList<ProjectDocumentState> documents;
    if (isNpcFile && !currentNpcFilePath.isEmpty())
    {
        documents.append(
            {currentNpcFilePath, ProjectDocumentType::NpcList,
             hasNpcUnsavedChanges});
    }
    if (isObjectFile && !currentObjectFilePath.isEmpty())
    {
        documents.append(
            {currentObjectFilePath, ProjectDocumentType::ObjectList,
             hasObjectUnsavedChanges});
    }
    if (isNpcResourceFile && !currentNpcResourceFilePath.isEmpty())
    {
        documents.append(
            {currentNpcResourceFilePath, ProjectDocumentType::NpcResource,
             hasNpcResourceUnsavedChanges});
    }
    return documents;
}

bool NpcDataEditorWindow::canAdoptDocumentPath(
    const QString& currentPath, const QString& targetPath) const
{
    return !documentPathValidator ||
        documentPathValidator(currentPath, targetPath);
}

void NpcDataEditorWindow::updateOverallDirtyStateAndNotify()
{
    hasUnsavedChanges = hasNpcUnsavedChanges || hasObjectUnsavedChanges ||
        hasNpcResourceUnsavedChanges;
    emit documentStatesChanged();
}

void NpcDataEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void NpcDataEditorWindow::retranslateDynamicUi()
{
    editDialogueAction->setText(tr("编辑对话"));
    editDialogueAction->setToolTip(
        tr("打开当前 NPC 脚本引用的对话"));
    auto setComboTexts = [this](QComboBox* combo,
                                const QList<QPair<int, QString>>& values)
    {
        QSet<QString> knownValues;
        for (const auto& value : values)
        {
            knownValues.insert(QString::number(value.first));
            const int index = combo->findData(value.first);
            if (index >= 0)
                combo->setItemText(index, value.second);
        }
        for (int index = 0; index < combo->count(); ++index)
        {
            const QVariant data = combo->itemData(index);
            if (!knownValues.contains(data.toString()))
                combo->setItemText(index, tr("%1 - 自定义值").arg(data.toString()));
        }
    };

    setComboTexts(ui->kindCombo, {
        {0, tr("0 - 普通")}, {1, tr("1 - 战斗")}, {2, tr("2 - 玩家")},
        {3, tr("3 - 伙伴")}, {4, tr("4 - 动物")}, {5, tr("5 - 事件")},
        {6, tr("6 - 避让玩家的动物")}, {7, tr("7 - 飞行动物")}});
    setComboTexts(ui->actionCombo, {
        {0, tr("0 - 无")}, {1, tr("1 - 漫游")}, {2, tr("2 - 前往")},
        {6, tr("6 - 原地站立（旧格式）")}});
    setComboTexts(ui->pathFinderCombo, {
        {0, tr("0 - 单步")}, {1, tr("1 - 最优")}});
    setComboTexts(ui->relationCombo, {
        {0, tr("0 - 友好")}, {1, tr("1 - 敌对")},
        {2, tr("2 - 中立")}, {3, tr("3 - 无关系")}});
    const QList<QPair<int, QString>> luminosityValues = {
        {0, tr("0 - 无")}, {1, tr("1 - 红")}, {2, tr("2 - 绿")},
        {3, tr("3 - 蓝")}, {4, tr("4 - 灰")}, {5, tr("5+ - 半透明")}};
    setComboTexts(ui->lumCombo, luminosityValues);
    setComboTexts(ui->objKindCombo, {
        {0, tr("0 - 装饰")}, {1, tr("1 - 箱子")}, {2, tr("2 - 尸体")},
        {3, tr("3 - 音源")}, {4, tr("4 - 随机音源")}, {5, tr("5 - 门")},
        {6, tr("6 - 陷阱")}, {7, tr("7 - 可拾取物")},
        {8, tr("8 - 可拾取物（旧格式）")}});
    setComboTexts(ui->objLumCombo, luminosityValues);
    setComboTexts(ui->objStateCombo, {
        {0, tr("0 - 停留")}, {1, tr("1 - 播放")},
        {2, tr("2 - 打开中")}, {3, tr("3 - 关闭中")}});

    npcResourceListHint->setText(
        tr("动作段按文件原顺序显示；未证实的段会明确标为自定义并原样保留。"));
    auto setFormLabel = [](QFormLayout* form, QWidget* field, const QString& text)
    {
        QLabel* label = qobject_cast<QLabel*>(form->labelForField(field));
        if (!label && form->parentWidget())
        {
            const auto labels = form->parentWidget()->findChildren<QLabel*>();
            for (QLabel* candidate : labels)
            {
                if (candidate->property("jxqyFormField").toString() == field->objectName())
                {
                    label = candidate;
                    break;
                }
            }
        }
        if (label)
            label->setText(text);
    };
    setFormLabel(npcResourceForm, npcResourceImageEdit, tr("动作图像"));
    setFormLabel(npcResourceForm, npcResourceShadeEdit, tr("阴影图像"));
    setFormLabel(npcResourceForm, npcResourceSoundEdit, tr("动作音效"));
    npcResourceExtraLabel->setText(tr("其他字段"));
    addNpcResourceExtraButton->setText(tr("添加字段"));
    removeNpcResourceExtraButton->setText(tr("删除字段"));
    npcResourceExtraTable->setHorizontalHeaderLabels({tr("字段"), tr("值")});
    npcResourceFooterHint->setText(tr(
        "帧数、方向、间隔和偏移来自 ASF/MPC 图像文件；NPCRes 只保存动作到图像/阴影/音效的映射。"));
    ui->entityTabWidget->setTabText(
        ui->entityTabWidget->indexOf(npcResourceListTab), tr("NPC资源"));
    ui->propertyTabWidget->setTabText(
        ui->propertyTabWidget->indexOf(npcResourcePropertyTab), tr("动作资源"));

    if (isNpcResourceFile)
    {
        refreshNpcResourceSections();
        showNpcResourceSection(npcResourceSectionList->currentRow());
    }
    else
    {
        npcResourcePreview->clearImage(tr("尚未打开 NPC 资源文件"));
        npcResourceMetadataLabel->setText(tr("未加载帧信息"));
    }
}

void NpcDataEditorWindow::setupNpcResourceEditor()
{
    npcResourceListTab = new QWidget(ui->entityTabWidget);
    npcResourceListTab->setObjectName(QStringLiteral("npcResourceListTab"));
    auto* listLayout = new QVBoxLayout(npcResourceListTab);
    npcResourceListHint = new QLabel(
        tr("动作段按文件原顺序显示；未证实的段会明确标为自定义并原样保留。"),
        npcResourceListTab);
    npcResourceListHint->setObjectName(QStringLiteral("npcResourceListHint"));
    npcResourceListHint->setWordWrap(true);
    npcResourceSectionList = new QListWidget(npcResourceListTab);
    npcResourceSectionList->setObjectName(QStringLiteral("npcResourceSectionList"));
    listLayout->addWidget(npcResourceListHint);
    listLayout->addWidget(npcResourceSectionList, 1);
    ui->entityTabWidget->addTab(npcResourceListTab, tr("NPC资源"));

    npcResourcePropertyTab = new QWidget(ui->propertyTabWidget);
    npcResourcePropertyTab->setObjectName(QStringLiteral("npcResourcePropertyTab"));
    auto* propertyLayout = new QVBoxLayout(npcResourcePropertyTab);

    npcResourceSectionLabel = new QLabel(npcResourcePropertyTab);
    npcResourceSectionLabel->setObjectName(QStringLiteral("npcResourceSectionLabel"));
    QFont sectionFont = npcResourceSectionLabel->font();
    sectionFont.setBold(true);
    sectionFont.setPointSize(sectionFont.pointSize() + 1);
    npcResourceSectionLabel->setFont(sectionFont);
    npcResourceStateLabel = new QLabel(npcResourcePropertyTab);
    npcResourceStateLabel->setObjectName(QStringLiteral("npcResourceStateLabel"));
    npcResourceStateLabel->setWordWrap(true);
    propertyLayout->addWidget(npcResourceSectionLabel);
    propertyLayout->addWidget(npcResourceStateLabel);

    npcResourceForm = new QFormLayout();
    npcResourceImageEdit = new QLineEdit(npcResourcePropertyTab);
    npcResourceImageEdit->setObjectName(QStringLiteral("npcResourceImageEdit"));
    npcResourceShadeEdit = new QLineEdit(npcResourcePropertyTab);
    npcResourceShadeEdit->setObjectName(QStringLiteral("npcResourceShadeEdit"));
    npcResourceSoundEdit = new QLineEdit(npcResourcePropertyTab);
    npcResourceSoundEdit->setObjectName(QStringLiteral("npcResourceSoundEdit"));
    npcResourceForm->addRow(tr("动作图像"), npcResourceImageEdit);
    npcResourceForm->addRow(tr("阴影图像"), npcResourceShadeEdit);
    npcResourceForm->addRow(tr("动作音效"), npcResourceSoundEdit);
    const QList<QWidget*> npcResourceFields = {
        npcResourceImageEdit, npcResourceShadeEdit, npcResourceSoundEdit};
    for (QWidget* field : npcResourceFields)
    {
        if (QWidget* label = npcResourceForm->labelForField(field))
            label->setProperty("jxqyFormField", field->objectName());
    }
    propertyLayout->addLayout(npcResourceForm);

    const QString imageFilter = tr(
        "动作图像 (*.asf *.mpc *.img);;所有文件 (*)");
    const QString shadeFilter = tr(
        "阴影图像 (*.shd);;所有文件 (*)");
    const QString soundFilter = tr(
        "音效文件 (*.wav *.mp3 *.ogg);;所有文件 (*)");
    auto getAssetsPath = [this]() -> QString { return assetsBasePath; };
    auto imageResourcePaths = [this]() -> QStringList
    {
        return {
            QDir(assetsBasePath).filePath("asf/character"),
            QDir(assetsBasePath).filePath("mpc/character"),
            QDir(assetsBasePath).filePath("asf/interlude"),
            QDir(assetsBasePath).filePath("mpc/interlude")
        };
    };
    auto imageDefaultPath = [this]() -> QString
    {
        const QStringList candidates = {
            QDir(assetsBasePath).filePath("asf/character"),
            QDir(assetsBasePath).filePath("mpc/character")
        };
        for (const QString& candidate : candidates)
        {
            if (QDir(candidate).exists())
                return candidate;
        }
        return candidates.front();
    };
    FilePickerHelper::addFilePickerButton(npcResourceImageEdit, npcResourceForm,
        imageFilter, getAssetsPath, true, this, imageDefaultPath, {}, false,
        imageResourcePaths);
    FilePickerHelper::addFilePickerButton(npcResourceShadeEdit, npcResourceForm,
        shadeFilter, getAssetsPath, true, this, imageDefaultPath, {}, false,
        imageResourcePaths);
    FilePickerHelper::addEntityResourcePickerButton(npcResourceSoundEdit, npcResourceForm,
        soundFilter, getAssetsPath,
        FilePickerHelper::EntityResourceField::WavFile, this);

    auto installNpcResourceDrop =
        [this, imageResourcePaths](QLineEdit* edit,
                                  const QSet<QString>& allowedSuffixes,
                                  bool sound)
    {
        AssetDragDrop::installLineEditTarget(edit,
            [this, imageResourcePaths, allowedSuffixes, sound](
                const AssetDragDrop::Payload& payload)
            {
                if (EditorAssetPath::logicalComparisonKey(payload.sourceRoot) !=
                    EditorAssetPath::logicalComparisonKey(assetsBasePath))
                {
                    return AssetDragDrop::DropResult{false, {},
                        tr("NPC 资源只接受当前活动资源包中的文件。")};
                }

                const QString suffix =
                    QFileInfo(payload.relativePath).suffix().toLower();
                if (!allowedSuffixes.contains(suffix))
                {
                    return AssetDragDrop::DropResult{false, {}, sound
                        ? tr("动作音效只接受 WAV、MP3 或 OGG 文件。")
                        : tr("该文件类型与当前 NPC 资源字段不匹配。")};
                }

                QString resourceReference;
                const bool validLocation = sound
                    ? FilePickerHelper::makeResourceReference(
                          assetsBasePath,
                          QDir(assetsBasePath).filePath("sound"),
                          payload.absolutePath, resourceReference)
                    : FilePickerHelper::makeResourceReference(
                          assetsBasePath, imageResourcePaths(),
                          payload.absolutePath, resourceReference);
                if (!validLocation)
                {
                    return AssetDragDrop::DropResult{false, {}, sound
                        ? tr("动作音效必须位于当前资源包的 sound 文件夹内。")
                        : tr("动作图像和阴影必须位于 character 或 interlude 资源目录内。")};
                }
                return AssetDragDrop::DropResult{
                    true, resourceReference, {}};
            });
    };
    installNpcResourceDrop(npcResourceImageEdit,
        {QStringLiteral("mpc"), QStringLiteral("asf"),
         QStringLiteral("img")}, false);
    installNpcResourceDrop(npcResourceShadeEdit,
        {QStringLiteral("shd")}, false);
    installNpcResourceDrop(npcResourceSoundEdit,
        {QStringLiteral("wav"), QStringLiteral("mp3"),
         QStringLiteral("ogg")}, true);

    auto* extraHeader = new QHBoxLayout();
    npcResourceExtraLabel = new QLabel(tr("其他字段"), npcResourcePropertyTab);
    npcResourceExtraLabel->setObjectName(QStringLiteral("npcResourceExtraLabel"));
    addNpcResourceExtraButton = new QPushButton(tr("添加字段"), npcResourcePropertyTab);
    addNpcResourceExtraButton->setObjectName(
        QStringLiteral("addNpcResourceExtraButton"));
    removeNpcResourceExtraButton = new QPushButton(
        tr("删除字段"), npcResourcePropertyTab);
    removeNpcResourceExtraButton->setObjectName(
        QStringLiteral("removeNpcResourceExtraButton"));
    extraHeader->addWidget(npcResourceExtraLabel);
    extraHeader->addStretch(1);
    extraHeader->addWidget(addNpcResourceExtraButton);
    extraHeader->addWidget(removeNpcResourceExtraButton);
    propertyLayout->addLayout(extraHeader);

    npcResourceExtraTable = new QTableWidget(0, 2, npcResourcePropertyTab);
    npcResourceExtraTable->setObjectName(QStringLiteral("npcResourceExtraTable"));
    npcResourceExtraTable->setHorizontalHeaderLabels({tr("字段"), tr("值")});
    npcResourceExtraTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    npcResourceExtraTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    npcResourceExtraTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    npcResourceExtraTable->setSelectionMode(QAbstractItemView::SingleSelection);
    propertyLayout->addWidget(npcResourceExtraTable);

    npcResourcePreview = new MpcPreviewLabel(npcResourcePropertyTab);
    npcResourcePreview->setObjectName(QStringLiteral("npcResourcePreview"));
    npcResourcePreview->setFrameShape(QFrame::StyledPanel);
    npcResourceMetadataLabel = new QLabel(npcResourcePropertyTab);
    npcResourceMetadataLabel->setObjectName(
        QStringLiteral("npcResourceMetadataLabel"));
    npcResourceMetadataLabel->setWordWrap(true);
    propertyLayout->addWidget(npcResourcePreview);
    propertyLayout->addWidget(npcResourceMetadataLabel);
    npcResourceFooterHint = new QLabel(
        tr("帧数、方向、间隔和偏移来自 ASF/MPC 图像文件；NPCRes 只保存动作到图像/阴影/音效的映射。"),
        npcResourcePropertyTab);
    npcResourceFooterHint->setObjectName(QStringLiteral("npcResourceFooterHint"));
    npcResourceFooterHint->setWordWrap(true);
    propertyLayout->addWidget(npcResourceFooterHint);

    ui->propertyTabWidget->addTab(npcResourcePropertyTab, tr("动作资源"));

    connect(npcResourceImageEdit, &QLineEdit::textChanged,
        this, &NpcDataEditorWindow::onNpcResourcePropertyChanged);
    connect(npcResourceShadeEdit, &QLineEdit::textChanged,
        this, &NpcDataEditorWindow::onNpcResourcePropertyChanged);
    connect(npcResourceSoundEdit, &QLineEdit::textChanged,
        this, &NpcDataEditorWindow::onNpcResourcePropertyChanged);
    connect(npcResourceExtraTable, &QTableWidget::cellChanged,
        this, &NpcDataEditorWindow::onNpcResourceExtraValueChanged);
    connect(addNpcResourceExtraButton, &QPushButton::clicked,
        this, &NpcDataEditorWindow::onAddNpcResourceExtraKey);
    connect(removeNpcResourceExtraButton, &QPushButton::clicked,
        this, &NpcDataEditorWindow::onRemoveNpcResourceExtraKey);

    npcResourcePreview->clearImage(tr("尚未打开 NPC 资源文件"));
    npcResourceMetadataLabel->setText(tr("未加载帧信息"));
}

void NpcDataEditorWindow::initializeCombos()
{
    ui->kindCombo->addItem(tr("0 - 普通"), 0);
    ui->kindCombo->addItem(tr("1 - 战斗"), 1);
    ui->kindCombo->addItem(tr("2 - 玩家"), 2);
    ui->kindCombo->addItem(tr("3 - 伙伴"), 3);
    ui->kindCombo->addItem(tr("4 - 动物"), 4);
    ui->kindCombo->addItem(tr("5 - 事件"), 5);
    ui->kindCombo->addItem(tr("6 - 避让玩家的动物"), 6);
    ui->kindCombo->addItem(tr("7 - 飞行动物"), 7);

    ui->actionCombo->addItem(tr("0 - 无"), 0);
    ui->actionCombo->addItem(tr("1 - 漫游"), 1);
    ui->actionCombo->addItem(tr("2 - 前往"), 2);
    ui->actionCombo->addItem(tr("6 - 原地站立（旧格式）"), 6);

    ui->pathFinderCombo->addItem(tr("0 - 单步"), 0);
    ui->pathFinderCombo->addItem(tr("1 - 最优"), 1);

    ui->relationCombo->addItem(tr("0 - 友好"), 0);
    ui->relationCombo->addItem(tr("1 - 敌对"), 1);
    ui->relationCombo->addItem(tr("2 - 中立"), 2);
    ui->relationCombo->addItem(tr("3 - 无关系"), 3);

    ui->lumCombo->addItem(tr("0 - 无"), 0);
    ui->lumCombo->addItem(tr("1 - 红"), 1);
    ui->lumCombo->addItem(tr("2 - 绿"), 2);
    ui->lumCombo->addItem(tr("3 - 蓝"), 3);
    ui->lumCombo->addItem(tr("4 - 灰"), 4);
    ui->lumCombo->addItem(tr("5+ - 半透明"), 5);

    ui->objKindCombo->addItem(tr("0 - 装饰"), 0);
    ui->objKindCombo->addItem(tr("1 - 箱子"), 1);
    ui->objKindCombo->addItem(tr("2 - 尸体"), 2);
    ui->objKindCombo->addItem(tr("3 - 音源"), 3);
    ui->objKindCombo->addItem(tr("4 - 随机音源"), 4);
    ui->objKindCombo->addItem(tr("5 - 门"), 5);
    ui->objKindCombo->addItem(tr("6 - 陷阱"), 6);
    ui->objKindCombo->addItem(tr("7 - 可拾取物"), 7);
    ui->objKindCombo->addItem(tr("8 - 可拾取物（旧格式）"), 8);

    ui->objLumCombo->addItem(tr("0 - 无"), 0);
    ui->objLumCombo->addItem(tr("1 - 红"), 1);
    ui->objLumCombo->addItem(tr("2 - 绿"), 2);
    ui->objLumCombo->addItem(tr("3 - 蓝"), 3);
    ui->objLumCombo->addItem(tr("4 - 灰"), 4);
    ui->objLumCombo->addItem(tr("5+ - 半透明"), 5);

    ui->objStateCombo->addItem(tr("0 - 停留"), 0);
    ui->objStateCombo->addItem(tr("1 - 播放"), 1);
    ui->objStateCombo->addItem(tr("2 - 打开中"), 2);
    ui->objStateCombo->addItem(tr("3 - 关闭中"), 3);
}

ClosePlan NpcDataEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    if (!hasUnsavedChanges)
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }

    const int result = QMessageBox::question(
        const_cast<NpcDataEditorWindow*>(this),
        tr("保存更改"),
        tr("NPC/物体/NPC资源数据已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (result == QMessageBox::Cancel)
        plan.decisions.append(CloseDecision::Cancelled);
    else if (result == QMessageBox::Yes)
        plan.decisions.append(CloseDecision::Save);
    else
        plan.decisions.append(CloseDecision::Discard);
    return plan;
}

bool NpcDataEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return false;
    if (plan.decisions.front() == CloseDecision::Save)
        return saveAllModified();
    return true;
}

void NpcDataEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return;
    allowPreparedClose();
}

NpcDataEditorWindow::SaveConfirmResult NpcDataEditorWindow::confirmSaveIfModified()
{
    if (!hasUnsavedChanges)
        return SaveConfirmResult::Discarded;

    int result = QMessageBox::question(this,
        tr("保存更改"),
        tr("NPC/物体/NPC资源数据已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Cancel)
        return SaveConfirmResult::Cancelled;

    if (result == QMessageBox::No)
        return SaveConfirmResult::Discarded;

    if (!saveAllModified())
        return SaveConfirmResult::Cancelled;

    return SaveConfirmResult::Saved;
}

void NpcDataEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose())
    {
        event->accept();
        emit documentClosed();
        return;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
    {
        event->ignore();
        return;
    }
    event->accept();
    emit documentClosed();
}

bool NpcDataEditorWindow::saveAllModified()
{
    if (isNpcFile && isObjectFile &&
        (hasNpcUnsavedChanges || hasObjectUnsavedChanges) &&
        pathsReferToSameTarget(currentNpcFilePath, currentObjectFilePath))
    {
        QMessageBox::warning(this, tr("保存目标冲突"),
            tr("NPC 与物体文档指向同一文件，无法安全地依次保存。\n%1")
                .arg(currentNpcFilePath));
        return false;
    }
    if (hasNpcResourceUnsavedChanges && isNpcResourceFile &&
        ((isNpcFile && pathsReferToSameTarget(
            currentNpcResourceFilePath, currentNpcFilePath)) ||
         (isObjectFile && pathsReferToSameTarget(
            currentNpcResourceFilePath, currentObjectFilePath))))
    {
        QMessageBox::warning(this, tr("保存目标冲突"),
            tr("NPC资源文档不能与 NPC/物体文档使用同一保存目标。\n%1")
                .arg(currentNpcResourceFilePath));
        return false;
    }

    QStringList targetPaths;
    if (hasNpcUnsavedChanges && isNpcFile)
    {
        collectNpcProperties();
        if (!normalizeNpcResourceReferencesForSave())
            return false;
        targetPaths.append(currentNpcFilePath);
    }
    if (hasObjectUnsavedChanges && isObjectFile)
    {
        collectObjectProperties();
        if (!normalizeObjectResourceReferencesForSave())
            return false;
        targetPaths.append(currentObjectFilePath);
    }
    if (hasNpcResourceUnsavedChanges && isNpcResourceFile)
    {
        if (!validateAndNormalizeNpcResourceDocument())
            return false;
        targetPaths.append(currentNpcResourceFilePath);
    }

    if (targetPaths.isEmpty())
        return true;
    auto groupMutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(targetPaths.front());
    if (!groupMutationLease)
        return false;
    for (int index = 1; index < targetPaths.size(); ++index)
    {
        if (!groupMutationLease.addResourcePath(targetPaths[index]))
            return false;
    }

    const QString transactionRoot = transactionRootForTargets(
        assetsBasePath, targetPaths);
    if (transactionRoot.isEmpty())
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法确定 NPC/物体同代事务的公共资源根。"));
        return false;
    }

    DurableFileTransaction transaction(transactionRoot);
    const QString transactionId = QUuid::createUuid().toString(QUuid::Id128);
    QString transactionError;
    QStringList savedNpcLines;
    QStringList savedObjectLines;
    QByteArray savedNpcResourceData;

    auto prepareDocument = [&](const QString& targetPath,
                               int documentKind)
    {
        const QString temporaryPath = targetPath + ".tmp." + transactionId;
        bool serialized = false;
        if (documentKind == 0)
        {
            serialized = saveNpcToFilePreserving(
                temporaryPath, false, &savedNpcLines);
        }
        else if (documentKind == 1)
        {
            serialized = saveObjectToFilePreserving(
                temporaryPath, false, &savedObjectLines);
        }
        else
        {
            serialized = saveNpcResourceToFile(
                temporaryPath, false, &savedNpcResourceData);
        }
        if (!serialized)
        {
            QFile::remove(temporaryPath);
            transactionError = tr("无法序列化文件: %1").arg(targetPath);
            return false;
        }
        if (!transaction.addPreparedWrite(
                targetPath, temporaryPath, transactionError))
        {
            QFile::remove(temporaryPath);
            return false;
        }
        return true;
    };

    if ((hasNpcUnsavedChanges && isNpcFile &&
            !prepareDocument(currentNpcFilePath, 0)) ||
        (hasObjectUnsavedChanges && isObjectFile &&
            !prepareDocument(currentObjectFilePath, 1)) ||
        (hasNpcResourceUnsavedChanges && isNpcResourceFile &&
            !prepareDocument(currentNpcResourceFilePath, 2)))
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("无法准备 NPC/物体同代事务：\n%1").arg(transactionError));
        return false;
    }

    if (!transaction.commit(transactionError))
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("NPC/物体同代事务提交失败：\n%1").arg(transactionError));
        return false;
    }
    if (!transactionError.isEmpty())
        QMessageBox::warning(this, tr("事务清理警告"), transactionError);

    if (hasNpcUnsavedChanges && isNpcFile)
    {
        originalNpcLines = savedNpcLines;
        normalizeNpcSectionIds();
        hasNpcUnsavedChanges = false;
    }
    if (hasObjectUnsavedChanges && isObjectFile)
    {
        originalObjectLines = savedObjectLines;
        normalizeObjectSectionIds();
        hasObjectUnsavedChanges = false;
    }
    if (hasNpcResourceUnsavedChanges && isNpcResourceFile)
    {
        npcResourceDocument->loadFromString(
            std::string(savedNpcResourceData.constData(),
                static_cast<size_t>(savedNpcResourceData.size())));
        hasNpcResourceUnsavedChanges = false;
    }
    updateOverallDirtyStateAndNotify();
    ProjectManager::instance().markDirty();
    return true;
}

bool NpcDataEditorWindow::openFile(const QString& fileName)
{
    const bool openingNpcResource = isNpcResourceFilePath(fileName);
    if (openingNpcResource &&
        (((isNpcFile && pathsReferToSameTarget(
               fileName, currentNpcFilePath)) ||
          (isObjectFile && pathsReferToSameTarget(
               fileName, currentObjectFilePath))) ||
         !canAdoptDocumentPath(currentNpcResourceFilePath, fileName)))
    {
        return false;
    }
    if (!openingNpcResource &&
        (fileName.endsWith(".npc", Qt::CaseInsensitive) ||
         fileName.endsWith("_npc.ini", Qt::CaseInsensitive)) &&
        !canAdoptDocumentPath(currentNpcFilePath, fileName))
    {
        return false;
    }
    if (!openingNpcResource &&
        (fileName.endsWith(".obj", Qt::CaseInsensitive) ||
         fileName.endsWith("_obj.ini", Qt::CaseInsensitive)) &&
        !canAdoptDocumentPath(currentObjectFilePath, fileName))
    {
        return false;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    if (openingNpcResource)
    {
        if (!loadNpcResourceFromFile(fileName, true))
            return false;
        ui->entityTabWidget->setCurrentIndex(2);
        updatePropertyTabVisibility();
    }
    else if (fileName.endsWith(".npc", Qt::CaseInsensitive) ||
        fileName.endsWith("_npc.ini", Qt::CaseInsensitive))
    {
        if (!loadNpcFromFile(fileName, true))
            return false;
        ui->entityTabWidget->setCurrentIndex(0);
        updatePropertyTabVisibility();
    }
    else if (fileName.endsWith(".obj", Qt::CaseInsensitive) ||
             fileName.endsWith("_obj.ini", Qt::CaseInsensitive))
    {
        if (!loadObjectFromFile(fileName, true))
            return false;
        ui->entityTabWidget->setCurrentIndex(1);
        updatePropertyTabVisibility();
    }
    else
    {
        return false;
    }
    return true;
}

bool NpcDataEditorWindow::isNpcResourceFilePath(const QString& fileName)
{
    QString normalized = QDir::fromNativeSeparators(
        QFileInfo(fileName).absoluteFilePath()).toLower();
    return normalized.endsWith(".ini") &&
        (normalized.contains("/ini/npcres/") ||
         QFileInfo(fileName).dir().dirName().compare(
             "npcres", Qt::CaseInsensitive) == 0);
}

bool NpcDataEditorWindow::openNpcFile(
    const QString& fileName, bool reportLoadErrors)
{
    if (!canAdoptDocumentPath(currentNpcFilePath, fileName))
        return false;
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    if (!loadNpcFromFile(fileName, reportLoadErrors))
        return false;
    ui->entityTabWidget->setCurrentIndex(0);
    updatePropertyTabVisibility();
    return true;
}

bool NpcDataEditorWindow::openObjectFile(
    const QString& fileName, bool reportLoadErrors)
{
    if (!canAdoptDocumentPath(currentObjectFilePath, fileName))
        return false;
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    if (!loadObjectFromFile(fileName, reportLoadErrors))
        return false;
    ui->entityTabWidget->setCurrentIndex(1);
    updatePropertyTabVisibility();
    return true;
}

bool NpcDataEditorWindow::openNpcResourceFile(
    const QString& fileName, bool reportLoadErrors)
{
    if ((isNpcFile &&
         pathsReferToSameTarget(fileName, currentNpcFilePath)) ||
        (isObjectFile &&
         pathsReferToSameTarget(fileName, currentObjectFilePath)) ||
        !canAdoptDocumentPath(currentNpcResourceFilePath, fileName))
    {
        return false;
    }
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;
    if (!loadNpcResourceFromFile(fileName, reportLoadErrors))
        return false;
    ui->entityTabWidget->setCurrentIndex(2);
    updatePropertyTabVisibility();
    return true;
}

bool NpcDataEditorWindow::setAssetsBasePath(const QString& path)
{
    const Decision decision = prepareAssetsPathSwitch(path);
    if (decision == Decision::Cancelled || !resolveAssetsPathSwitch(decision))
        return false;
    commitAssetsPathSwitch(path);
    return true;
}

AssetsPathSwitchParticipant::Decision NpcDataEditorWindow::prepareAssetsPathSwitch(
    const QString& path) const
{
    if (QDir::cleanPath(assetsBasePath).compare(
            QDir::cleanPath(path),
#ifdef Q_OS_WIN
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
            ) == 0)
    {
        return Decision::Ready;
    }

    if (hasUnsavedChanges)
    {
        const int result = QMessageBox::question(const_cast<NpcDataEditorWindow*>(this),
            tr("保存更改"), tr("NPC/物体/NPC资源数据已修改，是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (result == QMessageBox::Cancel)
            return Decision::Cancelled;
        return result == QMessageBox::Yes ? Decision::Save : Decision::Discard;
    }
    return Decision::Ready;
}

bool NpcDataEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    if (decision == Decision::Cancelled)
        return false;
    if (decision == Decision::Save)
        return saveAllModified();
    return true;
}

void NpcDataEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    if (QDir::cleanPath(assetsBasePath).compare(
            QDir::cleanPath(path),
#ifdef Q_OS_WIN
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
            ) == 0)
    {
        assetsBasePath = path;
        return;
    }
    if (isNpcResourceFile &&
        ((isNpcFile && pathsReferToSameTarget(
            currentNpcResourceFilePath, currentNpcFilePath)) ||
         (isObjectFile && pathsReferToSameTarget(
            currentNpcResourceFilePath, currentObjectFilePath))))
    {
        QMessageBox::warning(this, tr("保存目标冲突"),
            tr("NPC资源文档不能与 NPC/物体文档使用同一保存目标。\n%1")
                .arg(currentNpcResourceFilePath));
        return;
    }

    if (isNpcFile || isObjectFile || isNpcResourceFile || hasUnsavedChanges)
    {
        npcEntries.clear();
        objectEntries.clear();
        originalNpcLines.clear();
        originalObjectLines.clear();
        currentNpcFilePath.clear();
        currentNpcMapName.clear();
        currentObjectFilePath.clear();
        currentNpcResourceFilePath.clear();
        isNpcFile = false;
        isObjectFile = false;
        isNpcResourceFile = false;
        currentNpcEditRow = -1;
        currentObjectEditRow = -1;
        nextNpcSectionId = 0;
        nextObjectSectionId = 0;
        hasNpcUnsavedChanges = false;
        hasObjectUnsavedChanges = false;
        hasNpcResourceUnsavedChanges = false;
        hasUnsavedChanges = false;
        npcResourceDocument = std::make_unique<INIFileEditor>();
        npcResourceSectionOrder.clear();
        refreshNpcList();
        refreshObjectList();
        refreshNpcResourceSections();
        ui->propertyTabWidget->setEnabled(false);
    }
    assetsBasePath = path;
    npcResourceImageCache->setAssetsBasePath(path.toUtf8().toStdString());
    npcResourceImageCache->clearCache();
    updateOverallDirtyStateAndNotify();
}

QString NpcDataEditorWindow::currentAssetsPath() const
{
    return assetsBasePath;
}

void NpcDataEditorWindow::onOpen()
{
    const bool openingNpcResource =
        ui->entityTabWidget->currentIndex() == 2;
    const QString filter = openingNpcResource
        ? tr("NPC资源文件 (*.ini);;所有文件 (*)")
        : tr("NPC/物体文件 (*.npc *.obj *_npc.ini *_obj.ini);;所有文件 (*)");
    const QString initialDirectory = openingNpcResource && !assetsBasePath.isEmpty()
        ? QDir(assetsBasePath).filePath("ini/npcres")
        : QString();
    QString fileName = QFileDialog::getOpenFileName(
        this,
        openingNpcResource
            ? tr("打开 NPC 资源文件")
            : tr("打开NPC/物体数据文件"),
        initialDirectory,
        filter,
        nullptr,
        QFileDialog::DontResolveSymlinks);

    if (fileName.isEmpty())
        return;

    if (openingNpcResource)
        openNpcResourceFile(fileName);
    else
        openFile(fileName);
}

void NpcDataEditorWindow::onSave()
{
    if (isNpcFile && isObjectFile &&
        pathsReferToSameTarget(currentNpcFilePath, currentObjectFilePath))
    {
        QMessageBox::warning(this, tr("保存目标冲突"),
            tr("NPC 与物体文档指向同一文件；请先将其中一个文档另存到不同目标。\n%1")
                .arg(currentNpcFilePath));
        return;
    }

    if (ui->entityTabWidget->currentIndex() == 0 && isNpcFile)
    {
        collectNpcProperties();
        if (!normalizeNpcResourceReferencesForSave())
            return;
        if (!saveNpcToFilePreserving(currentNpcFilePath))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法保存NPC文件: %1").arg(currentNpcFilePath));
            return;
        }
        hasNpcUnsavedChanges = false;
        updateOverallDirtyStateAndNotify();
        ProjectManager::instance().markDirty();
    }
    else if (ui->entityTabWidget->currentIndex() == 1 && isObjectFile)
    {
        collectObjectProperties();
        if (!normalizeObjectResourceReferencesForSave())
            return;
        if (!saveObjectToFilePreserving(currentObjectFilePath))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法保存物体文件: %1").arg(currentObjectFilePath));
            return;
        }
        hasObjectUnsavedChanges = false;
        updateOverallDirtyStateAndNotify();
        ProjectManager::instance().markDirty();
    }
    else if (ui->entityTabWidget->currentIndex() == 2 && isNpcResourceFile)
    {
        if (!validateAndNormalizeNpcResourceDocument())
            return;
        if (!saveNpcResourceToFile(currentNpcResourceFilePath))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法保存 NPC 资源文件: %1")
                    .arg(currentNpcResourceFilePath));
            return;
        }
        hasNpcResourceUnsavedChanges = false;
        updateOverallDirtyStateAndNotify();
        ProjectManager::instance().markDirty();
    }
    else
    {
        onSaveAs();
    }
}

void NpcDataEditorWindow::onSaveAs()
{
    QString filter;
    const int documentKind = ui->entityTabWidget->currentIndex();
    if (documentKind == 0)
    {
        filter = tr("NPC文件 (*.npc *_npc.ini);;所有文件 (*)");
    }
    else if (documentKind == 1)
    {
        filter = tr("物体文件 (*.obj *_obj.ini);;所有文件 (*)");
    }
    else
    {
        filter = tr("NPC资源文件 (*.ini);;所有文件 (*)");
    }

    QString fileName = QFileDialog::getSaveFileName(this,
        documentKind == 2
            ? tr("保存 NPC 资源文件")
            : tr("保存NPC/物体数据文件"),
        documentKind == 2 && !assetsBasePath.isEmpty()
            ? QDir(assetsBasePath).filePath("ini/npcres")
            : QString(),
        filter);

    if (fileName.isEmpty())
        return;

    QStringList otherTargets;
    if (documentKind != 0 && isNpcFile)
        otherTargets.append(currentNpcFilePath);
    if (documentKind != 1 && isObjectFile)
        otherTargets.append(currentObjectFilePath);
    if (documentKind != 2 && isNpcResourceFile)
        otherTargets.append(currentNpcResourceFilePath);
    bool conflicts = false;
    for (const QString& otherTarget : otherTargets)
        conflicts = conflicts || pathsReferToSameTarget(fileName, otherTarget);
    if (conflicts)
    {
        QMessageBox::warning(this, tr("保存目标冲突"),
            tr("NPC、物体与 NPC资源文档不能使用同一保存目标。\n%1")
                .arg(fileName));
        return;
    }

    if (documentKind == 0)
    {
        if (!saveNpcFileAs(fileName))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法保存NPC文件: %1").arg(fileName));
            return;
        }
    }
    else if (documentKind == 1)
    {
        if (!saveObjectFileAs(fileName))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法保存物体文件: %1").arg(fileName));
            return;
        }
    }
    else
    {
        if (!saveNpcResourceFileAs(fileName))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法保存 NPC 资源文件: %1").arg(fileName));
            return;
        }
    }

    updateOverallDirtyStateAndNotify();
}

bool NpcDataEditorWindow::saveNpcFileAs(const QString& fileName)
{
    if (fileName.trimmed().isEmpty() ||
        (isObjectFile &&
         pathsReferToSameTarget(fileName, currentObjectFilePath)) ||
        (isNpcResourceFile &&
         pathsReferToSameTarget(fileName, currentNpcResourceFilePath)) ||
        !canAdoptDocumentPath(currentNpcFilePath, fileName))
    {
        return false;
    }

    collectNpcProperties();
    if (!normalizeNpcResourceReferencesForSave() ||
        !saveNpcToFilePreserving(fileName))
    {
        return false;
    }

    currentNpcFilePath = fileName;
    isNpcFile = true;
    hasNpcUnsavedChanges = false;
    updateOverallDirtyStateAndNotify();
    ProjectManager::instance().markDirty();
    return true;
}

bool NpcDataEditorWindow::saveObjectFileAs(const QString& fileName)
{
    if (fileName.trimmed().isEmpty() ||
        (isNpcFile && pathsReferToSameTarget(fileName, currentNpcFilePath)) ||
        (isNpcResourceFile &&
         pathsReferToSameTarget(fileName, currentNpcResourceFilePath)) ||
        !canAdoptDocumentPath(currentObjectFilePath, fileName))
    {
        return false;
    }

    collectObjectProperties();
    if (!normalizeObjectResourceReferencesForSave() ||
        !saveObjectToFilePreserving(fileName))
    {
        return false;
    }

    currentObjectFilePath = fileName;
    isObjectFile = true;
    hasObjectUnsavedChanges = false;
    updateOverallDirtyStateAndNotify();
    ProjectManager::instance().markDirty();
    return true;
}

bool NpcDataEditorWindow::saveNpcResourceFileAs(const QString& fileName)
{
    if (fileName.trimmed().isEmpty() ||
        (isNpcFile &&
         pathsReferToSameTarget(fileName, currentNpcFilePath)) ||
        (isObjectFile &&
         pathsReferToSameTarget(fileName, currentObjectFilePath)) ||
        !canAdoptDocumentPath(currentNpcResourceFilePath, fileName))
    {
        return false;
    }

    if (!validateAndNormalizeNpcResourceDocument() ||
        !saveNpcResourceToFile(fileName))
    {
        return false;
    }

    currentNpcResourceFilePath = fileName;
    isNpcResourceFile = true;
    hasNpcResourceUnsavedChanges = false;
    updateOverallDirtyStateAndNotify();
    ProjectManager::instance().markDirty();
    return true;
}

void NpcDataEditorWindow::onAddEntry()
{
    const int documentKind = ui->entityTabWidget->currentIndex();
    if (documentKind == 2)
    {
        QStringList choices;
        for (const QString& section : canonicalNpcResourceSections())
        {
            if (!npcResourceDocument->hasSection(
                    section.toUtf8().toStdString()))
            {
                choices.append(section);
            }
        }
        choices.append(tr("自定义动作段..."));
        bool accepted = false;
        QString sectionName = QInputDialog::getItem(this,
            tr("添加 NPC 动作段"), tr("动作段"), choices, 0, false,
            &accepted);
        if (!accepted || sectionName.isEmpty())
            return;
        if (sectionName == tr("自定义动作段..."))
        {
            sectionName = QInputDialog::getText(this,
                tr("添加自定义动作段"), tr("段名"), QLineEdit::Normal,
                QString(), &accepted).trimmed();
            if (!accepted || sectionName.isEmpty())
                return;
        }
        if (!addNpcResourceSection(sectionName))
        {
            QMessageBox::warning(this, tr("添加失败"),
                tr("动作段名称为空、包含非法字符或已经存在：%1")
                    .arg(sectionName));
        }
        return;
    }
    if (documentKind == 0)
    {
        collectNpcProperties();
        NpcData data;
        data.sectionId = nextNpcSectionId++;
        data.properties["Name"] = tr("新NPC");
        data.properties["Kind"] = "0";
        data.properties["Dir"] = "0";
        data.properties["MapX"] = "0";
        data.properties["MapY"] = "0";
        data.properties["Action"] = "0";
        data.properties["WalkSpeed"] = "1";
        data.properties["StandSpeed"] = "0";
        data.properties["PathFinder"] = "0";
        data.properties["DialogRadius"] = "1";
        data.properties["Relation"] = "0";
        data.properties["Life"] = "100";
        data.properties["LifeMax"] = "100";
        data.properties["LevelUpExp"] = "0";
        data.properties["AttackLevel"] = "0";
        data.properties["MagicLevel"] = "0";
        npcEntries.append(data);
        ui->propertyTabWidget->setEnabled(true);
        refreshNpcList();
        ui->npcListWidget->setCurrentRow(npcEntries.size() - 1);
    }
    else
    {
        collectObjectProperties();
        ObjectData data;
        data.sectionId = nextObjectSectionId++;
        data.properties["ObjName"] = tr("新物体");
        data.properties["Kind"] = "0";
        data.properties["Dir"] = "0";
        data.properties["MapX"] = "0";
        data.properties["MapY"] = "0";
        data.properties["OffsetX"] = "0";
        data.properties["OffsetY"] = "0";
        data.properties["Lum"] = "0";
        data.properties["Damage"] = "0";
        data.properties["Frame"] = "0";
        data.properties["State"] = "0";
        data.properties["ActionTime"] = "0";
        objectEntries.append(data);
        ui->propertyTabWidget->setEnabled(true);
        refreshObjectList();
        ui->objectListWidget->setCurrentRow(objectEntries.size() - 1);
    }

    if (documentKind == 0)
        hasNpcUnsavedChanges = true;
    else
        hasObjectUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
}

void NpcDataEditorWindow::onRemoveEntry()
{
    const int documentKind = ui->entityTabWidget->currentIndex();
    if (documentKind == 2)
    {
        if (npcResourceSectionList->currentRow() < 0)
            return;
        if (QMessageBox::question(this, tr("删除动作段"),
                tr("确定删除动作段 [%1] 及其全部字段吗？")
                    .arg(currentNpcResourceSection()),
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        {
            removeCurrentNpcResourceSection();
        }
        return;
    }
    if (documentKind == 0)
    {
        int row = ui->npcListWidget->currentRow();
        if (row >= 0 && row < npcEntries.size())
        {
            npcEntries.removeAt(row);
            currentNpcEditRow = -1;
            refreshNpcList();
            hasNpcUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
    else
    {
        int row = ui->objectListWidget->currentRow();
        if (row >= 0 && row < objectEntries.size())
        {
            objectEntries.removeAt(row);
            currentObjectEditRow = -1;
            refreshObjectList();
            hasObjectUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
}

void NpcDataEditorWindow::onMoveUp()
{
    const int documentKind = ui->entityTabWidget->currentIndex();
    if (documentKind == 2)
        return;
    if (documentKind == 0)
    {
        collectNpcProperties();
        int row = ui->npcListWidget->currentRow();
        if (row > 0)
        {
            npcEntries.swapItemsAt(row, row - 1);
            currentNpcEditRow = -1;
            refreshNpcList();
            ui->npcListWidget->setCurrentRow(row - 1);
            hasNpcUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
    else
    {
        collectObjectProperties();
        int row = ui->objectListWidget->currentRow();
        if (row > 0)
        {
            objectEntries.swapItemsAt(row, row - 1);
            currentObjectEditRow = -1;
            refreshObjectList();
            ui->objectListWidget->setCurrentRow(row - 1);
            hasObjectUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
}

void NpcDataEditorWindow::onMoveDown()
{
    const int documentKind = ui->entityTabWidget->currentIndex();
    if (documentKind == 2)
        return;
    if (documentKind == 0)
    {
        collectNpcProperties();
        int row = ui->npcListWidget->currentRow();
        if (row >= 0 && row < npcEntries.size() - 1)
        {
            npcEntries.swapItemsAt(row, row + 1);
            currentNpcEditRow = -1;
            refreshNpcList();
            ui->npcListWidget->setCurrentRow(row + 1);
            hasNpcUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
    else
    {
        collectObjectProperties();
        int row = ui->objectListWidget->currentRow();
        if (row >= 0 && row < objectEntries.size() - 1)
        {
            objectEntries.swapItemsAt(row, row + 1);
            currentObjectEditRow = -1;
            refreshObjectList();
            ui->objectListWidget->setCurrentRow(row + 1);
            hasObjectUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
}

void NpcDataEditorWindow::onDuplicateEntry()
{
    const int documentKind = ui->entityTabWidget->currentIndex();
    if (documentKind == 2)
    {
        const QString sourceSection = currentNpcResourceSection();
        if (sourceSection.isEmpty())
            return;
        bool accepted = false;
        const QString newSection = QInputDialog::getText(this,
            tr("复制动作段"), tr("新段名"), QLineEdit::Normal,
            sourceSection + tr("副本"), &accepted).trimmed();
        if (!accepted || newSection.isEmpty())
            return;
        if (!addNpcResourceSection(newSection, sourceSection))
        {
            QMessageBox::warning(this, tr("复制失败"),
                tr("动作段名称为空、包含非法字符或已经存在：%1")
                    .arg(newSection));
        }
        return;
    }
    if (documentKind == 0)
    {
        collectNpcProperties();
        int row = ui->npcListWidget->currentRow();
        if (row >= 0 && row < npcEntries.size())
        {
            NpcData data = npcEntries[row];
            data.sectionId = nextNpcSectionId++;
            npcEntries.insert(row + 1, data);
            currentNpcEditRow = -1;
            refreshNpcList();
            ui->npcListWidget->setCurrentRow(row + 1);
            hasNpcUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
    else
    {
        collectObjectProperties();
        int row = ui->objectListWidget->currentRow();
        if (row >= 0 && row < objectEntries.size())
        {
            ObjectData data = objectEntries[row];
            data.sectionId = nextObjectSectionId++;
            objectEntries.insert(row + 1, data);
            currentObjectEditRow = -1;
            refreshObjectList();
            ui->objectListWidget->setCurrentRow(row + 1);
            hasObjectUnsavedChanges = true;
            updateOverallDirtyStateAndNotify();
        }
    }
}

void NpcDataEditorWindow::onNpcSelectionChanged()
{
    if (updatingFromCode) return;
    collectNpcProperties();
    int row = ui->npcListWidget->currentRow();
    currentNpcEditRow = row;
    showNpcProperties(row);
    updatePropertyTabVisibility();
}

void NpcDataEditorWindow::onEditDialogueFromNpc()
{
    collectNpcProperties();
    const int row = ui->npcListWidget->currentRow();
    if (row < 0 || row >= npcEntries.size())
        return;
    const QString scriptPath = currentNpcDialogueScriptPath();
    if (scriptPath.isEmpty())
    {
        QMessageBox::information(
            this, tr("无法编辑对话"),
            tr("当前 NPC 没有可定位的脚本。请先填写脚本文件，并确认 NPC 文件包含地图名称。"));
        return;
    }
    if (!QFileInfo(scriptPath).isFile())
    {
        QMessageBox::information(
            this, tr("无法编辑对话"),
            tr("没有找到当前 NPC 的脚本：\n%1").arg(scriptPath));
        return;
    }
    emit editDialogueFromNpcRequested(
        scriptPath, npcEntries[row].properties.value("Name"));
}

void NpcDataEditorWindow::onObjectSelectionChanged()
{
    if (updatingFromCode) return;
    collectObjectProperties();
    int row = ui->objectListWidget->currentRow();
    currentObjectEditRow = row;
    showObjectProperties(row);
}

void NpcDataEditorWindow::onEntityTabChanged(int index)
{
    if (!updatingFromCode)
    {
        collectNpcProperties();
        collectObjectProperties();
    }
    Q_UNUSED(index);
    updatePropertyTabVisibility();
}

void NpcDataEditorWindow::onNpcPropertyChanged()
{
    if (updatingFromCode) return;
    collectNpcProperties();
    int row = ui->npcListWidget->currentRow();
    if (row >= 0 && row < npcEntries.size())
    {
        updatingFromCode = true;
        ui->npcListWidget->item(row)->setText(
            QString("NPC%1 - %2").arg(npcEntries[row].sectionId, 3, 10, QChar('0')).arg(npcEntries[row].properties.value("Name", "")));
        updatingFromCode = false;
    }
    hasNpcUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
    updatePropertyTabVisibility();
}

void NpcDataEditorWindow::onObjectPropertyChanged()
{
    if (updatingFromCode) return;
    collectObjectProperties();
    int row = ui->objectListWidget->currentRow();
    if (row >= 0 && row < objectEntries.size())
    {
        updatingFromCode = true;
        ui->objectListWidget->item(row)->setText(
            QString("OBJ%1 - %2").arg(objectEntries[row].sectionId, 3, 10, QChar('0')).arg(objectEntries[row].properties.value("ObjName", "")));
        updatingFromCode = false;
    }
    hasObjectUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
}

QString NpcDataEditorWindow::currentNpcResourceSection() const
{
    const QListWidgetItem* item = npcResourceSectionList
        ? npcResourceSectionList->currentItem()
        : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void NpcDataEditorWindow::refreshNpcResourceSections()
{
    const QString selected = currentNpcResourceSection();
    updatingFromCode = true;
    npcResourceSectionList->clear();
    int selectedRow = -1;
    for (int index = 0; index < npcResourceSectionOrder.size(); ++index)
    {
        const QString& section = npcResourceSectionOrder[index];
        QString display = section;
        if (!isKnownNpcResourceSection(section))
            display += tr("  · 自定义/未证实");
        auto* item = new QListWidgetItem(display, npcResourceSectionList);
        item->setData(Qt::UserRole, section);
        if (section.compare(selected, Qt::CaseInsensitive) == 0)
            selectedRow = index;
    }
    updatingFromCode = false;
    if (selectedRow < 0 && !npcResourceSectionOrder.isEmpty())
        selectedRow = 0;
    npcResourceSectionList->setCurrentRow(selectedRow);
    if (selectedRow < 0)
        showNpcResourceSection(-1);
}

void NpcDataEditorWindow::showNpcResourceSection(int index)
{
    updatingFromCode = true;
    if (!npcResourceDocument || index < 0 ||
        index >= npcResourceSectionOrder.size())
    {
        npcResourceSectionLabel->setText(tr("未选择动作段"));
        npcResourceStateLabel->clear();
        npcResourceImageEdit->clear();
        npcResourceShadeEdit->clear();
        npcResourceSoundEdit->clear();
        npcResourceExtraTable->setRowCount(0);
        npcResourcePreview->clearImage(tr("未选择动作段"));
        npcResourceMetadataLabel->setText(tr("未加载帧信息"));
        updatingFromCode = false;
        return;
    }

    const QString section = npcResourceSectionOrder[index];
    const std::string sectionUtf8 = section.toUtf8().toStdString();
    npcResourceSectionLabel->setText(tr("动作段 [%1]").arg(section));
    const QString lower = section.toLower();
    if (isKnownNpcResourceSection(section))
    {
        if (lower == QStringLiteral("astand") ||
            lower == QStringLiteral("awalk") ||
            lower == QStringLiteral("arun") ||
            lower == QStringLiteral("ajump"))
        {
            npcResourceStateLabel->setText(tr(
                "当前 C++ 运行时消费的历史 A* 战斗动作别名；同文件存在对应 Fight* 段时，Fight* 段会覆盖它。"));
        }
        else
        {
            npcResourceStateLabel->setText(tr(
                "当前 C++ 运行时与参考实现已确认消费该动作段。"));
        }
    }
    else
    {
        npcResourceStateLabel->setText(tr(
            "自定义/未证实动作段：编辑器保留并允许修改内容，但不承诺当前运行时会消费。"));
    }

    npcResourceImageEdit->setText(QString::fromUtf8(
        npcResourceDocument->get(sectionUtf8, "Image", "").c_str()));
    npcResourceShadeEdit->setText(QString::fromUtf8(
        npcResourceDocument->get(sectionUtf8, "Shade", "").c_str()));
    npcResourceSoundEdit->setText(QString::fromUtf8(
        npcResourceDocument->get(sectionUtf8, "Sound", "").c_str()));

    npcResourceExtraTable->setRowCount(0);
    const std::vector<std::string> keys =
        npcResourceDocument->getKeyNames(sectionUtf8);
    for (const std::string& key : keys)
    {
        const QString keyName = QString::fromUtf8(key.c_str());
        const QString lowerKey = keyName.toLower();
        if (lowerKey == QStringLiteral("image") ||
            lowerKey == QStringLiteral("shade") ||
            lowerKey == QStringLiteral("sound"))
        {
            continue;
        }
        const int row = npcResourceExtraTable->rowCount();
        npcResourceExtraTable->insertRow(row);
        auto* keyItem = new QTableWidgetItem(keyName);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        npcResourceExtraTable->setItem(row, 0, keyItem);
        npcResourceExtraTable->setItem(row, 1, new QTableWidgetItem(
            QString::fromUtf8(npcResourceDocument->get(
                sectionUtf8, key, "").c_str())));
    }
    updatingFromCode = false;
    updateNpcResourcePreview();
}

void NpcDataEditorWindow::onNpcResourceSelectionChanged()
{
    if (updatingFromCode)
        return;
    showNpcResourceSection(npcResourceSectionList->currentRow());
    ui->propertyTabWidget->setEnabled(
        npcResourceSectionList->currentRow() >= 0);
    updatePropertyTabVisibility();
}

void NpcDataEditorWindow::onNpcResourcePropertyChanged()
{
    if (updatingFromCode || !npcResourceDocument)
        return;
    const QString section = currentNpcResourceSection();
    if (section.isEmpty())
        return;

    QString key;
    auto* edit = qobject_cast<QLineEdit*>(sender());
    if (edit == npcResourceImageEdit)
        key = QStringLiteral("Image");
    else if (edit == npcResourceShadeEdit)
        key = QStringLiteral("Shade");
    else if (edit == npcResourceSoundEdit)
        key = QStringLiteral("Sound");
    if (key.isEmpty())
        return;

    npcResourceDocument->set(section.toUtf8().toStdString(),
        key.toStdString(), edit->text().toUtf8().toStdString());
    hasNpcResourceUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
    if (edit == npcResourceImageEdit)
        updateNpcResourcePreview();
}

void NpcDataEditorWindow::onNpcResourceExtraValueChanged(int row, int column)
{
    if (updatingFromCode || column != 1 || !npcResourceDocument)
        return;
    const QString section = currentNpcResourceSection();
    const QTableWidgetItem* keyItem = npcResourceExtraTable->item(row, 0);
    const QTableWidgetItem* valueItem = npcResourceExtraTable->item(row, 1);
    if (section.isEmpty() || !keyItem || !valueItem)
        return;
    npcResourceDocument->set(section.toUtf8().toStdString(),
        keyItem->text().toUtf8().toStdString(),
        valueItem->text().toUtf8().toStdString());
    hasNpcResourceUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
}

void NpcDataEditorWindow::onAddNpcResourceExtraKey()
{
    const QString section = currentNpcResourceSection();
    if (section.isEmpty() || !npcResourceDocument)
        return;
    bool accepted = false;
    const QString key = QInputDialog::getText(this, tr("添加字段"),
        tr("字段名"), QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || key.isEmpty())
        return;
    const QString lower = key.toLower();
    if (key.contains('=') || key.contains(':') || key.contains('[') ||
        key.contains(']') || key.contains('\n') || key.contains('\r') ||
        lower == QStringLiteral("image") || lower == QStringLiteral("shade") ||
        lower == QStringLiteral("sound") ||
        npcResourceDocument->hasKey(section.toUtf8().toStdString(),
            key.toUtf8().toStdString()))
    {
        QMessageBox::warning(this, tr("添加失败"),
            tr("字段名非法、属于标准字段或已经存在：%1").arg(key));
        return;
    }
    npcResourceDocument->set(section.toUtf8().toStdString(),
        key.toUtf8().toStdString(), "");
    hasNpcResourceUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
    showNpcResourceSection(npcResourceSectionList->currentRow());
}

void NpcDataEditorWindow::onRemoveNpcResourceExtraKey()
{
    const QString section = currentNpcResourceSection();
    const int row = npcResourceExtraTable->currentRow();
    const QTableWidgetItem* item = npcResourceExtraTable->item(row, 0);
    if (section.isEmpty() || row < 0 || !item || !npcResourceDocument)
        return;
    npcResourceDocument->removeKey(section.toUtf8().toStdString(),
        item->text().toUtf8().toStdString());
    hasNpcResourceUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
    showNpcResourceSection(npcResourceSectionList->currentRow());
}

bool NpcDataEditorWindow::addNpcResourceSection(
    const QString& sectionName, const QString& copyFromSection)
{
    if (!npcResourceDocument ||
        !npcResourceDocument->addSection(
            sectionName.toUtf8().toStdString()))
    {
        return false;
    }
    if (!copyFromSection.isEmpty())
    {
        const std::string source = copyFromSection.toUtf8().toStdString();
        for (const std::string& key : npcResourceDocument->getKeyNames(source))
        {
            npcResourceDocument->set(sectionName.toUtf8().toStdString(), key,
                npcResourceDocument->get(source, key, ""));
        }
    }
    npcResourceSectionOrder.append(sectionName);
    hasNpcResourceUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
    refreshNpcResourceSections();
    for (int row = 0; row < npcResourceSectionList->count(); ++row)
    {
        if (npcResourceSectionList->item(row)->data(Qt::UserRole).toString()
                .compare(sectionName, Qt::CaseInsensitive) == 0)
        {
            npcResourceSectionList->setCurrentRow(row);
            break;
        }
    }
    return true;
}

bool NpcDataEditorWindow::removeCurrentNpcResourceSection()
{
    const QString section = currentNpcResourceSection();
    if (section.isEmpty() || !npcResourceDocument)
        return false;
    npcResourceDocument->removeSection(section.toUtf8().toStdString());
    for (int index = 0; index < npcResourceSectionOrder.size(); ++index)
    {
        if (npcResourceSectionOrder[index].compare(
                section, Qt::CaseInsensitive) == 0)
        {
            npcResourceSectionOrder.removeAt(index);
            break;
        }
    }
    hasNpcResourceUnsavedChanges = true;
    updateOverallDirtyStateAndNotify();
    refreshNpcResourceSections();
    return true;
}

QStringList NpcDataEditorWindow::npcResourceImageCandidates(
    const QString& imageName) const
{
    QString normalized;
    if (!normalizeNpcResourceAssetReference(imageName, normalized) ||
        normalized.isEmpty())
    {
        return {};
    }
    normalized = QDir::fromNativeSeparators(normalized);
    const QString lower = normalized.toLower();
    QStringList candidates;
    auto appendUnique = [&](const QString& candidate)
    {
        for (const QString& existing : candidates)
        {
            if (existing.compare(candidate,
#ifdef Q_OS_WIN
                    Qt::CaseInsensitive
#else
                    Qt::CaseSensitive
#endif
                    ) == 0)
            {
                return;
            }
        }
        candidates.append(candidate);
    };

    if (lower.startsWith("asf/") || lower.startsWith("mpc/"))
    {
        appendUnique(normalized);
        if (lower.startsWith("asf/"))
            appendUnique(QStringLiteral("mpc/") + normalized.mid(4));
        else
            appendUnique(QStringLiteral("asf/") + normalized.mid(4));
        return candidates;
    }

    QString suffix = normalized;
    if (lower.startsWith("character/"))
        suffix = normalized.mid(QStringLiteral("character/").size());
    appendUnique(QStringLiteral("asf/character/") + suffix);
    appendUnique(QStringLiteral("mpc/character/") + suffix);
    appendUnique(QStringLiteral("asf/interlude/") + suffix);
    appendUnique(QStringLiteral("mpc/interlude/") + suffix);
    return candidates;
}

void NpcDataEditorWindow::updateNpcResourcePreview()
{
    if (!npcResourceImageCache || assetsBasePath.isEmpty())
    {
        npcResourcePreview->clearImage(tr("请先设置资源目录"));
        npcResourceMetadataLabel->setText(tr("未加载帧信息"));
        return;
    }
    const QString imageName = npcResourceImageEdit->text().trimmed();
    if (imageName.isEmpty())
    {
        npcResourcePreview->clearImage(tr("该动作段未设置图像"));
        npcResourceMetadataLabel->setText(tr("未加载帧信息"));
        return;
    }

    for (const QString& candidate : npcResourceImageCandidates(imageName))
    {
        const std::string relative = candidate.toUtf8().toStdString();
        const QImage image = npcResourceImageCache->getFrameImage(relative, 0);
        if (image.isNull())
            continue;
        npcResourcePreview->setSourceImage(image);
        int offsetX = 0;
        int offsetY = 0;
        npcResourceImageCache->getFrameOffset(
            relative, 0, offsetX, offsetY);
        npcResourceMetadataLabel->setText(tr(
            "已解析: %1\n帧数 %2 · 方向 %3 · 间隔 %4 ms · 首帧偏移 (%5, %6)")
            .arg(candidate)
            .arg(npcResourceImageCache->getFrameCount(relative))
            .arg(npcResourceImageCache->getDirection(relative))
            .arg(npcResourceImageCache->getInterval(relative))
            .arg(offsetX)
            .arg(offsetY));
        return;
    }

    npcResourcePreview->clearImage(tr("未找到或无法解析图像资源"));
    npcResourceMetadataLabel->setText(tr("引用: %1").arg(imageName));
}

bool NpcDataEditorWindow::loadNpcResourceFromFile(
    const QString& fileName, bool reportLoadErrors)
{
    std::string utf8Text;
    QStringList loadedLines;
    QString errorMessage;
    if (!readIniTextAsUtf8(fileName, utf8Text, loadedLines, errorMessage))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(
                this, tr("打开 NPC 资源文件失败"), errorMessage);
        }
        return false;
    }

    QStringList loadedSectionOrder;
    if (!validateNpcResourceRawShape(
            loadedLines, loadedSectionOrder, errorMessage))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开 NPC 资源文件失败"),
                tr("%1\n\n文件: %2").arg(errorMessage, fileName));
        }
        return false;
    }

    auto loadedDocument = std::make_unique<INIFileEditor>();
    if (!loadedDocument->loadFromString(utf8Text))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开 NPC 资源文件失败"),
                tr("INI 语法无效: %1").arg(fileName));
        }
        return false;
    }

    npcResourceDocument = std::move(loadedDocument);
    npcResourceSectionOrder = std::move(loadedSectionOrder);
    currentNpcResourceFilePath = fileName;
    isNpcResourceFile = true;
    hasNpcResourceUnsavedChanges = false;
    updateOverallDirtyStateAndNotify();
    npcResourceImageCache->setAssetsBasePath(
        assetsBasePath.toUtf8().toStdString());
    npcResourceImageCache->clearCache();
    refreshNpcResourceSections();
    ui->propertyTabWidget->setEnabled(
        !npcResourceSectionOrder.isEmpty());
    return true;
}

bool NpcDataEditorWindow::validateAndNormalizeNpcResourceDocument()
{
    if (!npcResourceDocument || npcResourceSectionOrder.isEmpty())
    {
        QMessageBox::warning(this, tr("NPC 资源无效"),
            tr("NPC 资源文件至少需要一个动作段。"));
        return false;
    }

    for (int sectionIndex = 0;
         sectionIndex < npcResourceSectionOrder.size(); ++sectionIndex)
    {
        const QString section = npcResourceSectionOrder[sectionIndex];
        const std::string sectionUtf8 = section.toUtf8().toStdString();
        const struct StandardField
        {
            const char* key;
            QString label;
            bool sound;
        } fields[] = {
            {"Image", tr("动作图像"), false},
            {"Shade", tr("阴影图像"), false},
            {"Sound", tr("动作音效"), true}
        };

        for (const StandardField& field : fields)
        {
            if (!npcResourceDocument->hasKey(sectionUtf8, field.key))
                continue;
            const QString value = QString::fromUtf8(
                npcResourceDocument->get(sectionUtf8, field.key, "").c_str());
            QString normalized;
            const bool valid = field.sound
                ? FilePickerHelper::normalizeEntityResourceReference(
                      FilePickerHelper::EntityResourceField::WavFile,
                      value, normalized)
                : normalizeNpcResourceAssetReference(value, normalized);
            if (!valid)
            {
                ui->entityTabWidget->setCurrentIndex(2);
                npcResourceSectionList->setCurrentRow(sectionIndex);
                QMessageBox::warning(this, tr("资源引用无效"),
                    tr("[%1].%2 必须是资源包内的相对名称，不能包含盘符、UNC 或父目录穿越。\n\n当前值: %3")
                        .arg(section, field.label, value));
                if (QString::fromLatin1(field.key) == QStringLiteral("Image"))
                    npcResourceImageEdit->setFocus();
                else if (QString::fromLatin1(field.key) == QStringLiteral("Shade"))
                    npcResourceShadeEdit->setFocus();
                else
                    npcResourceSoundEdit->setFocus();
                return false;
            }
            npcResourceDocument->set(sectionUtf8, field.key,
                normalized.toUtf8().toStdString());
        }
    }
    const int selectedRow = npcResourceSectionList->currentRow();
    if (selectedRow >= 0)
        showNpcResourceSection(selectedRow);
    return true;
}

bool NpcDataEditorWindow::saveNpcResourceToFile(
    const QString& fileName, bool updateBaseline, QByteArray* savedData)
{
    if (!npcResourceDocument)
        return false;
    const std::string serialized = npcResourceDocument->saveToString();
    const QByteArray data(serialized.data(),
        static_cast<qsizetype>(serialized.size()));

    QStringList savedLines = splitUtf8TextLines(serialized);
    QStringList savedSectionOrder;
    QString errorMessage;
    if (!validateNpcResourceRawShape(
            savedLines, savedSectionOrder, errorMessage))
    {
        return false;
    }
    if (!writeBytesAtomically(fileName, data))
        return false;

    if (savedData)
        *savedData = data;
    if (updateBaseline)
    {
        auto reloaded = std::make_unique<INIFileEditor>();
        if (!reloaded->loadFromString(serialized))
            return false;
        npcResourceDocument = std::move(reloaded);
        npcResourceSectionOrder = std::move(savedSectionOrder);
        refreshNpcResourceSections();
    }
    return true;
}

bool NpcDataEditorWindow::loadNpcFromFile(
    const QString& fileName, bool reportLoadErrors)
{
    std::string utf8Text;
    QStringList loadedLines;
    QString errorMessage;
    if (!readIniTextAsUtf8(fileName, utf8Text, loadedLines, errorMessage))
    {
        if (reportLoadErrors)
            QMessageBox::warning(this, tr("打开 NPC 文件失败"), errorMessage);
        return false;
    }

    INIFileEditor ini;
    if (!ini.loadFromString(utf8Text))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开 NPC 文件失败"),
                tr("INI 语法无效: %1").arg(fileName));
        }
        return false;
    }

    int expectedCount = 0;
    if (!validateEntityListShape(ini, loadedLines, "NPC", expectedCount, errorMessage))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开 NPC 文件失败"),
                tr("%1\n\n文件: %2").arg(errorMessage, fileName));
        }
        return false;
    }

    std::vector<std::string> sections = ini.getSectionNames();
    QMap<int, NpcData> npcMap;
    for (const std::string& section : sections)
    {
        QString sectionName = QString::fromStdString(section).trimmed();
        if (sectionName.startsWith("npc", Qt::CaseInsensitive))
        {
            bool ok = false;
            int index = sectionName.mid(3).toInt(&ok);
            if (!ok)
                continue;

            NpcData data;
            data.sectionId = index;
            std::vector<std::string> keys = ini.getKeyNames(section);
            QSet<QString> presentKeys;
            for (const std::string& key : keys)
                presentKeys.insert(QString::fromStdString(key).toLower());
            for (const std::string& key : keys)
            {
                QString keyName = QString::fromStdString(key);
                const QString keyLower = keyName.toLower();
                if (keyLower == "defence" && presentKeys.contains("defend"))
                    continue;
                QString canonicalKey = npcKeyCaseMap().value(keyName.toLower(), keyName);
                data.properties[canonicalKey] = QString::fromStdString(ini.get(section, key));
            }
            npcMap[index] = data;
        }
    }

    QList<NpcData> loadedEntries;
    QList<int> sortedKeys = npcMap.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    for (int key : sortedKeys)
        loadedEntries.append(npcMap[key]);

    if (loadedEntries.size() != expectedCount)
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开 NPC 文件失败"),
                tr("NPC 段数量与 [Head].Count 不一致: %1").arg(fileName));
        }
        return false;
    }

    npcEntries = std::move(loadedEntries);
    currentNpcMapName = QString::fromUtf8(
        ini.get("Head", "Map", "")).trimmed();
    originalNpcLines = std::move(loadedLines);
    nextNpcSectionId = expectedCount;
    currentNpcFilePath = fileName;
    isNpcFile = true;
    ui->propertyTabWidget->setEnabled(true);
    currentNpcEditRow = -1;
    refreshNpcList();
    if (!npcEntries.isEmpty())
        ui->npcListWidget->setCurrentRow(0);
    hasNpcUnsavedChanges = false;
    updateOverallDirtyStateAndNotify();
    return true;
}

bool NpcDataEditorWindow::loadObjectFromFile(
    const QString& fileName, bool reportLoadErrors)
{
    std::string utf8Text;
    QStringList loadedLines;
    QString errorMessage;
    if (!readIniTextAsUtf8(fileName, utf8Text, loadedLines, errorMessage))
    {
        if (reportLoadErrors)
            QMessageBox::warning(this, tr("打开物体文件失败"), errorMessage);
        return false;
    }

    INIFileEditor ini;
    if (!ini.loadFromString(utf8Text))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开物体文件失败"),
                tr("INI 语法无效: %1").arg(fileName));
        }
        return false;
    }

    int expectedCount = 0;
    if (!validateEntityListShape(ini, loadedLines, "OBJ", expectedCount, errorMessage))
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开物体文件失败"),
                tr("%1\n\n文件: %2").arg(errorMessage, fileName));
        }
        return false;
    }

    std::vector<std::string> sections = ini.getSectionNames();
    QMap<int, ObjectData> objectMap;
    for (const std::string& section : sections)
    {
        QString sectionName = QString::fromStdString(section).trimmed();
        if (sectionName.startsWith("obj", Qt::CaseInsensitive))
        {
            bool ok = false;
            int index = sectionName.mid(3).toInt(&ok);
            if (!ok)
                continue;

            ObjectData data;
            data.sectionId = index;
            std::vector<std::string> keys = ini.getKeyNames(section);
            QSet<QString> presentKeys;
            for (const std::string& key : keys)
                presentKeys.insert(QString::fromStdString(key).toLower());
            for (const std::string& key : keys)
            {
                QString keyName = QString::fromStdString(key);
                const QString keyLower = keyName.toLower();
                if ((keyLower == "name" && presentKeys.contains("objname")) ||
                    (keyLower == "offx" && presentKeys.contains("offsetx")) ||
                    (keyLower == "offy" && presentKeys.contains("offsety")))
                {
                    continue;
                }
                QString canonicalKey = objectKeyCaseMap().value(keyLower, keyName);
                data.properties[canonicalKey] = QString::fromStdString(ini.get(section, key));
            }
            objectMap[index] = data;
        }
    }

    QList<ObjectData> loadedEntries;
    QList<int> sortedKeys = objectMap.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    for (int key : sortedKeys)
        loadedEntries.append(objectMap[key]);

    if (loadedEntries.size() != expectedCount)
    {
        if (reportLoadErrors)
        {
            QMessageBox::warning(this, tr("打开物体文件失败"),
                tr("物体段数量与 [Head].Count 不一致: %1").arg(fileName));
        }
        return false;
    }

    objectEntries = std::move(loadedEntries);
    originalObjectLines = std::move(loadedLines);
    nextObjectSectionId = expectedCount;
    currentObjectFilePath = fileName;
    isObjectFile = true;
    ui->propertyTabWidget->setEnabled(true);
    currentObjectEditRow = -1;
    refreshObjectList();
    if (!objectEntries.isEmpty())
        ui->objectListWidget->setCurrentRow(0);
    hasObjectUnsavedChanges = false;
    updateOverallDirtyStateAndNotify();
    return true;
}

QStringList NpcDataEditorWindow::serializeNpcLines(
    const QList<NpcData>& entries) const
{
    QStringList lines;
    lines << "[Head]";
    lines << QString("Count=%1").arg(entries.size());
    lines << "";

    for (int i = 0; i < entries.size(); i++)
    {
        lines << QString("[NPC%1]").arg(i, 3, 10, QChar('0'));
        const auto& props = entries[i].properties;

        QStringList knownKeys = {
            "Name", "Kind", "NPCIni", "Dir", "MapX", "MapY",
            "Action", "WalkSpeed", "StandSpeed", "PathFinder", "DialogRadius",
            "ScriptFile", "State", "Relation",
            "Life", "LifeMax", "Thew", "ThewMax", "Mana", "ManaMax",
            "Attack", "Defend", "Evade", "Duck",
            "Exp", "LevelUpExp", "Level", "AttackLevel", "MagicLevel",
            "Lum", "VisionRadius", "AttackRadius",
            "BodyIni", "FlyIni", "FlyIni2", "FlyInis", "MagicIni", "DeathScript"
        };

        QSet<QString> writtenKeys;
        for (const QString& key : knownKeys)
        {
            if (props.contains(key))
            {
                lines << QString("%1=%2").arg(key, props[key]);
                writtenKeys.insert(key.toLower());
            }
        }

        for (auto it = props.constBegin(); it != props.constEnd(); ++it)
        {
            if (!writtenKeys.contains(it.key().toLower()))
            {
                lines << QString("%1=%2").arg(it.key(), it.value());
            }
        }

        lines << "";
    }

    return lines;
}

bool NpcDataEditorWindow::saveNpcToFile(const QString& fileName,
                                        bool updateBaseline,
                                        QStringList* savedLines)
{
    const QStringList lines = serializeNpcLines(npcEntries);
    if (!writeTextLinesAtomically(fileName, lines))
        return false;

    if (savedLines)
        *savedLines = lines;
    if (updateBaseline)
    {
        originalNpcLines = lines;
        normalizeNpcSectionIds();
    }
    return true;
}

QStringList NpcDataEditorWindow::serializeNpcLinesPreserving(
    const QList<NpcData>& entries) const
{
    QStringList knownKeys = {
        "Name", "Kind", "NPCIni", "Dir", "MapX", "MapY",
        "Action", "WalkSpeed", "StandSpeed", "PathFinder", "DialogRadius",
        "ScriptFile", "State", "Relation",
        "Life", "LifeMax", "Thew", "ThewMax", "Mana", "ManaMax",
        "Attack", "Defend", "Evade", "Duck",
        "Exp", "LevelUpExp", "Level", "AttackLevel", "MagicLevel",
        "Lum", "VisionRadius", "AttackRadius",
        "BodyIni", "FlyIni", "FlyIni2", "FlyInis", "MagicIni", "DeathScript"
    };
    QSet<QString> knownKeysLower;
    for (const QString& key : knownKeys)
        knownKeysLower.insert(key.toLower());
    knownKeysLower.insert("defence");

    if (originalNpcLines.isEmpty())
        return serializeNpcLines(entries);

    // Build map from current sectionId -> list index (before renumbering)
    QMap<int, int> sectionIdToListIndex;
    for (int i = 0; i < entries.size(); i++)
        sectionIdToListIndex[entries[i].sectionId] = i;

    QStringList outputLines;

    QString currentSection;
    int currentNpcListIndex = -1;  // index into npcEntries list
    QSet<int> writtenNpcListIndices;
    QSet<QString> writtenKeysInNpc;
    bool headSectionWritten = false;
    bool headCountWritten = false;

    auto flushMissingNpcKeys = [&]() {
        if (currentNpcListIndex >= 0 && currentNpcListIndex < entries.size())
        {
            const auto& props = entries[currentNpcListIndex].properties;
            for (const QString& key : knownKeys)
            {
                if (!writtenKeysInNpc.contains(key.toLower()) && props.contains(key))
                {
                    outputLines << QString("%1=%2").arg(key, props[key]);
                }
            }
            for (auto it = props.constBegin(); it != props.constEnd(); ++it)
            {
                if (!knownKeysLower.contains(it.key().toLower())
                    && !writtenKeysInNpc.contains(it.key().toLower()))
                {
                    outputLines << QString("%1=%2").arg(it.key(), it.value());
                }
            }
        }
    };

    for (const QString& origLine : originalNpcLines)
    {
        QString trimmed = origLine.trimmed();

        if (trimmed.startsWith("[") && trimmed.endsWith("]"))
        {
            QString sectionName = trimmed.mid(1, trimmed.length() - 2).trimmed();

            if (currentSection == "npc")
                flushMissingNpcKeys();
            else if (currentSection == "head" && !headCountWritten)
            {
                outputLines << QString("Count=%1").arg(entries.size());
                headCountWritten = true;
            }

            if (sectionName.toLower() == "head")
            {
                currentSection = "head";
                headSectionWritten = true;
                outputLines << origLine;
                continue;
            }
            else if (sectionName.toLower().startsWith("npc"))
            {
                bool ok = false;
                int origSectionNum = sectionName.mid(3).toInt(&ok);
                if (ok)
                {
                    // Look up by original sectionId
                    auto it = sectionIdToListIndex.find(origSectionNum);
                    if (it != sectionIdToListIndex.end())
                    {
                        int listIdx = it.value();
                        currentSection = "npc";
                        currentNpcListIndex = listIdx;
                        writtenNpcListIndices.insert(listIdx);
                        writtenKeysInNpc.clear();
                        outputLines << QString("[NPC%1]").arg(listIdx, 3, 10, QChar('0'));
                        continue;
                    }
                    else
                    {
                        // Original section number doesn't match any entry's sectionId - skip it
                        currentSection = "skip_npc";
                        currentNpcListIndex = -1;
                        continue;
                    }
                }
                else
                {
                    currentSection = sectionName;
                    currentNpcListIndex = -1;
                    outputLines << origLine;
                    continue;
                }
            }
            else
            {
                currentSection = sectionName;
                outputLines << origLine;
                continue;
            }
        }

        if (currentSection == "skip_npc")
        {
            if (trimmed.isEmpty())
                outputLines << origLine;
            continue;
        }

        if (trimmed.isEmpty())
        {
            outputLines << origLine;
            continue;
        }

        if (trimmed.startsWith(";") || trimmed.startsWith("#"))
        {
            outputLines << origLine;
            continue;
        }

        if (currentSection.isEmpty())
        {
            outputLines << origLine;
            continue;
        }

        if (currentSection == "head")
        {
            int equalPos = trimmed.indexOf('=');
            if (equalPos < 0)
            {
                outputLines << origLine;
                continue;
            }
            QString key = trimmed.left(equalPos).trimmed().toLower();
            if (key == "count")
            {
            outputLines << QString("Count=%1").arg(entries.size());
                headCountWritten = true;
            }
            else
            {
                outputLines << origLine;
            }
        }
        else if (currentSection == "npc" &&
                 currentNpcListIndex >= 0 &&
                 currentNpcListIndex < entries.size())
        {
            int equalPos = trimmed.indexOf('=');
            if (equalPos < 0)
            {
                outputLines << origLine;
                continue;
            }
            QString key = trimmed.left(equalPos).trimmed();
            QString keyLower = key.toLower();

            if (knownKeysLower.contains(keyLower))
            {
                QString canonicalKey = npcKeyCaseMap().value(keyLower, key);
                const QString canonicalKeyLower = canonicalKey.toLower();
                if (writtenKeysInNpc.contains(canonicalKeyLower))
                    continue;
                writtenKeysInNpc.insert(keyLower);
                writtenKeysInNpc.insert(canonicalKey.toLower());
                const auto& props = entries[currentNpcListIndex].properties;
                if (props.contains(canonicalKey))
                {
                    outputLines << canonicalKey + "=" + props[canonicalKey];
                }
                else
                {
                    outputLines << origLine;
                }
            }
            else
            {
                writtenKeysInNpc.insert(keyLower);
                outputLines << origLine;
            }
        }
        else
        {
            outputLines << origLine;
        }
    }

    if (currentSection == "npc")
        flushMissingNpcKeys();
    else if (currentSection == "head" && !headCountWritten)
    {
        outputLines << QString("Count=%1").arg(entries.size());
        headCountWritten = true;
    }

    if (!headSectionWritten)
    {
        outputLines << "[Head]";
        outputLines << QString("Count=%1").arg(entries.size());
        outputLines << "";
    }

    // Append entries that were not matched in original lines
    for (int i = 0; i < entries.size(); i++)
    {
        if (writtenNpcListIndices.contains(i))
            continue;

        outputLines << "";
        outputLines << QString("[NPC%1]").arg(i, 3, 10, QChar('0'));
        const auto& props = entries[i].properties;

        QSet<QString> writtenKeys;
        for (const QString& key : knownKeys)
        {
            if (props.contains(key))
            {
                outputLines << QString("%1=%2").arg(key, props[key]);
                writtenKeys.insert(key.toLower());
            }
        }

        for (auto it = props.constBegin(); it != props.constEnd(); ++it)
        {
            if (!writtenKeys.contains(it.key().toLower()))
            {
                outputLines << QString("%1=%2").arg(it.key(), it.value());
            }
        }
    }

    return outputLines;
}

bool NpcDataEditorWindow::saveNpcToFilePreserving(
    const QString& fileName,
    bool updateBaseline,
    QStringList* savedLines)
{
    const QStringList outputLines =
        serializeNpcLinesPreserving(npcEntries);
    if (!writeTextLinesAtomically(fileName, outputLines))
        return false;

    if (savedLines)
        *savedLines = outputLines;
    if (updateBaseline)
    {
        originalNpcLines = outputLines;
        normalizeNpcSectionIds();
    }
    return true;
}

QStringList NpcDataEditorWindow::serializeObjectLines(
    const QList<ObjectData>& entries) const
{
    QStringList lines;
    lines << "[Head]";
    lines << QString("Count=%1").arg(entries.size());
    lines << "";

    for (int i = 0; i < entries.size(); i++)
    {
        lines << QString("[OBJ%1]").arg(i, 3, 10, QChar('0'));
        const auto& props = entries[i].properties;

        QStringList knownKeys = {
            "ObjName", "ObjFile", "ScriptFile", "WavFile",
            "Kind", "Dir", "MapX", "MapY",
            "OffsetX", "OffsetY", "Lum", "Damage", "Frame", "State", "ActionTime"
        };

        QSet<QString> writtenKeys;
        for (const QString& key : knownKeys)
        {
            if (props.contains(key))
            {
                lines << QString("%1=%2").arg(key, props[key]);
                writtenKeys.insert(key.toLower());
            }
        }

        for (auto it = props.constBegin(); it != props.constEnd(); ++it)
        {
            if (!writtenKeys.contains(it.key().toLower()))
            {
                lines << QString("%1=%2").arg(it.key(), it.value());
            }
        }

        lines << "";
    }

    return lines;
}

bool NpcDataEditorWindow::saveObjectToFile(const QString& fileName,
                                           bool updateBaseline,
                                           QStringList* savedLines)
{
    const QStringList lines = serializeObjectLines(objectEntries);
    if (!writeTextLinesAtomically(fileName, lines))
        return false;

    if (savedLines)
        *savedLines = lines;
    if (updateBaseline)
    {
        originalObjectLines = lines;
        normalizeObjectSectionIds();
    }
    return true;
}

QStringList NpcDataEditorWindow::serializeObjectLinesPreserving(
    const QList<ObjectData>& entries) const
{
    QStringList knownKeys = {
        "ObjName", "ObjFile", "ScriptFile", "WavFile",
        "Kind", "Dir", "MapX", "MapY",
        "OffsetX", "OffsetY", "Lum", "Damage", "Frame", "State", "ActionTime"
    };
    QSet<QString> knownKeysLower;
    for (const QString& key : knownKeys)
        knownKeysLower.insert(key.toLower());
    knownKeysLower.insert("offx");
    knownKeysLower.insert("offy");
    knownKeysLower.insert("name");

    if (originalObjectLines.isEmpty())
        return serializeObjectLines(entries);

    // Build map: sectionId -> list index
    QMap<int, int> sectionIdToListIndex;
    for (int i = 0; i < entries.size(); i++)
        sectionIdToListIndex[entries[i].sectionId] = i;

    QStringList outputLines;

    QString currentSection;
    int currentObjListIndex = -1;
    QSet<int> writtenObjListIndices;
    QSet<QString> writtenKeysInObj;
    bool headSectionWritten = false;
    bool headCountWritten = false;

    auto flushMissingObjKeys = [&]() {
        if (currentObjListIndex >= 0 && currentObjListIndex < entries.size())
        {
            const auto& props = entries[currentObjListIndex].properties;
            for (const QString& key : knownKeys)
            {
                if (!writtenKeysInObj.contains(key.toLower()) && props.contains(key))
                {
                    outputLines << QString("%1=%2").arg(key, props[key]);
                }
            }
            for (auto it = props.constBegin(); it != props.constEnd(); ++it)
            {
                if (!knownKeysLower.contains(it.key().toLower())
                    && !writtenKeysInObj.contains(it.key().toLower()))
                {
                    outputLines << QString("%1=%2").arg(it.key(), it.value());
                }
            }
        }
    };

    for (const QString& origLine : originalObjectLines)
    {
        QString trimmed = origLine.trimmed();

        if (trimmed.startsWith("[") && trimmed.endsWith("]"))
        {
            QString sectionName = trimmed.mid(1, trimmed.length() - 2).trimmed();

            if (currentSection == "obj")
                flushMissingObjKeys();
            else if (currentSection == "head" && !headCountWritten)
            {
                outputLines << QString("Count=%1").arg(entries.size());
                headCountWritten = true;
            }

            if (sectionName.toLower() == "head")
            {
                currentSection = "head";
                headSectionWritten = true;
                outputLines << origLine;
                continue;
            }
            else if (sectionName.toLower().startsWith("obj"))
            {
                bool ok = false;
                int origSectionNum = sectionName.mid(3).toInt(&ok);
                if (ok)
                {
                    // Look up by original sectionId
                    auto it = sectionIdToListIndex.find(origSectionNum);
                    if (it != sectionIdToListIndex.end())
                    {
                        int listIdx = it.value();
                        currentSection = "obj";
                        currentObjListIndex = listIdx;
                        writtenObjListIndices.insert(listIdx);
                        writtenKeysInObj.clear();
                        outputLines << QString("[OBJ%1]").arg(listIdx, 3, 10, QChar('0'));
                        continue;
                    }
                    else
                    {
                        // Original section number doesn't match any entry's sectionId - skip it
                        currentSection = "skip_obj";
                        currentObjListIndex = -1;
                        continue;
                    }
                }
                else
                {
                    currentSection = sectionName;
                    currentObjListIndex = -1;
                    outputLines << origLine;
                    continue;
                }
            }
            else
            {
                currentSection = sectionName;
                outputLines << origLine;
                continue;
            }
        }

        if (currentSection == "skip_obj")
        {
            if (trimmed.isEmpty())
                outputLines << origLine;
            continue;
        }

        if (trimmed.isEmpty())
        {
            outputLines << origLine;
            continue;
        }

        if (trimmed.startsWith(";") || trimmed.startsWith("#"))
        {
            outputLines << origLine;
            continue;
        }

        if (currentSection.isEmpty())
        {
            outputLines << origLine;
            continue;
        }

        if (currentSection == "head")
        {
            int equalPos = trimmed.indexOf('=');
            if (equalPos < 0)
            {
                outputLines << origLine;
                continue;
            }
            QString key = trimmed.left(equalPos).trimmed().toLower();
            if (key == "count")
            {
            outputLines << QString("Count=%1").arg(entries.size());
                headCountWritten = true;
            }
            else
            {
                outputLines << origLine;
            }
        }
        else if (currentSection == "obj" &&
                 currentObjListIndex >= 0 &&
                 currentObjListIndex < entries.size())
        {
            int equalPos = trimmed.indexOf('=');
            if (equalPos < 0)
            {
                outputLines << origLine;
                continue;
            }
            QString key = trimmed.left(equalPos).trimmed();
            QString keyLower = key.toLower();

            if (knownKeysLower.contains(keyLower))
            {
                QString canonicalKey = objectKeyCaseMap().value(keyLower, key);
                const QString canonicalKeyLower = canonicalKey.toLower();
                if (writtenKeysInObj.contains(canonicalKeyLower))
                    continue;
                writtenKeysInObj.insert(keyLower);
                writtenKeysInObj.insert(canonicalKey.toLower());
                const auto& props = entries[currentObjListIndex].properties;
                if (props.contains(canonicalKey))
                {
                    outputLines << canonicalKey + "=" + props[canonicalKey];
                }
                else
                {
                    outputLines << origLine;
                }
            }
            else
            {
                writtenKeysInObj.insert(keyLower);
                outputLines << origLine;
            }
        }
        else
        {
            outputLines << origLine;
        }
    }

    if (currentSection == "obj")
        flushMissingObjKeys();
    else if (currentSection == "head" && !headCountWritten)
    {
        outputLines << QString("Count=%1").arg(entries.size());
        headCountWritten = true;
    }

    if (!headSectionWritten)
    {
        outputLines << "[Head]";
        outputLines << QString("Count=%1").arg(entries.size());
        outputLines << "";
    }

    // Append entries that were not matched in original lines
    for (int i = 0; i < entries.size(); i++)
    {
        if (writtenObjListIndices.contains(i))
            continue;

        outputLines << "";
        outputLines << QString("[OBJ%1]").arg(i, 3, 10, QChar('0'));
        const auto& props = entries[i].properties;

        QSet<QString> writtenKeys;
        for (const QString& key : knownKeys)
        {
            if (props.contains(key))
            {
                outputLines << QString("%1=%2").arg(key, props[key]);
                writtenKeys.insert(key.toLower());
            }
        }

        for (auto it = props.constBegin(); it != props.constEnd(); ++it)
        {
            if (!writtenKeys.contains(it.key().toLower()))
            {
                outputLines << QString("%1=%2").arg(it.key(), it.value());
            }
        }
    }

    return outputLines;
}

bool NpcDataEditorWindow::saveObjectToFilePreserving(
    const QString& fileName,
    bool updateBaseline,
    QStringList* savedLines)
{
    const QStringList outputLines =
        serializeObjectLinesPreserving(objectEntries);
    if (!writeTextLinesAtomically(fileName, outputLines))
        return false;

    if (savedLines)
        *savedLines = outputLines;
    if (updateBaseline)
    {
        originalObjectLines = outputLines;
        normalizeObjectSectionIds();
    }
    return true;
}

void NpcDataEditorWindow::refreshNpcList()
{
    updatingFromCode = true;
    ui->npcListWidget->clear();
    for (int i = 0; i < npcEntries.size(); i++)
    {
        QString text = QString("NPC%1 - %2").arg(npcEntries[i].sectionId, 3, 10, QChar('0')).arg(npcEntries[i].properties.value("Name", ""));
        ui->npcListWidget->addItem(text);
    }
    updatingFromCode = false;
}

void NpcDataEditorWindow::refreshObjectList()
{
    updatingFromCode = true;
    ui->objectListWidget->clear();
    for (int i = 0; i < objectEntries.size(); i++)
    {
        QString text = QString("OBJ%1 - %2").arg(objectEntries[i].sectionId, 3, 10, QChar('0')).arg(objectEntries[i].properties.value("ObjName", ""));
        ui->objectListWidget->addItem(text);
    }
    updatingFromCode = false;
}

void NpcDataEditorWindow::normalizeNpcSectionIds()
{
    updatingFromCode = true;
    for (int i = 0; i < npcEntries.size(); i++)
    {
        npcEntries[i].sectionId = i;
        if (QListWidgetItem* item = ui->npcListWidget->item(i))
        {
            item->setText(QString("NPC%1 - %2")
                .arg(i, 3, 10, QChar('0'))
                .arg(npcEntries[i].properties.value("Name", "")));
        }
    }
    nextNpcSectionId = npcEntries.size();
    updatingFromCode = false;
}

void NpcDataEditorWindow::normalizeObjectSectionIds()
{
    updatingFromCode = true;
    for (int i = 0; i < objectEntries.size(); i++)
    {
        objectEntries[i].sectionId = i;
        if (QListWidgetItem* item = ui->objectListWidget->item(i))
        {
            item->setText(QString("OBJ%1 - %2")
                .arg(i, 3, 10, QChar('0'))
                .arg(objectEntries[i].properties.value("ObjName", "")));
        }
    }
    nextObjectSectionId = objectEntries.size();
    updatingFromCode = false;
}

void NpcDataEditorWindow::showNpcProperties(int index)
{
    updatingFromCode = true;

    if (index < 0 || index >= npcEntries.size())
    {
        updatingFromCode = false;
        return;
    }

    const auto& props = npcEntries[index].properties;

    ui->nameEdit->setText(props.value("Name", ""));
    ui->resourceIniEdit->setText(props.value("NPCIni", ""));
    setSpinBoxValuePreservingRange(ui->dirSpinBox, props.value("Dir", "0"), 0);
    setSpinBoxValuePreservingRange(ui->mapXSpinBox, props.value("MapX", "0"), 0);
    setSpinBoxValuePreservingRange(ui->mapYSpinBox, props.value("MapY", "0"), 0);
    ui->scriptEdit->setText(props.value("ScriptFile", ""));

    selectComboValuePreservingUnknown(ui->kindCombo, props.value("Kind", "0"));
    selectComboValuePreservingUnknown(ui->actionCombo, props.value("Action", "0"));

    setSpinBoxValuePreservingRange(ui->walkSpeedSpinBox, props.value("WalkSpeed", "1"), 1);
    setSpinBoxValuePreservingRange(ui->standSpeedSpinBox, props.value("StandSpeed", "0"), 0);

    selectComboValuePreservingUnknown(ui->pathFinderCombo, props.value("PathFinder", "0"));

    setSpinBoxValuePreservingRange(ui->dialogRadiusSpinBox, props.value("DialogRadius", "1"), 1);

    selectComboValuePreservingUnknown(ui->relationCombo, props.value("Relation", "0"));

    setSpinBoxValuePreservingRange(ui->visionRadiusSpinBox, props.value("VisionRadius", "0"), 0);
    setSpinBoxValuePreservingRange(ui->attackRadiusSpinBox, props.value("AttackRadius", "0"), 0);

    setSpinBoxValuePreservingRange(ui->lifeSpinBox, props.value("Life", "0"), 0);
    setSpinBoxValuePreservingRange(ui->lifeMaxSpinBox, props.value("LifeMax", "0"), 0);
    setSpinBoxValuePreservingRange(ui->thewSpinBox, props.value("Thew", "0"), 0);
    setSpinBoxValuePreservingRange(ui->thewMaxSpinBox, props.value("ThewMax", "0"), 0);
    setSpinBoxValuePreservingRange(ui->manaSpinBox, props.value("Mana", "0"), 0);
    setSpinBoxValuePreservingRange(ui->manaMaxSpinBox, props.value("ManaMax", "0"), 0);
    setSpinBoxValuePreservingRange(ui->attackSpinBox, props.value("Attack", "0"), 0);
    setSpinBoxValuePreservingRange(ui->defendSpinBox, props.value("Defend", "0"), 0);
    setSpinBoxValuePreservingRange(ui->evadeSpinBox, props.value("Evade", "0"), 0);
    setSpinBoxValuePreservingRange(ui->duckSpinBox, props.value("Duck", "0"), 0);
    setSpinBoxValuePreservingRange(ui->expSpinBox, props.value("Exp", "0"), 0);
    setSpinBoxValuePreservingRange(ui->levelSpinBox, props.value("Level", "0"), 0);
    setSpinBoxValuePreservingRange(ui->levelUpExpSpinBox, props.value("LevelUpExp", "0"), 0);
    setSpinBoxValuePreservingRange(ui->attackLevelSpinBox, props.value("AttackLevel", "0"), 0);
    setSpinBoxValuePreservingRange(ui->magicLevelSpinBox, props.value("MagicLevel", "0"), 0);

    ui->bodyIniEdit->setText(props.value("BodyIni", ""));
    ui->flyIniEdit->setText(props.value("FlyIni", ""));
    ui->flyIni2Edit->setText(props.value("FlyIni2", ""));
    ui->flyInisEdit->setText(props.value("FlyInis", ""));
    ui->magicIniEdit->setText(props.value("MagicIni", ""));
    ui->deathScriptEdit->setText(props.value("DeathScript", ""));

    selectComboValuePreservingUnknown(ui->lumCombo, props.value("Lum", "0"));

    setSpinBoxValuePreservingRange(ui->stateSpinBox, props.value("State", "0"), 0);

    updatingFromCode = false;
}

void NpcDataEditorWindow::showObjectProperties(int index)
{
    updatingFromCode = true;

    if (index < 0 || index >= objectEntries.size())
    {
        updatingFromCode = false;
        return;
    }

    const auto& props = objectEntries[index].properties;

    ui->objNameEdit->setText(props.value("ObjName", ""));
    ui->objFileEdit->setText(props.value("ObjFile", ""));
    ui->objScriptEdit->setText(props.value("ScriptFile", ""));
    ui->wavFileEdit->setText(props.value("WavFile", ""));

    selectComboValuePreservingUnknown(ui->objKindCombo, props.value("Kind", "0"));

    setSpinBoxValuePreservingRange(ui->objDirSpinBox, props.value("Dir", "0"), 0);
    setSpinBoxValuePreservingRange(ui->objMapXSpinBox, props.value("MapX", "0"), 0);
    setSpinBoxValuePreservingRange(ui->objMapYSpinBox, props.value("MapY", "0"), 0);
    setSpinBoxValuePreservingRange(ui->offsetXSpinBox, props.value("OffsetX", "0"), 0);
    setSpinBoxValuePreservingRange(ui->offsetYSpinBox, props.value("OffsetY", "0"), 0);

    selectComboValuePreservingUnknown(ui->objLumCombo, props.value("Lum", "0"));

    setSpinBoxValuePreservingRange(ui->damageSpinBox, props.value("Damage", "0"), 0);
    setSpinBoxValuePreservingRange(ui->frameSpinBox, props.value("Frame", "0"), 0);

    selectComboValuePreservingUnknown(ui->objStateCombo, props.value("State", "0"));
    ui->actionTimeEdit->setText(props.value("ActionTime", "0"));

    updatingFromCode = false;
}

void NpcDataEditorWindow::collectNpcProperties()
{
    int row = currentNpcEditRow;
    if (row < 0 || row >= npcEntries.size())
        return;

    auto& props = npcEntries[row].properties;

    props["Name"] = ui->nameEdit->text();
    props["Kind"] = ui->kindCombo->currentData().toString();
    props["NPCIni"] = ui->resourceIniEdit->text();
    props["Dir"] = QString::number(ui->dirSpinBox->value());
    props["MapX"] = QString::number(ui->mapXSpinBox->value());
    props["MapY"] = QString::number(ui->mapYSpinBox->value());
    props["ScriptFile"] = ui->scriptEdit->text();
    props["Action"] = ui->actionCombo->currentData().toString();
    props["WalkSpeed"] = QString::number(ui->walkSpeedSpinBox->value());
    props["StandSpeed"] = QString::number(ui->standSpeedSpinBox->value());
    props["PathFinder"] = ui->pathFinderCombo->currentData().toString();
    props["DialogRadius"] = QString::number(ui->dialogRadiusSpinBox->value());
    props["Relation"] = ui->relationCombo->currentData().toString();
    props["VisionRadius"] = QString::number(ui->visionRadiusSpinBox->value());
    props["AttackRadius"] = QString::number(ui->attackRadiusSpinBox->value());

    props["Life"] = QString::number(ui->lifeSpinBox->value());
    props["LifeMax"] = QString::number(ui->lifeMaxSpinBox->value());
    props["Thew"] = QString::number(ui->thewSpinBox->value());
    props["ThewMax"] = QString::number(ui->thewMaxSpinBox->value());
    props["Mana"] = QString::number(ui->manaSpinBox->value());
    props["ManaMax"] = QString::number(ui->manaMaxSpinBox->value());
    props["Attack"] = QString::number(ui->attackSpinBox->value());
    props["Defend"] = QString::number(ui->defendSpinBox->value());
    props["Evade"] = QString::number(ui->evadeSpinBox->value());
    props["Duck"] = QString::number(ui->duckSpinBox->value());
    props["Exp"] = QString::number(ui->expSpinBox->value());
    props["Level"] = QString::number(ui->levelSpinBox->value());
    props["LevelUpExp"] = QString::number(ui->levelUpExpSpinBox->value());
    props["AttackLevel"] = QString::number(ui->attackLevelSpinBox->value());
    props["MagicLevel"] = QString::number(ui->magicLevelSpinBox->value());

    props["BodyIni"] = ui->bodyIniEdit->text();
    props["FlyIni"] = ui->flyIniEdit->text();
    props["FlyIni2"] = ui->flyIni2Edit->text();
    props["FlyInis"] = ui->flyInisEdit->text();
    props["MagicIni"] = ui->magicIniEdit->text();
    props["DeathScript"] = ui->deathScriptEdit->text();
    props["Lum"] = ui->lumCombo->currentData().toString();
    props["State"] = QString::number(ui->stateSpinBox->value());
}

void NpcDataEditorWindow::collectObjectProperties()
{
    int row = currentObjectEditRow;
    if (row < 0 || row >= objectEntries.size())
        return;

    auto& props = objectEntries[row].properties;

    props["ObjName"] = ui->objNameEdit->text();
    props["ObjFile"] = ui->objFileEdit->text();
    props["ScriptFile"] = ui->objScriptEdit->text();
    props["WavFile"] = ui->wavFileEdit->text();
    props["Kind"] = ui->objKindCombo->currentData().toString();
    props["Dir"] = QString::number(ui->objDirSpinBox->value());
    props["MapX"] = QString::number(ui->objMapXSpinBox->value());
    props["MapY"] = QString::number(ui->objMapYSpinBox->value());
    props["OffsetX"] = QString::number(ui->offsetXSpinBox->value());
    props["OffsetY"] = QString::number(ui->offsetYSpinBox->value());
    props["Lum"] = ui->objLumCombo->currentData().toString();
    props["Damage"] = QString::number(ui->damageSpinBox->value());
    props["Frame"] = QString::number(ui->frameSpinBox->value());
    props["State"] = ui->objStateCombo->currentData().toString();
    props["ActionTime"] = ui->actionTimeEdit->text().trimmed();
}

bool NpcDataEditorWindow::normalizedNpcEntriesForSave(
    const QList<NpcData>& sourceEntries,
    QList<NpcData>& normalizedEntries,
    int& invalidRow,
    QString& invalidFieldKey,
    QString& invalidValue) const
{
    struct Field
    {
        QString key;
        FilePickerHelper::EntityResourceField resourceField;
    };
    const QList<Field> fields = {
        {"NPCIni",
            FilePickerHelper::EntityResourceField::NpcIni},
        {"ScriptFile",
            FilePickerHelper::EntityResourceField::ScriptFile},
        {"BodyIni",
            FilePickerHelper::EntityResourceField::BodyIni},
        {"FlyIni",
            FilePickerHelper::EntityResourceField::FlyIni},
        {"FlyIni2",
            FilePickerHelper::EntityResourceField::FlyIni},
        {"MagicIni",
            FilePickerHelper::EntityResourceField::MagicIni},
        {"DeathScript",
            FilePickerHelper::EntityResourceField::ScriptFile},
    };

    invalidRow = -1;
    invalidFieldKey.clear();
    invalidValue.clear();
    normalizedEntries = sourceEntries;
    for (int row = 0; row < normalizedEntries.size(); ++row)
    {
        auto& properties = normalizedEntries[row].properties;
        for (const Field& field : fields)
        {
            const QString value = properties.value(field.key);
            QString normalized;
            if (!FilePickerHelper::normalizeEntityResourceReference(
                    field.resourceField, value, normalized))
            {
                invalidRow = row;
                invalidFieldKey = field.key;
                invalidValue = value;
                return false;
            }
            if (properties.contains(field.key) || !normalized.isEmpty())
                properties[field.key] = normalized;
        }

        const QString magicList = properties.value("FlyInis");
        QString normalizedMagicList;
        if (!FilePickerHelper::normalizeEntityResourceReference(
                FilePickerHelper::EntityResourceField::FlyInis,
                magicList, normalizedMagicList))
        {
            invalidRow = row;
            invalidFieldKey = QStringLiteral("FlyInis");
            invalidValue = magicList;
            return false;
        }
        if (properties.contains("FlyInis") || !normalizedMagicList.isEmpty())
            properties["FlyInis"] = normalizedMagicList;
    }

    return true;
}

bool NpcDataEditorWindow::normalizeNpcResourceReferencesForSave()
{
    QList<NpcData> normalizedEntries;
    int invalidRow = -1;
    QString invalidFieldKey;
    QString invalidValue;
    if (!normalizedNpcEntriesForSave(
            npcEntries,
            normalizedEntries,
            invalidRow,
            invalidFieldKey,
            invalidValue))
    {
        ui->entityTabWidget->setCurrentIndex(0);
        ui->npcListWidget->setCurrentRow(invalidRow);
        ui->propertyTabWidget->setCurrentIndex(3);
        if (invalidFieldKey == QStringLiteral("FlyInis"))
        {
            QMessageBox::warning(this, tr("资源引用无效"),
                tr("飞行魔法列表必须使用 file.ini[:距离] 项，并以分号分隔；文件名相对于 ini/magic。\n\n当前值: %1")
                    .arg(invalidValue));
            ui->flyInisEdit->setFocus();
        }
        else
        {
            struct FieldUi
            {
                const char* key;
                QLineEdit* edit;
                QString label;
            };
            const QList<FieldUi> fields = {
                {"NPCIni", ui->resourceIniEdit, tr("资源 INI")},
                {"ScriptFile", ui->scriptEdit, tr("脚本文件")},
                {"BodyIni", ui->bodyIniEdit, tr("尸体 INI")},
                {"FlyIni", ui->flyIniEdit, tr("飞行魔法 1")},
                {"FlyIni2", ui->flyIni2Edit, tr("飞行魔法 2")},
                {"MagicIni", ui->magicIniEdit, tr("魔法 INI")},
                {"DeathScript", ui->deathScriptEdit, tr("死亡脚本")},
            };
            for (const FieldUi& field : fields)
            {
                if (invalidFieldKey == QLatin1String(field.key))
                {
                    QMessageBox::warning(this, tr("资源引用无效"),
                        tr("%1 必须是其运行时资源目录内的相对名称，不能包含绝对路径或其他业务目录。\n\n当前值: %2")
                            .arg(field.label, invalidValue));
                    field.edit->setFocus();
                    break;
                }
            }
        }
        return false;
    }

    const int selectedRow = currentNpcEditRow;
    npcEntries = std::move(normalizedEntries);
    if (selectedRow >= 0 && selectedRow < npcEntries.size())
        showNpcProperties(selectedRow);
    return true;
}

bool NpcDataEditorWindow::normalizedObjectEntriesForSave(
    const QList<ObjectData>& sourceEntries,
    QList<ObjectData>& normalizedEntries,
    int& invalidRow,
    QString& invalidFieldKey,
    QString& invalidValue) const
{
    struct Field
    {
        QString key;
        FilePickerHelper::EntityResourceField resourceField;
    };
    const QList<Field> fields = {
        {"ObjFile",
            FilePickerHelper::EntityResourceField::ObjFile},
        {"ScriptFile",
            FilePickerHelper::EntityResourceField::ScriptFile},
        {"WavFile",
            FilePickerHelper::EntityResourceField::WavFile},
    };

    invalidRow = -1;
    invalidFieldKey.clear();
    invalidValue.clear();
    normalizedEntries = sourceEntries;
    for (int row = 0; row < normalizedEntries.size(); ++row)
    {
        auto& properties = normalizedEntries[row].properties;
        const bool hasActionTime = properties.contains("ActionTime");
        const QString actionTimeText = hasActionTime
            ? properties.value("ActionTime").trimmed()
            : QStringLiteral("0");
        std::int64_t actionTime = 0;
        const bool actionTimeOk = INIFileEditor::tryParseInt64(
            actionTimeText.toStdString(), actionTime);
        if (!actionTimeOk || actionTime < 0)
        {
            invalidRow = row;
            invalidFieldKey = QStringLiteral("ActionTime");
            invalidValue = actionTimeText;
            return false;
        }
        if (hasActionTime)
            properties["ActionTime"] = QString::number(actionTime);

        for (const Field& field : fields)
        {
            const QString value = properties.value(field.key);
            QString normalized;
            if (!FilePickerHelper::normalizeEntityResourceReference(
                    field.resourceField, value, normalized))
            {
                invalidRow = row;
                invalidFieldKey = field.key;
                invalidValue = value;
                return false;
            }
            if (properties.contains(field.key) || !normalized.isEmpty())
            properties[field.key] = normalized;
        }
    }

    return true;
}

bool NpcDataEditorWindow::normalizeObjectResourceReferencesForSave()
{
    QList<ObjectData> normalizedEntries;
    int invalidRow = -1;
    QString invalidFieldKey;
    QString invalidValue;
    if (!normalizedObjectEntriesForSave(
            objectEntries,
            normalizedEntries,
            invalidRow,
            invalidFieldKey,
            invalidValue))
    {
        ui->entityTabWidget->setCurrentIndex(1);
        ui->objectListWidget->setCurrentRow(invalidRow);
        ui->propertyTabWidget->setCurrentIndex(4);
        if (invalidFieldKey == QStringLiteral("ActionTime"))
        {
            QMessageBox::warning(this, tr("字段值无效"),
                tr("动作已进行毫秒必须是 0 到 9223372036854775807 之间的整数。\n\n当前值: %1")
                    .arg(invalidValue));
            ui->actionTimeEdit->setFocus();
        }
        else
        {
            struct FieldUi
            {
                const char* key;
                QLineEdit* edit;
                QString label;
            };
            const QList<FieldUi> fields = {
                {"ObjFile", ui->objFileEdit, tr("资源 INI")},
                {"ScriptFile", ui->objScriptEdit, tr("脚本文件")},
                {"WavFile", ui->wavFileEdit, tr("音效文件")},
            };
            for (const FieldUi& field : fields)
            {
                if (invalidFieldKey == QLatin1String(field.key))
                {
                    QMessageBox::warning(this, tr("资源引用无效"),
                        tr("%1 必须是其运行时资源目录内的相对名称，不能包含绝对路径或其他业务目录。\n\n当前值: %2")
                            .arg(field.label, invalidValue));
                    field.edit->setFocus();
                    break;
                }
            }
        }
        return false;
    }

    const int selectedRow = currentObjectEditRow;
    objectEntries = std::move(normalizedEntries);
    if (selectedRow >= 0 && selectedRow < objectEntries.size())
        showObjectProperties(selectedRow);
    return true;
}

void NpcDataEditorWindow::updatePropertyTabVisibility()
{
    const int documentKind = ui->entityTabWidget->currentIndex();
    const bool isNpc = documentKind == 0;
    const bool isObject = documentKind == 1;
    const bool isNpcResource = documentKind == 2;

    ui->propertyTabWidget->setTabVisible(0, isNpc);
    ui->propertyTabWidget->setTabVisible(1, isNpc);
    ui->propertyTabWidget->setTabVisible(2, isNpc);
    ui->propertyTabWidget->setTabVisible(3, isNpc);
    ui->propertyTabWidget->setTabVisible(4, isObject);
    ui->propertyTabWidget->setTabVisible(5, isNpcResource);

    ui->actionMoveUp->setEnabled(!isNpcResource);
    ui->actionMoveDown->setEnabled(!isNpcResource);
    ui->actionDuplicateEntry->setEnabled(
        !isNpcResource || npcResourceSectionList->currentRow() >= 0);

    if (isNpc)
    {
        if (ui->propertyTabWidget->currentIndex() >= 4)
            ui->propertyTabWidget->setCurrentIndex(0);
    }
    else if (isObject)
    {
        ui->propertyTabWidget->setCurrentIndex(4);
    }
    else
    {
        ui->propertyTabWidget->setCurrentIndex(5);
    }

    bool enabled = false;
    if (isNpc)
        enabled = isNpcFile && ui->npcListWidget->currentRow() >= 0;
    else if (isObject)
        enabled = isObjectFile && ui->objectListWidget->currentRow() >= 0;
    else
        enabled = isNpcResourceFile &&
            npcResourceSectionList->currentRow() >= 0;
    ui->propertyTabWidget->setEnabled(enabled);
    const int npcRow = ui->npcListWidget->currentRow();
    editDialogueAction->setEnabled(
        isNpc && isNpcFile && npcRow >= 0 && npcRow < npcEntries.size() &&
        !npcEntries[npcRow].properties.value("ScriptFile").trimmed().isEmpty() &&
        !currentNpcMapName.trimmed().isEmpty());
}

QString NpcDataEditorWindow::currentNpcDialogueScriptPath() const
{
    const int row = ui->npcListWidget->currentRow();
    if (assetsBasePath.isEmpty() || row < 0 || row >= npcEntries.size())
        return {};
    QString reference = QDir::fromNativeSeparators(
        npcEntries[row].properties.value("ScriptFile").trimmed());
    if (reference.isEmpty())
        return {};

    QString candidate;
    if (QDir::isAbsolutePath(reference))
    {
        candidate = reference;
    }
    else if (reference.startsWith(QStringLiteral("script/"),
                                  Qt::CaseInsensitive))
    {
        candidate = QDir(assetsBasePath).filePath(reference);
    }
    else if (reference.startsWith(QStringLiteral("map/"),
                                  Qt::CaseInsensitive))
    {
        candidate = QDir(assetsBasePath).filePath(
            QStringLiteral("script/") + reference);
    }
    else
    {
        const QString mapFolder = QFileInfo(
            QDir::fromNativeSeparators(currentNpcMapName)).completeBaseName();
        if (mapFolder.isEmpty())
            return {};
        candidate = QDir(assetsBasePath).filePath(
            QStringLiteral("script/map/%1/%2")
                .arg(mapFolder, reference));
    }

    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(candidate);
    return EditorAssetPath::isInside(assetsBasePath, normalized)
        ? normalized : QString();
}
