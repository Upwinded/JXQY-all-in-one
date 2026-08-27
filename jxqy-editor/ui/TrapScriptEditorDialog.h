#pragma once

#include <QDialog>
#include <QMap>

#include "../core/INIFileEditor.h"

class MapFileEditor;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QTableWidget;

class TrapScriptEditorDialog : public QDialog
{
    Q_OBJECT

public:
    TrapScriptEditorDialog(const QString& mapName,
                           const QString& assetsBasePath,
                           const MapFileEditor* mapEditor,
                           QWidget* parent = nullptr);

    bool savedChanges() const;

protected:
    void accept() override;
    void reject() override;

private:
    void loadCurrentContext();
    bool saveCurrentContext();
    void populateTable();
    void updateContextDisplay(const QString& message = QString());
    void revertContextCombo();
    bool confirmContextSwitch();

    QString mapName;
    QString mapSection;
    QString assetsBasePath;
    QString currentFilePath;
    QMap<int, int> trapUsageCount;
    INIFileEditor trapsIni;

    QComboBox* contextComboBox = nullptr;
    QLabel* contextNameLabel = nullptr;
    QLabel* pathLabel = nullptr;
    QLabel* messageLabel = nullptr;
    QTableWidget* table = nullptr;
    QDialogButtonBox* buttonBox = nullptr;

    int activeContextIndex = 0;
    bool loadingTable = false;
    bool dirty = false;
    bool saved = false;
    bool temporarySaveConfirmed = false;
};
