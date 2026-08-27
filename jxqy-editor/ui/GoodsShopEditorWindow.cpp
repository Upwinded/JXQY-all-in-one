#include "GoodsShopEditorWindow.h"

#include "FilePickerHelper.h"
#include "MpcPreviewLabel.h"
#include "../core/AssetPreviewLoader.h"
#include "../core/EditorAssetPath.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{
class GoodsShopSnapshotCommand final : public QUndoCommand
{
public:
    GoodsShopSnapshotCommand(
        QByteArray before, QByteArray after,
        std::function<void(const QByteArray&)> apply,
        const QString& description)
        : beforeBytes(std::move(before))
        , afterBytes(std::move(after))
        , applyBytes(std::move(apply))
    {
        setText(description);
    }

    void undo() override { applyBytes(beforeBytes); }
    void redo() override { applyBytes(afterBytes); }

private:
    QByteArray beforeBytes;
    QByteArray afterBytes;
    std::function<void(const QByteArray&)> applyBytes;
};

QString replacePackageExtension(const QString& path,
                                const QString& extension)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash ? path.left(dot) + extension : path + extension;
}

QString goodsFieldSuffix(GoodsTextField field)
{
    switch (field)
    {
    case GoodsTextField::Name: return QStringLiteral("Name");
    case GoodsTextField::Introduction: return QStringLiteral("Introduction");
    case GoodsTextField::Effect: return QStringLiteral("Effect");
    case GoodsTextField::Image: return QStringLiteral("Image");
    case GoodsTextField::Icon: return QStringLiteral("Icon");
    case GoodsTextField::EquipmentPart: return QStringLiteral("EquipmentPart");
    }
    return QStringLiteral("Unknown");
}

QSpinBox* createIntegerSpinBox(QWidget* parent, const QString& objectName)
{
    auto spin = new QSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setRange(0, 999999999);
    spin->setGroupSeparatorShown(true);
    return spin;
}
}

GoodsShopEditorWindow::GoodsShopEditorWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("goodsShopEditorWindow"));
    undoStack = new QUndoStack(this);
    undoStack->setObjectName(QStringLiteral("goodsShopUndoStack"));
    setupUi();
    setupActions();
    setupConnections();
    retranslateDynamicUi();
    refreshFromDocument();
}

GoodsShopEditorWindow::~GoodsShopEditorWindow()
{
    if (undoStack)
    {
        disconnect(undoStack, nullptr, this, nullptr);
        undoStack->clear();
    }
}

bool GoodsShopEditorWindow::isGoodsFilePath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(
        EditorAssetPath::normalizedAbsolutePath(path));
    return normalized.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive) &&
        normalized.contains(QStringLiteral("/ini/goods/"), Qt::CaseInsensitive);
}

bool GoodsShopEditorWindow::isShopFilePath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(
        EditorAssetPath::normalizedAbsolutePath(path));
    return normalized.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive) &&
        normalized.contains(QStringLiteral("/ini/buy/"), Qt::CaseInsensitive);
}

bool GoodsShopEditorWindow::isSupportedFilePath(const QString& path)
{
    return isGoodsFilePath(path) || isShopFilePath(path);
}

bool GoodsShopEditorWindow::openFile(const QString& requestedPath)
{
    if (requestedPath.trimmed().isEmpty())
        return false;
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    if (!isSupportedFilePath(normalized) ||
        (documentPathValidator &&
         !documentPathValidator(filePath, normalized)) ||
        !confirmSaveIfModified())
    {
        return false;
    }

    QString error;
    if (isGoodsFilePath(normalized))
    {
        GoodsDocument candidate;
        if (!candidate.openFile(normalized, &error))
        {
            QMessageBox::warning(
                this, tr("无法打开物品"),
                tr("无法读取物品文件：\n%1\n\n%2").arg(normalized, error));
            return false;
        }
        goodsDocument = std::move(candidate);
        kind = GoodsShopDocumentKind::Goods;
    }
    else
    {
        ShopDocument candidate;
        if (!candidate.openFile(normalized, &error))
        {
            QMessageBox::warning(
                this, tr("无法打开商店"),
                tr("无法读取商店货单：\n%1\n\n%2").arg(normalized, error));
            return false;
        }
        shopDocument = std::move(candidate);
        kind = GoodsShopDocumentKind::Shop;
    }

    filePath = normalized;
    undoStack->clear();
    undoStack->setClean();
    refreshCatalogLists();
    refreshFromDocument();
    emit documentStatesChanged();
    return true;
}

bool GoodsShopEditorWindow::saveFile()
{
    if (filePath.isEmpty() || kind == GoodsShopDocumentKind::None)
        return false;
    commitPendingGoodsEditors();
    QString error;
    const bool saved = kind == GoodsShopDocumentKind::Goods
        ? goodsDocument.saveFile(filePath, &error)
        : shopDocument.saveFile(filePath, &error);
    if (!saved)
    {
        QMessageBox::warning(
            this, tr("保存失败"),
            tr("无法保存当前内容：\n%1\n\n%2").arg(filePath, error));
        return false;
    }
    undoStack->setClean();
    refreshCatalogLists();
    updateWindowTitle();
    emit documentStatesChanged();
    return true;
}

bool GoodsShopEditorWindow::saveAsFile(const QString& requestedPath)
{
    if (requestedPath.trimmed().isEmpty())
        return false;
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    const bool matchingType =
        (kind == GoodsShopDocumentKind::Goods && isGoodsFilePath(normalized)) ||
        (kind == GoodsShopDocumentKind::Shop && isShopFilePath(normalized));
    if (!matchingType ||
        (documentPathValidator &&
         !documentPathValidator(filePath, normalized)))
    {
        return false;
    }
    const QString previousPath = filePath;
    filePath = normalized;
    if (!saveFile())
    {
        filePath = previousPath;
        return false;
    }
    return true;
}

bool GoodsShopEditorWindow::duplicateCurrentGoodsTo(
    const QString& requestedPath, QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    commitPendingGoodsEditors();
    if (kind != GoodsShopDocumentKind::Goods || filePath.isEmpty())
    {
        if (errorMessage)
            *errorMessage = tr("当前没有可复制的物品。");
        return false;
    }

    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(requestedPath);
    const QString goodsRoot = QDir(assetsBasePath).filePath(
        QStringLiteral("ini/goods"));
    if (!isGoodsFilePath(normalized) ||
        !EditorAssetPath::isInside(goodsRoot, normalized))
    {
        if (errorMessage)
            *errorMessage = tr("新物品必须保存到当前项目的物品目录。");
        return false;
    }
    if (QFileInfo::exists(normalized))
    {
        if (errorMessage)
            *errorMessage = tr("目标文件已经存在，请换一个文件名。");
        return false;
    }

    QString error;
    if (!goodsDocument.saveFile(normalized, &error))
    {
        if (errorMessage)
            *errorMessage = error;
        return false;
    }
    refreshCatalogLists();
    emit openGoodsShopFileRequested(normalized);
    return true;
}

bool GoodsShopEditorWindow::hasUnsavedChanges() const
{
    return !undoStack->isClean();
}

QString GoodsShopEditorWindow::currentFilePath() const
{
    return filePath;
}

QString GoodsShopEditorWindow::displayName() const
{
    if (kind == GoodsShopDocumentKind::Goods)
    {
        const QString name = goodsDocument.text(GoodsTextField::Name).trimmed();
        if (!name.isEmpty())
            return name;
    }
    return QFileInfo(filePath).completeBaseName();
}

GoodsShopDocumentKind GoodsShopEditorWindow::documentKind() const
{
    return kind;
}

void GoodsShopEditorWindow::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path.isEmpty()
        ? QString()
        : EditorAssetPath::normalizedAbsolutePath(path);
    refreshCatalogLists();
    refreshGoodsPreviews();
    refreshShopSelectionPreview();
}

void GoodsShopEditorWindow::setDocumentPathValidator(
    std::function<bool(const QString&, const QString&)> validator)
{
    documentPathValidator = std::move(validator);
}

QList<ProjectDocumentState>
GoodsShopEditorWindow::currentProjectDocuments() const
{
    if (filePath.isEmpty() || kind == GoodsShopDocumentKind::None)
        return {};
    const ProjectDocumentType type = kind == GoodsShopDocumentKind::Goods
        ? ProjectDocumentType::Goods : ProjectDocumentType::Shop;
    return {{filePath, type, hasUnsavedChanges()}};
}

