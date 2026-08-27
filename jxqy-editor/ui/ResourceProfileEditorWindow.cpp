#include "ResourceProfileEditorWindow.h"
#include "../core/AuthoringMutationGate.h"
#include "../core/EditorAssetPath.h"
#include "../core/BuildVersion.h"
#include "../core/DurableFileTransaction.h"
#include "../core/ProjectManager.h"
#include "../core/INIFileEditor.h"
#include "../core/OnlineResourcePackageExporter.h"
#include "../core/ResourcePathValidation.h"
#include "../../src/File/StrictRelativeResourcePath.h"
#include "../../src/Game/Data/DefeatedNpcExperience.h"
#include "../../src/Resource/ModReleaseAssets.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QStandardPaths>
#include <QPlainTextEdit>
#include <QCloseEvent>
#include <QImage>
#include <QPixmap>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QRegularExpression>

#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr qsizetype MaximumTeamInfoBytes = 64 * 1024;

enum class LogicalResourceReadStatus
{
    Ready,
    NotFound,
    InvalidPath,
    NotRegularFile,
    TooLarge,
    ReadFailed,
    InvalidText
};

LogicalResourceReadStatus readBoundedLogicalResource(
    const QString& rootPath,
    const QString& relativePath,
    qsizetype maximumBytes,
    QByteArray& bytes)
{
    bytes.clear();
    QString resolvedPath;
    if (maximumBytes < 0 ||
        !EditorAssetPath::resolveLogicalResourcePath(
            rootPath,
            relativePath,
            resolvedPath))
    {
        return LogicalResourceReadStatus::InvalidPath;
    }

    const QFileInfo fileInfo(resolvedPath);
    if (!fileInfo.exists())
        return LogicalResourceReadStatus::NotFound;
    if (!fileInfo.isFile())
        return LogicalResourceReadStatus::NotRegularFile;
    if (fileInfo.size() < 0 ||
        fileInfo.size() > maximumBytes)
    {
        return LogicalResourceReadStatus::TooLarge;
    }

    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly))
        return LogicalResourceReadStatus::ReadFailed;
    bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError)
    {
        bytes.clear();
        return LogicalResourceReadStatus::ReadFailed;
    }
    if (bytes.size() > maximumBytes)
    {
        bytes.clear();
        return LogicalResourceReadStatus::TooLarge;
    }
    return LogicalResourceReadStatus::Ready;
}

LogicalResourceReadStatus readLogicalDescription(
    const QString& rootPath,
    const QString& relativePath,
    QString& text)
{
    text.clear();
    QByteArray bytes;
    LogicalResourceReadStatus status =
        readBoundedLogicalResource(
            rootPath,
            relativePath,
            static_cast<qsizetype>(
                ModRelease::MaximumDescriptionBytes),
            bytes);
    if (status != LogicalResourceReadStatus::Ready)
        return status;
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);

    const std::string utf8(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
    if (!ModRelease::isValidDescriptionUtf8(utf8))
        return LogicalResourceReadStatus::InvalidText;
    text = QString::fromUtf8(
        bytes.constData(),
        bytes.size());
    return LogicalResourceReadStatus::Ready;
}

bool assetsPathsEqual(const QString& left, const QString& right)
{
    if (left.trimmed().isEmpty() || right.trimmed().isEmpty())
        return left.trimmed().isEmpty() && right.trimmed().isEmpty();

    const QString normalizedLeft = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
    const QString normalizedRight = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
    return normalizedLeft.compare(normalizedRight, caseSensitivity) == 0;
}

