#pragma once

#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"

#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/DialogueDocument.h"
#include "../core/ProjectDocumentRegistry.h"

#include <QWidget>

#include <functional>

class QAction;
class QCloseEvent;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QToolBar;
class QUndoStack;
class MpcPreviewLabel;

class DialogueEditorWindow : public QWidget,
                             public CloseTransactionParticipant,
                             public AssetsPathSwitchParticipant
{
    Q_OBJECT

public:
    explicit DialogueEditorWindow(QWidget* parent = nullptr);
    ~DialogueEditorWindow() override;

    static bool isDialogueFilePath(const QString& filePath);

    bool openFile(const QString& filePath,
                  const QString& section = QString());
    bool selectSection(const QString& section);
    bool saveFile();
    bool hasUnsavedChanges() const;
    QString currentFilePath() const;
    QString currentSectionName() const;
    QString displayName() const;

    void setCaller(const QString& scriptPath, int line, int column);
    void clearCaller();
    bool hasCaller() const;
    void setAssetsBasePath(const QString& path);
    void setDocumentPathValidator(
        std::function<bool(const QString&, const QString&)> validator);

    QList<ProjectDocumentState> currentProjectDocuments() const;
    DesktopRunDocumentSnapshot desktopRunDocumentSnapshot() const;

    ClosePlan prepareCloseTransaction() const override;
    bool resolveCloseTransaction(const ClosePlan& plan) override;
    void commitCloseTransaction(const ClosePlan& plan) override;

    Decision prepareAssetsPathSwitch(const QString& path) const override;
    bool resolveAssetsPathSwitch(Decision decision) override;
    void commitAssetsPathSwitch(const QString& path) override;
    QString currentAssetsPath() const override;

signals:
    void documentStatesChanged();
    void documentClosed();
    void playtestRequested();
    void returnToCallerRequested(
        const QString& scriptPath, int line, int column);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupActions();
    void setupConnections();
    void retranslateDynamicUi();
    bool loadDocumentBytes(const QByteArray& bytes);
    void pushSnapshotChange(const QByteArray& before,
                            const QByteArray& after,
                            const QString& description);
    void commitPendingLineEditors();
    void refreshSectionList();
    void refreshLineList();
    void refreshLineEditor();
    void refreshPortraitChoices();
    void refreshPortraitPreview();
    void updateWindowTitle();
    void updateActionStates();
    bool confirmSaveIfModified();
    QStringList portraitCandidates(const QString& reference) const;

    DialogueDocument document;
    QString filePath;
    QString assetsBasePath;
    QString selectedSection;
    int selectedLineRow = -1;
    QString callerScriptPath;
    int callerLine = 0;
    int callerColumn = 0;
    std::function<bool(const QString&, const QString&)>
        documentPathValidator;
    bool refreshing = false;

    QToolBar* toolBar = nullptr;
    QAction* saveAction = nullptr;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* playtestAction = nullptr;
    QAction* returnAction = nullptr;
    QUndoStack* undoStack = nullptr;

    QLineEdit* searchEdit = nullptr;
    QListWidget* sectionList = nullptr;
    QListWidget* lineList = nullptr;
    QLabel* sectionSummaryLabel = nullptr;
    QLabel* preservationLabel = nullptr;
    QLineEdit* speakerEdit = nullptr;
    QPlainTextEdit* textEdit = nullptr;
    QComboBox* portraitModeCombo = nullptr;
    QComboBox* portraitReferenceCombo = nullptr;
    MpcPreviewLabel* portraitPreview = nullptr;
    QLabel* callerHintLabel = nullptr;
};
