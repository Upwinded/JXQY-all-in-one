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
    explicit MenuEditorWindow(QWidget* parent = nullptr);
    ~MenuEditorWindow();

    bool openMenuDefinition(const QString& filePath);
    void newMenuDefinition();
    bool saveMenuDefinitionAs(const QString& filePath);
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

protected:
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
    void onSaveAsFile();
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
    void setupToolBar();
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
                                  const QString& componentType);
    void clearModifiedOwner(const QString& ownerId);
    QString selectedOwnerId() const;
    void markSubMenuModified(int index);
    QByteArray currentSnapshotFingerprint();
    QByteArray snapshotFingerprint(const EditorSnapshot& snapshot) const;
    EditorSnapshot createSnapshot();
    void restoreSnapshot(const EditorSnapshot& snapshot);
    void resetHistory();
    void updateUndoRedoState();
    void establishSavePoint();
    void markModified();
    bool canAdoptDocumentPath(const QString& targetPath) const;

    Ui::MenuEditorWindow* ui;

    MenuPreviewCanvas* previewCanvas = nullptr;
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
    QString assetsCollectionPath;
    QStringList uiReadRoots;
    bool activeWriteRootLocked = false;
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
    QByteArray savedSnapshotFingerprint;
    bool restoringHistory = false;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* newAction = nullptr;
    QAction* openAction = nullptr;
    QAction* saveAction = nullptr;
    QAction* saveAsAction = nullptr;
    QAction* refreshAction = nullptr;
    DocumentPathValidator documentPathValidator;
};
