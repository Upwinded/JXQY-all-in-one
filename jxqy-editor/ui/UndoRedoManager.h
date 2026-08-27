#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QString>
#include <QStack>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>
#include <utility>
#include "../core/MapFileEditor.h"
#include "MapRenderCanvas.h"

enum class UndoDomain : uint8_t
{
    None = 0,
    Map = 1,
    NpcList = 2,
    ObjectList = 4,
    All = Map | NpcList | ObjectList
};

inline UndoDomain operator|(UndoDomain left, UndoDomain right)
{
    return static_cast<UndoDomain>(
        static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
}

inline bool hasUndoDomain(UndoDomain value, UndoDomain domain)
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(domain)) != 0;
}

struct UndoRevisionState
{
    uint64_t map = 0;
    uint64_t npcList = 0;
    uint64_t objectList = 0;
};

class UndoCommand
{
public:
    virtual ~UndoCommand() = default;

    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual QString description() const = 0;
    virtual UndoDomain affectedDomains() const { return UndoDomain::Map; }

    void setRevisionStates(const UndoRevisionState& before, const UndoRevisionState& after)
    {
        beforeRevision = before;
        afterRevision = after;
    }
    const UndoRevisionState& getBeforeRevision() const { return beforeRevision; }
    const UndoRevisionState& getAfterRevision() const { return afterRevision; }

private:
    UndoRevisionState beforeRevision;
    UndoRevisionState afterRevision;
};

class TileEditCommand : public UndoCommand
{
public:
    TileEditCommand(int tileX, int tileY, const MapTileData& oldTileData,
                    const MapTileData& newTileData, MapFileEditor* editor)
        : positionX(tileX)
        , positionY(tileY)
        , oldData(oldTileData)
        , newData(newTileData)
        , mapEditor(editor)
    {
    }

    void undo() override
    {
        if (mapEditor)
        {
            mapEditor->setTile(positionX, positionY, oldData);
        }
    }

    void redo() override
    {
        if (mapEditor)
        {
            mapEditor->setTile(positionX, positionY, newData);
        }
    }

    QString description() const override
    {
        return QCoreApplication::translate("UndoCommand", "编辑图块 (%1, %2)")
            .arg(positionX).arg(positionY);
    }

private:
    int positionX = 0;
    int positionY = 0;
    MapTileData oldData;
    MapTileData newData;
    MapFileEditor* mapEditor = nullptr;
};

class EntityAddCommand : public UndoCommand
{
public:
    EntityAddCommand(const MapEntityData& entityData, int index, bool isNpc,
                     std::vector<MapEntityData>* npcList, std::vector<MapEntityData>* objectList)
        : entityData(entityData)
        , insertIndex(index)
        , isNpc(isNpc)
        , npcListPointer(npcList)
        , objectListPointer(objectList)
    {
    }

    void undo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && insertIndex >= 0 && insertIndex < (int)list->size())
        {
            list->erase(list->begin() + insertIndex);
        }
    }

    void redo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && insertIndex >= 0 && insertIndex <= (int)list->size())
        {
            list->insert(list->begin() + insertIndex, entityData);
        }
    }

    QString description() const override
    {
        QString entityType = isNpc ? QStringLiteral("NPC")
            : QCoreApplication::translate("UndoCommand", "物体");
        return QCoreApplication::translate("UndoCommand", "添加%1: %2")
            .arg(entityType).arg(QString::fromUtf8(entityData.name));
    }

    UndoDomain affectedDomains() const override
    {
        return isNpc ? UndoDomain::NpcList : UndoDomain::ObjectList;
    }

private:
    MapEntityData entityData;
    int insertIndex = 0;
    bool isNpc = true;
    std::vector<MapEntityData>* npcListPointer = nullptr;
    std::vector<MapEntityData>* objectListPointer = nullptr;
};

