#include "MenuEditorWindow.h"
#include "ui_MenuEditorWindow.h"
#include "../../src/Types/ComponentTypes.h"

#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

void MenuEditorWindow::setupPreviewControls()
{
    auto* panel = new QWidget(this);
    panel->setObjectName("menuPreviewWorkspace");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto* controls = new QHBoxLayout();
    previewModeCombo = new QComboBox(panel);
    previewModeCombo->setObjectName("menuPreviewMode");
    previewModeCombo->addItem(tr("编辑界面"));
    previewModeCombo->addItem(tr("预览游戏窗口"));
    controls->addWidget(previewModeCombo);
    auto* guides = new QCheckBox(tr("显示辅助框"), panel);
    guides->setObjectName("menuEditingGuides");
    guides->setChecked(true);
    controls->addWidget(guides);
    connect(guides, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    controls->addStretch();
    auto* fit = new QPushButton(tr("适合窗口"), panel);
    fit->setObjectName("menuPreviewFit");
    controls->addWidget(fit);
    auto* actual = new QPushButton(tr("100%"), panel);
    actual->setObjectName("menuPreviewActualSize");
    controls->addWidget(actual);
    previewZoomSpin = new QDoubleSpinBox(panel);
    previewZoomSpin->setObjectName("menuPreviewZoom");
    previewZoomSpin->setRange(1, 800);
    previewZoomSpin->setDecimals(0);
    previewZoomSpin->setSuffix(" %");
    previewZoomSpin->setValue(100);
    previewZoomSpin->setKeyboardTracking(false);
    previewZoomSpin->setToolTip(tr("查看倍率只改变编辑器显示，不改变游戏布局"));
    controls->addWidget(previewZoomSpin);
    layout->addLayout(controls);

    previewSizeControls = new QWidget(panel);
    auto* sizes = new QHBoxLayout(previewSizeControls);
    sizes->setContentsMargins(0, 0, 0, 0);
    sizes->addWidget(new QLabel(tr("游戏窗口"), panel));
    previewWidthSpin = new QSpinBox(panel);
    previewWidthSpin->setObjectName("menuPreviewWidth");
    previewWidthSpin->setRange(640, 8192);
    previewWidthSpin->setValue(1280);
    previewWidthSpin->setKeyboardTracking(false);
    previewHeightSpin = new QSpinBox(panel);
    previewHeightSpin->setObjectName("menuPreviewHeight");
    previewHeightSpin->setRange(480, 8192);
    previewHeightSpin->setValue(720);
    previewHeightSpin->setKeyboardTracking(false);
    sizes->addWidget(previewWidthSpin);
    sizes->addWidget(new QLabel(QString::fromUtf8("×"), panel));
    sizes->addWidget(previewHeightSpin);
    sizes->addWidget(new QLabel(tr("像素"), panel));
    sizes->addStretch();
    layout->addWidget(previewSizeControls);
    auto* statesPanel = new QWidget(panel);
    statesPanel->setObjectName("componentStateControls");
    auto* states = new QHBoxLayout(statesPanel);
    states->setContentsMargins(0, 0, 0, 0);
    states->addWidget(new QLabel(tr("按钮状态"), panel));
    previewButtonState = new QComboBox(panel);
    previewButtonState->setObjectName("menuPreviewButtonState");
    previewButtonState->addItems({tr("正常"), tr("悬停"), tr("按下")});
    states->addWidget(previewButtonState);
    states->addSpacing(12);
    states->addWidget(new QLabel(tr("滑块位置"), panel));
    previewSliderPosition = new QSpinBox(panel);
    previewSliderPosition->setObjectName("menuPreviewSliderPosition");
    previewSliderPosition->setRange(0, 100);
    previewSliderPosition->setSuffix(" %");
    previewSliderPosition->setValue(50);
    previewSliderPosition->setKeyboardTracking(false);
    states->addWidget(previewSliderPosition);
    states->addStretch();
    auto* previewOnly = new QLabel(tr("仅预览，不保存"), panel);
    states->addWidget(previewOnly);
    layout->addWidget(statesPanel);
    choicePreviewControls = new QWidget(panel);
    auto* choiceLayout = new QHBoxLayout(choicePreviewControls);
    choiceLayout->setContentsMargins(0, 0, 0, 0);
    choicePreviewText = new QPlainTextEdit(choicePreviewControls);
    choicePreviewText->setObjectName("choicePreviewText");
    choicePreviewText->setPlaceholderText(tr("每行一个预览选项；留空使用示例。实际文字和分支在剧情脚本中修改。"));
    choicePreviewText->setMaximumHeight(80);
    choiceLayout->addWidget(choicePreviewText, 1);
    auto* script = new QPushButton(tr("编辑剧情脚本…"), choicePreviewControls);
    script->setToolTip(tr("脚本中的选择指令提供文字，返回的选项序号用于判断剧情分支。此处只编辑列表外观。"));
    connect(script, &QPushButton::clicked, this, &MenuEditorWindow::openScriptEditorRequested);
    choiceLayout->addWidget(script);
    layout->addWidget(choicePreviewControls);
    choicePreviewControls->hide();
    connect(choicePreviewText, &QPlainTextEdit::textChanged, this, [this]() { updatePreview(); });
    connect(previewButtonState, &QComboBox::currentIndexChanged, this, [this]() { updatePreview(); });
    connect(previewSliderPosition, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { updatePreview(); });
    ui->mainSplitter->replaceWidget(1, panel);
    layout->addWidget(ui->previewScrollArea, 1);
    layout->addWidget(findChild<QLabel*>("conversationLayoutResult"));
    ui->previewScrollArea->viewport()->installEventFilter(this);

    const auto changedSize = [this]()
    {
        updatePreview();
    };
    connect(previewWidthSpin, qOverload<int>(&QSpinBox::valueChanged), this, changedSize);
    connect(previewHeightSpin, qOverload<int>(&QSpinBox::valueChanged), this, changedSize);
    connect(previewModeCombo, &QComboBox::currentIndexChanged, this, [this]()
    {
        previewAutoFit = true;
        if (previewModeCombo->currentIndex() == 0)
        {
            const QSignalBlocker blocker(toolTipPreviewCombo);
            toolTipPreviewCombo->setCurrentIndex(0);
        }
        updatePreview();
    });
    connect(fit, &QPushButton::clicked, this, [this]() { previewAutoFit = true; schedulePreviewFit(); });
    connect(actual, &QPushButton::clicked, this, [this]()
    {
        previewAutoFit = false;
        previewCanvas->setScaleFactor(1);
        const QSignalBlocker blocker(previewZoomSpin);
        previewZoomSpin->setValue(100);
    });
    connect(previewZoomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value)
    {
        previewAutoFit = false;
        previewCanvas->setScaleFactor(value / 100.0);
    });
    previewSizeControls->hide();
}

