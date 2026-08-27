#include "MagicEditorWindow.h"

#include "FilePickerHelper.h"
#include "MagicRangePreview.h"
#include "MpcPreviewLabel.h"
#include "../core/AssetPreviewLoader.h"
#include "../core/EditorAssetPath.h"

#include <QAction>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{
class MagicSnapshotCommand final : public QUndoCommand
{
public:
    MagicSnapshotCommand(
        QByteArray before,
        QByteArray after,
        std::function<void(const QByteArray&)> apply,
        const QString& description)
        : beforeBytes(std::move(before))
        , afterBytes(std::move(after))
        , applyBytes(std::move(apply))
    {
        setText(description);
    }

    void undo() override { applyBytes(beforeBytes); }
    void redo() override { applyBytes(afterBytes); }

private:
    QByteArray beforeBytes;
    QByteArray afterBytes;
    std::function<void(const QByteArray&)> applyBytes;
};

struct LevelColumn
{
    MagicIntegerField field;
    const char* objectName;
};

const LevelColumn levelColumns[] = {
    {MagicIntegerField::Effect, "effect"},
    {MagicIntegerField::LifeCost, "lifeCost"},
    {MagicIntegerField::ThewCost, "thewCost"},
    {MagicIntegerField::ManaCost, "manaCost"},
    {MagicIntegerField::LevelUpExperience, "levelUpExperience"},
    {MagicIntegerField::AttackRadius, "attackRadius"}
};

QString replacePackageExtension(const QString& path, const QString& extension)
{
    const int slash = std::max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    const int dot = path.lastIndexOf('.');
    return dot > slash ? path.left(dot) + extension : path + extension;
}

QString magicTextFieldObjectSuffix(MagicTextField field)
{
    switch (field)
    {
    case MagicTextField::Name: return QStringLiteral("Name");
    case MagicTextField::Introduction: return QStringLiteral("Introduction");
    case MagicTextField::Image: return QStringLiteral("Image");
    case MagicTextField::Icon: return QStringLiteral("Icon");
    case MagicTextField::FlyingImage: return QStringLiteral("FlyingImage");
    case MagicTextField::FlyingSound: return QStringLiteral("FlyingSound");
    case MagicTextField::VanishImage: return QStringLiteral("VanishImage");
    case MagicTextField::VanishSound: return QStringLiteral("VanishSound");
    case MagicTextField::SuperModeImage: return QStringLiteral("SuperModeImage");
    case MagicTextField::SuperModeSound: return QStringLiteral("SuperModeSound");
    case MagicTextField::ActionFile: return QStringLiteral("ActionFile");
    case MagicTextField::UseActionFile: return QStringLiteral("UseActionFile");
    case MagicTextField::AttackFile: return QStringLiteral("AttackFile");
    }
    return QStringLiteral("Unknown");
}
}

MagicEditorWindow::MagicEditorWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("magicEditorWindow"));
    undoStack = new QUndoStack(this);
    undoStack->setObjectName(QStringLiteral("magicUndoStack"));
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    mediaPlayer->setAudioOutput(audioOutput);
    setupUi();
    setupActions();
    setupConnections();
    refreshFromDocument();
}

MagicEditorWindow::~MagicEditorWindow()
{
    if (undoStack)
    {
        disconnect(undoStack, nullptr, this, nullptr);
        undoStack->clear();
    }
    if (mediaPlayer)
        mediaPlayer->stop();
}

bool MagicEditorWindow::isMagicFilePath(const QString& filePath)
{
    QString normalized = QDir::fromNativeSeparators(
        EditorAssetPath::normalizedAbsolutePath(filePath));
    return normalized.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive) &&
        normalized.contains(QStringLiteral("/ini/magic/"), Qt::CaseInsensitive);
}

bool MagicEditorWindow::openFile(const QString& requestedPath)
{
    if (requestedPath.trimmed().isEmpty())
        return false;
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    if (!isMagicFilePath(normalized) ||
        (documentPathValidator &&
         !documentPathValidator(filePath, normalized)) ||
        !confirmSaveIfModified())
    {
        return false;
    }

    MagicDocument candidate;
    QString error;
    if (!candidate.openFile(normalized, &error))
    {
        QMessageBox::warning(
            this, tr("无法打开武功"),
            tr("无法读取武功文件：\n%1\n\n%2").arg(normalized, error));
        return false;
    }

    document = std::move(candidate);
    filePath = normalized;
    undoStack->clear();
    undoStack->setClean();
    refreshDocumentList();
    refreshFromDocument();
    emit documentStatesChanged();
    return true;
}

bool MagicEditorWindow::saveFile()
{
    if (filePath.isEmpty())
        return false;
    commitPendingTextEditors();
    QString error;
    if (!document.saveFile(filePath, &error))
    {
        QMessageBox::warning(
            this, tr("保存失败"),
            tr("无法保存武功文件：\n%1\n\n%2").arg(filePath, error));
        return false;
    }
    undoStack->setClean();
    updateWindowTitle();
    emit documentStatesChanged();
    return true;
}

bool MagicEditorWindow::saveAsFile(const QString& requestedPath)
{
    if (requestedPath.trimmed().isEmpty())
        return false;
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    if (documentPathValidator &&
        !documentPathValidator(filePath, normalized))
    {
        return false;
    }
    const QString previousPath = filePath;
    filePath = normalized;
    if (!saveFile())
    {
        filePath = previousPath;
        return false;
    }
    refreshDocumentList();
    return true;
}

bool MagicEditorWindow::hasUnsavedChanges() const
{
    return !undoStack->isClean();
}

QString MagicEditorWindow::currentFilePath() const
{
    return filePath;
}

QString MagicEditorWindow::displayName() const
{
    const QString name = document.text(MagicTextField::Name).trimmed();
    return name.isEmpty() ? QFileInfo(filePath).completeBaseName() : name;
}

void MagicEditorWindow::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path.isEmpty()
        ? QString()
        : EditorAssetPath::normalizedAbsolutePath(path);
    refreshDocumentList();
    refreshResourcePreviews();
}

void MagicEditorWindow::setDocumentPathValidator(
    std::function<bool(const QString&, const QString&)> validator)
{
    documentPathValidator = std::move(validator);
}

QList<ProjectDocumentState>
MagicEditorWindow::currentProjectDocuments() const
{
    if (filePath.isEmpty())
        return {};
    return {{filePath, ProjectDocumentType::Magic, hasUnsavedChanges()}};
}

DesktopRunDocumentSnapshot
MagicEditorWindow::desktopRunDocumentSnapshot() const
{
    DesktopRunDocumentSnapshot snapshot;
    snapshot.filePath = filePath;
    snapshot.type = ProjectDocumentType::Magic;
    snapshot.dirty = hasUnsavedChanges();
    snapshot.includeInOverlay = snapshot.dirty;
    snapshot.serializationSupported = !filePath.isEmpty();
    snapshot.bytes = document.serializedBytes();
    if (!snapshot.serializationSupported)
        snapshot.diagnosticCode = QStringLiteral("magic.document.unsaved");
    return snapshot;
}

ClosePlan MagicEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    if (!hasUnsavedChanges())
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        const_cast<MagicEditorWindow*>(this),
        tr("保存更改"),
        tr("武功“%1”已修改，是否保存？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    plan.decisions.append(
        choice == QMessageBox::Save ? CloseDecision::Save :
        choice == QMessageBox::Discard ? CloseDecision::Discard :
        CloseDecision::Cancelled);
    return plan;
}

bool MagicEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    return plan.decisions.size() == 1 && !plan.isCancelled() &&
        (plan.decisions.front() != CloseDecision::Save || saveFile());
}

void MagicEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() == 1 && !plan.isCancelled())
        allowPreparedClose();
}