bool isValidTeamInfoUtf8(const QByteArray& bytes)
{
    const std::string text(bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
    if (text.find('\0') != std::string::npos ||
        !ResourcePathSafety::isValidUtf8(text))
    {
        return false;
    }
    for (unsigned char character : text)
    {
        if (character < 0x20 && character != '\r' && character != '\n' &&
            character != '\t')
        {
            return false;
        }
    }
    return true;
}

std::string exactTrimmedUtf8(const QString& text)
{
    const QByteArray utf8 = text.trimmed().toUtf8();
    return std::string(
        utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString normalizedRelativePathKey(const QString& path)
{
    QString key = path.trimmed();
    key.replace('\\', '/');
    return key;
}

bool isEditorTransactionStatePath(const QString& relativePath)
{
    QString normalized = relativePath.trimmed();
    normalized.replace('\\', '/');
    return normalized.compare(
               QStringLiteral(".jxqy_editor"),
               Qt::CaseInsensitive) == 0 ||
        normalized.startsWith(
            QStringLiteral(".jxqy_editor/"),
            Qt::CaseInsensitive);
}

bool readFileSnapshot(
    const QString& path,
    bool& exists,
    QByteArray& bytes)
{
    exists = QFileInfo::exists(path);
    bytes.clear();
    if (!exists)
    {
        return true;
    }
    if (!QFileInfo(path).isFile())
    {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return false;
    }
    bytes = file.readAll();
    return file.error() == QFileDevice::NoError;
}

QString compatibilityStatusText(
    ModRelease::CompatibilityStatus status,
    const QString& currentEngineVersion,
    const QString& minimumEngineVersion)
{
    switch (status)
    {
    case ModRelease::CompatibilityStatus::LegacyCompatible:
        return ResourceProfileEditorWindow::tr(
            "兼容状态：旧清单（未声明最低引擎版本）");
    case ModRelease::CompatibilityStatus::Compatible:
        return ResourceProfileEditorWindow::tr(
            "兼容状态：可由当前引擎 %1 运行").arg(currentEngineVersion);
    case ModRelease::CompatibilityStatus::RequiresNewerEngine:
        return ResourceProfileEditorWindow::tr(
            "兼容状态：需要引擎 %1，当前为 %2")
            .arg(minimumEngineVersion, currentEngineVersion);
    case ModRelease::CompatibilityStatus::InvalidMinimumEngineVersion:
        return ResourceProfileEditorWindow::tr(
            "兼容状态：最低引擎版本不是严格 SemVer");
    case ModRelease::CompatibilityStatus::InvalidCurrentEngineVersion:
        return ResourceProfileEditorWindow::tr(
            "兼容状态：当前构建的引擎版本无效");
    }
    return QString();
}
}

ResourceProfileEditorWindow::ResourceProfileEditorWindow(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

ResourceProfileEditorWindow::~ResourceProfileEditorWindow()
{
}

ClosePlan ResourceProfileEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    if (!m_hasUnsavedChanges)
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }

    const auto result = QMessageBox::question(
        const_cast<ResourceProfileEditorWindow*>(this),
        tr("未保存的修改"),
        tr("当前资源包有未保存的修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (result == QMessageBox::Cancel)
        plan.decisions.append(CloseDecision::Cancelled);
    else if (result == QMessageBox::Yes)
        plan.decisions.append(CloseDecision::Save);
    else
        plan.decisions.append(CloseDecision::Discard);
    return plan;
}

bool ResourceProfileEditorWindow::resolveCloseTransaction(
    const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return false;
    if (plan.decisions.front() == CloseDecision::Save)
    {
        onSave();
        return !m_hasUnsavedChanges;
    }
    return true;
}

void ResourceProfileEditorWindow::commitCloseTransaction(
    const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return;
    if (plan.decisions.front() == CloseDecision::Discard)
        m_hasUnsavedChanges = false;
    allowPreparedClose();
}

void ResourceProfileEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose())
    {
        event->accept();
        return;
    }

    if (confirmSaveIfModified())
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

void ResourceProfileEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void ResourceProfileEditorWindow::setupUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(content);
    scrollArea->setWidget(content);
    outerLayout->addWidget(scrollArea);

    // 顶部：资源包选择区
    auto* topGroup = new QGroupBox(this);
    topGroup->setObjectName(QStringLiteral("resourcePackGroup"));
    auto* topLayout = new QFormLayout(topGroup);
    m_assetsPathLabel = new QLabel(this);
    m_assetsPathLabel->setWordWrap(true);
    m_packCombo = new QComboBox(this);
    m_packCombo->setObjectName("resourcePackCombo");
    m_packCombo->setMinimumWidth(300);
    m_packPathLabel = new QLabel(this);
    m_packPathLabel->setWordWrap(true);
    m_activePackLabel = new QLabel(this);
    m_activePackLabel->setObjectName(QStringLiteral("activeResourcePackLabel"));
    m_createDefaultBtn = new QPushButton(this);
    m_createDefaultBtn->setObjectName(
        QStringLiteral("createDefaultManifestButton"));
    m_setActivePackBtn = new QPushButton(this);
    m_setActivePackBtn->setObjectName(
        QStringLiteral("setActiveResourcePackButton"));

    auto* assetsPathFieldLabel = new QLabel(this);
    assetsPathFieldLabel->setObjectName(QStringLiteral("assetsPathFieldLabel"));
    auto* packFieldLabel = new QLabel(this);
    packFieldLabel->setObjectName(QStringLiteral("packFieldLabel"));
    auto* packPathFieldLabel = new QLabel(this);
    packPathFieldLabel->setObjectName(QStringLiteral("packPathFieldLabel"));
    auto* activePackFieldLabel = new QLabel(this);
    activePackFieldLabel->setObjectName(
        QStringLiteral("activePackFieldLabel"));
    auto* activePackRow = new QWidget(this);
    auto* activePackLayout = new QHBoxLayout(activePackRow);
    activePackLayout->setContentsMargins(0, 0, 0, 0);
    activePackLayout->addWidget(m_activePackLabel, 1);
    activePackLayout->addWidget(m_setActivePackBtn);
    topLayout->addRow(assetsPathFieldLabel, m_assetsPathLabel);
    topLayout->addRow(packFieldLabel, m_packCombo);
    topLayout->addRow(packPathFieldLabel, m_packPathLabel);
    topLayout->addRow(activePackFieldLabel, activePackRow);
    topLayout->addRow("", m_createDefaultBtn);

    mainLayout->addWidget(topGroup);

    connect(m_packCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onPackSelectionChanged);
    connect(m_createDefaultBtn, &QPushButton::clicked,
        this, &ResourceProfileEditorWindow::onCreateDefaultManifest);
    connect(m_setActivePackBtn, &QPushButton::clicked,
        this, &ResourceProfileEditorWindow::onSetActiveResourcePack);

    // [Game] 区
    auto* gameGroup = new QGroupBox(this);
    gameGroup->setObjectName(QStringLiteral("gameGroup"));
    auto* gameLayout = new QFormLayout(gameGroup);
    m_idEdit = new QLineEdit(this);
    m_idEdit->setObjectName("profileIdEdit");
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("profileNameEdit");
    m_authorEdit = new QLineEdit(this);
    m_authorEdit->setObjectName("profileAuthorEdit");
    m_profileVersionEdit = new QLineEdit(this);
    m_profileVersionEdit->setObjectName("profileVersionEdit");
    m_typeSpin = new QSpinBox(this);
    m_typeSpin->setObjectName("profileTypeSpin");
    m_typeSpin->setRange(0, 3);
    m_inheritTypeCheck = new QCheckBox(this);
    m_inheritTypeCheck->setObjectName("inheritTypeCheck");
    m_useWavCheck = new QCheckBox(this);
    gameLayout->addRow("Id:", m_idEdit);
    auto* nameFieldLabel = new QLabel(this);
    nameFieldLabel->setObjectName(QStringLiteral("profileNameFieldLabel"));
    gameLayout->addRow(nameFieldLabel, m_nameEdit);
    auto* authorFieldLabel = new QLabel(this);
    authorFieldLabel->setObjectName(QStringLiteral("profileAuthorFieldLabel"));
    gameLayout->addRow(authorFieldLabel, m_authorEdit);
    auto* profileVersionFieldLabel = new QLabel(this);
    profileVersionFieldLabel->setObjectName(
        QStringLiteral("profileVersionFieldLabel"));
    gameLayout->addRow(profileVersionFieldLabel, m_profileVersionEdit);
    gameLayout->addRow("Type:", m_typeSpin);
    gameLayout->addRow("", m_inheritTypeCheck);
    gameLayout->addRow("", m_useWavCheck);
    mainLayout->addWidget(gameGroup);

    // [Experience] 区：模式和倍率都可以独立使用 Game.Type 默认值。
    auto* experienceGroup = new QGroupBox(this);
    experienceGroup->setObjectName(QStringLiteral("experienceGroup"));
    auto* experienceLayout = new QFormLayout(experienceGroup);
    m_experienceModeCombo = new QComboBox(this);
    m_experienceModeCombo->setObjectName(
        QStringLiteral("experienceModeCombo"));
    auto* experienceModeFieldLabel = new QLabel(this);
    experienceModeFieldLabel->setObjectName(
        QStringLiteral("experienceModeFieldLabel"));
    experienceLayout->addRow(
        experienceModeFieldLabel, m_experienceModeCombo);

    m_experienceMultiplierCheck = new QCheckBox(this);
    m_experienceMultiplierCheck->setObjectName(
        QStringLiteral("experienceMultiplierCheck"));
    m_experienceMultiplierSpin = new QDoubleSpinBox(this);
    m_experienceMultiplierSpin->setObjectName(
        QStringLiteral("experienceMultiplierSpin"));
    m_experienceMultiplierSpin->setRange(0.0, 1000.0);
    m_experienceMultiplierSpin->setDecimals(3);
    m_experienceMultiplierSpin->setSingleStep(0.1);
    auto* multiplierRow = new QWidget(this);
    auto* multiplierLayout = new QHBoxLayout(multiplierRow);
    multiplierLayout->setContentsMargins(0, 0, 0, 0);
    multiplierLayout->addWidget(m_experienceMultiplierCheck);
    multiplierLayout->addWidget(m_experienceMultiplierSpin, 1);
    auto* experienceMultiplierFieldLabel = new QLabel(this);
    experienceMultiplierFieldLabel->setObjectName(
        QStringLiteral("experienceMultiplierFieldLabel"));
    experienceLayout->addRow(
        experienceMultiplierFieldLabel, multiplierRow);

    m_levelUpThresholdModeCombo = new QComboBox(this);
    m_levelUpThresholdModeCombo->setObjectName(
        QStringLiteral("levelUpThresholdModeCombo"));
    auto* levelUpThresholdModeFieldLabel = new QLabel(this);
    levelUpThresholdModeFieldLabel->setObjectName(
        QStringLiteral("levelUpThresholdModeFieldLabel"));
    experienceLayout->addRow(
        levelUpThresholdModeFieldLabel, m_levelUpThresholdModeCombo);

    m_experiencePreviewLabel = new QLabel(this);
    m_experiencePreviewLabel->setObjectName(
        QStringLiteral("experiencePreviewLabel"));
    m_experiencePreviewLabel->setWordWrap(true);
    auto* experiencePreviewFieldLabel = new QLabel(this);
    experiencePreviewFieldLabel->setObjectName(
        QStringLiteral("experiencePreviewFieldLabel"));
    experienceLayout->addRow(
        experiencePreviewFieldLabel, m_experiencePreviewLabel);
    mainLayout->addWidget(experienceGroup);

    // [Gameplay] 区：数值可以显式写入，也可保留旧资源的类型回退。
    auto* gameplayGroup = new QGroupBox(this);
    gameplayGroup->setObjectName(QStringLiteral("gameplayGroup"));
    auto* gameplayLayout = new QFormLayout(gameplayGroup);
    m_partnerFollowRadiusCheck = new QCheckBox(this);
    m_partnerFollowRadiusCheck->setObjectName(
        QStringLiteral("partnerFollowRadiusCheck"));
    m_partnerFollowRadiusSpin = new QSpinBox(this);
    m_partnerFollowRadiusSpin->setObjectName(
        QStringLiteral("partnerFollowRadiusSpin"));
    m_partnerFollowRadiusSpin->setRange(
        0, std::numeric_limits<int>::max());
    auto* partnerFollowRadiusRow = new QWidget(this);
    auto* partnerFollowRadiusLayout =
        new QHBoxLayout(partnerFollowRadiusRow);
    partnerFollowRadiusLayout->setContentsMargins(0, 0, 0, 0);
    partnerFollowRadiusLayout->addWidget(m_partnerFollowRadiusCheck);
    partnerFollowRadiusLayout->addWidget(m_partnerFollowRadiusSpin, 1);
    auto* partnerFollowRadiusFieldLabel = new QLabel(this);
    partnerFollowRadiusFieldLabel->setObjectName(
        QStringLiteral("partnerFollowRadiusFieldLabel"));
    gameplayLayout->addRow(
        partnerFollowRadiusFieldLabel, partnerFollowRadiusRow);

    m_partnerFollowRunRadiusCheck = new QCheckBox(this);
    m_partnerFollowRunRadiusCheck->setObjectName(
        QStringLiteral("partnerFollowRunRadiusCheck"));
    m_partnerFollowRunRadiusSpin = new QSpinBox(this);
    m_partnerFollowRunRadiusSpin->setObjectName(
        QStringLiteral("partnerFollowRunRadiusSpin"));
    m_partnerFollowRunRadiusSpin->setRange(
        0, std::numeric_limits<int>::max());
    auto* partnerFollowRunRadiusRow = new QWidget(this);
    auto* partnerFollowRunRadiusLayout =
        new QHBoxLayout(partnerFollowRunRadiusRow);
    partnerFollowRunRadiusLayout->setContentsMargins(0, 0, 0, 0);
    partnerFollowRunRadiusLayout->addWidget(m_partnerFollowRunRadiusCheck);
    partnerFollowRunRadiusLayout->addWidget(
        m_partnerFollowRunRadiusSpin, 1);
    auto* partnerFollowRunRadiusFieldLabel = new QLabel(this);
    partnerFollowRunRadiusFieldLabel->setObjectName(
        QStringLiteral("partnerFollowRunRadiusFieldLabel"));
    gameplayLayout->addRow(
        partnerFollowRunRadiusFieldLabel, partnerFollowRunRadiusRow);
    mainLayout->addWidget(gameplayGroup);

    // [Combat] 区：战斗数值由资源包显式声明，不再从 Game.Type 推导。
    auto* combatGroup = new QGroupBox(this);
    combatGroup->setObjectName(QStringLiteral("combatGroup"));
    auto* combatLayout = new QFormLayout(combatGroup);
    m_minimumMagicDamageSpin = new QSpinBox(this);
    m_minimumMagicDamageSpin->setObjectName(
        QStringLiteral("minimumMagicDamageSpin"));
    m_minimumMagicDamageSpin->setRange(
        0, std::numeric_limits<int>::max());
    auto* minimumMagicDamageFieldLabel = new QLabel(this);
    minimumMagicDamageFieldLabel->setObjectName(
        QStringLiteral("minimumMagicDamageFieldLabel"));
    combatLayout->addRow(
        minimumMagicDamageFieldLabel, m_minimumMagicDamageSpin);
    m_magicEffectCalculationModeCombo = new QComboBox(this);
    m_magicEffectCalculationModeCombo->setObjectName(
        QStringLiteral("magicEffectCalculationModeCombo"));
    auto* magicEffectCalculationModeFieldLabel = new QLabel(this);
    magicEffectCalculationModeFieldLabel->setObjectName(
        QStringLiteral("magicEffectCalculationModeFieldLabel"));
    combatLayout->addRow(
        magicEffectCalculationModeFieldLabel,
        m_magicEffectCalculationModeCombo);
    mainLayout->addWidget(combatGroup);

    // [Script] 区：脚本协议独立于 Game.Type，可为旧资源保留回退。
    auto* scriptGroup = new QGroupBox(this);
    scriptGroup->setObjectName(QStringLiteral("scriptGroup"));
    auto* scriptLayout = new QFormLayout(scriptGroup);
    m_npcActionProfileCombo = new QComboBox(this);
    m_npcActionProfileCombo->setObjectName(
        QStringLiteral("npcActionProfileCombo"));
    auto* npcActionProfileFieldLabel = new QLabel(this);
    npcActionProfileFieldLabel->setObjectName(
        QStringLiteral("npcActionProfileFieldLabel"));
    scriptLayout->addRow(
        npcActionProfileFieldLabel, m_npcActionProfileCombo);
    m_npcRuntimeProfileCombo = new QComboBox(this);
    m_npcRuntimeProfileCombo->setObjectName(
        QStringLiteral("npcRuntimeProfileCombo"));
    auto* npcRuntimeProfileFieldLabel = new QLabel(this);
    npcRuntimeProfileFieldLabel->setObjectName(
        QStringLiteral("npcRuntimeProfileFieldLabel"));
    scriptLayout->addRow(
        npcRuntimeProfileFieldLabel, m_npcRuntimeProfileCombo);
    m_specialActionModeCombo = new QComboBox(this);
    m_specialActionModeCombo->setObjectName(
        QStringLiteral("specialActionModeCombo"));
    auto* specialActionModeFieldLabel = new QLabel(this);
    specialActionModeFieldLabel->setObjectName(
        QStringLiteral("specialActionModeFieldLabel"));
    scriptLayout->addRow(
        specialActionModeFieldLabel, m_specialActionModeCombo);
    m_addLifeModeCombo = new QComboBox(this);
    m_addLifeModeCombo->setObjectName(
        QStringLiteral("addLifeModeCombo"));
    auto* addLifeModeFieldLabel = new QLabel(this);
    addLifeModeFieldLabel->setObjectName(
        QStringLiteral("addLifeModeFieldLabel"));
    scriptLayout->addRow(addLifeModeFieldLabel, m_addLifeModeCombo);
    mainLayout->addWidget(scriptGroup);

    // [LevelUp] 区：提示模板与 Magic INI 都由资源包声明。
    auto* levelUpGroup = new QGroupBox(this);
    levelUpGroup->setObjectName(QStringLiteral("levelUpGroup"));
    auto* levelUpLayout = new QFormLayout(levelUpGroup);
    m_levelUpMessageEdit = new QLineEdit(this);
    m_levelUpMessageEdit->setObjectName(
        QStringLiteral("levelUpMessageEdit"));
    auto* levelUpMessageFieldLabel = new QLabel(this);
    levelUpMessageFieldLabel->setObjectName(
        QStringLiteral("levelUpMessageFieldLabel"));
    levelUpLayout->addRow(
        levelUpMessageFieldLabel, m_levelUpMessageEdit);
    m_levelUpRandomEffectsEdit = new QPlainTextEdit(this);
    m_levelUpRandomEffectsEdit->setObjectName(
        QStringLiteral("levelUpRandomEffectsEdit"));
    m_levelUpRandomEffectsEdit->setMinimumHeight(90);
    auto* levelUpRandomEffectsFieldLabel = new QLabel(this);
    levelUpRandomEffectsFieldLabel->setObjectName(
        QStringLiteral("levelUpRandomEffectsFieldLabel"));
    levelUpLayout->addRow(
        levelUpRandomEffectsFieldLabel,
        m_levelUpRandomEffectsEdit);
    m_levelUpMaleEffectEdit = new QLineEdit(this);
    m_levelUpMaleEffectEdit->setObjectName(
        QStringLiteral("levelUpMaleEffectEdit"));
    auto* levelUpMaleEffectFieldLabel = new QLabel(this);
    levelUpMaleEffectFieldLabel->setObjectName(
        QStringLiteral("levelUpMaleEffectFieldLabel"));
    levelUpLayout->addRow(
        levelUpMaleEffectFieldLabel,
        createPickerRow(
            m_levelUpMaleEffectEdit,
            [this]() { onPickLevelUpMaleEffect(); }));
    m_levelUpFemaleEffectEdit = new QLineEdit(this);
    m_levelUpFemaleEffectEdit->setObjectName(
        QStringLiteral("levelUpFemaleEffectEdit"));
    auto* levelUpFemaleEffectFieldLabel = new QLabel(this);
    levelUpFemaleEffectFieldLabel->setObjectName(
        QStringLiteral("levelUpFemaleEffectFieldLabel"));
    levelUpLayout->addRow(
        levelUpFemaleEffectFieldLabel,
        createPickerRow(
            m_levelUpFemaleEffectEdit,
            [this]() { onPickLevelUpFemaleEffect(); }));
    mainLayout->addWidget(levelUpGroup);

    // [Release] 区：只描述当前 MOD 自身的发布信息和品牌文件。
    auto* releaseGroup = new QGroupBox(this);
    releaseGroup->setObjectName(QStringLiteral("releaseGroup"));
    auto* releaseLayout = new QFormLayout(releaseGroup);
    m_releaseDateEdit = new QLineEdit(this);
    m_releaseDateEdit->setObjectName(QStringLiteral("releaseDateEdit"));
    auto* releaseDateFieldLabel = new QLabel(this);
    releaseDateFieldLabel->setObjectName(
        QStringLiteral("releaseDateFieldLabel"));
    releaseLayout->addRow(releaseDateFieldLabel, m_releaseDateEdit);

    m_minimumEngineVersionEdit = new QLineEdit(this);
    m_minimumEngineVersionEdit->setObjectName(
        QStringLiteral("minimumEngineVersionEdit"));
    auto* minimumEngineVersionFieldLabel = new QLabel(this);
    minimumEngineVersionFieldLabel->setObjectName(
        QStringLiteral("minimumEngineVersionFieldLabel"));
    releaseLayout->addRow(
        minimumEngineVersionFieldLabel, m_minimumEngineVersionEdit);
    m_releaseCompatibilityLabel = new QLabel(this);
    m_releaseCompatibilityLabel->setObjectName(
        QStringLiteral("releaseCompatibilityLabel"));
    m_releaseCompatibilityLabel->setWordWrap(true);
    releaseLayout->addRow(QString(), m_releaseCompatibilityLabel);

    m_releaseCoverEdit = new QLineEdit(this);
    m_releaseCoverEdit->setObjectName(QStringLiteral("releaseCoverEdit"));
    auto* releaseCoverFieldLabel = new QLabel(this);
    releaseCoverFieldLabel->setObjectName(
        QStringLiteral("releaseCoverFieldLabel"));
    releaseLayout->addRow(
        releaseCoverFieldLabel,
        createPickerRow(
            m_releaseCoverEdit,
            [this]() { onPickReleaseCover(); },
            QStringLiteral("releaseCoverPickerButton")));
    m_releaseCoverPreviewLabel = new QLabel(this);
    m_releaseCoverPreviewLabel->setObjectName(
        QStringLiteral("releaseCoverPreviewLabel"));
    m_releaseCoverPreviewLabel->setAlignment(Qt::AlignCenter);
    m_releaseCoverPreviewLabel->setMinimumSize(240, 135);
    m_releaseCoverPreviewLabel->setMaximumHeight(220);
    m_releaseCoverPreviewLabel->setFrameShape(QFrame::StyledPanel);
    auto* releaseCoverPreviewFieldLabel = new QLabel(this);
    releaseCoverPreviewFieldLabel->setObjectName(
        QStringLiteral("releaseCoverPreviewFieldLabel"));
    releaseLayout->addRow(
        releaseCoverPreviewFieldLabel, m_releaseCoverPreviewLabel);

    m_releaseDescriptionFileEdit = new QLineEdit(this);
    m_releaseDescriptionFileEdit->setObjectName(
        QStringLiteral("releaseDescriptionFileEdit"));
    auto* releaseDescriptionFileFieldLabel = new QLabel(this);
    releaseDescriptionFileFieldLabel->setObjectName(
        QStringLiteral("releaseDescriptionFileFieldLabel"));
    releaseLayout->addRow(
        releaseDescriptionFileFieldLabel,
        createPickerRow(
            m_releaseDescriptionFileEdit,
            [this]() { onPickReleaseDescriptionFile(); },
            QStringLiteral("releaseDescriptionFilePickerButton")));
    m_reloadReleaseDescriptionBtn = new QPushButton(this);
    m_reloadReleaseDescriptionBtn->setObjectName(
        QStringLiteral("reloadReleaseDescriptionButton"));
    releaseLayout->addRow(QString(), m_reloadReleaseDescriptionBtn);
    m_releaseDescriptionTextEdit = new QPlainTextEdit(this);
    m_releaseDescriptionTextEdit->setObjectName(
        QStringLiteral("releaseDescriptionTextEdit"));
    m_releaseDescriptionTextEdit->setMinimumHeight(140);
    auto* releaseDescriptionTextFieldLabel = new QLabel(this);
    releaseDescriptionTextFieldLabel->setObjectName(
        QStringLiteral("releaseDescriptionTextFieldLabel"));
    releaseLayout->addRow(
        releaseDescriptionTextFieldLabel, m_releaseDescriptionTextEdit);
    m_exportOnlinePackageBtn = new QPushButton(this);
    m_exportOnlinePackageBtn->setObjectName(
        QStringLiteral("exportOnlinePackageButton"));
    releaseLayout->addRow(QString(), m_exportOnlinePackageBtn);
    mainLayout->addWidget(releaseGroup);

    // [Resource] / [Save] 区
    auto* resourceGroup = new QGroupBox(this);
    resourceGroup->setObjectName(QStringLiteral("resourceGroup"));
    auto* resourceLayout = new QFormLayout(resourceGroup);
    m_dependencyIdEdit = new QLineEdit(this);
    m_dependencyIdEdit->setObjectName("dependencyIdEdit");
    m_resourceOnlyCheck = new QCheckBox(this);
    m_resourceOnlyCheck->setObjectName(
        QStringLiteral("resourceOnlyCheck"));
    m_textEncodingConvertedCheck = new QCheckBox(this);
    m_textEncodingConvertedCheck->setObjectName(
        QStringLiteral("textEncodingConvertedCheck"));
    m_saveNamespaceEdit = new QLineEdit(this);
    m_saveNamespaceEdit->setObjectName("saveNamespaceEdit");
    resourceLayout->addRow("DependencyId:", m_dependencyIdEdit);
    resourceLayout->addRow(QString(), m_resourceOnlyCheck);
    resourceLayout->addRow(QString(), m_textEncodingConvertedCheck);
    resourceLayout->addRow("Save Namespace:", m_saveNamespaceEdit);
    mainLayout->addWidget(resourceGroup);

    // [UI] / [Features] 区：UI 基底、布局族和功能能力相互独立。
    auto* uiGroup = new QGroupBox(this);
    uiGroup->setObjectName(QStringLiteral("uiFeaturesGroup"));
    auto* uiLayout = new QFormLayout(uiGroup);
    m_uiBaseIdEdit = new QLineEdit(this);
    m_uiBaseIdEdit->setObjectName("uiBaseIdEdit");
    m_uiProfileEdit = new QLineEdit(this);
    m_uiProfileEdit->setObjectName("uiProfileEdit");
    m_preferLocalUiCheck = new QCheckBox(this);
    m_featuresEdit = new QPlainTextEdit(this);
    m_featuresEdit->setObjectName("featuresEdit");
    m_featuresEdit->setMinimumHeight(72);
    uiLayout->addRow("BaseId:", m_uiBaseIdEdit);
    uiLayout->addRow("Profile:", m_uiProfileEdit);
    uiLayout->addRow("", m_preferLocalUiCheck);
    auto* featuresFieldLabel = new QLabel(this);
    featuresFieldLabel->setObjectName(QStringLiteral("featuresFieldLabel"));
    uiLayout->addRow(featuresFieldLabel, m_featuresEdit);
    mainLayout->addWidget(uiGroup);

    // [Startup] 区
    auto* startupGroup = new QGroupBox(this);
    startupGroup->setObjectName(QStringLiteral("startupGroup"));
    auto* startupLayout = new QVBoxLayout(startupGroup);
    m_videosList = new QListWidget(this);
    m_videosList->setMinimumHeight(100);
    auto* videoBtnLayout = new QHBoxLayout();
    m_addVideoBtn = new QPushButton(this);
    m_removeVideoBtn = new QPushButton(this);
    m_videoUpBtn = new QPushButton(this);
    m_videoDownBtn = new QPushButton(this);
    videoBtnLayout->addWidget(m_addVideoBtn);
    videoBtnLayout->addWidget(m_removeVideoBtn);
    videoBtnLayout->addWidget(m_videoUpBtn);
    videoBtnLayout->addWidget(m_videoDownBtn);
    videoBtnLayout->addStretch();
    startupLayout->addWidget(m_videosList);
    startupLayout->addLayout(videoBtnLayout);
    mainLayout->addWidget(startupGroup);

    connect(m_addVideoBtn, &QPushButton::clicked, this, &ResourceProfileEditorWindow::onAddVideo);
    connect(m_removeVideoBtn, &QPushButton::clicked, this, &ResourceProfileEditorWindow::onRemoveVideo);
    connect(m_videoUpBtn, &QPushButton::clicked, this, &ResourceProfileEditorWindow::onMoveVideoUp);
    connect(m_videoDownBtn, &QPushButton::clicked, this, &ResourceProfileEditorWindow::onMoveVideoDown);

    // [Title] 区
    auto* titleGroup = new QGroupBox(this);
    titleGroup->setObjectName(QStringLiteral("titleGroup"));
    auto* titleLayout = new QFormLayout(titleGroup);
    m_titleMenuEdit = new QLineEdit(this);
    m_titleMenuEdit->setObjectName("titleMenuEdit");
    m_titleNewYearMenuEdit = new QLineEdit(this);
    m_titleNewYearMenuEdit->setObjectName("titleNewYearMenuEdit");
    m_titleMusicEdit = new QLineEdit(this);
    m_teamVideoEdit = new QLineEdit(this);
    titleLayout->addRow("Menu:", createPickerRow(m_titleMenuEdit, [this]() { onPickTitleMenu(); }));
    titleLayout->addRow("NewYearMenu:", createPickerRow(m_titleNewYearMenuEdit, [this]() { onPickTitleNewYearMenu(); }));
    titleLayout->addRow("Music:", createPickerRow(m_titleMusicEdit, [this]() { onPickTitleMusic(); }));
    titleLayout->addRow("TeamVideo:", createPickerRow(m_teamVideoEdit, [this]() { onPickTeamVideo(); }));

    mainLayout->addWidget(titleGroup);

    // [Team] 区：只记录当前 MOD 自身的团队说明文件。
    auto* teamGroup = new QGroupBox(this);
    teamGroup->setObjectName(QStringLiteral("teamGroup"));
    auto* teamLayout = new QFormLayout(teamGroup);
    m_teamInfoFileEdit = new QLineEdit(this);
    m_teamInfoFileEdit->setObjectName("teamInfoFileEdit");
    auto* teamInfoFileFieldLabel = new QLabel(this);
    teamInfoFileFieldLabel->setObjectName(QStringLiteral("teamInfoFileFieldLabel"));
    teamLayout->addRow(teamInfoFileFieldLabel, createPickerRow(
        m_teamInfoFileEdit, [this]() { onPickTeamInfoFile(); }));
    m_reloadTeamInfoBtn = new QPushButton(this);
    m_reloadTeamInfoBtn->setObjectName(QStringLiteral("reloadTeamInfoButton"));
    teamLayout->addRow(QString(), m_reloadTeamInfoBtn);
    m_teamInfoTextEdit = new QPlainTextEdit(this);
    m_teamInfoTextEdit->setObjectName(QStringLiteral("teamInfoTextEdit"));
    m_teamInfoTextEdit->setMinimumHeight(160);
    auto* teamInfoTextFieldLabel = new QLabel(this);
    teamInfoTextFieldLabel->setObjectName(QStringLiteral("teamInfoTextFieldLabel"));
    teamLayout->addRow(teamInfoTextFieldLabel, m_teamInfoTextEdit);
    mainLayout->addWidget(teamGroup);

    // [NewGame] 区
    auto* newGameGroup = new QGroupBox(this);
    newGameGroup->setObjectName(QStringLiteral("newGameGroup"));
    auto* newGameLayout = new QFormLayout(newGameGroup);
    m_newGameScriptEdit = new QLineEdit(this);
    newGameLayout->addRow("Script:", createPickerRow(m_newGameScriptEdit, [this]() { onPickNewGameScript(); }));

    mainLayout->addWidget(newGameGroup);

    // 底部操作按钮
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    m_saveBtn = new QPushButton(this);
    m_saveBtn->setObjectName(QStringLiteral("saveProfileButton"));
    m_saveAsBtn = new QPushButton(this);
    m_saveAsBtn->setObjectName(QStringLiteral("saveProfileAsButton"));
    bottomLayout->addWidget(m_saveBtn);
    bottomLayout->addWidget(m_saveAsBtn);
    mainLayout->addLayout(bottomLayout);

    connect(m_saveBtn, &QPushButton::clicked, this, &ResourceProfileEditorWindow::onSave);
    connect(m_saveAsBtn, &QPushButton::clicked, this, &ResourceProfileEditorWindow::onSaveAs);

    // 字段变化标记
    connect(m_idEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_authorEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_profileVersionEdit, &QLineEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_typeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_inheritTypeCheck, &QCheckBox::toggled, this, [this](bool inherited)
    {
        m_typeSpin->setEnabled(!inherited);
        updateExperiencePreview();
        onFieldChanged();
    });
    connect(m_useWavCheck, &QCheckBox::stateChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_experienceModeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this]()
        {
            updateExperiencePreview();
            onFieldChanged();
        });
    connect(m_experienceMultiplierCheck, &QCheckBox::toggled,
        this, [this](bool checked)
        {
            m_experienceMultiplierSpin->setEnabled(checked);
            updateExperiencePreview();
            onFieldChanged();
        });
    connect(m_experienceMultiplierSpin,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this, [this]()
        {
            updateExperiencePreview();
            onFieldChanged();
        });
    connect(m_levelUpThresholdModeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_partnerFollowRadiusCheck, &QCheckBox::toggled,
        this, [this](bool checked)
        {
            m_partnerFollowRadiusSpin->setEnabled(checked);
            onFieldChanged();
        });
    connect(m_partnerFollowRadiusSpin,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_partnerFollowRunRadiusCheck, &QCheckBox::toggled,
        this, [this](bool checked)
        {
            m_partnerFollowRunRadiusSpin->setEnabled(checked);
            onFieldChanged();
        });
    connect(m_partnerFollowRunRadiusSpin,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_minimumMagicDamageSpin,
        QOverload<int>::of(&QSpinBox::valueChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_magicEffectCalculationModeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_npcActionProfileCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_npcRuntimeProfileCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_specialActionModeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_addLifeModeCombo,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_typeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
        this, [this]() { updateExperiencePreview(); });
    connect(m_levelUpMessageEdit, &QLineEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_levelUpRandomEffectsEdit, &QPlainTextEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_levelUpMaleEffectEdit, &QLineEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_levelUpFemaleEffectEdit, &QLineEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_releaseDateEdit, &QLineEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_minimumEngineVersionEdit, &QLineEdit::textChanged,
        this, [this]()
        {
            updateReleaseCompatibilityLabel();
            onFieldChanged();
        });
    connect(m_releaseCoverEdit, &QLineEdit::textChanged,
        this, [this]()
        {
            updateReleaseCoverPreview();
            onFieldChanged();
        });
    connect(m_releaseDescriptionFileEdit, &QLineEdit::textChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(
        m_releaseDescriptionTextEdit,
        &QPlainTextEdit::textChanged,
        this,
        [this]()
        {
            if (!m_updatingFromCode)
            {
                m_releaseDescriptionTextModified = true;
                onFieldChanged();
            }
        });
    connect(
        m_reloadReleaseDescriptionBtn,
        &QPushButton::clicked,
        this,
        &ResourceProfileEditorWindow::onReloadReleaseDescriptionFile);
    connect(
        m_exportOnlinePackageBtn,
        &QPushButton::clicked,
        this,
        &ResourceProfileEditorWindow::onExportOnlinePackage);
    connect(m_dependencyIdEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_resourceOnlyCheck, &QCheckBox::stateChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_textEncodingConvertedCheck, &QCheckBox::stateChanged,
        this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_saveNamespaceEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_uiBaseIdEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_uiProfileEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_preferLocalUiCheck, &QCheckBox::stateChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_featuresEdit, &QPlainTextEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_titleMenuEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_titleNewYearMenuEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_titleMusicEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_teamVideoEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_teamInfoFileEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);
    connect(m_teamInfoTextEdit, &QPlainTextEdit::textChanged, this, [this]()
    {
        if (!m_updatingFromCode)
        {
            m_teamInfoTextModified = true;
            onFieldChanged();
        }
    });
    connect(m_reloadTeamInfoBtn, &QPushButton::clicked,
        this, &ResourceProfileEditorWindow::onReloadTeamInfoFile);
    connect(m_newGameScriptEdit, &QLineEdit::textChanged, this, &ResourceProfileEditorWindow::onFieldChanged);

    setMinimumSize(640, 600);
    retranslateUi();
}

void ResourceProfileEditorWindow::retranslateUi()
{
    setWindowTitle(tr("MOD 发布与资源设置"));
    findChild<QGroupBox*>(QStringLiteral("resourcePackGroup"))->setTitle(tr("资源包选择"));
    findChild<QGroupBox*>(QStringLiteral("gameGroup"))->setTitle(tr("[Game] 游戏信息"));
    findChild<QGroupBox*>(QStringLiteral("experienceGroup"))->setTitle(
        tr("[Experience] 经验规则"));
    findChild<QGroupBox*>(QStringLiteral("gameplayGroup"))->setTitle(
        tr("[Gameplay] 伙伴行为"));
    findChild<QGroupBox*>(QStringLiteral("combatGroup"))->setTitle(
        tr("[Combat] 战斗规则"));
    findChild<QGroupBox*>(QStringLiteral("scriptGroup"))->setTitle(
        tr("[Script] 脚本兼容规则"));
    findChild<QGroupBox*>(QStringLiteral("levelUpGroup"))->setTitle(
        tr("[LevelUp] 升级提示与特效"));
    findChild<QGroupBox*>(QStringLiteral("releaseGroup"))->setTitle(
        tr("[Release] MOD 发布信息"));
    findChild<QGroupBox*>(QStringLiteral("resourceGroup"))->setTitle(
        tr("[Resource]/[Save] 依赖与存档"));
    findChild<QGroupBox*>(QStringLiteral("uiFeaturesGroup"))->setTitle(
        tr("[UI]/[Features] 界面基底与功能"));
    findChild<QGroupBox*>(QStringLiteral("startupGroup"))->setTitle(tr("[Startup] 启动视频"));
    findChild<QGroupBox*>(QStringLiteral("titleGroup"))->setTitle(tr("[Title] 标题"));
    findChild<QGroupBox*>(QStringLiteral("teamGroup"))->setTitle(tr("[Team] MOD 团队说明"));
    findChild<QGroupBox*>(QStringLiteral("newGameGroup"))->setTitle(tr("[NewGame] 新游戏"));

    findChild<QLabel*>(QStringLiteral("assetsPathFieldLabel"))->setText(tr("Assets 路径:"));
    findChild<QLabel*>(QStringLiteral("packFieldLabel"))->setText(tr("资源包:"));
    findChild<QLabel*>(QStringLiteral("packPathFieldLabel"))->setText(tr("资源包路径:"));
    findChild<QLabel*>(QStringLiteral("activePackFieldLabel"))->setText(
        tr("项目活动包:"));
    findChild<QLabel*>(QStringLiteral("profileNameFieldLabel"))->setText(tr("名称:"));
    findChild<QLabel*>(QStringLiteral("profileAuthorFieldLabel"))->setText(tr("MOD 作者:"));
    findChild<QLabel*>(QStringLiteral("profileVersionFieldLabel"))->setText(
        tr("MOD 展示版本:"));
    findChild<QLabel*>(QStringLiteral("releaseDateFieldLabel"))->setText(
        tr("发布日期:"));
    findChild<QLabel*>(
        QStringLiteral("minimumEngineVersionFieldLabel"))->setText(
        tr("最低引擎版本:"));
    findChild<QLabel*>(QStringLiteral("releaseCoverFieldLabel"))->setText(
        tr("封面文件:"));
    findChild<QLabel*>(
        QStringLiteral("releaseCoverPreviewFieldLabel"))->setText(
        tr("封面预览:"));
    findChild<QLabel*>(
        QStringLiteral("releaseDescriptionFileFieldLabel"))->setText(
        tr("简介文件:"));
    findChild<QLabel*>(
        QStringLiteral("releaseDescriptionTextFieldLabel"))->setText(
        tr("MOD 简介:"));
    findChild<QLabel*>(QStringLiteral("featuresFieldLabel"))->setText(tr("功能开关:"));
    findChild<QLabel*>(QStringLiteral("experienceModeFieldLabel"))->setText(
        tr("击杀 NPC 经验模式:"));
    findChild<QLabel*>(QStringLiteral("experienceMultiplierFieldLabel"))->setText(
        tr("自动经验倍率:"));
    findChild<QLabel*>(QStringLiteral("levelUpThresholdModeFieldLabel"))->setText(
        tr("升级经验阈值:"));
    findChild<QLabel*>(QStringLiteral("experiencePreviewFieldLabel"))->setText(
        tr("示例预览:"));
    findChild<QLabel*>(QStringLiteral("partnerFollowRadiusFieldLabel"))->setText(
        tr("伙伴跟随距离:"));
    findChild<QLabel*>(QStringLiteral("partnerFollowRunRadiusFieldLabel"))->setText(
        tr("伙伴跑动距离:"));
    findChild<QLabel*>(QStringLiteral("minimumMagicDamageFieldLabel"))->setText(
        tr("武功最低伤害:"));
    findChild<QLabel*>(
        QStringLiteral("magicEffectCalculationModeFieldLabel"))->setText(
        tr("武功效果计算:"));
    findChild<QLabel*>(QStringLiteral("npcActionProfileFieldLabel"))->setText(
        tr("NPC 动作编号协议:"));
    findChild<QLabel*>(QStringLiteral("npcRuntimeProfileFieldLabel"))->setText(
        tr("NPC 运行语义:"));
    findChild<QLabel*>(QStringLiteral("specialActionModeFieldLabel"))->setText(
        tr("NPC 特殊动作方式:"));
    findChild<QLabel*>(QStringLiteral("addLifeModeFieldLabel"))->setText(
        tr("AddLife 语义:"));
    m_minimumMagicDamageSpin->setToolTip(tr(
        "每次武功伤害结算的最低值；该值只由当前资源包配置决定。"));
    m_magicEffectCalculationModeCombo->setToolTip(tr(
        "替代攻击力用于剑二和月影；叠加攻击力用于新剑。"));
    findChild<QLabel*>(QStringLiteral("levelUpMessageFieldLabel"))->setText(
        tr("提示模板:"));
    findChild<QLabel*>(
        QStringLiteral("levelUpRandomEffectsFieldLabel"))->setText(
        tr("随机特效:"));
    findChild<QLabel*>(
        QStringLiteral("levelUpMaleEffectFieldLabel"))->setText(
        tr("男性特效:"));
    findChild<QLabel*>(
        QStringLiteral("levelUpFemaleEffectFieldLabel"))->setText(
        tr("女性特效:"));
    findChild<QLabel*>(QStringLiteral("teamInfoFileFieldLabel"))->setText(
        tr("团队说明文件:"));
    findChild<QLabel*>(QStringLiteral("teamInfoTextFieldLabel"))->setText(
        tr("团队说明内容:"));

    m_createDefaultBtn->setText(tr("创建默认 Manifest"));
    m_createDefaultBtn->setToolTip(tr("在当前 assets 根目录下创建 game_profile.ini"));
    m_setActivePackBtn->setText(tr("设为活动包"));
    m_setActivePackBtn->setToolTip(
        tr("将当前资源包设为项目内容编辑器使用的活动包"));
    m_inheritTypeCheck->setText(tr("从内容依赖继承（不写 Game.Type）"));
    m_useWavCheck->setText(tr("保留 wav 音乐优先逻辑"));
    const int previousExperienceMode = m_experienceModeCombo->currentIndex();
    {
        const QSignalBlocker blocker(m_experienceModeCombo);
        m_experienceModeCombo->clear();
        m_experienceModeCombo->addItem(tr("使用 Game.Type 默认值"));
        m_experienceModeCombo->addItem(tr("读取 NPC.Exp（剑侠情缘二）"));
        m_experienceModeCombo->addItem(tr("角色等级 × NPC 等级 + ExpBonus"));
        m_experienceModeCombo->setCurrentIndex(
            previousExperienceMode >= 0 ? previousExperienceMode : 0);
    }
    m_experienceMultiplierCheck->setText(tr("写入配置"));
    const auto refillCombo = [](QComboBox* combo, const QStringList& items)
    {
        const int previousIndex = combo->currentIndex();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItems(items);
        combo->setCurrentIndex(
            previousIndex >= 0 && previousIndex < items.size()
                ? previousIndex
                : 0);
    };
    refillCombo(
        m_levelUpThresholdModeCombo,
        { tr("使用 Game.Type 默认值"),
          tr("经验大于或等于阈值"),
          tr("经验严格大于阈值") });
    refillCombo(
        m_magicEffectCalculationModeCombo,
        { tr("Effect 替代人物攻击力"),
          tr("人物攻击力与 Effect 相加") });
    m_partnerFollowRadiusCheck->setText(tr("写入配置"));
    m_partnerFollowRunRadiusCheck->setText(tr("写入配置"));
    refillCombo(
        m_npcActionProfileCombo,
        { tr("使用 Game.Type 默认值"),
          tr("剑侠情缘二动作编号"),
          tr("月影传说动作编号"),
          tr("新剑侠情缘动作编号") });
    refillCombo(
        m_npcRuntimeProfileCombo,
        { tr("使用 Game.Type 默认值"),
          tr("剑侠情缘二运行语义"),
          tr("月影/新剑运行语义") });
    refillCombo(
        m_specialActionModeCombo,
        { tr("使用 Game.Type 默认值"),
          tr("替换当前动作"),
          tr("覆盖在当前动作上") });
    refillCombo(
        m_addLifeModeCombo,
        { tr("使用 Game.Type 默认值"),
          tr("使用主角生命规则"),
          tr("直接增减并限制范围") });
    m_levelUpMessageEdit->setPlaceholderText(tr(
        "支持 {name} 和 {level}，留空则不显示升级提示"));
    m_levelUpRandomEffectsEdit->setPlaceholderText(tr(
        "每行一个 ini/magic 下的 Magic INI；未配置男女特效时随机选择"));
    m_levelUpMaleEffectEdit->setPlaceholderText(tr(
        "ini/magic 下的 Magic INI 文件名，可留空"));
    m_levelUpFemaleEffectEdit->setPlaceholderText(tr(
        "ini/magic 下的 Magic INI 文件名，可留空"));
    m_profileVersionEdit->setPlaceholderText(
        tr("展示字符串，例如 1.041；不按 SemVer 解析"));
    m_releaseDateEdit->setPlaceholderText(tr("YYYY-MM-DD，例如 2026-07-25"));
    m_minimumEngineVersionEdit->setPlaceholderText(
        tr("严格 SemVer，例如 1.4.3 或 2.0.0-beta.1"));
    m_releaseCoverEdit->setPlaceholderText(
        tr("当前 MOD 根内相对路径，例如 ui/mod-cover.png"));
    m_releaseDescriptionFileEdit->setPlaceholderText(
        tr("当前 MOD 根内 UTF-8 文件；正文非空时留空使用 description.txt"));
    m_releaseDescriptionTextEdit->setPlaceholderText(
        tr("只介绍当前 MOD；最大 64 KiB，不从依赖资源包读取。"));
    m_reloadReleaseDescriptionBtn->setText(tr("从文件重新载入简介"));
    m_reloadReleaseDescriptionBtn->setToolTip(
        tr("按 Release.DescriptionFile 从当前 MOD 根读取 UTF-8 简介"));
    m_exportOnlinePackageBtn->setText(tr("发布资源..."));
    m_exportOnlinePackageBtn->setToolTip(tr(
        "生成完整 ZIP、单资源在线目录，并完成 CRC32 与 ZIP 回读校验"));
    m_dependencyIdEdit->setPlaceholderText(
        tr("有序依赖 Game.Id，例如 JXQY2,YYCS（左到右深度优先）"));
    m_resourceOnlyCheck->setText(
        tr("仅作为资源依赖（不显示在游戏选择列表，不能直接启动）"));
    m_saveNamespaceEdit->setPlaceholderText(tr("留空时使用 Game.Id"));
    m_textEncodingConvertedCheck->setText(
        tr("文本编码转换已完成（资源文本为 UTF-8）"));
    m_uiBaseIdEdit->setPlaceholderText(tr("仅回退 UI 资源，例如 YYCS"));
    m_uiProfileEdit->setPlaceholderText(tr("布局族，例如 JXQY2 / YYCS / XJXQY"));
    m_preferLocalUiCheck->setText(tr("当前资源包 UI 优先（允许局部覆盖）"));
    m_featuresEdit->setPlaceholderText(tr(
        "每行一项，例如 MagicTriggerAtAnimationEnd=1 或 RageSystem=1；"
        "缺省玩法键使用通用默认值，布局键由 UI.Profile 成套决定；"
        "资源转换会写入实际基底默认值"));
    m_teamInfoFileEdit->setPlaceholderText(tr(
        "例如 team.txt；填写正文后留空会自动使用 team.txt"));
    m_teamInfoTextEdit->setPlaceholderText(tr(
        "仅填写当前 MOD 的制作人员、分工和致谢；不要填写引擎作者或全局主页链接。"));
    m_reloadTeamInfoBtn->setText(tr("从文件重新载入正文"));
    m_reloadTeamInfoBtn->setToolTip(tr(
        "按 Team.InfoFile 从当前资源包重新读取 UTF-8 团队说明"));
    m_addVideoBtn->setText(tr("添加"));
    m_removeVideoBtn->setText(tr("删除"));
    m_videoUpBtn->setText(tr("上移"));
    m_videoDownBtn->setText(tr("下移"));
    m_saveBtn->setText(tr("保存"));
    m_saveAsBtn->setText(tr("另存为..."));

    const auto pickerButtons = findChildren<QPushButton*>();
    for (QPushButton* button : pickerButtons)
    {
        if (button->property("jxqyFilePicker").toBool())
            button->setToolTip(tr("选择文件"));
    }

    m_assetsPathLabel->setText(m_assetsBasePath.isEmpty() ? tr("（未设置）") : m_assetsBasePath);
    if (m_assetsBasePath.isEmpty() || !QDir(m_assetsBasePath).exists())
    {
        m_packPathLabel->setText(tr("（assets 路径无效）"));
    }
    else if (m_currentPackIndex >= 0 && m_currentPackIndex < m_packs.size())
    {
        m_packPathLabel->setText(m_packs[m_currentPackIndex].rootPath);
    }
    if (m_packs.isEmpty() && m_packCombo->count() > 0)
    {
        m_packCombo->setItemText(0,
            m_transactionRecoveryBlocked
                ? tr("（资源事务恢复失败）")
                : tr("（未发现资源包，可点击“创建默认 Manifest”）"));
    }
    updateProjectResourceContextUi();
    updateReleaseCompatibilityLabel();
    updateReleaseCoverPreview();
    updateExperiencePreview();
}

void ResourceProfileEditorWindow::setProjectResourceContext(
    const QString& activeResourcePackId,
    bool projectOpen,
    const QString& activeResourcePackEntryKey)
{
    m_activeResourcePackId = activeResourcePackId.trimmed();
    m_activeResourcePackEntryKey =
        activeResourcePackEntryKey.trimmed();
    m_projectOpen = projectOpen;
    updateProjectResourceContextUi();
}

AssetsPathSwitchParticipant::PathScope
ResourceProfileEditorWindow::assetsPathScope() const
{
    return PathScope::ResourceCollectionRoot;
}

bool ResourceProfileEditorWindow::setAssetsBasePath(const QString& path)
{
    const Decision decision = prepareAssetsPathSwitch(path);
    if (decision == Decision::Cancelled || !resolveAssetsPathSwitch(decision))
        return false;
    commitAssetsPathSwitch(path);
    return true;
}

AssetsPathSwitchParticipant::Decision ResourceProfileEditorWindow::prepareAssetsPathSwitch(
    const QString& path) const
{
    Q_UNUSED(path);
    if (!m_hasUnsavedChanges)
        return Decision::Ready;

    const auto result = QMessageBox::question(const_cast<ResourceProfileEditorWindow*>(this),
        tr("未保存的修改"),
        tr("当前资源包有未保存的修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (result == QMessageBox::Cancel)
        return Decision::Cancelled;
    return result == QMessageBox::Yes ? Decision::Save : Decision::Discard;
}

bool ResourceProfileEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    if (decision == Decision::Cancelled)
        return false;
    if (decision == Decision::Save)
    {
        onSave();
        return !m_hasUnsavedChanges;
    }
    return true;
}

void ResourceProfileEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    m_hasUnsavedChanges = false;
    if (!assetsPathsEqual(path, m_assetsBasePath))
    {
        m_assetsBasePath = path;
        m_assetsPathLabel->setText(
            path.isEmpty() ? tr("（未设置）") : path);
    }
    refreshPackList();
}

QString ResourceProfileEditorWindow::currentAssetsPath() const
{
    return m_assetsBasePath;
}

void ResourceProfileEditorWindow::refreshPackList(bool showRecoveryError)
{
    m_updatingFromCode = true;
    m_packCombo->clear();
    m_packs.clear();
    m_currentPackIndex = -1;
    m_transactionRecoveryBlocked = false;
    m_transactionRecoveryErrorText.clear();
    m_createDefaultBtn->setEnabled(false);
    m_saveBtn->setEnabled(false);
    m_saveAsBtn->setEnabled(false);

    if (m_assetsBasePath.isEmpty() || !QDir(m_assetsBasePath).exists())
    {
        m_packPathLabel->setText(tr("（assets 路径无效）"));
        m_updatingFromCode = false;
        return;
    }

    QStringList recoveryErrors;
    m_packs = ResourcePackScanner::scanPacks(
        m_assetsBasePath, recoveryErrors);
    if (!recoveryErrors.isEmpty())
    {
        m_transactionRecoveryBlocked = true;
        m_transactionRecoveryErrorText = recoveryErrors.join('\n');
        m_packCombo->addItem(tr("（资源事务恢复失败）"));
        m_packPathLabel->setText(m_transactionRecoveryErrorText);
        m_updatingFromCode = false;
        if (showRecoveryError)
        {
            QMessageBox::critical(
                this,
                tr("文件事务恢复失败"),
                tr("资源目录中存在未能安全恢复的保存事务。为避免覆盖可恢复数据，"
                   "创建和保存功能已停用：\n%1")
                    .arg(m_transactionRecoveryErrorText));
        }
        return;
    }

    if (m_packs.isEmpty())
    {
        m_packCombo->addItem(tr("（未发现资源包，可点击“创建默认 Manifest”）"));
        m_packPathLabel->setText("");
        m_createDefaultBtn->setEnabled(true);
        m_updatingFromCode = false;
    }
    else
    {
        m_saveBtn->setEnabled(true);
        m_saveAsBtn->setEnabled(true);
        for (const auto& pack : m_packs)
        {
            QString displayText = pack.profile.name;
            if (displayText.isEmpty())
            {
                displayText = pack.profile.id;
            }
            if (displayText.isEmpty())
            {
                displayText = QFileInfo(pack.rootPath).fileName();
            }
            // 若有多个资源包且名称重复，附加路径用于区分
            int dupCount = 0;
            for (const auto& other : m_packs)
            {
                if (&other != &pack && other.profile.name == pack.profile.name && !pack.profile.name.isEmpty())
                {
                    dupCount++;
                    break;
                }
            }
            if (dupCount > 0)
            {
                displayText += "  [" + pack.rootPath + "]";
            }
            m_packCombo->addItem(displayText);
        }
        m_packPathLabel->setText(m_packs[0].rootPath);
        // 允许 onPackSelectionChanged 处理首包加载
        m_updatingFromCode = false;
        m_hasUnsavedChanges = false;
        m_currentPackIndex = -1;
        onPackSelectionChanged(0);
    }
}

void ResourceProfileEditorWindow::onPackSelectionChanged(int index)
{
    if (m_updatingFromCode)
    {
        return;
    }

    if (index < 0 || index >= m_packs.size())
    {
        return;
    }

    if (m_hasUnsavedChanges)
    {
        if (!confirmSaveIfModified())
        {
            // 用户取消，恢复选择
            m_updatingFromCode = true;
            m_packCombo->setCurrentIndex(m_currentPackIndex);
            m_updatingFromCode = false;
            return;
        }
    }

    m_currentPackIndex = index;
    const auto& pack = m_packs[index];
    m_packPathLabel->setText(pack.rootPath);
    loadProfileToUi(pack.profile);
    m_hasUnsavedChanges = false;
    updateProjectResourceContextUi();
}

void ResourceProfileEditorWindow::onSetActiveResourcePack()
{
    if (!m_projectOpen || m_currentPackIndex < 0 ||
        m_currentPackIndex >= m_packs.size())
    {
        return;
    }

    const ResourcePackInfo& pack =
        m_packs[m_currentPackIndex];
    const QString resourcePackId =
        pack.profile.id.trimmed();
    if (!resourcePackId.isEmpty())
    {
        emit activateResourcePackRequested(
            resourcePackId,
            pack.stableEntryKey.trimmed());
    }
}

void ResourceProfileEditorWindow::updateProjectResourceContextUi()
{
    if (!m_activePackLabel || !m_setActivePackBtn)
        return;

    if (!m_projectOpen)
        m_activePackLabel->setText(tr("（未打开项目）"));
    else if (m_activeResourcePackId.isEmpty())
        m_activePackLabel->setText(tr("（未选择）"));
    else
        m_activePackLabel->setText(m_activeResourcePackId);

    QString selectedId;
    QString selectedEntryKey;
    if (m_currentPackIndex >= 0 && m_currentPackIndex < m_packs.size())
    {
        selectedId = m_packs[m_currentPackIndex].profile.id.trimmed();
        selectedEntryKey =
            m_packs[m_currentPackIndex].stableEntryKey.trimmed();
    }
    const bool alreadyActive =
        !m_activeResourcePackEntryKey.isEmpty()
        ? selectedEntryKey.compare(
              m_activeResourcePackEntryKey,
              Qt::CaseInsensitive) == 0
        : selectedId.compare(
              m_activeResourcePackId,
              Qt::CaseInsensitive) == 0;
    m_setActivePackBtn->setEnabled(
        m_projectOpen && !selectedId.isEmpty() &&
        !alreadyActive);
}

void ResourceProfileEditorWindow::loadProfileToUi(const GameProfile& profile)
{
    m_updatingFromCode = true;
    m_invalidLoadedReleaseFields.clear();
    ModRelease::ModReleaseMetadata releaseMetadata =
        profile.releaseMetadata;
    GameProfile sourceProfile;
    if (!profile.manifestPath.isEmpty() &&
        sourceProfile.loadFromFile(profile.manifestPath))
    {
        // Catalog parsing may explicitly decode a historical manifest
        // encoding for runtime compatibility. The editor must still inspect
        // the authoritative source bytes so an invalid UTF-8 release field is
        // never rewritten merely because the shared catalog could read it.
        releaseMetadata =
            sourceProfile.releaseMetadata;
    }
    auto decodeReleaseField =
        [this](const std::string& value,
               ModRelease::MetadataField field)
    {
        const QByteArray bytes(
            value.data(), static_cast<qsizetype>(value.size()));
        const QString decoded =
            QString::fromUtf8(bytes.constData(), bytes.size());
        if (decoded.toUtf8() != bytes)
        {
            m_invalidLoadedReleaseFields.insert(
                static_cast<int>(field));
        }
        return decoded;
    };
    m_idEdit->setText(profile.id);
    m_nameEdit->setText(profile.name);
    m_authorEdit->setText(profile.author);
    m_profileVersionEdit->setText(
        decodeReleaseField(
            releaseMetadata.displayVersion,
            ModRelease::MetadataField::DisplayVersion));
    m_typeSpin->setValue(profile.type);
    m_inheritTypeCheck->setChecked(!profile.typeDefined);
    m_typeSpin->setEnabled(profile.typeDefined);
    m_useWavCheck->setChecked(profile.useWav);
    m_experienceModeCombo->setCurrentIndex(
        !profile.defeatedNpcExperienceModeDefined
            ? 0
            : (profile.defeatedNpcExperienceMode ==
                       DefeatedNpcExperienceMode::StoredExperience
                   ? 1
                   : 2));
    m_experienceMultiplierCheck->setChecked(
        profile.experienceMultiplierDefined);
    m_experienceMultiplierSpin->setValue(profile.experienceMultiplier);
    m_experienceMultiplierSpin->setEnabled(
        profile.experienceMultiplierDefined);
    m_levelUpThresholdModeCombo->setCurrentIndex(
        !profile.levelUpThresholdModeDefined
            ? 0
            : (profile.levelUpThresholdMode ==
                       LevelUpThresholdMode::GreaterThanOrEqual
                   ? 1
                   : 2));
    m_partnerFollowRadiusCheck->setChecked(
        profile.partnerFollowRadiusDefined);
    m_partnerFollowRadiusSpin->setValue(profile.partnerFollowRadius);
    m_partnerFollowRadiusSpin->setEnabled(
        profile.partnerFollowRadiusDefined);
    m_partnerFollowRunRadiusCheck->setChecked(
        profile.partnerFollowRunRadiusDefined);
    m_partnerFollowRunRadiusSpin->setValue(
        profile.partnerFollowRunRadius);
    m_partnerFollowRunRadiusSpin->setEnabled(
        profile.partnerFollowRunRadiusDefined);
    m_minimumMagicDamageSpin->setValue(
        profile.minimumMagicDamageDefined
            ? profile.minimumMagicDamage
            : 10);
    m_magicEffectCalculationModeCombo->setCurrentIndex(
        profile.magicEffectCalculationModeDefined &&
                profile.magicEffectCalculationMode ==
                    MagicEffectCalculationMode::AddToAttack
            ? 1
            : 0);
    int npcActionProfileIndex = 0;
    if (profile.npcActionProfileDefined)
    {
        npcActionProfileIndex = 1;
        if (profile.npcActionProfile == ScriptNpcActionProfile::Yycs)
        {
            npcActionProfileIndex = 2;
        }
        else if (profile.npcActionProfile ==
                 ScriptNpcActionProfile::Xjxqy)
        {
            npcActionProfileIndex = 3;
        }
    }
    m_npcActionProfileCombo->setCurrentIndex(npcActionProfileIndex);
    m_npcRuntimeProfileCombo->setCurrentIndex(
        !profile.npcRuntimeProfileDefined
            ? 0
            : (profile.npcRuntimeProfile == ScriptNpcRuntimeProfile::Legacy
                   ? 1
                   : 2));
    m_specialActionModeCombo->setCurrentIndex(
        !profile.specialActionModeDefined
            ? 0
            : (profile.specialActionMode == ScriptSpecialActionMode::Replace
                   ? 1
                   : 2));
    m_addLifeModeCombo->setCurrentIndex(
        !profile.addLifeModeDefined
            ? 0
            : (profile.addLifeMode == ScriptAddLifeMode::PlayerRules
                   ? 1
                   : 2));
    m_levelUpMessageEdit->setText(profile.levelUpMessage);
    m_levelUpRandomEffectsEdit->setPlainText(
        profile.levelUpRandomEffects.join('\n'));
    m_levelUpMaleEffectEdit->setText(profile.levelUpMaleEffect);
    m_levelUpFemaleEffectEdit->setText(profile.levelUpFemaleEffect);
    m_releaseDateEdit->setText(
        decodeReleaseField(
            releaseMetadata.releaseDate,
            ModRelease::MetadataField::ReleaseDate));
    m_minimumEngineVersionEdit->setText(
        decodeReleaseField(
            releaseMetadata.minimumEngineVersion,
            ModRelease::MetadataField::MinimumEngineVersion));
    m_releaseCoverEdit->setText(
        decodeReleaseField(
            releaseMetadata.coverPath,
            ModRelease::MetadataField::CoverPath));
    m_releaseDescriptionFileEdit->setText(
        decodeReleaseField(
            releaseMetadata.descriptionFilePath,
            ModRelease::MetadataField::DescriptionFilePath));
    loadReleaseDescriptionText(
        m_releaseDescriptionFileEdit->text(),
        false);
    m_dependencyIdEdit->setText(profile.dependencyId);
    m_resourceOnlyCheck->setChecked(profile.resourceOnly);
    m_textEncodingConvertedCheck->setChecked(
        profile.textEncodingConverted);
    m_saveNamespaceEdit->setText(profile.saveNamespace);
    m_uiBaseIdEdit->setText(profile.uiBaseId);
    m_uiProfileEdit->setText(profile.uiProfile);
    m_preferLocalUiCheck->setChecked(profile.preferLocalUi);
    QStringList featureLines;
    for (auto feature = profile.features.cbegin(); feature != profile.features.cend(); ++feature)
    {
        featureLines.append(feature.key() + "=" + (feature.value() ? "1" : "0"));
    }
    m_featuresEdit->setPlainText(featureLines.join('\n'));

    m_videosList->clear();
    for (const auto& video : profile.startupVideos)
    {
        m_videosList->addItem(video);
    }

    m_titleMenuEdit->setText(profile.titleMenu);
    m_titleNewYearMenuEdit->setText(profile.titleNewYearMenu);
    m_titleMusicEdit->setText(profile.titleMusic);
    m_teamVideoEdit->setText(profile.titleTeamVideo);
    m_teamInfoFileEdit->setText(profile.teamInfoFile);
    loadTeamInfoText(profile.teamInfoFile, false);

    m_newGameScriptEdit->setText(profile.newGameScript);

    m_updatingFromCode = false;
    updateReleaseCompatibilityLabel();
    updateReleaseCoverPreview();
    updateExperiencePreview();
}

GameProfile ResourceProfileEditorWindow::collectProfileFromUi() const
{
    GameProfile profile;
    profile.id = m_idEdit->text();
    profile.name = m_nameEdit->text();
    profile.author = m_authorEdit->text().trimmed();
    profile.releaseMetadata.displayVersion =
        exactTrimmedUtf8(m_profileVersionEdit->text());
    profile.type = m_typeSpin->value();
    profile.typeDefined = !m_inheritTypeCheck->isChecked();
    profile.useWav = m_useWavCheck->isChecked();
    profile.defeatedNpcExperienceModeDefined =
        m_experienceModeCombo->currentIndex() > 0;
    profile.defeatedNpcExperienceMode =
        m_experienceModeCombo->currentIndex() == 1
            ? DefeatedNpcExperienceMode::StoredExperience
            : DefeatedNpcExperienceMode::LevelProductWithBonus;
    profile.experienceMultiplierDefined =
        m_experienceMultiplierCheck->isChecked();
    profile.experienceMultiplier =
        m_experienceMultiplierSpin->value();
    profile.levelUpThresholdModeDefined =
        m_levelUpThresholdModeCombo->currentIndex() > 0;
    profile.levelUpThresholdMode =
        m_levelUpThresholdModeCombo->currentIndex() == 2
            ? LevelUpThresholdMode::GreaterThan
            : LevelUpThresholdMode::GreaterThanOrEqual;
    profile.partnerFollowRadiusDefined =
        m_partnerFollowRadiusCheck->isChecked();
    profile.partnerFollowRadius = m_partnerFollowRadiusSpin->value();
    profile.partnerFollowRunRadiusDefined =
        m_partnerFollowRunRadiusCheck->isChecked();
    profile.partnerFollowRunRadius =
        m_partnerFollowRunRadiusSpin->value();
    profile.minimumMagicDamage =
        m_minimumMagicDamageSpin->value();
    profile.minimumMagicDamageDefined = true;
    profile.magicEffectCalculationMode =
        m_magicEffectCalculationModeCombo->currentIndex() == 1
            ? MagicEffectCalculationMode::AddToAttack
            : MagicEffectCalculationMode::ReplaceAttack;
    profile.magicEffectCalculationModeDefined = true;
    profile.npcActionProfileDefined =
        m_npcActionProfileCombo->currentIndex() > 0;
    switch (m_npcActionProfileCombo->currentIndex())
    {
    case 2:
        profile.npcActionProfile = ScriptNpcActionProfile::Yycs;
        break;
    case 3:
        profile.npcActionProfile = ScriptNpcActionProfile::Xjxqy;
        break;
    default:
        profile.npcActionProfile = ScriptNpcActionProfile::Legacy;
        break;
    }
    profile.npcRuntimeProfileDefined =
        m_npcRuntimeProfileCombo->currentIndex() > 0;
    profile.npcRuntimeProfile =
        m_npcRuntimeProfileCombo->currentIndex() == 2
            ? ScriptNpcRuntimeProfile::Trilogy
            : ScriptNpcRuntimeProfile::Legacy;
    profile.specialActionModeDefined =
        m_specialActionModeCombo->currentIndex() > 0;
    profile.specialActionMode =
        m_specialActionModeCombo->currentIndex() == 2
            ? ScriptSpecialActionMode::Overlay
            : ScriptSpecialActionMode::Replace;
    profile.addLifeModeDefined =
        m_addLifeModeCombo->currentIndex() > 0;
    profile.addLifeMode =
        m_addLifeModeCombo->currentIndex() == 2
            ? ScriptAddLifeMode::DirectClamp
            : ScriptAddLifeMode::PlayerRules;
    profile.levelUpMessage = m_levelUpMessageEdit->text();
    profile.levelUpRandomEffects.clear();
    for (const QString& rawEffect :
         m_levelUpRandomEffectsEdit->toPlainText().split('\n'))
    {
        const QString effect = rawEffect.trimmed();
        if (!effect.isEmpty())
        {
            profile.levelUpRandomEffects.append(effect);
        }
    }
    profile.levelUpMaleEffect =
        m_levelUpMaleEffectEdit->text().trimmed();
    profile.levelUpFemaleEffect =
        m_levelUpFemaleEffectEdit->text().trimmed();
    profile.releaseMetadata.releaseDate =
        exactTrimmedUtf8(m_releaseDateEdit->text());
    profile.releaseMetadata.minimumEngineVersion =
        exactTrimmedUtf8(m_minimumEngineVersionEdit->text());
    QString coverPath = m_releaseCoverEdit->text().trimmed();
    coverPath.replace('\\', '/');
    profile.releaseMetadata.coverPath = exactTrimmedUtf8(coverPath);
    QString descriptionFilePath =
        m_releaseDescriptionFileEdit->text().trimmed();
    descriptionFilePath.replace('\\', '/');
    profile.releaseMetadata.descriptionFilePath =
        exactTrimmedUtf8(descriptionFilePath);
    profile.dependencyId = m_dependencyIdEdit->text();
    profile.resourceOnly = m_resourceOnlyCheck->isChecked();
    profile.textEncodingConverted =
        m_textEncodingConvertedCheck->isChecked();
    profile.saveNamespace = m_saveNamespaceEdit->text();
    profile.uiBaseId = m_uiBaseIdEdit->text().trimmed();
    profile.uiProfile = m_uiProfileEdit->text().trimmed();
    profile.preferLocalUi = m_preferLocalUiCheck->isChecked();
    profile.features.clear();
    const QStringList featureLines = m_featuresEdit->toPlainText().split('\n');
    for (const QString& rawLine : featureLines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(';') || line.startsWith('#'))
        {
            continue;
        }
        const qsizetype separator = line.indexOf('=');
        const QString featureName = (separator >= 0 ? line.left(separator) : line).trimmed();
        const QString value = separator >= 0 ? line.mid(separator + 1).trimmed().toLower() : "1";
        if (!featureName.isEmpty())
        {
            const bool enabled = value == "1" || value == "true" || value == "yes" || value == "on";
            profile.features.insert(featureName, enabled);
        }
    }

    profile.startupVideos.clear();
    for (int i = 0; i < m_videosList->count(); i++)
    {
        QListWidgetItem* item = m_videosList->item(i);
        if (item)
        {
            profile.startupVideos.append(item->text());
        }
    }

    profile.titleMenu = m_titleMenuEdit->text();
    profile.titleNewYearMenu = m_titleNewYearMenuEdit->text();
    profile.titleMusic = m_titleMusicEdit->text();
    profile.titleTeamVideo = m_teamVideoEdit->text();
    profile.teamInfoFile = m_teamInfoFileEdit->text().trimmed();

    profile.newGameScript = m_newGameScriptEdit->text();

    return profile;
}

void ResourceProfileEditorWindow::updateVideoList()
{
    // 由 collectProfileFromUi 直接读取，这里无需额外操作
}

QString ResourceProfileEditorWindow::currentPackRoot() const
{
    if (m_currentPackIndex < 0 || m_currentPackIndex >= m_packs.size())
    {
        return m_assetsBasePath;
    }
    return m_packs[m_currentPackIndex].rootPath;
}

QWidget* ResourceProfileEditorWindow::createPickerRow(
    QLineEdit* edit,
    std::function<void()> onPick,
    const QString& buttonObjectName)
{
    auto* wrapper = new QWidget(this);
    auto* layout = new QHBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    edit->setParent(wrapper);
    layout->addWidget(edit, 1);
    auto* btn = new QPushButton("...", wrapper);
    if (!buttonObjectName.isEmpty())
    {
        btn->setObjectName(buttonObjectName);
    }
    btn->setProperty("jxqyFilePicker", true);
    btn->setFixedSize(28, edit->sizeHint().height());
    btn->setToolTip(tr("选择文件"));
    layout->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [onPick]() { onPick(); });
    return wrapper;
}

