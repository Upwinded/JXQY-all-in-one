#include "MagicRangePreview.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace
{
constexpr int MovementPathLength = 4;
using PreviewTiles = QSet<QPoint>;

struct PreviewArrow
{
    QPoint from;
    QPoint to;
};

struct PreviewGeometry
{
    PreviewTiles primaryTiles;
    PreviewTiles secondaryTiles;
    PreviewTiles actorDestinationTiles;
    QVector<PreviewArrow> arrows;
    QVector<PreviewArrow> connections;
    QPoint casterTile = QPoint(0, 0);
};

int regionShapeRange(int level)
{
    level = std::clamp(level, 1, 10);
    return 3 + ((level - 1) / 3) * 2;
}

int sectorProjectileCount(int level)
{
    level = std::clamp(level, 1, 10);
    return 3 + ((level - 1) / 3) * 2;
}

QPoint stepTile(QPoint from, int direction)
{
    direction = ((direction % 8) + 8) % 8;
    const int line = std::abs(from.y() % 2);
    switch (direction)
    {
    case 0: from.ry() += 2; break;
    case 1: from += QPoint(line - 1, 1); break;
    case 2: from.rx() -= 1; break;
    case 3: from += QPoint(line - 1, -1); break;
    case 4: from.ry() -= 2; break;
    case 5: from += QPoint(line, -1); break;
    case 6: from.rx() += 1; break;
    case 7: from += QPoint(line, 1); break;
    default: break;
    }
    return from;
}

QPoint advanceTile(QPoint from, int direction, int count)
{
    for (int index = 0; index < count; ++index)
        from = stepTile(from, direction);
    return from;
}

void addMovingEndpoints(
    PreviewGeometry& geometry, QPoint start, int direction)
{
    geometry.primaryTiles.insert(start);
    geometry.secondaryTiles.insert(
        advanceTile(start, direction, MovementPathLength));
}

void addTravelPreview(PreviewGeometry& geometry)
{
    const QPoint caster(0, 0);
    const QPoint start = stepTile(caster, 6);
    const QPoint destination = advanceTile(
        caster, 6, MovementPathLength);
    geometry.primaryTiles.insert(start);
    geometry.secondaryTiles.insert(destination);
    geometry.arrows.push_back({start, destination});
}

PreviewGeometry previewGeometry(int moveKind, int region, int level)
{
    level = std::clamp(level, 1, 10);
    const QPoint caster(0, 0);
    PreviewGeometry geometry;

    switch (moveKind)
    {
    case 1:
    case 22:
        geometry.primaryTiles.insert(
            advanceTile(caster, 6, MovementPathLength));
        break;
    case 2:
    case 3:
    case 16:
    case 17:
        addTravelPreview(geometry);
        break;
    case 4:
    case 5:
    case 6:
        geometry.primaryTiles.insert(caster);
        for (int direction = 0; direction < 8; ++direction)
        {
            const QPoint destination = advanceTile(caster, direction, 3);
            geometry.secondaryTiles.insert(destination);
            geometry.arrows.push_back({caster, destination});
        }
        break;
    case 7:
    case 8:
    {
        geometry.primaryTiles.insert(caster);
        const int projectileCount = sectorProjectileCount(level);
        QPoint destination = advanceTile(
            advanceTile(caster, 6, MovementPathLength),
            4,
            projectileCount / 2);
        for (int projectile = 0; projectile < projectileCount; ++projectile)
        {
            geometry.secondaryTiles.insert(destination);
            geometry.arrows.push_back({caster, destination});
            destination = stepTile(destination, 0);
        }
        break;
    }
    case 9:
    {
        QPoint tile = advanceTile(
            advanceTile(caster, 6, 3), 4, level);
        for (int column = 0; column < level * 2 + 1; ++column)
        {
            geometry.primaryTiles.insert(tile);
            tile = stepTile(tile, 0);
        }
        break;
    }
    case 10:
    {
        QPoint tile = advanceTile(caster, 4, level);
        for (int column = 0; column < level * 2 + 1; ++column)
        {
            addMovingEndpoints(geometry, tile, 6);
            tile = stepTile(tile, 0);
        }
        geometry.arrows.push_back({
            caster,
            advanceTile(caster, 6, MovementPathLength)});
        break;
    }
    case 24:
    {
        PreviewTiles formation = {caster};
        QPoint upper = caster;
        QPoint lower = caster;
        for (int step = 1; step <= level; ++step)
        {
            upper = stepTile(upper, 5);
            lower = stepTile(lower, 7);
            formation.insert(upper);
            formation.insert(lower);
        }
        for (const QPoint& tile : formation)
            addMovingEndpoints(geometry, tile, 6);
        geometry.arrows.push_back({
            caster,
            advanceTile(caster, 6, MovementPathLength)});
        break;
    }
    case 11:
    {
        const int range = regionShapeRange(level);
        switch (region)
        {
        case 1:
        {
            const QPoint target = advanceTile(caster, 6, 3);
            QPoint row = advanceTile(target, 0, range / 2);
            for (int rowIndex = 0; rowIndex < range; ++rowIndex)
            {
                QPoint tile = row;
                for (int column = 0; column < range; ++column)
                {
                    geometry.primaryTiles.insert(tile);
                    tile = stepTile(tile, 5);
                }
                row = stepTile(row, 3);
            }
            break;
        }
        case 2:
            for (int direction : {1, 3, 5, 7})
            {
                QPoint tile = caster;
                for (int step = 0; step < range; ++step)
                {
                    tile = stepTile(tile, direction);
                    geometry.primaryTiles.insert(tile);
                }
            }
            break;
        case 3:
        {
            QPoint row = advanceTile(stepTile(caster, 6), 4, 2);
            for (int rowIndex = 0; rowIndex < range; ++rowIndex)
            {
                QPoint tile = row;
                for (int column = 0; column < 5; ++column)
                {
                    geometry.primaryTiles.insert(tile);
                    tile = stepTile(tile, 0);
                }
                row = stepTile(row, 6);
            }
            break;
        }
        case 4:
        {
            QPoint rowCenter = caster;
            for (int rowIndex = 0; rowIndex < range; ++rowIndex)
            {
                rowCenter = stepTile(rowCenter, 6);
                QPoint tile = advanceTile(rowCenter, 0, rowIndex);
                for (int column = 0; column < rowIndex * 2 + 1; ++column)
                {
                    geometry.primaryTiles.insert(tile);
                    tile = stepTile(tile, 4);
                }
            }
            break;
        }
        case 5:
        {
            const QPoint tip = stepTile(caster, 6);
            geometry.primaryTiles.insert(tip);
            QPoint upper = tip;
            QPoint lower = tip;
            for (int step = 1; step < range; ++step)
            {
                upper = stepTile(upper, 5);
                lower = stepTile(lower, 7);
                geometry.primaryTiles.insert(upper);
                geometry.primaryTiles.insert(lower);
            }
            break;
        }
        default:
            break;
        }
        break;
    }
    case 13:
    case 23:
        geometry.primaryTiles.insert(caster);
        break;
    case 15:
        for (int y = -6; y <= 6; ++y)
        {
            for (int x = -3; x <= 3; ++x)
                geometry.primaryTiles.insert(QPoint(x, y));
        }
        break;
    case 19:
        for (int step = 0; step < MovementPathLength; ++step)
        {
            geometry.primaryTiles.insert(
                advanceTile(caster, 6, step));
        }
        geometry.casterTile = advanceTile(
            caster, 6, MovementPathLength);
        geometry.arrows.push_back({caster, geometry.casterTile});
        break;
    case 20:
    {
        const QPoint destination = advanceTile(
            caster, 6, MovementPathLength);
        geometry.actorDestinationTiles.insert(destination);
        geometry.arrows.push_back({caster, destination});
        break;
    }
    case 21:
    {
        const QPoint target = advanceTile(
            caster, 6, MovementPathLength);
        geometry.primaryTiles.insert(target);
        geometry.connections.push_back({caster, target});
        break;
    }
    default:
        break;
    }
    return geometry;
}

int tileScreenX(const QPoint& tile)
{
    return tile.x() * 2 + std::abs(tile.y() % 2);
}

int tileScreenY(const QPoint& tile)
{
    return tile.y();
}
}