QSize MenuEditorWindow::previewWindowSize() const
{
    return previewWidthSpin ? QSize(previewWidthSpin->value(), previewHeightSpin->value()) : QSize(1280, 720);
}

void MenuEditorWindow::schedulePreviewFit()
{
    if (!previewAutoFit || previewFitPending || !previewCanvas) return;
    previewFitPending = true;
    QTimer::singleShot(0, this, [this]()
    {
        previewFitPending = false;
        if (!previewAutoFit || !isVisible()) return;
        const QSize available = ui->previewScrollArea->viewport()->size() - QSize(40, 40);
        const QSize content = previewCanvas->worldBounds.size();
        if (available.width() <= 0 || available.height() <= 0 || !content.isValid()) return;
        const double scale = std::min({1.0, double(available.width()) / content.width(), double(available.height()) / content.height()});
        if (std::abs(previewCanvas->getScaleFactor() - scale) > 0.0001) previewCanvas->setScaleFactor(scale);
        const QSignalBlocker blocker(previewZoomSpin);
        previewZoomSpin->setValue(scale * 100.0);
    });
}

void MenuEditorWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    schedulePreviewFit();
}

bool MenuEditorWindow::eventFilter(QObject* object, QEvent* event)
{
    if (object == ui->previewScrollArea->viewport() && event->type() == QEvent::Resize) schedulePreviewFit();
    return QWidget::eventFilter(object, event);
}

