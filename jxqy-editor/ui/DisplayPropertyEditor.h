#pragma once

#include <QWidget>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QToolButton;
class QLineEdit;

// An optional INI display value with an appropriate editing control.
class DisplayPropertyEditor : public QWidget
{
    Q_OBJECT

public:
    enum class ValueType { Text, Boolean, Integer, Percentage, Color };
    explicit DisplayPropertyEditor(ValueType type, QWidget* parent = nullptr);
    QString text() const;
    void setText(const QString& value);
    void clear() { setText({}); }
    void setPlaceholderText(const QString& value);
    void setDefaultValue(const QString& value);
    void setIntegerRange(int minimum, int maximum);
    void setBooleanLabels(const QString& enabled, const QString& disabled);
    QLineEdit* textInput() const { return lineEdit; }

signals:
    void textChanged(const QString& value);
    void editingFinished();

private:
    void refresh();
    void commit(const QString& value);
    ValueType valueType;
    QString currentValue;
    QString defaultValue;
    QString placeholder;
    bool updating = false;
    QComboBox* booleanCombo = nullptr;
    QLineEdit* lineEdit = nullptr;
    QSpinBox* integerSpin = nullptr;
    QDoubleSpinBox* percentageSpin = nullptr;
    QPushButton* colorButton = nullptr;
    QPushButton* defaultButton = nullptr;
    QToolButton* resetButton = nullptr;
};
