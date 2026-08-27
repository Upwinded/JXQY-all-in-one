#pragma once

#include "AssetsPathSwitchParticipant.h"
#include "CloseTransactionParticipant.h"

#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/GoodsShopDocument.h"
#include "../core/ProjectDocumentRegistry.h"

#include <QWidget>

#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QToolBar;
class QUndoStack;
class MpcPreviewLabel;

enum class GoodsShopDocumentKind
{
    None,
    Goods,
    Shop
};

class GoodsShopEditorWindow : public QWidget,
                              public CloseTransactionParticipant,
                              public AssetsPathSwitchParticipant
{
    Q_OBJECT

public:
    explicit GoodsShopEditorWindow(QWidget* parent = nullptr);
    ~GoodsShopEditorWindow() override;

    static bool isGoodsFilePath(const QString& filePath);
    static bool isShopFilePath(const QString& filePath);
    static bool isSupportedFilePath(const QString& filePath);

    bool openFile(const QString& filePath);
    bool saveFile();
    bool saveAsFile(const QString& filePath);
    bool duplicateCurrentGoodsTo(const QString& targetPath,
                                 QString* errorMessage = nullptr);
    bool hasUnsavedChanges() const;
    QString currentFilePath() const;
    QString displayName() const;
    GoodsShopDocumentKind documentKind() const;

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
    void openGoodsShopFileRequested(const QString& filePath);
    void playtestRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void setupActions();
    void setupConnections();
    QWidget* createCatalogPanel();
    QWidget* createGoodsPage();
    QWidget* createShopPage();
    QWidget* createGoodsImageCard(const QString& title,
                                  GoodsTextField field,
                                  MpcPreviewLabel*& preview,
                                  QLineEdit*& edit);

    bool loadDocumentBytes(const QByteArray& bytes);
    QByteArray currentDocumentBytes() const;
    void pushSnapshotChange(const QByteArray& before,
                            const QByteArray& after,
                            const QString& description);
    void pushGoodsTextChange(GoodsTextField field, const QString& value,
                             const QString& description);
    void pushGoodsIntegerChange(GoodsIntegerField field, int value,
                                const QString& description);
    void pushShopMutation(const QString& description,
                          const std::function<bool()>& mutation);
    void commitPendingGoodsEditors();

    void refreshFromDocument();
    void refreshGoodsPage();
    void refreshShopPage();
    void refreshCatalogLists();
    void filterGoodsList();
    void filterShopList();
    void refreshGoodsPreviews();
    void refreshShopSelectionPreview();
    void updateWindowTitle();
    void updateActionStates();
    void updateShopButtons();
    bool confirmSaveIfModified();
    void browseGoodsResource(GoodsTextField field);
    void duplicateCurrentGoods();
    void addSelectedGoodsToShop();
    QString goodsPathForReference(const QString& iniFile) const;
    QStringList imageCandidates(const QString& reference) const;
    void showPreview(MpcPreviewLabel* label, const QString& reference);
    QLineEdit* goodsLineEdit(GoodsTextField field) const;
    QSpinBox* goodsSpinBox(GoodsIntegerField field) const;
    void retranslateDynamicUi();

    GoodsDocument goodsDocument;
    ShopDocument shopDocument;
    GoodsShopDocumentKind kind = GoodsShopDocumentKind::None;
    QString filePath;
    QString assetsBasePath;
    std::function<bool(const QString&, const QString&)>
        documentPathValidator;
    bool refreshing = false;

    QToolBar* toolBar = nullptr;
    QAction* saveAction = nullptr;
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    QAction* duplicateAction = nullptr;
    QAction* playtestAction = nullptr;
    QUndoStack* undoStack = nullptr;

    QLineEdit* goodsSearchEdit = nullptr;
    QListWidget* goodsList = nullptr;
    QLineEdit* shopSearchEdit = nullptr;
    QListWidget* shopList = nullptr;
    QPushButton* addSelectedGoodsButton = nullptr;

    QLabel* fileSummaryLabel = nullptr;
    QLabel* preservationLabel = nullptr;
    QStackedWidget* editorStack = nullptr;
    QWidget* goodsPage = nullptr;
    QWidget* shopPage = nullptr;

    QLineEdit* goodsNameEdit = nullptr;
    QComboBox* goodsKindCombo = nullptr;
    QPlainTextEdit* goodsIntroductionEdit = nullptr;
    QLineEdit* goodsEffectEdit = nullptr;
    QSpinBox* goodsCostSpin = nullptr;
    QSpinBox* goodsSellPriceSpin = nullptr;
    QLabel* goodsPriceHint = nullptr;
    QLineEdit* goodsImageEdit = nullptr;
    QLineEdit* goodsIconEdit = nullptr;
    MpcPreviewLabel* goodsImagePreview = nullptr;
    MpcPreviewLabel* goodsIconPreview = nullptr;
    QStackedWidget* goodsEffectStack = nullptr;
    QSpinBox* goodsLifeSpin = nullptr;
    QSpinBox* goodsThewSpin = nullptr;
    QSpinBox* goodsManaSpin = nullptr;
    QComboBox* goodsPartCombo = nullptr;
    QSpinBox* goodsLifeMaximumSpin = nullptr;
    QSpinBox* goodsThewMaximumSpin = nullptr;
    QSpinBox* goodsManaMaximumSpin = nullptr;
    QSpinBox* goodsAttackSpin = nullptr;
    QSpinBox* goodsDefendSpin = nullptr;
    QSpinBox* goodsEvadeSpin = nullptr;

    QCheckBox* shopStockLimitedCheck = nullptr;
    QSpinBox* shopBuyPercentSpin = nullptr;
    QSpinBox* shopRecyclePercentSpin = nullptr;
    QTableWidget* shopItemsTable = nullptr;
    QPushButton* shopRemoveButton = nullptr;
    QPushButton* shopMoveUpButton = nullptr;
    QPushButton* shopMoveDownButton = nullptr;
    QLabel* shopItemSummaryLabel = nullptr;
    MpcPreviewLabel* shopItemImagePreview = nullptr;
    MpcPreviewLabel* shopItemIconPreview = nullptr;
};