DesktopRunDocumentSnapshot
GoodsShopEditorWindow::desktopRunDocumentSnapshot() const
{
    DesktopRunDocumentSnapshot snapshot;
    snapshot.filePath = filePath;
    snapshot.type = kind == GoodsShopDocumentKind::Shop
        ? ProjectDocumentType::Shop : ProjectDocumentType::Goods;
    snapshot.dirty = hasUnsavedChanges();
    snapshot.includeInOverlay = snapshot.dirty;
    snapshot.serializationSupported = !filePath.isEmpty() &&
        kind != GoodsShopDocumentKind::None;
    snapshot.bytes = currentDocumentBytes();
    if (!snapshot.serializationSupported)
        snapshot.diagnosticCode = QStringLiteral("goods-shop.document.unsaved");
    return snapshot;
}

ClosePlan GoodsShopEditorWindow::prepareCloseTransaction() const
{
    const_cast<GoodsShopEditorWindow*>(this)->commitPendingGoodsEditors();
    ClosePlan plan;
    if (!hasUnsavedChanges())
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }
    const QMessageBox::StandardButton choice = QMessageBox::question(
        const_cast<GoodsShopEditorWindow*>(this),
        tr("保存更改"),
        kind == GoodsShopDocumentKind::Goods
            ? tr("物品“%1”已修改，是否保存？").arg(displayName())
            : tr("商店“%1”已修改，是否保存？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    plan.decisions.append(
        choice == QMessageBox::Save ? CloseDecision::Save :
        choice == QMessageBox::Discard ? CloseDecision::Discard :
        CloseDecision::Cancelled);
    return plan;
}

bool GoodsShopEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    return plan.decisions.size() == 1 && !plan.isCancelled() &&
        (plan.decisions.front() != CloseDecision::Save || saveFile());
}

void GoodsShopEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() == 1 && !plan.isCancelled())
        allowPreparedClose();
}

AssetsPathSwitchParticipant::Decision
GoodsShopEditorWindow::prepareAssetsPathSwitch(const QString& path) const
{
    Q_UNUSED(path);
    const_cast<GoodsShopEditorWindow*>(this)->commitPendingGoodsEditors();
    if (!hasUnsavedChanges())
        return Decision::Ready;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        const_cast<GoodsShopEditorWindow*>(this),
        tr("切换项目资源"),
        tr("当前物品或商店已修改。切换项目资源前是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return choice == QMessageBox::Save ? Decision::Save :
        choice == QMessageBox::Discard ? Decision::Discard :
        Decision::Cancelled;
}

bool GoodsShopEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    return decision != Decision::Cancelled &&
        (decision != Decision::Save || saveFile());
}

void GoodsShopEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    setAssetsBasePath(path);
}

QString GoodsShopEditorWindow::currentAssetsPath() const
{
    return assetsBasePath;
}

void GoodsShopEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose() || confirmSaveIfModified())
    {
        emit documentClosed();
        event->accept();
        return;
    }
    event->ignore();
}

void GoodsShopEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateDynamicUi();
    QWidget::changeEvent(event);
}

bool GoodsShopEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == goodsIntroductionEdit &&
        event->type() == QEvent::FocusOut && !refreshing)
    {
        pushGoodsTextChange(
            GoodsTextField::Introduction,
            goodsIntroductionEdit->toPlainText(),
            tr("修改物品说明"));
    }
    return QWidget::eventFilter(watched, event);
}

void GoodsShopEditorWindow::setupUi()
{
    auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("goodsShopToolBar"));
    rootLayout->addWidget(toolBar);

    auto splitter = new QSplitter(this);
    splitter->setObjectName(QStringLiteral("goodsShopMainSplitter"));
    rootLayout->addWidget(splitter, 1);
    splitter->addWidget(createCatalogPanel());

    auto editorScroll = new QScrollArea(splitter);
    editorScroll->setObjectName(QStringLiteral("goodsShopEditorScroll"));
    editorScroll->setWidgetResizable(true);
    auto editorPanel = new QWidget(editorScroll);
    auto editorLayout = new QVBoxLayout(editorPanel);
    fileSummaryLabel = new QLabel(editorPanel);
    fileSummaryLabel->setObjectName(QStringLiteral("goodsShopFileSummaryLabel"));
    fileSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    editorLayout->addWidget(fileSummaryLabel);

    editorStack = new QStackedWidget(editorPanel);
    editorStack->setObjectName(QStringLiteral("goodsShopEditorStack"));
    goodsPage = createGoodsPage();
    shopPage = createShopPage();
    editorStack->addWidget(goodsPage);
    editorStack->addWidget(shopPage);
    editorLayout->addWidget(editorStack, 1);

    preservationLabel = new QLabel(editorPanel);
    preservationLabel->setObjectName(
        QStringLiteral("goodsShopPreservationLabel"));
    preservationLabel->setWordWrap(true);
    editorLayout->addWidget(preservationLabel);
    editorScroll->setWidget(editorPanel);
    splitter->addWidget(editorScroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 900});
}

QWidget* GoodsShopEditorWindow::createCatalogPanel()
{
    auto panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("goodsShopCatalogPanel"));
    panel->setMinimumWidth(250);
    panel->setMaximumWidth(420);
    auto layout = new QVBoxLayout(panel);
    auto title = new QLabel(tr("物品与商店"), panel);
    title->setObjectName(QStringLiteral("goodsShopCatalogTitle"));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto tabs = new QTabWidget(panel);
    tabs->setObjectName(QStringLiteral("goodsShopCatalogTabs"));
    auto goodsTab = new QWidget(tabs);
    auto goodsLayout = new QVBoxLayout(goodsTab);
    goodsSearchEdit = new QLineEdit(goodsTab);
    goodsSearchEdit->setObjectName(QStringLiteral("goodsSearchEdit"));
    goodsSearchEdit->setClearButtonEnabled(true);
    goodsList = new QListWidget(goodsTab);
    goodsList->setObjectName(QStringLiteral("goodsDocumentList"));
    goodsLayout->addWidget(goodsSearchEdit);
    addSelectedGoodsButton = new QPushButton(tr("加入当前商店"), goodsTab);
    addSelectedGoodsButton->setObjectName(
        QStringLiteral("shopAddSelectedGoodsButton"));
    goodsLayout->addWidget(addSelectedGoodsButton);
    auto goodsHint = new QLabel(
        tr("单击选择；双击打开物品资料。编辑商店时可把选中物品加入货单。"),
        goodsTab);
    goodsHint->setObjectName(QStringLiteral("goodsCatalogHint"));
    goodsHint->setWordWrap(true);
    goodsLayout->addWidget(goodsHint);
    goodsLayout->addWidget(goodsList, 1);

    auto shopTab = new QWidget(tabs);
    auto shopLayout = new QVBoxLayout(shopTab);
    shopSearchEdit = new QLineEdit(shopTab);
    shopSearchEdit->setObjectName(QStringLiteral("shopSearchEdit"));
    shopSearchEdit->setClearButtonEnabled(true);
    shopList = new QListWidget(shopTab);
    shopList->setObjectName(QStringLiteral("shopDocumentList"));
    shopLayout->addWidget(shopSearchEdit);
    shopLayout->addWidget(shopList, 1);
    auto shopHint = new QLabel(tr("双击或按 Enter 打开商店货单。"), shopTab);
    shopHint->setObjectName(QStringLiteral("shopCatalogHint"));
    shopHint->setWordWrap(true);
    shopLayout->addWidget(shopHint);

    tabs->addTab(goodsTab, tr("物品"));
    tabs->addTab(shopTab, tr("商店"));
    layout->addWidget(tabs, 1);
    return panel;
}