class EntityDeleteCommand : public UndoCommand
{
public:
    EntityDeleteCommand(const MapEntityData& entityData, int index, bool isNpc,
                        std::vector<MapEntityData>* npcList, std::vector<MapEntityData>* objectList)
        : entityData(entityData)
        , deleteIndex(index)
        , isNpc(isNpc)
        , npcListPointer(npcList)
        , objectListPointer(objectList)
    {
    }

    void undo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && deleteIndex >= 0 && deleteIndex <= (int)list->size())
        {
            list->insert(list->begin() + deleteIndex, entityData);
        }
    }

    void redo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && deleteIndex >= 0 && deleteIndex < (int)list->size())
        {
            list->erase(list->begin() + deleteIndex);
        }
    }

    QString description() const override
    {
        QString entityType = isNpc ? QStringLiteral("NPC")
            : QCoreApplication::translate("UndoCommand", "物体");
        return QCoreApplication::translate("UndoCommand", "删除%1: %2")
            .arg(entityType).arg(QString::fromUtf8(entityData.name));
    }

    UndoDomain affectedDomains() const override
    {
        return isNpc ? UndoDomain::NpcList : UndoDomain::ObjectList;
    }

private:
    MapEntityData entityData;
    int deleteIndex = 0;
    bool isNpc = true;
    std::vector<MapEntityData>* npcListPointer = nullptr;
    std::vector<MapEntityData>* objectListPointer = nullptr;
};

class EntityMoveCommand : public UndoCommand
{
public:
    EntityMoveCommand(int entityIndex, bool isNpc, int oldPositionX, int oldPositionY,
                      int newPositionX, int newPositionY, std::vector<MapEntityData>* npcList,
                      std::vector<MapEntityData>* objectList)
        : entityIndex(entityIndex)
        , isNpc(isNpc)
        , oldPositionX(oldPositionX)
        , oldPositionY(oldPositionY)
        , newPositionX(newPositionX)
        , newPositionY(newPositionY)
        , npcListPointer(npcList)
        , objectListPointer(objectList)
    {
    }

    void undo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && entityIndex >= 0 && entityIndex < (int)list->size())
        {
            (*list)[entityIndex].mapX = oldPositionX;
            (*list)[entityIndex].mapY = oldPositionY;
        }
    }

    void redo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && entityIndex >= 0 && entityIndex < (int)list->size())
        {
            (*list)[entityIndex].mapX = newPositionX;
            (*list)[entityIndex].mapY = newPositionY;
        }
    }

    QString description() const override
    {
        QString entityType = isNpc ? QStringLiteral("NPC")
            : QCoreApplication::translate("UndoCommand", "物体");
        return QCoreApplication::translate("UndoCommand", "移动%1到 (%2, %3)")
            .arg(entityType).arg(newPositionX).arg(newPositionY);
    }

    UndoDomain affectedDomains() const override
    {
        return isNpc ? UndoDomain::NpcList : UndoDomain::ObjectList;
    }

private:
    int entityIndex = 0;
    bool isNpc = true;
    int oldPositionX = 0;
    int oldPositionY = 0;
    int newPositionX = 0;
    int newPositionY = 0;
    std::vector<MapEntityData>* npcListPointer = nullptr;
    std::vector<MapEntityData>* objectListPointer = nullptr;
};

class TileFillCommand : public UndoCommand
{
public:
    TileFillCommand(const std::map<std::pair<int,int>, MapTileData>& oldTilesData,
                    const std::map<std::pair<int,int>, MapTileData>& newTilesData,
                    MapFileEditor* editor)
        : oldTilesData(oldTilesData)
        , newTilesData(newTilesData)
        , mapEditor(editor)
    {
    }

    void undo() override
    {
        if (!mapEditor) return;
        for (const auto& pair : oldTilesData)
        {
            mapEditor->setTile(pair.first.first, pair.first.second, pair.second);
        }
    }

    void redo() override
    {
        if (!mapEditor) return;
        for (const auto& pair : newTilesData)
        {
            mapEditor->setTile(pair.first.first, pair.first.second, pair.second);
        }
    }

