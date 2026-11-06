#pragma once

#include <QWidget>
#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"
#include <QTreeWidgetItem>
#include <QAction>
#include <QCloseEvent>
#include <QByteArray>
#include <QSet>
#include <QStringList>
#include <functional>

#include "MenuPreviewCanvas.h"
#include "ComponentPropertyEditor.h"
#include "AssetBrowser.h"
#include "../core/ProjectDocumentRegistry.h"
#include "../core/MenuPageContext.h"

struct MenuComponentDefinition
{
    QString editorId;
    QString type;
    QString name;
    QString file;
    QString bind;
    QString format;
    QString controllerUp;
    QString controllerDown;
    QString controllerLeft;
    QString controllerRight;
    QStringList originalLines;
    QString optionLayout;
    QString spacingY;
    bool runtimeDefault = false;
};

struct SubMenuDefinition
{
    QString editorId;
    QString name;
    QString file;
    QList<MenuComponentDefinition> components;
    QString windowFile;
    QRect windowRect;
    QString backgroundImage;
    bool windowStretch = false;
    QList<PreviewComponent> previewComponents;
};

struct MenuDefinition
{
    QString menuName;
    bool visible = false;
    QString windowFile;
    QString backgroundImage;
    QRect windowRect;
    QList<MenuComponentDefinition> components;
    QList<SubMenuDefinition> subMenus;
};

namespace Ui
{
class MenuEditorWindow;
}
class QComboBox;
class QCheckBox;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QShowEvent;
class QPlainTextEdit;

enum class SelectedItemType
{
    None,
    MenuWindow,
    Component,
    SubComponent,
    SubMenu,
    SubMenuComponent
};