QWidget* GoodsShopEditorWindow::createGoodsPage()
{
    auto page = new QWidget(this);
    page->setObjectName(QStringLiteral("goodsEditorPage"));
    auto layout = new QVBoxLayout(page);

    auto identityGroup = new QGroupBox(tr("物品信息"), page);
    identityGroup->setObjectName(QStringLiteral("goodsIdentityGroup"));
    auto identityForm = new QFormLayout(identityGroup);
    goodsNameEdit = new QLineEdit(identityGroup);
    goodsNameEdit->setObjectName(QStringLiteral("goodsNameEdit"));
    goodsKindCombo = new QComboBox(identityGroup);
    goodsKindCombo->setObjectName(QStringLiteral("goodsKindCombo"));
    goodsIntroductionEdit = new QPlainTextEdit(identityGroup);
    goodsIntroductionEdit->setObjectName(
        QStringLiteral("goodsIntroductionEdit"));
    goodsIntroductionEdit->setMaximumHeight(100);
    goodsIntroductionEdit->installEventFilter(this);
    goodsEffectEdit = new QLineEdit(identityGroup);
    goodsEffectEdit->setObjectName(QStringLiteral("goodsEffectEdit"));
    identityForm->addRow(tr("名称"), goodsNameEdit);
    if (QWidget* label = identityForm->labelForField(goodsNameEdit))
        label->setObjectName(QStringLiteral("goodsNameLabel"));
    identityForm->addRow(tr("类型"), goodsKindCombo);
    if (QWidget* label = identityForm->labelForField(goodsKindCombo))
        label->setObjectName(QStringLiteral("goodsKindLabel"));
    identityForm->addRow(tr("说明"), goodsIntroductionEdit);
    if (QWidget* label = identityForm->labelForField(goodsIntroductionEdit))
        label->setObjectName(QStringLiteral("goodsIntroductionLabel"));
    identityForm->addRow(tr("游戏内效果文字"), goodsEffectEdit);
    if (QWidget* label = identityForm->labelForField(goodsEffectEdit))
        label->setObjectName(QStringLiteral("goodsEffectLabel"));
    layout->addWidget(identityGroup);

    auto priceGroup = new QGroupBox(tr("价格"), page);
    priceGroup->setObjectName(QStringLiteral("goodsPriceGroup"));
    auto priceForm = new QFormLayout(priceGroup);
    goodsCostSpin = createIntegerSpinBox(
        priceGroup, QStringLiteral("goodsCostSpin"));
    goodsSellPriceSpin = createIntegerSpinBox(
        priceGroup, QStringLiteral("goodsSellPriceSpin"));
    priceForm->addRow(tr("基础价格"), goodsCostSpin);
    if (QWidget* label = priceForm->labelForField(goodsCostSpin))
        label->setObjectName(QStringLiteral("goodsCostLabel"));
    priceForm->addRow(tr("回收价格"), goodsSellPriceSpin);
    if (QWidget* label = priceForm->labelForField(goodsSellPriceSpin))
        label->setObjectName(QStringLiteral("goodsSellPriceLabel"));
    goodsPriceHint = new QLabel(
        tr("回收价格为 0 时由游戏按基础价格计算；商店比例在商店货单中调整。"),
        priceGroup);
    goodsPriceHint->setObjectName(QStringLiteral("goodsPriceHint"));
    goodsPriceHint->setWordWrap(true);
    priceForm->addRow(goodsPriceHint);
    layout->addWidget(priceGroup);

    auto effectsGroup = new QGroupBox(tr("使用或装备效果"), page);
    effectsGroup->setObjectName(QStringLiteral("goodsEffectsGroup"));
    auto effectsLayout = new QVBoxLayout(effectsGroup);
    goodsEffectStack = new QStackedWidget(effectsGroup);
    goodsEffectStack->setObjectName(QStringLiteral("goodsEffectStack"));

    auto drugPage = new QWidget(goodsEffectStack);
    drugPage->setObjectName(QStringLiteral("goodsDrugEffectPage"));
    auto drugForm = new QFormLayout(drugPage);
    goodsLifeSpin = createIntegerSpinBox(drugPage, QStringLiteral("goodsLifeSpin"));
    goodsThewSpin = createIntegerSpinBox(drugPage, QStringLiteral("goodsThewSpin"));
    goodsManaSpin = createIntegerSpinBox(drugPage, QStringLiteral("goodsManaSpin"));
    drugForm->addRow(tr("恢复生命"), goodsLifeSpin);
    drugForm->addRow(tr("恢复体力"), goodsThewSpin);
    drugForm->addRow(tr("恢复内力"), goodsManaSpin);
    goodsEffectStack->addWidget(drugPage);

    auto equipmentPage = new QWidget(goodsEffectStack);
    equipmentPage->setObjectName(QStringLiteral("goodsEquipmentEffectPage"));
    auto equipmentForm = new QFormLayout(equipmentPage);
    goodsPartCombo = new QComboBox(equipmentPage);
    goodsPartCombo->setObjectName(QStringLiteral("goodsPartCombo"));
    goodsLifeMaximumSpin = createIntegerSpinBox(
        equipmentPage, QStringLiteral("goodsLifeMaximumSpin"));
    goodsThewMaximumSpin = createIntegerSpinBox(
        equipmentPage, QStringLiteral("goodsThewMaximumSpin"));
    goodsManaMaximumSpin = createIntegerSpinBox(
        equipmentPage, QStringLiteral("goodsManaMaximumSpin"));
    goodsAttackSpin = createIntegerSpinBox(
        equipmentPage, QStringLiteral("goodsAttackSpin"));
    goodsDefendSpin = createIntegerSpinBox(
        equipmentPage, QStringLiteral("goodsDefendSpin"));
    goodsEvadeSpin = createIntegerSpinBox(
        equipmentPage, QStringLiteral("goodsEvadeSpin"));
    equipmentForm->addRow(tr("装备位置"), goodsPartCombo);
    equipmentForm->addRow(tr("生命上限"), goodsLifeMaximumSpin);
    equipmentForm->addRow(tr("体力上限"), goodsThewMaximumSpin);
    equipmentForm->addRow(tr("内力上限"), goodsManaMaximumSpin);
    equipmentForm->addRow(tr("攻击"), goodsAttackSpin);
    equipmentForm->addRow(tr("防御"), goodsDefendSpin);
    equipmentForm->addRow(tr("闪避"), goodsEvadeSpin);
    goodsEffectStack->addWidget(equipmentPage);

    auto otherPage = new QWidget(goodsEffectStack);
    otherPage->setObjectName(QStringLiteral("goodsOtherEffectPage"));
    auto otherLayout = new QVBoxLayout(otherPage);
    auto otherHint = new QLabel(
        tr("这类物品的脚本和其它高级效果会保持原样；第一版不把它们展开为技术字段。"),
        otherPage);
    otherHint->setObjectName(QStringLiteral("goodsOtherEffectHint"));
    otherHint->setWordWrap(true);
    otherLayout->addWidget(otherHint);
    otherLayout->addStretch();
    goodsEffectStack->addWidget(otherPage);
    effectsLayout->addWidget(goodsEffectStack);
    layout->addWidget(effectsGroup);

    auto resourcesGroup = new QGroupBox(tr("图片预览"), page);
    resourcesGroup->setObjectName(QStringLiteral("goodsResourcesGroup"));
    auto resourcesLayout = new QHBoxLayout(resourcesGroup);
    resourcesLayout->addWidget(createGoodsImageCard(
        tr("物品图片"), GoodsTextField::Image,
        goodsImagePreview, goodsImageEdit), 1);
    resourcesLayout->addWidget(createGoodsImageCard(
        tr("背包图标"), GoodsTextField::Icon,
        goodsIconPreview, goodsIconEdit), 1);
    layout->addWidget(resourcesGroup);
    layout->addStretch();
    return page;
}

QWidget* GoodsShopEditorWindow::createGoodsImageCard(
    const QString& title, GoodsTextField field,
    MpcPreviewLabel*& preview, QLineEdit*& edit)
{
    auto card = new QGroupBox(title, this);
    card->setObjectName(QStringLiteral("goods%1Group").arg(
        goodsFieldSuffix(field)));
    card->setProperty("goodsTextField", static_cast<int>(field));
    auto layout = new QVBoxLayout(card);
    preview = new MpcPreviewLabel(card);
    preview->setObjectName(QStringLiteral("goods%1Preview").arg(
        goodsFieldSuffix(field)));
    preview->setMinimumSize(180, 135);
    preview->setMaximumHeight(200);
    preview->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(preview, 1);
    auto row = new QHBoxLayout();
    edit = new QLineEdit(card);
    edit->setObjectName(QStringLiteral("goods%1Edit").arg(
        goodsFieldSuffix(field)));
    auto browse = new QPushButton(tr("选择…"), card);
    browse->setObjectName(QStringLiteral("goodsResourceBrowseButton"));
    row->addWidget(edit, 1);
    row->addWidget(browse);
    layout->addLayout(row);
    connect(edit, &QLineEdit::editingFinished, this,
        [this, field, edit, title]()
        {
            pushGoodsTextChange(field, edit->text(),
                                tr("修改%1").arg(title));
        });
    connect(browse, &QPushButton::clicked, this,
        [this, field]() { browseGoodsResource(field); });
    return card;
}

