#include "DisplayPropertyEditor.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QStringList>
#include <limits>

namespace
{
QColor displayColor(const QString& value)
{
    QString text = value.trimmed();
    if (text.startsWith('#'))
        return QColor(text);
    if (text.contains(','))
    {
        const auto channels = text.split(',');
        if (channels.size() == 3 || channels.size() == 4)
        {
            int values[] = {0, 0, 0, 255};
            for (int index = 0; index < channels.size(); ++index)
            {
                bool valid = false;
                values[index] = channels[index].trimmed().toInt(&valid);
                if (!valid || values[index] < 0 || values[index] > 255)
                    return {};
            }
            return QColor(values[0], values[1], values[2], values[3]);
        }
        return {};
    }
    bool valid = false;
    const auto number = text.toUInt(&valid, 0);
    return valid ? QColor::fromRgba(number) : QColor();
}
}

DisplayPropertyEditor::DisplayPropertyEditor(ValueType type, QWidget* parent)
    : QWidget(parent), valueType(type)
{
    QHBoxLayout* layout = nullptr;
    if (type == ValueType::Text)
    {
        auto* form = new QFormLayout(this);
        form->setContentsMargins(0, 0, 0, 0);
        lineEdit = new QLineEdit(this);
        form->addRow(lineEdit);
        connect(lineEdit, &QLineEdit::editingFinished, this, [this]()
        {
            if (!updating) commit(lineEdit->text());
        });
        refresh();
        return;
    }
    layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    if (type == ValueType::Boolean)
    {
        booleanCombo = new QComboBox(this);
        booleanCombo->setObjectName("booleanValue");
        booleanCombo->addItem(tr("使用默认"), QString());
        booleanCombo->addItem(tr("开启"), "true");
        booleanCombo->addItem(tr("关闭"), "false");
        layout->addWidget(booleanCombo, 1);
        connect(booleanCombo, &QComboBox::currentIndexChanged, this, [this](int index)
        {
            if (!updating && index >= 0)
                commit(booleanCombo->itemData(index).toString());
        });
    }
    else if (type == ValueType::Color)
    {
        colorButton = new QPushButton(this);
        colorButton->setObjectName("colorValue");
        layout->addWidget(colorButton, 1);
        connect(colorButton, &QPushButton::clicked, this, [this]()
        {
            QColor initial = displayColor(currentValue.isEmpty() ? defaultValue : currentValue);
            if (!initial.isValid()) initial = Qt::white;
            const QColor selected = QColorDialog::getColor(initial, this, tr("选择颜色"), QColorDialog::ShowAlphaChannel);
            if (selected.isValid())
                commit(QString("0x%1").arg(selected.rgba(), 8, 16, QChar('0')).toUpper().replace("0X", "0x"));
        });
    }
    else
    {
        defaultButton = new QPushButton(tr("使用默认"), this);
        layout->addWidget(defaultButton, 1);
        if (type == ValueType::Percentage)
        {
            percentageSpin = new QDoubleSpinBox(this);
            percentageSpin->setObjectName("percentageValue");
            percentageSpin->setRange(0.01, 10000.0);
            percentageSpin->setDecimals(2);
            percentageSpin->setSuffix(" %");
            percentageSpin->setKeyboardTracking(false);
            layout->addWidget(percentageSpin, 1);
            connect(percentageSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value)
            {
                if (!updating) commit(QString::number(value / 100.0, 'g', 12));
            });
        }
        else
        {
            integerSpin = new QSpinBox(this);
            integerSpin->setObjectName("integerValue");
            integerSpin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            integerSpin->setKeyboardTracking(false);
            layout->addWidget(integerSpin, 1);
            connect(integerSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value)
            {
                if (!updating) commit(QString::number(value));
            });
        }
        connect(defaultButton, &QPushButton::clicked, this, [this]()
        {
            QString value = defaultValue.isEmpty() ? (percentageSpin ? "1" : "0") : defaultValue;
            if (integerSpin) value = QString::number(qBound(integerSpin->minimum(), value.toInt(), integerSpin->maximum()));
            commit(value);
            if (integerSpin) { integerSpin->setFocus(); integerSpin->selectAll(); }
            if (percentageSpin) { percentageSpin->setFocus(); percentageSpin->selectAll(); }
        });
    }
    resetButton = new QToolButton(this);
    resetButton->setObjectName("resetValue");
    resetButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    resetButton->setToolTip(tr("恢复默认值"));
    layout->addWidget(resetButton);
    connect(resetButton, &QToolButton::clicked, this, [this]() { commit({}); });
    refresh();
}