class MenuEditorWindow : public QWidget,
                         public AssetsPathSwitchParticipant,
                         public CloseTransactionParticipant
{
    Q_OBJECT

public:
    struct BagGridSettings
    {
        int rows = 3;
        int columns = 3;
        int left = 0;
        int top = 0;
        int width = 32;
        int height = 32;
        int horizontalGap = 0;
        int verticalGap = 0;
        bool horizontalGapKnown = false;
        bool verticalGapKnown = false;
    };
    bool readBagGrid(BagGridSettings& settings);
    bool applyBagGrid(const BagGridSettings& settings, QString* error = nullptr);
    explicit MenuEditorWindow(QWidget* parent = nullptr);
    ~MenuEditorWindow();

    bool openMenuDefinition(const QString& filePath);
    bool saveMenuDefinitionAs(const QString& filePath);
    bool selectPage(const QString& page);
    bool saveCurrentPage();
    QString currentPage() const { return selectedPage; }
    QString pageSourcePath() const { return currentFilePath; }
    QString pageTargetPath() const;
    QString readOnlyReason() const { return pageRestriction; }
    bool setAssetsBasePath(const QString& path);
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
    void openScriptEditorRequested();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAddComponent();
    void onAddSubMenu();
    void onRemoveComponent();
    void onComponentSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onPropertyChanged();
    void onDefinitionPropertyChanged();
    void onResourceFileEditingFinished();
    void onSaveFile();
    void onPreviewRefresh();
    void onUndo();
    void onRedo();
    void onComponentMovedUp();
    void onComponentMovedDown();
    void onPreviewComponentSelected(const QString& componentName);
    void onPreviewComponentMoved(const QString& componentName, int newX, int newY);
    void onPreviewComponentResized(const QString& componentName, int newX, int newY, int newWidth, int newHeight);
    void onPreviewComponentPropertyChanged(const QString& componentName, int left, int top, int width, int height);
    void onAssetFileSelected(const QString& relativePath);
    void onAssetFileDoubleClicked(const QString& relativePath);

private:
    void prepareSkillsControls();
    QSet<QString> skillsFields(const QString& owner) const;
    void applySkillsProperties(const QString& resource, ComponentIniProperties& value) const;
    bool validateSkills(QString& error);
    bool stageSkillsFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& value);
    QStringList skillsPreviewStates() const;
    void updateSkillsPreview(QList<PreviewComponent>& components);
    QSet<QString> joystickFields(const QString& owner) const;
    void applyJoystickIntegers(ComponentIniProperties& value) const;
    bool validateJoystick(QString& error);
    bool stageJoystickFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& value);
    QStringList joystickPreviewStates() const;
    void updateJoystickPreview(QList<PreviewComponent>& components);
    bool loadDicePage(const QString& path);
    ComponentIniProperties diceProperties() const;
    void prepareDiceProperties();
    bool validateDicePage(QString& error);
    bool saveDiceDocument(const QString& path, bool updatePath);
    void updateDiceTree();
    void updateDicePropertyEditor();
    void applyDicePropertyEdit();
    void updateDicePreview();
    friend struct MenuDiceTestAccess;
    QSet<QString> controllerHelpFields() const;
    void recordControllerHelpDiskState();
    bool controllerHelpDiskMatches() const;
    void prepareControllerHelpDefaults();
    bool stageControllerHelpProperties(const ComponentIniProperties& proposed);
    bool validateControllerHelp(QString& error);
    QStringList controllerHelpPreviewStates() const;
    void updateControllerHelpPreview(QList<PreviewComponent>& components);
    QMap<QString,QPair<bool,QByteArray>> controllerHelpDiskState;
    friend struct MenuNpcInfoTestAccess;
    friend struct MenuControllerHelpTestAccess;
    ComponentIniProperties npcInfoProperties();
    void prepareNpcInfoDefaults();
    bool loadFixedWindowPage(const QString& path);
    bool stageNpcInfoProperties(const ComponentIniProperties& proposed);
    bool validateNpcInfo(QString& error);
    bool saveFixedWindowDocument(const QString& path, bool updatePath);
    QStringList npcInfoPreviewStates() const;
    void updateNpcInfoPreview(QList<PreviewComponent>& components);
    friend struct MenuNoticeTestAccess;
    friend struct MenuHudTestAccess;
    static bool isNoticePage(const QString& page);
    QSet<QString> noticeFields(const QString& owner) const;
    ComponentIniProperties noticeWindowProperties();
    void prepareNoticeDefaults();
    bool validateNotice(QString& error);
    bool validNoticeValue(const QString& key, const QString& text) const;
    bool stageNoticeFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& value);
    QStringList noticePreviewStates() const;
    void updateNoticePreview(QList<PreviewComponent>& components);
    static bool isHudPage(const QString& page);
    QSet<QString> hudFields(const QString& owner) const;
    bool validateHud(QString& error);
    bool stageFixedDisplayFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& value);
    QStringList hudPreviewStates() const;
    void updateHudPreview(QList<PreviewComponent>& components);
    QSet<QString> mapThumbnailFields(const QString& owner) const;
    bool validateMapThumbnail(QString& error);
    bool stageMapThumbnailFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& value);
    bool mapThumbnailCanvasIsEditable(const QString& owner) const;
    QStringList mapThumbnailPreviewStates() const;
    void updateMapThumbnailPreview(QList<PreviewComponent>& components);
    friend struct MenuMapThumbnailTestAccess;
    QString pageResource(const QString& page) const;
    QString pageDisplayName(const QString& page) const;
    QString resolvedPageSource(const QString& page) const;
    QString documentPage(const QString& filePath) const;
    void refreshPageList();
    void updatePagePermissions();
    bool validatePageSave(const QString& target, QString& error);
    QSet<QString> editableFields(const QString& owner) const;
    bool optionAutomaticSaveAvailable() const;
    QMap<QString, ComponentIniProperties> optionDefaultProperties(bool controllerPresent);
    void prepareOptionDefaults();
    bool loadYesNoFallback(const QString& filePath);
    QSet<QString> systemPageFields(const QString& owner) const;
    bool validateSystemPage(QString& error) const;
    QSet<QString> storagePageFields(const QString& owner) const;
    bool validateStoragePage(QString& error);
    bool validateStorageLabels(QString& error) const;
    static bool validMenuBindingFormat(QString format, const QString& binding);
    void updateStoragePreview(QList<PreviewComponent>& components);
    QStringList storagePreviewStates();
    QComboBox* saveLoadContextCombo = nullptr;
    QAction* saveLoadContextAction = nullptr;
    bool saveLoadAtTitle = false;
    void prepareTitleTeamDefaults();
    static unsigned int displayColor(const QString& text, unsigned int fallback);
    void updateTitleTeamPreview(QList<PreviewComponent>& components);
    void setupConversationTools();
    friend struct MenuConversationTestAccess;
    friend struct MenuPassiveTestAccess;
    static QString conversationPreviewPage(const QString& text, int charactersPerPage, bool lastPage);
    QStringList conversationPreviewStates() const;
    QSet<QString> conversationFields(const QString& owner) const;
    bool conversationCanvasIsEditable() const;
    bool stageConversationFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& proposed);
    void updateConversationPreview(QList<PreviewComponent>& components);
    QSet<QString> passiveFields(const QString& owner) const;
    QStringList passivePreviewStates() const;
    void updatePassivePreview(QList<PreviewComponent>& components, const QRect& panel, const QSize& viewport);
    static QRect previewPanelRectangle(const ComponentIniProperties& window, const QSize& viewport);
    QSet<QString> titleTeamFields(const QString& owner) const;
    bool validateTitleTeam(QString& error);
    bool stageTitleTeamFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& properties);
    void preparePartnerHeadDefaults();
    void setupPartnerHeadTools();
    void updatePartnerHeadPreview(QList<PreviewComponent>& components);
    QComboBox* partnerHeadPreview = nullptr;
    class QSpinBox* partnerHeadSpacing = nullptr;
    QAction* partnerHeadPreviewAction = nullptr;
    QAction* partnerHeadSpacingAction = nullptr;
    void adjustOptionPreview(const MenuComponentDefinition& definition, PreviewComponent& component);
    ComponentIniProperties allowedProperties(const QString& owner,
        const ComponentIniProperties& original, const ComponentIniProperties& proposed) const;
    bool canEditPage() const;
    bool effectiveQingyuTheme() const;
    QByteArray pageContextFingerprint() const;
    bool canEditGeometry(const QString& owner, bool resizing) const;
    void clearPageDocument();
    void setupToolBar();
    void setupPreviewControls();
    void finishPreviewPresentation();
    void schedulePreviewFit();
    QSize previewWindowSize() const;
    void editBagGrid();
    bool isGoodsMenu() const;
    bool isToolTipMenu() const;
    bool validateKnownMenu(QString& error);
    void retranslateDynamicUi();
    void updateComponentTree();
    void updatePropertyEditor();
    void updatePreview();
    bool loadFromMenuFile(const QString& filePath);
    QStringList saveToMenuFile(const QString& filePath);
    void readComponentIniProperties(const QString& iniPath, ComponentIniProperties& props);
    bool writeComponentIniProperties(const QString& iniPath, const ComponentIniProperties& props,
                                     const QSet<QString>& componentTypes,
                                     const QString& readPath = "");
    void selectComponentByIndex(int index);
    void loadSubMenuDefinition(SubMenuDefinition& subMenu);
    bool saveSubMenuDefinition(const SubMenuDefinition& subMenu, const QString& outputPath = "");

    struct ModifiedComponentIni
    {
        ComponentIniProperties properties;
        QString resourcePath;
        QString sourcePath;
        QSet<QString> owners;
        QMap<QString, QString> ownerComponentTypes;
        // History snapshots also keep effective, read-only property overlays so
        // undo can cross a save point after the on-disk file has changed.  Only
        // entries explicitly edited in the current snapshot may be written.
        bool stagedForWrite = false;
    };

    struct FileTransaction
    {
        QString target;
        QString temp;
    };

    struct EditorSnapshot
    {
        MenuDefinition definition;
        QMap<QString, ModifiedComponentIni> modifiedInis;
        QSet<QString> modifiedSubMenus;
    };

    struct ExternalResourceReferences
    {
        QSet<QString> paths;
        QMap<QString, QSet<QString>> sources;
        bool collected = false;
        bool complete = true;
    };

    bool saveDocumentToPath(const QString& filePath, bool updateCurrentPath);
    bool validateControllerNavigationOverrides(QString& error) const;
    bool commitTransactions(QList<FileTransaction>& transactions);
    bool prepareTransaction(const QString& targetPath, FileTransaction& transaction,
                            QSet<QString>& targetKeys, QString& error) const;
    void cleanupTransactionTemps(const QList<FileTransaction>& transactions) const;
    void rebuildResourceContext(const QString& preferredFilePath = QString());
    bool resolveLocalResourcePath(const QString& resourcePath, QString& absolutePath) const;
    bool resolveReadableResourcePath(const QString& resourcePath, QString& absolutePath) const;
    bool resourcePathForFile(const QString& filePath, QString& resourcePath) const;
    ComponentIniProperties propertiesForResource(const QString& resourcePath);
    bool stageComponentProperties(const QString& ownerId, const QString& resourcePath,
                                  const ComponentIniProperties& properties,
                                  const QString& componentType, bool forceCopy = false,
                                  ExternalResourceReferences* externalReferences = nullptr);
    ExternalResourceReferences collectExternalResourceReferences() const;
    bool canReuseComponentResource(const QString& ownerId, const QString& resourcePath,
                                   ExternalResourceReferences* externalReferences = nullptr) const;
    void clearModifiedOwner(const QString& ownerId);
    QString selectedOwnerId() const;
    void markSubMenuModified(int index);
    QByteArray currentSnapshotFingerprint();
    QByteArray snapshotFingerprint(const EditorSnapshot& snapshot) const;
    EditorSnapshot createSnapshot();
    void restoreSnapshot(const EditorSnapshot& snapshot);
    void resetHistory();
    void updateUndoRedoState();
    void establishSavePoint(const QSet<QString>& writtenComponentPaths);
    void markModified();
    bool canAdoptDocumentPath(const QString& targetPath) const;

    Ui::MenuEditorWindow* ui;

    MenuPreviewCanvas* previewCanvas = nullptr;
    QComboBox* previewModeCombo = nullptr;
    QSpinBox* previewWidthSpin = nullptr;
    QSpinBox* previewHeightSpin = nullptr;
    QDoubleSpinBox* previewZoomSpin = nullptr;
    QComboBox* previewButtonState = nullptr;
    QSpinBox* previewSliderPosition = nullptr;
    QPlainTextEdit* choicePreviewText = nullptr;
    QWidget* choicePreviewControls = nullptr;
    QWidget* previewSizeControls = nullptr;
    QPoint previewInterfaceOrigin;
    bool previewAutoFit = true;
    bool previewFitPending = false;
    ComponentPropertyEditor* propertyEditor = nullptr;
    AssetBrowser* assetBrowser = nullptr;

    MenuDefinition currentDefinition;
    QMap<QString, ModifiedComponentIni> modifiedComponentInis;
    QMap<QString, ComponentIniProperties> componentIniCache;
    QSet<QString> modifiedSubMenuIds;
    bool hasUnsavedChanges = false;
    QString currentFilePath;
    QString assetsBasePath;
    QString configuredAssetsBasePath;
    MenuPageContext pageResources;
    QString selectedPage;
    QString requestedPage;
    QString selectedTheme = "original";
    QString pageRestriction;
    QString pageEntryResource;
    QString documentStructureIssue;
    QByteArray documentContextFingerprint;
    bool applyingBagGrid = false;
    bool pageSwitchConfirmed = false;
    QComboBox* pageCombo = nullptr;
    QComboBox* themeCombo = nullptr;
    QLabel* pageContextLabel = nullptr;
    bool resourceRecoveryBlocked = false;
    QStringList resourceRecoveryErrors;
    int selectedComponentIndex = -1;
    int selectedSubMenuIndex = -1;
    int selectedSubMenuComponentIndex = -1;
    QString selectedSubComponentName;
    QString selectedSubComponentPath;
    SelectedItemType selectedItemType = SelectedItemType::None;
    bool updatingFromCode = false;
    QStringList originalMenuFileLines;
    QList<EditorSnapshot> history;
    int historyIndex = -1;
    QMap<QString, ComponentIniProperties> lastWrittenComponentProperties;
    QByteArray savedSnapshotFingerprint;
    bool restoringHistory = false;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* saveAction = nullptr;
    QAction* refreshAction = nullptr;
    QAction* bagGridAction = nullptr;
    QComboBox* optionViewCombo = nullptr;
    QAction* optionViewAction = nullptr;
    QCheckBox* optionControllerPreview = nullptr;
    QAction* optionControllerAction = nullptr;
    int optionDefaultHeight = 0;
    QComboBox* toolTipPreviewCombo = nullptr;
    QAction* toolTipPreviewAction = nullptr;
    QLabel* editStatusLabel = nullptr;
    DocumentPathValidator documentPathValidator;
};
