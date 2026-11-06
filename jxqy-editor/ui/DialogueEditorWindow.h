#pragma once

#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"

#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/DialogueDocument.h"
#include "../core/ContentAuthoring.h"
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
    bool createSection(const QString& name, bool copyCurrent = false, QString* errorMessage = nullptr);
    bool renameSection(const QString& name, QString* errorMessage = nullptr);
    bool deleteSection(QString* errorMessage = nullptr);
    bool insertLine(int row, QString* errorMessage = nullptr);
    bool removeLine(int row, QString* errorMessage = nullptr);
    bool moveLine(int from, int to, QString* errorMessage = nullptr);
    ContentReferenceReport currentReferences() const;
    void setDocumentSnapshotsProvider(std::function<QList<DesktopRunDocumentSnapshot>()> provider);
    void setFileMutationPreflight(std::function<bool(const QStringList&, QString&)> preflight);

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
    void filesChanged(const QStringList& paths);
    void openReferenceRequested(const QString& path, int line);
    void usageScriptRequested(const QString& source);
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
    void setupAuthoringActions();
    void retranslateAuthoringActions();
    void showReferences();
    bool applyLineOrder(const std::function<bool()>& mutation, int selectedRow, QString* errorMessage);
    QByteArray pendingDocumentBytes() const;
    bool writeDocument(const QByteArray& bytes, QString& error);
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
    QString loadedSourcePath;
    QByteArray loadedBytes;
    QString assetsBasePath;
    QString selectedSection;
    int selectedLineRow = -1;
    QString callerScriptPath;
    int callerLine = 0;
    int callerColumn = 0;
    std::function<bool(const QString&, const QString&)>
        documentPathValidator;
    bool refreshing = false;
    std::function<QList<DesktopRunDocumentSnapshot>()> snapshotsProvider;
    std::function<bool(const QStringList&, QString&)> mutationPreflight;

    QToolBar* toolBar = nullptr;
    QAction* saveAction = nullptr;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* playtestAction = nullptr;
    QAction* returnAction = nullptr;
    QAction* newSectionAction = nullptr;
    QAction* copySectionAction = nullptr;
    QAction* renameSectionAction = nullptr;
    QAction* deleteSectionAction = nullptr;
    QAction* referencesAction = nullptr;
    QAction* usageScriptAction = nullptr;
    QAction* insertLineAction = nullptr;
    QAction* removeLineAction = nullptr;
    QAction* moveLineUpAction = nullptr;
    QAction* moveLineDownAction = nullptr;
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
