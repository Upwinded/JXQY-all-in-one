#include "EntityPropertyDialog.h"
#include "FilePickerHelper.h"
#include "MapRenderCanvas.h"

#include <QScrollArea>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QLabel>
#include <QDir>
#include <algorithm>

EntityPropertyDialog::EntityPropertyDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("实体属性"));
    setMinimumSize(480, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);

    QFormLayout* commonForm = new QFormLayout();

    nameEdit = new QLineEdit();
    nameEdit->setObjectName("entityNameEdit");
    commonForm->addRow(tr("名称"), nameEdit);

    iniFileEdit = new QLineEdit();
    iniFileEdit->setObjectName("entityIniFileEdit");
    iniFileLabel = new QLabel(tr("NPCIni"));
    iniFileLabel->setObjectName("entityIniFileLabel");
    commonForm->addRow(iniFileLabel, iniFileEdit);

    mapXSpin = new QSpinBox();
    mapXSpin->setRange(0, 9999);
    commonForm->addRow(tr("MapX"), mapXSpin);

    mapYSpin = new QSpinBox();
    mapYSpin->setRange(0, 9999);
    commonForm->addRow(tr("MapY"), mapYSpin);

    directionSpin = new QSpinBox();
    directionSpin->setRange(0, 7);
    commonForm->addRow(tr("方向"), directionSpin);

    kindCombo = new QComboBox();
    kindCombo->addItem(tr("0 - 普通"), 0);
    kindCombo->addItem(tr("1 - 战斗型"), 1);
    kindCombo->addItem(tr("2 - 玩家"), 2);
    kindCombo->addItem(tr("3 - 同伴"), 3);
    kindCombo->addItem(tr("4 - 地面动物"), 4);
    kindCombo->addItem(tr("5 - 事件NPC"), 5);
    kindCombo->addItem(tr("6 - 怕人动物"), 6);
    kindCombo->addItem(tr("7 - 飞行NPC"), 7);
    commonForm->addRow(tr("种类"), kindCombo);

    scriptFileEdit = new QLineEdit();
    scriptFileEdit->setObjectName("entityScriptFileEdit");
    commonForm->addRow(tr("脚本文件"), scriptFileEdit);

    lumSpin = new QSpinBox();
    lumSpin->setRange(0, 9999);
    commonForm->addRow(tr("亮度"), lumSpin);

    stateSpin = new QSpinBox();
    stateSpin->setRange(0, 9999);
    commonForm->addRow(tr("状态"), stateSpin);

    scrollLayout->addLayout(commonForm);

    npcFieldsWidget = new QWidget();
    QFormLayout* npcForm = new QFormLayout(npcFieldsWidget);

    actionSpin = new QSpinBox();
    actionSpin->setRange(0, 9999);
    npcForm->addRow(tr("动作"), actionSpin);

    relationCombo = new QComboBox();
    relationCombo->addItem(tr("0 - 友方"), 0);
    relationCombo->addItem(tr("1 - 敌方"), 1);
    relationCombo->addItem(tr("2 - 中立"), 2);
    relationCombo->addItem(tr("3 - 无差别"), 3);
    npcForm->addRow(tr("关系"), relationCombo);

    walkSpeedSpin = new QSpinBox();
    walkSpeedSpin->setRange(0, 9999);
    npcForm->addRow(tr("行走速度"), walkSpeedSpin);

    standSpeedSpin = new QSpinBox();
    standSpeedSpin->setRange(0, 9999);
    npcForm->addRow(tr("站立速度"), standSpeedSpin);

    pathFinderSpin = new QSpinBox();
    pathFinderSpin->setRange(0, 9999);
    npcForm->addRow(tr("寻路模式"), pathFinderSpin);

    dialogRadiusSpin = new QSpinBox();
    dialogRadiusSpin->setRange(0, 9999);
    npcForm->addRow(tr("对话半径"), dialogRadiusSpin);

    lifeSpin = new QSpinBox();
    lifeSpin->setRange(0, 999999);
    npcForm->addRow(tr("生命"), lifeSpin);

    lifeMaxSpin = new QSpinBox();
    lifeMaxSpin->setRange(0, 999999);
    npcForm->addRow(tr("生命上限"), lifeMaxSpin);

    thewSpin = new QSpinBox();
    thewSpin->setRange(0, 999999);
    npcForm->addRow(tr("体力"), thewSpin);

    thewMaxSpin = new QSpinBox();
    thewMaxSpin->setRange(0, 999999);
    npcForm->addRow(tr("体力上限"), thewMaxSpin);

    manaSpin = new QSpinBox();
    manaSpin->setRange(0, 999999);
    npcForm->addRow(tr("内力"), manaSpin);

    manaMaxSpin = new QSpinBox();
    manaMaxSpin->setRange(0, 999999);
    npcForm->addRow(tr("内力上限"), manaMaxSpin);

    attackSpin = new QSpinBox();
    attackSpin->setRange(0, 999999);
    npcForm->addRow(tr("攻击力"), attackSpin);

    defendSpin = new QSpinBox();
    defendSpin->setRange(0, 999999);
    npcForm->addRow(tr("防御力"), defendSpin);

    evadeSpin = new QSpinBox();
    evadeSpin->setRange(0, 999999);
    npcForm->addRow(tr("闪避"), evadeSpin);

    duckSpin = new QSpinBox();
    duckSpin->setRange(0, 999999);
    npcForm->addRow(tr("躲闪"), duckSpin);

    expSpin = new QSpinBox();
    expSpin->setRange(0, 9999999);
    npcForm->addRow(tr("经验"), expSpin);

    levelUpExpSpin = new QSpinBox();
    levelUpExpSpin->setRange(0, 9999999);
    npcForm->addRow(tr("升级经验"), levelUpExpSpin);

    levelSpin = new QSpinBox();
    levelSpin->setRange(0, 9999);
    npcForm->addRow(tr("等级"), levelSpin);

    attackLevelSpin = new QSpinBox();
    attackLevelSpin->setRange(0, 9999);
    npcForm->addRow(tr("攻击等级"), attackLevelSpin);

    magicLevelSpin = new QSpinBox();
    magicLevelSpin->setRange(0, 9999);
    npcForm->addRow(tr("武功等级"), magicLevelSpin);

    visionRadiusSpin = new QSpinBox();
    visionRadiusSpin->setRange(0, 9999);
    npcForm->addRow(tr("视野半径"), visionRadiusSpin);

    attackRadiusSpin = new QSpinBox();
    attackRadiusSpin->setRange(0, 9999);
    npcForm->addRow(tr("攻击半径"), attackRadiusSpin);

    bodyIniEdit = new QLineEdit();
    bodyIniEdit->setObjectName("entityBodyIniEdit");
    npcForm->addRow(tr("身体Ini"), bodyIniEdit);

    flyIniEdit = new QLineEdit();
    flyIniEdit->setObjectName("entityFlyIniEdit");
    npcForm->addRow(tr("飞行Ini"), flyIniEdit);

    flyIni2Edit = new QLineEdit();
    flyIni2Edit->setObjectName("entityFlyIni2Edit");
    npcForm->addRow(tr("飞行Ini2"), flyIni2Edit);

    flyInisEdit = new QLineEdit();
    flyInisEdit->setObjectName("entityFlyInisEdit");
    npcForm->addRow(tr("飞行魔法列表"), flyInisEdit);

    magicIniEdit = new QLineEdit();
    magicIniEdit->setObjectName("entityMagicIniEdit");
    npcForm->addRow(tr("武功Ini"), magicIniEdit);

    deathScriptEdit = new QLineEdit();
    deathScriptEdit->setObjectName("entityDeathScriptEdit");
    npcForm->addRow(tr("死亡脚本"), deathScriptEdit);

    idleSpin = new QSpinBox();
    idleSpin->setRange(0, 9999);
    npcForm->addRow(tr("攻击间隔"), idleSpin);

    fixedPositionEdit = new QLineEdit();
    npcForm->addRow(tr("巡逻路径"), fixedPositionEdit);

    aiTypeCombo = new QComboBox();
    aiTypeCombo->addItem(tr("0 - 原版AI"), 0);
    aiTypeCombo->addItem(tr("1 - 随机移动随机攻击"), 1);
    aiTypeCombo->addItem(tr("2 - 随机移动不攻击"), 2);
    npcForm->addRow(tr("AI类型"), aiTypeCombo);

    scriptFileRightEdit = new QLineEdit();
    scriptFileRightEdit->setObjectName("entityScriptFileRightEdit");
    npcForm->addRow(tr("右键脚本"), scriptFileRightEdit);

    timerScriptFileEdit = new QLineEdit();
    timerScriptFileEdit->setObjectName("entityTimerScriptFileEdit");
    npcForm->addRow(tr("定时脚本"), timerScriptFileEdit);

    timerScriptIntervalSpin = new QSpinBox();
    timerScriptIntervalSpin->setRange(0, 9999999);
    npcForm->addRow(tr("定时间隔(ms)"), timerScriptIntervalSpin);

    canInteractDirectlyCombo = new QComboBox();
    canInteractDirectlyCombo->addItem(tr("0 - 需走近"), 0);
    canInteractDirectlyCombo->addItem(tr("1 - 远程交互"), 1);
    npcForm->addRow(tr("直接交互"), canInteractDirectlyCombo);

    attack2Spin = new QSpinBox();
    attack2Spin->setRange(0, 999999);
    npcForm->addRow(tr("攻击力2"), attack2Spin);

    attack3Spin = new QSpinBox();
    attack3Spin->setRange(0, 999999);
    npcForm->addRow(tr("攻击力3"), attack3Spin);

    defend2Spin = new QSpinBox();
    defend2Spin->setRange(0, 999999);
    npcForm->addRow(tr("防御力2"), defend2Spin);

    defend3Spin = new QSpinBox();
    defend3Spin->setRange(0, 999999);
    npcForm->addRow(tr("防御力3"), defend3Spin);

    expBonusSpin = new QSpinBox();
    expBonusSpin->setRange(0, 9999999);
    npcForm->addRow(tr("额外经验"), expBonusSpin);

    invincibleCombo = new QComboBox();
    invincibleCombo->addItem(tr("0 - 否"), 0);
    invincibleCombo->addItem(tr("1 - 是"), 1);
    npcForm->addRow(tr("无敌"), invincibleCombo);

    groupSpin = new QSpinBox();
    groupSpin->setRange(0, 9999);
    npcForm->addRow(tr("分组"), groupSpin);

    dropIniEdit = new QLineEdit();
    dropIniEdit->setObjectName("entityDropIniEdit");
    npcForm->addRow(tr("掉落配置"), dropIniEdit);

    noDropWhenDieCombo = new QComboBox();
    noDropWhenDieCombo->addItem(tr("0 - 正常掉落"), 0);
    noDropWhenDieCombo->addItem(tr("1 - 不掉落"), 1);
    npcForm->addRow(tr("死亡不掉落"), noDropWhenDieCombo);

    reviveMillisecondsSpin = new QSpinBox();
    reviveMillisecondsSpin->setRange(0, 9999999);
    npcForm->addRow(tr("复活时间(ms)"), reviveMillisecondsSpin);

    visibleVariableNameEdit = new QLineEdit();
    npcForm->addRow(tr("可见变量名"), visibleVariableNameEdit);

    visibleVariableValueSpin = new QSpinBox();
    visibleVariableValueSpin->setRange(-9999, 9999);
    npcForm->addRow(tr("可见变量值"), visibleVariableValueSpin);

    scrollLayout->addWidget(npcFieldsWidget);

    objectFieldsWidget = new QWidget();
    QFormLayout* objectForm = new QFormLayout(objectFieldsWidget);

    kindObjCombo = new QComboBox();
    kindObjCombo->addItem(tr("0 - 动态（持续动画）"), 0);
    kindObjCombo->addItem(tr("1 - 静态（状态帧）"), 1);
    kindObjCombo->addItem(tr("2 - 尸体"), 2);
    kindObjCombo->addItem(tr("3 - 循环音效"), 3);
    kindObjCombo->addItem(tr("4 - 随机音效"), 4);
    kindObjCombo->addItem(tr("5 - 门"), 5);
    kindObjCombo->addItem(tr("6 - 陷阱"), 6);
    kindObjCombo->addItem(tr("7 - 掉落物"), 7);
    kindObjCombo->addItem(tr("8 - 旧版掉落物"), 8);
    objectForm->addRow(tr("种类"), kindObjCombo);

    offsetXSpin = new QSpinBox();
    offsetXSpin->setRange(-9999, 9999);
    objectForm->addRow(tr("偏移X"), offsetXSpin);

    offsetYSpin = new QSpinBox();
    offsetYSpin->setRange(-9999, 9999);
    objectForm->addRow(tr("偏移Y"), offsetYSpin);

    frameSpin = new QSpinBox();
    frameSpin->setRange(0, 9999);
    objectForm->addRow(tr("帧"), frameSpin);

    heightSpin = new QSpinBox();
    heightSpin->setRange(0, 9999);
    objectForm->addRow(tr("高度"), heightSpin);

    wavFileEdit = new QLineEdit();
    wavFileEdit->setObjectName("entityWavFileEdit");
    objectForm->addRow(tr("音效文件"), wavFileEdit);

    damageSpin = new QSpinBox();
    damageSpin->setRange(0, 999999);
    objectForm->addRow(tr("伤害"), damageSpin);

    actionTimeEdit = new QLineEdit();
    actionTimeEdit->setObjectName("entityActionTimeEdit");
    actionTimeEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("(?:0|[1-9][0-9]{0,18})")),
        actionTimeEdit));
    objectForm->addRow(tr("动作时间"), actionTimeEdit);

    scriptFileRightObjEdit = new QLineEdit();
    scriptFileRightObjEdit->setObjectName("entityObjectScriptFileRightEdit");
    objectForm->addRow(tr("右键脚本"), scriptFileRightObjEdit);

    scriptFileJustTouchCombo = new QComboBox();
    scriptFileJustTouchCombo->addItem(tr("0 - 否"), 0);
    scriptFileJustTouchCombo->addItem(tr("1 - 触碰即触发"), 1);
    objectForm->addRow(tr("触碰触发"), scriptFileJustTouchCombo);

    canInteractDirectlyObjCombo = new QComboBox();
    canInteractDirectlyObjCombo->addItem(tr("0 - 需走近"), 0);
    canInteractDirectlyObjCombo->addItem(tr("1 - 远程交互"), 1);
    objectForm->addRow(tr("直接交互"), canInteractDirectlyObjCombo);

    timerScriptFileObjEdit = new QLineEdit();
    timerScriptFileObjEdit->setObjectName("entityObjectTimerScriptFileEdit");
    objectForm->addRow(tr("定时脚本"), timerScriptFileObjEdit);

    timerScriptIntervalObjSpin = new QSpinBox();
    timerScriptIntervalObjSpin->setRange(0, 9999999);
    objectForm->addRow(tr("定时间隔(ms)"), timerScriptIntervalObjSpin);

    reviveNpcIniEdit = new QLineEdit();
    reviveNpcIniEdit->setObjectName("entityReviveNpcIniEdit");
    objectForm->addRow(tr("复活NPC配置"), reviveNpcIniEdit);

    millisecondsToRemoveSpin = new QSpinBox();
    millisecondsToRemoveSpin->setRange(0, 9999999);
    objectForm->addRow(tr("自动移除(ms)"), millisecondsToRemoveSpin);

    scrollLayout->addWidget(objectFieldsWidget);

    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    // 添加文件选择按钮
    const QString iniFilter = tr("INI Files (*.ini);;All Files (*)");
    const QString scriptFilter = tr("Script Files (*.txt *.ini *.lua *.script);;All Files (*)");
    const QString wavFilter = tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)");

    auto getAssetsPath = [this]() -> QString { return assetsBasePath; };
    // Helper: build a defaultDirGetter that tries primary subdirectory, then fallback,
    // then returns empty (so pickFileAndSetResult falls back to assetsBasePath).
    auto makeDefaultDirGetter = [this](const QString& primary, const QString& fallback = QString()) -> std::function<QString()>
    {
        return [this, primary, fallback]() -> QString
        {
            QString basePath = assetsBasePath;
            if (basePath.isEmpty()) return QString();
            QString primaryPath = QDir(basePath).filePath(primary);
            if (QDir(primaryPath).exists()) return primaryPath;
            if (!fallback.isEmpty())
            {
                QString fallbackPath = QDir(basePath).filePath(fallback);
                if (QDir(fallbackPath).exists()) return fallbackPath;
            }
            return QString();
        };
    };

    // iniFileEdit: NPCIni when NPC, ObjFile when Object
    auto iniFileDefaultDirGetter = [this]() -> QString
    {
        const FilePickerHelper::EntityResourceField field = isNpcEntity
            ? FilePickerHelper::EntityResourceField::NpcIni
            : FilePickerHelper::EntityResourceField::ObjFile;
        const QStringList folders = FilePickerHelper::entityResourceFolders(field);
        return folders.isEmpty()
            ? QString()
            : QDir(assetsBasePath).filePath(folders.front());
    };
    auto iniFileFieldGetter = [this]() -> FilePickerHelper::EntityResourceField
    {
        return isNpcEntity
            ? FilePickerHelper::EntityResourceField::NpcIni
            : FilePickerHelper::EntityResourceField::ObjFile;
    };

    // commonForm 通过 addLayout 嵌入，iniFileEdit/scriptFileEdit 的 parentWidget()->layout()
    // 不是 QFormLayout，需要显式传入 formLayout
    FilePickerHelper::addEntityResourcePickerButton(iniFileEdit, commonForm,
        iniFilter, getAssetsPath, iniFileFieldGetter, this,
        iniFileDefaultDirGetter);
    FilePickerHelper::addEntityResourcePickerButton(scriptFileEdit, commonForm,
        scriptFilter, getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile,
        this, makeDefaultDirGetter("script/common", "script"));

    // npcForm/objectForm 是 QWidget 的直接 layout，自动查找可工作
    FilePickerHelper::addEntityResourcePickerButton(bodyIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::BodyIni, this);
    FilePickerHelper::addEntityResourcePickerButton(flyIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::FlyIni, this);
    FilePickerHelper::addEntityResourcePickerButton(flyIni2Edit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::FlyIni, this);
    FilePickerHelper::addEntityResourceAppendPickerButton(flyInisEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::FlyInis, this);
    FilePickerHelper::addEntityResourcePickerButton(magicIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::MagicIni, this);
    FilePickerHelper::addEntityResourcePickerButton(deathScriptEdit, scriptFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile, this);
    FilePickerHelper::addEntityResourcePickerButton(scriptFileRightEdit, scriptFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile, this);
    FilePickerHelper::addEntityResourcePickerButton(timerScriptFileEdit, scriptFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile, this);
    FilePickerHelper::addEntityResourcePickerButton(dropIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::DropIni, this);
    FilePickerHelper::addEntityResourcePickerButton(wavFileEdit, wavFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::WavFile, this);
    FilePickerHelper::addEntityResourcePickerButton(scriptFileRightObjEdit,
        scriptFilter, getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile,
        this);
    FilePickerHelper::addEntityResourcePickerButton(timerScriptFileObjEdit,
        scriptFilter, getAssetsPath, FilePickerHelper::EntityResourceField::ScriptFile,
        this);
    FilePickerHelper::addEntityResourcePickerButton(reviveNpcIniEdit, iniFilter,
        getAssetsPath, FilePickerHelper::EntityResourceField::ReviveNpcIni, this);

    buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(actionTimeEdit, &QLineEdit::textChanged,
            this, &EntityPropertyDialog::updateAcceptButton);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]()
    {
        std::int64_t value = 0;
        if (!isNpcEntity && !readActionTime(value))
            return;

        NormalizedResourceReferences references;
        QLineEdit* invalidEdit = nullptr;
        QString errorMessage;
        if (!readResourceReferences(references, &invalidEdit, &errorMessage))
        {
            QMessageBox::warning(this, tr("资源引用无效"), errorMessage);
            if (invalidEdit)
                invalidEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

EntityPropertyDialog::~EntityPropertyDialog()
{
}

void EntityPropertyDialog::setEntity(const MapEntityData& entity)
{
    isNpcEntity = entity.isNpc;
    directionSpin->setRange(std::min(0, entity.direction),
                            std::max(7, entity.direction));

    auto selectPreservingUnknownValue = [this](QComboBox* combo, int value)
    {
        int index = combo->findData(value);
        if (index < 0)
        {
            combo->addItem(tr("%1 - 未知值（原样保留）").arg(value), value);
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
    };

    if (isNpcEntity)
    {
        setWindowTitle(tr("NPC属性"));
        iniFileLabel->setText(tr("NPCIni"));
        npcFieldsWidget->show();
        objectFieldsWidget->hide();

        iniFileEdit->setToolTip(tr("NPC资源文件路径"));

        nameEdit->setText(QString::fromUtf8(entity.name.c_str()));
        iniFileEdit->setText(QString::fromUtf8(entity.iniFile.c_str()));
        mapXSpin->setValue(entity.mapX);
        mapYSpin->setValue(entity.mapY);
        directionSpin->setValue(entity.direction);
        selectPreservingUnknownValue(kindCombo, entity.kind);
        scriptFileEdit->setText(QString::fromUtf8(entity.scriptFile.c_str()));
        lumSpin->setValue(entity.lum);
        stateSpin->setValue(entity.state);

        actionSpin->setValue(entity.action);
        relationCombo->setCurrentIndex(relationCombo->findData(entity.relation));
        walkSpeedSpin->setValue(entity.walkSpeed);
        standSpeedSpin->setValue(entity.standSpeed);
        pathFinderSpin->setValue(entity.pathFinder);
        dialogRadiusSpin->setValue(entity.dialogRadius);
        lifeSpin->setValue(entity.life);
        lifeMaxSpin->setValue(entity.lifeMax);
        thewSpin->setValue(entity.thew);
        thewMaxSpin->setValue(entity.thewMax);
        manaSpin->setValue(entity.mana);
        manaMaxSpin->setValue(entity.manaMax);
        attackSpin->setValue(entity.attack);
        defendSpin->setValue(entity.defend);
        evadeSpin->setValue(entity.evade);
        duckSpin->setValue(entity.duck);
        expSpin->setValue(entity.exp);
        levelUpExpSpin->setValue(entity.levelUpExp);
        levelSpin->setValue(entity.level);
        attackLevelSpin->setValue(entity.attackLevel);
        magicLevelSpin->setValue(entity.magicLevel);
        visionRadiusSpin->setValue(entity.visionRadius);
        attackRadiusSpin->setValue(entity.attackRadius);
        bodyIniEdit->setText(QString::fromUtf8(entity.bodyIni.c_str()));
        flyIniEdit->setText(QString::fromUtf8(entity.flyIni.c_str()));
        flyIni2Edit->setText(QString::fromUtf8(entity.flyIni2.c_str()));
        flyInisEdit->setText(QString::fromUtf8(entity.flyInis.c_str()));
        magicIniEdit->setText(QString::fromUtf8(entity.magicIni.c_str()));
        deathScriptEdit->setText(QString::fromUtf8(entity.deathScript.c_str()));

        idleSpin->setValue(entity.idle);
        fixedPositionEdit->setText(QString::fromUtf8(entity.fixedPosition.c_str()));
        aiTypeCombo->setCurrentIndex(aiTypeCombo->findData(entity.aiType));
        scriptFileRightEdit->setText(QString::fromUtf8(entity.scriptFileRight.c_str()));
        timerScriptFileEdit->setText(QString::fromUtf8(entity.timerScriptFile.c_str()));
        timerScriptIntervalSpin->setValue(entity.timerScriptInterval);
        canInteractDirectlyCombo->setCurrentIndex(canInteractDirectlyCombo->findData(entity.canInteractDirectly));
        attack2Spin->setValue(entity.attack2);
        attack3Spin->setValue(entity.attack3);
        defend2Spin->setValue(entity.defend2);
        defend3Spin->setValue(entity.defend3);
        expBonusSpin->setValue(entity.expBonus);
        invincibleCombo->setCurrentIndex(invincibleCombo->findData(entity.invincible));
        groupSpin->setValue(entity.group);
        dropIniEdit->setText(QString::fromUtf8(entity.dropIni.c_str()));
        noDropWhenDieCombo->setCurrentIndex(noDropWhenDieCombo->findData(entity.noDropWhenDie));
        reviveMillisecondsSpin->setValue(entity.reviveMilliseconds);
        visibleVariableNameEdit->setText(QString::fromUtf8(entity.visibleVariableName.c_str()));
        visibleVariableValueSpin->setValue(entity.visibleVariableValue);
    }
    else
    {
        setWindowTitle(tr("物体属性"));
        iniFileLabel->setText(tr("ObjFile"));
        npcFieldsWidget->hide();
        objectFieldsWidget->show();

        iniFileEdit->setToolTip(tr("物体资源文件路径"));

        nameEdit->setText(QString::fromUtf8(entity.name.c_str()));
        iniFileEdit->setText(QString::fromUtf8(entity.iniFile.c_str()));
        mapXSpin->setValue(entity.mapX);
        mapYSpin->setValue(entity.mapY);
        directionSpin->setValue(entity.direction);
        selectPreservingUnknownValue(kindCombo, entity.kind);
        scriptFileEdit->setText(QString::fromUtf8(entity.scriptFile.c_str()));
        lumSpin->setValue(entity.lum);
        stateSpin->setValue(entity.state);

        selectPreservingUnknownValue(kindObjCombo, entity.kind);
        offsetXSpin->setValue(entity.offsetX);
        offsetYSpin->setValue(entity.offsetY);
        frameSpin->setValue(entity.frame);
        heightSpin->setValue(entity.height);
        wavFileEdit->setText(QString::fromUtf8(entity.wavFile.c_str()));
        damageSpin->setValue(entity.damage);
        actionTimeEdit->setText(QString::number(entity.actionTime));
        scriptFileRightObjEdit->setText(QString::fromUtf8(entity.scriptFileRight.c_str()));
        scriptFileJustTouchCombo->setCurrentIndex(scriptFileJustTouchCombo->findData(entity.scriptFileJustTouch));
        canInteractDirectlyObjCombo->setCurrentIndex(canInteractDirectlyObjCombo->findData(entity.canInteractDirectly));
        timerScriptFileObjEdit->setText(QString::fromUtf8(entity.timerScriptFile.c_str()));
        timerScriptIntervalObjSpin->setValue(entity.timerScriptInterval);
        reviveNpcIniEdit->setText(QString::fromUtf8(entity.reviveNpcIni.c_str()));
        millisecondsToRemoveSpin->setValue(entity.millisecondsToRemove);
    }
    updateAcceptButton();
}

bool EntityPropertyDialog::applyToEntity(MapEntityData& entity) const
{
    std::int64_t actionTime = 0;
    if (!isNpcEntity && !readActionTime(actionTime))
        return false;

    NormalizedResourceReferences references;
    if (!readResourceReferences(references))
        return false;

    entity.name = nameEdit->text().toUtf8().constData();
    entity.iniFile = references.iniFile.toUtf8().constData();
    entity.mapX = mapXSpin->value();
    entity.mapY = mapYSpin->value();
    entity.direction = directionSpin->value();
    entity.kind = kindCombo->currentData().toInt();
    entity.scriptFile = references.scriptFile.toUtf8().constData();
    entity.lum = lumSpin->value();
    entity.state = stateSpin->value();

    if (isNpcEntity)
    {
        entity.action = actionSpin->value();
        entity.relation = relationCombo->currentData().toInt();
        entity.walkSpeed = walkSpeedSpin->value();
        entity.standSpeed = standSpeedSpin->value();
        entity.pathFinder = pathFinderSpin->value();
        entity.dialogRadius = dialogRadiusSpin->value();
        entity.life = lifeSpin->value();
        entity.lifeMax = lifeMaxSpin->value();
        entity.thew = thewSpin->value();
        entity.thewMax = thewMaxSpin->value();
        entity.mana = manaSpin->value();
        entity.manaMax = manaMaxSpin->value();
        entity.attack = attackSpin->value();
        entity.defend = defendSpin->value();
        entity.evade = evadeSpin->value();
        entity.duck = duckSpin->value();
        entity.exp = expSpin->value();
        entity.levelUpExp = levelUpExpSpin->value();
        entity.level = levelSpin->value();
        entity.attackLevel = attackLevelSpin->value();
        entity.magicLevel = magicLevelSpin->value();
        entity.visionRadius = visionRadiusSpin->value();
        entity.attackRadius = attackRadiusSpin->value();
        entity.bodyIni = references.bodyIni.toUtf8().constData();
        entity.flyIni = references.flyIni.toUtf8().constData();
        entity.flyIni2 = references.flyIni2.toUtf8().constData();
        entity.flyInis = references.flyInis.toUtf8().constData();
        entity.magicIni = references.magicIni.toUtf8().constData();
        entity.deathScript = references.deathScript.toUtf8().constData();

        entity.idle = idleSpin->value();
        entity.fixedPosition = fixedPositionEdit->text().toUtf8().constData();
        entity.aiType = aiTypeCombo->currentData().toInt();
        entity.scriptFileRight = references.scriptFileRight.toUtf8().constData();
        entity.timerScriptFile = references.timerScriptFile.toUtf8().constData();
        entity.timerScriptInterval = timerScriptIntervalSpin->value();
        entity.canInteractDirectly = canInteractDirectlyCombo->currentData().toInt();
        entity.attack2 = attack2Spin->value();
        entity.attack3 = attack3Spin->value();
        entity.defend2 = defend2Spin->value();
        entity.defend3 = defend3Spin->value();
        entity.expBonus = expBonusSpin->value();
        entity.invincible = invincibleCombo->currentData().toInt();
        entity.group = groupSpin->value();
        entity.dropIni = references.dropIni.toUtf8().constData();
        entity.noDropWhenDie = noDropWhenDieCombo->currentData().toInt();
        entity.reviveMilliseconds = reviveMillisecondsSpin->value();
        entity.visibleVariableName = visibleVariableNameEdit->text().toUtf8().constData();
        entity.visibleVariableValue = visibleVariableValueSpin->value();
    }
    else
    {
        entity.kind = kindObjCombo->currentData().toInt();
        entity.offsetX = offsetXSpin->value();
        entity.offsetY = offsetYSpin->value();
        entity.frame = frameSpin->value();
        entity.height = heightSpin->value();
        entity.wavFile = references.wavFile.toUtf8().constData();
        entity.damage = damageSpin->value();
        entity.actionTime = actionTime;
        entity.scriptFileRight = references.scriptFileRight.toUtf8().constData();
        entity.scriptFileJustTouch = scriptFileJustTouchCombo->currentData().toInt();
        entity.canInteractDirectly = canInteractDirectlyObjCombo->currentData().toInt();
        entity.timerScriptFile = references.timerScriptFile.toUtf8().constData();
        entity.timerScriptInterval = timerScriptIntervalObjSpin->value();
        entity.reviveNpcIni = references.reviveNpcIni.toUtf8().constData();
        entity.millisecondsToRemove = millisecondsToRemoveSpin->value();
    }
    return true;
}

bool EntityPropertyDialog::readActionTime(std::int64_t& value) const
{
    if (!actionTimeEdit)
        return false;
    bool ok = false;
    const qlonglong parsed = actionTimeEdit->text().toLongLong(&ok);
    if (!ok || parsed < 0)
        return false;
    value = static_cast<std::int64_t>(parsed);
    return true;
}

bool EntityPropertyDialog::readResourceReferences(
    NormalizedResourceReferences& references,
    QLineEdit** invalidEdit,
    QString* errorMessage) const
{
    auto normalize = [this, invalidEdit, errorMessage](
        QLineEdit* edit,
        FilePickerHelper::EntityResourceField field,
        const QString& label,
        QString& output) -> bool
    {
        if (FilePickerHelper::normalizeEntityResourceReference(
                field, edit->text(), output))
        {
            return true;
        }
        if (invalidEdit)
            *invalidEdit = edit;
        if (errorMessage)
        {
            if (field == FilePickerHelper::EntityResourceField::FlyInis)
            {
                *errorMessage = tr(
                    "飞行魔法列表必须使用 file.ini[:距离] 项，并以分号分隔；文件名相对于 ini/magic。\n\n当前值: %1")
                    .arg(edit->text());
            }
            else
            {
                *errorMessage = tr(
                    "%1 必须是其运行时资源目录内的相对名称，不能包含绝对路径或其他业务目录。\n\n当前值: %2")
                    .arg(label, edit->text());
            }
        }
        return false;
    };

    const FilePickerHelper::EntityResourceField iniField = isNpcEntity
        ? FilePickerHelper::EntityResourceField::NpcIni
        : FilePickerHelper::EntityResourceField::ObjFile;
    if (!normalize(iniFileEdit, iniField,
            isNpcEntity ? tr("NPCIni") : tr("ObjFile"), references.iniFile) ||
        !normalize(scriptFileEdit, FilePickerHelper::EntityResourceField::ScriptFile,
            tr("脚本文件"), references.scriptFile))
    {
        return false;
    }

    if (isNpcEntity)
    {
        return normalize(bodyIniEdit, FilePickerHelper::EntityResourceField::BodyIni,
                   tr("身体Ini"), references.bodyIni) &&
            normalize(flyIniEdit, FilePickerHelper::EntityResourceField::FlyIni,
                   tr("飞行Ini"), references.flyIni) &&
            normalize(flyIni2Edit, FilePickerHelper::EntityResourceField::FlyIni,
                   tr("飞行Ini2"), references.flyIni2) &&
            normalize(flyInisEdit, FilePickerHelper::EntityResourceField::FlyInis,
                   tr("飞行魔法列表"), references.flyInis) &&
            normalize(magicIniEdit, FilePickerHelper::EntityResourceField::MagicIni,
                   tr("武功Ini"), references.magicIni) &&
            normalize(deathScriptEdit,
                   FilePickerHelper::EntityResourceField::ScriptFile,
                   tr("死亡脚本"), references.deathScript) &&
            normalize(scriptFileRightEdit,
                   FilePickerHelper::EntityResourceField::ScriptFile,
                   tr("右键脚本"), references.scriptFileRight) &&
            normalize(timerScriptFileEdit,
                   FilePickerHelper::EntityResourceField::ScriptFile,
                   tr("定时脚本"), references.timerScriptFile) &&
            normalize(dropIniEdit, FilePickerHelper::EntityResourceField::DropIni,
                   tr("掉落配置"), references.dropIni);
    }

    return normalize(wavFileEdit, FilePickerHelper::EntityResourceField::WavFile,
               tr("音效文件"), references.wavFile) &&
        normalize(scriptFileRightObjEdit,
               FilePickerHelper::EntityResourceField::ScriptFile,
               tr("右键脚本"), references.scriptFileRight) &&
        normalize(timerScriptFileObjEdit,
               FilePickerHelper::EntityResourceField::ScriptFile,
               tr("定时脚本"), references.timerScriptFile) &&
        normalize(reviveNpcIniEdit,
               FilePickerHelper::EntityResourceField::ReviveNpcIni,
               tr("复活NPC配置"), references.reviveNpcIni);
}

void EntityPropertyDialog::updateAcceptButton()
{
    if (!buttonBox)
        return;
    std::int64_t value = 0;
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
        isNpcEntity || readActionTime(value));
}

void EntityPropertyDialog::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path;
}