QWidget* GoodsShopEditorWindow::createShopPage()
{
    auto page = new QWidget(this);
    page->setObjectName(QStringLiteral("shopEditorPage"));
    auto layout = new QVBoxLayout(page);

    auto optionsGroup = new QGroupBox(tr("商店设置"), page);
    optionsGroup->setObjectName(QStringLiteral("shopOptionsGroup"));
    auto optionsForm = new QFormLayout(optionsGroup);
    shopStockLimitedCheck = new QCheckBox(
        tr("启用有限库存"), optionsGroup);
    shopStockLimitedCheck->setObjectName(
        QStringLiteral("shopStockLimitedCheck"));
    shopBuyPercentSpin = createIntegerSpinBox(
        optionsGroup, QStringLiteral("shopBuyPercentSpin"));
    shopBuyPercentSpin->setRange(0, 10000);
    shopBuyPercentSpin->setSuffix(QStringLiteral("%"));
    shopRecyclePercentSpin = createIntegerSpinBox(
        optionsGroup, QStringLiteral("shopRecyclePercentSpin"));
    shopRecyclePercentSpin->setRange(0, 10000);
    shopRecyclePercentSpin->setSuffix(QStringLiteral("%"));
    optionsForm->addRow(shopStockLimitedCheck);
    optionsForm->addRow(tr("出售价格比例"), shopBuyPercentSpin);
    if (QWidget* label = optionsForm->labelForField(shopBuyPercentSpin))
        label->setObjectName(QStringLiteral("shopBuyPercentLabel"));
    optionsForm->addRow(tr("回收价格比例"), shopRecyclePercentSpin);
    if (QWidget* label = optionsForm->labelForField(shopRecyclePercentSpin))
        label->setObjectName(QStringLiteral("shopRecyclePercentLabel"));
    auto priceHint = new QLabel(
        tr("100% 表示使用物品本身的价格；这里只调整比例，不模拟整套经济。"),
        optionsGroup);
    priceHint->setObjectName(QStringLiteral("shopPriceHint"));
    priceHint->setWordWrap(true);
    optionsForm->addRow(priceHint);
    layout->addWidget(optionsGroup);

    auto contentsGroup = new QGroupBox(tr("货单内容"), page);
    contentsGroup->setObjectName(QStringLiteral("shopContentsGroup"));
    auto contentsLayout = new QVBoxLayout(contentsGroup);
    auto contentsHint = new QLabel(
        tr("从左侧物品库选择后加入。表格顺序就是游戏中的商店顺序。"),
        contentsGroup);
    contentsHint->setObjectName(QStringLiteral("shopContentsHint"));
    contentsHint->setWordWrap(true);
    contentsLayout->addWidget(contentsHint);
    shopItemsTable = new QTableWidget(contentsGroup);
    shopItemsTable->setObjectName(QStringLiteral("shopItemsTable"));
    shopItemsTable->setColumnCount(4);
    shopItemsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    shopItemsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    shopItemsTable->setAlternatingRowColors(true);
    shopItemsTable->verticalHeader()->setVisible(false);
    shopItemsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    shopItemsTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    shopItemsTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    shopItemsTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    contentsLayout->addWidget(shopItemsTable, 1);
    auto buttons = new QHBoxLayout();
    shopRemoveButton = new QPushButton(tr("移出商店"), contentsGroup);
    shopRemoveButton->setObjectName(QStringLiteral("shopRemoveButton"));
    shopMoveUpButton = new QPushButton(tr("上移"), contentsGroup);
    shopMoveUpButton->setObjectName(QStringLiteral("shopMoveUpButton"));
    shopMoveDownButton = new QPushButton(tr("下移"), contentsGroup);
    shopMoveDownButton->setObjectName(QStringLiteral("shopMoveDownButton"));
    buttons->addWidget(shopRemoveButton);
    buttons->addStretch();
    buttons->addWidget(shopMoveUpButton);
    buttons->addWidget(shopMoveDownButton);
    contentsLayout->addLayout(buttons);
    layout->addWidget(contentsGroup, 1);

    auto previewGroup = new QGroupBox(tr("当前物品预览"), page);
    previewGroup->setObjectName(QStringLiteral("shopItemPreviewGroup"));
    auto previewLayout = new QHBoxLayout(previewGroup);
    shopItemIconPreview = new MpcPreviewLabel(previewGroup);
    shopItemIconPreview->setObjectName(QStringLiteral("shopItemIconPreview"));
    shopItemIconPreview->setMinimumSize(140, 110);
    shopItemIconPreview->setFrameShape(QFrame::StyledPanel);
    shopItemImagePreview = new MpcPreviewLabel(previewGroup);
    shopItemImagePreview->setObjectName(QStringLiteral("shopItemImagePreview"));
    shopItemImagePreview->setMinimumSize(180, 110);
    shopItemImagePreview->setFrameShape(QFrame::StyledPanel);
    shopItemSummaryLabel = new QLabel(previewGroup);
    shopItemSummaryLabel->setObjectName(
        QStringLiteral("shopItemSummaryLabel"));
    shopItemSummaryLabel->setWordWrap(true);
    previewLayout->addWidget(shopItemIconPreview);
    previewLayout->addWidget(shopItemImagePreview);
    previewLayout->addWidget(shopItemSummaryLabel, 1);
    layout->addWidget(previewGroup);
    return page;
}

void GoodsShopEditorWindow::setupActions()
{
    saveAction = toolBar->addAction(tr("保存"));
    saveAction->setObjectName(QStringLiteral("goodsShopSaveAction"));
    saveAction->setShortcut(QKeySequence::Save);
    toolBar->addSeparator();
    undoAction = undoStack->createUndoAction(this, tr("撤销"));
    undoAction->setObjectName(QStringLiteral("goodsShopUndoAction"));
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction = undoStack->createRedoAction(this, tr("重做"));
    redoAction->setObjectName(QStringLiteral("goodsShopRedoAction"));
    redoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    toolBar->addAction(undoAction);
    toolBar->addAction(redoAction);
    toolBar->addSeparator();
    duplicateAction = toolBar->addAction(tr("复制物品…"));
    duplicateAction->setObjectName(QStringLiteral("goodsDuplicateAction"));
    playtestAction = toolBar->addAction(tr("试玩当前内容"));
    playtestAction->setObjectName(QStringLiteral("goodsShopPlaytestAction"));
}

