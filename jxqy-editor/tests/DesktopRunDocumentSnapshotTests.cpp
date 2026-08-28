#include "../core/DesktopRunDocumentSnapshot.h"
#include "../core/MapFileEditor.h"
#include "../ui/MapEditorWindow.h"
#include "../ui/MapRenderCanvas.h"
#include "../ui/NpcDataEditorWindow.h"
#include "../ui/ScriptEditorWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTextCursor>

#include <iostream>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QDir directory = QFileInfo(path).dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
        file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

const DesktopRunDocumentSnapshot* findSnapshot(
    const QList<DesktopRunDocumentSnapshot>& snapshots,
    ProjectDocumentType type,
    bool overlayOnly = false)
{
    for (const DesktopRunDocumentSnapshot& snapshot : snapshots)
    {
        if (snapshot.type == type &&
            (!overlayOnly || snapshot.includeInOverlay))
        {
            return &snapshot;
        }
    }
    return nullptr;
}

bool sameDocumentStates(
    const QList<ProjectDocumentState>& first,
    const QList<ProjectDocumentState>& second)
{
    if (first.size() != second.size())
        return false;
    for (int index = 0; index < first.size(); ++index)
    {
        if (first[index].filePath != second[index].filePath ||
            first[index].type != second[index].type ||
            first[index].dirty != second[index].dirty)
        {
            return false;
        }
    }
    return true;
}