void MenuEditorWindow::finishPreviewPresentation()
{
    if (!previewModeCombo || !previewCanvas) return;
    const bool simulation = previewModeCombo->currentIndex() == 1;
    auto* guides = findChild<QCheckBox*>("menuEditingGuides");
    guides->setVisible(!simulation);
    previewCanvas->setEditingGuidesVisible(!simulation && guides->isChecked());
    choicePreviewControls->setVisible(selectedPage == "choose" && simulation);
    bool hasButtons = false, hasSliders = false;
    std::function<void(PreviewComponent&)> showState = [&](PreviewComponent& component)
    {
        hasButtons |= component.type.contains("Button") || component.type == "CheckBox";
        hasSliders |= component.type == "Scrollbar";
        for (const QString& key : {QString("normalcolor"), QString("hovercolor"), QString("presscolor"), QString("backgroundcolor"),
            QString("hoverbackgroundcolor"), QString("pressedbackgroundcolor"), QString("bordercolor")})
            if (component.rawValues.contains(key)) component.rawValues[key] = QString::number(displayColor(component.rawValues[key], component.color));
        previewCanvas->applyButtonPreviewState(component, previewButtonState->currentIndex());
        if (component.type == "Scrollbar" && !component.children.isEmpty())
        {
            auto& slider = component.children.first();
            const bool vertical = component.rawValues.value("style", "1").toInt() == 1;
            const bool fitted = QStringList{"1", "true", "yes", "on"}.contains(component.rawValues.value("fittrack").toLower());
            const int begin = fitted ? 0 : component.rawValues.value("slidebegin", "5").toInt();
            const int end = fitted ? qMax(0, vertical ? component.rect.height() - slider.rect.height() : component.rect.width() - slider.rect.width())
                : component.rawValues.value("slideend", "145").toInt();
            const int position = qRound(begin + (end - begin) * previewSliderPosition->value() / 100.0);
            if (vertical) slider.rect.moveTop(component.rect.top() + position);
            else slider.rect.moveLeft(component.rect.left() + position);
        }
        for (auto& child : component.children) showState(child);
    };
    for (auto& component : previewCanvas->components) showState(component);
    for (auto& submenu : previewCanvas->subMenus)
        for (auto& component : submenu.components) showState(component);
    findChild<QWidget*>("componentStateControls")->setVisible(hasButtons || hasSliders);
    previewButtonState->setEnabled(hasButtons);
    previewSliderPosition->setEnabled(hasSliders);
    QList<QPixmap> thumbnails;
    if (auto* selected = previewCanvas->findComponentById(previewCanvas->getSelectedComponent()))
        if (selected->type.contains("Button") || selected->type == "CheckBox")
            for (int state = 0; state < 3; ++state) thumbnails.append(previewCanvas->componentThumbnail(*selected, state));
    propertyEditor->setButtonStatePreviews(thumbnails);
    ui->menuConfigWidget->setVisible(selectedPage.isEmpty());
    previewSizeControls->setVisible(simulation);
    const QStringList alreadyResolved = {"dialog", "choose", "message", "timer", "titleteam", "systemnotice", "scriptmessages", "top", "bottom", "column", "mapthumbnail", "joystick", "skills", "npcinfo", "controllerhelp", "dicegame"};
    if (simulation && !alreadyResolved.contains(selectedPage) && !currentDefinition.windowFile.isEmpty())
    {
        auto window = propertiesForResource(currentDefinition.windowFile);
        window.width = previewCanvas->menuRect.width();
        window.height = previewCanvas->menuRect.height();
        if (selectedPage == "title")
        {
            // Title forces these after reading the INI, including legacy resources without them.
            window.align = "alClient";
            window.alignX = window.alignY = 0;
            window.keepAspect = true;
        }
        const QRect rectangle = previewPanelRectangle(window, previewWindowSize());
        previewInterfaceOrigin = rectangle.topLeft();
        bool numericAlignment = false;
        const int alignment = window.align.toInt(&numericAlignment, 0);
        const bool client = window.align.compare("alClient", Qt::CaseInsensitive) == 0 || (numericAlignment && alignment == alClient);
        previewCanvas->setWindowStretch(window.stretch || client, window.keepAspect);
        if (window.width > 0 && window.height > 0)
        {
            float sx = float(rectangle.width()) / window.width, sy = float(rectangle.height()) / window.height;
            if (window.keepAspect) sx = sy = std::min(sx, sy);
            const double uniform = std::min(double(rectangle.width()) / window.width, double(rectangle.height()) / window.height);
            const QPoint offset = window.keepAspect ? QPoint((rectangle.width() - int(std::round(window.width * uniform))) / 2,
                                                            (rectangle.height() - int(std::round(window.height * uniform))) / 2) : QPoint();
            // Panel supplies the centered offset; Element truncates each scaled coordinate and extent.
            const auto scaled = [&](const QRect& source, const QPoint& origin)
            {
                return QRect(int(source.x() * sx) + origin.x(), int(source.y() * sy) + origin.y(),
                             int(source.width() * sx), int(source.height() * sy));
            };
            std::function<void(PreviewComponent&, const QPoint&)> transformComponent = [&](PreviewComponent& component, const QPoint& origin)
            {
                component.rect = scaled(component.rect, origin);
                for (auto& child : component.children) transformComponent(child, origin);
            };
            for (auto& component : previewCanvas->components) transformComponent(component, offset);
            for (auto& submenu : previewCanvas->subMenus)
            {
                submenu.windowRect = scaled(submenu.windowRect, offset);
                for (auto& component : submenu.components) transformComponent(component, {});
            }
            previewCanvas->menuRect = QRect(QPoint(), rectangle.size());
        }
    }
    previewCanvas->setAspectFitViewport({});
    if (selectedPage == "option" && optionViewCombo->currentIndex() > 0)
        previewCanvas->setWindowPreviewVisible(false);
    previewCanvas->setGameWindowPreview(simulation ? previewWindowSize() : QSize(), simulation ? previewInterfaceOrigin : QPoint());
    toolTipPreviewAction->setVisible(simulation && toolTipPreviewCombo->count() > 1 && selectedPage != "title");
    schedulePreviewFit();
}