    QString description() const override
    {
        return QCoreApplication::translate("UndoCommand", "填充图块（%1 个）")
            .arg((int)newTilesData.size());
    }

private:
    std::map<std::pair<int,int>, MapTileData> oldTilesData;
    std::map<std::pair<int,int>, MapTileData> newTilesData;
    MapFileEditor* mapEditor = nullptr;
};

class EntityPropertyEditCommand : public UndoCommand
{
public:
    EntityPropertyEditCommand(int entityIndex, bool isNpc, const MapEntityData& oldEntityData,
                              const MapEntityData& newEntityData, std::vector<MapEntityData>* npcList,
                              std::vector<MapEntityData>* objectList)
        : entityIndex(entityIndex)
        , isNpc(isNpc)
        , oldEntityData(oldEntityData)
        , newEntityData(newEntityData)
        , npcListPointer(npcList)
        , objectListPointer(objectList)
    {
    }

    void undo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && entityIndex >= 0 && entityIndex < (int)list->size())
        {
            (*list)[entityIndex] = oldEntityData;
        }
    }

    void redo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list && entityIndex >= 0 && entityIndex < (int)list->size())
        {
            (*list)[entityIndex] = newEntityData;
        }
    }

    QString description() const override
    {
        QString entityType = isNpc ? QStringLiteral("NPC")
            : QCoreApplication::translate("UndoCommand", "物体");
        return QCoreApplication::translate("UndoCommand", "编辑%1属性: %2")
            .arg(entityType)
            .arg(QString::fromUtf8(oldEntityData.name));
    }

    UndoDomain affectedDomains() const override
    {
        return isNpc ? UndoDomain::NpcList : UndoDomain::ObjectList;
    }

private:
    int entityIndex = 0;
    bool isNpc = true;
    MapEntityData oldEntityData;
    MapEntityData newEntityData;
    std::vector<MapEntityData>* npcListPointer = nullptr;
    std::vector<MapEntityData>* objectListPointer = nullptr;
};

class EntityListReplaceCommand : public UndoCommand
{
public:
    EntityListReplaceCommand(bool isNpc, const std::vector<MapEntityData>& oldList,
                             const std::vector<MapEntityData>& newList,
                             std::vector<MapEntityData>* npcList,
                             std::vector<MapEntityData>* objectList)
        : isNpc(isNpc)
        , oldEntityList(oldList)
        , newEntityList(newList)
        , npcListPointer(npcList)
        , objectListPointer(objectList)
    {
    }

    void undo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list)
        {
            *list = oldEntityList;
        }
    }

    void redo() override
    {
        std::vector<MapEntityData>* list = isNpc ? npcListPointer : objectListPointer;
        if (list)
        {
            *list = newEntityList;
        }
    }

    QString description() const override
    {
        QString entityType = isNpc ? QStringLiteral("NPC")
            : QCoreApplication::translate("UndoCommand", "物体");
        return QCoreApplication::translate("UndoCommand", "替换%1列表（%2 -> %3 个实体）")
            .arg(entityType)
            .arg((int)oldEntityList.size())
            .arg((int)newEntityList.size());
    }

    UndoDomain affectedDomains() const override
    {
        return isNpc ? UndoDomain::NpcList : UndoDomain::ObjectList;
    }

private:
    bool isNpc = true;
    std::vector<MapEntityData> oldEntityList;
    std::vector<MapEntityData> newEntityList;
    std::vector<MapEntityData>* npcListPointer = nullptr;
    std::vector<MapEntityData>* objectListPointer = nullptr;
};