bool testMapCanvasAndBundleSnapshots()
{
    std::cerr << "RUN map snapshots\n";
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create map snapshot temporary directory"))
    {
        return false;
    }

    const QString assetsRoot = temporaryDirectory.path();
    const QString mapPath =
        QDir(assetsRoot).filePath(QStringLiteral("map/scene.map"));
    const QString npcPath =
        QDir(assetsRoot).filePath(QStringLiteral("ini/npc/scene.npc"));
    const QString objectPath =
        QDir(assetsRoot).filePath(QStringLiteral("ini/obj/scene.obj"));
    const QString sourceMpcPath =
        QDir(assetsRoot).filePath(QStringLiteral("source/未保存地砖.mpc"));
    const QByteArray sourceMpcBytes("MPC snapshot bytes\0\x7f", 20);
    if (!check(
            QDir(assetsRoot).mkpath(QStringLiteral("map")) &&
                writeBytes(
                npcPath,
                QByteArray(
                    "[Head]\nMap=scene.map\nCount=1\n\n"
                    "[NPC007]\nName=守卫\nMapX=1\nMapY=2\n"
                    "CustomNpc=keep\n")) &&
                writeBytes(
                    objectPath,
                    QByteArray(
                        "[Head]\nMap=scene.map\nCount=1\n\n"
                        "[OBJ009]\nObjName=木箱\nMapX=2\nMapY=3\n"
                        "ActionTime=7\nCustomObject=keep\n")) &&
                writeBytes(sourceMpcPath, sourceMpcBytes),
            "write map snapshot fixtures"))
    {
        return false;
    }

    MapEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    if (!check(
            window.setAssetsBasePath(assetsRoot) &&
                window.createNewMap(3, 4) &&
                window.saveMapFile(mapPath) &&
                window.loadNpcListFromFile(npcPath, false) &&
                window.loadObjectListFromFile(objectPath, false),
            "prepare map snapshot document bundle"))
    {
        return false;
    }

    MapRenderCanvas* canvas = window.findChild<MapRenderCanvas*>();
    if (!check(canvas != nullptr, "find map canvas for snapshot test"))
        return false;

    const std::string npcBytes = canvas->serializeNpcList();
    const std::string objectBytes = canvas->serializeObjectList();
    const QString npcSavePath =
        QDir(assetsRoot).filePath(QStringLiteral("snapshot-save.npc"));
    const QString objectSavePath =
        QDir(assetsRoot).filePath(QStringLiteral("snapshot-save.obj"));
    const QList<ProjectDocumentState> beforeCanvasSerialization =
        window.currentProjectDocuments();
    const QList<DesktopRunDocumentSnapshot>
        cleanGenericSnapshots =
            window.desktopRunGenericDocumentSnapshots();
    bool ok = check(
        canvas->saveNpcList(npcSavePath.toUtf8().toStdString()) &&
            canvas->saveObjectList(objectSavePath.toUtf8().toStdString()),
        "save map companion lists through production path");
    ok = check(
        cleanGenericSnapshots.isEmpty(),
        "lightweight generic collection omits every clean direct MAP, NPC, and OBJ snapshot") &&
        ok;
    ok = check(
        readBytes(npcSavePath) ==
                QByteArray(
                    npcBytes.data(),
                    static_cast<qsizetype>(npcBytes.size())) &&
            readBytes(objectSavePath) ==
                QByteArray(
                    objectBytes.data(),
                    static_cast<qsizetype>(objectBytes.size())),
        "pure map companion serializers match production save bytes") && ok;
    ok = check(
        sameDocumentStates(
            beforeCanvasSerialization,
            window.currentProjectDocuments()) &&
            canvas->serializeNpcList() == npcBytes &&
            canvas->serializeObjectList() == objectBytes,
        "map companion serialization leaves baselines and dirty state unchanged") &&
        ok;

    window.setMpcFileSelectionForTest(
        {[sourceMpcPath]() { return sourceMpcPath; }});
    ok = check(
        QMetaObject::invokeMethod(
            &window, "onAddMpcSlot", Qt::DirectConnection),
        "add pending MPC through production editor action") && ok;
    QApplication::processEvents();

    const QString managedMpcPath =
        QDir(assetsRoot).filePath(
            QStringLiteral("mpc/map/scene/未保存地砖.mpc"));
    const QByteArray savedMapBeforeSnapshot = readBytes(mapPath);
    const QByteArray managedMpcBeforeSnapshot = readBytes(managedMpcPath);
    const QList<ProjectDocumentState> beforeBundle =
        window.currentProjectDocuments();
    const MapEditorWindow::DesktopRunSnapshotBundle bundle =
        window.desktopRunSnapshotBundle();
    const QList<DesktopRunDocumentSnapshot>
        genericPendingSnapshots =
            window.desktopRunGenericDocumentSnapshots();
    const QList<ProjectDocumentState> afterBundle =
        window.currentProjectDocuments();

    const DesktopRunDocumentSnapshot* mapSnapshot =
        findSnapshot(bundle.documents, ProjectDocumentType::Map);
    const DesktopRunDocumentSnapshot* npcSnapshot =
        findSnapshot(bundle.documents, ProjectDocumentType::NpcList);
    const DesktopRunDocumentSnapshot* objectSnapshot =
        findSnapshot(bundle.documents, ProjectDocumentType::ObjectList);
    const DesktopRunDocumentSnapshot* pendingMpcSnapshot =
        findSnapshot(bundle.documents, ProjectDocumentType::Image, true);
    const DesktopRunDocumentSnapshot* genericMapSnapshot =
        findSnapshot(
            genericPendingSnapshots,
            ProjectDocumentType::Map);
    const DesktopRunDocumentSnapshot* genericPendingMpcSnapshot =
        findSnapshot(
            genericPendingSnapshots,
            ProjectDocumentType::Image,
            true);
    const std::vector<uint8_t> expectedMapBytes =
        window.getMapEditor().saveToBuffer();
    const QByteArray expectedMap(
        reinterpret_cast<const char*>(expectedMapBytes.data()),
        static_cast<qsizetype>(expectedMapBytes.size()));

    ok = check(
        bundle.mapLoaded &&
            bundle.mapWidth == 3 &&
            bundle.mapHeight == 4 &&
            bundle.currentMapFilePath ==
                QFileInfo(mapPath).absoluteFilePath(),
        "map bundle exposes dimensions and stable absolute path") && ok;
    ok = check(
        mapSnapshot &&
            mapSnapshot->dirty &&
            mapSnapshot->serializationSupported &&
            !mapSnapshot->includeInOverlay &&
            mapSnapshot->bytes == expectedMap,
        "map bundle contains exact dirty MAP bytes") && ok;
    ok = check(
        npcSnapshot &&
            npcSnapshot->filePath == QFileInfo(npcPath).absoluteFilePath() &&
            npcSnapshot->serializationSupported &&
            npcSnapshot->bytes ==
                QByteArray(
                    npcBytes.data(),
                    static_cast<qsizetype>(npcBytes.size())) &&
            objectSnapshot &&
            objectSnapshot->filePath ==
                QFileInfo(objectPath).absoluteFilePath() &&
            objectSnapshot->serializationSupported &&
            objectSnapshot->bytes ==
                QByteArray(
                    objectBytes.data(),
                    static_cast<qsizetype>(objectBytes.size())),
        "map bundle contains exact open companion document bytes") && ok;
    ok = check(
        pendingMpcSnapshot &&
            pendingMpcSnapshot->dirty &&
            pendingMpcSnapshot->includeInOverlay &&
            pendingMpcSnapshot->serializationSupported &&
            pendingMpcSnapshot->ownerMapFilePath ==
                QFileInfo(mapPath).absoluteFilePath() &&
            pendingMpcSnapshot->filePath ==
                QFileInfo(managedMpcPath).absoluteFilePath() &&
            pendingMpcSnapshot->bytes == sourceMpcBytes,
        "map bundle exports pending MPC with its exact owner MAP provenance") && ok;
    ok = check(
        genericPendingSnapshots.size() == 2 &&
            genericMapSnapshot &&
            genericMapSnapshot->dirty &&
            genericMapSnapshot->bytes == expectedMap &&
            genericPendingMpcSnapshot &&
            genericPendingMpcSnapshot->filePath ==
                QFileInfo(managedMpcPath).absoluteFilePath() &&
            genericPendingMpcSnapshot->ownerMapFilePath ==
                QFileInfo(mapPath).absoluteFilePath() &&
            findSnapshot(
                genericPendingSnapshots,
                ProjectDocumentType::NpcList) == nullptr &&
            findSnapshot(
                genericPendingSnapshots,
                ProjectDocumentType::ObjectList) == nullptr,
        "lightweight generic collection serializes the dirty MAP and referenced pending MPC but skips clean companions") &&
        ok;
    ok = check(
        savedMapBeforeSnapshot == readBytes(mapPath) &&
            managedMpcBeforeSnapshot == readBytes(managedMpcPath) &&
            sameDocumentStates(beforeBundle, afterBundle),
        "map bundle snapshot leaves disk and dirty state unchanged") && ok;

    QAction* undoAction = window.findChild<QAction*>(
        QStringLiteral("actionUndo"));
    QAction* redoAction = window.findChild<QAction*>(
        QStringLiteral("actionRedo"));
    if (undoAction)
        undoAction->trigger();
    QApplication::processEvents();
    const bool undoPreserved =
        window.getMapEditor().getMpcInfo(0).name.empty() &&
        !QFileInfo::exists(managedMpcPath);
    const MapEditorWindow::DesktopRunSnapshotBundle undoneBundle =
        window.desktopRunSnapshotBundle();
    const bool undoOnlyPayloadExcluded =
        findSnapshot(
            undoneBundle.documents,
            ProjectDocumentType::Image,
            true) == nullptr;

    MapEditorWindow sharedReferenceWindow;
    sharedReferenceWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
    sharedReferenceWindow.setAssetsBasePath(assetsRoot);
    const bool sharedReferencePrepared =
        sharedReferenceWindow.createNewMap(2, 2);
    sharedReferenceWindow.getMapEditorRef().setMpcPath(
        "mpc/map/scene");
    MpcInfoData sharedMpcInfo;
    sharedMpcInfo.name =
        QFileInfo(sourceMpcPath).fileName().toUtf8().toStdString();
    sharedReferenceWindow.getMapEditorRef().setMpcInfo(0, sharedMpcInfo);
    const MapEditorWindow::DesktopRunSnapshotBundle sharedBundle =
        sharedReferenceWindow.desktopRunSnapshotBundle();
    const DesktopRunDocumentSnapshot* sharedPendingSnapshot =
        findSnapshot(
            sharedBundle.documents,
            ProjectDocumentType::Image,
            true);
    const bool sharedReferencedPayloadIncluded =
        sharedReferencePrepared &&
        sharedPendingSnapshot &&
        sharedPendingSnapshot->filePath ==
            QFileInfo(managedMpcPath).absoluteFilePath() &&
        sharedPendingSnapshot->bytes == sourceMpcBytes;

    if (redoAction)
        redoAction->trigger();
    QApplication::processEvents();
    ok = check(
        undoAction &&
            redoAction &&
            undoPreserved &&
            undoOnlyPayloadExcluded &&
            sharedReferencedPayloadIncluded &&
            QString::fromUtf8(
                window.getMapEditor().getMpcInfo(0).name.c_str()) ==
                QFileInfo(sourceMpcPath).fileName() &&
            readBytes(managedMpcPath) == sourceMpcBytes,
        "map snapshot excludes undo-only local payloads, exports shared references and preserves undo/redo") &&
        ok;

    const QString runtimeSaveNpcPath =
        QDir(assetsRoot).filePath(
            QStringLiteral("save/game/runtime-scene.npc"));
    const QString runtimeSaveObjectPath =
        QDir(assetsRoot).filePath(
            QStringLiteral("save/game/runtime-scene.obj"));
    const QString runtimeSaveMapPath =
        QDir(assetsRoot).filePath(
            QStringLiteral("map/runtime-scene.map"));
    MapEditorWindow runtimeSaveWindow;
    runtimeSaveWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
    const bool runtimeSavePrepared =
        writeBytes(runtimeSaveNpcPath, readBytes(npcPath)) &&
        writeBytes(runtimeSaveObjectPath, readBytes(objectPath)) &&
        runtimeSaveWindow.setAssetsBasePath(assetsRoot) &&
        runtimeSaveWindow.createNewMap(2, 2) &&
        runtimeSaveWindow.saveMapFile(
            runtimeSaveMapPath) &&
        runtimeSaveWindow.loadNpcListFromFile(
            runtimeSaveNpcPath, false) &&
        runtimeSaveWindow.loadObjectListFromFile(
            runtimeSaveObjectPath, false);
    const MapEditorWindow::DesktopRunSnapshotBundle runtimeSaveBundle =
        runtimeSaveWindow.desktopRunSnapshotBundle();
    const QList<DesktopRunDocumentSnapshot>
        runtimeSaveGenericClean =
            runtimeSaveWindow.
                desktopRunGenericDocumentSnapshots();
    const DesktopRunDocumentSnapshot* runtimeSaveNpcSnapshot =
        findSnapshot(
            runtimeSaveBundle.documents,
            ProjectDocumentType::NpcList);
    const DesktopRunDocumentSnapshot* runtimeSaveObjectSnapshot =
        findSnapshot(
            runtimeSaveBundle.documents,
            ProjectDocumentType::ObjectList);
    ok = check(
        runtimeSavePrepared &&
            runtimeSaveBundle.assetsBasePath ==
                QFileInfo(assetsRoot).absoluteFilePath() &&
            runtimeSaveNpcSnapshot &&
            !runtimeSaveNpcSnapshot->dirty &&
            runtimeSaveNpcSnapshot->includeInOverlay &&
            QDir(runtimeSaveBundle.assetsBasePath).relativeFilePath(
                runtimeSaveNpcSnapshot->filePath) ==
                QStringLiteral("save/game/runtime-scene.npc") &&
            runtimeSaveObjectSnapshot &&
            !runtimeSaveObjectSnapshot->dirty &&
            runtimeSaveObjectSnapshot->includeInOverlay &&
            QDir(runtimeSaveBundle.assetsBasePath).relativeFilePath(
                runtimeSaveObjectSnapshot->filePath) ==
                QStringLiteral("save/game/runtime-scene.obj"),
        "runtime-save companions are pinned into overlay with safe active-root-relative paths") &&
        ok;
    ok = check(
        runtimeSaveGenericClean.isEmpty(),
        "lightweight generic collection does not pin clean runtime-save NPC or OBJ companions") &&
        ok;

    MapRenderCanvas* runtimeSaveCanvas =
        runtimeSaveWindow.findChild<MapRenderCanvas*>();
    if (!check(
            runtimeSaveCanvas != nullptr,
            "find runtime-save canvas for dirty companion snapshots"))
    {
        return false;
    }
    MapEntityData addedNpc;
    addedNpc.name = "dirty-npc";
    addedNpc.isNpc = true;
    const int addedNpcIndex =
        static_cast<int>(
            runtimeSaveCanvas->getNpcList().size());
    runtimeSaveCanvas->getNpcListRef().push_back(
        addedNpc);
    MapEntityData addedObject;
    addedObject.name = "dirty-object";
    addedObject.isNpc = false;
    const int addedObjectIndex =
        static_cast<int>(
            runtimeSaveCanvas->getObjectList().size());
    runtimeSaveCanvas->getObjectListRef().push_back(
        addedObject);
    const bool dirtySignalsInvoked =
        QMetaObject::invokeMethod(
            &runtimeSaveWindow,
            "onEntityPlaced",
            Qt::DirectConnection,
            Q_ARG(int, addedNpcIndex),
            Q_ARG(bool, true)) &&
        QMetaObject::invokeMethod(
            &runtimeSaveWindow,
            "onEntityPlaced",
            Qt::DirectConnection,
            Q_ARG(int, addedObjectIndex),
            Q_ARG(bool, false));
    const QList<DesktopRunDocumentSnapshot>
        dirtyCompanionSnapshots =
            runtimeSaveWindow.
                desktopRunGenericDocumentSnapshots();
    const DesktopRunDocumentSnapshot* dirtyNpcSnapshot =
        findSnapshot(
            dirtyCompanionSnapshots,
            ProjectDocumentType::NpcList);
    const DesktopRunDocumentSnapshot* dirtyObjectSnapshot =
        findSnapshot(
            dirtyCompanionSnapshots,
            ProjectDocumentType::ObjectList);
    const std::string expectedDirtyNpcBytes =
        runtimeSaveCanvas->serializeNpcList();
    const std::string expectedDirtyObjectBytes =
        runtimeSaveCanvas->serializeObjectList();
    ok = check(
        dirtySignalsInvoked &&
            dirtyCompanionSnapshots.size() == 2 &&
            dirtyNpcSnapshot &&
            dirtyNpcSnapshot->dirty &&
            !dirtyNpcSnapshot->includeInOverlay &&
            dirtyNpcSnapshot->bytes ==
                QByteArray(
                    expectedDirtyNpcBytes.data(),
                    static_cast<qsizetype>(
                        expectedDirtyNpcBytes.size())) &&
            dirtyObjectSnapshot &&
            dirtyObjectSnapshot->dirty &&
            !dirtyObjectSnapshot->includeInOverlay &&
            dirtyObjectSnapshot->bytes ==
                QByteArray(
                    expectedDirtyObjectBytes.data(),
                    static_cast<qsizetype>(
                        expectedDirtyObjectBytes.size())) &&
            findSnapshot(
                dirtyCompanionSnapshots,
                ProjectDocumentType::Map) == nullptr,
        "lightweight generic collection serializes exact dirty NPC and OBJ bytes without reserializing the clean MAP") &&
        ok;
    return ok;
}