AssetsPathSwitchParticipant::Decision
MagicEditorWindow::prepareAssetsPathSwitch(const QString& path) const
{
    Q_UNUSED(path);
    if (!hasUnsavedChanges())
        return Decision::Ready;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        const_cast<MagicEditorWindow*>(this),
        tr("切换项目资源"),
        tr("当前武功已修改。切换项目资源前是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return choice == QMessageBox::Save ? Decision::Save :
        choice == QMessageBox::Discard ? Decision::Discard :
        Decision::Cancelled;
}

bool MagicEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    return decision != Decision::Cancelled &&
        (decision != Decision::Save || saveFile());
}

void MagicEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    setAssetsBasePath(path);
}

QString MagicEditorWindow::currentAssetsPath() const
{
    return assetsBasePath;
}

void MagicEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose() || confirmSaveIfModified())
    {
        mediaPlayer->stop();
        emit documentClosed();
        event->accept();
        return;
    }
    event->ignore();
}

void MagicEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateDynamicUi();
    QWidget::changeEvent(event);
}

bool MagicEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == introductionEdit && event->type() == QEvent::FocusOut &&
        !refreshing)
    {
        pushTextChange(MagicTextField::Introduction,
                       introductionEdit->toPlainText(),
                       tr("修改武功说明"));
    }
    return QWidget::eventFilter(watched, event);
}

void MagicEditorWindow::setupUi()
{
    auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("magicToolBar"));
    rootLayout->addWidget(toolBar);

    auto splitter = new QSplitter(this);
    splitter->setObjectName(QStringLiteral("magicMainSplitter"));
    rootLayout->addWidget(splitter, 1);

    auto listPanel = new QWidget(splitter);
    listPanel->setMinimumWidth(220);
    listPanel->setMaximumWidth(380);
    auto listLayout = new QVBoxLayout(listPanel);
    auto listTitle = new QLabel(tr("武功列表"), listPanel);
    listTitle->setObjectName(QStringLiteral("magicListTitle"));
    QFont titleFont = listTitle->font();
    titleFont.setBold(true);
    listTitle->setFont(titleFont);
    listLayout->addWidget(listTitle);
    searchEdit = new QLineEdit(listPanel);
    searchEdit->setObjectName(QStringLiteral("magicSearchEdit"));
    searchEdit->setPlaceholderText(tr("搜索名称或文件名"));
    searchEdit->setClearButtonEnabled(true);
    listLayout->addWidget(searchEdit);
    documentList = new QListWidget(listPanel);
    documentList->setObjectName(QStringLiteral("magicDocumentList"));
    listLayout->addWidget(documentList, 1);
    auto listHint = new QLabel(
        tr("双击或按 Enter 打开；已打开的武功会切回原标签。"), listPanel);
    listHint->setObjectName(QStringLiteral("magicListHint"));
    listHint->setWordWrap(true);
    listLayout->addWidget(listHint);

    auto editorScroll = new QScrollArea(splitter);
    editorScroll->setObjectName(QStringLiteral("magicEditorScroll"));
    editorScroll->setWidgetResizable(true);
    auto editorPanel = new QWidget(editorScroll);
    auto editorLayout = new QVBoxLayout(editorPanel);

    fileSummaryLabel = new QLabel(editorPanel);
    fileSummaryLabel->setObjectName(QStringLiteral("magicFileSummaryLabel"));
    fileSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    editorLayout->addWidget(fileSummaryLabel);

    auto tabs = new QTabWidget(editorPanel);
    tabs->setObjectName(QStringLiteral("magicEditorTabs"));
    tabs->addTab(createOverviewPage(), tr("概览与效果"));
    tabs->addTab(createLevelsPage(), tr("等级参数"));
    editorLayout->addWidget(tabs, 1);

    preservationLabel = new QLabel(editorPanel);
    preservationLabel->setObjectName(QStringLiteral("magicPreservationLabel"));
    preservationLabel->setWordWrap(true);
    editorLayout->addWidget(preservationLabel);
    editorScroll->setWidget(editorPanel);

    splitter->addWidget(listPanel);
    splitter->addWidget(editorScroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 900});
}

void MagicEditorWindow::setupActions()
{
    saveAction = toolBar->addAction(tr("保存"));
    saveAction->setObjectName(QStringLiteral("magicSaveAction"));
    saveAction->setShortcut(QKeySequence::Save);
    toolBar->addSeparator();
    undoAction = undoStack->createUndoAction(this, tr("撤销"));
    undoAction->setObjectName(QStringLiteral("magicUndoAction"));
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction = undoStack->createRedoAction(this, tr("重做"));
    redoAction->setObjectName(QStringLiteral("magicRedoAction"));
    redoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    toolBar->addAction(undoAction);
    toolBar->addAction(redoAction);
    toolBar->addSeparator();
    playtestAction = toolBar->addAction(tr("试玩当前武功"));
    playtestAction->setObjectName(QStringLiteral("magicPlaytestAction"));
    playtestAction->setToolTip(
        tr("只带入当前武功和所选等级，并以战斗状态进入试玩场景"));
}

void MagicEditorWindow::setupConnections()
{
    connect(saveAction, &QAction::triggered,
        this,
        [this]()
        {
            commitPendingTextEditors();
            saveFile();
        });
    connect(playtestAction, &QAction::triggered,
        this,
        [this]()
        {
            commitPendingTextEditors();
            emit playtestRequested();
        });
    connect(searchEdit, &QLineEdit::textChanged,
            this, &MagicEditorWindow::filterDocumentList);
    connect(documentList, &QListWidget::itemActivated, this,
        [this](QListWidgetItem* item)
        {
            if (item)
            {
                commitPendingTextEditors();
                emit openMagicFileRequested(item->data(Qt::UserRole).toString());
            }
        });
    connect(undoStack, &QUndoStack::indexChanged, this,
        [this]()
        {
            refreshFromDocument();
            emit documentStatesChanged();
        });
    connect(undoStack, &QUndoStack::cleanChanged, this,
        [this]()
        {
            updateWindowTitle();
            updateActionStates();
            emit documentStatesChanged();
        });
}