class MapResizeCommand : public UndoCommand
{
public:
    MapResizeCommand(int32_t oldWidth, int32_t oldHeight,
                     int32_t newWidth, int32_t newHeight,
                     const std::vector<std::vector<MapTileData>>& oldTileData,
                     const std::vector<std::vector<MapTileData>>& newTileData,
                     const std::vector<MapEntityData>& oldNpcList,
                     const std::vector<MapEntityData>& oldObjectList,
                     const std::vector<MapEntityData>& newNpcList,
                     const std::vector<MapEntityData>& newObjectList,
                     MapFileEditor* editor,
                     std::vector<MapEntityData>* npcList,
                     std::vector<MapEntityData>* objectList,
                     std::function<void()> updateCallback)
        : oldWidth(oldWidth)
        , oldHeight(oldHeight)
        , newWidth(newWidth)
        , newHeight(newHeight)
        , oldTileData(oldTileData)
        , newTileData(newTileData)
        , oldNpcList(oldNpcList)
        , oldObjectList(oldObjectList)
        , newNpcList(newNpcList)
        , newObjectList(newObjectList)
        , mapEditor(editor)
        , npcListPointer(npcList)
        , objectListPointer(objectList)
        , updateCallback(updateCallback)
    {
    }

    void undo() override
    {
        if (mapEditor)
        {
            mapEditor->setTileDataAndSize(oldTileData, oldWidth, oldHeight);
        }
        if (npcListPointer)
        {
            *npcListPointer = oldNpcList;
        }
        if (objectListPointer)
        {
            *objectListPointer = oldObjectList;
        }
        if (updateCallback)
        {
            updateCallback();
        }
    }

    void redo() override
    {
        if (mapEditor)
        {
            mapEditor->setTileDataAndSize(newTileData, newWidth, newHeight);
        }
        if (npcListPointer)
        {
            *npcListPointer = newNpcList;
        }
        if (objectListPointer)
        {
            *objectListPointer = newObjectList;
        }
        if (updateCallback)
        {
            updateCallback();
        }
    }

    QString description() const override
    {
        return QCoreApplication::translate("UndoCommand", "调整地图大小（%1x%2 -> %3x%4）")
            .arg(oldWidth).arg(oldHeight).arg(newWidth).arg(newHeight);
    }

    UndoDomain affectedDomains() const override
    {
        UndoDomain domains = UndoDomain::Map;
        if (oldNpcList.size() != newNpcList.size())
            domains = domains | UndoDomain::NpcList;
        if (oldObjectList.size() != newObjectList.size())
            domains = domains | UndoDomain::ObjectList;
        return domains;
    }

private:
    int32_t oldWidth = 0;
    int32_t oldHeight = 0;
    int32_t newWidth = 0;
    int32_t newHeight = 0;
    std::vector<std::vector<MapTileData>> oldTileData;
    std::vector<std::vector<MapTileData>> newTileData;
    std::vector<MapEntityData> oldNpcList;
    std::vector<MapEntityData> oldObjectList;
    std::vector<MapEntityData> newNpcList;
    std::vector<MapEntityData> newObjectList;
    MapFileEditor* mapEditor = nullptr;
    std::vector<MapEntityData>* npcListPointer = nullptr;
    std::vector<MapEntityData>* objectListPointer = nullptr;
    std::function<void()> updateCallback;
};

class MpcInfoEditCommand : public UndoCommand
{
public:
    /// MPC 信息表 + 图层引用清理的复合撤销命令。
    /// oldMpc/newMpc 为完整 255 槽位的旧/新 MPC 信息表。
    /// tileClearKeys 为删除 MPC 时需要清空引用的瓦片 (x,y) 集合（仅记录被改动层）。
    /// 该命令不压缩/移动槽位，删除槽位后的后续索引保持不变。
    MpcInfoEditCommand(const MpcInfoData (&oldMpc)[MAP_EDITOR_MPC_COUNT],
                       const MpcInfoData (&newMpc)[MAP_EDITOR_MPC_COUNT],
                       const std::map<std::pair<int,int>, MapTileData>& oldTilesData,
                       const std::map<std::pair<int,int>, MapTileData>& newTilesData,
                       MapFileEditor* editor,
                       std::function<void()> refreshCallback,
                       const QString& descriptionText)
        : mapEditor(editor)
        , refreshCallback(refreshCallback)
        , descriptionText(descriptionText)
    {
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
        {
            oldMpcInfo[i] = oldMpc[i];
            newMpcInfo[i] = newMpc[i];
        }
        oldTiles = oldTilesData;
        newTiles = newTilesData;
    }