bool testGenericMapSnapshotCollection()
{
    std::cerr << "RUN generic map snapshot collection\n";
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create generic map snapshot temporary directory"))
    {
        return false;
    }

    const QString ownerMapPath =
        temporaryDirectory.filePath(
            QStringLiteral("map/owner.map"));
    DesktopRunDocumentSnapshot mapSnapshot;
    mapSnapshot.filePath = ownerMapPath;
    mapSnapshot.type = ProjectDocumentType::Map;
    mapSnapshot.dirty = true;
    mapSnapshot.serializationSupported = true;
    mapSnapshot.bytes = QByteArray("map");

    DesktopRunDocumentSnapshot runtimeSaveNpc;
    runtimeSaveNpc.filePath =
        temporaryDirectory.filePath(
            QStringLiteral("save/game/owner.npc"));
    runtimeSaveNpc.type = ProjectDocumentType::NpcList;
    runtimeSaveNpc.includeInOverlay = true;
    runtimeSaveNpc.serializationSupported = true;
    runtimeSaveNpc.bytes = QByteArray("npc");

    DesktopRunDocumentSnapshot runtimeSaveObject =
        runtimeSaveNpc;
    runtimeSaveObject.filePath =
        temporaryDirectory.filePath(
            QStringLiteral("save/game/owner.obj"));
    runtimeSaveObject.type =
        ProjectDocumentType::ObjectList;
    runtimeSaveObject.bytes = QByteArray("object");

    DesktopRunDocumentSnapshot pendingMpc;
    pendingMpc.filePath =
        temporaryDirectory.filePath(
            QStringLiteral("mpc/map/owner/pending.mpc"));
    pendingMpc.ownerMapFilePath = ownerMapPath;
    pendingMpc.type = ProjectDocumentType::Image;
    pendingMpc.dirty = true;
    pendingMpc.includeInOverlay = true;
    pendingMpc.serializationSupported = true;
    pendingMpc.bytes = QByteArray("pending");

    DesktopRunDocumentSnapshot ownerlessMpc = pendingMpc;
    ownerlessMpc.filePath =
        temporaryDirectory.filePath(
            QStringLiteral("mpc/map/owner/ownerless.mpc"));
    ownerlessMpc.ownerMapFilePath.clear();

    const QList<DesktopRunDocumentSnapshot> snapshots =
        genericDesktopRunMapDocumentSnapshots({
            mapSnapshot,
            runtimeSaveNpc,
            runtimeSaveObject,
            pendingMpc,
            ownerlessMpc
        });
    const DesktopRunDocumentSnapshot* genericMap =
        findSnapshot(snapshots, ProjectDocumentType::Map);
    const DesktopRunDocumentSnapshot* genericNpc =
        findSnapshot(snapshots, ProjectDocumentType::NpcList);
    const DesktopRunDocumentSnapshot* genericObject =
        findSnapshot(snapshots, ProjectDocumentType::ObjectList);
    const DesktopRunDocumentSnapshot* genericMpc =
        findSnapshot(
            snapshots,
            ProjectDocumentType::Image,
            true);
    bool ok = check(
        snapshots.size() == 4 &&
            genericMap &&
            genericMap->dirty &&
            !genericMap->includeInOverlay &&
            genericNpc &&
            !genericNpc->dirty &&
            !genericNpc->includeInOverlay &&
            genericObject &&
            !genericObject->dirty &&
            !genericObject->includeInOverlay,
        "generic collector retains direct documents but removes clean runtime-save pinning");
    ok = check(
        genericMpc &&
            genericMpc->filePath == pendingMpc.filePath &&
            genericMpc->ownerMapFilePath == ownerMapPath &&
            genericMpc->includeInOverlay,
        "generic collector keeps only explicitly pinned MPC snapshots with owner provenance") &&
        ok;
    return ok;
}

