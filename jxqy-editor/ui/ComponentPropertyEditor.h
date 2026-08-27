#pragma once

#include "AssetDragDrop.h"

#include <QWidget>
#include <QMap>
#include <QRect>
#include <QString>
#include <QStringList>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QGroupBox;
class QSpinBox;
class QToolButton;

namespace Ui
{
class ComponentPropertyEditor;
}

struct ComponentIniProperties
{
    QString name;
    bool nameSet = false;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    QString image;
    QString bitmap;
    QString baseImage;
    QString thumbImage;
    QString align;
    int alignX = 0;
    int alignY = 0;
    QString sound;
    QString kind;
    int up = 0;
    int down = 1;
    int track = 1;
    bool hoverSound = true;
    bool animate = false;
    unsigned int color = 0xFFFFFFFF;
    unsigned int selColor = 0xFFE6C864;
    int font = 18;
    int style = 1;
    int min = 0;
    int max = 72;
    int position = 0;
    int lineSize = 3;
    int pageSize = 9;
    int slideBegin = 5;
    int slideEnd = 145;
    QString slideBtn;
    int itemHeight = 0;
    int itemCount = 0;
    QStringList items;
    int range = 50;
    QString text;
    QString icon;
    QString iconImage;
    int indicateType = 0;
    QString indicateImage;
    double percent = 1.0;
    unsigned int normalColor = 0xFFFFFFFF;
    unsigned int hoverColor = 0xFFFFFF00;
    unsigned int pressColor = 0xFF00FF00;
    bool stretch = false;
    bool keepAspect = false;
    bool centerImage = false;
    bool cropContent = false;
    bool cropBlack = false;
    int frame = -1;
    QString backImage1;
    QString backImage2;
    bool autoShrink = false;
    bool stretchSet = false;
    bool fontSet = false;
    bool bitmapSet = false;
    bool hoverSoundSet = false;
    bool animateSet = false;
    bool keepAspectSet = false;
    bool centerImageSet = false;
    bool cropContentSet = false;
    bool cropBlackSet = false;
    bool frameSet = false;
    bool backImage1Set = false;
    bool backImage2Set = false;
    bool autoShrinkSet = false;
    bool iconSet = false;
    bool iconImageSet = false;
    bool positionSet = false;
    QMap<QString, QString> rawValues;
};

class ComponentPropertyEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ComponentPropertyEditor(QWidget* parent = nullptr);
    ~ComponentPropertyEditor();

    void setComponentType(const QString& type);
    void setProperties(const ComponentIniProperties& props);
    ComponentIniProperties getProperties() const;

    void setDefinitionProperties(const QString& name, const QString& file,
                                  const QString& bind, const QString& format);
    QString getComponentName() const;
    QString getComponentFile() const;
    QString getComponentBind() const;
    QString getComponentFormat() const;
    void setControllerNavigationOverrides(const QString& up, const QString& down,
                                          const QString& left, const QString& right);
    QString getControllerUp() const;
    QString getControllerDown() const;
    QString getControllerLeft() const;
    QString getControllerRight() const;

    void setMenuWindowProperties(const QString& windowFile, int width, int height,
                                  const QString& image, const QString& bitmap,
                                  const QString& align,
                                  int alignX, int alignY, bool stretch = false);
    QString getMenuWindowFile() const;
    int getMenuWindowWidth() const;
    int getMenuWindowHeight() const;
    QString getMenuWindowImage() const;
    QString getMenuWindowBitmap() const;
    QString getMenuWindowAlign() const;
    int getMenuWindowAlignX() const;
    int getMenuWindowAlignY() const;
    bool getMenuWindowStretch() const;

    void setSubMenuWindowFile(const QString& windowFile);
    QString getSubMenuWindowFile() const;
    void setSubMenuBackgroundImage(const QString& backgroundImage);
    QString getSubMenuBackgroundImage() const;
    void setSubMenuWindowRect(const QRect& rect);
    QRect getSubMenuWindowRect() const;

    void setListBoxItems(const QStringList& items);
    QStringList getListBoxItems() const;

    void setAssetsBasePath(const QString& path);
    void setAssetDropRoots(const QStringList& roots);

    void clearAll();

protected:
    void changeEvent(QEvent* event) override;

signals:
    void propertiesChanged();
    void definitionPropertiesChanged();
    void resourceFileEditingFinished();

private slots:
    void onAnyPropertyChanged();
    void onListBoxAddItem();
    void onListBoxRemoveItem();
    void onListBoxEditItem();
    void onItemCountChanged(int count);

private:
    void showGroup(QWidget* group);
    void hideAllTypeSpecificGroups();
    void updateListBoxItemsList();
    void updateTrackVisibility();
    void addColorPickerButton(QLineEdit* colorEdit);
    void updateColorButtonStyle(QPushButton* button, const QString& colorText);
    void retranslateDynamicUi();
    void updateAdvancedPropertyVisibility();
    void setupAssetDropTargets();
    AssetDragDrop::DropResult evaluateAssetDrop(
        const AssetDragDrop::Payload& payload, bool sound) const;

    Ui::ComponentPropertyEditor* ui;

    QString currentType;
    bool updatingFromCode = false;
    QStringList listBoxItems;
    QString assetsBasePath;
    QStringList assetDropRoots;
    ComponentIniProperties currentProperties;

    QToolButton* advancedPropertiesButton = nullptr;
    bool imageAdvancedAvailable = false;
    bool buttonAdvancedAvailable = false;
    bool labelAdvancedAvailable = false;

    QGroupBox* imageOptionsGroup = nullptr;
    QLineEdit* iniNameEdit = nullptr;
    QLineEdit* bitmapEdit = nullptr;
    QSpinBox* frameSpin = nullptr;
    QCheckBox* keepAspectCheck = nullptr;
    QCheckBox* centerImageCheck = nullptr;
    QCheckBox* cropContentCheck = nullptr;
    QCheckBox* cropBlackCheck = nullptr;
    QLineEdit* backImage1Edit = nullptr;
    QLineEdit* backImage2Edit = nullptr;

    QGroupBox* buttonAdvancedGroup = nullptr;
    QCheckBox* hoverSoundCheck = nullptr;
    QCheckBox* animateCheck = nullptr;

    QGroupBox* roundIconGroup = nullptr;
    QLineEdit* iconEdit = nullptr;
    QLineEdit* iconImageEdit = nullptr;

    QGroupBox* labelAdvancedGroup = nullptr;
    QCheckBox* autoShrinkCheck = nullptr;
    QSpinBox* scrollbarPositionSpin = nullptr;
    QLineEdit* menuWindowBitmapEdit = nullptr;
};