    void undo() override
    {
        if (!mapEditor) return;
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
            mapEditor->setMpcInfo(i, oldMpcInfo[i]);
        for (const auto& pair : oldTiles)
            mapEditor->setTile(pair.first.first, pair.first.second, pair.second);
        if (refreshCallback) refreshCallback();
    }

    void redo() override
    {
        if (!mapEditor) return;
        for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
            mapEditor->setMpcInfo(i, newMpcInfo[i]);
        for (const auto& pair : newTiles)
            mapEditor->setTile(pair.first.first, pair.first.second, pair.second);
        if (refreshCallback) refreshCallback();
    }

    QString description() const override { return descriptionText; }

private:
    MpcInfoData oldMpcInfo[MAP_EDITOR_MPC_COUNT];
    MpcInfoData newMpcInfo[MAP_EDITOR_MPC_COUNT];
    std::map<std::pair<int,int>, MapTileData> oldTiles;
    std::map<std::pair<int,int>, MapTileData> newTiles;
    MapFileEditor* mapEditor = nullptr;
    std::function<void()> refreshCallback;
    QString descriptionText;
};

class UndoRedoManager : public QObject
{
    Q_OBJECT

public:
    explicit UndoRedoManager(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~UndoRedoManager()
    {
        clear();
    }

    void pushCommand(UndoCommand* command)
    {
        if (!command)
        {
            return;
        }

        UndoRevisionState before = currentRevision;
        advanceRevision(command->affectedDomains());
        command->setRevisionStates(before, currentRevision);
        undoStack.push(command);

        while (!redoStack.isEmpty())
        {
            delete redoStack.pop();
        }

        trimUndoStack();

        emit undoStackChanged();
    }

    void undo()
    {
        if (undoStack.isEmpty())
        {
            return;
        }

        UndoCommand* command = undoStack.pop();
        command->undo();
        applyRevisionForDomains(
            command->getBeforeRevision(), command->affectedDomains());
        redoStack.push(command);

        emit undoStackChanged();
        emit undoDescription(command->description());
    }

    void redo()
    {
        if (redoStack.isEmpty())
        {
            return;
        }

        UndoCommand* command = redoStack.pop();
        command->redo();
        applyRevisionForDomains(
            command->getAfterRevision(), command->affectedDomains());
        undoStack.push(command);

        trimUndoStack();

        emit undoStackChanged();
        emit redoDescription(command->description());
    }

    bool canUndo() const
    {
        return !undoStack.isEmpty();
    }

    bool canRedo() const
    {
        return !redoStack.isEmpty();
    }

    void clear()
    {
        while (!undoStack.isEmpty())
        {
            delete undoStack.pop();
        }

        while (!redoStack.isEmpty())
        {
            delete redoStack.pop();
        }

        currentRevision = UndoRevisionState();
        savedRevision = UndoRevisionState();

        emit undoStackChanged();
    }

    void markSaved(UndoDomain domains)
    {
        if (hasUndoDomain(domains, UndoDomain::Map))
            savedRevision.map = currentRevision.map;
        if (hasUndoDomain(domains, UndoDomain::NpcList))
            savedRevision.npcList = currentRevision.npcList;
        if (hasUndoDomain(domains, UndoDomain::ObjectList))
            savedRevision.objectList = currentRevision.objectList;
    }

    void markDirty(UndoDomain domains)
    {
        advanceRevision(domains);
    }

    /// Replace one loaded document domain without destroying unrelated undo
    /// history. Commands spanning the replaced domain (for example a resize
    /// that also removed NPCs) form a history barrier because their stored
    /// snapshot no longer belongs to the new document. Only the contiguous
    /// commands newer than the newest such barrier remain safe to undo/redo;
    /// retaining older/dependent commands would let history cross a state that
    /// can no longer be reconstructed.
    void resetDomains(UndoDomain domains, bool modified = false)
    {
        auto discardThroughNewestAffected = [domains](QStack<UndoCommand*>& stack)
        {
            int newestAffectedIndex = -1;
            for (int index = stack.size() - 1; index >= 0; --index)
            {
                if (hasUndoDomain(stack[index]->affectedDomains(), domains))
                {
                    newestAffectedIndex = index;
                    break;
                }
            }

            if (newestAffectedIndex < 0)
                return;

            QStack<UndoCommand*> kept;
            for (int index = 0; index <= newestAffectedIndex; ++index)
                delete stack[index];
            for (int index = newestAffectedIndex + 1; index < stack.size(); ++index)
                kept.push(stack[index]);
            stack = std::move(kept);
        };
        discardThroughNewestAffected(undoStack);
        discardThroughNewestAffected(redoStack);

        if (hasUndoDomain(domains, UndoDomain::Map))
        {
            currentRevision.map = nextRevision++;
            savedRevision.map = currentRevision.map;
        }
        if (hasUndoDomain(domains, UndoDomain::NpcList))
        {
            currentRevision.npcList = nextRevision++;
            savedRevision.npcList = currentRevision.npcList;
        }
        if (hasUndoDomain(domains, UndoDomain::ObjectList))
        {
            currentRevision.objectList = nextRevision++;
            savedRevision.objectList = currentRevision.objectList;
        }
        if (modified)
            advanceRevision(domains);
        emit undoStackChanged();
    }

    bool isModified(UndoDomain domain) const
    {
        if (hasUndoDomain(domain, UndoDomain::Map) &&
            currentRevision.map != savedRevision.map)
        {
            return true;
        }
        if (hasUndoDomain(domain, UndoDomain::NpcList) &&
            currentRevision.npcList != savedRevision.npcList)
        {
            return true;
        }
        if (hasUndoDomain(domain, UndoDomain::ObjectList) &&
            currentRevision.objectList != savedRevision.objectList)
        {
            return true;
        }
        return false;
    }

    void setMaxStackSize(int maxSize)
    {
        maximumStackSize = maxSize > 0 ? maxSize : 1;
        trimUndoStack();
    }

    int getUndoCount() const
    {
        return undoStack.size();
    }

    int getRedoCount() const
    {
        return redoStack.size();
    }

    QString getUndoDescription() const
    {
        if (undoStack.isEmpty())
        {
            return QString();
        }
        return undoStack.top()->description();
    }

    QString getRedoDescription() const
    {
        if (redoStack.isEmpty())
        {
            return QString();
        }
        return redoStack.top()->description();
    }

signals:
    void undoStackChanged();
    void undoDescription(const QString& description);
    void redoDescription(const QString& description);

private:
    void advanceRevision(UndoDomain domains)
    {
        if (hasUndoDomain(domains, UndoDomain::Map))
            currentRevision.map = nextRevision++;
        if (hasUndoDomain(domains, UndoDomain::NpcList))
            currentRevision.npcList = nextRevision++;
        if (hasUndoDomain(domains, UndoDomain::ObjectList))
            currentRevision.objectList = nextRevision++;
    }

    void applyRevisionForDomains(const UndoRevisionState& source, UndoDomain domains)
    {
        if (hasUndoDomain(domains, UndoDomain::Map))
            currentRevision.map = source.map;
        if (hasUndoDomain(domains, UndoDomain::NpcList))
            currentRevision.npcList = source.npcList;
        if (hasUndoDomain(domains, UndoDomain::ObjectList))
            currentRevision.objectList = source.objectList;
    }

    void trimUndoStack()
    {
        while (undoStack.size() > maximumStackSize)
        {
            delete undoStack.first();
            undoStack.remove(0);
        }
    }

    QStack<UndoCommand*> undoStack;
    QStack<UndoCommand*> redoStack;
    int maximumStackSize = 50;
    UndoRevisionState currentRevision;
    UndoRevisionState savedRevision;
    uint64_t nextRevision = 1;
};