MagicRangePreview::MagicRangePreview(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(260, 250);
    setObjectName(QStringLiteral("magicRangePreview"));
}

void MagicRangePreview::setRange(
    int moveKind, int region, int attackRadius, int level)
{
    currentMoveKind = moveKind;
    currentRegion = region;
    currentAttackRadius = attackRadius;
    currentLevel = std::clamp(level, 1, 10);
    update();
}

QSize MagicRangePreview::sizeHint() const
{
    return QSize(340, 300);
}

void MagicRangePreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());

    const QRect content = rect().adjusted(12, 10, -12, -10);
    painter.setPen(palette().text().color());
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(content, Qt::AlignTop | Qt::AlignHCenter,
                     tr("第 %1 级 · %2").arg(currentLevel).arg(movementText()));

    QFont normalFont = painter.font();
    normalFont.setBold(false);
    painter.setFont(normalFont);
    painter.drawText(QRect(content.left(), content.top() + 23,
                           content.width(), 34),
                     Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                     rangeText());

    const PreviewGeometry geometry = previewGeometry(
        currentMoveKind, currentRegion, currentLevel);
    PreviewTiles visibleTiles = geometry.primaryTiles;
    visibleTiles.unite(geometry.secondaryTiles);
    visibleTiles.unite(geometry.actorDestinationTiles);
    visibleTiles.insert(geometry.casterTile);
    int minimumX = 0;
    int maximumX = 0;
    int minimumY = 0;
    int maximumY = 0;
    for (const QPoint& tile : visibleTiles)
    {
        minimumX = std::min(minimumX, tile.x());
        maximumX = std::max(maximumX, tile.x());
        minimumY = std::min(minimumY, tile.y());
        maximumY = std::max(maximumY, tile.y());
    }
    --minimumX;
    ++maximumX;
    minimumY -= 2;
    maximumY += 2;

    int minimumScreenX = (std::numeric_limits<int>::max)();
    int maximumScreenX = (std::numeric_limits<int>::min)();
    for (int y = minimumY; y <= maximumY; ++y)
    {
        minimumScreenX = std::min(
            minimumScreenX, tileScreenX(QPoint(minimumX, y)));
        maximumScreenX = std::max(
            maximumScreenX, tileScreenX(QPoint(maximumX, y)));
    }
    const int normalizedWidth = maximumScreenX - minimumScreenX + 2;
    const int normalizedHeight = maximumY - minimumY + 2;
    const QRect gridArea = content.adjusted(0, 60, 0, -28);
    const qreal halfWidth = std::max<qreal>(
        2.5, std::min(
            static_cast<qreal>(gridArea.width()) / normalizedWidth,
            static_cast<qreal>(gridArea.height()) * 2.0 /
                normalizedHeight));
    const qreal halfHeight = halfWidth / 2.0;
    const qreal centerScreenX =
        (minimumScreenX + maximumScreenX) / 2.0;
    const qreal centerScreenY = (minimumY + maximumY) / 2.0;
    const QColor affected = palette().highlight().color();
    QColor ending = affected.lighter(145);
    ending.setAlpha(165);
    const QColor grid = palette().mid().color();
    const QColor center = QColor(230, 150, 55);
    QColor actorDestination = center.lighter(130);
    actorDestination.setAlpha(190);

    auto tileCenterPosition = [&](const QPoint& tile)
    {
        return QPointF(
            gridArea.center().x() +
                (tileScreenX(tile) - centerScreenX) * halfWidth,
            gridArea.center().y() +
                (tileScreenY(tile) - centerScreenY) * halfHeight);
    };

    for (int y = minimumY; y <= maximumY; ++y)
    {
        for (int x = minimumX; x <= maximumX; ++x)
        {
            const QPoint tile(x, y);
            const QPointF tileCenter = tileCenterPosition(tile);
            const QPolygonF diamond({
                tileCenter + QPointF(0.0, -halfHeight),
                tileCenter + QPointF(halfWidth, 0.0),
                tileCenter + QPointF(0.0, halfHeight),
                tileCenter + QPointF(-halfWidth, 0.0)});
            QColor fill = palette().alternateBase().color();
            if (geometry.primaryTiles.contains(tile))
            {
                fill = affected;
                fill.setAlpha(135);
            }
            if (geometry.secondaryTiles.contains(tile))
                fill = ending;
            if (geometry.actorDestinationTiles.contains(tile))
                fill = actorDestination;
            if (tile == geometry.casterTile)
                fill = center;
            painter.setPen(grid);
            painter.setBrush(fill);
            painter.drawPolygon(diamond);
        }
    }

    auto clippedLine = [&](const PreviewArrow& arrow)
    {
        QPointF start = tileCenterPosition(arrow.from);
        QPointF end = tileCenterPosition(arrow.to);
        const QPointF delta = end - start;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length <= 0.1)
            return QPair<QPointF, QPointF>(start, end);
        const QPointF unit = delta / length;
        const qreal inset = std::min<qreal>(
            halfWidth * 0.8, length * 0.25);
        return QPair<QPointF, QPointF>(
            start + unit * inset,
            end - unit * inset);
    };

    if (!geometry.connections.isEmpty())
    {
        QPen connectionPen(palette().text().color());
        connectionPen.setStyle(Qt::DashLine);
        connectionPen.setWidthF(std::max<qreal>(1.3, halfHeight * 0.18));
        painter.setPen(connectionPen);
        for (const PreviewArrow& connection : geometry.connections)
        {
            const auto line = clippedLine(connection);
            painter.drawLine(line.first, line.second);
        }
    }

    if (!geometry.arrows.isEmpty())
    {
        QPen arrowPen(palette().text().color());
        const bool multipleArrows = geometry.arrows.size() > 3;
        arrowPen.setWidthF(std::max<qreal>(
            multipleArrows ? 1.0 : 1.5,
            halfHeight * (multipleArrows ? 0.12 : 0.22)));
        painter.setPen(arrowPen);
        painter.setBrush(arrowPen.color());
        for (const PreviewArrow& arrow : geometry.arrows)
        {
            const auto line = clippedLine(arrow);
            const QPointF delta = line.second - line.first;
            const qreal length = std::hypot(delta.x(), delta.y());
            if (length <= 0.1)
                continue;
            const QPointF unit = delta / length;
            const QPointF perpendicular(-unit.y(), unit.x());
            painter.drawLine(line.first, line.second);
            const qreal arrowSize = std::max<qreal>(
                multipleArrows ? 3.5 : 5.0,
                halfWidth * (multipleArrows ? 0.35 : 0.55));
            painter.drawPolygon(QPolygonF({
                line.second,
                line.second - unit * arrowSize +
                    perpendicular * arrowSize * 0.55,
                line.second - unit * arrowSize -
                    perpendicular * arrowSize * 0.55}));
        }
    }

    painter.setPen(palette().text().color());
    painter.drawText(QRect(content.left(), content.bottom() - 22,
                           content.width(), 22),
                     Qt::AlignCenter,
                     legendText());
}