QWidget* MagicEditorWindow::createOverviewPage()
{
    auto page = new QWidget(this);
    page->setObjectName(QStringLiteral("magicOverviewPage"));
    auto layout = new QVBoxLayout(page);

    auto identityGroup = new QGroupBox(tr("武功信息"), page);
    identityGroup->setObjectName(QStringLiteral("magicIdentityGroup"));
    auto form = new QFormLayout(identityGroup);
    nameEdit = new QLineEdit(identityGroup);
    nameEdit->setObjectName(QStringLiteral("magicNameEdit"));
    form->addRow(tr("名称"), nameEdit);
    if (QWidget* label = form->labelForField(nameEdit))
        label->setObjectName(QStringLiteral("magicNameLabel"));
    introductionEdit = new QPlainTextEdit(identityGroup);
    introductionEdit->setObjectName(QStringLiteral("magicIntroductionEdit"));
    introductionEdit->setMaximumHeight(105);
    introductionEdit->installEventFilter(this);
    form->addRow(tr("说明"), introductionEdit);
    if (QWidget* label = form->labelForField(introductionEdit))
        label->setObjectName(QStringLiteral("magicIntroductionLabel"));
    layout->addWidget(identityGroup);

    auto effectGroup = new QGroupBox(tr("作用方式与范围"), page);
    effectGroup->setObjectName(QStringLiteral("magicEffectGroup"));
    auto effectLayout = new QHBoxLayout(effectGroup);
    auto effectForm = new QFormLayout();
    moveKindCombo = new QComboBox(effectGroup);
    moveKindCombo->setObjectName(QStringLiteral("magicMoveKindCombo"));
    regionCombo = new QComboBox(effectGroup);
    regionCombo->setObjectName(QStringLiteral("magicRegionCombo"));
    previewLevelSpin = new QSpinBox(effectGroup);
    previewLevelSpin->setObjectName(QStringLiteral("magicPreviewLevelSpin"));
    previewLevelSpin->setRange(1, MagicDocument::MaximumLevel);
    previewLevelSpin->setValue(1);
    effectForm->addRow(tr("当前等级"), previewLevelSpin);
    if (QWidget* label = effectForm->labelForField(previewLevelSpin))
        label->setObjectName(QStringLiteral("magicPreviewLevelLabel"));
    effectForm->addRow(tr("作用方式"), moveKindCombo);
    if (QWidget* label = effectForm->labelForField(moveKindCombo))
        label->setObjectName(QStringLiteral("magicMoveKindLabel"));
    effectForm->addRow(tr("区域形状"), regionCombo);
    if (QWidget* label = effectForm->labelForField(regionCombo))
        label->setObjectName(QStringLiteral("magicRegionLabel"));
    selectedLevelEffectLabel = new QLabel(effectGroup);
    selectedLevelEffectLabel->setObjectName(
        QStringLiteral("magicSelectedLevelEffectLabel"));
    effectForm->addRow(tr("本级效果值"), selectedLevelEffectLabel);
    if (QWidget* label = effectForm->labelForField(selectedLevelEffectLabel))
        label->setObjectName(QStringLiteral("magicSelectedLevelEffectTitle"));
    auto previewHint = new QLabel(
        tr("范围图是便于理解的示意；最终速度、碰撞和连锁效果以试玩为准。"),
        effectGroup);
    previewHint->setObjectName(QStringLiteral("magicRangeHint"));
    previewHint->setWordWrap(true);
    effectForm->addRow(previewHint);
    effectLayout->addLayout(effectForm, 1);
    rangePreview = new MagicRangePreview(effectGroup);
    effectLayout->addWidget(rangePreview, 1);
    layout->addWidget(effectGroup);

    auto appearanceGroup = new QGroupBox(tr("当前等级的速度与表现"), page);
    appearanceGroup->setObjectName(QStringLiteral("magicAppearanceGroup"));
    auto appearanceLayout = new QGridLayout(appearanceGroup);
    auto movementForm = new QFormLayout();
    auto visualForm = new QFormLayout();
    auto createLevelSpin = [appearanceGroup](const QString& objectName)
    {
        auto spin = new QSpinBox(appearanceGroup);
        spin->setObjectName(objectName);
        spin->setRange(0, 1000000);
        return spin;
    };
    specialKindCombo = new QComboBox(appearanceGroup);
    specialKindCombo->setObjectName(QStringLiteral("magicSpecialKindCombo"));
    speedSpin = createLevelSpin(QStringLiteral("magicSpeedSpin"));
    waitFrameSpin = createLevelSpin(QStringLiteral("magicWaitFrameSpin"));
    lifeFrameSpin = createLevelSpin(QStringLiteral("magicLifeFrameSpin"));
    alphaBlendCombo = new QComboBox(appearanceGroup);
    alphaBlendCombo->setObjectName(QStringLiteral("magicAlphaBlendCombo"));
    flyingLumSpin = createLevelSpin(QStringLiteral("magicFlyingLumSpin"));
    vanishLumSpin = createLevelSpin(QStringLiteral("magicVanishLumSpin"));
    movementForm->addRow(tr("附加效果"), specialKindCombo);
    movementForm->addRow(tr("飞行速度"), speedSpin);
    movementForm->addRow(tr("延迟出现（帧）"), waitFrameSpin);
    movementForm->addRow(tr("持续时间（帧）"), lifeFrameSpin);
    visualForm->addRow(tr("画面混合"), alphaBlendCombo);
    visualForm->addRow(tr("飞行亮度"), flyingLumSpin);
    visualForm->addRow(tr("命中亮度"), vanishLumSpin);
    const struct AppearanceLabel
    {
        QWidget* field;
        const char* objectName;
    } appearanceLabels[] = {
        {specialKindCombo, "magicSpecialKindLabel"},
        {speedSpin, "magicSpeedLabel"},
        {waitFrameSpin, "magicWaitFrameLabel"},
        {lifeFrameSpin, "magicLifeFrameLabel"},
        {alphaBlendCombo, "magicAlphaBlendLabel"},
        {flyingLumSpin, "magicFlyingLumLabel"},
        {vanishLumSpin, "magicVanishLumLabel"}
    };
    for (const AppearanceLabel& entry : appearanceLabels)
    {
        QFormLayout* owner = entry.field == specialKindCombo ||
                entry.field == speedSpin || entry.field == waitFrameSpin ||
                entry.field == lifeFrameSpin
            ? movementForm : visualForm;
        if (QWidget* label = owner->labelForField(entry.field))
            label->setObjectName(QString::fromLatin1(entry.objectName));
    }
    appearanceLayout->addLayout(movementForm, 0, 0);
    appearanceLayout->addLayout(visualForm, 0, 1);
    layout->addWidget(appearanceGroup);

    auto resourcesGroup = new QGroupBox(tr("图片与声音"), page);
    resourcesGroup->setObjectName(QStringLiteral("magicResourcesGroup"));
    auto resourcesLayout = new QGridLayout(resourcesGroup);
    resourcesLayout->addWidget(createResourceCard(
        tr("技能展示"), MagicTextField::Image, QStringLiteral("magic"),
        imagePreview, imageEdit), 0, 0);
    resourcesLayout->addWidget(createResourceCard(
        tr("武功图标"), MagicTextField::Icon, QStringLiteral("magic"),
        iconPreview, iconEdit), 0, 1);
    resourcesLayout->addWidget(createResourceCard(
        tr("飞行特效"), MagicTextField::FlyingImage, QStringLiteral("effect"),
        flyingImagePreview, flyingImageEdit), 1, 0);
    resourcesLayout->addWidget(createResourceCard(
        tr("命中特效"), MagicTextField::VanishImage, QStringLiteral("effect"),
        vanishImagePreview, vanishImageEdit), 1, 1);
    resourcesLayout->addWidget(createSoundRow(
        MagicTextField::FlyingSound, tr("施放声音"), flyingSoundEdit), 3, 0);
    resourcesLayout->addWidget(createSoundRow(
        MagicTextField::VanishSound, tr("命中声音"), vanishSoundEdit), 3, 1);
    resourcesLayout->addWidget(createResourceCard(
        tr("全屏特效"), MagicTextField::SuperModeImage, QStringLiteral("effect"),
        superModeImagePreview, superModeImageEdit), 2, 0);
    resourcesLayout->addWidget(createSoundRow(
        MagicTextField::SuperModeSound, tr("全屏声音"), superModeSoundEdit), 2, 1);
    layout->addWidget(resourcesGroup);

    auto actionGroup = new QGroupBox(tr("施展动作与修炼攻击"), page);
    actionGroup->setObjectName(QStringLiteral("magicActionGroup"));
    auto actionLayout = new QGridLayout(actionGroup);
    actionLayout->addWidget(createResourceCard(
        tr("关联攻击时的动作"), MagicTextField::ActionFile,
        QStringLiteral("character"), actionFilePreview, actionFileEdit), 0, 0);
    actionLayout->addWidget(createResourceCard(
        tr("施展动作（优先）"), MagicTextField::UseActionFile,
        QStringLiteral("character"), useActionFilePreview, useActionFileEdit), 0, 1);
    auto attackPanel = new QGroupBox(tr("修炼时的攻击效果"), actionGroup);
    attackPanel->setObjectName(QStringLiteral("magicAttackFileGroup"));
    attackPanel->setProperty("magicTextField",
                             static_cast<int>(MagicTextField::AttackFile));
    auto attackLayout = new QVBoxLayout(attackPanel);
    auto attackHint = new QLabel(
        tr("角色修炼此武功后，普通攻击会附带这里选择的武功效果。"),
        attackPanel);
    attackHint->setObjectName(QStringLiteral("magicAttackFileHint"));
    attackHint->setWordWrap(true);
    attackLayout->addWidget(attackHint);
    auto attackRow = new QHBoxLayout();
    attackFileCombo = new QComboBox(attackPanel);
    attackFileCombo->setObjectName(QStringLiteral("magicAttackFileCombo"));
    attackFileCombo->setEditable(true);
    attackFileCombo->setInsertPolicy(QComboBox::NoInsert);
    attackFileEdit = attackFileCombo->lineEdit();
    attackFileEdit->setObjectName(QStringLiteral("magicAttackFileEdit"));
    attackFileEdit->setProperty("magicTextField",
                                static_cast<int>(MagicTextField::AttackFile));
    auto openAttackButton = new QPushButton(tr("打开关联武功"), attackPanel);
    openAttackButton->setObjectName(QStringLiteral("magicOpenAttackFileButton"));
    attackRow->addWidget(attackFileCombo, 1);
    attackRow->addWidget(openAttackButton);
    attackLayout->addLayout(attackRow);
    actionLayout->addWidget(attackPanel, 1, 0, 1, 2);
    layout->addWidget(actionGroup);
    layout->addStretch();

    connect(nameEdit, &QLineEdit::editingFinished, this,
        [this]()
        {
            pushTextChange(MagicTextField::Name, nameEdit->text(),
                           tr("修改武功名称"));
        });
    connect(moveKindCombo, &QComboBox::currentIndexChanged, this,
        [this](int)
        {
            if (!refreshing)
                pushIntegerChange(MagicIntegerField::MoveKind, selectedLevel(),
                                  moveKindCombo->currentData().toInt(),
                                  tr("修改第 %1 级作用方式").arg(selectedLevel()));
        });
    connect(regionCombo, &QComboBox::currentIndexChanged, this,
        [this](int)
        {
            if (!refreshing)
                pushIntegerChange(MagicIntegerField::Region, selectedLevel(),
                                  regionCombo->currentData().toInt(),
                                  tr("修改第 %1 级区域形状").arg(selectedLevel()));
        });
    connect(specialKindCombo, &QComboBox::currentIndexChanged, this,
        [this](int)
        {
            if (!refreshing)
            {
                pushIntegerChange(MagicIntegerField::SpecialKind, selectedLevel(),
                                  specialKindCombo->currentData().toInt(),
                                  tr("修改第 %1 级附加效果").arg(selectedLevel()));
            }
        });
    connect(alphaBlendCombo, &QComboBox::currentIndexChanged, this,
        [this](int)
        {
            if (!refreshing)
            {
                pushIntegerChange(MagicIntegerField::AlphaBlend, selectedLevel(),
                                  alphaBlendCombo->currentData().toInt(),
                                  tr("修改第 %1 级画面混合").arg(selectedLevel()));
            }
        });
    auto connectLevelSpin = [this](QSpinBox* spin, MagicIntegerField field,
                                   const QString& description)
    {
        connect(spin, &QSpinBox::valueChanged, this,
            [this, field, description](int value)
            {
                if (!refreshing)
                {
                    pushIntegerChange(field, selectedLevel(), value,
                                      description.arg(selectedLevel()));
                }
            });
    };
    connectLevelSpin(speedSpin, MagicIntegerField::Speed,
                     tr("修改第 %1 级飞行速度"));
    connectLevelSpin(waitFrameSpin, MagicIntegerField::WaitFrame,
                     tr("修改第 %1 级出现延迟"));
    connectLevelSpin(lifeFrameSpin, MagicIntegerField::LifeFrame,
                     tr("修改第 %1 级持续时间"));
    connectLevelSpin(flyingLumSpin, MagicIntegerField::FlyingLum,
                     tr("修改第 %1 级飞行亮度"));
    connectLevelSpin(vanishLumSpin, MagicIntegerField::VanishLum,
                     tr("修改第 %1 级命中亮度"));
    connect(attackFileEdit, &QLineEdit::editingFinished, this,
        [this]()
        {
            pushTextChange(MagicTextField::AttackFile, attackFileEdit->text(),
                           tr("修改修炼时的攻击效果"));
        });
    connect(attackFileCombo, &QComboBox::activated, this,
        [this](int)
        {
            pushTextChange(MagicTextField::AttackFile,
                           attackFileCombo->currentText(),
                           tr("修改修炼时的攻击效果"));
        });
    connect(openAttackButton, &QPushButton::clicked, this,
        [this]()
        {
            QString reference = QDir::fromNativeSeparators(
                attackFileCombo->currentText().trimmed());
            if (reference.startsWith(QStringLiteral("ini/magic/"),
                                     Qt::CaseInsensitive))
            {
                reference.remove(0, 10);
            }
            QString targetPath;
            if (reference.isEmpty() ||
                !EditorAssetPath::resolveLogicalResourcePath(
                    assetsBasePath, QStringLiteral("ini/magic/") + reference,
                    targetPath) || !QFileInfo(targetPath).isFile())
            {
                QMessageBox::information(
                    this, tr("无法打开"), tr("关联武功文件不存在或尚未选择。"));
                return;
            }
            commitPendingTextEditors();
            emit openMagicFileRequested(targetPath);
        });
    connect(previewLevelSpin, &QSpinBox::valueChanged, this,
        [this](int level)
        {
            if (levelTable && levelTable->currentRow() != level - 1)
            {
                const int column = std::max(1, levelTable->currentColumn());
                levelTable->setCurrentCell(level - 1, column);
            }
            else
            {
                refreshLevelEffectControls();
                refreshRangePreview();
            }
        });
    return page;
}