void GoodsShopEditorWindow::setupConnections()
{
    connect(saveAction, &QAction::triggered, this,
        [this]() { commitPendingGoodsEditors(); saveFile(); });
    connect(duplicateAction, &QAction::triggered,
            this, &GoodsShopEditorWindow::duplicateCurrentGoods);
    connect(playtestAction, &QAction::triggered, this,
        [this]()
        {
            commitPendingGoodsEditors();
            emit playtestRequested();
        });
    connect(undoStack, &QUndoStack::indexChanged, this,
        [this]()
        {
            refreshFromDocument();
            refreshCatalogLists();
            emit documentStatesChanged();
        });
    connect(undoStack, &QUndoStack::cleanChanged, this,
        [this]()
        {
            updateWindowTitle();
            updateActionStates();
            emit documentStatesChanged();
        });

    connect(goodsSearchEdit, &QLineEdit::textChanged,
            this, &GoodsShopEditorWindow::filterGoodsList);
    connect(shopSearchEdit, &QLineEdit::textChanged,
            this, &GoodsShopEditorWindow::filterShopList);
    connect(goodsList, &QListWidget::itemActivated, this,
        [this](QListWidgetItem* item)
        {
            if (item)
            {
                commitPendingGoodsEditors();
                emit openGoodsShopFileRequested(
                    item->data(Qt::UserRole).toString());
            }
        });
    connect(shopList, &QListWidget::itemActivated, this,
        [this](QListWidgetItem* item)
        {
            if (item)
            {
                commitPendingGoodsEditors();
                emit openGoodsShopFileRequested(
                    item->data(Qt::UserRole).toString());
            }
        });
    connect(goodsList, &QListWidget::currentItemChanged, this,
        [this]() { updateShopButtons(); });
    connect(addSelectedGoodsButton, &QPushButton::clicked,
            this, &GoodsShopEditorWindow::addSelectedGoodsToShop);

    connect(goodsNameEdit, &QLineEdit::editingFinished, this,
        [this]()
        {
            pushGoodsTextChange(GoodsTextField::Name, goodsNameEdit->text(),
                                tr("修改物品名称"));
        });
    connect(goodsEffectEdit, &QLineEdit::editingFinished, this,
        [this]()
        {
            pushGoodsTextChange(GoodsTextField::Effect,
                                goodsEffectEdit->text(),
                                tr("修改效果文字"));
        });
    connect(goodsKindCombo, &QComboBox::currentIndexChanged, this,
        [this](int)
        {
            if (!refreshing)
                pushGoodsIntegerChange(
                    GoodsIntegerField::Kind,
                    goodsKindCombo->currentData().toInt(),
                    tr("修改物品类型"));
        });
    connect(goodsPartCombo, &QComboBox::currentIndexChanged, this,
        [this](int)
        {
            if (!refreshing)
                pushGoodsTextChange(
                    GoodsTextField::EquipmentPart,
                    goodsPartCombo->currentData().toString(),
                    tr("修改装备位置"));
        });

    const std::pair<QSpinBox*, GoodsIntegerField> goodsSpins[] = {
        {goodsCostSpin, GoodsIntegerField::Cost},
        {goodsSellPriceSpin, GoodsIntegerField::SellPrice},
        {goodsLifeSpin, GoodsIntegerField::Life},
        {goodsThewSpin, GoodsIntegerField::Thew},
        {goodsManaSpin, GoodsIntegerField::Mana},
        {goodsLifeMaximumSpin, GoodsIntegerField::LifeMaximum},
        {goodsThewMaximumSpin, GoodsIntegerField::ThewMaximum},
        {goodsManaMaximumSpin, GoodsIntegerField::ManaMaximum},
        {goodsAttackSpin, GoodsIntegerField::Attack},
        {goodsDefendSpin, GoodsIntegerField::Defend},
        {goodsEvadeSpin, GoodsIntegerField::Evade}
    };
    for (const auto& entry : goodsSpins)
    {
        connect(entry.first, &QSpinBox::valueChanged, this,
            [this, field = entry.second](int value)
            {
                if (!refreshing)
                    pushGoodsIntegerChange(field, value,
                                           tr("修改物品数值"));
            });
    }

    connect(shopStockLimitedCheck, &QCheckBox::toggled, this,
        [this](bool checked)
        {
            if (!refreshing)
                pushShopMutation(
                    tr("修改库存方式"),
                    [this, checked]()
                    {
                        shopDocument.setStockLimited(checked);
                        return true;
                    });
        });
    connect(shopBuyPercentSpin, &QSpinBox::valueChanged, this,
        [this](int value)
        {
            if (!refreshing)
                pushShopMutation(
                    tr("修改出售价格比例"),
                    [this, value]()
                    {
                        shopDocument.setBuyPercent(value);
                        return true;
                    });
        });
    connect(shopRecyclePercentSpin, &QSpinBox::valueChanged, this,
        [this](int value)
        {
            if (!refreshing)
                pushShopMutation(
                    tr("修改回收价格比例"),
                    [this, value]()
                    {
                        shopDocument.setRecyclePercent(value);
                        return true;
                    });
        });
    connect(shopItemsTable, &QTableWidget::itemChanged, this,
        [this](QTableWidgetItem* item)
        {
            if (refreshing || !item || item->column() != 3 ||
                !shopDocument.stockLimited())
            {
                return;
            }
            bool ok = false;
            const int number = item->text().toInt(&ok);
            if (!ok || number < 0)
            {
                refreshShopPage();
                return;
            }
            const int row = item->row();
            pushShopMutation(
                tr("修改商店库存"),
                [this, row, number]()
                {
                    return shopDocument.setItemNumber(row, number);
                });
        });
    connect(shopItemsTable, &QTableWidget::currentCellChanged, this,
        [this](int, int, int, int)
        {
            refreshShopSelectionPreview();
            updateShopButtons();
        });
    connect(shopItemsTable, &QTableWidget::cellDoubleClicked, this,
        [this](int row, int)
        {
            const QString path = goodsPathForReference(
                shopDocument.item(row).iniFile);
            if (!path.isEmpty())
                emit openGoodsShopFileRequested(path);
        });
    connect(shopRemoveButton, &QPushButton::clicked, this,
        [this]()
        {
            const int row = shopItemsTable->currentRow();
            pushShopMutation(
                tr("移出商店"),
                [this, row]() { return shopDocument.removeItem(row); });
        });
    connect(shopMoveUpButton, &QPushButton::clicked, this,
        [this]()
        {
            const int row = shopItemsTable->currentRow();
            pushShopMutation(
                tr("上移商店物品"),
                [this, row]() { return shopDocument.moveItem(row, row - 1); });
            shopItemsTable->selectRow(std::max(0, row - 1));
        });
    connect(shopMoveDownButton, &QPushButton::clicked, this,
        [this]()
        {
            const int row = shopItemsTable->currentRow();
            pushShopMutation(
                tr("下移商店物品"),
                [this, row]() { return shopDocument.moveItem(row, row + 1); });
            shopItemsTable->selectRow(
                std::min(shopDocument.itemCount() - 1, row + 1));
        });
}

bool GoodsShopEditorWindow::loadDocumentBytes(const QByteArray& bytes)
{
    QString error;
    const bool loaded = kind == GoodsShopDocumentKind::Goods
        ? goodsDocument.load(bytes, &error)
        : kind == GoodsShopDocumentKind::Shop
            ? shopDocument.load(bytes, &error) : false;
    if (loaded)
        refreshFromDocument();
    return loaded;
}

QByteArray GoodsShopEditorWindow::currentDocumentBytes() const
{
    if (kind == GoodsShopDocumentKind::Goods)
        return goodsDocument.serializedBytes();
    if (kind == GoodsShopDocumentKind::Shop)
        return shopDocument.serializedBytes();
    return {};
}

void GoodsShopEditorWindow::pushSnapshotChange(
    const QByteArray& before, const QByteArray& after,
    const QString& description)
{
    if (before == after)
        return;
    undoStack->push(new GoodsShopSnapshotCommand(
        before, after,
        [this](const QByteArray& bytes) { loadDocumentBytes(bytes); },
        description));
}

void GoodsShopEditorWindow::pushGoodsTextChange(
    GoodsTextField field, const QString& value,
    const QString& description)
{
    if (refreshing || kind != GoodsShopDocumentKind::Goods ||
        goodsDocument.text(field) == value)
    {
        return;
    }
    const QByteArray before = goodsDocument.serializedBytes();
    goodsDocument.setText(field, value);
    const QByteArray after = goodsDocument.serializedBytes();
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, description);
}

void GoodsShopEditorWindow::pushGoodsIntegerChange(
    GoodsIntegerField field, int value, const QString& description)
{
    if (refreshing || kind != GoodsShopDocumentKind::Goods ||
        (goodsDocument.hasFixedInteger(field) &&
         goodsDocument.integer(field) == value))
    {
        return;
    }
    const QByteArray before = goodsDocument.serializedBytes();
    goodsDocument.setInteger(field, value);
    const QByteArray after = goodsDocument.serializedBytes();
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, description);
}

void GoodsShopEditorWindow::pushShopMutation(
    const QString& description, const std::function<bool()>& mutation)
{
    if (refreshing || kind != GoodsShopDocumentKind::Shop)
        return;
    const QByteArray before = shopDocument.serializedBytes();
    if (!mutation())
        return;
    const QByteArray after = shopDocument.serializedBytes();
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, description);
}

void GoodsShopEditorWindow::commitPendingGoodsEditors()
{
    if (refreshing || kind != GoodsShopDocumentKind::Goods || filePath.isEmpty())
        return;
    const QByteArray before = goodsDocument.serializedBytes();
    goodsDocument.setText(GoodsTextField::Name, goodsNameEdit->text());
    goodsDocument.setText(GoodsTextField::Introduction,
                          goodsIntroductionEdit->toPlainText());
    goodsDocument.setText(GoodsTextField::Effect, goodsEffectEdit->text());
    goodsDocument.setText(GoodsTextField::Image, goodsImageEdit->text());
    goodsDocument.setText(GoodsTextField::Icon, goodsIconEdit->text());
    const QByteArray after = goodsDocument.serializedBytes();
    if (before == after)
        return;
    loadDocumentBytes(before);
    pushSnapshotChange(before, after, tr("修改物品内容"));
}

void GoodsShopEditorWindow::refreshFromDocument()
{
    refreshing = true;
    if (kind == GoodsShopDocumentKind::Shop)
    {
        editorStack->setCurrentWidget(shopPage);
        refreshShopPage();
    }
    else
    {
        editorStack->setCurrentWidget(goodsPage);
        refreshGoodsPage();
    }
    refreshing = false;
    updateWindowTitle();
    updateActionStates();
    fileSummaryLabel->setText(
        filePath.isEmpty()
            ? tr("从左侧列表或项目树打开一个物品或商店货单。")
            : kind == GoodsShopDocumentKind::Goods
                ? tr("正在编辑物品：%1").arg(
                      QDir::toNativeSeparators(filePath))
                : tr("正在编辑商店：%1").arg(
                      QDir::toNativeSeparators(filePath)));
    const int hidden = kind == GoodsShopDocumentKind::Goods
        ? goodsDocument.hiddenFieldCount()
        : kind == GoodsShopDocumentKind::Shop
            ? shopDocument.hiddenFieldCount() : 0;
    preservationLabel->setText(
        filePath.isEmpty()
            ? QString()
            : tr("原文件中还有 %1 项高级内容未在此显示；保存时会保持不变。")
                  .arg(hidden));
}

