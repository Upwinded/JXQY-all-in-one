#include "MagicEditorWindow.h"
#include "FilePickerHelper.h"
#include "../core/ContentAuthoring.h"
#include "../core/EditorAssetPath.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <climits>

QWidget* MagicEditorWindow::createLinkedEffectsPage()
{
    auto* page = new QWidget(this);
    page->setObjectName("magicLinkedEffectsPage");
    auto* layout = new QVBoxLayout(page);
    linkedLevelCombo = new QComboBox(page);
    linkedLevelCombo->setObjectName("magicLinkedLevelCombo");
    layout->addWidget(linkedLevelCombo);
    auto* hint = new QLabel(page);
    hint->setObjectName("magicLinkedHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    for (const QString& key : MagicDocument::linkedFileKeys() + MagicDocument::linkedParameterKeys())
    {
        auto* row = new QWidget(page);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        QWidget* editor;
        if (key == "Parasitic" || key == "BounceFly" || key == "JumpToTarget" || key == "ExplodeWhenLifeFrameEnd") editor = new QCheckBox(row);
        else
        {
            auto* edit = new QLineEdit(row);
            if (MagicDocument::linkedParameterKeys().contains(key))
                edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,20}"), edit));
            connect(edit, &QLineEdit::editingFinished, this, &MagicEditorWindow::commitPendingTextEditors);
            editor = edit;
        }
        editor->setObjectName("magicLinked_" + key);
        editor->setToolTip(key);
        linkedEditors.insert(key, editor);
        rowLayout->addWidget(editor, 1);
        auto changed = [this]()
        {
            if (refreshing) return;
            linkedErrorLabel->setVisible(!linkedInputsValid());
            updateWindowTitle();
            updateActionStates();
            emit documentStatesChanged();
        };
        if (auto* edit = qobject_cast<QLineEdit*>(editor)) connect(edit, &QLineEdit::textChanged, this, changed);
        else connect(qobject_cast<QCheckBox*>(editor), &QCheckBox::toggled, this, [this, changed]()
        {
            changed();
            commitPendingTextEditors();
        });
        if (MagicDocument::linkedFileKeys().contains(key))
        {
            auto* choose = new QPushButton(tr("选择…"), row);
            choose->setObjectName("magicLinkedChoose_" + key);
            choose->setProperty("linkedChoose", true);
            rowLayout->addWidget(choose);
            connect(choose, &QPushButton::clicked, this, [this, editor]()
            {
                QString relative;
                const QString file = FilePickerHelper::chooseResourceFile(this, assetsBasePath, {"ini/magic"}, {"*.ini"}, {}, &relative);
                if (file.isEmpty()) return;
                qobject_cast<QLineEdit*>(editor)->setText(relative.mid(10).replace('/', '\\'));
                commitPendingTextEditors();
            });
            auto* open = new QPushButton(tr("打开"), row);
            open->setObjectName("magicLinkedOpen_" + key);
            open->setProperty("linkedOpen", true);
            rowLayout->addWidget(open);
            connect(open, &QPushButton::clicked, this, [this, key]()
            {
                if (!linkedInputsValid()) return;
                QString relative = linkedEditorValue(key).trimmed();
                relative.replace('\\', '/');
                if (relative.startsWith("ini/magic/", Qt::CaseInsensitive)) relative.remove(0, 10);
                const QString file = ContentAuthoring::resolve(assetsBasePath, "ini/magic/" + relative);
                if (file.isEmpty()) QMessageBox::information(this, tr("无法打开"), tr("关联武功文件不存在或尚未选择。"));
                else
                {
                    commitPendingTextEditors();
                    emit openMagicFileRequested(file);
                }
            });
        }
        auto* reset = new QPushButton(tr("还原通用"), row);
        reset->setObjectName("magicLinkedReset_" + key);
        linkedResetButtons.insert(key, reset);
        rowLayout->addWidget(reset);
        connect(reset, &QPushButton::clicked, this, [this, key]()
        {
            if (!linkedInputsValid()) return;
            commitPendingTextEditors();
            const auto before = document.serializedBytes();
            auto pending = document;
            pending.removeLinkedOverride(key, linkedEditLevel);
            pushSnapshotChange(before, pending.serializedBytes(), tr("还原关联触发通用值"));
        });
        auto* label = new QLabel(page);
        label->setObjectName("magicLinkedLabel_" + key);
        form->addRow(label, row);
    }
    linkedErrorLabel = new QLabel(page);
    linkedErrorLabel->setObjectName("magicLinkedInputError");
    linkedErrorLabel->setStyleSheet("color: #d24646;");
    linkedErrorLabel->setWordWrap(true);
    linkedErrorLabel->hide();
    layout->addWidget(linkedErrorLabel);
    layout->addStretch();
    connect(linkedLevelCombo, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (refreshing) return;
        if (!linkedInputsValid())
        {
            const QSignalBlocker block(linkedLevelCombo);
            linkedLevelCombo->setCurrentIndex(linkedEditLevel);
            return;
        }
        commitPendingTextEditors();
        linkedEditLevel = index;
        refreshLinkedEffects();
    });
    retranslateLinkedEffects();
    return page;
}

QString MagicEditorWindow::linkedEditorValue(const QString& key) const
{
    QWidget* editor = linkedEditors.value(key);
    if (const auto* edit = qobject_cast<QLineEdit*>(editor)) return edit->text();
    if (const auto* check = qobject_cast<QCheckBox*>(editor)) return check->isChecked() ? "1" : "0";
    return {};
}