QString DisplayPropertyEditor::text() const { return currentValue; }

void DisplayPropertyEditor::setText(const QString& value)
{
    if (value == currentValue) return;
    currentValue = value;
    refresh();
    emit textChanged(currentValue);
}

void DisplayPropertyEditor::setPlaceholderText(const QString& value)
{
    placeholder = value;
    if (lineEdit) lineEdit->setPlaceholderText(value);
    setToolTip(value);
}

void DisplayPropertyEditor::setBooleanLabels(const QString& enabled, const QString& disabled)
{
    if (!booleanCombo) return;
    booleanCombo->setItemText(1, enabled);
    booleanCombo->setItemText(2, disabled);
    refresh();
}

void DisplayPropertyEditor::setDefaultValue(const QString& value)
{
    if (defaultValue == value) return;
    defaultValue = value;
    refresh();
}

void DisplayPropertyEditor::setIntegerRange(int minimum, int maximum)
{
    if (!integerSpin) return;
    updating = true;
    integerSpin->setRange(minimum, maximum);
    updating = false;
    refresh();
}

void DisplayPropertyEditor::commit(const QString& value)
{
    if (updating || currentValue == value) return;
    setText(value);
    emit editingFinished();
}

void DisplayPropertyEditor::refresh()
{
    updating = true;
    const bool inherited = currentValue.isEmpty();
    if (lineEdit) lineEdit->setText(currentValue);
    if (booleanCombo)
    {
        const QString normalized = currentValue.trimmed().toLower();
        const bool enabled = QStringList{"true", "1", "yes", "on"}.contains(normalized);
        const bool disabled = QStringList{"false", "0", "no", "off"}.contains(normalized);
        const int index = inherited ? 0 : enabled ? 1 : disabled ? 2 : -1;
        booleanCombo->setCurrentIndex(index);
        const bool defaultEnabled = QStringList{"true", "1", "yes", "on"}.contains(defaultValue.toLower());
        booleanCombo->setItemText(0, defaultValue.isEmpty() ? tr("使用默认")
            : tr("默认：%1").arg(booleanCombo->itemText(defaultEnabled ? 1 : 2)));
        booleanCombo->setToolTip(index < 0 ? tr("配置值无效：%1").arg(currentValue) : placeholder);
    }
    if (defaultButton)
    {
        defaultButton->setVisible(inherited);
        const QString displayDefault = percentageSpin && !defaultValue.isEmpty()
            ? QString::number(defaultValue.toDouble() * 100.0, 'g', 8) + " %" : defaultValue;
        defaultButton->setText(displayDefault.isEmpty() ? tr("使用默认（点击设置）") : tr("默认：%1").arg(displayDefault));
    }
    if (integerSpin)
    {
        integerSpin->setVisible(!inherited);
        bool valid = false;
        const auto value = currentValue.toLongLong(&valid, 0);
        if (valid && value >= integerSpin->minimum() && value <= integerSpin->maximum())
            integerSpin->setValue(static_cast<int>(value));
        integerSpin->setToolTip(valid ? placeholder : tr("配置值无效：%1").arg(currentValue));
    }
    if (percentageSpin)
    {
        percentageSpin->setVisible(!inherited);
        bool valid = false;
        const double value = currentValue.toDouble(&valid);
        if (valid) percentageSpin->setValue(value * 100.0);
    }
    if (colorButton)
    {
        const QColor color = displayColor(inherited ? defaultValue : currentValue);
        colorButton->setText(inherited ? tr("默认颜色") : tr("选择颜色…"));
        colorButton->setStyleSheet(color.isValid()
            ? QString("QPushButton { border-left: 28px solid %1; }").arg(color.name(QColor::HexArgb)) : QString());
        colorButton->setToolTip(currentValue.isEmpty() ? placeholder : currentValue);
    }
    if (resetButton) resetButton->setEnabled(!inherited);
    updating = false;
}