void ResourceProfileEditorWindow::onAddVideo()
{
    QString packRoot = currentPackRoot();
    QString defaultDir = packRoot.isEmpty() ? QString() : QDir(packRoot).absoluteFilePath("video");
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("选择视频文件"),
        defaultDir,
        "Video (*.avi *.mp4 *.mkv)",
        nullptr,
        QFileDialog::DontResolveSymlinks);
    if (fileName.isEmpty())
    {
        return;
    }

    // 运行时 Startup.Videos 只保存文件名（相对于 video\ 目录）
    QFileInfo fi(fileName);
    m_videosList->addItem(fi.fileName());
    markModified();
}

void ResourceProfileEditorWindow::onRemoveVideo()
{
    int row = m_videosList->currentRow();
    if (row < 0)
    {
        return;
    }
    delete m_videosList->takeItem(row);
    markModified();
}

void ResourceProfileEditorWindow::onMoveVideoUp()
{
    int row = m_videosList->currentRow();
    if (row <= 0)
    {
        return;
    }
    QListWidgetItem* item = m_videosList->takeItem(row);
    m_videosList->insertItem(row - 1, item);
    m_videosList->setCurrentRow(row - 1);
    markModified();
}

void ResourceProfileEditorWindow::onMoveVideoDown()
{
    int row = m_videosList->currentRow();
    if (row < 0 || row >= m_videosList->count() - 1)
    {
        return;
    }
    QListWidgetItem* item = m_videosList->takeItem(row);
    m_videosList->insertItem(row + 1, item);
    m_videosList->setCurrentRow(row + 1);
    markModified();
}

