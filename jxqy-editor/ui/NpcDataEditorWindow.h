#pragma once

#include <QWidget>
#include <QByteArray>
#include <QMap>
#include <QList>
#include <QStringList>
#include <QCloseEvent>
#include <functional>
#include <memory>
#include <utility>
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"
#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/ProjectDocumentRegistry.h"

namespace Ui
{
class NpcDataEditorWindow;
}

class INIFileEditor;
class MpcImageCache;
class MpcPreviewLabel;
class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QFormLayout;
class QWidget;

struct NpcData
{
    int sectionId = -1;
    QMap<QString, QString> properties;
};

struct ObjectData
{
    int sectionId = -1;
    QMap<QString, QString> properties;
};

class NpcDataEditorWindow : public QWidget,
                            public AssetsPathSwitchParticipant,
                            public CloseTransactionParticipant
{
    Q_OBJECT

public:
    explicit NpcDataEditorWindow(QWidget* parent = nullptr);
    ~NpcDataEditorWindow();

    bool openFile(const QString& fileName);
    bool openNpcFile(const QString& fileName, bool reportLoadErrors = true);
    bool openObjectFile(const QString& fileName, bool reportLoadErrors = true);
    bool openNpcResourceFile(const QString& fileName,
                             bool reportLoadErrors = true);
    bool saveNpcFileAs(const QString& fileName);
    bool saveObjectFileAs(const QString& fileName);
    bool saveNpcResourceFileAs(const QString& fileName);
    static bool isNpcResourceFilePath(const QString& fileName);
    bool setAssetsBasePath(const QString& path);
    QList<DesktopRunDocumentSnapshot> desktopRunDocumentSnapshots() const;
    QList<ProjectDocumentState> currentProjectDocuments() const;

    using DocumentPathValidator =
        std::function<bool(const QString& currentPath,
                           const QString& targetPath)>;
    void setDocumentPathValidator(DocumentPathValidator validator)
    {
        documentPathValidator = std::move(validator);
    }

    Decision prepareAssetsPathSwitch(const QString& path) const override;
    bool resolveAssetsPathSwitch(Decision decision) override;
    void commitAssetsPathSwitch(const QString& path) override;
    QString currentAssetsPath() const override;

    ClosePlan prepareCloseTransaction() const override;
    bool resolveCloseTransaction(const ClosePlan& plan) override;
    void commitCloseTransaction(const ClosePlan& plan) override;

    enum class SaveConfirmResult { Saved, Discarded, Cancelled };
    SaveConfirmResult confirmSaveIfModified();

signals:
    void documentStatesChanged();
    void documentClosed();
    void editDialogueFromNpcRequested(
        const QString& scriptPath, const QString& npcName);

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpen();
    void onSave();
    void onSaveAs();
    void onAddEntry();
    void onRemoveEntry();
    void onMoveUp();
    void onMoveDown();
    void onDuplicateEntry();
    void onNpcSelectionChanged();
    void onObjectSelectionChanged();
    void onEntityTabChanged(int index);
    void onNpcPropertyChanged();
    void onObjectPropertyChanged();
    void onNpcResourceSelectionChanged();
    void onNpcResourcePropertyChanged();
    void onNpcResourceExtraValueChanged(int row, int column);
    void onAddNpcResourceExtraKey();
    void onRemoveNpcResourceExtraKey();
    void onEditDialogueFromNpc();

private:
    void initializeCombos();
    void retranslateDynamicUi();
    bool loadNpcFromFile(const QString& fileName, bool reportLoadErrors);
    bool loadObjectFromFile(const QString& fileName, bool reportLoadErrors);
    bool loadNpcResourceFromFile(const QString& fileName,
                                 bool reportLoadErrors);
    bool saveAllModified();
    bool saveNpcToFile(const QString& fileName, bool updateBaseline = true,
                       QStringList* savedLines = nullptr);
    bool saveNpcToFilePreserving(const QString& fileName,
                                 bool updateBaseline = true,
                                 QStringList* savedLines = nullptr);
    bool saveObjectToFile(const QString& fileName, bool updateBaseline = true,
                          QStringList* savedLines = nullptr);
    bool saveObjectToFilePreserving(const QString& fileName,
                                    bool updateBaseline = true,
                                    QStringList* savedLines = nullptr);
    bool saveNpcResourceToFile(const QString& fileName,
                               bool updateBaseline = true,
                               QByteArray* savedData = nullptr);
    QStringList serializeNpcLines(
        const QList<NpcData>& entries) const;
    QStringList serializeNpcLinesPreserving(
        const QList<NpcData>& entries) const;
    QStringList serializeObjectLines(
        const QList<ObjectData>& entries) const;
    QStringList serializeObjectLinesPreserving(
        const QList<ObjectData>& entries) const;
    bool normalizedNpcEntriesForSave(
        const QList<NpcData>& sourceEntries,
        QList<NpcData>& normalizedEntries,
        int& invalidRow,
        QString& invalidFieldKey,
        QString& invalidValue) const;
    bool normalizedObjectEntriesForSave(
        const QList<ObjectData>& sourceEntries,
        QList<ObjectData>& normalizedEntries,
        int& invalidRow,
        QString& invalidFieldKey,
        QString& invalidValue) const;
    bool validateAndNormalizeNpcResourceDocument();
    void setupNpcResourceEditor();
    void refreshNpcResourceSections();
    void showNpcResourceSection(int index);
    void updateNpcResourcePreview();
    QStringList npcResourceImageCandidates(const QString& imageName) const;
    QString currentNpcResourceSection() const;
    bool addNpcResourceSection(const QString& sectionName,
                               const QString& copyFromSection = QString());
    bool removeCurrentNpcResourceSection();
    void refreshNpcList();
    void refreshObjectList();
    void normalizeNpcSectionIds();
    void normalizeObjectSectionIds();
    void showNpcProperties(int index);
    void showObjectProperties(int index);
    void collectNpcProperties();
    void collectObjectProperties();
    bool normalizeNpcResourceReferencesForSave();
    bool normalizeObjectResourceReferencesForSave();
    bool canAdoptDocumentPath(const QString& currentPath,
                              const QString& targetPath) const;
    void updateOverallDirtyStateAndNotify();
    void updatePropertyTabVisibility();
    QString currentNpcDialogueScriptPath() const;

    Ui::NpcDataEditorWindow* ui;

    QList<NpcData> npcEntries;
    QList<ObjectData> objectEntries;
    QStringList originalNpcLines;
    QStringList originalObjectLines;

    QString currentNpcFilePath;
    QString currentNpcMapName;
    QString currentObjectFilePath;
    QString currentNpcResourceFilePath;
    bool isNpcFile = false;
    bool isObjectFile = false;
    bool isNpcResourceFile = false;
    bool updatingFromCode = false;
    bool hasNpcUnsavedChanges = false;
    bool hasObjectUnsavedChanges = false;
    bool hasNpcResourceUnsavedChanges = false;
    bool hasUnsavedChanges = false;
    int currentNpcEditRow = -1;
    int currentObjectEditRow = -1;
    int nextNpcSectionId = 0;
    int nextObjectSectionId = 0;
    QString assetsBasePath;
    DocumentPathValidator documentPathValidator;

    std::unique_ptr<INIFileEditor> npcResourceDocument;
    std::unique_ptr<MpcImageCache> npcResourceImageCache;
    QStringList npcResourceSectionOrder;
    QWidget* npcResourceListTab = nullptr;
    QListWidget* npcResourceSectionList = nullptr;
    QWidget* npcResourcePropertyTab = nullptr;
    QLabel* npcResourceSectionLabel = nullptr;
    QLabel* npcResourceStateLabel = nullptr;
    QLabel* npcResourceListHint = nullptr;
    QLabel* npcResourceExtraLabel = nullptr;
    QLabel* npcResourceFooterHint = nullptr;
    QFormLayout* npcResourceForm = nullptr;
    QLineEdit* npcResourceImageEdit = nullptr;
    QLineEdit* npcResourceShadeEdit = nullptr;
    QLineEdit* npcResourceSoundEdit = nullptr;
    QTableWidget* npcResourceExtraTable = nullptr;
    QPushButton* addNpcResourceExtraButton = nullptr;
    QPushButton* removeNpcResourceExtraButton = nullptr;
    MpcPreviewLabel* npcResourcePreview = nullptr;
    QLabel* npcResourceMetadataLabel = nullptr;
    QAction* editDialogueAction = nullptr;
};