bool testScriptSnapshot()
{
    std::cerr << "RUN script snapshot\n";
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create script snapshot temporary directory"))
    {
        return false;
    }
    const QByteArray crlfBytes("Print(1)\r\n");
    QByteArray normalizedBytes;
    bool ok = check(
        ScriptEditorWindow::normalizeDesktopRunSourceBytes(
            crlfBytes, normalizedBytes) &&
            normalizedBytes == QByteArray("Print(1)\n"),
        "desktop-run source normalization matches editor CRLF semantics");
    const QByteArray gbkBytes =
        QByteArray("Print(\"") +
        QByteArray::fromHex("B2E2CAD4") +
        QByteArray("\")\r\n");
    ok = check(
        ScriptEditorWindow::normalizeDesktopRunSourceBytes(
            gbkBytes, normalizedBytes) &&
            normalizedBytes ==
                QString::fromUtf8("Print(\"测试\")\n").toUtf8(),
        "desktop-run source normalization matches editor GBK decoding") &&
        ok;

    const QString sourcePath =
        temporaryDirectory.filePath(QStringLiteral("原始 脚本.txt"));
    const QString savedPath =
        temporaryDirectory.filePath(QStringLiteral("快照 输出.txt"));
    const QByteArray originalBytes("local value = 1\n");
    if (!check(
            writeBytes(sourcePath, originalBytes),
            "write script snapshot fixture"))
    {
        return false;
    }

    ScriptEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    if (!check(window.openFile(sourcePath), "open script snapshot fixture"))
        return false;
    ScriptEditor* editor = window.findChild<ScriptEditor*>();
    if (!check(editor != nullptr, "find script editor"))
        return false;
    editor->moveCursor(QTextCursor::End);
    window.insertScriptCall(QString::fromUtf8("Talk(\"未保存\")\n"));

    const QByteArray diskBefore = readBytes(sourcePath);
    const bool undoAvailableBefore =
        editor->document()->isUndoAvailable();
    const DesktopRunDocumentSnapshot snapshot =
        window.desktopRunDocumentSnapshot();
    ok = check(
        snapshot.filePath == QFileInfo(sourcePath).absoluteFilePath() &&
            snapshot.type == ProjectDocumentType::Script &&
            snapshot.dirty &&
            snapshot.serializationSupported &&
            !snapshot.includeInOverlay &&
            snapshot.bytes == editor->toPlainText().toUtf8(),
        "script snapshot exposes exact UTF-8 current buffer") ;
    ok = check(
        readBytes(sourcePath) == diskBefore &&
            editor->document()->isModified() &&
            editor->document()->isUndoAvailable() == undoAvailableBefore,
        "script snapshot leaves disk, dirty state and undo availability unchanged") &&
        ok;
    editor->undo();
    const bool undoWorked =
        editor->toPlainText().toUtf8() == originalBytes;
    editor->redo();
    ok = check(
        undoWorked &&
            editor->toPlainText().toUtf8() == snapshot.bytes,
        "script snapshot leaves undo and redo history executable") && ok;
    ok = check(
        window.saveAsFile(savedPath) &&
            readBytes(savedPath) == snapshot.bytes,
        "script snapshot bytes match production save output") && ok;
    return ok;
}