bool MagicRangePreview::affectsPreviewCell(
    int moveKind, int region, int level, int x, int y)
{
    const PreviewGeometry geometry = previewGeometry(
        moveKind, region, level);
    const QPoint tile(x, y);
    return geometry.primaryTiles.contains(tile) ||
        geometry.secondaryTiles.contains(tile) ||
        geometry.actorDestinationTiles.contains(tile);
}

QString MagicRangePreview::movementText() const
{
    switch (currentMoveKind)
    {
    case 1: return tr("目标点");
    case 2: return tr("飞行");
    case 3: return tr("连续飞行");
    case 4: return tr("环形");
    case 5: return tr("心形环绕");
    case 6: return tr("螺旋环绕");
    case 7: return tr("扇形");
    case 8: return tr("随机扇形");
    case 9: return tr("直线");
    case 10: return tr("直线移动");
    case 11: return tr("区域");
    case 13: return tr("对自身");
    case 15: return tr("全屏");
    case 16: return tr("追踪目标");
    case 17: return tr("投掷");
    case 19: return tr("轨迹");
    case 20: return tr("位移");
    case 21: return tr("控制");
    case 22: return tr("召唤");
    case 23: return tr("时间停止");
    case 24: return tr("V 形移动");
    default: return tr("其它作用方式 (%1)").arg(currentMoveKind);
    }
}