void ResourceProfileEditorWindow::onPickTitleMenu()
{
    // Title.Menu 是相对于资源包根目录的路径（如 ini\ui\title\title.menu.ini）
    pickFileForEdit(m_titleMenuEdit, "ini/ui/title", "INI (*.ini)", PathMode::RelativeToPackRoot);
}

void ResourceProfileEditorWindow::onPickTitleNewYearMenu()
{
    pickFileForEdit(m_titleNewYearMenuEdit, "ini/ui/title", "INI (*.ini)", PathMode::RelativeToPackRoot);
}

void ResourceProfileEditorWindow::onPickTitleMusic()
{
    // Title.Music 运行时会自动拼 music\ 前缀，只保存文件名
    pickFileForEdit(m_titleMusicEdit, "music", "Music (*.mp3 *.ogg *.wma *.wav)", PathMode::FileNameOnly);
}

void ResourceProfileEditorWindow::onPickTeamVideo()
{
    // Title.TeamVideo 运行时会自动拼 video\ 前缀，只保存文件名
    pickFileForEdit(m_teamVideoEdit, "video", "Video (*.avi *.mp4 *.mkv)", PathMode::FileNameOnly);
}

void ResourceProfileEditorWindow::onPickLevelUpMaleEffect()
{
    pickFileForEdit(
        m_levelUpMaleEffectEdit,
        "ini/magic",
        "INI (*.ini)",
        PathMode::FileNameOnly);
}

