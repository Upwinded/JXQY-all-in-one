#pragma once

#include <QWidget>

class MagicRangePreview : public QWidget
{
    Q_OBJECT

public:
    explicit MagicRangePreview(QWidget* parent = nullptr);

    void setRange(int moveKind, int region, int attackRadius, int level);
    QSize sizeHint() const override;

    static bool affectsPreviewCell(
        int moveKind, int region, int level, int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString movementText() const;
    QString rangeText() const;
    QString legendText() const;

    int currentMoveKind = 1;
    int currentRegion = 0;
    int currentAttackRadius = 0;
    int currentLevel = 1;
};