bool MagicEditorWindow::linkedInputsValid() const
{
    for (auto it = linkedEditors.begin(); it != linkedEditors.end(); ++it)
    {
        const QString value = linkedEditorValue(it.key());
        if (value == loadedLinkedValues.value(it.key())) continue;
        if (MagicDocument::linkedFileKeys().contains(it.key()))
        {
            QString normalized;
            if (!value.trimmed().isEmpty() && !EditorAssetPath::normalizeResourcePath(value.trimmed(), normalized)) return false;
        }
        else
        {
            bool valid;
            const qlonglong number = value.toLongLong(&valid);
            const bool time = it.key() == "FlyInterval" || it.key() == "SecondMagicDelay" || it.key() == "ParasiticInterval";
            if (!valid || number < 0 || (!time && number > INT_MAX) || (it.key() == "RandMagicProbability" && number > 100)) return false;
        }
    }
    return true;
}

void MagicEditorWindow::appendLinkedEdits(MagicDocument& pending) const
{
    for (auto it = linkedEditors.begin(); it != linkedEditors.end(); ++it)
    {
        QString value = linkedEditorValue(it.key());
        if (value == loadedLinkedValues.value(it.key())) continue;
        if (MagicDocument::linkedFileKeys().contains(it.key()))
        {
            value = value.trimmed();
            value.replace('\\', '/');
            if (value.startsWith("ini/magic/", Qt::CaseInsensitive)) value.remove(0, 10);
            value.replace('/', '\\');
        }
        else
        {
            bool valid;
            const qlonglong number = value.toLongLong(&valid);
            const bool time = it.key() == "FlyInterval" || it.key() == "SecondMagicDelay" || it.key() == "ParasiticInterval";
            if (!valid || number < 0 || (!time && number > INT_MAX) || (it.key() == "RandMagicProbability" && number > 100)) continue;
            value = QString::number(number);
        }
        pending.setLinkedValue(it.key(), value, linkedEditLevel);
    }
}

void MagicEditorWindow::refreshLinkedEffects()
{
    if (!linkedLevelCombo) return;
    const bool wasRefreshing = refreshing;
    refreshing = true;
    {
        const QSignalBlocker block(linkedLevelCombo);
        linkedLevelCombo->setCurrentIndex(linkedEditLevel);
    }
    for (auto it = linkedEditors.begin(); it != linkedEditors.end(); ++it)
    {
        QString value = document.linkedValue(it.key(), linkedEditLevel);
        if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
        {
            if (value.isEmpty() && MagicDocument::linkedParameterKeys().contains(it.key())) value = "0";
            edit->setText(value);
        }
        else qobject_cast<QCheckBox*>(it.value())->setChecked(value.toLongLong(nullptr, 0) > 0);
        loadedLinkedValues[it.key()] = linkedEditorValue(it.key());
        auto* reset = linkedResetButtons.value(it.key());
        reset->setVisible(linkedEditLevel > 0 && it.key() != "ExplodeWhenLifeFrameEnd");
        reset->setEnabled(document.hasLinkedOverride(it.key(), linkedEditLevel));
    }
    linkedErrorLabel->hide();
    refreshing = wasRefreshing;
}

void MagicEditorWindow::retranslateLinkedEffects()
{
    if (!linkedLevelCombo) return;
    const QSignalBlocker blocker(linkedLevelCombo);
    linkedLevelCombo->clear();
    linkedLevelCombo->addItem(tr("通用值（没有单独设置的等级使用）"), 0);
    for (int level = 1; level <= MagicDocument::MaximumLevel; ++level) linkedLevelCombo->addItem(tr("仅第 %1 级").arg(level), level);
    linkedLevelCombo->setCurrentIndex(linkedEditLevel);
    findChild<QLabel*>("magicLinkedHint")->setText(tr("选择触发时使用的武功及参数。等级未单独设置时沿用通用值；还原通用会移除该等级的单独设置。留空的关联不触发，完整连锁效果请试玩检查。"));
    const QMap<QString, QString> labels = {
        {"FlyMagic", tr("飞行途中触发")}, {"ExplodeMagicFile", tr("爆炸时触发")},
        {"ParasiticMagic", tr("寄生时触发")}, {"RandMagicFile", tr("概率追加武功")},
        {"SecondMagicFile", tr("施放后追加武功")}, {"MagicWhenNewPos", tr("移到新格时触发")},
        {"MagicToUseWhenKillEnemy", tr("击败目标后触发")}, {"BounceFlyEndMagic", tr("击退结束时触发")},
        {"ChangeMagic", tr("累计命中后替换武功")}, {"JumpEndMagic", tr("跳跃结束时触发")},
        {"FlyInterval", tr("飞行触发间隔（毫秒）")}, {"RandMagicProbability", tr("追加概率（0—100%）")},
        {"SecondMagicDelay", tr("追加等待（毫秒）")}, {"Parasitic", tr("启用寄生")},
        {"ParasiticInterval", tr("寄生触发间隔（毫秒）")}, {"ParasiticMaxEffect", tr("寄生最大累计效果值")},
        {"HitCountToChangeMagic", tr("替换所需命中次数")}, {"BounceFly", tr("启用击退")},
        {"JumpToTarget", tr("启用跳向目标")}, {"ExplodeWhenLifeFrameEnd", tr("持续时间结束时也爆炸（所有等级）")}};
    for (auto it = labels.begin(); it != labels.end(); ++it) findChild<QLabel*>("magicLinkedLabel_" + it.key())->setText(it.value());
    if (linkedErrorLabel) linkedErrorLabel->setText(tr("请检查关联路径和参数：文件使用相对路径，数值使用非负整数，概率为0至100。当前输入已保留。"));
    for (auto* button : linkedResetButtons) button->setText(tr("还原通用"));
    for (auto* button : findChildren<QPushButton*>())
    {
        if (button->property("linkedChoose").toBool()) button->setText(tr("选择…"));
        if (button->property("linkedOpen").toBool()) button->setText(tr("打开"));
    }
}
