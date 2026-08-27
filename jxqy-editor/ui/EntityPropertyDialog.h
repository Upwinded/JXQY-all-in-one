#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <cstdint>
#include "../core/MapFileEditor.h"

struct MapEntityData;
class QDialogButtonBox;

class EntityPropertyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EntityPropertyDialog(QWidget* parent = nullptr);
    ~EntityPropertyDialog();

    void setEntity(const MapEntityData& entity);
    bool applyToEntity(MapEntityData& entity) const;
    void setAssetsBasePath(const QString& path);

private:
    QLineEdit* nameEdit = nullptr;
    QLineEdit* iniFileEdit = nullptr;
    QLabel* iniFileLabel = nullptr;
    QSpinBox* mapXSpin = nullptr;
    QSpinBox* mapYSpin = nullptr;
    QSpinBox* directionSpin = nullptr;
    QComboBox* kindCombo = nullptr;
    QLineEdit* scriptFileEdit = nullptr;
    QSpinBox* actionSpin = nullptr;
    QComboBox* relationCombo = nullptr;
    QSpinBox* lumSpin = nullptr;
    QSpinBox* stateSpin = nullptr;
    QSpinBox* walkSpeedSpin = nullptr;
    QSpinBox* pathFinderSpin = nullptr;
    QSpinBox* dialogRadiusSpin = nullptr;
    QSpinBox* lifeSpin = nullptr;
    QSpinBox* lifeMaxSpin = nullptr;
    QSpinBox* standSpeedSpin = nullptr;
    QSpinBox* thewSpin = nullptr;
    QSpinBox* thewMaxSpin = nullptr;
    QSpinBox* manaSpin = nullptr;
    QSpinBox* manaMaxSpin = nullptr;
    QSpinBox* attackSpin = nullptr;
    QSpinBox* defendSpin = nullptr;
    QSpinBox* evadeSpin = nullptr;
    QSpinBox* duckSpin = nullptr;
    QSpinBox* expSpin = nullptr;
    QSpinBox* levelUpExpSpin = nullptr;
    QSpinBox* levelSpin = nullptr;
    QSpinBox* attackLevelSpin = nullptr;
    QSpinBox* magicLevelSpin = nullptr;
    QSpinBox* visionRadiusSpin = nullptr;
    QSpinBox* attackRadiusSpin = nullptr;
    QLineEdit* bodyIniEdit = nullptr;
    QLineEdit* flyIniEdit = nullptr;
    QLineEdit* flyIni2Edit = nullptr;
    QLineEdit* flyInisEdit = nullptr;
    QLineEdit* magicIniEdit = nullptr;
    QLineEdit* deathScriptEdit = nullptr;

    QSpinBox* idleSpin = nullptr;
    QLineEdit* fixedPositionEdit = nullptr;
    QComboBox* aiTypeCombo = nullptr;
    QLineEdit* scriptFileRightEdit = nullptr;
    QLineEdit* timerScriptFileEdit = nullptr;
    QSpinBox* timerScriptIntervalSpin = nullptr;
    QComboBox* canInteractDirectlyCombo = nullptr;
    QSpinBox* attack2Spin = nullptr;
    QSpinBox* attack3Spin = nullptr;
    QSpinBox* defend2Spin = nullptr;
    QSpinBox* defend3Spin = nullptr;
    QSpinBox* expBonusSpin = nullptr;
    QComboBox* invincibleCombo = nullptr;
    QSpinBox* groupSpin = nullptr;
    QLineEdit* dropIniEdit = nullptr;
    QComboBox* noDropWhenDieCombo = nullptr;
    QSpinBox* reviveMillisecondsSpin = nullptr;
    QLineEdit* visibleVariableNameEdit = nullptr;
    QSpinBox* visibleVariableValueSpin = nullptr;

    QSpinBox* offsetXSpin = nullptr;
    QSpinBox* offsetYSpin = nullptr;
    QSpinBox* frameSpin = nullptr;
    QLineEdit* wavFileEdit = nullptr;
    QSpinBox* damageSpin = nullptr;
    QLineEdit* actionTimeEdit = nullptr;

    QSpinBox* heightSpin = nullptr;
    QLineEdit* scriptFileRightObjEdit = nullptr;
    QComboBox* scriptFileJustTouchCombo = nullptr;
    QComboBox* canInteractDirectlyObjCombo = nullptr;
    QLineEdit* timerScriptFileObjEdit = nullptr;
    QSpinBox* timerScriptIntervalObjSpin = nullptr;
    QLineEdit* reviveNpcIniEdit = nullptr;
    QSpinBox* millisecondsToRemoveSpin = nullptr;

    QComboBox* kindObjCombo = nullptr;

    QWidget* npcFieldsWidget = nullptr;
    QWidget* objectFieldsWidget = nullptr;

    bool isNpcEntity = true;
    QString assetsBasePath;
    QDialogButtonBox* buttonBox = nullptr;

    struct NormalizedResourceReferences
    {
        QString iniFile;
        QString scriptFile;
        QString bodyIni;
        QString flyIni;
        QString flyIni2;
        QString flyInis;
        QString magicIni;
        QString deathScript;
        QString scriptFileRight;
        QString timerScriptFile;
        QString dropIni;
        QString wavFile;
        QString reviveNpcIni;
    };

    bool readActionTime(std::int64_t& value) const;
    bool readResourceReferences(NormalizedResourceReferences& references,
                                QLineEdit** invalidEdit = nullptr,
                                QString* errorMessage = nullptr) const;
    void updateAcceptButton();
};