void GoodsShopEditorWindow::refreshGoodsPage()
{
    if (!goodsPage)
        return;
    const QSignalBlocker blockName(goodsNameEdit);
    const QSignalBlocker blockKind(goodsKindCombo);
    const QSignalBlocker blockIntro(goodsIntroductionEdit);
    const QSignalBlocker blockEffect(goodsEffectEdit);
    goodsNameEdit->setText(goodsDocument.text(GoodsTextField::Name));
    goodsIntroductionEdit->setPlainText(
        goodsDocument.text(GoodsTextField::Introduction));
    goodsEffectEdit->setText(goodsDocument.text(GoodsTextField::Effect));
    goodsImageEdit->setText(goodsDocument.text(GoodsTextField::Image));
    goodsIconEdit->setText(goodsDocument.text(GoodsTextField::Icon));

    const int goodsKind = goodsDocument.integer(GoodsIntegerField::Kind);
    int kindIndex = goodsKindCombo->findData(goodsKind);
    if (kindIndex < 0)
    {
        goodsKindCombo->addItem(tr("其它类型 (%1)").arg(goodsKind), goodsKind);
        kindIndex = goodsKindCombo->count() - 1;
    }
    goodsKindCombo->setCurrentIndex(kindIndex);
    goodsEffectStack->setCurrentIndex(
        goodsKind == 0 ? 0 : goodsKind == 1 ? 1 : 2);

    const std::pair<QSpinBox*, GoodsIntegerField> spins[] = {
        {goodsCostSpin, GoodsIntegerField::Cost},
        {goodsSellPriceSpin, GoodsIntegerField::SellPrice},
        {goodsLifeSpin, GoodsIntegerField::Life},
        {goodsThewSpin, GoodsIntegerField::Thew},
        {goodsManaSpin, GoodsIntegerField::Mana},
        {goodsLifeMaximumSpin, GoodsIntegerField::LifeMaximum},
        {goodsThewMaximumSpin, GoodsIntegerField::ThewMaximum},
        {goodsManaMaximumSpin, GoodsIntegerField::ManaMaximum},
        {goodsAttackSpin, GoodsIntegerField::Attack},
        {goodsDefendSpin, GoodsIntegerField::Defend},
        {goodsEvadeSpin, GoodsIntegerField::Evade}
    };
    for (const auto& entry : spins)
    {
        const QSignalBlocker blocker(entry.first);
        entry.first->setValue(std::max(0, goodsDocument.integer(entry.second)));
        const bool fixed = goodsDocument.hasFixedInteger(entry.second);
        entry.first->setEnabled(fixed && kind == GoodsShopDocumentKind::Goods);
        entry.first->setToolTip(
            fixed ? QString() :
            tr("当前值使用了随机规则，第一版保持原样；不会在这里改写。"));
    }

    const QString part = goodsDocument.text(
        GoodsTextField::EquipmentPart).trimmed();
    int partIndex = goodsPartCombo->findData(part);
    if (partIndex < 0)
    {
        goodsPartCombo->addItem(
            part.isEmpty() ? tr("未指定") : tr("其它位置：%1").arg(part), part);
        partIndex = goodsPartCombo->count() - 1;
    }
    {
        const QSignalBlocker blockPart(goodsPartCombo);
        goodsPartCombo->setCurrentIndex(partIndex);
    }
    refreshGoodsPreviews();
}

void GoodsShopEditorWindow::refreshShopPage()
{
    if (!shopPage)
        return;
    const int selectedRow = shopItemsTable->currentRow();
    {
        const QSignalBlocker blockStock(shopStockLimitedCheck);
        const QSignalBlocker blockBuy(shopBuyPercentSpin);
        const QSignalBlocker blockRecycle(shopRecyclePercentSpin);
        shopStockLimitedCheck->setChecked(shopDocument.stockLimited());
        shopBuyPercentSpin->setValue(shopDocument.buyPercent());
        shopRecyclePercentSpin->setValue(shopDocument.recyclePercent());
    }

    const QSignalBlocker blocker(shopItemsTable);
    shopItemsTable->clearContents();
    shopItemsTable->setRowCount(shopDocument.itemCount());
    shopItemsTable->setHorizontalHeaderLabels(
        {tr("顺序"), tr("物品"), tr("文件"), tr("库存")});
    for (int row = 0; row < shopDocument.itemCount(); ++row)
    {
        const ShopDocumentItem shopItem = shopDocument.item(row);
        GoodsDocument goods;
        QString name;
        const QString goodsPath = goodsPathForReference(shopItem.iniFile);
        if (!goodsPath.isEmpty() && goods.openFile(goodsPath))
            name = goods.text(GoodsTextField::Name).trimmed();
        if (name.isEmpty())
            name = tr("未找到物品");

        auto orderItem = new QTableWidgetItem(QString::number(row + 1));
        orderItem->setFlags(orderItem->flags() & ~Qt::ItemIsEditable);
        orderItem->setTextAlignment(Qt::AlignCenter);
        auto nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        auto fileItem = new QTableWidgetItem(shopItem.iniFile);
        fileItem->setFlags(fileItem->flags() & ~Qt::ItemIsEditable);
        auto numberItem = new QTableWidgetItem(
            shopDocument.stockLimited()
                ? QString::number(shopItem.number) : tr("不限"));
        if (!shopDocument.stockLimited())
            numberItem->setFlags(numberItem->flags() & ~Qt::ItemIsEditable);
        numberItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        shopItemsTable->setItem(row, 0, orderItem);
        shopItemsTable->setItem(row, 1, nameItem);
        shopItemsTable->setItem(row, 2, fileItem);
        shopItemsTable->setItem(row, 3, numberItem);
    }
    if (shopDocument.itemCount() > 0)
    {
        shopItemsTable->selectRow(
            std::clamp(selectedRow, 0, shopDocument.itemCount() - 1));
    }
    refreshShopSelectionPreview();
    updateShopButtons();
}