QWidget* MagicEditorWindow::createLevelsPage()
{
    auto page = new QWidget(this);
    page->setObjectName(QStringLiteral("magicLevelsPage"));
    auto layout = new QVBoxLayout(page);
    auto hint = new QLabel(
        tr("每行对应游戏中的一个武功等级。浅色斜体值表示该等级沿用前一级；直接修改即可为本级保存。"),
        page);
    hint->setObjectName(QStringLiteral("magicLevelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    levelTable = new QTableWidget(MagicDocument::MaximumLevel, 7, page);
    levelTable->setObjectName(QStringLiteral("magicLevelTable"));
    levelTable->setAlternatingRowColors(true);
    levelTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    levelTable->verticalHeader()->setVisible(false);
    levelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(levelTable, 1);
    connect(levelTable, &QTableWidget::itemChanged, this,
        [this](QTableWidgetItem* item)
        {
            if (refreshing || !item || item->column() <= 0)
                return;
            bool ok = false;
            const int value = item->text().toInt(&ok);
            if (!ok)
            {
                refreshLevelTable();
                return;
            }
            const int level = item->row() + 1;
            const LevelColumn& column = levelColumns[item->column() - 1];
            pushIntegerChange(column.field, level, value,
                              tr("修改第 %1 级参数").arg(level));
        });
    connect(levelTable, &QTableWidget::currentCellChanged, this,
        [this](int row, int, int, int)
        {
            if (previewLevelSpin && row >= 0 &&
                previewLevelSpin->value() != row + 1)
            {
                const QSignalBlocker blocker(previewLevelSpin);
                previewLevelSpin->setValue(row + 1);
            }
            refreshLevelEffectControls();
            refreshRangePreview();
        });
    return page;
}

QWidget* MagicEditorWindow::createResourceCard(
    const QString& title, MagicTextField field, const QString& category,
    MpcPreviewLabel*& preview, QLineEdit*& lineEdit)
{
    auto card = new QGroupBox(title, this);
    card->setObjectName(
        QStringLiteral("magic%1Group").arg(magicTextFieldObjectSuffix(field)));
    card->setProperty("magicTextField", static_cast<int>(field));
    auto layout = new QVBoxLayout(card);
    preview = new MpcPreviewLabel(card);
    preview->setObjectName(
        QStringLiteral("magic%1Preview").arg(magicTextFieldObjectSuffix(field)));
    preview->setMinimumSize(160, 120);
    preview->setMaximumHeight(180);
    preview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(preview, 1);
    auto row = new QHBoxLayout();
    lineEdit = new QLineEdit(card);
    lineEdit->setObjectName(
        QStringLiteral("magic%1Edit").arg(magicTextFieldObjectSuffix(field)));
    lineEdit->setProperty("magicTextField", static_cast<int>(field));
    auto browseButton = new QPushButton(tr("选择…"), card);
    browseButton->setObjectName(QStringLiteral("magicResourceBrowseButton"));
    row->addWidget(lineEdit, 1);
    row->addWidget(browseButton);
    layout->addLayout(row);
    connect(lineEdit, &QLineEdit::editingFinished, this,
        [this, field, lineEdit, title]()
        {
            pushTextChange(field, lineEdit->text(),
                           tr("修改%1").arg(title));
        });
    connect(browseButton, &QPushButton::clicked, this,
        [this, field, category]()
        {
            browseResource(field, category,
                           tr("图片文件 (*.mpc *.asf *.imp *.img *.png);;所有文件 (*.*)"));
        });
    return card;
}

QWidget* MagicEditorWindow::createSoundRow(
    MagicTextField field, const QString& label, QLineEdit*& lineEdit)
{
    auto panel = new QGroupBox(label, this);
    panel->setObjectName(
        QStringLiteral("magic%1Group").arg(magicTextFieldObjectSuffix(field)));
    panel->setProperty("magicTextField", static_cast<int>(field));
    auto layout = new QHBoxLayout(panel);
    lineEdit = new QLineEdit(panel);
    lineEdit->setObjectName(
        QStringLiteral("magic%1Edit").arg(magicTextFieldObjectSuffix(field)));
    lineEdit->setProperty("magicTextField", static_cast<int>(field));
    auto browseButton = new QPushButton(tr("选择…"), panel);
    auto playButton = new QPushButton(tr("试听"), panel);
    browseButton->setObjectName(QStringLiteral("magicSoundBrowseButton"));
    playButton->setObjectName(QStringLiteral("magicSoundPlayButton"));
    layout->addWidget(lineEdit, 1);
    layout->addWidget(browseButton);
    layout->addWidget(playButton);
    connect(lineEdit, &QLineEdit::editingFinished, this,
        [this, field, lineEdit, label]()
        {
            pushTextChange(field, lineEdit->text(),
                           tr("修改%1").arg(label));
        });
    connect(browseButton, &QPushButton::clicked, this,
        [this, field]()
        {
            browseResource(field, QStringLiteral("sound"),
                           tr("音频文件 (*.wav *.mp3 *.ogg);;所有文件 (*.*)"));
        });
    connect(playButton, &QPushButton::clicked, this,
        [this, field]() { playSound(field); });
    return panel;
}

bool MagicEditorWindow::loadDocumentBytes(const QByteArray& bytes)
{
    QString error;
    if (!document.load(bytes, &error))
        return false;
    refreshFromDocument();
    return true;
}

void MagicEditorWindow::pushTextChange(
    MagicTextField field, const QString& value, const QString& description)
{
    if (refreshing || filePath.isEmpty() || document.text(field) == value)
        return;
    const QByteArray before = document.serializedBytes();
    document.setText(field, value);
    const QByteArray after = document.serializedBytes();
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, description);
}

void MagicEditorWindow::pushIntegerChange(
    MagicIntegerField field, int level, int value, const QString& description)
{
    if (refreshing || filePath.isEmpty() ||
        (document.hasIntegerOverride(field, level) &&
         document.effectiveInteger(field, level) == value))
    {
        return;
    }
    const QByteArray before = document.serializedBytes();
    document.setInteger(field, level, value);
    const QByteArray after = document.serializedBytes();
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, description);
}

