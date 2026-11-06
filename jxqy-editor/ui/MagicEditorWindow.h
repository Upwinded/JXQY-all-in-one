#pragma once

#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"

#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/MagicDocument.h"
#include "../core/ContentAuthoring.h"
#include "../core/ProjectDocumentRegistry.h"

#include <QHash>
#include <QWidget>

#include <functional>

class QAction;
class QAudioOutput;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMediaPlayer;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolBar;
class QToolButton;
class QUndoStack;
class MagicRangePreview;
class MpcPreviewLabel;

class MagicEditorWindow : public QWidget,
                          public CloseTransactionParticipant,
                          public AssetsPathSwitchParticipant
{
    Q_OBJECT

public:
    explicit MagicEditorWindow(QWidget* parent = nullptr);
    ~MagicEditorWindow() override;

    static bool isMagicFilePath(const QString& filePath);

    bool openFile(const QString& filePath);
    bool saveFile();
    bool saveAsFile(const QString& filePath);
    bool hasUnsavedChanges() const;
    QString currentFilePath() const;
    QString displayName() const;
    int selectedLevel() const;
    QString createMagicFile(const QString& name, bool copyCurrent,
                            QString* errorMessage = nullptr);
    bool renameMagicFile(const QString& newFileName, QString* errorMessage = nullptr);
    bool deleteMagicFile(QString* errorMessage = nullptr);
    ContentReferenceReport currentReferences() const;
    void setDocumentSnapshotsProvider(
        std::function<QList<DesktopRunDocumentSnapshot>()> provider);
    void setFileMutationPreflight(
        std::function<bool(const QStringList&, QString&)> preflight);

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
    void openMagicFileRequested(const QString& filePath);
    void playtestRequested();
    void filesChanged(const QStringList& paths);
    void openReferenceRequested(const QString& path, int line);
    void learningScriptRequested(const QString& source);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupActions();
    void setupConnections();
    QWidget* createOverviewPage();
    QWidget* createLevelsPage();
    QWidget* createLinkedEffectsPage();
    void refreshLinkedEffects();
    void retranslateLinkedEffects();
    void appendLinkedEdits(MagicDocument& pending) const;
    bool linkedInputsValid() const;
    QString linkedEditorValue(const QString& key) const;
    QWidget* createResourceCard(
        const QString& title,
        MagicTextField field,
        const QString& category,
        MpcPreviewLabel*& preview,
        QLineEdit*& lineEdit);
    QWidget* createSoundRow(
        MagicTextField field,
        const QString& label,
        QLineEdit*& lineEdit);

    bool loadDocumentBytes(const QByteArray& bytes);
    void pushTextChange(MagicTextField field, const QString& value,
                        const QString& description);
    void pushIntegerChange(MagicIntegerField field, int level, int value,
                           const QString& description);
    void pushSnapshotChange(const QByteArray& before,
                            const QByteArray& after,
                            const QString& description);
    void commitPendingTextEditors();
    QByteArray currentEditorBytes() const;
    void refreshFromDocument();
    void refreshLevelTable();
    void refreshLevelEffectControls();
    void refreshAttackFileChoices();
    void refreshRangePreview();
    void refreshResourcePreviews();
    void refreshDocumentList();
    void filterDocumentList();
    void updateWindowTitle();
    void updateActionStates();
    bool confirmSaveIfModified();
    void browseResource(MagicTextField field, const QString& category,
                        const QString& filter);
    void playSound(MagicTextField field);
    QStringList imageCandidates(const QString& reference,
                                const QString& category) const;
    QString resolveSoundPath(const QString& reference) const;
    MpcPreviewLabel* previewForField(MagicTextField field) const;
    QLineEdit* lineEditForField(MagicTextField field) const;
    void retranslateDynamicUi();
    void setupAuthoringActions();
    void setupAuthoringMenu();
    void retranslateAuthoringActions();
    void showReferences();
    bool authoringTarget(const QString& target, QString& error) const;
    bool mutationReady(const QStringList& paths, QString& error) const;

    MagicDocument document;
    QString filePath;
    QString assetsBasePath;
    QString loadedSourcePath;
    QByteArray loadedBytes;
    std::function<QList<DesktopRunDocumentSnapshot>()> documentSnapshotsProvider;
    std::function<bool(const QStringList&, QString&)> fileMutationPreflight;
    std::function<bool(const QString&, const QString&)>
        documentPathValidator;
    bool refreshing = false;

    QToolBar* toolBar = nullptr;
    QAction* saveAction = nullptr;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* playtestAction = nullptr;
    QAction* newAction = nullptr;
    QAction* duplicateAction = nullptr;
    QAction* renameAction = nullptr;
    QAction* deleteAction = nullptr;
    QAction* referencesAction = nullptr;
    QAction* learningScriptAction = nullptr;
    QToolButton* moreButton = nullptr;
    QUndoStack* undoStack = nullptr;

    QLineEdit* searchEdit = nullptr;
    QListWidget* documentList = nullptr;
    QLabel* fileSummaryLabel = nullptr;
    QLabel* preservationLabel = nullptr;
    QLineEdit* nameEdit = nullptr;
    QPlainTextEdit* introductionEdit = nullptr;
    QComboBox* moveKindCombo = nullptr;
    QComboBox* regionCombo = nullptr;
    QComboBox* specialKindCombo = nullptr;
    QComboBox* alphaBlendCombo = nullptr;
    QSpinBox* previewLevelSpin = nullptr;
    QSpinBox* speedSpin = nullptr;
    QSpinBox* waitFrameSpin = nullptr;
    QSpinBox* lifeFrameSpin = nullptr;
    QSpinBox* flyingLumSpin = nullptr;
    QSpinBox* vanishLumSpin = nullptr;
    QLabel* selectedLevelEffectLabel = nullptr;
    MagicRangePreview* rangePreview = nullptr;
    QTableWidget* levelTable = nullptr;
    QComboBox* linkedLevelCombo = nullptr;
    int linkedEditLevel = 0;
    QMap<QString, QWidget*> linkedEditors;
    QMap<QString, QString> loadedLinkedValues;
    QMap<QString, QPushButton*> linkedResetButtons;
    QLabel* linkedErrorLabel = nullptr;

    QLineEdit* imageEdit = nullptr;
    QLineEdit* iconEdit = nullptr;
    QLineEdit* flyingImageEdit = nullptr;
    QLineEdit* vanishImageEdit = nullptr;
    QLineEdit* flyingSoundEdit = nullptr;
    QLineEdit* vanishSoundEdit = nullptr;
    QLineEdit* superModeImageEdit = nullptr;
    QLineEdit* superModeSoundEdit = nullptr;
    QLineEdit* actionFileEdit = nullptr;
    QLineEdit* useActionFileEdit = nullptr;
    QLineEdit* attackFileEdit = nullptr;
    QComboBox* attackFileCombo = nullptr;
    MpcPreviewLabel* imagePreview = nullptr;
    MpcPreviewLabel* iconPreview = nullptr;
    MpcPreviewLabel* flyingImagePreview = nullptr;
    MpcPreviewLabel* vanishImagePreview = nullptr;
    MpcPreviewLabel* superModeImagePreview = nullptr;
    MpcPreviewLabel* actionFilePreview = nullptr;
    MpcPreviewLabel* useActionFilePreview = nullptr;

    QMediaPlayer* mediaPlayer = nullptr;
    QAudioOutput* audioOutput = nullptr;
};