bool testNpcDataSnapshots()
{
    std::cerr << "RUN NPC data snapshots\n";
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create NPC data snapshot temporary directory"))
    {
        return false;
    }
    const QString npcPath =
        temporaryDirectory.filePath(QStringLiteral("scene.npc"));
    const QString objectPath =
        temporaryDirectory.filePath(QStringLiteral("scene.obj"));
    const QString npcResourcePath =
        temporaryDirectory.filePath(
            QStringLiteral("ini/npcres/actor.ini"));
    const QString npcOutputPath =
        temporaryDirectory.filePath(QStringLiteral("saved-scene.npc"));
    const QString objectOutputPath =
        temporaryDirectory.filePath(QStringLiteral("saved-scene.obj"));
    const QByteArray npcOriginal(
        "; keep npc comment\n[Head]\nCount=1\n\n"
        "[NPC000]\nName=Old\nNPCIni=actor.ini\n"
        "MapX=1\nMapY=2\nUnknownNpc=keep\n");
    const QByteArray objectOriginal(
        "# keep object comment\n[Head]\nCount=1\n\n"
        "[OBJ000]\nObjName=Old Box\nObjFile=box.ini\n"
        "MapX=2\nMapY=3\nActionTime=0x7\nUnknownObj=keep\n");
    if (!check(
            writeBytes(npcPath, npcOriginal) &&
                writeBytes(objectPath, objectOriginal) &&
                writeBytes(
                    npcResourcePath,
                    QByteArray(
                        "[Stand]\nImage=actor.mpc\n"
                        "Shade=actor.shd\nSound=actor.wav\n")),
            "write NPC data snapshot fixtures"))
    {
        return false;
    }

    NpcDataEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    const bool assetsPathSet =
        window.setAssetsBasePath(temporaryDirectory.path());
    const bool npcOpened =
        assetsPathSet && window.openNpcFile(npcPath, false);
    const bool objectOpened =
        npcOpened && window.openObjectFile(objectPath, false);
    const bool npcResourceOpened =
        objectOpened &&
        window.openNpcResourceFile(npcResourcePath, false);
    if (!check(
            assetsPathSet &&
                npcOpened &&
                objectOpened &&
                npcResourceOpened,
            "open NPC, object and NPC resource documents"))
    {
        std::cerr
            << "  assets=" << assetsPathSet
            << " npc=" << npcOpened
            << " object=" << objectOpened
            << " resource=" << npcResourceOpened << '\n';
        return false;
    }

    QLineEdit* npcName =
        window.findChild<QLineEdit*>(QStringLiteral("nameEdit"));
    QLineEdit* objectName =
        window.findChild<QLineEdit*>(QStringLiteral("objNameEdit"));
    if (!check(
            npcName && objectName,
            "find NPC and object property editors"))
    {
        return false;
    }
    npcName->setText(QString::fromUtf8("未保存守卫"));
    objectName->setText(QString::fromUtf8("未保存木箱"));
    QApplication::processEvents();

    const QList<ProjectDocumentState> before =
        window.currentProjectDocuments();
    const QByteArray npcDiskBefore = readBytes(npcPath);
    const QByteArray objectDiskBefore = readBytes(objectPath);
    const QList<DesktopRunDocumentSnapshot> firstSnapshots =
        window.desktopRunDocumentSnapshots();
    const QList<DesktopRunDocumentSnapshot> secondSnapshots =
        window.desktopRunDocumentSnapshots();
    const QList<ProjectDocumentState> after =
        window.currentProjectDocuments();
    const DesktopRunDocumentSnapshot* npcSnapshot =
        findSnapshot(firstSnapshots, ProjectDocumentType::NpcList);
    const DesktopRunDocumentSnapshot* objectSnapshot =
        findSnapshot(firstSnapshots, ProjectDocumentType::ObjectList);
    const DesktopRunDocumentSnapshot* resourceSnapshot =
        findSnapshot(firstSnapshots, ProjectDocumentType::NpcResource);
    const DesktopRunDocumentSnapshot* secondNpcSnapshot =
        findSnapshot(secondSnapshots, ProjectDocumentType::NpcList);
    const DesktopRunDocumentSnapshot* secondObjectSnapshot =
        findSnapshot(secondSnapshots, ProjectDocumentType::ObjectList);

    bool ok = check(
        npcSnapshot &&
            npcSnapshot->dirty &&
            npcSnapshot->serializationSupported &&
            npcSnapshot->bytes.contains(
                QString::fromUtf8("Name=未保存守卫").toUtf8()) &&
            objectSnapshot &&
            objectSnapshot->dirty &&
            objectSnapshot->serializationSupported &&
            objectSnapshot->bytes.contains(
                QString::fromUtf8("ObjName=未保存木箱").toUtf8()),
        "NPC and object snapshots contain current preserving-save bytes");
    ok = check(
        resourceSnapshot &&
            !resourceSnapshot->serializationSupported &&
            resourceSnapshot->diagnosticCode ==
                QStringLiteral(
                    "editor_run.overlay.npc_resource_snapshot_unsupported"),
        "NPC resource snapshot explicitly reports unsupported serialization") &&
        ok;
    ok = check(
        npcDiskBefore == readBytes(npcPath) &&
            objectDiskBefore == readBytes(objectPath) &&
            sameDocumentStates(before, after) &&
            firstSnapshots.size() == secondSnapshots.size() &&
            npcSnapshot &&
            secondNpcSnapshot &&
            secondNpcSnapshot->bytes == npcSnapshot->bytes &&
            objectSnapshot &&
            secondObjectSnapshot &&
            secondObjectSnapshot->bytes == objectSnapshot->bytes,
        "NPC data snapshots leave disk, baselines, section IDs and dirty state unchanged") &&
        ok;

    const QByteArray npcSnapshotBytes =
        npcSnapshot ? npcSnapshot->bytes : QByteArray();
    const QByteArray objectSnapshotBytes =
        objectSnapshot ? objectSnapshot->bytes : QByteArray();
    ok = check(
        window.saveNpcFileAs(npcOutputPath) &&
            window.saveObjectFileAs(objectOutputPath) &&
            readBytes(npcOutputPath) == npcSnapshotBytes &&
            readBytes(objectOutputPath) == objectSnapshotBytes,
        "NPC and object snapshot bytes match production preserving-save output") &&
        ok;
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    bool ok = true;
    ok = testMapCanvasAndBundleSnapshots() && ok;
    ok = testGenericMapSnapshotCollection() && ok;
    ok = testScriptSnapshot() && ok;
    ok = testNpcDataSnapshots() && ok;
    return ok ? 0 : 1;
}