void MagicEditorWindow::pushSnapshotChange(
    const QByteArray& before, const QByteArray& after,
    const QString& description)
{
    if (before == after)
        return;
    undoStack->push(new MagicSnapshotCommand(
        before, after,
        [this](const QByteArray& bytes) { loadDocumentBytes(bytes); },
        description));
}

void MagicEditorWindow::commitPendingTextEditors()
{
    if (refreshing || filePath.isEmpty())
        return;

    const QByteArray before = document.serializedBytes();
    document.setText(MagicTextField::Name, nameEdit->text());
    document.setText(MagicTextField::Introduction,
                     introductionEdit->toPlainText());
    for (MagicTextField field : {
             MagicTextField::Image, MagicTextField::Icon,
             MagicTextField::FlyingImage, MagicTextField::VanishImage,
             MagicTextField::FlyingSound, MagicTextField::VanishSound,
             MagicTextField::SuperModeImage, MagicTextField::SuperModeSound,
             MagicTextField::ActionFile, MagicTextField::UseActionFile,
             MagicTextField::AttackFile})
    {
        if (QLineEdit* edit = lineEditForField(field))
            document.setText(field, edit->text());
    }
    const QByteArray after = document.serializedBytes();
    if (before == after)
        return;

    loadDocumentBytes(before);
    pushSnapshotChange(before, after, tr("修改武功内容"));
}

void MagicEditorWindow::refreshFromDocument()
{
    refreshing = true;
    const QSignalBlocker blockName(nameEdit);
    const QSignalBlocker blockIntro(introductionEdit);
    nameEdit->setText(document.text(MagicTextField::Name));
    introductionEdit->setPlainText(document.text(MagicTextField::Introduction));

    retranslateDynamicUi();

    for (MagicTextField field : {
             MagicTextField::Image, MagicTextField::Icon,
             MagicTextField::FlyingImage, MagicTextField::VanishImage,
             MagicTextField::FlyingSound, MagicTextField::VanishSound,
             MagicTextField::SuperModeImage, MagicTextField::SuperModeSound,
             MagicTextField::ActionFile, MagicTextField::UseActionFile,
             MagicTextField::AttackFile})
    {
        if (QLineEdit* edit = lineEditForField(field))
            edit->setText(document.text(field));
    }
    refreshing = false;
    refreshLevelTable();
    refreshLevelEffectControls();
    refreshRangePreview();
    refreshResourcePreviews();
    updateWindowTitle();
    updateActionStates();
    fileSummaryLabel->setText(
        filePath.isEmpty()
        ? tr("从左侧列表或项目树打开一个武功文件。")
        : tr("正在编辑：%1").arg(QDir::toNativeSeparators(filePath)));
    preservationLabel->setText(
        filePath.isEmpty()
        ? QString()
        : tr("原文件中还有 %1 项高级内容未在此显示；保存时会保持不变。")
              .arg(document.hiddenFieldCount()));
}

void MagicEditorWindow::refreshLevelTable()
{
    refreshing = true;
    const QSignalBlocker blocker(levelTable);
    levelTable->setHorizontalHeaderLabels({
        tr("等级"), tr("效果值"), tr("生命消耗"), tr("体力消耗"),
        tr("内力消耗"), tr("升级经验"), tr("攻击距离")});
    for (int level = 1; level <= MagicDocument::MaximumLevel; ++level)
    {
        auto levelItem = new QTableWidgetItem(QString::number(level));
        levelItem->setFlags(levelItem->flags() & ~Qt::ItemIsEditable);
        levelItem->setTextAlignment(Qt::AlignCenter);
        levelTable->setItem(level - 1, 0, levelItem);
        for (int column = 0; column < 6; ++column)
        {
            const MagicIntegerField field = levelColumns[column].field;
            auto item = new QTableWidgetItem(QString::number(
                document.effectiveInteger(field, level)));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (!document.hasIntegerOverride(field, level))
            {
                QFont font = item->font();
                font.setItalic(true);
                item->setFont(font);
                item->setForeground(palette().placeholderText());
                item->setToolTip(tr("本级未单独设置，当前显示沿用值；编辑后会为本级保存。"));
            }
            levelTable->setItem(level - 1, column + 1, item);
        }
    }
    if (levelTable->currentRow() < 0)
        levelTable->setCurrentCell(0, 1);
    refreshing = false;
}

void MagicEditorWindow::refreshRangePreview()
{
    if (!rangePreview)
        return;
    const int level = selectedLevel();
    if (selectedLevelEffectLabel)
    {
        selectedLevelEffectLabel->setText(QString::number(
            document.effectiveInteger(MagicIntegerField::Effect, level)));
    }
    rangePreview->setRange(
        document.effectiveInteger(MagicIntegerField::MoveKind, level),
        document.effectiveInteger(MagicIntegerField::Region, level),
        document.effectiveInteger(MagicIntegerField::AttackRadius, level),
        level);
}