void ResourceProfileEditorWindow::onPickLevelUpFemaleEffect()
{
    pickFileForEdit(
        m_levelUpFemaleEffectEdit,
        "ini/magic",
        "INI (*.ini)",
        PathMode::FileNameOnly);
}

void ResourceProfileEditorWindow::onPickReleaseCover()
{
    const QString packRoot = currentPackRoot();
    const QString fileName = chooseProfileAssetFile(
        tr("选择 MOD 封面"),
        packRoot,
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.qoi *.tga)"));
    if (fileName.isEmpty())
    {
        return;
    }
    QString relativePath;
    if (!EditorAssetPath::makeLogicalResourceRelativePath(
            packRoot,
            fileName,
            relativePath))
    {
        QMessageBox::warning(
            this,
            tr("路径无效"),
            tr("封面文件必须位于当前 MOD 根目录内。"));
        return;
    }

    const std::string utf8Path = exactTrimmedUtf8(relativePath);
    if (!ResourcePathSafety::isStrictRelativeResourcePath(utf8Path))
    {
        QMessageBox::warning(
            this,
            tr("路径无效"),
            tr("封面文件必须使用当前 MOD 根内的安全相对路径。"));
        return;
    }
    m_releaseCoverEdit->setText(relativePath);
}

void ResourceProfileEditorWindow::onPickReleaseDescriptionFile()
{
    if (m_releaseDescriptionTextModified)
    {
        const auto result = QMessageBox::question(
            this,
            tr("选择 MOD 简介文件"),
            tr("选择其他文件会丢弃尚未保存的 MOD 简介正文，是否继续？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes)
        {
            return;
        }
    }

    const QString packRoot = currentPackRoot();
    const QString fileName = chooseProfileAssetFile(
        tr("选择 MOD 简介文件"),
        packRoot,
        tr("文本 (*.txt *.md)"));
    if (fileName.isEmpty())
    {
        return;
    }
    QString relativePath;
    if (!EditorAssetPath::makeLogicalResourceRelativePath(
            packRoot,
            fileName,
            relativePath))
    {
        QMessageBox::warning(
            this,
            tr("路径无效"),
            tr("简介文件必须位于当前 MOD 根目录内。"));
        return;
    }

    const std::string utf8Path = exactTrimmedUtf8(relativePath);
    if (!ResourcePathSafety::isStrictRelativeResourcePath(utf8Path))
    {
        QMessageBox::warning(
            this,
            tr("路径无效"),
            tr("简介文件必须使用当前 MOD 根内的安全相对路径。"));
        return;
    }
    m_releaseDescriptionFileEdit->setText(relativePath);
    loadReleaseDescriptionText(relativePath, true);
}

void ResourceProfileEditorWindow::onReloadReleaseDescriptionFile()
{
    if (m_releaseDescriptionTextModified)
    {
        const auto result = QMessageBox::question(
            this,
            tr("重新载入 MOD 简介"),
            tr("重新载入会丢弃尚未保存的 MOD 简介正文，是否继续？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes)
        {
            return;
        }
    }
    loadReleaseDescriptionText(
        m_releaseDescriptionFileEdit->text().trimmed(), true);
}

void ResourceProfileEditorWindow::onPickTeamInfoFile()
{
    // Team.InfoFile 是相对于当前资源包根目录的路径。
    if (m_teamInfoTextModified)
    {
        const auto result = QMessageBox::question(this,
            tr("选择团队说明文件"),
            tr("选择其他文件会丢弃尚未保存的团队说明正文，是否继续？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes)
        {
            return;
        }
    }
    const QString previousPath = m_teamInfoFileEdit->text();
    pickFileForEdit(m_teamInfoFileEdit, QString(), "Text (*.txt *.md)",
        PathMode::RelativeToPackRoot, true);
    if (m_teamInfoFileEdit->text() != previousPath)
    {
        loadTeamInfoText(m_teamInfoFileEdit->text().trimmed(), true);
    }
}

void ResourceProfileEditorWindow::onReloadTeamInfoFile()
{
    if (m_teamInfoTextModified)
    {
        const auto result = QMessageBox::question(this,
            tr("重新载入团队说明"),
            tr("重新载入会丢弃尚未保存的团队说明正文，是否继续？"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes)
        {
            return;
        }
    }
    loadTeamInfoText(m_teamInfoFileEdit->text().trimmed(), true);
}

void ResourceProfileEditorWindow::onPickNewGameScript()
{
    // NewGame.Script 使用 ScriptAPI 搜索规则（script\common\ 等），只保存文件名
    pickFileForEdit(m_newGameScriptEdit, "script/common", "Script (*.txt *.lua)", PathMode::FileNameOnly);
}

void ResourceProfileEditorWindow::pickFileForEdit(
    QLineEdit* edit, const QString& defaultSubDir, const QString& filter,
    PathMode mode, bool requireSafeRelativeResourcePath)
{
    QString packRoot = currentPackRoot();
    QString defaultDir = packRoot.isEmpty() ? QString() : QDir(packRoot).absoluteFilePath(defaultSubDir);
    QString fileName = chooseProfileAssetFile(
        tr("选择文件"),
        defaultDir,
        filter);
    if (fileName.isEmpty())
    {
        return;
    }

    QString relativeName;
    if (mode == PathMode::FileNameOnly)
    {
        // 运行时会自动加 video\ 或 music\ 前缀，只保存文件名
        QFileInfo fi(fileName);
        relativeName = fi.fileName();
    }
    else
    {
        // RelativeToPackRoot: 保存相对于资源包根目录的路径
        if (requireSafeRelativeResourcePath)
        {
            if (!EditorAssetPath::makeLogicalResourceRelativePath(
                    packRoot,
                    fileName,
                    relativeName))
            {
                QMessageBox::warning(this, tr("路径无效"),
                    tr("所选团队说明文件必须位于当前资源包根目录内。"));
                return;
            }
            relativeName.replace('/', '\\');
        }
        else if (!packRoot.isEmpty())
        {
            QDir packDir(packRoot);
            relativeName = packDir.relativeFilePath(fileName);
            // 统一使用反斜杠（与运行时一致）
            relativeName.replace('/', '\\');
        }
        else
        {
            QFileInfo fi(fileName);
            relativeName = fi.fileName();
        }
    }
    if (requireSafeRelativeResourcePath &&
        !EditorResourcePath::isSafeOptionalRelativeResourcePath(relativeName))
    {
        QMessageBox::warning(this, tr("路径无效"),
            tr("所选团队说明文件必须位于当前资源包根目录内。"));
        return;
    }
    edit->setText(relativeName);
    markModified();
}

bool ResourceProfileEditorWindow::loadTeamInfoText(
    const QString& relativePath, bool showErrors)
{
    m_loadedTeamInfoFilePath = normalizedRelativePathKey(relativePath);
    auto replaceEditorText = [this](const QString& text)
    {
        const bool wasUpdatingFromCode = m_updatingFromCode;
        m_updatingFromCode = true;
        m_teamInfoTextEdit->setPlainText(text);
        m_teamInfoTextModified = false;
        m_updatingFromCode = wasUpdatingFromCode;
    };

    if (relativePath.isEmpty())
    {
        replaceEditorText(QString());
        return true;
    }

    QString resolvedPath;
    if (!EditorResourcePath::isSafeOptionalRelativeResourcePath(relativePath) ||
        !EditorAssetPath::resolveLogicalResourcePath(
            currentPackRoot(),
            relativePath,
            resolvedPath))
    {
        replaceEditorText(QString());
        if (showErrors)
        {
            QMessageBox::warning(this, tr("团队说明无法载入"),
                tr("Team.InfoFile 必须是当前资源包根目录内的安全相对路径。"));
        }
        return false;
    }

    QFile file(resolvedPath);
    if (!file.exists())
    {
        replaceEditorText(QString());
        if (showErrors)
        {
            QMessageBox::information(this, tr("团队说明文件不存在"),
                tr("当前路径尚无文件；可直接填写正文，保存资源清单时会创建该文件。"));
        }
        return true;
    }
    if (file.size() > MaximumTeamInfoBytes || !file.open(QIODevice::ReadOnly))
    {
        replaceEditorText(QString());
        if (showErrors)
        {
            QMessageBox::warning(this, tr("团队说明无法载入"),
                tr("团队说明文件无法读取，或大小超过 64 KiB。"));
        }
        return false;
    }

    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF"))
    {
        bytes.remove(0, 3);
    }
    if (bytes.size() > MaximumTeamInfoBytes || !isValidTeamInfoUtf8(bytes))
    {
        replaceEditorText(QString());
        if (showErrors)
        {
            QMessageBox::warning(this, tr("团队说明无法载入"),
                tr("团队说明必须是不超过 64 KiB 的有效 UTF-8 文本。"));
        }
        return false;
    }

    replaceEditorText(QString::fromUtf8(bytes.constData(), bytes.size()));
    return true;
}

bool ResourceProfileEditorWindow::prepareTeamInfoWrite(
    GameProfile& profile, const QString& packRoot, QString& resolvedPath,
    QByteArray& bytes, bool& shouldWrite)
{
    resolvedPath.clear();
    bytes.clear();
    shouldWrite = false;
    if (!m_teamInfoTextModified)
    {
        return true;
    }

    const QString text = m_teamInfoTextEdit->toPlainText();
    if (profile.teamInfoFile.isEmpty())
    {
        if (text.trimmed().isEmpty())
        {
            return true;
        }
        profile.teamInfoFile = QStringLiteral("team.txt");
    }

    if (!EditorResourcePath::isSafeOptionalRelativeResourcePath(
            profile.teamInfoFile) ||
        !EditorAssetPath::resolveInside(
            packRoot, profile.teamInfoFile, resolvedPath))
    {
        QMessageBox::warning(this, tr("清单无效"),
            tr("Team.InfoFile 必须留空，或填写当前资源包根目录内的安全相对路径。"));
        return false;
    }

    bytes = text.toUtf8();
    if (QString::fromUtf8(bytes.constData(), bytes.size()) != text ||
        bytes.size() > MaximumTeamInfoBytes || !isValidTeamInfoUtf8(bytes))
    {
        QMessageBox::warning(this, tr("团队说明无效"),
            tr("团队说明必须是不超过 64 KiB 的有效 UTF-8 文本，且不能包含 NUL 或控制字符。"));
        return false;
    }
    shouldWrite = true;
    return true;
}

bool ResourceProfileEditorWindow::loadReleaseDescriptionText(
    const QString& relativePath,
    bool showErrors)
{
    m_loadedReleaseDescriptionFilePath =
        normalizedRelativePathKey(relativePath);
    auto replaceEditorText = [this](const QString& text)
    {
        const bool wasUpdatingFromCode = m_updatingFromCode;
        m_updatingFromCode = true;
        m_releaseDescriptionTextEdit->setPlainText(text);
        m_releaseDescriptionTextModified = false;
        m_updatingFromCode = wasUpdatingFromCode;
    };

    if (relativePath.trimmed().isEmpty())
    {
        replaceEditorText(QString());
        return true;
    }

    QString text;
    const LogicalResourceReadStatus status =
        readLogicalDescription(
            currentPackRoot(),
            relativePath,
            text);
    if (status == LogicalResourceReadStatus::Ready)
    {
        replaceEditorText(text);
        return true;
    }

    replaceEditorText(QString());
    if (showErrors)
    {
        if (status ==
            LogicalResourceReadStatus::NotFound)
        {
            QMessageBox::information(
                this,
                tr("MOD 简介文件不存在"),
                tr("当前路径尚无文件；可直接填写正文，保存资源清单时会创建该文件。"));
        }
        else
        {
            QMessageBox::warning(
                this,
                tr("MOD 简介无法载入"),
                tr("简介必须是当前 MOD 根内不超过 64 KiB 的有效 UTF-8 文本，"
                   "且不能包含 NUL 或控制字符。"));
        }
    }
    return status ==
        LogicalResourceReadStatus::NotFound;
}

bool ResourceProfileEditorWindow::prepareReleaseDescriptionWrite(
    GameProfile& profile,
    const QString& packRoot,
    QString& resolvedPath,
    QByteArray& bytes,
    bool& shouldWrite)
{
    resolvedPath.clear();
    bytes.clear();
    shouldWrite = false;
    if (!m_releaseDescriptionTextModified)
    {
        return true;
    }

    const QString text = m_releaseDescriptionTextEdit->toPlainText();
    if (profile.releaseMetadata.descriptionFilePath.empty())
    {
        if (text.trimmed().isEmpty())
        {
            return true;
        }
        profile.releaseMetadata.descriptionFilePath = "description.txt";
    }

    const auto normalized =
        ResourcePathSafety::normalizeStrictRelativeResourcePath(
            profile.releaseMetadata.descriptionFilePath);
    if (!normalized.succeeded())
    {
        QMessageBox::warning(
            this,
            tr("清单无效"),
            tr("Release.DescriptionFile 必须留空，或填写当前 MOD 根内的安全相对路径。"));
        return false;
    }
    profile.releaseMetadata.descriptionFilePath =
        normalized.normalizedPath;
    const QString relativePath =
        QString::fromStdString(normalized.normalizedPath);
    if (!EditorAssetPath::resolveInside(
            packRoot, relativePath, resolvedPath))
    {
        QMessageBox::warning(
            this,
            tr("清单无效"),
            tr("Release.DescriptionFile 解析后必须位于当前 MOD 根目录内。"));
        return false;
    }

    bytes = text.toUtf8();
    const std::string utf8Text(
        bytes.constData(), static_cast<std::size_t>(bytes.size()));
    if (QString::fromUtf8(bytes.constData(), bytes.size()) != text ||
        static_cast<std::size_t>(bytes.size()) >
            ModRelease::MaximumDescriptionBytes ||
        !ModRelease::isValidDescriptionUtf8(utf8Text))
    {
        QMessageBox::warning(
            this,
            tr("MOD 简介无效"),
            tr("MOD 简介必须是不超过 64 KiB 的有效 UTF-8 文本，"
               "且不能包含 NUL 或控制字符。"));
        return false;
    }
    shouldWrite = true;
    return true;
}

void ResourceProfileEditorWindow::updateReleaseCompatibilityLabel()
{
    if (!m_releaseCompatibilityLabel ||
        !m_minimumEngineVersionEdit)
    {
        return;
    }

    ModRelease::ModReleaseMetadata metadata;
    metadata.minimumEngineVersion =
        exactTrimmedUtf8(m_minimumEngineVersionEdit->text());
    const QString currentEngineVersion = BuildVersion::engineVersion();
    const ModRelease::CompatibilityResult compatibility =
        ModRelease::evaluateCompatibility(
            metadata, exactTrimmedUtf8(currentEngineVersion));
    m_releaseCompatibilityLabel->setText(compatibilityStatusText(
        compatibility.status,
        currentEngineVersion,
        m_minimumEngineVersionEdit->text().trimmed()));
    QString color = QStringLiteral("#2e7d32");
    if (compatibility.status ==
        ModRelease::CompatibilityStatus::RequiresNewerEngine) {
        color = QStringLiteral("#a05a00");
    } else if (compatibility.status ==
                   ModRelease::CompatibilityStatus::InvalidMinimumEngineVersion ||
               compatibility.status ==
                   ModRelease::CompatibilityStatus::InvalidCurrentEngineVersion) {
        color = QStringLiteral("#b71c1c");
    }
    m_releaseCompatibilityLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(color));
}

void ResourceProfileEditorWindow::updateReleaseCoverPreview()
{
    if (!m_releaseCoverPreviewLabel || !m_releaseCoverEdit)
    {
        return;
    }

    auto showPlaceholder = [this]()
    {
        m_releaseCoverPreviewLabel->setPixmap(QPixmap());
        m_releaseCoverPreviewLabel->setText(tr("无可用封面"));
    };

    QString relativePath = m_releaseCoverEdit->text().trimmed();
    relativePath.replace('\\', '/');
    QByteArray encodedBytes;
    if (readBoundedLogicalResource(
            currentPackRoot(),
            relativePath,
            static_cast<qsizetype>(
                EncodedImageSafety::
                    MaxEncodedImageBytes),
            encodedBytes) !=
            LogicalResourceReadStatus::Ready)
    {
        showPlaceholder();
        return;
    }

    EncodedImageSafety::Dimensions dimensions;
    if (!EncodedImageSafety::inspectSafeDimensions(
            reinterpret_cast<const std::uint8_t*>(
                encodedBytes.constData()),
            static_cast<std::size_t>(
                encodedBytes.size()),
            dimensions))
    {
        showPlaceholder();
        return;
    }
    const QImage image = QImage::fromData(
        encodedBytes);
    if (image.isNull())
    {
        showPlaceholder();
        return;
    }
    m_releaseCoverPreviewLabel->setText(QString());
    m_releaseCoverPreviewLabel->setPixmap(
        QPixmap::fromImage(image).scaled(
            320,
            180,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void ResourceProfileEditorWindow::updateExperiencePreview()
{
    if (m_experiencePreviewLabel == nullptr ||
        m_experienceModeCombo == nullptr)
    {
        return;
    }

    if (m_inheritTypeCheck->isChecked() &&
        (m_experienceModeCombo->currentIndex() == 0 ||
         !m_experienceMultiplierCheck->isChecked()))
    {
        m_experiencePreviewLabel->setText(tr(
            "当前资源继承 Game.Type，默认经验模式或倍率由内容依赖决定；"
            "请显式配置两项后再查看数值示例。"));
        return;
    }

    ResourceManifest previewManifest;
    previewManifest.type = m_typeSpin->value();
    previewManifest.typeDefined = !m_inheritTypeCheck->isChecked();
    previewManifest.defeatedNpcExperienceModeDefined =
        m_experienceModeCombo->currentIndex() > 0;
    previewManifest.defeatedNpcExperienceMode =
        m_experienceModeCombo->currentIndex() == 1
            ? DefeatedNpcExperienceMode::StoredExperience
            : DefeatedNpcExperienceMode::LevelProductWithBonus;
    previewManifest.experienceMultiplierDefined =
        m_experienceMultiplierCheck->isChecked();
    previewManifest.experienceMultiplier =
        m_experienceMultiplierSpin->value();

    // Use fixed, visible inputs so authors can immediately see whether the
    // selected mode reads NPC.Exp or calculates the level product.
    const int baseExperience = calculateDefeatedNpcBaseExperience(
        previewManifest,
        4,
        5,
        100,
        0);
    const double scaledExperience = scaleAutomaticExperience(
        baseExperience,
        previewManifest.resolvedExperienceMultiplier());
    const int automaticExperience =
        roundAutomaticExperience(scaledExperience);
    m_experiencePreviewLabel->setText(tr(
        "示例：角色等级 4、NPC 等级 5、NPC.Exp=100、ExpBonus=0。"
        "角色获得 %1；修炼武功和当前使用武功按各自 Fraction 乘同一倍率后向下取整；"
        "命中经验仍只使用 HitMagicExp.LevelFactor，不乘此倍率。")
        .arg(automaticExperience));
}

bool ResourceProfileEditorWindow::saveProfileTransaction(
    GameProfile& profile,
    const QString& manifestPath,
    const QString& packRoot,
    const ResourcePackInfo* indexedPack,
    QString& errorOrWarning)
{
    errorOrWarning.clear();
    const QString transactionRoot =
        indexedPack != nullptr && !m_assetsBasePath.isEmpty()
            ? m_assetsBasePath
            : packRoot;
    QStringList recoveryErrors;
    if (!DurableFileTransaction::recoverPending(
            transactionRoot, recoveryErrors))
    {
        errorOrWarning = tr("无法恢复上一次资源清单保存事务：\n%1")
            .arg(recoveryErrors.join('\n'));
        return false;
    }
    auto mutationLease = AuthoringMutationGate::instance().
        acquireMutationLeaseForPath(packRoot);
    if (!mutationLease ||
        !mutationLease.addResourcePath(manifestPath) ||
        (!profile.manifestPath.isEmpty() &&
         !mutationLease.addResourcePath(profile.manifestPath)))
    {
        errorOrWarning = tr(
            "资源包正在更新或进行其他写入，无法保存资源清单。");
        return false;
    }

    QString teamInfoPath;
    QByteArray teamInfoBytes;
    bool shouldWriteTeamInfo = false;
    QString descriptionPath;
    QByteArray descriptionBytes;
    bool shouldWriteDescription = false;
    if (!prepareTeamInfoWrite(
            profile,
            packRoot,
            teamInfoPath,
            teamInfoBytes,
            shouldWriteTeamInfo) ||
        !prepareReleaseDescriptionWrite(
            profile,
            packRoot,
            descriptionPath,
            descriptionBytes,
            shouldWriteDescription) ||
        !validateProfileForSave(profile, packRoot))
    {
        return false;
    }

    QByteArray manifestBytes;
    QByteArray manifestSourceBytes;
    bool manifestSourceExists = false;
    if (!profile.prepareSaveBytes(
            manifestPath,
            manifestBytes,
            &manifestSourceBytes,
            &manifestSourceExists))
    {
        errorOrWarning = tr(
            "无法读取现有 game_profile.ini；已取消保存以避免丢失未知字段或注释。");
        return false;
    }

    DurableFileTransaction transaction(transactionRoot);
    QStringList targetKeys;
    QByteArray manifestExpectedBytes = manifestSourceBytes;
    bool manifestExpectedExists = manifestSourceExists;
    if (profile.manifestPath.isEmpty() ||
        EditorAssetPath::comparisonKey(profile.manifestPath) !=
            EditorAssetPath::comparisonKey(manifestPath))
    {
        if (!readFileSnapshot(
                manifestPath,
                manifestExpectedExists,
                manifestExpectedBytes))
        {
            errorOrWarning = tr(
                "无法读取另存目标；已取消保存以避免覆盖未知内容。");
            return false;
        }
    }
    auto addWrite =
        [&](const QString& targetPath,
            const QByteArray& bytes,
            bool expectedExists,
            const QByteArray& expectedBytes) -> bool
    {
        const QString targetKey =
            EditorAssetPath::comparisonKey(targetPath);
        if (targetKeys.contains(targetKey))
        {
            errorOrWarning = tr(
                "多个清单字段指向同一个保存目标：%1").arg(targetPath);
            return false;
        }
        targetKeys.append(targetKey);
        return transaction.addBytesWriteChecked(
            targetPath,
            bytes,
            expectedExists,
            expectedBytes,
            errorOrWarning);
    };

    bool teamInfoExists = false;
    QByteArray teamInfoSourceBytes;
    bool descriptionExists = false;
    QByteArray descriptionSourceBytes;
    if ((shouldWriteTeamInfo &&
         !readFileSnapshot(
             teamInfoPath, teamInfoExists, teamInfoSourceBytes)) ||
        (shouldWriteDescription &&
         !readFileSnapshot(
             descriptionPath,
             descriptionExists,
             descriptionSourceBytes)))
    {
        errorOrWarning = tr(
            "无法读取待写入的说明文件；已取消整个保存事务。");
        return false;
    }

    if (!addWrite(
            manifestPath,
            manifestBytes,
            manifestExpectedExists,
            manifestExpectedBytes) ||
        (shouldWriteTeamInfo &&
         !addWrite(
             teamInfoPath,
             teamInfoBytes,
             teamInfoExists,
             teamInfoSourceBytes)) ||
        (shouldWriteDescription &&
         !addWrite(
             descriptionPath,
             descriptionBytes,
             descriptionExists,
             descriptionSourceBytes)))
    {
        transaction.cancel();
        return false;
    }
    return transaction.commit(errorOrWarning);
}

void ResourceProfileEditorWindow::onSave()
{
    if (m_transactionRecoveryBlocked)
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("当前资源目录仍有未恢复事务，保存功能保持停用：\n%1")
                .arg(m_transactionRecoveryErrorText));
        return;
    }
    if (m_currentPackIndex < 0 || m_currentPackIndex >= m_packs.size())
    {
        QMessageBox::information(this, tr("提示"), tr("请先选择一个资源包。"));
        return;
    }

    const ResourcePackInfo pack = m_packs[m_currentPackIndex];
    GameProfile profile = collectProfileFromUi();
    profile.manifestPath = pack.manifestPath;
    profile.rootPath = pack.rootPath;
    QString errorOrWarning;
    if (saveProfileTransaction(
            profile,
            pack.manifestPath,
            pack.rootPath,
            &pack,
            errorOrWarning))
    {
        const bool reloadTeamInfo =
            (!m_teamInfoTextModified &&
             m_loadedTeamInfoFilePath !=
                 normalizedRelativePathKey(profile.teamInfoFile)) ||
            (m_teamInfoTextModified &&
             profile.teamInfoFile.isEmpty());
        const bool reloadReleaseDescription =
            (!m_releaseDescriptionTextModified &&
             m_loadedReleaseDescriptionFilePath !=
                 normalizedRelativePathKey(QString::fromStdString(
                     profile.releaseMetadata.descriptionFilePath))) ||
            (m_releaseDescriptionTextModified &&
             profile.releaseMetadata.descriptionFilePath.empty());
        m_updatingFromCode = true;
        m_teamInfoFileEdit->setText(profile.teamInfoFile);
        m_profileVersionEdit->setText(QString::fromStdString(
            profile.releaseMetadata.displayVersion));
        m_releaseDateEdit->setText(QString::fromStdString(
            profile.releaseMetadata.releaseDate));
        m_minimumEngineVersionEdit->setText(QString::fromStdString(
            profile.releaseMetadata.minimumEngineVersion));
        m_releaseCoverEdit->setText(QString::fromStdString(
            profile.releaseMetadata.coverPath));
        m_releaseDescriptionFileEdit->setText(
            QString::fromStdString(
                profile.releaseMetadata.descriptionFilePath));
        m_updatingFromCode = false;
        if (reloadTeamInfo)
        {
            loadTeamInfoText(profile.teamInfoFile, false);
        }
        else
        {
            m_loadedTeamInfoFilePath =
                normalizedRelativePathKey(profile.teamInfoFile);
        }
        if (reloadReleaseDescription)
        {
            loadReleaseDescriptionText(
                QString::fromStdString(
                    profile.releaseMetadata.descriptionFilePath),
                false);
        }
        else
        {
            m_loadedReleaseDescriptionFilePath =
                normalizedRelativePathKey(QString::fromStdString(
                    profile.releaseMetadata.descriptionFilePath));
        }
        m_invalidLoadedReleaseFields.clear();
        m_hasUnsavedChanges = false;
        m_teamInfoTextModified = false;
        m_releaseDescriptionTextModified = false;
        // 更新内存中的数据
        m_packs[m_currentPackIndex].profile = profile;
        if (pack.profile.id.compare(profile.id, Qt::CaseSensitive) != 0)
        {
            emit resourcePackIdentityChanged(
                pack.profile.id, profile.id, pack.rootPath);
        }
        updateProjectResourceContextUi();
        updateReleaseCompatibilityLabel();
        updateReleaseCoverPreview();
        QString message = tr("已保存到：\n") + pack.manifestPath;
        if (!errorOrWarning.isEmpty())
        {
            message += tr("\n\n保存完成，但清理旧事务文件时出现警告：\n") +
                errorOrWarning;
        }
        QMessageBox::information(this, tr("成功"), message);
    }
    else if (!errorOrWarning.isEmpty())
    {
        QMessageBox::critical(this, tr("错误"),
            errorOrWarning);
    }
}

void ResourceProfileEditorWindow::onSaveAs()
{
    if (m_transactionRecoveryBlocked)
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("当前资源目录仍有未恢复事务，另存功能保持停用：\n%1")
                .arg(m_transactionRecoveryErrorText));
        return;
    }
    if (m_currentPackIndex < 0 || m_currentPackIndex >= m_packs.size())
    {
        QMessageBox::information(
            this, tr("提示"), tr("请先选择一个资源包。"));
        return;
    }

    const ResourcePackInfo pack = m_packs[m_currentPackIndex];
    const bool originalHadUnsavedChanges = m_hasUnsavedChanges;
    QString defaultName = "game_profile.ini";
    const QString sourcePackRoot = currentPackRoot();
    QString defaultDir = sourcePackRoot.isEmpty()
        ? QStandardPaths::writableLocation(
              QStandardPaths::DocumentsLocation)
        : sourcePackRoot;
    QString fileName = chooseManifestSavePath(
        QDir(defaultDir).absoluteFilePath(defaultName));
    if (fileName.isEmpty())
    {
        return;
    }

    const QString currentManifestPath = pack.manifestPath;
    const QString canonicalCurrentManifest =
        QFileInfo(currentManifestPath).canonicalFilePath();
    const QString canonicalTargetManifest =
        QFileInfo(fileName).canonicalFilePath();
    if (!canonicalCurrentManifest.isEmpty() &&
        canonicalCurrentManifest.compare(
            canonicalTargetManifest, Qt::CaseSensitive) == 0)
    {
        onSave();
        return;
    }
    if (m_teamInfoTextModified ||
        m_releaseDescriptionTextModified)
    {
        QMessageBox::warning(
            this,
            tr("另存前需要先保存正文"),
            tr("团队说明或 MOD 简介正文仍有未保存修改。请先使用“保存”将复合事务"
               "写入当前资源包，再另存清单副本；另存操作不会改写当前包的辅助文件。"));
        return;
    }

    GameProfile profile = collectProfileFromUi();
    profile.manifestPath = currentManifestPath;
    const QString targetPackRoot = QFileInfo(fileName).absolutePath();
    const QFileInfo targetInfo(fileName);
    const QString targetFileName = targetInfo.fileName();
    if (targetFileName.isEmpty() ||
        targetInfo.suffix().compare(
            QStringLiteral("ini"), Qt::CaseInsensitive) != 0 ||
        EditorAssetPath::comparisonKey(targetInfo.absolutePath()) !=
            EditorAssetPath::comparisonKey(sourcePackRoot))
    {
        QMessageBox::warning(
            this,
            tr("另存位置无效"),
            tr("资源清单副本必须使用当前 MOD 根目录中的普通 .ini 文件名。"));
        return;
    }
    const QString canonicalSourceRoot =
        QFileInfo(sourcePackRoot).canonicalFilePath();
    const QString canonicalTargetRoot =
        QFileInfo(targetPackRoot).canonicalFilePath();
    if (canonicalSourceRoot.isEmpty() ||
        canonicalTargetRoot.isEmpty() ||
        canonicalSourceRoot.compare(
            canonicalTargetRoot, Qt::CaseSensitive) != 0)
    {
        QMessageBox::warning(
            this,
            tr("另存位置无效"),
            tr("资源清单只能另存到当前 MOD 根目录。跨目录复制还需要同时复制"
               "封面、简介和团队说明，当前操作不会静默创建不完整资源包。"));
        return;
    }
    QSet<QString> reservedTargetKeys;
    auto reserveTarget = [&reservedTargetKeys](const QString& path)
    {
        if (!path.isEmpty())
        {
            reservedTargetKeys.insert(
                EditorAssetPath::comparisonKey(path));
        }
    };
    reserveTarget(pack.manifestPath);
    reserveTarget(QDir(sourcePackRoot).filePath(
        QStringLiteral("game_profile.ini")));
    reserveTarget(QDir(sourcePackRoot).filePath(
        QStringLiteral("resources.ini")));
    auto reserveProfileAssets =
        [&reserveTarget, &sourcePackRoot](const GameProfile& reservedProfile)
    {
        QString resolvedReservedPath;
        if (!reservedProfile.teamInfoFile.isEmpty() &&
            EditorAssetPath::resolveInside(
                sourcePackRoot,
                reservedProfile.teamInfoFile,
                resolvedReservedPath))
        {
            reserveTarget(resolvedReservedPath);
        }
        for (const std::string* relativePath : {
                 &reservedProfile.releaseMetadata.coverPath,
                 &reservedProfile.releaseMetadata.descriptionFilePath })
        {
            if (!relativePath->empty() &&
                EditorAssetPath::resolveInside(
                    sourcePackRoot,
                    QString::fromStdString(*relativePath),
                    resolvedReservedPath))
            {
                reserveTarget(resolvedReservedPath);
            }
        }
    };
    reserveProfileAssets(pack.profile);
    reserveProfileAssets(profile);
    if (reservedTargetKeys.contains(
            EditorAssetPath::comparisonKey(fileName)))
    {
        QMessageBox::warning(
            this,
            tr("另存位置无效"),
            tr("资源清单副本不能覆盖当前清单、resources.ini、封面、"
               "MOD 简介或团队说明文件。"));
        return;
    }
    QString errorOrWarning;
    if (saveProfileTransaction(
            profile,
            fileName,
            targetPackRoot,
            nullptr,
            errorOrWarning))
    {
        m_hasUnsavedChanges = originalHadUnsavedChanges;
        QString message = tr("已另存到：\n") + fileName;
        if (m_hasUnsavedChanges)
        {
            message += tr("\n\n当前资源包原文件仍有未保存修改。");
        }
        if (!errorOrWarning.isEmpty())
        {
            message += tr("\n\n保存完成，但清理旧事务文件时出现警告：\n") +
                errorOrWarning;
        }
        QMessageBox::information(this, tr("成功"), message);
    }
    else if (!errorOrWarning.isEmpty())
    {
        QMessageBox::critical(
            this,
            tr("错误"),
            errorOrWarning);
    }
}

void ResourceProfileEditorWindow::onExportOnlinePackage()
{
    if (m_currentPackIndex < 0 || m_currentPackIndex >= m_packs.size())
    {
        QMessageBox::information(
            this, tr("提示"), tr("请先选择一个资源包。"));
        return;
    }
    if (m_hasUnsavedChanges)
    {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            tr("导出前保存"),
            tr("在线资源包必须从已保存的清单导出。是否先保存当前修改？"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (choice != QMessageBox::Yes)
        {
            return;
        }
        onSave();
        if (m_hasUnsavedChanges)
        {
            return;
        }
    }

    const ResourcePackInfo& pack = m_packs[m_currentPackIndex];
    const GameProfile profile = collectProfileFromUi();
    if (!validateProfileForSave(profile, pack.rootPath))
    {
        return;
    }
    if (profile.id.trimmed().isEmpty() ||
        profile.releaseMetadata.displayVersion.empty() ||
        profile.releaseMetadata.minimumEngineVersion.empty())
    {
        QMessageBox::warning(
            this,
            tr("无法导出在线资源包"),
            tr("在线资源包必须填写 Game.Id、Game.Version 和 Release.MinimumEngineVersion。"));
        return;
    }

    QString fileStem = profile.id.trimmed().toLower();
    fileStem.replace(QRegularExpression(
        QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
    fileStem = fileStem.trimmed();
    if (fileStem.isEmpty())
    {
        fileStem = QStringLiteral("resource");
    }
    const QString suggestedName = fileStem + QStringLiteral(".zip");
    const QString outputPath = chooseOnlinePackageSavePath(
        QDir(QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation)).filePath(suggestedName));
    if (outputPath.isEmpty())
    {
        return;
    }

    const OnlineResourcePackageExporter::Result result =
        OnlineResourcePackageExporter::exportPackage(
            pack.rootPath,
            outputPath);
    if (!result.succeeded())
    {
        QString reason;
        switch (result.status)
        {
        case OnlineResourcePackageExporter::Status::SourceBusy:
            reason = tr("资源正在保存或恢复事务，请稍后重试。");
            break;
        case OnlineResourcePackageExporter::Status::MissingManifest:
            reason = tr("资源包根目录缺少 game_profile.ini。");
            break;
        case OnlineResourcePackageExporter::Status::InvalidManifest:
            reason = tr("game_profile.ini 缺少有效的 Game.Id、Game.Version 或最低引擎版本。");
            break;
        case OnlineResourcePackageExporter::Status::UnsafeSourceEntry:
            reason = tr("资源包包含符号链接、目录联接或不安全路径。");
            break;
        case OnlineResourcePackageExporter::Status::NonLowercasePath:
            reason = tr("资源路径包含 ASCII 大写字母；在线资源名必须统一为小写。");
            break;
        case OnlineResourcePackageExporter::Status::DuplicatePortablePath:
            reason = tr("资源包包含在大小写不敏感平台上冲突的路径。");
            break;
        case OnlineResourcePackageExporter::Status::TooManyFiles:
            reason = tr("资源包文件数量超过上限。");
            break;
        case OnlineResourcePackageExporter::Status::PackageTooLarge:
            reason = tr("资源包解压后总大小超过上限。");
            break;
        case OnlineResourcePackageExporter::Status::SourceReadFailed:
            reason = tr("读取源文件失败。");
            break;
        case OnlineResourcePackageExporter::Status::OutputOpenFailed:
        case OnlineResourcePackageExporter::Status::OutputCommitFailed:
            reason = tr("无法安全写入目标文件。");
            break;
        case OnlineResourcePackageExporter::Status::ArchiveWriteFailed:
            reason = tr("ZIP 归档写入失败。");
            break;
        case OnlineResourcePackageExporter::Status::ArchiveReadbackFailed:
            reason = tr("ZIP 已生成，但回读目录结构或资源身份校验失败。");
            break;
        case OnlineResourcePackageExporter::Status::CatalogWriteFailed:
            reason = tr("无法生成对应的单资源在线目录文件。");
            break;
        case OnlineResourcePackageExporter::Status::ChecksumReadFailed:
            reason = tr("归档已生成，但无法计算 CRC32。");
            break;
        default:
            reason = tr("导出参数无效。");
            break;
        }
        if (!result.errorPath.isEmpty())
        {
            reason += tr("\n\n相关路径：%1").arg(result.errorPath);
        }
        QMessageBox::critical(
            this, tr("在线资源包导出失败"), reason);
        return;
    }

    QMessageBox::information(
        this,
        tr("资源发布制品已生成"),
        tr("资源包：%1\n目录文件：%2\n文件数：%3\n解压大小：%4 字节\n归档大小：%5 字节\nCRC32：%6\n\n源资源未被修改。")
            .arg(outputPath)
            .arg(result.catalogPath)
            .arg(result.fileCount)
            .arg(result.uncompressedSize)
            .arg(result.archiveSize)
            .arg(result.crc32Hex));
}

QString ResourceProfileEditorWindow::chooseManifestSavePath(
    const QString& suggestedPath)
{
    return QFileDialog::getSaveFileName(
        this,
        tr("另存为"),
        suggestedPath,
        QStringLiteral("INI (*.ini)"));
}

QString ResourceProfileEditorWindow::chooseOnlinePackageSavePath(
    const QString& suggestedPath)
{
    return QFileDialog::getSaveFileName(
        this,
        tr("发布资源"),
        suggestedPath,
        QStringLiteral("ZIP (*.zip)"));
}

QString ResourceProfileEditorWindow::chooseProfileAssetFile(
    const QString& title,
    const QString& initialPath,
    const QString& filter)
{
    return QFileDialog::getOpenFileName(
        this,
        title,
        initialPath,
        filter,
        nullptr,
        QFileDialog::DontResolveSymlinks);
}

bool ResourceProfileEditorWindow::validateProfileForSave(
    const GameProfile& profile, const QString& packRoot)
{
    if (!m_invalidLoadedReleaseFields.isEmpty())
    {
        QMessageBox::warning(
            this,
            tr("清单无效"),
            tr("原清单包含无效 UTF-8 发布字段。请逐项重新填写带错误的发布信息后再保存。"));
        return false;
    }
    if (profile.id.trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("清单无效"),
            tr("Game.Id 不能为空。"));
        return false;
    }
    if (profile.experienceMultiplierDefined &&
        (!std::isfinite(profile.experienceMultiplier) ||
         profile.experienceMultiplier < 0.0))
    {
        QMessageBox::warning(this, tr("清单无效"),
            tr("Experience.ExperienceMultiplier 必须是有限的非负数。"));
        return false;
    }
    QStringList levelUpEffectFiles = profile.levelUpRandomEffects;
    if (!profile.levelUpMaleEffect.isEmpty())
    {
        levelUpEffectFiles.append(profile.levelUpMaleEffect);
    }
    if (!profile.levelUpFemaleEffect.isEmpty())
    {
        levelUpEffectFiles.append(profile.levelUpFemaleEffect);
    }
    for (const QString& effectFile : levelUpEffectFiles)
    {
        if (effectFile.contains(',') ||
            !effectFile.endsWith(
                QStringLiteral(".ini"), Qt::CaseInsensitive) ||
            !EditorResourcePath::isSafeOptionalRelativeResourcePath(
                effectFile))
        {
            QMessageBox::warning(
                this,
                tr("清单无效"),
                tr("LevelUp 特效必须是 ini/magic 下的安全相对 INI 路径，"
                   "且文件名不能包含逗号：%1").arg(effectFile));
            return false;
        }
    }
    const QList<QLineEdit*> releaseTextFields = {
        m_profileVersionEdit,
        m_releaseDateEdit,
        m_minimumEngineVersionEdit,
        m_releaseCoverEdit,
        m_releaseDescriptionFileEdit
    };
    for (const QLineEdit* field : releaseTextFields)
    {
        const QString value = field->text().trimmed();
        const QByteArray utf8 = value.toUtf8();
        if (QString::fromUtf8(utf8.constData(), utf8.size()) != value)
        {
            QMessageBox::warning(
                this,
                tr("清单无效"),
                tr("MOD 发布信息包含无法写入 UTF-8 的字符。"));
            return false;
        }
    }

    const auto releaseIssues =
        ModRelease::validateMetadata(profile.releaseMetadata);
    if (!releaseIssues.empty())
    {
        QString message;
        switch (releaseIssues.front().field)
        {
        case ModRelease::MetadataField::DisplayVersion:
            message = tr("Game.Version 必须是有效 UTF-8 展示文本，且不能包含控制字符。");
            break;
        case ModRelease::MetadataField::ReleaseDate:
            message = tr("Release.Date 必须留空，或填写真实的 YYYY-MM-DD 公历日期。");
            break;
        case ModRelease::MetadataField::MinimumEngineVersion:
            message = tr("Release.MinimumEngineVersion 必须留空，或填写严格 SemVer，例如 1.4.3。");
            break;
        case ModRelease::MetadataField::CoverPath:
            message = tr("Release.Cover 必须留空，或填写当前 MOD 根内的安全相对路径。");
            break;
        case ModRelease::MetadataField::DescriptionFilePath:
            message = tr("Release.DescriptionFile 必须留空，或填写当前 MOD 根内的安全相对路径。");
            break;
        }
        QMessageBox::warning(this, tr("清单无效"), message);
        return false;
    }

    QString resolvedCoverPath;
    QString resolvedDescriptionPath;
    const std::pair<const std::string*, QString*> releasePaths[] = {
        {&profile.releaseMetadata.coverPath, &resolvedCoverPath},
        {&profile.releaseMetadata.descriptionFilePath,
         &resolvedDescriptionPath}
    };
    for (const auto& releasePath : releasePaths)
    {
        if (releasePath.first->empty())
        {
            continue;
        }
        if (!EditorAssetPath::resolveLogicalResourcePath(
                packRoot,
                QString::fromStdString(*releasePath.first),
                *releasePath.second))
        {
            QMessageBox::warning(
                this,
                tr("清单无效"),
                tr("封面和简介文件必须使用当前 MOD 根目录下的安全相对路径。"));
            return false;
        }
    }

    QString resolvedTeamInfoPath;
    if (!EditorResourcePath::isSafeOptionalRelativeResourcePath(
            profile.teamInfoFile) ||
        (!profile.teamInfoFile.isEmpty() &&
         !EditorAssetPath::resolveLogicalResourcePath(
             packRoot, profile.teamInfoFile, resolvedTeamInfoPath)))
    {
        QMessageBox::warning(this, tr("清单无效"),
            tr("Team.InfoFile 必须留空，或填写当前资源包根目录内的安全相对路径。"));
        return false;
    }
    if (isEditorTransactionStatePath(profile.teamInfoFile) ||
        isEditorTransactionStatePath(QString::fromStdString(
            profile.releaseMetadata.descriptionFilePath)))
    {
        QMessageBox::warning(
            this,
            tr("清单无效"),
            tr("团队说明和 MOD 简介不能写入 .jxqy_editor 内部状态目录。"));
        return false;
    }
    QSet<QString> structuralTargetKeys;
    auto addStructuralTarget =
        [&structuralTargetKeys](const QString& path)
        {
            if (!path.isEmpty())
            {
                structuralTargetKeys.insert(
                    EditorAssetPath::comparisonKey(path));
            }
        };
    addStructuralTarget(profile.manifestPath);
    addStructuralTarget(QDir(packRoot).filePath(
        QStringLiteral("game_profile.ini")));
    addStructuralTarget(QDir(packRoot).filePath(
        QStringLiteral("resources.ini")));
    const QString teamInfoTargetKey =
        resolvedTeamInfoPath.isEmpty()
            ? QString()
            : EditorAssetPath::comparisonKey(
                  resolvedTeamInfoPath);
    const QString descriptionTargetKey =
        resolvedDescriptionPath.isEmpty()
            ? QString()
            : EditorAssetPath::comparisonKey(
                  resolvedDescriptionPath);
    const QString coverTargetKey =
        resolvedCoverPath.isEmpty()
            ? QString()
            : EditorAssetPath::comparisonKey(
                  resolvedCoverPath);
    const bool writesTeamInfo =
        m_teamInfoTextModified &&
        !teamInfoTargetKey.isEmpty();
    const bool writesDescription =
        m_releaseDescriptionTextModified &&
        !descriptionTargetKey.isEmpty();
    if ((writesTeamInfo &&
         (structuralTargetKeys.contains(
              teamInfoTargetKey) ||
          teamInfoTargetKey ==
              descriptionTargetKey ||
          teamInfoTargetKey ==
              coverTargetKey)) ||
        (writesDescription &&
         (structuralTargetKeys.contains(
              descriptionTargetKey) ||
          descriptionTargetKey ==
              teamInfoTargetKey ||
          descriptionTargetKey ==
              coverTargetKey)))
    {
        QMessageBox::warning(
            this,
            tr("清单无效"),
            tr("Team.InfoFile 和 Release.DescriptionFile 必须使用彼此独立的正文文件，"
               "且不能覆盖 game_profile.ini、resources.ini 或封面文件。"));
        return false;
    }
    if (!m_teamInfoTextModified &&
        !profile.teamInfoFile.isEmpty() &&
        QFileInfo::exists(resolvedTeamInfoPath))
    {
        QFile teamInfoFile(resolvedTeamInfoPath);
        if (!QFileInfo(resolvedTeamInfoPath).isFile() ||
            teamInfoFile.size() > MaximumTeamInfoBytes ||
            !teamInfoFile.open(QIODevice::ReadOnly))
        {
            QMessageBox::warning(
                this,
                tr("团队说明无法载入"),
                tr("团队说明文件无法读取，或大小超过 64 KiB。"));
            return false;
        }
        QByteArray teamInfoBytes = teamInfoFile.readAll();
        if (teamInfoBytes.startsWith("\xEF\xBB\xBF"))
        {
            teamInfoBytes.remove(0, 3);
        }
        if (teamInfoFile.error() != QFileDevice::NoError ||
            teamInfoBytes.size() > MaximumTeamInfoBytes ||
            !isValidTeamInfoUtf8(teamInfoBytes))
        {
            QMessageBox::warning(
                this,
                tr("团队说明无法载入"),
                tr("团队说明必须是不超过 64 KiB 的有效 UTF-8 文本。"));
            return false;
        }
    }
    if (!m_releaseDescriptionTextModified &&
        !profile.releaseMetadata.descriptionFilePath.empty())
    {
        QString descriptionText;
        const LogicalResourceReadStatus status =
            readLogicalDescription(
                packRoot,
                QString::fromStdString(
                    profile.releaseMetadata.
                        descriptionFilePath),
                descriptionText);
        if (status !=
                LogicalResourceReadStatus::Ready &&
            status !=
                LogicalResourceReadStatus::NotFound)
        {
            QMessageBox::warning(
                this,
                tr("MOD 简介无法载入"),
                tr("简介必须是当前 MOD 根内不超过 64 KiB 的有效 UTF-8 文本，"
                   "且不能包含 NUL 或控制字符。"));
            return false;
        }
    }
    return true;
}

void ResourceProfileEditorWindow::onCreateDefaultManifest()
{
    if (m_transactionRecoveryBlocked)
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("当前资源目录仍有未恢复事务，不能创建或覆盖 Manifest：\n%1")
                .arg(m_transactionRecoveryErrorText));
        return;
    }
    if (m_assetsBasePath.isEmpty() || !QDir(m_assetsBasePath).exists())
    {
        QMessageBox::warning(this, tr("提示"), tr("请先设置有效的 assets 路径。"));
        return;
    }

    const QString manifestPath =
        QDir(m_assetsBasePath).absoluteFilePath("game_profile.ini");
    GameProfile defaultProfile = GameProfile::createDefault();
    QByteArray manifestBytes;
    QByteArray expectedSourceBytes;
    bool expectedSourceExists = false;
    if (!defaultProfile.prepareSaveBytes(
            manifestPath,
            manifestBytes,
            &expectedSourceBytes,
            &expectedSourceExists))
    {
        QMessageBox::critical(
            this,
            tr("错误"),
            tr("无法读取现有 game_profile.ini；已取消创建以避免覆盖未知内容。"));
        return;
    }
    if (expectedSourceExists)
    {
        auto ret = QMessageBox::question(this,
            tr("确认"),
            tr("game_profile.ini 已存在，是否覆盖？"),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes)
        {
            return;
        }
    }

    DurableFileTransaction transaction(m_assetsBasePath);
    QString errorOrWarning;
    if (!transaction.addBytesWriteChecked(
            manifestPath,
            manifestBytes,
            expectedSourceExists,
            expectedSourceBytes,
            errorOrWarning) ||
        !transaction.commit(errorOrWarning))
    {
        QMessageBox::critical(
            this,
            tr("错误"),
            tr("创建默认 Manifest 失败：\n%1").arg(errorOrWarning));
        return;
    }

    refreshPackList(false);
    QString message = tr("已创建默认 Manifest：\n") + manifestPath;
    if (!errorOrWarning.isEmpty())
    {
        message += tr("\n\n创建完成，但清理旧事务文件时出现警告：\n") +
            errorOrWarning;
    }
    if (m_transactionRecoveryBlocked)
    {
        message += tr(
            "\n\n资源事务仍无法恢复，创建和保存功能已停用：\n%1")
            .arg(m_transactionRecoveryErrorText);
        QMessageBox::warning(
            this, tr("创建完成，但资源事务恢复失败"), message);
    }
    else
    {
        QMessageBox::information(this, tr("成功"), message);
    }
}

void ResourceProfileEditorWindow::onFieldChanged()
{
    if (m_updatingFromCode)
    {
        return;
    }
    const QObject* changedField = sender();
    if (changedField == m_profileVersionEdit)
    {
        m_invalidLoadedReleaseFields.remove(static_cast<int>(
            ModRelease::MetadataField::DisplayVersion));
    }
    else if (changedField == m_releaseDateEdit)
    {
        m_invalidLoadedReleaseFields.remove(static_cast<int>(
            ModRelease::MetadataField::ReleaseDate));
    }
    else if (changedField == m_minimumEngineVersionEdit)
    {
        m_invalidLoadedReleaseFields.remove(static_cast<int>(
            ModRelease::MetadataField::MinimumEngineVersion));
    }
    else if (changedField == m_releaseCoverEdit)
    {
        m_invalidLoadedReleaseFields.remove(static_cast<int>(
            ModRelease::MetadataField::CoverPath));
    }
    else if (changedField == m_releaseDescriptionFileEdit)
    {
        m_invalidLoadedReleaseFields.remove(static_cast<int>(
            ModRelease::MetadataField::DescriptionFilePath));
    }
    markModified();
}

void ResourceProfileEditorWindow::markModified()
{
    if (!m_updatingFromCode)
    {
        m_hasUnsavedChanges = true;
    }
}

bool ResourceProfileEditorWindow::confirmSaveIfModified()
{
    if (!m_hasUnsavedChanges)
    {
        return true;
    }

    auto ret = QMessageBox::question(this,
        tr("未保存的修改"),
        tr("当前资源包有未保存的修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (ret == QMessageBox::Yes)
    {
        onSave();
        return !m_hasUnsavedChanges;
    }
    else if (ret == QMessageBox::No)
    {
        m_hasUnsavedChanges = false;
        return true;
    }
    return false;
}