void GoodsShopEditorWindow::refreshCatalogLists()
{
    if (!goodsList || !shopList)
        return;
    const QString selectedGoods = goodsList->currentItem()
        ? goodsList->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString selectedShop = shopList->currentItem()
        ? shopList->currentItem()->data(Qt::UserRole).toString() : QString();
    goodsList->clear();
    shopList->clear();

    const QDir goodsDirectory(
        QDir(assetsBasePath).filePath(QStringLiteral("ini/goods")));
    const QFileInfoList goodsFiles = goodsDirectory.entryInfoList(
        {QStringLiteral("*.ini")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : goodsFiles)
    {
        GoodsDocument candidate;
        QString name;
        if (candidate.openFile(info.absoluteFilePath()))
            name = candidate.text(GoodsTextField::Name).trimmed();
        auto item = new QListWidgetItem(
            name.isEmpty() ? info.completeBaseName()
                           : tr("%1\n%2").arg(name, info.fileName()),
            goodsList);
        const QString path = EditorAssetPath::normalizedAbsolutePath(
            info.absoluteFilePath());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        const QString wanted = selectedGoods.isEmpty() &&
            kind == GoodsShopDocumentKind::Goods ? filePath : selectedGoods;
        if (ProjectDocumentRegistry::documentPathKey(path) ==
            ProjectDocumentRegistry::documentPathKey(wanted))
        {
            goodsList->setCurrentItem(item);
        }
    }

    const QDir shopDirectory(
        QDir(assetsBasePath).filePath(QStringLiteral("ini/buy")));
    const QFileInfoList shopFiles = shopDirectory.entryInfoList(
        {QStringLiteral("*.ini")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : shopFiles)
    {
        auto item = new QListWidgetItem(info.completeBaseName(), shopList);
        const QString path = EditorAssetPath::normalizedAbsolutePath(
            info.absoluteFilePath());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        const QString wanted = selectedShop.isEmpty() &&
            kind == GoodsShopDocumentKind::Shop ? filePath : selectedShop;
        if (ProjectDocumentRegistry::documentPathKey(path) ==
            ProjectDocumentRegistry::documentPathKey(wanted))
        {
            shopList->setCurrentItem(item);
        }
    }
    filterGoodsList();
    filterShopList();
    updateShopButtons();
}

void GoodsShopEditorWindow::filterGoodsList()
{
    const QString filter = goodsSearchEdit->text().trimmed();
    for (int index = 0; index < goodsList->count(); ++index)
    {
        QListWidgetItem* item = goodsList->item(index);
        item->setHidden(!filter.isEmpty() &&
            !item->text().contains(filter, Qt::CaseInsensitive) &&
            !item->data(Qt::UserRole).toString().contains(
                filter, Qt::CaseInsensitive));
    }
}

void GoodsShopEditorWindow::filterShopList()
{
    const QString filter = shopSearchEdit->text().trimmed();
    for (int index = 0; index < shopList->count(); ++index)
    {
        QListWidgetItem* item = shopList->item(index);
        item->setHidden(!filter.isEmpty() &&
            !item->text().contains(filter, Qt::CaseInsensitive) &&
            !item->data(Qt::UserRole).toString().contains(
                filter, Qt::CaseInsensitive));
    }
}

void GoodsShopEditorWindow::refreshGoodsPreviews()
{
    if (!goodsImagePreview || !goodsIconPreview)
        return;
    showPreview(goodsImagePreview,
                goodsDocument.text(GoodsTextField::Image));
    showPreview(goodsIconPreview,
                goodsDocument.text(GoodsTextField::Icon));
}

void GoodsShopEditorWindow::refreshShopSelectionPreview()
{
    if (!shopItemSummaryLabel || !shopItemImagePreview || !shopItemIconPreview)
        return;
    const int row = shopItemsTable ? shopItemsTable->currentRow() : -1;
    if (kind != GoodsShopDocumentKind::Shop || row < 0 ||
        row >= shopDocument.itemCount())
    {
        shopItemSummaryLabel->setText(tr("选择货单中的物品查看名称、价格和图片。"));
        shopItemImagePreview->clearImage(tr("未选择"));
        shopItemIconPreview->clearImage(tr("未选择"));
        return;
    }

    const ShopDocumentItem item = shopDocument.item(row);
    GoodsDocument goods;
    const QString goodsPath = goodsPathForReference(item.iniFile);
    if (goodsPath.isEmpty() || !goods.openFile(goodsPath))
    {
        shopItemSummaryLabel->setText(
            tr("未找到物品：%1\n保存货单时仍会保留这个引用。")
                .arg(item.iniFile));
        shopItemImagePreview->clearImage(tr("未找到"));
        shopItemIconPreview->clearImage(tr("未找到"));
        return;
    }

    const int sellPrice = goods.integer(GoodsIntegerField::SellPrice);
    shopItemSummaryLabel->setText(
        tr("%1\n%2\n基础价格：%3\n回收价格：%4")
            .arg(goods.text(GoodsTextField::Name).trimmed(),
                 goods.text(GoodsTextField::Effect).trimmed())
            .arg(goods.integer(GoodsIntegerField::Cost))
            .arg(sellPrice > 0 ? QString::number(sellPrice)
                               : tr("按游戏默认")));
    showPreview(shopItemImagePreview,
                goods.text(GoodsTextField::Image));
    showPreview(shopItemIconPreview,
                goods.text(GoodsTextField::Icon));
}

void GoodsShopEditorWindow::updateWindowTitle()
{
    QString title;
    if (filePath.isEmpty())
        title = tr("物品与商店编辑器");
    else if (kind == GoodsShopDocumentKind::Goods)
        title = tr("物品 - %1%2").arg(
            displayName(), hasUnsavedChanges() ? QStringLiteral("*") : QString());
    else
        title = tr("商店 - %1%2").arg(
            displayName(), hasUnsavedChanges() ? QStringLiteral("*") : QString());
    setWindowTitle(title);
}

void GoodsShopEditorWindow::updateActionStates()
{
    const bool opened = !filePath.isEmpty() &&
        kind != GoodsShopDocumentKind::None;
    saveAction->setEnabled(opened && hasUnsavedChanges());
    duplicateAction->setEnabled(kind == GoodsShopDocumentKind::Goods);
    playtestAction->setEnabled(opened);
    addSelectedGoodsButton->setVisible(kind == GoodsShopDocumentKind::Shop);
    updateShopButtons();
}

void GoodsShopEditorWindow::updateShopButtons()
{
    if (!shopItemsTable)
        return;
    const int row = shopItemsTable->currentRow();
    const bool editingShop = kind == GoodsShopDocumentKind::Shop;
    const bool selected = editingShop && row >= 0 &&
        row < shopDocument.itemCount();
    addSelectedGoodsButton->setEnabled(
        editingShop && goodsList && goodsList->currentItem() &&
        shopDocument.itemCount() < ShopDocument::MaximumItems);
    shopRemoveButton->setEnabled(selected);
    shopMoveUpButton->setEnabled(selected && row > 0);
    shopMoveDownButton->setEnabled(
        selected && row + 1 < shopDocument.itemCount());
}

bool GoodsShopEditorWindow::confirmSaveIfModified()
{
    commitPendingGoodsEditors();
    if (!hasUnsavedChanges())
        return true;
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("保存更改"),
        kind == GoodsShopDocumentKind::Goods
            ? tr("物品“%1”已修改，是否保存？").arg(displayName())
            : tr("商店“%1”已修改，是否保存？").arg(displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel)
        return false;
    return choice != QMessageBox::Save || saveFile();
}

void GoodsShopEditorWindow::browseGoodsResource(GoodsTextField field)
{
    if (assetsBasePath.isEmpty())
        return;
    const QStringList folders = {
        QStringLiteral("asf/goods"), QStringLiteral("mpc/goods")};
    QString initialDirectory;
    for (const QString& folder : folders)
    {
        const QString candidate = QDir(assetsBasePath).filePath(folder);
        if (QDir(candidate).exists())
        {
            initialDirectory = candidate;
            break;
        }
    }
    const QString selected = QFileDialog::getOpenFileName(
        this, tr("选择物品图片"), initialDirectory,
        tr("图片文件 (*.mpc *.asf *.imp *.img *.png);;所有文件 (*.*)"));
    if (selected.isEmpty())
        return;
    QString reference;
    QStringList absoluteFolders;
    for (const QString& folder : folders)
        absoluteFolders.append(QDir(assetsBasePath).filePath(folder));
    if (!FilePickerHelper::makeResourceReference(
            assetsBasePath, absoluteFolders, selected, reference))
    {
        QMessageBox::warning(
            this, tr("无法选择资源"),
            tr("请选择当前项目物品图片目录中的文件。"));
        return;
    }
    pushGoodsTextChange(field, reference, tr("修改物品图片"));
}

void GoodsShopEditorWindow::duplicateCurrentGoods()
{
    commitPendingGoodsEditors();
    if (kind != GoodsShopDocumentKind::Goods)
        return;
    const QString goodsRoot = QDir(assetsBasePath).filePath(
        QStringLiteral("ini/goods"));
    const QFileInfo source(filePath);
    const QString suggested = QDir(goodsRoot).filePath(
        source.completeBaseName() + tr("-副本.ini"));
    const QString target = QFileDialog::getSaveFileName(
        this, tr("复制为新物品"), suggested, tr("物品文件 (*.ini)"));
    if (target.isEmpty())
        return;
    QString error;
    if (!duplicateCurrentGoodsTo(target, &error))
        QMessageBox::warning(this, tr("无法复制物品"), error);
}

void GoodsShopEditorWindow::addSelectedGoodsToShop()
{
    if (kind != GoodsShopDocumentKind::Shop || !goodsList->currentItem())
        return;
    const QString selectedPath =
        goodsList->currentItem()->data(Qt::UserRole).toString();
    const QString iniFile = QFileInfo(selectedPath).fileName();
    pushShopMutation(
        tr("加入商店"),
        [this, iniFile]() { return shopDocument.addItem(iniFile, 1); });
    if (shopDocument.itemCount() > 0)
        shopItemsTable->selectRow(shopDocument.itemCount() - 1);
}

QString GoodsShopEditorWindow::goodsPathForReference(
    const QString& iniFile) const
{
    QString absolutePath;
    if (!EditorAssetPath::resolveLogicalResourcePath(
            assetsBasePath,
            QStringLiteral("ini/goods/") +
                QDir::fromNativeSeparators(iniFile.trimmed()),
            absolutePath))
    {
        return {};
    }
    return QFileInfo(absolutePath).isFile() ? absolutePath : QString();
}

QStringList GoodsShopEditorWindow::imageCandidates(
    const QString& reference) const
{
    QString normalized = QDir::fromNativeSeparators(reference.trimmed());
    if (normalized.isEmpty())
        return {};
    QStringList candidates;
    auto append = [&candidates](const QString& candidate)
    {
        if (!candidate.isEmpty() &&
            !candidates.contains(candidate, Qt::CaseInsensitive))
        {
            candidates.append(candidate);
        }
    };
    if (normalized.startsWith(QStringLiteral("asf/"), Qt::CaseInsensitive) ||
        normalized.startsWith(QStringLiteral("mpc/"), Qt::CaseInsensitive))
    {
        append(normalized);
    }
    else
    {
        if (normalized.startsWith(QStringLiteral("goods/"),
                                  Qt::CaseInsensitive))
        {
            normalized.remove(0, 6);
        }
        append(QStringLiteral("asf/goods/") + normalized);
        append(QStringLiteral("mpc/goods/") + normalized);
    }
    const QStringList originals = candidates;
    for (const QString& candidate : originals)
    {
        if (candidate.startsWith(QStringLiteral("asf/"), Qt::CaseInsensitive))
        {
            append(replacePackageExtension(
                QStringLiteral("mpc/") + candidate.mid(4),
                QStringLiteral(".mpc")));
        }
        else if (candidate.startsWith(QStringLiteral("mpc/"),
                                      Qt::CaseInsensitive))
        {
            append(replacePackageExtension(
                QStringLiteral("asf/") + candidate.mid(4),
                QStringLiteral(".asf")));
        }
    }
    return candidates;
}

void GoodsShopEditorWindow::showPreview(
    MpcPreviewLabel* label, const QString& reference)
{
    if (!label)
        return;
    if (reference.trimmed().isEmpty())
    {
        label->clearImage(tr("未设置"));
        return;
    }
    for (const QString& candidate : imageCandidates(reference))
    {
        AssetPreviewData preview;
        if (AssetPreviewLoader::load(assetsBasePath, candidate, &preview) &&
            preview.kind == AssetPreviewKind::Image)
        {
            label->setSourceImage(preview.image);
            label->setToolTip(
                tr("%1\n%2×%3，%4 帧")
                    .arg(candidate)
                    .arg(preview.imageWidth)
                    .arg(preview.imageHeight)
                    .arg(preview.frameCount));
            return;
        }
    }
    label->clearImage(tr("未找到预览\n可继续保存引用"));
    label->setToolTip(reference);
}

QLineEdit* GoodsShopEditorWindow::goodsLineEdit(
    GoodsTextField field) const
{
    switch (field)
    {
    case GoodsTextField::Name: return goodsNameEdit;
    case GoodsTextField::Effect: return goodsEffectEdit;
    case GoodsTextField::Image: return goodsImageEdit;
    case GoodsTextField::Icon: return goodsIconEdit;
    default: return nullptr;
    }
}

QSpinBox* GoodsShopEditorWindow::goodsSpinBox(
    GoodsIntegerField field) const
{
    switch (field)
    {
    case GoodsIntegerField::Cost: return goodsCostSpin;
    case GoodsIntegerField::SellPrice: return goodsSellPriceSpin;
    case GoodsIntegerField::Life: return goodsLifeSpin;
    case GoodsIntegerField::Thew: return goodsThewSpin;
    case GoodsIntegerField::Mana: return goodsManaSpin;
    case GoodsIntegerField::LifeMaximum: return goodsLifeMaximumSpin;
    case GoodsIntegerField::ThewMaximum: return goodsThewMaximumSpin;
    case GoodsIntegerField::ManaMaximum: return goodsManaMaximumSpin;
    case GoodsIntegerField::Attack: return goodsAttackSpin;
    case GoodsIntegerField::Defend: return goodsDefendSpin;
    case GoodsIntegerField::Evade: return goodsEvadeSpin;
    default: return nullptr;
    }
}

void GoodsShopEditorWindow::retranslateDynamicUi()
{
    const bool wasRefreshing = refreshing;
    refreshing = true;
    if (saveAction)
        saveAction->setText(tr("保存"));
    if (undoAction)
        undoAction->setText(tr("撤销"));
    if (redoAction)
        redoAction->setText(tr("重做"));
    if (duplicateAction)
        duplicateAction->setText(tr("复制物品…"));
    if (playtestAction)
    {
        playtestAction->setText(tr("试玩当前内容"));
        playtestAction->setToolTip(
            documentKind() == GoodsShopDocumentKind::Goods
                ? tr("把当前物品加入隔离试玩背包，再进入试玩场景")
                : tr("进入试玩场景后直接打开当前商店"));
    }
    if (QLabel* label = findChild<QLabel*>(
            QStringLiteral("goodsShopCatalogTitle")))
        label->setText(tr("物品与商店"));
    if (goodsSearchEdit)
        goodsSearchEdit->setPlaceholderText(tr("搜索物品名称或文件名"));
    if (shopSearchEdit)
        shopSearchEdit->setPlaceholderText(tr("搜索商店文件名"));
    if (addSelectedGoodsButton)
        addSelectedGoodsButton->setText(tr("加入当前商店"));
    if (QTabWidget* tabs = findChild<QTabWidget*>(
            QStringLiteral("goodsShopCatalogTabs")))
    {
        tabs->setTabText(0, tr("物品"));
        tabs->setTabText(1, tr("商店"));
    }
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("goodsCatalogHint")))
        label->setText(tr("单击选择；双击打开物品资料。编辑商店时可把选中物品加入货单。"));
    if (QLabel* label = findChild<QLabel*>(QStringLiteral("shopCatalogHint")))
        label->setText(tr("双击或按 Enter 打开商店货单。"));

    if (goodsKindCombo)
    {
        const int current = goodsKindCombo->currentData().toInt();
        goodsKindCombo->clear();
        goodsKindCombo->addItem(tr("药品"), 0);
        goodsKindCombo->addItem(tr("装备"), 1);
        goodsKindCombo->addItem(tr("其它物品"), 2);
        const int index = goodsKindCombo->findData(current);
        if (index >= 0)
            goodsKindCombo->setCurrentIndex(index);
    }
    if (goodsPartCombo)
    {
        const QString current = goodsPartCombo->currentData().toString();
        goodsPartCombo->clear();
        goodsPartCombo->addItem(tr("未指定"), QString());
        goodsPartCombo->addItem(tr("身体"), QStringLiteral("Body"));
        goodsPartCombo->addItem(tr("脚部"), QStringLiteral("Foot"));
        goodsPartCombo->addItem(tr("头部"), QStringLiteral("Head"));
        goodsPartCombo->addItem(tr("颈部"), QStringLiteral("Neck"));
        goodsPartCombo->addItem(tr("背部"), QStringLiteral("Back"));
        goodsPartCombo->addItem(tr("手腕"), QStringLiteral("Wrist"));
        goodsPartCombo->addItem(tr("手持"), QStringLiteral("Hand"));
        const int index = goodsPartCombo->findData(current);
        if (index >= 0)
            goodsPartCombo->setCurrentIndex(index);
    }

    const struct TextUpdate { const char* name; QString text; } textUpdates[] = {
        {"goodsNameLabel", tr("名称")},
        {"goodsKindLabel", tr("类型")},
        {"goodsIntroductionLabel", tr("说明")},
        {"goodsEffectLabel", tr("游戏内效果文字")},
        {"goodsCostLabel", tr("基础价格")},
        {"goodsSellPriceLabel", tr("回收价格")},
        {"goodsPriceHint", tr("回收价格为 0 时由游戏按基础价格计算；商店比例在商店货单中调整。")},
        {"goodsOtherEffectHint", tr("这类物品的脚本和其它高级效果会保持原样；第一版不把它们展开为技术字段。")},
        {"shopBuyPercentLabel", tr("出售价格比例")},
        {"shopRecyclePercentLabel", tr("回收价格比例")},
        {"shopPriceHint", tr("100% 表示使用物品本身的价格；这里只调整比例，不模拟整套经济。")},
        {"shopContentsHint", tr("从左侧物品库选择后加入。表格顺序就是游戏中的商店顺序。")}
    };
    for (const TextUpdate& update : textUpdates)
    {
        if (QLabel* label = findChild<QLabel*>(QString::fromLatin1(update.name)))
            label->setText(update.text);
    }
    const struct GroupUpdate { const char* name; QString title; } groupUpdates[] = {
        {"goodsIdentityGroup", tr("物品信息")},
        {"goodsPriceGroup", tr("价格")},
        {"goodsEffectsGroup", tr("使用或装备效果")},
        {"goodsResourcesGroup", tr("图片预览")},
        {"shopOptionsGroup", tr("商店设置")},
        {"shopContentsGroup", tr("货单内容")},
        {"shopItemPreviewGroup", tr("当前物品预览")}
    };
    for (const GroupUpdate& update : groupUpdates)
    {
        if (QGroupBox* group = findChild<QGroupBox*>(
                QString::fromLatin1(update.name)))
            group->setTitle(update.title);
    }
    for (QGroupBox* group : findChildren<QGroupBox*>())
    {
        const QVariant fieldValue = group->property("goodsTextField");
        if (!fieldValue.isValid())
            continue;
        const GoodsTextField field = static_cast<GoodsTextField>(
            fieldValue.toInt());
        group->setTitle(field == GoodsTextField::Image
            ? tr("物品图片") : tr("背包图标"));
    }
    for (QPushButton* button : findChildren<QPushButton*>(
             QStringLiteral("goodsResourceBrowseButton")))
        button->setText(tr("选择…"));
    if (shopStockLimitedCheck)
        shopStockLimitedCheck->setText(tr("启用有限库存"));
    if (shopRemoveButton)
        shopRemoveButton->setText(tr("移出商店"));
    if (shopMoveUpButton)
        shopMoveUpButton->setText(tr("上移"));
    if (shopMoveDownButton)
        shopMoveDownButton->setText(tr("下移"));
    if (shopItemsTable)
        shopItemsTable->setHorizontalHeaderLabels(
            {tr("顺序"), tr("物品"), tr("文件"), tr("库存")});

    refreshing = wasRefreshing;
    if (!wasRefreshing)
    {
        refreshFromDocument();
        refreshCatalogLists();
    }
}