void MagicEditorWindow::refreshLevelEffectControls()
{
    if (!moveKindCombo || !regionCombo)
        return;

    const bool previouslyRefreshing = refreshing;
    refreshing = true;
    const int level = selectedLevel();
    const int moveKind = document.effectiveInteger(
        MagicIntegerField::MoveKind, level);
    const int region = document.effectiveInteger(
        MagicIntegerField::Region, level);

    int moveIndex = moveKindCombo->findData(moveKind);
    if (moveIndex < 0)
    {
        moveKindCombo->addItem(
            tr("其它作用方式 (%1)").arg(moveKind), moveKind);
        moveIndex = moveKindCombo->count() - 1;
    }
    moveKindCombo->setCurrentIndex(moveIndex);

    int regionIndex = regionCombo->findData(region);
    if (regionIndex < 0)
    {
        regionCombo->addItem(tr("其它区域 (%1)").arg(region), region);
        regionIndex = regionCombo->count() - 1;
    }
    regionCombo->setCurrentIndex(regionIndex);
    regionCombo->setEnabled(moveKind == 11);

    if (specialKindCombo)
    {
        const int specialKind = document.effectiveInteger(
            MagicIntegerField::SpecialKind, level);
        specialKindCombo->clear();
        specialKindCombo->addItem(tr("无"), 0);
        if (moveKind == 13)
        {
            const std::pair<int, QString> selfEffects[] = {
                {1, tr("恢复生命")}, {2, tr("恢复体力")},
                {3, tr("减伤护盾")}, {4, tr("隐身（保持）")},
                {5, tr("隐身（攻击显形）")}, {6, tr("格挡伤害")},
                {7, tr("变身")}, {8, tr("清除异常状态")},
                {9, tr("更换轻功")}, {10, tr("定身")},
                {11, tr("吸收伤害护盾")}, {99, tr("临时改变属性")}
            };
            for (const auto& effect : selfEffects)
                specialKindCombo->addItem(effect.second, effect.first);
        }
        else
        {
            specialKindCombo->addItem(tr("冻结"), 1);
            specialKindCombo->addItem(tr("中毒"), 2);
            specialKindCombo->addItem(tr("石化"), 3);
        }
        int specialIndex = specialKindCombo->findData(specialKind);
        if (specialIndex < 0)
        {
            specialKindCombo->addItem(
                tr("其它附加效果 (%1)").arg(specialKind), specialKind);
            specialIndex = specialKindCombo->count() - 1;
        }
        specialKindCombo->setCurrentIndex(specialIndex);
    }
    if (alphaBlendCombo)
    {
        const int alphaBlend = document.effectiveInteger(
            MagicIntegerField::AlphaBlend, level);
        alphaBlendCombo->clear();
        alphaBlendCombo->addItem(tr("普通"), 0);
        alphaBlendCombo->addItem(tr("透明叠加"), 1);
        int alphaIndex = alphaBlendCombo->findData(alphaBlend);
        if (alphaIndex < 0)
        {
            alphaBlendCombo->addItem(
                tr("其它混合方式 (%1)").arg(alphaBlend), alphaBlend);
            alphaIndex = alphaBlendCombo->count() - 1;
        }
        alphaBlendCombo->setCurrentIndex(alphaIndex);
    }
    const struct LevelSpinValue
    {
        QSpinBox* spin;
        MagicIntegerField field;
    } spinValues[] = {
        {speedSpin, MagicIntegerField::Speed},
        {waitFrameSpin, MagicIntegerField::WaitFrame},
        {lifeFrameSpin, MagicIntegerField::LifeFrame},
        {flyingLumSpin, MagicIntegerField::FlyingLum},
        {vanishLumSpin, MagicIntegerField::VanishLum}
    };
    for (const LevelSpinValue& entry : spinValues)
    {
        if (entry.spin)
            entry.spin->setValue(document.effectiveInteger(entry.field, level));
    }
    refreshing = previouslyRefreshing;
}

void MagicEditorWindow::refreshResourcePreviews()
{
    const struct PreviewField
    {
        MagicTextField field;
        const char* category;
        bool animated;
    } fields[] = {
        {MagicTextField::Image, "magic", false},
        {MagicTextField::Icon, "magic", false},
        {MagicTextField::FlyingImage, "effect", true},
        {MagicTextField::VanishImage, "effect", true},
        {MagicTextField::SuperModeImage, "effect", false},
        {MagicTextField::ActionFile, "character", false},
        {MagicTextField::UseActionFile, "character", false}
    };
    for (const PreviewField& entry : fields)
    {
        MpcPreviewLabel* label = previewForField(entry.field);
        if (!label)
            continue;
        const QString reference = document.text(entry.field).trimmed();
        if (reference.isEmpty())
        {
            label->clearImage(tr("未设置"));
            continue;
        }
        bool loaded = false;
        for (const QString& candidate :
             imageCandidates(reference, QString::fromLatin1(entry.category)))
        {
            AssetAnimationPreviewData animation;
            AssetPreviewData preview;
            const bool previewLoaded = entry.animated
                ? AssetPreviewLoader::loadAnimation(
                      assetsBasePath, candidate, &animation)
                : AssetPreviewLoader::load(
                      assetsBasePath, candidate, &preview);
            if (entry.animated)
                preview = animation.preview;
            if (previewLoaded && preview.kind == AssetPreviewKind::Image)
            {
                if (entry.animated)
                {
                    label->setSourceAnimation(
                        animation.frames, preview.directions,
                        preview.intervalMilliseconds);
                }
                else
                {
                    label->setSourceImage(preview.image);
                }
                label->setToolTip(
                    tr("%1\n%2×%3，%4 帧")
                        .arg(candidate)
                        .arg(preview.imageWidth)
                        .arg(preview.imageHeight)
                        .arg(preview.frameCount));
                loaded = true;
                break;
            }
        }
        if (!loaded)
        {
            label->clearImage(tr("未找到预览\n可继续保存引用"));
            label->setToolTip(reference);
        }
    }
}

void MagicEditorWindow::refreshAttackFileChoices()
{
    if (!attackFileCombo)
        return;
    const QString currentText = attackFileCombo->currentText();
    const QSignalBlocker blocker(attackFileCombo);
    attackFileCombo->clear();
    attackFileCombo->addItem(QString());
    const QDir directory(QDir(assetsBasePath).filePath(QStringLiteral("ini/magic")));
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.ini")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : files)
        attackFileCombo->addItem(info.fileName());
    attackFileCombo->setCurrentText(currentText);
}

void MagicEditorWindow::refreshDocumentList()
{
    const QString selectedPath = filePath;
    documentList->clear();
    const QDir directory(QDir(assetsBasePath).filePath(QStringLiteral("ini/magic")));
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.ini")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : files)
    {
        MagicDocument candidate;
        QString name;
        if (candidate.openFile(info.absoluteFilePath()))
            name = candidate.text(MagicTextField::Name).trimmed();
        auto item = new QListWidgetItem(
            name.isEmpty()
            ? info.completeBaseName()
            : tr("%1\n%2").arg(name, info.fileName()),
            documentList);
        item->setData(Qt::UserRole,
                      EditorAssetPath::normalizedAbsolutePath(info.absoluteFilePath()));
        item->setToolTip(info.absoluteFilePath());
        if (ProjectDocumentRegistry::documentPathKey(info.absoluteFilePath()) ==
            ProjectDocumentRegistry::documentPathKey(selectedPath))
        {
            documentList->setCurrentItem(item);
        }
    }
    refreshAttackFileChoices();
    filterDocumentList();
}