QString MagicRangePreview::rangeText() const
{
    QString description;
    switch (currentMoveKind)
    {
    case 0:
        description = tr("未定义作用方式，不显示推测范围");
        break;
    case 1:
        description = tr("只在目标落点生成效果");
        break;
    case 2:
        description = tr("从施放者飞向目标");
        break;
    case 3:
        description = tr("连续发射 %1 道，均飞向目标").arg(currentLevel);
        break;
    case 4:
        description = tr("从施放者向四周同时发散 32 道");
        break;
    case 5:
        description = tr("向四周发散，飞行时序形成心形");
        break;
    case 6:
        description = tr("向四周依次发射，形成螺旋效果");
        break;
    case 7:
        description = tr("向前方扇形发射 %1 道")
            .arg(sectorProjectileCount(currentLevel));
        break;
    case 8:
        description = tr("向前方随机错开发射 %1 道")
            .arg(sectorProjectileCount(currentLevel));
        break;
    case 9:
        description = tr("目标处横排 %1 格")
            .arg(currentLevel * 2 + 1);
        break;
    case 10:
        description = tr("起始宽度 %1 格，整排向施放方向移动")
            .arg(currentLevel * 2 + 1);
        break;
    case 11:
    {
        const int range = regionShapeRange(currentLevel);
        switch (currentRegion)
        {
        case 1: description = tr("目标处 %1 × %1 格方形区域").arg(range); break;
        case 2: description = tr("十字四向各延伸 %1 格").arg(range); break;
        case 3: description = tr("向前 %1 排，每排 5 格").arg(range); break;
        case 4: description = tr("向前 %1 排，逐排展开").arg(range); break;
        case 5: description = tr("向前 %1 排，V 形展开").arg(range); break;
        case 6: description = tr("自定义范围需通过试玩查看"); break;
        default: description = tr("未指定区域形状"); break;
        }
        break;
    }
    case 13:
        description = tr("只对施放者生效");
        break;
    case 15:
        description = tr("作用于整个画面");
        break;
    case 16:
        description = tr("飞向并持续追踪移动目标");
        break;
    case 17:
        description = tr("从施放者投掷到目标位置");
        break;
    case 19:
        description = tr("施放者移动时，在经过位置留下效果");
        break;
    case 20:
        description = tr("施放者从起点移动到目标位置");
        break;
    case 21:
        description = tr("效果附着施放者，并控制选定目标");
        break;
    case 22:
        description = tr("在目标位置产生召唤");
        break;
    case 23:
        description = tr("由施放者触发时间停止，不表示周围范围");
        break;
    case 24:
        description = tr("V 形排列 %1 道并向施放方向移动")
            .arg(currentLevel * 2 + 1);
        break;
    default:
        description = tr("当前类型没有可用的静态范围示意");
        break;
    }

    const bool hasTargetDistance = currentAttackRadius > 0 &&
        currentMoveKind != 13 && currentMoveKind != 15 &&
        currentMoveKind != 19 && currentMoveKind != 23;
    if (hasTargetDistance)
    {
        return tr("%1；攻击距离：%2 格")
            .arg(description)
            .arg(currentAttackRadius);
    }
    return description;
}

QString MagicRangePreview::legendText() const
{
    switch (currentMoveKind)
    {
    case 0:
        return tr("橙色为施放者；当前类型不绘制猜测范围");
    case 1:
    case 22:
        return tr("橙色为施放者；蓝色为目标落点；施放方向向右");
    case 2:
    case 3:
    case 16:
    case 17:
        return tr("橙色为施放者；深蓝为起点，浅蓝为目标；箭头为移动方向");
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        return tr("橙色为施放者；浅蓝为发散方向；箭头为弹道方向");
    case 10:
    case 24:
        return tr("橙色为施放者；深蓝为起始，浅蓝为移动后；箭头为移动方向");
    case 13:
    case 23:
        return tr("橙色格为施放者，也是效果起点");
    case 19:
        return tr("蓝色为留下的效果；橙色为移动后位置；箭头为角色移动方向");
    case 20:
        return tr("橙色为起点，浅橙为目标位置；箭头为角色移动方向");
    case 21:
        return tr("橙色为施放者，蓝色为受控目标；虚线表示控制关系");
    default:
        return tr("橙色为施放者；蓝色按当前等级显示；施放方向向右");
    }
}