void MagicEditorWindow::filterDocumentList()
{
    const QString filter = searchEdit->text().trimmed();
    for (int index = 0; index < documentList->count(); ++index)
    {
        QListWidgetItem* item = documentList->item(index);
        item->setHidden(!filter.isEmpty() &&
            !item->text().contains(filter, Qt::CaseInsensitive) &&
            !item->data(Qt::UserRole).toString().contains(filter, Qt::CaseInsensitive));
    }
}

void MagicEditorWindow::updateWindowTitle()
{
    const QString title = filePath.isEmpty()
        ? tr("武功与特效编辑器")
        : tr("武功 - %1%2")
              .arg(displayName(), hasUnsavedChanges() ? QStringLiteral("*") : QString());
    setWindowTitle(title);
}

void MagicEditorWindow::updateActionStates()
{
    const bool opened = !filePath.isEmpty();
    saveAction->setEnabled(opened && hasUnsavedChanges());
    playtestAction->setEnabled(opened);
}

bool MagicEditorWindow::confirmSaveIfModified()
{
    commitPendingTextEditors();
    if (!hasUnsavedChanges())
        return true;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("保存更改"),
        tr("武功“%1”已修改，是否保存？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel)
        return false;
    return choice != QMessageBox::Save || saveFile();
}

void MagicEditorWindow::browseResource(
    MagicTextField field, const QString& category, const QString& filter)
{
    if (assetsBasePath.isEmpty())
        return;
    QStringList folders;
    if (category == QStringLiteral("sound"))
    {
        folders = {QStringLiteral("sound")};
    }
    else
    {
        folders = {QStringLiteral("asf/%1").arg(category),
                   QStringLiteral("mpc/%1").arg(category)};
    }
    QString initialDirectory;
    for (const QString& folder : folders)
    {
        const QString candidate = QDir(assetsBasePath).filePath(folder);
        if (QDir(candidate).exists())
        {
            initialDirectory = candidate;
            break;
        }
    }
    const QString selected = QFileDialog::getOpenFileName(
        this, tr("选择资源"), initialDirectory, filter);
    if (selected.isEmpty())
        return;
    QString reference;
    QStringList absoluteFolders;
    for (const QString& folder : folders)
        absoluteFolders.append(QDir(assetsBasePath).filePath(folder));
    if (!FilePickerHelper::makeResourceReference(
            assetsBasePath, absoluteFolders, selected, reference))
    {
        QMessageBox::warning(this, tr("无法选择资源"),
                             tr("请选择当前项目对应资源目录中的文件。"));
        return;
    }
    pushTextChange(field, reference, tr("修改资源引用"));
}

void MagicEditorWindow::playSound(MagicTextField field)
{
    const QString path = resolveSoundPath(document.text(field));
    if (path.isEmpty())
    {
        QMessageBox::information(this, tr("无法试听"),
                                 tr("当前声音引用未设置或文件不存在。"));
        return;
    }
    mediaPlayer->stop();
    mediaPlayer->setSource(QUrl::fromLocalFile(path));
    mediaPlayer->play();
}

QStringList MagicEditorWindow::imageCandidates(
    const QString& reference, const QString& category) const
{
    QString normalized = QDir::fromNativeSeparators(reference.trimmed());
    if (normalized.isEmpty())
        return {};
    QStringList candidates;
    auto append = [&candidates](const QString& candidate)
    {
        if (!candidate.isEmpty() && !candidates.contains(candidate, Qt::CaseInsensitive))
            candidates.append(candidate);
    };
    if (normalized.startsWith(QStringLiteral("asf/"), Qt::CaseInsensitive) ||
        normalized.startsWith(QStringLiteral("mpc/"), Qt::CaseInsensitive))
    {
        append(normalized);
    }
    else
    {
        const QString categoryPrefix = category + QLatin1Char('/');
        if (normalized.startsWith(categoryPrefix, Qt::CaseInsensitive))
            normalized.remove(0, categoryPrefix.size());
        append(QStringLiteral("asf/%1/%2").arg(category, normalized));
        append(QStringLiteral("mpc/%1/%2").arg(category, normalized));
    }
    const QStringList originals = candidates;
    for (const QString& candidate : originals)
    {
        if (candidate.startsWith(QStringLiteral("asf/"), Qt::CaseInsensitive))
        {
            append(replacePackageExtension(
                QStringLiteral("mpc/") + candidate.mid(4), QStringLiteral(".mpc")));
        }
        else if (candidate.startsWith(QStringLiteral("mpc/"), Qt::CaseInsensitive))
        {
            append(replacePackageExtension(
                QStringLiteral("asf/") + candidate.mid(4), QStringLiteral(".asf")));
        }
    }
    return candidates;
}

QString MagicEditorWindow::resolveSoundPath(const QString& reference) const
{
    QString normalized = QDir::fromNativeSeparators(reference.trimmed());
    if (normalized.isEmpty())
        return {};
    if (normalized.startsWith(QStringLiteral("sound/"), Qt::CaseInsensitive))
        normalized.remove(0, 6);
    QString absolutePath;
    if (!EditorAssetPath::resolveLogicalResourcePath(
            assetsBasePath, QStringLiteral("sound/") + normalized,
            absolutePath))
    {
        return {};
    }
    if (!QFileInfo(absolutePath).isFile() && QFileInfo(normalized).suffix().isEmpty())
    {
        EditorAssetPath::resolveLogicalResourcePath(
            assetsBasePath, QStringLiteral("sound/") + normalized +
                QStringLiteral(".wav"), absolutePath);
    }
    return QFileInfo(absolutePath).isFile() ? absolutePath : QString();
}

MpcPreviewLabel* MagicEditorWindow::previewForField(MagicTextField field) const
{
    switch (field)
    {
    case MagicTextField::Image: return imagePreview;
    case MagicTextField::Icon: return iconPreview;
    case MagicTextField::FlyingImage: return flyingImagePreview;
    case MagicTextField::VanishImage: return vanishImagePreview;
    case MagicTextField::SuperModeImage: return superModeImagePreview;
    case MagicTextField::ActionFile: return actionFilePreview;
    case MagicTextField::UseActionFile: return useActionFilePreview;
    default: return nullptr;
    }
}

QLineEdit* MagicEditorWindow::lineEditForField(MagicTextField field) const
{
    switch (field)
    {
    case MagicTextField::Image: return imageEdit;
    case MagicTextField::Icon: return iconEdit;
    case MagicTextField::FlyingImage: return flyingImageEdit;
    case MagicTextField::FlyingSound: return flyingSoundEdit;
    case MagicTextField::VanishImage: return vanishImageEdit;
    case MagicTextField::VanishSound: return vanishSoundEdit;
    case MagicTextField::SuperModeImage: return superModeImageEdit;
    case MagicTextField::SuperModeSound: return superModeSoundEdit;
    case MagicTextField::ActionFile: return actionFileEdit;
    case MagicTextField::UseActionFile: return useActionFileEdit;
    case MagicTextField::AttackFile: return attackFileEdit;
    default: return nullptr;
    }
}

int MagicEditorWindow::selectedLevel() const
{
    return previewLevelSpin ? previewLevelSpin->value() : 1;
}

void MagicEditorWindow::retranslateDynamicUi()
{
    const bool previouslyRefreshing = refreshing;
    refreshing = true;
    const int currentMoveKind = moveKindCombo ? moveKindCombo->currentData().toInt() : 1;
    const int currentRegion = regionCombo ? regionCombo->currentData().toInt() : 0;
    if (moveKindCombo)
    {
        moveKindCombo->clear();
        const std::pair<int, QString> movements[] = {
            {1, tr("目标点")}, {2, tr("飞行")}, {3, tr("连续飞行")},
            {4, tr("环形")}, {5, tr("心形环绕")}, {6, tr("螺旋环绕")},
            {7, tr("扇形")}, {8, tr("随机扇形")}, {9, tr("直线")},
            {10, tr("直线移动")}, {11, tr("区域")}, {13, tr("对自身")},
            {15, tr("全屏")}, {16, tr("追踪目标")}, {17, tr("投掷")},
            {19, tr("轨迹")}, {20, tr("位移")}, {21, tr("控制")},
            {22, tr("召唤")}, {23, tr("时间停止")}, {24, tr("V 形移动")}
        };
        for (const auto& movement : movements)
            moveKindCombo->addItem(movement.second, movement.first);
        const int index = moveKindCombo->findData(currentMoveKind);
        if (index >= 0)
            moveKindCombo->setCurrentIndex(index);
    }
    if (regionCombo)
    {
        regionCombo->clear();
        regionCombo->addItem(tr("未指定"), 0);
        regionCombo->addItem(tr("方形"), 1);
        regionCombo->addItem(tr("十字"), 2);
        regionCombo->addItem(tr("波浪"), 3);
        regionCombo->addItem(tr("三角"), 4);
        regionCombo->addItem(tr("V 形"), 5);
        regionCombo->addItem(tr("自定义区域文件"), 6);
        const int index = regionCombo->findData(currentRegion);
        if (index >= 0)
            regionCombo->setCurrentIndex(index);
    }
    if (searchEdit)
        searchEdit->setPlaceholderText(tr("搜索名称或文件名"));

    if (saveAction)
        saveAction->setText(tr("保存"));
    if (undoAction)
        undoAction->setText(tr("撤销"));
    if (redoAction)
        redoAction->setText(tr("重做"));
    if (playtestAction)
    {
        playtestAction->setText(tr("试玩当前武功"));
        playtestAction->setToolTip(
            tr("只带入当前武功和所选等级，并以战斗状态进入试玩场景"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("magicListTitle")))
        label->setText(tr("武功列表"));
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("magicListHint")))
    {
        label->setText(tr("双击或按 Enter 打开；已打开的武功会切回原标签。"));
    }
    if (QTabWidget* tabs =
            findChild<QTabWidget*>(QStringLiteral("magicEditorTabs")))
    {
        tabs->setTabText(0, tr("概览与效果"));
        tabs->setTabText(1, tr("等级参数"));
    }
    if (QGroupBox* group =
            findChild<QGroupBox*>(QStringLiteral("magicIdentityGroup")))
    {
        group->setTitle(tr("武功信息"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("magicNameLabel")))
        label->setText(tr("名称"));
    if (QLabel* label =
            findChild<QLabel*>(QStringLiteral("magicIntroductionLabel")))
    {
        label->setText(tr("说明"));
    }
    if (QGroupBox* group =
            findChild<QGroupBox*>(QStringLiteral("magicEffectGroup")))
    {
        group->setTitle(tr("作用方式与范围"));
    }
    if (QLabel* label =
            findChild<QLabel*>(QStringLiteral("magicMoveKindLabel")))
    {
        label->setText(tr("作用方式"));
    }
    if (QLabel* label =
            findChild<QLabel*>(QStringLiteral("magicRegionLabel")))
    {
        label->setText(tr("区域形状"));
    }
    if (QLabel* label =
            findChild<QLabel*>(QStringLiteral("magicPreviewLevelLabel")))
    {
        label->setText(tr("当前等级"));
    }
    if (QLabel* label = findChild<QLabel*>(
            QStringLiteral("magicSelectedLevelEffectTitle")))
    {
        label->setText(tr("本级效果值"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("magicRangeHint")))
    {
        label->setText(
            tr("范围图是便于理解的示意；最终速度、碰撞和连锁效果以试玩为准。"));
    }
    if (QGroupBox* group =
            findChild<QGroupBox*>(QStringLiteral("magicAppearanceGroup")))
    {
        group->setTitle(tr("当前等级的速度与表现"));
    }
    const struct AppearanceTranslation
    {
        const char* objectName;
        QString text;
    } appearanceTranslations[] = {
        {"magicSpecialKindLabel", tr("附加效果")},
        {"magicSpeedLabel", tr("飞行速度")},
        {"magicWaitFrameLabel", tr("延迟出现（帧）")},
        {"magicLifeFrameLabel", tr("持续时间（帧）")},
        {"magicAlphaBlendLabel", tr("画面混合")},
        {"magicFlyingLumLabel", tr("飞行亮度")},
        {"magicVanishLumLabel", tr("命中亮度")}
    };
    for (const AppearanceTranslation& entry : appearanceTranslations)
    {
        if (QLabel* label = findChild<QLabel*>(
                QString::fromLatin1(entry.objectName)))
        {
            label->setText(entry.text);
        }
    }
    if (QGroupBox* group =
            findChild<QGroupBox*>(QStringLiteral("magicResourcesGroup")))
    {
        group->setTitle(tr("图片与声音"));
    }
    auto fieldTitle = [this](MagicTextField field)
    {
        switch (field)
        {
        case MagicTextField::Image: return tr("技能展示");
        case MagicTextField::Icon: return tr("武功图标");
        case MagicTextField::FlyingImage: return tr("飞行特效");
        case MagicTextField::VanishImage: return tr("命中特效");
        case MagicTextField::FlyingSound: return tr("施放声音");
        case MagicTextField::VanishSound: return tr("命中声音");
        case MagicTextField::SuperModeImage: return tr("全屏特效");
        case MagicTextField::SuperModeSound: return tr("全屏声音");
        case MagicTextField::ActionFile: return tr("关联攻击时的动作");
        case MagicTextField::UseActionFile: return tr("施展动作（优先）");
        case MagicTextField::AttackFile: return tr("修炼时的攻击效果");
        case MagicTextField::Name: return tr("名称");
        case MagicTextField::Introduction: return tr("说明");
        }
        return QString();
    };
    for (QGroupBox* group : findChildren<QGroupBox*>())
    {
        const QVariant fieldValue = group->property("magicTextField");
        if (fieldValue.isValid())
        {
            group->setTitle(fieldTitle(
                static_cast<MagicTextField>(fieldValue.toInt())));
        }
    }
    if (QGroupBox* group =
            findChild<QGroupBox*>(QStringLiteral("magicActionGroup")))
    {
        group->setTitle(tr("施展动作与修炼攻击"));
    }
    if (QLabel* label =
            findChild<QLabel*>(QStringLiteral("magicAttackFileHint")))
    {
        label->setText(
            tr("角色修炼此武功后，普通攻击会附带这里选择的武功效果。"));
    }
    if (QPushButton* button = findChild<QPushButton*>(
            QStringLiteral("magicOpenAttackFileButton")))
    {
        button->setText(tr("打开关联武功"));
    }
    for (QPushButton* button :
         findChildren<QPushButton*>(QStringLiteral("magicResourceBrowseButton")))
    {
        button->setText(tr("选择…"));
    }
    for (QPushButton* button :
         findChildren<QPushButton*>(QStringLiteral("magicSoundBrowseButton")))
    {
        button->setText(tr("选择…"));
    }
    for (QPushButton* button :
         findChildren<QPushButton*>(QStringLiteral("magicSoundPlayButton")))
    {
        button->setText(tr("试听"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("magicLevelHint")))
    {
        label->setText(
            tr("每行对应游戏中的一个武功等级。浅色斜体值表示该等级沿用前一级；直接修改即可为本级保存。"));
    }
    refreshing = previouslyRefreshing;
    if (!previouslyRefreshing)
        refreshLevelEffectControls();
    updateWindowTitle();
    if (levelTable && !previouslyRefreshing)
        refreshLevelTable();
    if (documentList && !previouslyRefreshing)
        refreshDocumentList();
    if (fileSummaryLabel)
    {
        fileSummaryLabel->setText(
            filePath.isEmpty()
            ? tr("从左侧列表或项目树打开一个武功文件。")
            : tr("正在编辑：%1").arg(QDir::toNativeSeparators(filePath)));
    }
    if (preservationLabel)
    {
        preservationLabel->setText(
            filePath.isEmpty()
            ? QString()
            : tr("原文件中还有 %1 项高级内容未在此显示；保存时会保持不变。")
                  .arg(document.hiddenFieldCount()));
    }
    if (rangePreview)
        rangePreview->update();
}
