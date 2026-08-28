#include "TestSupport.h"

#include "../core/AssetCliRunner.h"
#include "../core/IMPImageFile.h"
#include "../core/JxAssetMigrator.h"
#include "../core/LuaScriptSyntaxValidator.h"
#include "../core/MapConverter.h"
#include "../core/MapFileEditor.h"
#include "../core/PicFileEditor.h"
#include "../core/ScriptConverter.h"
#include "../core/Util.h"
#include "../../src/Image/PicDecoder.h"
#include "../../src/tests/MapV3ContractFixture.h"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <filesystem>
#include <system_error>
#endif

namespace
{
using namespace TestSupport;

bool createDirectoryLink(
    const QString& target,
    const QString& link,
    QString& errorText)
{
    errorText.clear();
#ifdef Q_OS_WIN
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    const std::wstring targetPath =
        QDir::toNativeSeparators(target).
            toStdWString();
    const std::wstring linkPath =
        QDir::toNativeSeparators(link).
            toStdWString();
    if (CreateSymbolicLinkW(
            linkPath.c_str(),
            targetPath.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY |
                allowUnprivilegedCreate) != FALSE)
    {
        return true;
    }
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER &&
        CreateSymbolicLinkW(
            linkPath.c_str(),
            targetPath.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY) != FALSE)
    {
        return true;
    }
    errorText = QStringLiteral("Windows error %1")
        .arg(static_cast<qulonglong>(
            GetLastError()));
    return false;
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(
        std::filesystem::u8path(
            target.toUtf8().constData()),
        std::filesystem::u8path(
            link.toUtf8().constData()),
        error);
    if (!error)
        return true;
    errorText =
        QString::fromStdString(error.message());
    return false;
#endif
}

bool createFileLink(
    const QString& target,
    const QString& link,
    QString& errorText)
{
    errorText.clear();
#ifdef Q_OS_WIN
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    const std::wstring targetPath =
        QDir::toNativeSeparators(target).
            toStdWString();
    const std::wstring linkPath =
        QDir::toNativeSeparators(link).
            toStdWString();
    if (CreateSymbolicLinkW(
            linkPath.c_str(),
            targetPath.c_str(),
            allowUnprivilegedCreate) != FALSE)
    {
        return true;
    }
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER &&
        CreateSymbolicLinkW(
            linkPath.c_str(),
            targetPath.c_str(),
            0) != FALSE)
    {
        return true;
    }
    errorText = QStringLiteral("Windows error %1")
        .arg(static_cast<qulonglong>(
            GetLastError()));
    return false;
#else
    std::error_code error;
    std::filesystem::create_symlink(
        std::filesystem::u8path(
            target.toUtf8().constData()),
        std::filesystem::u8path(
            link.toUtf8().constData()),
        error);
    if (!error)
        return true;
    errorText =
        QString::fromStdString(error.message());
    return false;
#endif
}

QString readTemporaryFile(FILE* file)
{
    if (!file)
        return QString();

    std::fflush(file);
    std::rewind(file);

    QByteArray data;
    char buffer[4096];
    while (true)
    {
        size_t readCount = std::fread(buffer, 1, sizeof(buffer), file);
        if (readCount > 0)
            data.append(buffer, static_cast<int>(readCount));
        if (readCount < sizeof(buffer))
            break;
    }
    return QString::fromUtf8(data);
}

bool testMpcPalette256()
{
    MPCFileHead head = {};
    std::memcpy(head.head, "MPC File Ver2.0", 16);
    head.picCount = 1;
    head.paletteLen = 256;

    std::vector<uint8_t> buffer;
    appendValue(buffer, head);

    std::vector<ColorARGB> palette(256);
    palette[5] = {30, 20, 10, 255};
    const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.data());
    buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.size() * sizeof(ColorARGB));

    int32_t frameOffset = 0;
    appendValue(buffer, frameOffset);

    MPCPicHead picHead = {};
    picHead.dataLen = static_cast<int32_t>(sizeof(MPCPicHead) + 2);
    picHead.width = 1;
    picHead.height = 1;
    appendValue(buffer, picHead);
    buffer.push_back(1);
    buffer.push_back(5);

    PicFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), static_cast<int>(buffer.size())),
               "load 256-color MPC"))
    {
        return false;
    }

    QImage image = editor.getFrameImage(0);
    PicDecodedFile runtimeDecoded;
    bool runtimeOk = PicDecoder::decodeMPC(
        buffer.data(), static_cast<int>(buffer.size()), runtimeDecoded);
    std::vector<uint8_t> legacyBuffer = buffer;
    legacyBuffer.erase(legacyBuffer.begin() + 124, legacyBuffer.begin() + 128);
    size_t legacyOffsetTable = 124 + palette.size() * sizeof(ColorARGB);
    int32_t legacyFirstOffset = static_cast<int32_t>(
        legacyOffsetTable + sizeof(int32_t));
    std::memcpy(
        legacyBuffer.data() + legacyOffsetTable,
        &legacyFirstOffset,
        sizeof(legacyFirstOffset));
    PicFileEditor legacyEditor;
    PicDecodedFile legacyRuntimeDecoded;
    bool legacyEditorOk = legacyEditor.loadFromBuffer(
        legacyBuffer.data(), static_cast<int>(legacyBuffer.size()));
    bool legacyRuntimeOk = PicDecoder::decodeMPC(
        legacyBuffer.data(), static_cast<int>(legacyBuffer.size()), legacyRuntimeDecoded);
    const std::string savedFileName = "core-regression-roundtrip.img";
    bool saved = editor.saveAsIMP(savedFileName);
    bool reverseConversionRejected = editor.convertFileFormat(
        savedFileName,
        "core-regression-reverse.mpc",
        false);
    std::vector<uint8_t> savedBuffer = Util::readFileToBuffer(savedFileName);
    std::remove(savedFileName.c_str());
    std::remove("core-regression-reverse.mpc");

    // Verify the saved file has IMG header (IMP/IMG format)
    bool hasImgHeader = savedBuffer.size() >= 16 &&
        std::memcmp(savedBuffer.data(), "IMG File Ver1.0", 15) == 0;

    return check(!image.isNull(), "decode MPC frame") &&
        check(image.pixelColor(0, 0) == QColor(10, 20, 30, 255),
              "MPC palette index 5 color") &&
        check(runtimeOk && runtimeDecoded.frames.size() == 1,
              "runtime decode 128-byte MPC header") &&
        check(runtimeDecoded.frames[0].pixelData ==
                  std::vector<uint8_t>({30, 20, 10, 255}),
              "runtime MPC 256-color palette") &&
        check(legacyEditorOk && legacyRuntimeOk,
              "read legacy editor 124-byte MPC layout") &&
        check(saved && hasImgHeader,
              "write MPC as IMP/IMG format") &&
        check(!reverseConversionRejected,
              "reject reverse IMG/IMP to legacy image conversion");
}

bool testAsfPalette256()
{
    ASFFileHead head = {};
    std::memcpy(head.head, "ASF 1.01", 8);
    head.width = 1;
    head.height = 1;
    head.picCount = 1;
    head.paletteLen = 256;

    std::vector<uint8_t> buffer;
    appendValue(buffer, head);

    std::vector<ColorARGB> palette(256);
    palette[7] = {60, 50, 40, 255};
    const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.data());
    buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.size() * sizeof(ColorARGB));

    int32_t frameOffset = static_cast<int32_t>(
        sizeof(ASFFileHead) + palette.size() * sizeof(ColorARGB) + 2 * sizeof(int32_t));
    int32_t frameLength = 3;
    appendValue(buffer, frameOffset);
    appendValue(buffer, frameLength);
    buffer.push_back(1);
    buffer.push_back(128);
    buffer.push_back(7);

    PicFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), static_cast<int>(buffer.size())),
               "load 256-color ASF"))
    {
        return false;
    }

    QImage image = editor.getFrameImage(0);
    PicDecodedFile runtimeDecoded;
    bool runtimeOk = PicDecoder::decodeASF(
        buffer.data(), static_cast<int>(buffer.size()), runtimeDecoded);
    std::vector<uint8_t> legacyBuffer = buffer;
    legacyBuffer.erase(legacyBuffer.begin() + 60, legacyBuffer.begin() + 64);
    size_t legacyOffsetTable = 60 + palette.size() * sizeof(ColorARGB);
    int32_t legacyFirstOffset = static_cast<int32_t>(
        legacyOffsetTable + 2 * sizeof(int32_t));
    std::memcpy(
        legacyBuffer.data() + legacyOffsetTable,
        &legacyFirstOffset,
        sizeof(legacyFirstOffset));
    PicFileEditor legacyEditor;
    PicDecodedFile legacyRuntimeDecoded;
    bool legacyEditorOk = legacyEditor.loadFromBuffer(
        legacyBuffer.data(), static_cast<int>(legacyBuffer.size()));
    bool legacyRuntimeOk = PicDecoder::decodeASF(
        legacyBuffer.data(), static_cast<int>(legacyBuffer.size()), legacyRuntimeDecoded);
    const std::string savedFileName = "core-regression-roundtrip.img";
    bool saved = editor.saveAsIMP(savedFileName);
    std::vector<uint8_t> savedBuffer = Util::readFileToBuffer(savedFileName);
    PicFileEditor savedEditor;
    bool savedReloaded = saved && savedEditor.loadFromFile(savedFileName);
    QImage savedImage = savedEditor.getFrameImage(0);
    std::remove(savedFileName.c_str());

    // Verify the saved file has IMG header (IMP/IMG format)
    bool hasImgHeader = savedBuffer.size() >= 16 &&
        std::memcmp(savedBuffer.data(), "IMG File Ver1.0", 15) == 0;

    PicFileEditor resizedEditor;
    QImage resizedFrame(2, 1, QImage::Format_ARGB32);
    resizedFrame.fill(Qt::transparent);
    bool resizeLoaded = resizedEditor.loadFromBuffer(
        buffer.data(), static_cast<int>(buffer.size()));
    if (resizeLoaded)
        resizedEditor.setFrameImage(0, resizedFrame);
    const std::string resizedFileName = "core-regression-resized.img";
    bool resizeSaved = resizeLoaded &&
        resizedEditor.saveAsIMP(resizedFileName);
    PicFileEditor reloadedResizedEditor;
    bool resizeReloaded = resizeSaved &&
        reloadedResizedEditor.loadFromFile(resizedFileName);
    std::remove(resizedFileName.c_str());

    return check(!image.isNull(), "decode ASF frame") &&
        check(image.pixelColor(0, 0) == QColor(40, 50, 60, 128),
              "ASF palette index 7 color and alpha") &&
        check(runtimeOk && runtimeDecoded.frames.size() == 1,
              "runtime decode 64-byte ASF header") &&
        check(runtimeDecoded.frames[0].pixelData ==
                  std::vector<uint8_t>({60, 50, 40, 128}),
              "runtime ASF 256-color palette and alpha") &&
        check(legacyEditorOk && legacyRuntimeOk,
              "read legacy editor 60-byte ASF layout") &&
        check(saved && hasImgHeader,
              "write ASF as IMP/IMG format") &&
        check(savedReloaded && !savedImage.isNull() &&
                  savedImage.pixelColor(0, 0) == QColor(40, 50, 60, 128),
              "write ASF as IMP/IMG preserves semi alpha") &&
        check(resizeReloaded &&
                  reloadedResizedEditor.getFrameImage(0).size() == QSize(2, 1),
              "update single-frame ASF global dimensions");
}

bool testNegativeImpLength()
{
    std::vector<uint8_t> buffer(64, 0);
    std::memcpy(buffer.data(), "IMG File Ver1.0", 16);
    int32_t frameCount = 1;
    int32_t negativeLength = -1;
    std::memcpy(buffer.data() + 16, &frameCount, sizeof(frameCount));
    std::memcpy(buffer.data() + 48, &negativeLength, sizeof(negativeLength));

    const std::string fileName = "core-regression-negative.imp";
    if (!Util::writeFileFromBuffer(fileName, buffer.data(), buffer.size()))
        return check(false, "write negative IMP fixture");

    IMPImageFile image;
    bool rejected = !image.load(fileName);
    std::remove(fileName.c_str());
    return check(rejected, "reject negative IMP frame length");
}

bool testBoundedMapPath()
{
    std::vector<uint8_t> buffer(
        MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * 0x40, 0);
    MapEditorHead head = {};
    std::memcpy(head.head, MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN);
    std::memset(head.path, 'A', MAP_EDITOR_PATH_LEN);
    head.infoLen = 0x40;
    head.nameLen = 0x20;
    std::memcpy(buffer.data(), &head, sizeof(head));

    MapFileEditor editor;
    return check(editor.loadFromBuffer(buffer.data(), buffer.size()), "load zero-size map") &&
        check(editor.getMpcPath().size() == MAP_EDITOR_PATH_LEN,
              "read non-NUL map path within fixed bound");
}

bool testMapFixedStringsSaveUtf8()
{
    std::vector<uint8_t> buffer(
        MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * 0x40, 0);
    MapEditorHead head = {};
    std::memcpy(head.head, MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN);
    head.infoLen = 0x40;
    head.nameLen = 0x20;
    std::memcpy(buffer.data(), &head, sizeof(head));

    const std::string testText =
        "\xE6\xB5\x8B\xE8\xAF\x95"
        "\xE6\xB5\x8B\xE8\xAF\x95"
        "\xE6\xB5\x8B\xE8\xAF\x95"
        "\xE6\xB5\x8B\xE8\xAF\x95"
        "\xE6\xB5\x8B\xE8\xAF\x95"
        "\xE6\xB5\x8B\xE8\xAF\x95";

    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()), "load fixed-string map fixture"))
        return false;

    editor.setMpcPath("mpc/map/" + testText);
    MpcInfoData info;
    info.name = testText + ".mpc";
    editor.setMpcInfo(0, info);

    std::vector<uint8_t> saved = editor.saveToBuffer();
    if (!check(!saved.empty(), "save fixed-string map fixture"))
        return false;

    MapEditorHead savedHead = {};
    std::memcpy(&savedHead, saved.data(), sizeof(savedHead));
    int32_t headerSize = 0;
    int32_t pathCapacity = 0;
    std::memcpy(&headerSize, savedHead.dataNil2, sizeof(headerSize));
    std::memcpy(&pathCapacity, savedHead.dataNil2 + 4, sizeof(pathCapacity));

    if (!check(std::memcmp(saved.data(), MAP_EDITOR_HEADSTR_V3, MAP_EDITOR_HEADSTR_LEN) == 0,
               "saved map defaults to MAP File Ver3.0") ||
        !check(headerSize >= MAP_EDITOR_HEAD_LEN, "saved MAP 3.0 header size is valid") ||
        !check(pathCapacity >= MAP_EDITOR_V3_PATH_LEN, "saved MAP 3.0 path field is expanded") ||
        !check(savedHead.nameLen >= MAP_EDITOR_V3_NAME_LEN, "saved MAP 3.0 name field is expanded") ||
        !check(savedHead.infoLen >= savedHead.nameLen + MAP_EDITOR_V3_INFO_EXTRA_LEN,
               "saved MAP 3.0 info field includes reserved data"))
    {
        return false;
    }

    const char* pathData = reinterpret_cast<const char*>(saved.data() + MAP_EDITOR_HEAD_LEN);
    size_t pathLength = 0;
    while (pathLength < static_cast<size_t>(pathCapacity) && pathData[pathLength] != '\0')
        pathLength++;
    std::string path(pathData, pathLength);

    const char* nameData = reinterpret_cast<const char*>(saved.data() + headerSize);
    size_t nameLength = 0;
    while (nameLength < static_cast<size_t>(savedHead.nameLen) && nameData[nameLength] != '\0')
        nameLength++;
    std::string name(nameData, nameLength);

    return check(path == "mpc/map/" + testText, "saved MAP 3.0 path is not truncated") &&
        check(name == testText + ".mpc", "saved MAP 3.0 MPC name is not truncated") &&
        check(Util::isUtf8(reinterpret_cast<const uint8_t*>(path.data()), path.size()),
              "saved map path is UTF-8") &&
        check(Util::isUtf8(reinterpret_cast<const uint8_t*>(name.data()), name.size()),
              "saved MPC name is UTF-8");
}

bool testLegacyMapGbkStringsThatLookLikeUtf8()
{
    std::vector<uint8_t> buffer(
        MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * 0x40, 0);
    MapEditorHead head = {};
    std::memcpy(head.head, MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN);
    head.infoLen = 0x40;
    head.nameLen = 0x20;
    std::memcpy(buffer.data(), &head, sizeof(head));

    const unsigned char gbkPath[] = {
        '\\', 'm', 'p', 'c', '\\', 'm', 'a', 'p', '\\',
        0xC9, 0xBD, 0xD5, 0xAF
    };
    const unsigned char gbkName[] = {
        'w', 'a', 'l', 'l', '-',
        0xC9, 0xBD, 0xD5, 0xAF,
        '.', 'm', 'p', 'c'
    };
    std::memcpy(buffer.data() + MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN,
                gbkPath, sizeof(gbkPath));
    std::memcpy(buffer.data() + MAP_EDITOR_HEAD_LEN,
                gbkName, sizeof(gbkName));

    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load legacy GBK map fixture"))
    {
        return false;
    }

    return check(editor.getMpcPath() == "\\mpc\\map\\山寨",
                 "legacy GBK map path is decoded even when bytes look like UTF-8") &&
        check(editor.getMpcInfo(0).name == "wall-山寨.mpc",
              "legacy GBK MPC name is decoded even when bytes look like UTF-8");
}

bool testMapConverterWritesVersion3()
{
    QTemporaryDir temporaryDirectory;
    if (!check(temporaryDirectory.isValid(), "create map converter temp directory"))
        return false;

    std::vector<uint8_t> buffer = makeEmptyMapBuffer(1, 1);
    const char* path = "Mpc/Map/Test/";
    const char* name = "Tile001.MPC";
    std::memcpy(buffer.data() + MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN,
                path, std::strlen(path));
    std::memcpy(buffer.data() + MAP_EDITOR_HEAD_LEN,
                name, std::strlen(name));

    QString inputPath = temporaryDirectory.filePath("input.map");
    QString outputPath = temporaryDirectory.filePath("output.map");
    if (!check(Util::writeFileFromBuffer(inputPath.toUtf8().toStdString(),
                                         buffer.data(), buffer.size()),
               "write map converter fixture"))
    {
        return false;
    }

    MapConverter converter;
    if (!check(converter.convertFile(inputPath.toUtf8().toStdString(),
                                     outputPath.toUtf8().toStdString(),
                                     true, false, false),
               "convert map fixture to MAP 3.0"))
    {
        return false;
    }

    std::vector<uint8_t> converted = Util::readFileToBuffer(outputPath.toUtf8().toStdString());
    MapFileEditor editor;
    return check(converted.size() > MAP_EDITOR_HEAD_LEN,
                 "converted MAP 3.0 fixture exists") &&
        check(std::memcmp(converted.data(), MAP_EDITOR_HEADSTR_V3, MAP_EDITOR_HEADSTR_LEN) == 0,
              "map converter writes MAP File Ver3.0") &&
        check(editor.loadFromBuffer(converted.data(), converted.size()),
              "MapFileEditor reloads converted MAP 3.0") &&
        check(editor.getMpcPath() == "mpc/map/test/",
              "converted MAP 3.0 lowercases ASCII in the MPC path") &&
        check(editor.getMpcInfo(0).name == "tile001.mpc",
              "converted MAP 3.0 lowercases ASCII in the MPC name");
}

bool testMapMigrationUsesVersionContract()
{
    QTemporaryDir sourceDirectory;
    QTemporaryDir outputDirectory;
    if (!check(sourceDirectory.isValid() && outputDirectory.isValid(),
               "create MAP migration version-contract temp directories"))
    {
        return false;
    }

    QDir source(sourceDirectory.path());
    if (!check(source.mkpath("map/shared-scene"),
               "create MAP migration fixture directories"))
        return false;

    std::vector<uint8_t> version2Buffer = makeEmptyMapBuffer(1, 1);
    const char* version2Path = MapV3ContractFixture::MpcPath;
    const char* version2Name = MapV3ContractFixture::MpcName;
    std::memcpy(version2Buffer.data() + MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN,
                version2Path, std::strlen(version2Path));
    std::memcpy(version2Buffer.data() + MAP_EDITOR_HEAD_LEN,
                version2Name, std::strlen(version2Name));

    MapEditorHead version2Head = {};
    std::memcpy(&version2Head, version2Buffer.data(), sizeof(version2Head));
    version2Head.dataNil[3] = static_cast<char>(0x4A);
    version2Head.dataNil2[20] = static_cast<char>(0x6A);
    std::memcpy(version2Buffer.data(), &version2Head, sizeof(version2Head));

    const int32_t mpcValues[4] = {
        MapV3ContractFixture::MpcIndex,
        MapV3ContractFixture::MpcDynamic,
        MapV3ContractFixture::MpcObstacle,
        MapV3ContractFixture::MpcNil
    };
    const size_t mpcValueOffset = MAP_EDITOR_HEAD_LEN + MAP_EDITOR_V2_NAME_LEN;
    std::memcpy(version2Buffer.data() + mpcValueOffset, mpcValues, sizeof(mpcValues));
    const size_t mpcOpaqueOffset = mpcValueOffset + sizeof(mpcValues);
    version2Buffer[mpcOpaqueOffset + 3] = 0x5B;

    MapTileData markerTile;
    for (int layer = 0; layer < MAP_EDITOR_TILE_LAYER; ++layer)
    {
        markerTile.layer[layer].frame = MapV3ContractFixture::LayerFrames[layer];
        markerTile.layer[layer].mpc = MapV3ContractFixture::LayerMpcs[layer];
    }
    markerTile.obstacle = MapV3ContractFixture::TileObstacle;
    markerTile.trap = MapV3ContractFixture::TileTrap;
    markerTile.end[0] = MapV3ContractFixture::TileEnd[0];
    markerTile.end[1] = MapV3ContractFixture::TileEnd[1];
    const size_t tileOffset =
        MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * MAP_EDITOR_V2_INFO_LEN;
    for (int layer = 0; layer < MAP_EDITOR_TILE_LAYER; ++layer)
    {
        version2Buffer[tileOffset + layer * 2] = markerTile.layer[layer].frame;
        version2Buffer[tileOffset + layer * 2 + 1] = markerTile.layer[layer].mpc;
    }
    version2Buffer[tileOffset + 6] = markerTile.obstacle;
    version2Buffer[tileOffset + 7] = markerTile.trap;
    version2Buffer[tileOffset + 8] = markerTile.end[0];
    version2Buffer[tileOffset + 9] = markerTile.end[1];

    const QString version2RelativePath = QStringLiteral("map/shared-scene/nested.map");
    const QString version2SourcePath = source.filePath(version2RelativePath);
    if (!check(Util::writeFileFromBuffer(version2SourcePath.toUtf8().toStdString(),
                                         version2Buffer.data(), version2Buffer.size()),
               "write MAP File Ver2.0 migration fixture"))
    {
        return false;
    }

    MapFileEditor version3Editor;
    if (!check(version3Editor.loadFromBuffer(version2Buffer.data(), version2Buffer.size()),
               "load MAP File Ver2.0 fixture for Ver3.0 source"))
    {
        return false;
    }
    version3Editor.setMpcPath("Mpc/Map/Already-Migrated/");
    MpcInfoData version3MpcInfo = version3Editor.getMpcInfo(0);
    version3MpcInfo.name = "Shared-Tile.MPC";
    version3Editor.setMpcInfo(0, version3MpcInfo);
    std::vector<uint8_t> version3Buffer = version3Editor.saveToBuffer();
    MapEditorHead version3Head = {};
    if (!check(version3Buffer.size() >= sizeof(version3Head),
               "MAP File Ver3.0 fixture contains a complete header"))
    {
        return false;
    }
    std::memcpy(&version3Head, version3Buffer.data(), sizeof(version3Head));
    int32_t version3PathLength = 0;
    std::memcpy(&version3PathLength, version3Head.dataNil2 + 4, sizeof(version3PathLength));
    if (!check(version3PathLength > 1,
               "MAP File Ver3.0 fixture declares extended path padding"))
    {
        return false;
    }
    const size_t unusedPathByte = MAP_EDITOR_HEAD_LEN +
        static_cast<size_t>(version3PathLength - 1);
    if (!check(version3Buffer.size() > unusedPathByte,
               "MAP File Ver3.0 fixture has extended path padding"))
    {
        return false;
    }
    version3Buffer[unusedPathByte] = 0x5A;

    std::vector<uint8_t> expectedNormalizedVersion3 = version3Buffer;
    auto lowercaseFixedString = [](
        std::vector<uint8_t>& data,
        size_t offset,
        size_t capacity)
    {
        for (size_t index = 0;
             index < capacity && data[offset + index] != 0;
             ++index)
        {
            uint8_t& character = data[offset + index];
            if (character >= 'A' && character <= 'Z')
            {
                character = static_cast<uint8_t>(
                    character + ('a' - 'A'));
            }
        }
    };
    lowercaseFixedString(
        expectedNormalizedVersion3,
        MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN,
        MAP_EDITOR_PATH_LEN);
    lowercaseFixedString(
        expectedNormalizedVersion3,
        MAP_EDITOR_HEAD_LEN,
        static_cast<size_t>(version3PathLength));
    lowercaseFixedString(
        expectedNormalizedVersion3,
        static_cast<size_t>(MAP_EDITOR_HEAD_LEN + version3PathLength),
        static_cast<size_t>(version3Head.nameLen));

    const QString version3SourcePath = source.filePath("map/current.map");
    if (!check(Util::writeFileFromBuffer(version3SourcePath.toUtf8().toStdString(),
                                         version3Buffer.data(), version3Buffer.size()),
               "write MAP File Ver3.0 migration fixture"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.convertScript = false;
    options.writeModProfile = false;
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result = migrator.migrate(
        sourceDirectory.path(), outputDirectory.path(), options, report);

    const std::vector<uint8_t> migratedVersion2 = Util::readFileToBuffer(
        QDir(outputDirectory.path()).filePath(version2RelativePath).toUtf8().toStdString());
    const std::vector<uint8_t> migratedVersion3 = Util::readFileToBuffer(
        QDir(outputDirectory.path()).filePath("map/current.map").toUtf8().toStdString());
    const std::vector<uint8_t> expectedVersion3 = MapV3ContractFixture::build();
    MapFileEditor migratedEditor;
    const bool migratedVersion2Loaded = !migratedVersion2.empty() &&
        migratedEditor.loadFromBuffer(migratedVersion2.data(), migratedVersion2.size());
    if (!check(migratedVersion2Loaded,
               "migrated MAP File Ver3.0 structure reloads"))
    {
        return false;
    }
    const MpcInfoData& migratedMpcInfo = migratedEditor.getMpcInfo(0);
    const MapTileData& migratedTile = migratedEditor.getTile(0, 0);

    return check(result == MigrationResult::Success,
                 "MAP migration version-contract fixture succeeds") &&
        check(report.convertedMaps == 2,
              "MAP migration report counts converted map data separately") &&
        check(migratedVersion2.size() > MAP_EDITOR_HEAD_LEN &&
                  std::memcmp(migratedVersion2.data(), MAP_EDITOR_HEADSTR_V3,
                              MAP_EDITOR_HEADSTR_LEN) == 0,
              "MAP File Ver2.0 migration writes the complete Ver3.0 contract") &&
        check(migratedVersion2 == expectedVersion3,
              "production MAP migration matches the shared Ver3.0 golden bytes") &&
        check(migratedEditor.getWidth() == 1 && migratedEditor.getHeight() == 1,
              "MAP migration preserves dimensions") &&
        check(migratedEditor.getMpcPath() == version2Path,
              "MAP migration preserves embedded shared MPC path") &&
        check(migratedMpcInfo.name == version2Name &&
                  migratedMpcInfo.index == mpcValues[0] &&
                  migratedMpcInfo.dynamic == mpcValues[1] &&
                  migratedMpcInfo.obstacle == mpcValues[2] &&
                  migratedMpcInfo.nil == mpcValues[3] &&
                  migratedMpcInfo.opaqueTail[3] == 0x5B,
              "MAP migration preserves MPC entry fields and opaque bytes") &&
        check(migratedTile.layer[0].frame == markerTile.layer[0].frame &&
                  migratedTile.layer[0].mpc == markerTile.layer[0].mpc &&
                  migratedTile.layer[1].frame == markerTile.layer[1].frame &&
                  migratedTile.layer[1].mpc == markerTile.layer[1].mpc &&
                  migratedTile.layer[2].frame == markerTile.layer[2].frame &&
                  migratedTile.layer[2].mpc == markerTile.layer[2].mpc &&
                  migratedTile.obstacle == markerTile.obstacle &&
                  migratedTile.trap == markerTile.trap &&
                  migratedTile.end[0] == markerTile.end[0] &&
                  migratedTile.end[1] == markerTile.end[1],
              "MAP migration preserves tile layers, obstacle, trap and tail") &&
        check(migratedVersion2.size() > MAP_EDITOR_HEAD_LEN &&
                  migratedVersion2[MAP_EDITOR_HEADSTR_LEN + 3] == 0x4A &&
                  migratedVersion2[MAP_EDITOR_HEAD_LEN - MAP_EDITOR_NULL2_LEN + 20] == 0x6A,
              "MAP migration preserves opaque header bytes") &&
        check(migratedVersion3 == expectedNormalizedVersion3,
              "MAP File Ver3.0 lowercases only embedded resource strings");
}

bool testMapMigrationRejectsUnsupportedAndMalformedVersions()
{
    QTemporaryDir sourceDirectory;
    QTemporaryDir outputDirectory;
    if (!check(sourceDirectory.isValid() && outputDirectory.isValid(),
               "create malformed MAP migration temp directories"))
    {
        return false;
    }

    QDir source(sourceDirectory.path());
    if (!check(source.mkpath("map"), "create malformed MAP fixture directory"))
        return false;

    std::vector<uint8_t> unsupported(MAP_EDITOR_HEADSTR_LEN, 0);
    std::memcpy(unsupported.data(), "MAP File Ver4.0", MAP_EDITOR_HEADSTR_LEN);
    std::vector<uint8_t> truncatedVersion3(MAP_EDITOR_HEADSTR_LEN, 0);
    std::memcpy(truncatedVersion3.data(), MAP_EDITOR_HEADSTR_V3, MAP_EDITOR_HEADSTR_LEN);
    std::vector<uint8_t> truncatedVersion2(MAP_EDITOR_HEADSTR_LEN, 0);
    std::memcpy(truncatedVersion2.data(), MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN);

    if (!check(Util::writeFileFromBuffer(source.filePath("map/future.map").toUtf8().toStdString(),
                                         unsupported.data(), unsupported.size()) &&
                   Util::writeFileFromBuffer(source.filePath("map/truncated-v3.map").toUtf8().toStdString(),
                                             truncatedVersion3.data(), truncatedVersion3.size()) &&
                   Util::writeFileFromBuffer(source.filePath("map/truncated-v2.map").toUtf8().toStdString(),
                                             truncatedVersion2.data(), truncatedVersion2.size()),
               "write unsupported and malformed MAP fixtures"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.convertScript = false;
    options.writeModProfile = false;
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result = migrator.migrate(
        sourceDirectory.path(), outputDirectory.path(), options, report);
    const QString migrationLog = report.logLines.join('\n');

    return check(result == MigrationResult::Failed && report.errorCount == 3,
                 "unsupported and malformed MAP files fail migration") &&
        check(migrationLog.contains("Unsupported MAP version header") &&
                  migrationLog.contains("Invalid MAP File Ver3.0 structure") &&
                  migrationLog.contains("Invalid MAP File Ver2.0 structure"),
              "MAP migration reports each version-contract failure") &&
        check(!QFileInfo::exists(QDir(outputDirectory.path()).filePath("map/future.map")) &&
                  !QFileInfo::exists(QDir(outputDirectory.path()).filePath("map/truncated-v3.map")) &&
                  !QFileInfo::exists(QDir(outputDirectory.path()).filePath("map/truncated-v2.map")),
              "failed MAP migration does not publish malformed files");
}

bool testMapFileEditorTransactionalOpaqueAndSafePaths()
{
    std::vector<uint8_t> buffer = makeEmptyMapBuffer(2, 2);
    MapEditorHead sourceHead = {};
    std::memcpy(&sourceHead, buffer.data(), sizeof(sourceHead));
    sourceHead.dataNil2[20] = static_cast<char>(0x6A);
    std::memcpy(buffer.data(), &sourceHead, sizeof(sourceHead));
    const size_t sourceOpaqueOffset =
        MAP_EDITOR_HEAD_LEN + MAP_EDITOR_V2_NAME_LEN + 16 + 3;
    buffer[sourceOpaqueOffset] = 0x5B;

    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load transactional MAP fixture"))
    {
        return false;
    }

    editor.setMapFileName("retained.map");
    MapTileData retainedTile = makeMarkerTile(9);
    editor.setTile(1, 1, retainedTile);
    MpcInfoData mpcInfo = editor.getMpcInfo(0);
    mpcInfo.name = "tile.mpc";
    editor.setMpcInfo(0, mpcInfo);
    editor.setMpcPath("\\mpc\\map\\scene");

    bool ok = check(editor.getMpcFilePath(0) == "mpc/map/scene/tile.mpc",
                    "normalize original leading-slash MAP resource path");
    editor.setMpcPath("../../escape");
    ok = check(editor.getMpcFilePath(0).empty(),
               "reject parent traversal in MAP resource path") && ok;
    editor.setMpcPath("\\mpc\\map\\scene");

    // Failed buffer loads must preserve the valid map, its path and edits.
    ok = check(!editor.loadFromBuffer(nullptr, 0),
               "reject null MAP buffer") && ok;
    ok = check(editor.isLoaded() && editor.getWidth() == 2 && editor.getHeight() == 2 &&
                   editor.getMapFileName() == "retained.map" &&
                   editor.getTile(1, 1).layer[0].mpc == retainedTile.layer[0].mpc,
               "failed MAP load preserves current document") && ok;

    std::vector<uint8_t> negativeDimensions = makeEmptyMapBuffer(1, 1);
    MapEditorHead negativeHead = {};
    std::memcpy(&negativeHead, negativeDimensions.data(), sizeof(negativeHead));
    negativeHead.width = -1;
    std::memcpy(negativeDimensions.data(), &negativeHead, sizeof(negativeHead));
    ok = check(!editor.loadFromBuffer(
                   negativeDimensions.data(), negativeDimensions.size()),
               "reject negative MAP dimensions") && ok;
    ok = check(editor.isLoaded() && editor.getWidth() == 2 && editor.getHeight() == 2 &&
                   editor.getMapFileName() == "retained.map",
               "negative-dimension MAP load preserves current document") && ok;

    std::vector<uint8_t> saved = editor.saveToBuffer();
    if (!check(saved.size() > MAP_EDITOR_HEAD_LEN, "save opaque MAP fixture"))
        return false;
    MapEditorHead savedHead = {};
    std::memcpy(&savedHead, saved.data(), sizeof(savedHead));
    int32_t savedHeaderSize = 0;
    std::memcpy(&savedHeaderSize, savedHead.dataNil2, sizeof(savedHeaderSize));
    const size_t savedOpaqueOffset = static_cast<size_t>(savedHeaderSize) +
        static_cast<size_t>(savedHead.nameLen) + 16 + 3;
    ok = check(static_cast<unsigned char>(savedHead.dataNil2[20]) == 0x6A,
               "preserve opaque MAP header bytes") && ok;
    ok = check(savedOpaqueOffset < saved.size() && saved[savedOpaqueOffset] == 0x5B,
               "preserve opaque MPC entry bytes") && ok;

    // A successful anonymous buffer load represents a new document and must
    // not retain the previous file path.
    std::vector<uint8_t> replacement = makeEmptyMapBuffer(1, 1);
    ok = check(editor.loadFromBuffer(replacement.data(), replacement.size()) &&
                   editor.getMapFileName().empty(),
               "successful buffer load clears stale MAP file name") && ok;
    return ok;
}

bool testScriptAliases()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "GetPlayerLevel($Level);\n"
        "Assing($Event,470);\n"
        "SetPlayrDir(1);\n"
        "PlayGoto(25,29);\n"
        "MessageBox(\"hello\");\n");

    return check(converted.find("getplayerlevel(\"Level\")") != std::string::npos,
                 "GetPlayerLevel output variable") &&
        check(converted.find("assign(\"Event\",470)") != std::string::npos,
              "Assing alias") &&
        check(converted.find("setplayerdir(1)") != std::string::npos,
              "SetPlayrDir alias") &&
        check(converted.find("playergoto(25,29)") != std::string::npos,
              "PlayGoto alias") &&
        check(converted.find("displaymessage(\"hello\")") != std::string::npos,
              "MessageBox alias");
}

bool testScriptLegacySpellingAliases()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "RunScirpt(\"event.txt\");\n"
        "LodaObj(\"HanYan-None.obj\");\n"
        "NpcAction(\"Guard\", 11);\n"
        "GetGoodsMun(\"goods.ini\");\n"
        "HideBottomWindow();\n"
        "ShowBottomWindow();\n");

    bool hasUnsupportedApi = false;
    for (const ScriptConversionDiagnostic& diagnostic : converter.getDiagnostics())
    {
        if (diagnostic.category == "UnsupportedApi")
            hasUnsupportedApi = true;
    }

    return check(converted.find("runscript(\"event.txt\")") != std::string::npos,
                 "RunScirpt alias") &&
        check(converted.find("loadobj(\"HanYan-None.obj\")") != std::string::npos,
              "LodaObj alias") &&
        check(converted.find("setnpcaction(\"Guard\",11)") != std::string::npos,
              "NpcAction alias") &&
        check(converted.find("getgoodsnum(\"goods.ini\")") != std::string::npos,
              "GetGoodsMun alias") &&
        check(converted.find("hidebottomwnd()") != std::string::npos,
              "HideBottomWindow alias") &&
        check(converted.find("showbottomwnd()") != std::string::npos,
              "ShowBottomWindow alias") &&
        check(!hasUnsupportedApi, "legacy spelling aliases are supported runtime APIs");
}

bool testScriptDiagnostics()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "if($Flag) goto @Start;\n"
        "NotAFunction\n"
        "UnknownCall(1);\n");

    const std::vector<ScriptConversionDiagnostic>& diagnostics = converter.getDiagnostics();
    bool hasUnhandledIf = false;
    bool hasUnhandledStatement = false;
    bool hasUnsupportedApi = false;
    for (const ScriptConversionDiagnostic& diagnostic : diagnostics)
    {
        if (diagnostic.category == "UnhandledIf")
            hasUnhandledIf = true;
        if (diagnostic.category == "UnhandledStatement")
            hasUnhandledStatement = true;
        if (diagnostic.category == "UnsupportedApi")
            hasUnsupportedApi = true;
    }

    return check(converted.find("TODO(jx-script-converter)") != std::string::npos,
                 "unhandled script output is marked") &&
        check(hasUnhandledIf, "script converter records unhandled if") &&
        check(hasUnhandledStatement, "script converter records unhandled statement") &&
        check(hasUnsupportedApi, "script converter records unsupported API");
}

bool testScriptGambleOutputVariable()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "Gamble(80, 0, $map027lwc);\n");

    bool hasUnsupportedApi = false;
    for (const ScriptConversionDiagnostic& diagnostic : converter.getDiagnostics())
    {
        if (diagnostic.category == "UnsupportedApi")
            hasUnsupportedApi = true;
    }

    return check(converted.find("gamble(80,0,\"map027lwc\")") != std::string::npos,
                 "Gamble output variable") &&
        check(!hasUnsupportedApi, "Gamble is a supported runtime API");
}

bool testScriptLegacyWildcardArguments()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "SetPlayerPos(*, *);\n"
        "SetPlayerDir(*);\n"
        "NpcGotoEx(\"Partner\", *, *);\n"
        "PlayerAddEmotion(-*);\n"
        "AddNpc(\"Partner\".ini, *, *, *);\n"
        "SetNpcAction(\"Partner\", , \"\");\n"
        "Assign($Result, *);\n");

    return check(converted.find("setplayerpos(-1,-1)") != std::string::npos,
                 "legacy wildcard player position arguments") &&
        check(converted.find("setplayerdir(-1)") != std::string::npos,
              "legacy wildcard player direction argument") &&
        check(converted.find("npcgotoex(\"Partner\",-1,-1)") != std::string::npos,
              "legacy wildcard NPC destination arguments") &&
        check(converted.find("playeraddemotion(-1)") != std::string::npos,
              "legacy negative wildcard argument") &&
        check(converted.find("addnpc(\"Partner.ini\",-1,-1,-1)") != std::string::npos,
              "legacy quoted string file extension") &&
        check(converted.find("setnpcaction(\"Partner\",-1,\"\")") != std::string::npos,
              "legacy empty middle argument") &&
        check(converted.find("assign(\"Result\",-1)") != std::string::npos,
              "legacy wildcard assignment value");
}

bool testScriptOriginalCompatibilityCommands()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "SetPartnerLevel(\"Partner\", 45);\n"
        "PlayerAddEmotion(1);\n"
        "PlayerAddJustice(-1);\n"
        "Memo(\"note\");\n");

    bool hasUnsupportedApi = false;
    for (const ScriptConversionDiagnostic& diagnostic : converter.getDiagnostics())
    {
        if (diagnostic.category == "UnsupportedApi")
            hasUnsupportedApi = true;
    }

    return check(converted.find("setpartnerlevel(\"Partner\",45)") != std::string::npos,
                 "SetPartnerLevel named partner form") &&
        check(converted.find("playeraddemotion(1)") != std::string::npos,
              "PlayerAddEmotion compatibility command") &&
        check(converted.find("playeraddjustice(-1)") != std::string::npos,
              "PlayerAddJustice compatibility command") &&
        check(converted.find("addtomemo(\"note\")") != std::string::npos,
              "Memo alias remains AddToMemo") &&
        check(!hasUnsupportedApi, "original compatibility commands are supported runtime APIs");
}

bool testScriptApiCatalogMatchesRuntimeRegistry()
{
    const QString sourceRoot = QDir::cleanPath(
        QDir::fromNativeSeparators(QString::fromUtf8(JXQY_SOURCE_ROOT_PATH)));
    QFile runtimeFile(QDir(sourceRoot).filePath("src/Game/Script/Script.cpp"));
    if (!check(runtimeFile.open(QIODevice::ReadOnly),
               "open runtime Script.cpp for API catalog contract"))
    {
        return false;
    }
    const QString runtimeSource = QString::fromUtf8(runtimeFile.readAll());

    const QRegularExpression functionPattern(
        QStringLiteral("(?m)^[\\t ]*regFunc\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)\\s*;"));
    auto extractFunctionNames = [&](const QString& source) {
        std::set<std::string> names;
        QRegularExpressionMatchIterator matches = functionPattern.globalMatch(source);
        while (matches.hasNext())
            names.insert(matches.next().captured(1).toLower().toStdString());
        return names;
    };
    std::set<std::string> registeredNames = extractFunctionNames(runtimeSource);

    bool aliasesAccepted = true;
    const QRegularExpression aliasPattern(
        QStringLiteral("(?m)^[\\t ]*regAlias\\s*\\(\\s*\"([A-Za-z_][A-Za-z0-9_]*)\""));
    QRegularExpressionMatchIterator aliasMatches = aliasPattern.globalMatch(runtimeSource);
    while (aliasMatches.hasNext())
    {
        const QString alias = aliasMatches.next().captured(1);
        ScriptConverter converter;
        converter.convertScript(alias.toStdString() + "();\n");
        for (const ScriptConversionDiagnostic& diagnostic : converter.getDiagnostics())
        {
            if (diagnostic.category == "UnsupportedApi")
            {
                aliasesAccepted = false;
                break;
            }
        }
        if (!aliasesAccepted)
            break;
    }

    const bool multilineRegistrationParsed =
        extractFunctionNames(QStringLiteral("regFunc(\n    MultilineApi\n);\n")) ==
            std::set<std::string>{"multilineapi"} &&
        aliasPattern.match(QStringLiteral("regAlias(\n    \"MultilineAlias\", handler);"))
            .captured(1) == QStringLiteral("MultilineAlias");

    return check(multilineRegistrationParsed,
                 "runtime API catalog parser accepts multiline registrations") &&
        check(registeredNames == ScriptConverter::runtimeApiNames(),
                  "editor canonical API catalog exactly matches runtime regFunc names") &&
        check(aliasesAccepted,
              "script converter accepts every runtime compatibility alias");
}

bool testJxqy2ProductionScriptTypoRepairs()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "SetNpcDir(\"史忠良\",\"5);\n"
        "PlayMusic(\"ks72.wav\"\");\n"
        "Return;;\n"
        "If($DuanJiaZhuangClose,1) @Talk1;\n"
        "ShowMessage((\"长安西郊\");\n"
        "Retuen;\n");
    LuaScriptSyntaxIssue syntaxIssue = LuaScriptSyntaxValidator::validateScriptContent(
        "jxqy2-production-typos.txt", converted);

    ScriptConverter invalidConditionConverter;
    std::string invalidCondition = invalidConditionConverter.convertScript(
        "If(DuanJiaZhuangClose,1) @Talk1;\n");
    const bool rejectsUnscopedCommaCondition = std::any_of(
        invalidConditionConverter.getDiagnostics().begin(),
        invalidConditionConverter.getDiagnostics().end(),
        [](const ScriptConversionDiagnostic& diagnostic) {
            return diagnostic.category == "UnhandledIf";
        });

    return check(converted.find("setnpcdir(\"史忠良\",5);") != std::string::npos,
                 "repair missing quote around JXQY2 numeric NPC direction") &&
        check(converted.find("playmusic(\"ks72.wav\");") != std::string::npos,
              "repair duplicated quote in JXQY2 music call") &&
        check(converted.find("if getvar(\"DuanJiaZhuangClose\") == 1 then goto Talk1 end") !=
                  std::string::npos,
              "repair JXQY2 comma equality condition") &&
        check(converted.find("showmessage(\"长安西郊\");") != std::string::npos,
              "repair duplicated opening parenthesis in JXQY2 message call") &&
        check(converted.find("goto __jx_script_return") != std::string::npos,
              "repair Return double semicolon and Retuen spelling") &&
        check(converter.getDiagnostics().empty(),
              "JXQY2 production typo repairs have no conversion diagnostics") &&
        check(rejectsUnscopedCommaCondition &&
                  invalidCondition.find("unhandled if condition") != std::string::npos,
              "comma equality repair only accepts legacy dollar-prefixed variables") &&
        check(syntaxIssue.message.isEmpty(),
              "JXQY2 production typo repairs remain valid Lua");
}

bool testScriptModCompatibilityCommands()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "GetPlayerMagicLevel(\"player-magic-test.ini\", $Level);\n"
        "SetNpcDestination(\"Guard\", *, *);\n"
        "SetWalkIsRun(1);\n"
        "ShowSystemMsg(\"saved\", 6000);\n"
        "BuyGoodsOnly(\"shop.ini\");\n"
        "GetLeechcraftDifference(\"Patient\", $LeechResult);\n"
        "GetLeechcraftDifference(\"Patient\", getvar(\"ExistingLeechResult\"));\n"
        "ChooseEx(\"pick\", \"A{$Level >= 1}\", \"B\", $Choice);\n"
        "ChooseEx(\"again\", \"A\", \"B\", getvar(\"ExistingChoice\"));\n"
        "ChoosePlus(\"#name\", \"2\", \"0\", \"pick\", \"A\", \"B\", $PlusChoice);\n"
        "ChoosePlus(\"#name\", \"2\", \"0\", \"again\", \"A\", \"B\", getvar(\"ExistingPlusChoice\"));\n"
        "GetNpcState(\"Guard\", \"KindValue\", $KindValue);\n"
        "GetNpcState(\"Guard\", \"KindValueMax\", getvar(\"ExistingKindValueMax\"));\n"
        "AddKindValue(\"Guard\", -100);\n"
        "SetMapNpcAttr(\"Guard\", \"Kind:1;Relation:1;KindValue:3500\", \"map001.npc\");\n"
        "SetNpcTalkContent(\"Guard\", \"hello\", \"map001.npc\");\n"
        "TalkSelfTip(\"Guard\", \"value\", getvar(\"KindValue\"));\n"
        "SetAllNpcIsEnemy();\n"
        "ShowStealWin(\"Guard\", \"StealSuccess.txt\", \"StealFail.txt\");\n"
        "ShowSignalTip(\"Guard\", 23, \"t1\");\n"
        "SetSignalTipHidden(\"Guard\");\n"
        "AddTalent(\"player-talent-test.ini\");\n"
        "ShowGiveGoodsWin(\"桃木剑\", \"GiveSuccess.txt\", \"GiveFail.txt\");\n"
        "ClearAllSave;\n"
        "EnableSave();\n"
        "DisableSave();\n");

    bool hasUnsupportedApi = false;
    for (const ScriptConversionDiagnostic& diagnostic : converter.getDiagnostics())
    {
        if (diagnostic.category == "UnsupportedApi")
            hasUnsupportedApi = true;
    }

    return check(converted.find("getplayermagiclevel(\"player-magic-test.ini\",\"Level\")") != std::string::npos,
                 "GetPlayerMagicLevel output variable") &&
        check(converted.find("setnpcdestination(\"Guard\",-1,-1)") != std::string::npos,
              "SetNpcDestination legacy wildcard arguments") &&
        check(converted.find("setwalkisrun(1)") != std::string::npos,
              "SetWalkIsRun compatibility command") &&
        check(converted.find("showsystemmsg(\"saved\",6000)") != std::string::npos,
              "ShowSystemMsg compatibility command") &&
        check(converted.find("buygoodsonly(\"shop.ini\")") != std::string::npos,
              "BuyGoodsOnly compatibility command") &&
        check(converted.find("getleechcraftdifference(\"Patient\",\"LeechResult\")") != std::string::npos,
              "GetLeechcraftDifference output variable") &&
        check(converted.find("getleechcraftdifference(\"Patient\",\"ExistingLeechResult\")") != std::string::npos,
              "GetLeechcraftDifference converted getvar output variable") &&
        check(converted.find("chooseex(\"pick\",\"A{$Level >= 1}\",\"B\",\"Choice\")") != std::string::npos,
              "ChooseEx output variable") &&
        check(converted.find("chooseex(\"again\",\"A\",\"B\",\"ExistingChoice\")") != std::string::npos,
              "ChooseEx converted getvar output variable") &&
        check(converted.find("chooseplus(\"#name\",\"2\",\"0\",\"pick\",\"A\",\"B\",\"PlusChoice\")") != std::string::npos,
              "ChoosePlus output variable") &&
        check(converted.find("chooseplus(\"#name\",\"2\",\"0\",\"again\",\"A\",\"B\",\"ExistingPlusChoice\")") != std::string::npos,
              "ChoosePlus converted getvar output variable") &&
        check(converted.find("getnpcstate(\"Guard\",\"KindValue\",\"KindValue\")") != std::string::npos,
              "GetNpcState output variable") &&
        check(converted.find("getnpcstate(\"Guard\",\"KindValueMax\",\"ExistingKindValueMax\")") != std::string::npos,
              "GetNpcState converted getvar output variable") &&
        check(converted.find("addkindvalue(\"Guard\",-100)") != std::string::npos,
              "AddKindValue compatibility command") &&
        check(converted.find("setmapnpcattr(\"Guard\",\"Kind:1;Relation:1;KindValue:3500\",\"map001.npc\")") != std::string::npos,
              "SetMapNpcAttr compatibility command") &&
        check(converted.find("setnpctalkcontent(\"Guard\",\"hello\",\"map001.npc\")") != std::string::npos,
              "SetNpcTalkContent compatibility command") &&
        check(converted.find("talkselftip(\"Guard\",\"value\",getvar(\"KindValue\"))") != std::string::npos,
              "TalkSelfTip input variable remains getvar") &&
        check(converted.find("setallnpcisenemy();") != std::string::npos,
              "SetAllNpcIsEnemy compatibility command") &&
        check(converted.find("showstealwin(\"Guard\",\"StealSuccess.txt\",\"StealFail.txt\")") != std::string::npos,
              "ShowStealWin compatibility command") &&
        check(converted.find("showsignaltip(\"Guard\",23,\"t1\")") != std::string::npos,
              "ShowSignalTip compatibility command") &&
        check(converted.find("setsignaltiphidden(\"Guard\")") != std::string::npos,
              "SetSignalTipHidden compatibility command") &&
        check(converted.find("addtalent(\"player-talent-test.ini\")") != std::string::npos,
              "AddTalent compatibility command") &&
        check(converted.find("showgivegoodswin(\"桃木剑\",\"GiveSuccess.txt\",\"GiveFail.txt\")") != std::string::npos,
              "ShowGiveGoodsWin compatibility command") &&
        check(converted.find("clearallsave();") != std::string::npos,
              "ClearAllSave bare no-argument command") &&
        check(converted.find("enablesave();") != std::string::npos,
              "EnableSave compatibility command") &&
        check(converted.find("disablesave();") != std::string::npos,
              "DisableSave compatibility command") &&
        check(!hasUnsupportedApi, "MOD compatibility commands are supported runtime APIs");
}

bool testScriptBareNoArgumentCommand()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "SetPlayerScn;\n"
        "SetPlayerScn\n");

    return check(converted.find("setplayerscn();") != std::string::npos,
                 "bare no-argument command with semicolon") &&
        check(converted.find("setplayerscn();", converted.find("setplayerscn();") + 1) != std::string::npos,
              "bare no-argument command without semicolon") &&
        check(converter.getDiagnostics().empty(),
              "bare no-argument runtime command converts without diagnostics");
}

bool testScriptTrailingColonFunctionCall()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "Say(\"hello\",0):\n");

    return check(converted.find("say(\"hello\",0);") != std::string::npos,
                 "function call with trailing colon converts") &&
        check(converter.getDiagnostics().empty(),
              "function call with trailing colon converts without diagnostics");
}

bool testMoonlightScriptCompatibility()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "Say(\"hello\",2_;\n"
        "If ($SubEvent25 == 10) Sub25Check;\n"
        "Return();\n"
        "@AfterReturn\n"
        "Say(\"bye\",0);\n"
        "SetNpcDir(\"张胜伟\"1);\n"
        "DelNpc(爆炸);\n"
        "Say(\"学得一招\"沧海月明珠有泪\"。\",);\n"
        "If ($Flag == 1) goto @Missing;\n"
        "@Duplicate\n"
        "Say(\"first\",0);\n"
        "@Duplicate\n"
        "Say(\"second\",0);\n");

    return check(converted.find("say(\"hello\",2);") != std::string::npos,
                 "repair Moonlight Say numeric argument typo") &&
        check(converted.find("if getvar(\"SubEvent25\") == 10 then goto Sub25Check end") != std::string::npos,
              "convert bare identifier if action to goto") &&
        check(converted.find("goto __jx_script_return") != std::string::npos,
              "convert Return() to Lua return jump") &&
        check(converted.find("::__jx_script_return::") != std::string::npos &&
                  converted.rfind("return;") > converted.find("::__jx_script_return::"),
              "append Lua return at script end") &&
        check(converted.find("::AfterReturn::") != std::string::npos,
              "labels after Return remain parseable") &&
        check(converted.find("setnpcdir(\"张胜伟\",1);") != std::string::npos,
              "repair missing comma after quoted string argument") &&
        check(converted.find("delnpc(\"爆炸\");") != std::string::npos,
              "quote bare non-ASCII function argument") &&
        check(converted.find("say(\"学得一招\\\"沧海月明珠有泪\\\"。\")") != std::string::npos,
              "escape interior quotes and drop empty trailing argument") &&
        check(converted.find("::Missing::") != std::string::npos,
              "append missing goto label") &&
        check(converted.find("::Duplicate__dup2::") != std::string::npos,
              "deduplicate repeated labels") &&
        check(converter.getDiagnostics().empty(),
              "Moonlight compatibility script converts without diagnostics");
}

bool testNewJxMultilineScriptCompatibility()
{
    ScriptConverter converter;
    std::string converted = converter.convertScript(
        "Say(\"文龙我儿：\n"
        "金兵怒犯，战事日危。\n"
        "父登绝笔\");\n"
        "Reurn;\n");

    LuaScriptSyntaxIssue syntaxIssue = LuaScriptSyntaxValidator::validateScriptContent(
        "newjx-multiline.txt", converted);
    std::string accidentallyValidGbk = {
        static_cast<char>(0xD2), static_cast<char>(0xA9),
        static_cast<char>(0xC6), static_cast<char>(0xB7)
    };
    const bool detectedGbk = ScriptConverter::detectAndConvertEncoding(accidentallyValidGbk);
    return check(converted.find(
                     "say(\"文龙我儿：\\n金兵怒犯，战事日危。\\n父登绝笔\");") !=
                     std::string::npos,
                 "join a production multiline Say string with explicit Lua newlines") &&
        check(converted.find("goto __jx_script_return") != std::string::npos,
              "repair the production Reurn spelling as Return") &&
        check(converter.getDiagnostics().empty(),
              "New JX multiline and Reurn compatibility converts without diagnostics") &&
        check(syntaxIssue.message.isEmpty(),
              "New JX multiline conversion remains valid Lua") &&
        check(detectedGbk && accidentallyValidGbk == QString::fromUtf8("药品").toUtf8().toStdString(),
              "script editor encoding detection repairs accidentally-valid GBK text");
}

bool testLuaScriptSyntaxValidator()
{
    LuaScriptSyntaxIssue okIssue = LuaScriptSyntaxValidator::validateScriptContent(
        "valid-script.txt",
        "if getvar(\"Flag\") == 1 then goto Done end\n"
        "::Done::\n"
        "return;\n");

    LuaScriptSyntaxIssue badIssue = LuaScriptSyntaxValidator::validateScriptContent(
        "bad-script.txt",
        "::Again::\n"
        "::Again::\n"
        "return;\n");
    const std::string invalidUtf8 = {
        static_cast<char>(0xC0), static_cast<char>(0xAF)
    };
    LuaScriptSyntaxIssue encodingIssue = LuaScriptSyntaxValidator::validateScriptContent(
        "invalid-encoding.txt", invalidUtf8);

    QTemporaryDir assetsDir;
    if (!check(assetsDir.isValid(), "create Lua syntax validator assets fixture"))
        return false;

    QDir root(assetsDir.path());
    if (!check(root.mkpath(QString::fromUtf8("script/未找到")) && root.mkpath("script/common"),
               "create Lua syntax validator script folders"))
    {
        return false;
    }

    auto writeFixture = [](const QString& filePath, const QByteArray& content) {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(content) == content.size();
    };

    QString orphanScriptPath = root.filePath(QString::fromUtf8("script/未找到/真实脚本.txt"));
    QString talkIndexPath = root.filePath("script/common/talkindex.txt");
    QString iniLikePath = root.filePath("script/common/data.txt");
    if (!check(writeFixture(orphanScriptPath, "return;\n") &&
                   writeFixture(talkIndexPath, "[1,0]hello\n") &&
                   writeFixture(iniLikePath, "[config]\nvalue=1\n"),
               "write Lua syntax validator script fixtures"))
    {
        return false;
    }

    LuaScriptSyntaxReport report = LuaScriptSyntaxValidator::validateAssetsScripts(assetsDir.path());

    return check(okIssue.message.isEmpty(), "valid Lua script syntax passes") &&
        check(!badIssue.message.isEmpty(), "invalid Lua script syntax fails") &&
        check(encodingIssue.message.contains("UTF-8"),
              "Lua syntax validator rejects invalid UTF-8 before parsing") &&
        check(badIssue.lineNumber > 0, "Lua syntax validator extracts error line") &&
        check(LuaScriptSyntaxValidator::shouldValidateScriptFile(assetsDir.path(), orphanScriptPath, "return;\n"),
              "script/未找到 real script is validated") &&
        check(!LuaScriptSyntaxValidator::shouldValidateScriptFile(assetsDir.path(), talkIndexPath, "[1,0]hello\n"),
              "TalkIndex text is skipped as generated dialogue data") &&
        check(!LuaScriptSyntaxValidator::shouldValidateScriptFile(assetsDir.path(), iniLikePath, "[config]\nvalue=1\n"),
              "INI-like script text is skipped") &&
        check(report.totalFiles == 3, "Lua validator counts script directory files") &&
        check(report.checkedFiles == 1, "Lua validator checks only real scripts") &&
        check(report.skippedFiles == 2, "Lua validator skips data text files") &&
        check(report.failedFiles == 0, "Lua validator fixture has no syntax failures");
}

bool testLuaScriptSyntaxValidatorSkipsLegacyDialogueAndDocs()
{
    QTemporaryDir assetsDir;
    if (!check(assetsDir.isValid(), "create Lua syntax validator legacy skip fixture"))
        return false;

    QDir root(assetsDir.path());
    if (!check(root.mkpath("script/map/test"), "create Lua syntax validator legacy map folder"))
        return false;

    auto writeFixture = [](const QString& filePath, const QByteArray& content) {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(content) == content.size();
    };

    QString mapTalkPath = root.filePath("script/map/test/talk.txt");
    QString helpPath = root.filePath(QString::fromUtf8("script/help编写脚本文件.txt"));
    QString errorSummaryPath = root.filePath(QString::fromUtf8("script/script错误汇总.txt"));
    if (!check(writeFixture(mapTalkPath, "[part]\n1=dialogue\n") &&
                   writeFixture(helpPath, "// documentation\nnot lua\n") &&
                   writeFixture(errorSummaryPath, "script\\map\\bad.txt notes\n"),
               "write Lua syntax validator legacy skip fixtures"))
    {
        return false;
    }

    LuaScriptSyntaxReport report = LuaScriptSyntaxValidator::validateAssetsScripts(assetsDir.path());

    return check(!LuaScriptSyntaxValidator::shouldValidateScriptFile(assetsDir.path(), mapTalkPath, "[part]\n1=dialogue\n"),
                 "legacy map talk text is skipped as dialogue data") &&
        check(!LuaScriptSyntaxValidator::shouldValidateScriptFile(assetsDir.path(), helpPath, "// documentation\nnot lua\n"),
              "legacy script help text is skipped as documentation") &&
        check(!LuaScriptSyntaxValidator::shouldValidateScriptFile(assetsDir.path(), errorSummaryPath, "script\\map\\bad.txt notes\n"),
              "legacy script error summary text is skipped as documentation") &&
        check(report.totalFiles == 3, "Lua validator counts legacy skip fixture files") &&
        check(report.checkedFiles == 0, "Lua validator does not check legacy data/docs") &&
        check(report.skippedFiles == 3, "Lua validator skips legacy data/docs") &&
        check(report.failedFiles == 0, "Lua validator legacy skip fixture has no failures");
}

bool testMigrationPreservesLegacyScriptDocumentation()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(),
               "create script documentation migration temp dirs"))
    {
        return false;
    }

    QDir source(sourceDir.path());
    const QString relativePath = QString::fromUtf8("script/Help编写脚本文件.txt");
    const QString sourcePath = source.filePath(relativePath);
    if (!check(source.mkpath("script") &&
               writeUtf8TextFile(sourcePath,
                   "{\"If\",FuncIf},\n功能: 这是脚本 API 帮助，不是可执行脚本。\n"),
               "write legacy script documentation fixture"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.resourceTypes = {AssetResourceType::Scripts};
    options.convertScript = true;
    options.sourceEncoding = "utf8";
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(
        sourceDir.path(), outputDir.path(), options, report);
    const QString migrated = readUtf8TextFile(QDir(outputDir.path()).filePath(relativePath));

    return check(result == MigrationResult::Success,
                 "legacy script documentation migration succeeds without warnings") &&
        check(report.warningCount == 0 && report.unhandledScriptStatements.isEmpty(),
              "legacy script documentation does not produce conversion diagnostics") &&
        check(migrated.contains("FuncIf") && migrated.contains(QString::fromUtf8("脚本 API 帮助")) &&
                  !migrated.contains("TODO(jx-script-converter)"),
              "legacy script documentation is transcoded but not rewritten as Lua");
}

bool testMigrationSkipsLegacySourceControlMetadata()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(), "create migration metadata temp dirs"))
        return false;

    QDir source(sourceDir.path());
    if (!check(source.mkpath("script") && source.mkpath("ini") && source.mkpath("map") && source.mkpath("asf"),
               "create minimal legacy asset layout"))
    {
        return false;
    }

    QFile metadata(source.filePath("asf/vssver.scc"));
    if (!check(metadata.open(QIODevice::WriteOnly), "write legacy source control metadata fixture"))
        return false;
    QByteArray payload;
    payload.append(char(0xff));
    payload.append(char(0xfe));
    payload.append("legacy source control metadata", 30);
    metadata.write(payload);
    metadata.close();

    AssetMigrationOptions options;
    options.convertScript = false;

    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);

    return check(result == MigrationResult::Success, "migration with only ignored metadata succeeds") &&
        check(!QFileInfo::exists(QDir(outputDir.path()).filePath("asf/vssver.scc")),
              "legacy source control metadata is not written to migration output") &&
        check(report.processedFiles == 0, "ignored metadata is not counted as processed asset");
}

bool testMigrationSkipsLegacyNonRuntimeFiles()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(), "create migration non-runtime temp dirs"))
        return false;

    QDir source(sourceDir.path());
    if (!check(source.mkpath("save/rpg0") && source.mkpath("Content/sound") &&
            source.mkpath("data") && source.mkpath("resource/editor") &&
            source.mkpath("Optional_Patch") && source.mkpath("font") &&
            source.mkpath("Mpc/UI") &&
            source.mkpath("ini/save") && source.mkpath("map") &&
            source.mkpath("script/common"),
            "create non-runtime filtering fixture layout"))
    {
        return false;
    }

    bool ok = true;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg0/game.ini"), "[State]\nMap=runtime\n"),
               "write root save fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("Content/sound/voice.xnb"), "xnb\n"),
               "write XNB sound fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("Jxqy.exe"), "exe\n"),
               "write exe fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("Engine.dll"), "dll\n"),
               "write dll fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("debug.txt"), "debug\n"),
               "write root debug log fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("readme.txt"), "readme\n"),
               "write root readme fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("Optional_Patch/Fix.TXT"), "patch\n"),
               "write optional patch fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("optional.zip"), "zip\n"),
               "write zip fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("rescue.rar"), "rar\n"),
               "write rar fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("data/font.dat"), "font\n"),
               "write legacy engine data fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("resource/editor/helper.json"), "{}\n"),
               "write editor resource fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("talkindex.txt"), "talk index\n"),
               "write root talk index fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("partneridx.ini"), "[partner]\n"),
               "write root partner index fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("font/font.dat"), "runtime font\n"),
               "write runtime font fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("Mpc/UI/Mouse.MPC"), "mpc\n"),
               "write uppercase runtime root fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("map/Other.map"), "not a map\n"),
               "write invalid map placeholder fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("migration_report.txt"), "report\n"),
               "write migration report fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath(".jxqy_asset_migration_marker"), "marker\n"),
               "write migration marker fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/save/game.ini"), "[State]\nMap=template\n"),
               "write ini save template fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("script/common/newgame.txt"), "loadgame(0);\n"),
               "write script fixture") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;

    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);
    QDir output(outputDir.path());

    ok = check(result != MigrationResult::Failed, "migration with non-runtime files succeeds") && ok;
    ok = check(report.errorCount == 0, "migration invalid map placeholders are not errors") && ok;
    ok = check(report.warningCount > 0 &&
            report.logLines.join('\n').contains("skipped non-MAP placeholder file"),
               "migration reports skipped invalid map placeholders") && ok;
    ok = check(!QFileInfo::exists(output.filePath("save/rpg0/game.ini")),
               "migration skips root runtime save directory") && ok;
    ok = check(!QFileInfo::exists(output.filePath("Content/sound/voice.xnb")),
               "migration skips C# Content payload") && ok;
    ok = check(!QFileInfo::exists(output.filePath("sound/voice.wav")),
               "migration skips XNA Content sound by default") && ok;
    ok = check(!QFileInfo::exists(output.filePath("Jxqy.exe")), "migration skips exe files") && ok;
    ok = check(!QFileInfo::exists(output.filePath("Engine.dll")), "migration skips dll files") && ok;
    ok = check(readRawFile(output.filePath("debug.txt")) ==
                   readRawFile(source.filePath("debug.txt")),
               "migration byte-preserves an unrecognized root file") && ok;
    ok = check(readRawFile(output.filePath("readme.txt")) ==
                   readRawFile(source.filePath("readme.txt")),
               "migration byte-preserves root documentation as custom content") && ok;
    ok = check(readRawFile(output.filePath("optional_patch/fix.txt")) ==
                   readRawFile(source.filePath("Optional_Patch/Fix.TXT")),
               "migration lowercases and byte-preserves an unrecognized custom path") && ok;
    ok = check(!QFileInfo::exists(output.filePath("optional.zip")), "migration skips zip attachments") && ok;
    ok = check(!QFileInfo::exists(output.filePath("rescue.rar")), "migration skips rar attachments") && ok;
    ok = check(readRawFile(output.filePath("data/font.dat")) ==
                   readRawFile(source.filePath("data/font.dat")),
               "migration preserves unrecognized legacy data bytes") && ok;
    ok = check(readRawFile(output.filePath("resource/editor/helper.json")) ==
                   readRawFile(source.filePath("resource/editor/helper.json")),
               "migration preserves unrecognized editor resource bytes") && ok;
    ok = check(!QFileInfo::exists(output.filePath("map/Other.map")),
               "migration skips invalid map placeholder output") && ok;
    ok = check(readUtf8TextFile(output.filePath(".jxqy_asset_migration_marker")) != "marker\n",
               "migration writes its own marker instead of copying source marker") && ok;
    ok = check(QFileInfo::exists(output.filePath("ini/save/game.ini")),
               "migration keeps ini/save initial template resources") && ok;
    ok = check(QFileInfo::exists(output.filePath("script/common/newgame.txt")),
               "migration keeps script resources") && ok;
    ok = check(QFileInfo::exists(output.filePath("talkindex.txt")),
               "migration keeps root talk index") && ok;
    ok = check(QFileInfo::exists(output.filePath("partneridx.ini")),
               "migration keeps root partner index") && ok;
    ok = check(QFileInfo::exists(output.filePath("font/font.dat")),
               "migration keeps runtime font directory") && ok;
    QStringList outputRootEntries = output.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    ok = check(QFileInfo::exists(output.filePath("mpc/ui/mouse.mpc")) &&
            outputRootEntries.contains("mpc") && !outputRootEntries.contains("Mpc"),
               "migration lowercases every runtime directory and file path component") && ok;
    QSet<QString> scannedSourcePaths;
    QDirIterator sourceEntries(
        sourceDir.path(),
        QDir::AllEntries |
            QDir::NoDotAndDotDot |
            QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    while (sourceEntries.hasNext())
    {
        scannedSourcePaths.insert(
            QDir(sourceDir.path()).relativeFilePath(
                sourceEntries.next()).replace('\\', '/'));
    }
    QSet<QString> reportedSourcePaths;
    int sourceOutcomeCount = 0;
    for (const AssetMigrationFileOutcome& outcome :
         report.fileOutcomes)
    {
        if (!outcome.sourceScan)
            continue;
        sourceOutcomeCount++;
        reportedSourcePaths.insert(outcome.sourcePath);
    }
    ok = check(
        sourceOutcomeCount == scannedSourcePaths.size() &&
            reportedSourcePaths == scannedSourcePaths,
        "migration records exactly one result for every scanned source file and directory entry") &&
        ok;
    return ok;
}

bool testMigrationMapsLegacyNewGameSaveTemplate()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(), "create legacy save template temp dirs"))
        return false;

    QDir source(sourceDir.path());
    if (!check(source.mkpath("save/rpg0") && source.mkpath("save/rpg1") &&
            source.mkpath("script/common"),
            "create legacy save template fixture layout"))
    {
        return false;
    }

    bool ok = true;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg0/Game.ini"), "[State]\nMap=template\n"),
               "write legacy new game Game.ini fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg0/start.npc"), "[1]\nName=npc\n"),
               "write legacy new game npc fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg1/game.ini"), "[State]\nMap=user\n"),
               "write user save fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("script/common/newgame.txt"), "loadgame(0);\n"),
               "write script fixture") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;

    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);
    QDir output(outputDir.path());
    const auto saveDirectoryOutcome = std::find_if(
        report.fileOutcomes.cbegin(),
        report.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return outcome.sourceScan &&
                outcome.sourcePath ==
                    QStringLiteral("save/rpg0") &&
                outcome.entryType ==
                    QStringLiteral("directory") &&
                outcome.action ==
                    AssetMigrationFileAction::Skip &&
                outcome.reason ==
                    QStringLiteral(
                        "known-non-runtime-directory");
        });
    const auto saveTemplateOutcome = std::find_if(
        report.fileOutcomes.cbegin(),
        report.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return outcome.sourceScan &&
                outcome.sourcePath ==
                    QStringLiteral("save/rpg0/Game.ini") &&
                outcome.outputPath ==
                    QStringLiteral("ini/save/game.ini") &&
                outcome.action ==
                    AssetMigrationFileAction::Convert;
        });

    ok = check(result != MigrationResult::Failed, "migration with legacy save template succeeds") && ok;
    ok = check(QFileInfo::exists(output.filePath("ini/save/game.ini")),
               "migration maps save/rpg0 Game.ini to ini/save/game.ini") && ok;
    ok = check(QFileInfo::exists(output.filePath("ini/save/start.npc")),
               "migration maps save/rpg0 npc templates to ini/save") && ok;
    ok = check(!QFileInfo::exists(output.filePath("save/rpg0/Game.ini")),
               "migration does not output root new-game save directory") && ok;
    ok = check(!QFileInfo::exists(output.filePath("save/rpg1/game.ini")),
               "migration skips non-template user save slots") && ok;
    ok = check(
        saveDirectoryOutcome !=
                report.fileOutcomes.cend() &&
            saveTemplateOutcome !=
                report.fileOutcomes.cend(),
        "save/rpg0 directory is reported as skipped while its files still map into ini/save") &&
        ok;
    return ok;
}

bool testMigrationOverlaysLegacyNewGameSaveTemplate()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(), "create legacy save overlay temp dirs"))
        return false;

    QDir source(sourceDir.path());
    if (!check(source.mkpath("ini/save") && source.mkpath("save/rpg0") &&
            source.mkpath("script/common"),
            "create legacy save overlay fixture layout"))
    {
        return false;
    }

    bool ok = true;
    ok = check(writeUtf8TextFile(source.filePath("ini/save/Game.ini"), "[State]\nMap=ini-template\n"),
               "write canonical Game.ini overlay fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/save/shared.npc"), "[1]\nName=ini-template\n"),
               "write canonical shared npc overlay fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/save/ini-only.npc"), "[1]\nName=ini-only\n"),
               "write canonical-only npc overlay fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg0/game.ini"), "[State]\nMap=rpg0-template\n"),
               "write rpg0 Game.ini overlay fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg0/shared.npc"), "[1]\nName=rpg0-template\n"),
               "write rpg0 shared npc overlay fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("save/rpg0/rpg-only.obj"), "[1]\nName=rpg-only\n"),
               "write rpg0-only object overlay fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("script/common/newgame.txt"), "loadgame(0);\n"),
               "write save overlay script fixture") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;

    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);
    QDir output(outputDir.path());

    ok = check(result != MigrationResult::Failed, "migration with legacy save overlay succeeds") && ok;
    ok = check(readUtf8TextFile(output.filePath("ini/save/game.ini")) ==
            "[State]\nMap=rpg0-template\n",
               "rpg0 Game.ini overrides canonical template case-insensitively") && ok;
    ok = check(readUtf8TextFile(output.filePath("ini/save/shared.npc")) ==
            "[1]\nName=rpg0-template\n",
               "rpg0 shared template overrides canonical template") && ok;
    ok = check(readUtf8TextFile(output.filePath("ini/save/ini-only.npc")) ==
            "[1]\nName=ini-only\n",
               "canonical-only template survives rpg0 overlay") && ok;
    ok = check(readUtf8TextFile(output.filePath("ini/save/rpg-only.obj")) ==
            "[1]\nName=rpg-only\n",
               "rpg0-only template is mapped into canonical template directory") && ok;
    ok = check(!QFileInfo::exists(output.filePath("save/rpg0/game.ini")),
               "legacy save overlay does not output writable save directory") && ok;
    return ok;
}

bool testMigrationRejectsUnsafeOutputPaths()
{
    auto configureOptions = []() {
        AssetMigrationOptions options;
        options.convertScript = false;
        return options;
    };

    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "create migration path safety temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourcePath = root.filePath("source");
    if (!check(root.mkpath("source"), "create migration safety source"))
        return false;

    JxAssetMigrator migrator;
    AssetMigrationReport report;
    bool ok = true;

    ok = check(migrator.migrate(sourcePath, sourcePath, configureOptions(), report) == MigrationResult::Failed,
               "migration rejects identical source and output directories") && ok;

    QString nestedOutput = root.filePath("source/output");
    ok = check(migrator.migrate(sourcePath, nestedOutput, configureOptions(), report) == MigrationResult::Failed,
               "migration rejects output directory inside source") && ok;

    QString outerOutput = root.filePath("outer");
    QString sourceInsideOutput = root.filePath("outer/source");
    if (!check(root.mkpath("outer/source"), "create source inside output fixture"))
        return false;
    ok = check(migrator.migrate(sourceInsideOutput, outerOutput, configureOptions(), report) == MigrationResult::Failed,
               "migration rejects source directory inside output") && ok;

    QString nonMigrationOutput = root.filePath("non-migration-output");
    if (!check(root.mkpath("non-migration-output"), "create non-migration output fixture"))
        return false;
    QFile unrelated(root.filePath("non-migration-output/keep.txt"));
    if (!check(unrelated.open(QIODevice::WriteOnly), "write non-migration output marker"))
        return false;
    unrelated.write("keep");
    unrelated.close();

    ok = check(migrator.migrate(sourcePath, nonMigrationOutput, configureOptions(), report) == MigrationResult::Failed,
               "migration rejects non-empty output directory without migration report") && ok;
    ok = check(QFileInfo::exists(root.filePath("non-migration-output/keep.txt")),
               "rejected non-migration output directory is left intact") && ok;

    QString previousMigrationOutput = root.filePath("previous-migration-output");
    if (!check(root.mkpath("previous-migration-output"), "create previous migration output fixture"))
        return false;
    QFile previousReport(root.filePath("previous-migration-output/migration_report.txt"));
    if (!check(previousReport.open(QIODevice::WriteOnly), "write previous migration marker"))
        return false;
    previousReport.write("previous");
    previousReport.close();

    ok = check(migrator.migrate(sourcePath, previousMigrationOutput, configureOptions(), report) == MigrationResult::Success,
               "migration allows non-empty previous migration output directory") && ok;
    ok = check(QFileInfo::exists(root.filePath("previous-migration-output/migration_report.txt")),
               "previous migration output receives new report") && ok;

    QString interruptedMigrationOutput = root.filePath("interrupted-migration-output");
    if (!check(root.mkpath("interrupted-migration-output"), "create interrupted migration output fixture"))
        return false;
    QFile marker(root.filePath("interrupted-migration-output/.jxqy_asset_migration_marker"));
    if (!check(marker.open(QIODevice::WriteOnly), "write migration output marker"))
        return false;
    marker.write("marker");
    marker.close();
    QFile stale(root.filePath("interrupted-migration-output/stale.txt"));
    if (!check(stale.open(QIODevice::WriteOnly), "write interrupted migration stale file"))
        return false;
    stale.write("stale");
    stale.close();

    ok = check(migrator.migrate(sourcePath, interruptedMigrationOutput, configureOptions(), report) == MigrationResult::Success,
               "migration allows marked interrupted migration output without report") && ok;
    ok = check(readRawFile(
                   root.filePath(
                       "interrupted-migration-output/stale.txt")) ==
                   QByteArray("stale"),
               "interrupted migration preserves unowned player files before replacing the output root") && ok;
    ok = check(QFileInfo::exists(root.filePath("interrupted-migration-output/.jxqy_asset_migration_marker")),
               "migration output marker is recreated") && ok;

    return ok;
}

bool testStrictUtf8ValidationAndEmptyImpSaveSafety()
{
    const uint8_t validUtf8[] = {0xE6, 0xB5, 0x8B, 0xE8, 0xAF, 0x95};
    const uint8_t overlongUtf8[] = {0xC0, 0xAF};
    const uint8_t invalidAfterNull[] = {'a', 0x00, 0xC0, 0xAF};

    QTemporaryDir directory;
    if (!check(directory.isValid(), "create atomic IMP save temp directory"))
        return false;
    const QString targetPath = QDir(directory.path()).filePath("existing.img");
    if (!check(writeRawFile(targetPath, QByteArray("keep")),
               "write existing IMP save fixture"))
    {
        return false;
    }

    IMPImageFile emptyImage;
    const bool rejected = !emptyImage.save(targetPath.toUtf8().toStdString());
    QFile target(targetPath);
    const QByteArray preserved = target.open(QIODevice::ReadOnly)
        ? target.readAll()
        : QByteArray();

    bool ok = true;
    ok = check(Util::isUtf8(validUtf8, sizeof(validUtf8)),
               "strict UTF-8 validator accepts valid Chinese text") && ok;
    ok = check(!Util::isUtf8(overlongUtf8, sizeof(overlongUtf8)),
               "strict UTF-8 validator rejects overlong sequences") && ok;
    ok = check(!Util::isUtf8(invalidAfterNull, sizeof(invalidAfterNull)),
               "strict UTF-8 validator checks bytes after NUL") && ok;
    ok = check(rejected && preserved == "keep",
               "invalid empty IMP save leaves the existing target intact") && ok;
    return ok;
}

bool testMigrationPublishesTransactionally()
{
    QTemporaryDir rootDirectory;
    if (!check(rootDirectory.isValid(), "create transactional migration temp root"))
        return false;
    QDir root(rootDirectory.path());
    const QString sourceRoot = root.filePath("source");
    const QString outputRoot = root.filePath("output");
    if (!check(root.mkpath("source/mpc/character") && root.mkpath("output"),
               "create transactional migration directories"))
    {
        return false;
    }

    const QByteArray badMpc("MPC File Ver2.0", 16);
    if (!check(writeRawFile(root.filePath("source/mpc/character/bad.mpc"), badMpc) &&
               writeRawFile(root.filePath("output/.jxqy_asset_migration_marker"), "old marker") &&
               writeRawFile(root.filePath("output/stable.txt"), "stable"),
               "write transactional migration fixtures"))
    {
        return false;
    }

    AssetMigrationOptions failingOptions;
    failingOptions.convertScript = false;
    failingOptions.legacyImages.setMode(
        LegacyImageCategory::Character, LegacyImageMode::Convert);
    failingOptions.writeModProfile = false;
    AssetMigrationReport failureReport;
    JxAssetMigrator migrator;
    MigrationResult failureResult = migrator.migrate(
        sourceRoot, outputRoot, failingOptions, failureReport);

    QFile stableFile(root.filePath("output/stable.txt"));
    const QByteArray stableBytes = stableFile.open(QIODevice::ReadOnly)
        ? stableFile.readAll()
        : QByteArray();
    const QString normalizedOutputPrefix = QDir::cleanPath(outputRoot) + "/";

    bool ok = true;
    ok = check(failureResult == MigrationResult::Failed &&
                   failureReport.errorCount > 0 &&
                   failureReport.failedImages == 1,
               "bad legacy image is reported as a failed image") && ok;
    ok = check(stableBytes == "stable" &&
               !QFileInfo::exists(root.filePath("output/mpc/character/bad.mpc")),
               "failed full migration preserves the previous published output") && ok;
    ok = check(QFileInfo::exists(failureReport.reportFilePath) &&
               !QDir::fromNativeSeparators(failureReport.reportFilePath)
                    .startsWith(QDir::fromNativeSeparators(normalizedOutputPrefix)),
               "failed migration keeps its report in an unpublished staging directory") && ok;

    const QString cancelledOutput = root.filePath("cancelled-output");
    root.mkpath("cancelled-output");
    writeRawFile(root.filePath("cancelled-output/.jxqy_asset_migration_marker"), "old marker");
    writeRawFile(root.filePath("cancelled-output/stable.txt"), "stable-cancel");
    AssetMigrationOptions cancelledOptions;
    cancelledOptions.convertScript = false;
    cancelledOptions.writeModProfile = false;
    AssetMigrationReport cancelledReport;
    MigrationResult cancelledResult = migrator.migrate(
        sourceRoot, cancelledOutput, cancelledOptions, cancelledReport,
        JxAssetMigrator::LogCallback(), JxAssetMigrator::ProgressCallback(),
        []() { return true; });
    ok = check(cancelledResult == MigrationResult::Failed && cancelledReport.cancelled,
               "cancelled migration records an explicit cancelled result") && ok;
    ok = check(readUtf8TextFile(root.filePath("cancelled-output/stable.txt")) == "stable-cancel",
               "cancelled full migration preserves the previous output") && ok;
    ok = check(QFileInfo::exists(cancelledReport.reportJsonFilePath) &&
               readUtf8TextFile(cancelledReport.reportJsonFilePath).contains("\"cancelled\": true"),
               "cancelled migration JSON report records cancellation") && ok;

    const QString scriptsOutput = root.filePath("scripts-output");
    root.mkpath("source/script");
    root.mkpath("scripts-output/script");
    writeRawFile(root.filePath("source/script/new.txt"), "Print(1)\n");
    writeRawFile(root.filePath("scripts-output/script/keep.txt"), "old-script");
    AssetMigrationOptions scriptsOptions;
    scriptsOptions.resourceTypes = {AssetResourceType::Scripts};
    scriptsOptions.convertScript = false;
    AssetMigrationReport scriptsReport;
    MigrationResult scriptsResult = migrator.migrate(
        sourceRoot, scriptsOutput, scriptsOptions, scriptsReport,
        JxAssetMigrator::LogCallback(), JxAssetMigrator::ProgressCallback(),
        []() { return true; });
    ok = check(scriptsResult == MigrationResult::Failed && scriptsReport.cancelled &&
               readUtf8TextFile(root.filePath("scripts-output/script/keep.txt")) == "old-script",
               "cancelled scripts-only migration preserves the published script subtree") && ok;

    const QString imagesOutput = root.filePath("images-output");
    root.mkpath("images-output/mpc/character");
    writeRawFile(root.filePath("images-output/mpc/character/keep.txt"), "old-images");
    AssetMigrationOptions imagesOptions;
    imagesOptions.resourceTypes = {AssetResourceType::Images};
    imagesOptions.includePrefix = "mpc/character";
    imagesOptions.legacyImages.setMode(
        LegacyImageCategory::Character, LegacyImageMode::Convert);
    imagesOptions.writeModProfile = false;
    AssetMigrationReport imagesReport;
    MigrationResult imagesResult = migrator.migrate(
        sourceRoot, imagesOutput, imagesOptions, imagesReport);
    ok = check(imagesResult == MigrationResult::Failed &&
               readUtf8TextFile(root.filePath("images-output/mpc/character/keep.txt")) == "old-images",
               "failed images-only migration preserves the published image subtree") && ok;

    const QString missingImagesOutput = root.filePath("missing-images-output");
    root.mkpath("missing-images-output/mpc/missing");
    writeRawFile(root.filePath("missing-images-output/mpc/missing/keep.txt"), "old-missing-images");
    AssetMigrationOptions missingImagesOptions = imagesOptions;
    missingImagesOptions.includePrefix = "mpc/missing";
    AssetMigrationReport missingImagesReport;
    MigrationResult missingImagesResult = migrator.migrate(
        sourceRoot, missingImagesOutput, missingImagesOptions, missingImagesReport);
    ok = check(missingImagesResult == MigrationResult::Failed &&
               readUtf8TextFile(root.filePath("missing-images-output/mpc/missing/keep.txt")) ==
                   "old-missing-images",
               "empty images-only match preserves the published image subtree") && ok;

    AssetMigrationOptions unsafeImagesOptions = imagesOptions;
    unsafeImagesOptions.includePrefix = "../outside";
    AssetMigrationReport unsafeImagesReport;
    MigrationResult unsafeImagesResult = migrator.migrate(
        sourceRoot, imagesOutput, unsafeImagesOptions, unsafeImagesReport);
    ok = check(unsafeImagesResult == MigrationResult::Failed &&
               readUtf8TextFile(root.filePath("images-output/mpc/character/keep.txt")) == "old-images" &&
               !QFileInfo::exists(root.filePath("outside")),
               "images-only migration rejects a parent-directory include prefix") && ok;

    const QString multiDomainOutput = root.filePath("multi-domain-output");
    root.mkpath("multi-domain-output/script");
    root.mkpath("multi-domain-output/mpc/character");
    writeRawFile(root.filePath("multi-domain-output/script/keep.txt"), "old-script-domain");
    writeRawFile(root.filePath("multi-domain-output/mpc/character/keep.txt"), "old-image-domain");
    AssetMigrationOptions multiDomainOptions;
    multiDomainOptions.resourceTypes = {
        AssetResourceType::Scripts,
        AssetResourceType::Images
    };
    multiDomainOptions.convertScript = false;
    multiDomainOptions.writeModProfile = false;
    multiDomainOptions.legacyImages.setMode(
        LegacyImageCategory::Character, LegacyImageMode::Convert);
    AssetMigrationReport multiDomainReport;
    const MigrationResult multiDomainResult = migrator.migrate(
        sourceRoot, multiDomainOutput, multiDomainOptions, multiDomainReport);
    ok = check(multiDomainResult == MigrationResult::Failed &&
                   readUtf8TextFile(root.filePath(
                       "multi-domain-output/script/keep.txt")) == "old-script-domain" &&
                   readUtf8TextFile(root.filePath(
                       "multi-domain-output/mpc/character/keep.txt")) == "old-image-domain" &&
                   !QFileInfo::exists(root.filePath(
                       "multi-domain-output/script/new.txt")),
               "failed multi-domain migration preserves every selected published domain") && ok;
    return ok;
}

bool testMigrationPreservesExistingPlayerFilesAndBlocksPathConflicts()
{
    QTemporaryDir directory;
    if (!check(
            directory.isValid(),
            "create existing-output preservation fixture"))
    {
        return false;
    }

    QDir root(directory.path());
    const QString sourceRoot =
        root.filePath("source");
    const QString outputRoot =
        root.filePath("output");
    bool ok = check(
        root.mkpath("source/sound") &&
            root.mkpath(
                "source/custom/source-empty") &&
            root.mkpath(
                "output/custom/player-empty") &&
            writeRawFile(
                root.filePath("source/sound/new.bin"),
                QByteArray("new-runtime")) &&
            writeRawFile(
                root.filePath(
                    "output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath("output/custom/player.bin"),
                QByteArray("player-owned")),
        "write existing-output preservation fixtures");
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;
    options.writeModProfile = false;
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result = migrator.migrate(
        sourceRoot,
        outputRoot,
        options,
        report);
    const auto preservedOutcome = std::find_if(
        report.fileOutcomes.cbegin(),
        report.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return !outcome.sourceScan &&
                outcome.sourcePath ==
                    QStringLiteral("custom/player.bin") &&
                outcome.action ==
                    AssetMigrationFileAction::Copy &&
                outcome.reason ==
                    QStringLiteral(
                        "preserve-existing-player-file");
        });
    const QString preservedPlayerSha256 =
        QString::fromLatin1(
            QCryptographicHash::hash(
                QByteArray("player-owned"),
                QCryptographicHash::Sha256).
                toHex());
    const auto sourceEmptyDirectoryOutcome =
        std::find_if(
            report.fileOutcomes.cbegin(),
            report.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "custom/source-empty") &&
                    outcome.entryType ==
                        QStringLiteral("directory") &&
                    outcome.action ==
                        AssetMigrationFileAction::Copy &&
                    outcome.reason ==
                        QStringLiteral(
                            "preserve-source-directory");
            });
    const auto existingEmptyDirectoryOutcome =
        std::find_if(
            report.fileOutcomes.cbegin(),
            report.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "custom/player-empty") &&
                    outcome.entryType ==
                        QStringLiteral("directory") &&
                    outcome.action ==
                        AssetMigrationFileAction::Copy &&
                    outcome.reason ==
                        QStringLiteral(
                            "preserve-existing-player-directory");
            });
    const QString textReport = readUtf8TextFile(
        root.filePath("output/migration_report.txt"));
    const QJsonArray jsonFileOutcomes =
        QJsonDocument::fromJson(
            readRawFile(
                root.filePath(
                    "output/migration_report.json")))
            .object()
            .value(QStringLiteral("files"))
            .toArray();
    const auto jsonSourceEmptyDirectoryOutcome =
        std::find_if(
            jsonFileOutcomes.cbegin(),
            jsonFileOutcomes.cend(),
            [](const QJsonValue& value)
            {
                const QJsonObject outcome =
                    value.toObject();
                return outcome.value(
                           QStringLiteral("source"))
                           .toString() ==
                        QStringLiteral(
                            "custom/source-empty") &&
                    outcome.value(
                           QStringLiteral("entryType"))
                           .toString() ==
                        QStringLiteral("directory") &&
                    outcome.value(
                           QStringLiteral("action"))
                           .toString() ==
                        QStringLiteral("copy");
            });
    const auto jsonPreservedPlayerOutcome =
        std::find_if(
            jsonFileOutcomes.cbegin(),
            jsonFileOutcomes.cend(),
            [&preservedPlayerSha256](
                const QJsonValue& value)
            {
                const QJsonObject outcome =
                    value.toObject();
                return outcome.value(
                           QStringLiteral("source")).
                           toString() ==
                        QStringLiteral(
                            "custom/player.bin") &&
                    outcome.value(
                           QStringLiteral("action")).
                           toString() ==
                        QStringLiteral("copy") &&
                    outcome.value(
                           QStringLiteral(
                               "outputSha256")).
                           toString() ==
                        preservedPlayerSha256;
            });
    ok = check(
        result != MigrationResult::Failed &&
            readRawFile(
                root.filePath("output/custom/player.bin")) ==
                QByteArray("player-owned") &&
            readRawFile(
                root.filePath("output/sound/new.bin")) ==
                QByteArray("new-runtime") &&
            QFileInfo(
                root.filePath(
                    "output/custom/source-empty")).
                isDir() &&
            QFileInfo(
                root.filePath(
                    "output/custom/player-empty")).
                isDir() &&
            preservedOutcome !=
                report.fileOutcomes.cend() &&
            !preservedOutcome->
                 outputSha256.isEmpty() &&
            preservedOutcome->
                    outputSha256 ==
                preservedPlayerSha256 &&
            sourceEmptyDirectoryOutcome !=
                report.fileOutcomes.cend() &&
            existingEmptyDirectoryOutcome !=
                report.fileOutcomes.cend(),
        "complete migration preserves source and existing empty directories and reports every directory copy") &&
        ok;
    ok = check(
        textReport.contains(
            QStringLiteral(
                "source=custom/player.bin")) &&
            textReport.contains(
                QStringLiteral("sha256=") +
                preservedPlayerSha256) &&
            jsonPreservedPlayerOutcome !=
                jsonFileOutcomes.cend(),
        "preserved player file SHA-256 is identical in memory, text, and JSON reports") &&
        ok;
    ok = check(
        textReport.contains(
            QStringLiteral(
                "[copy] type=directory "
                "source=custom/source-empty")) &&
            jsonSourceEmptyDirectoryOutcome !=
                jsonFileOutcomes.cend(),
        "text and JSON reports classify preserved empty directories as directory copy outcomes") &&
        ok;

    const QString conflictSourceRoot =
        root.filePath("conflict-source");
    const QString conflictOutputRoot =
        root.filePath("conflict-output");
    ok = check(
        root.mkpath("conflict-source/custom/nested") &&
            root.mkpath("conflict-output/custom") &&
            writeRawFile(
                root.filePath(
                    "conflict-source/custom/nested/new.bin"),
                QByteArray("new")) &&
            writeRawFile(
                root.filePath(
                    "conflict-output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath(
                    "conflict-output/custom/nested"),
                QByteArray("player-file")),
        "write existing-output path-conflict fixtures") &&
        ok;
    AssetMigrationReport conflictReport;
    const MigrationResult conflictResult =
        migrator.migrate(
            conflictSourceRoot,
            conflictOutputRoot,
            options,
            conflictReport);
    const auto conflictOutcome = std::find_if(
        conflictReport.fileOutcomes.cbegin(),
        conflictReport.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return !outcome.sourceScan &&
                outcome.action ==
                    AssetMigrationFileAction::Fail &&
                outcome.reason ==
                    QStringLiteral(
                        "existing-file-conflicts-with-staged-entry");
        });
    ok = check(
        conflictResult == MigrationResult::Failed &&
            readRawFile(
                root.filePath(
                    "conflict-output/custom/nested")) ==
                QByteArray("player-file") &&
            !QFileInfo::exists(
                root.filePath(
                    "conflict-output/custom/nested/new.bin")) &&
            conflictOutcome !=
                conflictReport.fileOutcomes.cend(),
        "path conflict blocks publication and preserves the complete old output generation") &&
        ok;

    const QString sourceLinkTargetRoot =
        root.filePath("source-link-target");
    const QString sourceLinkRoot =
        root.filePath("source-link-source");
    const QString sourceLinkOutputRoot =
        root.filePath("source-link-output");
    ok = check(
        root.mkpath(
            "source-link-target/directory") &&
            root.mkpath(
                "source-link-source/sound") &&
            root.mkpath(
                "source-link-output") &&
            writeRawFile(
                root.filePath(
                    "source-link-target/directory/outside.bin"),
                QByteArray("outside-directory")) &&
            writeRawFile(
                root.filePath(
                    "source-link-target/outside-file.bin"),
                QByteArray("outside-file")) &&
            writeRawFile(
                root.filePath(
                    "source-link-source/sound/new.bin"),
                QByteArray("new-runtime")) &&
            writeRawFile(
                root.filePath(
                    "source-link-output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath(
                    "source-link-output/stable.bin"),
                QByteArray("stable")),
        "write source filesystem-link migration fixtures") &&
        ok;
    QString sourceDirectoryLinkError;
    QString sourceFileLinkError;
    const bool sourceDirectoryLinkCreated =
        createDirectoryLink(
            QDir(sourceLinkTargetRoot).filePath(
                QStringLiteral("directory")),
            QDir(sourceLinkRoot).filePath(
                QStringLiteral("linked-directory")),
            sourceDirectoryLinkError);
    const bool sourceFileLinkCreated =
        createFileLink(
            QDir(sourceLinkTargetRoot).filePath(
                QStringLiteral("outside-file.bin")),
            QDir(sourceLinkRoot).filePath(
                QStringLiteral("linked-file.bin")),
            sourceFileLinkError);
    if (sourceDirectoryLinkCreated &&
        sourceFileLinkCreated)
    {
        AssetMigrationReport sourceLinkReport;
        const MigrationResult sourceLinkResult =
            migrator.migrate(
                sourceLinkRoot,
                sourceLinkOutputRoot,
                options,
                sourceLinkReport);
        const auto sourceDirectoryLinkOutcome =
            std::find_if(
                sourceLinkReport.fileOutcomes.cbegin(),
                sourceLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(
                                "linked-directory") &&
                        outcome.entryType ==
                            QStringLiteral(
                                "directory-link") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "source-directory-link-not-supported");
                });
        const auto sourceFileLinkOutcome =
            std::find_if(
                sourceLinkReport.fileOutcomes.cbegin(),
                sourceLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(
                                "linked-file.bin") &&
                        outcome.entryType ==
                            QStringLiteral(
                                "file-link") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "source-file-link-not-supported");
                });
        const bool sourceDirectoryLinkWasFollowed =
            std::any_of(
                sourceLinkReport.fileOutcomes.cbegin(),
                sourceLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return outcome.sourceScan &&
                        outcome.sourcePath.startsWith(
                            QStringLiteral(
                                "linked-directory/"));
                });
        ok = check(
            sourceLinkResult ==
                    MigrationResult::Failed &&
                readRawFile(
                    QDir(sourceLinkOutputRoot).
                        filePath(
                            QStringLiteral(
                                "stable.bin"))) ==
                    QByteArray("stable") &&
                !QFileInfo::exists(
                    QDir(sourceLinkOutputRoot).
                        filePath(
                            QStringLiteral(
                                "sound/new.bin"))) &&
                sourceDirectoryLinkOutcome !=
                    sourceLinkReport.fileOutcomes.cend() &&
                sourceFileLinkOutcome !=
                    sourceLinkReport.fileOutcomes.cend() &&
                !sourceDirectoryLinkWasFollowed,
            "source file and directory links fail explicitly without following targets or replacing old output") &&
            ok;
    }
    else
    {
        std::cout
            << "(source filesystem-link migration checks skipped: "
            << sourceDirectoryLinkError.toStdString()
            << " / "
            << sourceFileLinkError.toStdString()
            << ")\n";
    }

    const QString existingLinkTargetRoot =
        root.filePath("existing-link-target");
    const QString existingLinkSourceRoot =
        root.filePath("existing-link-source");
    const QString existingLinkOutputRoot =
        root.filePath("existing-link-output");
    ok = check(
        root.mkpath(
            "existing-link-target/directory") &&
            root.mkpath(
                "existing-link-source/sound") &&
            root.mkpath(
                "existing-link-output") &&
            writeRawFile(
                root.filePath(
                    "existing-link-target/directory/outside.bin"),
                QByteArray("outside-directory")) &&
            writeRawFile(
                root.filePath(
                    "existing-link-target/outside-file.bin"),
                QByteArray("outside-file")) &&
            writeRawFile(
                root.filePath(
                    "existing-link-source/sound/new.bin"),
                QByteArray("new-runtime")) &&
            writeRawFile(
                root.filePath(
                    "existing-link-output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath(
                    "existing-link-output/stable.bin"),
                QByteArray("stable")),
        "write existing-output filesystem-link migration fixtures") &&
        ok;
    QString existingDirectoryLinkError;
    QString existingFileLinkError;
    const bool existingDirectoryLinkCreated =
        createDirectoryLink(
            QDir(existingLinkTargetRoot).filePath(
                QStringLiteral("directory")),
            QDir(existingLinkOutputRoot).filePath(
                QStringLiteral("linked-directory")),
            existingDirectoryLinkError);
    const bool existingFileLinkCreated =
        createFileLink(
            QDir(existingLinkTargetRoot).filePath(
                QStringLiteral("outside-file.bin")),
            QDir(existingLinkOutputRoot).filePath(
                QStringLiteral("linked-file.bin")),
            existingFileLinkError);
    if (existingDirectoryLinkCreated &&
        existingFileLinkCreated)
    {
        AssetMigrationOptions existingLinkOptions =
            options;
        existingLinkOptions.resourceTypes = {
            AssetResourceType::Audio};
        AssetMigrationReport existingLinkReport;
        const MigrationResult existingLinkResult =
            migrator.migrate(
                existingLinkSourceRoot,
                existingLinkOutputRoot,
                existingLinkOptions,
                existingLinkReport);
        const auto existingDirectoryLinkOutcome =
            std::find_if(
                existingLinkReport.fileOutcomes.cbegin(),
                existingLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(
                                "linked-directory") &&
                        outcome.entryType ==
                            QStringLiteral(
                                "directory-link") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "existing-link-not-supported");
                });
        const auto existingFileLinkOutcome =
            std::find_if(
                existingLinkReport.fileOutcomes.cbegin(),
                existingLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(
                                "linked-file.bin") &&
                        outcome.entryType ==
                            QStringLiteral(
                                "file-link") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "existing-link-not-supported");
                });
        const bool existingDirectoryLinkWasFollowed =
            std::any_of(
                existingLinkReport.fileOutcomes.cbegin(),
                existingLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath.startsWith(
                            QStringLiteral(
                                "linked-directory/"));
                });
        ok = check(
            existingLinkResult ==
                    MigrationResult::Failed &&
                readRawFile(
                    QDir(existingLinkOutputRoot).
                        filePath(
                            QStringLiteral(
                                "stable.bin"))) ==
                    QByteArray("stable") &&
                !QFileInfo::exists(
                    QDir(existingLinkOutputRoot).
                        filePath(
                            QStringLiteral(
                                "sound/new.bin"))) &&
                existingDirectoryLinkOutcome !=
                    existingLinkReport.fileOutcomes.cend() &&
                existingFileLinkOutcome !=
                    existingLinkReport.fileOutcomes.cend() &&
                !existingDirectoryLinkWasFollowed,
            "partial migration fails explicitly for existing-output file and directory links outside the selected domain without following targets") &&
            ok;
    }
    else
    {
        std::cout
            << "(existing filesystem-link migration checks skipped: "
            << existingDirectoryLinkError.toStdString()
            << " / "
            << existingFileLinkError.toStdString()
            << ")\n";
    }

    const QString sourceRootLinkTarget =
        root.filePath(
            "source-root-link-target");
    const QString sourceRootLinkPath =
        root.filePath(
            "source-root-link");
    ok = check(
        root.mkpath(
            "source-root-link-target/sound") &&
            writeRawFile(
                root.filePath(
                    "source-root-link-target/sound/new.bin"),
                QByteArray("new-runtime")),
        "write source-root filesystem-link migration fixtures") &&
        ok;
    QString sourceRootLinkError;
    if (createDirectoryLink(
            sourceRootLinkTarget,
            sourceRootLinkPath,
            sourceRootLinkError))
    {
        const QString sourceRootLinkOutput =
            root.filePath(
                "source-root-link-output");
        AssetMigrationReport sourceRootLinkReport;
        const MigrationResult sourceRootLinkResult =
            migrator.migrate(
                sourceRootLinkPath,
                sourceRootLinkOutput,
                options,
                sourceRootLinkReport);
        const auto sourceRootLinkOutcome =
            std::find_if(
                sourceRootLinkReport.fileOutcomes.cbegin(),
                sourceRootLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(".") &&
                        outcome.entryType ==
                            QStringLiteral(
                                "directory-link") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "source-root-link-not-supported");
                });
        ok = check(
            sourceRootLinkResult ==
                    MigrationResult::Failed &&
                !QFileInfo::exists(
                    sourceRootLinkOutput) &&
                sourceRootLinkOutcome !=
                    sourceRootLinkReport.fileOutcomes.cend(),
            "source root directory link fails explicitly before scanning or creating output") &&
            ok;
    }
    else
    {
        std::cout
            << "(source-root filesystem-link migration check skipped: "
            << sourceRootLinkError.toStdString()
            << ")\n";
    }

    const QString outputRootLinkTarget =
        root.filePath(
            "output-root-link-target");
    const QString outputRootLinkPath =
        root.filePath(
            "output-root-link");
    ok = check(
        root.mkpath(
            "output-root-link-target") &&
            writeRawFile(
                root.filePath(
                    "output-root-link-target/stable.bin"),
                QByteArray("stable")),
        "write output-root filesystem-link migration fixtures") &&
        ok;
    QString outputRootLinkError;
    if (createDirectoryLink(
            outputRootLinkTarget,
            outputRootLinkPath,
            outputRootLinkError))
    {
        AssetMigrationReport outputRootLinkReport;
        const MigrationResult outputRootLinkResult =
            migrator.migrate(
                sourceRoot,
                outputRootLinkPath,
                options,
                outputRootLinkReport);
        const auto outputRootLinkOutcome =
            std::find_if(
                outputRootLinkReport.fileOutcomes.cbegin(),
                outputRootLinkReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(".") &&
                        outcome.entryType ==
                            QStringLiteral(
                                "directory-link") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "existing-output-root-link-not-supported");
                });
        ok = check(
            outputRootLinkResult ==
                    MigrationResult::Failed &&
                readRawFile(
                    QDir(outputRootLinkTarget).
                        filePath(
                            QStringLiteral(
                                "stable.bin"))) ==
                    QByteArray("stable") &&
                !QFileInfo::exists(
                    QDir(outputRootLinkTarget).
                        filePath(
                            QStringLiteral(
                                "sound/new.bin"))) &&
                outputRootLinkOutcome !=
                    outputRootLinkReport.fileOutcomes.cend(),
            "existing output root directory link fails explicitly without writing through the link") &&
            ok;
    }
    else
    {
        std::cout
            << "(output-root filesystem-link migration check skipped: "
            << outputRootLinkError.toStdString()
            << ")\n";
    }
    return ok;
}

bool testMigrationProtectsModifiedManagedOutputs()
{
    QTemporaryDir directory;
    if (!check(
            directory.isValid(),
            "create managed-output provenance fixture"))
    {
        return false;
    }

    QDir root(directory.path());
    const QString sourceRoot =
        root.filePath("source");
    const QString outputRoot =
        root.filePath("output");
    const QString relativePath =
        QStringLiteral("custom/managed.bin");
    const QString sourcePath =
        QDir(sourceRoot).filePath(relativePath);
    const QString outputPath =
        QDir(outputRoot).filePath(relativePath);
    bool ok = check(
        root.mkpath("source/custom") &&
            writeRawFile(
                sourcePath,
                QByteArray("generated-v1")),
        "write first managed-output generation");
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;
    options.writeModProfile = false;
    JxAssetMigrator migrator;
    AssetMigrationReport firstReport;
    const MigrationResult firstResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            firstReport);
    const QByteArray firstBytes =
        QByteArray("generated-v1");
    const QString firstSha256 =
        QString::fromLatin1(
            QCryptographicHash::hash(
                firstBytes,
                QCryptographicHash::Sha256).
                toHex());
    const QByteArray firstMarkerBytes =
        readRawFile(
            root.filePath(
                "output/.jxqy_asset_migration_marker"));
    const QJsonObject firstMarker =
        QJsonDocument::fromJson(
            firstMarkerBytes).object();
    const QJsonObject firstMarkerHashes =
        firstMarker.value(
            QStringLiteral(
                "managedOutputSha256")).
            toObject();
    const QJsonObject firstJsonReport =
        QJsonDocument::fromJson(
            readRawFile(
                root.filePath(
                    "output/migration_report.json"))).
            object();
    ok = check(
        firstResult == MigrationResult::Success &&
            readRawFile(outputPath) ==
                firstBytes &&
            firstMarker.value(
                QStringLiteral("schemaVersion")).
                toInt() == 1 &&
            firstMarker.value(
                QStringLiteral("hashAlgorithm")).
                toString() ==
                QStringLiteral("sha256") &&
            firstMarkerHashes.value(
                relativePath).toString() ==
                firstSha256 &&
            firstJsonReport.value(
                QStringLiteral(
                    "managedOutputSha256")).
                toObject().value(
                    relativePath).toString() ==
                firstSha256,
        "first generation publishes matching marker and report SHA-256 provenance") &&
        ok;

    ok = check(
        writeRawFile(
            sourcePath,
            QByteArray("generated-v2")),
        "write second managed-output generation") &&
        ok;
    AssetMigrationReport secondReport;
    const MigrationResult secondResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            secondReport);
    const QByteArray secondMarkerBytes =
        readRawFile(
            root.filePath(
                "output/.jxqy_asset_migration_marker"));
    ok = check(
        secondResult == MigrationResult::Success &&
            readRawFile(outputPath) ==
                QByteArray("generated-v2") &&
            secondMarkerBytes !=
                firstMarkerBytes,
        "an intact prior managed file may be replaced by the next generated version") &&
        ok;

    ok = check(
        writeRawFile(
            outputPath,
            QByteArray("player-edited")) &&
            writeRawFile(
                sourcePath,
                QByteArray("generated-v3")),
        "modify the prior managed output as player content") &&
        ok;
    AssetMigrationReport modifiedReport;
    const MigrationResult modifiedResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            modifiedReport);
    const auto modifiedConflict =
        std::find_if(
            modifiedReport.fileOutcomes.cbegin(),
            modifiedReport.fileOutcomes.cend(),
            [&relativePath](
                const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        relativePath &&
                    outcome.outputPath ==
                        relativePath &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-managed-output-modified") &&
                    !outcome.outputSha256.isEmpty();
            });
    const QJsonObject modifiedJson =
        QJsonDocument::fromJson(
            readRawFile(
                modifiedReport.
                    reportJsonFilePath)).
            object();
    const QJsonArray modifiedFiles =
        modifiedJson.value(
            QStringLiteral("files")).
            toArray();
    const auto modifiedJsonConflict =
        std::find_if(
            modifiedFiles.cbegin(),
            modifiedFiles.cend(),
            [&relativePath](
                const QJsonValue& value)
            {
                const QJsonObject outcome =
                    value.toObject();
                return outcome.value(
                           QStringLiteral("source")).
                           toString() ==
                        relativePath &&
                    outcome.value(
                           QStringLiteral("output")).
                           toString() ==
                        relativePath &&
                    outcome.value(
                           QStringLiteral("action")).
                           toString() ==
                        QStringLiteral("fail") &&
                    outcome.value(
                           QStringLiteral("reason")).
                           toString() ==
                        QStringLiteral(
                            "existing-managed-output-modified");
            });
    ok = check(
        modifiedResult ==
                MigrationResult::Failed &&
            readRawFile(outputPath) ==
                QByteArray("player-edited") &&
            readRawFile(
                root.filePath(
                    "output/.jxqy_asset_migration_marker")) ==
                secondMarkerBytes &&
            modifiedConflict !=
                modifiedReport.fileOutcomes.cend() &&
            modifiedJsonConflict !=
                modifiedFiles.cend() &&
            readUtf8TextFile(
                modifiedReport.reportFilePath).
                contains(
                    QStringLiteral(
                        "reason=existing-managed-output-modified")),
        "player-modified managed bytes block publication, preserve the old generation, and appear in both reports") &&
        ok;

    ok = check(
        QFile::remove(outputPath),
        "delete the prior managed output as player content") &&
        ok;
    AssetMigrationReport deletedReport;
    const MigrationResult deletedResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            deletedReport);
    const auto deletedConflict =
        std::find_if(
            deletedReport.fileOutcomes.cbegin(),
            deletedReport.fileOutcomes.cend(),
            [&relativePath](
                const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        relativePath &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-managed-output-deleted");
            });
    ok = check(
        deletedResult ==
                MigrationResult::Failed &&
            !QFileInfo::exists(outputPath) &&
            deletedConflict !=
                deletedReport.fileOutcomes.cend(),
        "player deletion of a previously managed file is not silently recreated") &&
        ok;

    ok = check(
        QFile::remove(sourcePath),
        "remove the current source producer for stale-provenance checks") &&
        ok;
    AssetMigrationReport deletedWithoutProducerReport;
    const MigrationResult deletedWithoutProducerResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            deletedWithoutProducerReport);
    const auto deletedWithoutProducerConflict =
        std::find_if(
            deletedWithoutProducerReport.
                fileOutcomes.cbegin(),
            deletedWithoutProducerReport.
                fileOutcomes.cend(),
            [&relativePath](
                const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        relativePath &&
                    outcome.outputPath ==
                        relativePath &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-managed-output-deleted");
            });
    ok = check(
        deletedWithoutProducerResult ==
                MigrationResult::Failed &&
            deletedWithoutProducerConflict !=
                deletedWithoutProducerReport.
                    fileOutcomes.cend() &&
            readRawFile(
                root.filePath(
                    "output/.jxqy_asset_migration_marker")) ==
                secondMarkerBytes,
        "a selected previous marker entry remains a deletion conflict even when the current source no longer produces it") &&
        ok;

    ok = check(
        writeRawFile(
            outputPath,
            QByteArray("generated-v2")),
        "restore intact prior managed bytes without a current producer") &&
        ok;
    AssetMigrationReport preservedWithoutProducerReport;
    const MigrationResult preservedWithoutProducerResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            preservedWithoutProducerReport);
    const QJsonObject preservedWithoutProducerMarker =
        QJsonDocument::fromJson(
            readRawFile(
                root.filePath(
                    "output/.jxqy_asset_migration_marker"))).
            object();
    ok = check(
        preservedWithoutProducerResult ==
                MigrationResult::Success &&
            readRawFile(outputPath) ==
                QByteArray("generated-v2") &&
            preservedWithoutProducerMarker.value(
                QStringLiteral(
                    "managedOutputSha256")).
                toObject().value(
                    relativePath).toString() ==
                QString::fromLatin1(
                    QCryptographicHash::hash(
                        QByteArray("generated-v2"),
                        QCryptographicHash::Sha256).
                        toHex()),
        "an intact selected previous managed file without a current producer is preserved with truthful provenance") &&
        ok;

    ok = check(
        writeRawFile(
            outputPath,
            QByteArray(
                "player-edited-without-producer")),
        "modify a prior managed file after its source producer is removed") &&
        ok;
    AssetMigrationReport modifiedWithoutProducerReport;
    const MigrationResult modifiedWithoutProducerResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            modifiedWithoutProducerReport);
    const auto modifiedWithoutProducerConflict =
        std::find_if(
            modifiedWithoutProducerReport.
                fileOutcomes.cbegin(),
            modifiedWithoutProducerReport.
                fileOutcomes.cend(),
            [&relativePath](
                const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        relativePath &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-managed-output-modified");
            });
    ok = check(
        modifiedWithoutProducerResult ==
                MigrationResult::Failed &&
            modifiedWithoutProducerConflict !=
                modifiedWithoutProducerReport.
                    fileOutcomes.cend(),
        "modified selected managed bytes still conflict after the current source stops producing that path") &&
        ok;

    const QString partialSourceRoot =
        root.filePath(
            QStringLiteral(
                "partial-provenance-source"));
    const QString partialOutputRoot =
        root.filePath(
            QStringLiteral(
                "partial-provenance-output"));
    const QString partialScriptPath =
        QStringLiteral(
            "script/managed.txt");
    const QString partialAudioPath =
        QStringLiteral(
            "sound/managed.wav");
    const QByteArray partialAudioBytes(
        "audio-v1");
    ok = check(
        root.mkpath(
            QStringLiteral(
                "partial-provenance-source/script")) &&
            root.mkpath(
                QStringLiteral(
                    "partial-provenance-source/sound")) &&
            writeRawFile(
                QDir(partialSourceRoot).
                    filePath(
                        partialScriptPath),
                QByteArray("script-v1")) &&
            writeRawFile(
                QDir(partialSourceRoot).
                    filePath(
                        partialAudioPath),
                partialAudioBytes),
        "write initial scripts-and-audio provenance fixture") &&
        ok;
    AssetMigrationReport partialBaselineReport;
    const MigrationResult partialBaselineResult =
        migrator.migrate(
            partialSourceRoot,
            partialOutputRoot,
            options,
            partialBaselineReport);
    const QJsonObject partialBaselineHashes =
        QJsonDocument::fromJson(
            readRawFile(
                QDir(partialOutputRoot).
                    filePath(
                        QStringLiteral(
                            ".jxqy_asset_migration_marker")))).
            object().value(
                QStringLiteral(
                    "managedOutputSha256")).
            toObject();
    const QString baselineAudioSha256 =
        partialBaselineHashes.value(
            partialAudioPath).toString();
    ok = check(
        partialBaselineResult ==
                MigrationResult::Success &&
            !baselineAudioSha256.isEmpty(),
        "initial full migration records scripts and audio provenance") &&
        ok;

    ok = check(
        writeRawFile(
            QDir(partialSourceRoot).
                filePath(
                    partialScriptPath),
            QByteArray("script-v2")),
        "update only the selected scripts producer") &&
        ok;
    AssetMigrationOptions scriptsOnlyOptions =
        options;
    scriptsOnlyOptions.resourceTypes = {
        AssetResourceType::Scripts};
    AssetMigrationReport scriptsOnlyReport;
    const MigrationResult scriptsOnlyResult =
        migrator.migrate(
            partialSourceRoot,
            partialOutputRoot,
            scriptsOnlyOptions,
            scriptsOnlyReport);
    const QJsonObject scriptsOnlyHashes =
        QJsonDocument::fromJson(
            readRawFile(
                QDir(partialOutputRoot).
                    filePath(
                        QStringLiteral(
                            ".jxqy_asset_migration_marker")))).
            object().value(
                QStringLiteral(
                    "managedOutputSha256")).
            toObject();
    ok = check(
        scriptsOnlyResult ==
                MigrationResult::Success &&
            readRawFile(
                QDir(partialOutputRoot).
                    filePath(
                        partialScriptPath)) ==
                QByteArray("script-v2") &&
            readRawFile(
                QDir(partialOutputRoot).
                    filePath(
                        partialAudioPath)) ==
                partialAudioBytes &&
            scriptsOnlyHashes.value(
                partialAudioPath).toString() ==
                baselineAudioSha256,
        "scripts-only migration updates selected provenance while retaining the unselected audio hash and bytes unchanged") &&
        ok;

    const QString legacySourceRoot =
        root.filePath("legacy-source");
    const QString legacyOutputRoot =
        root.filePath("legacy-output");
    const QString legacyRelativePath =
        QStringLiteral("custom/legacy.bin");
    ok = check(
        root.mkpath("legacy-source/custom") &&
            root.mkpath("legacy-output/custom") &&
            writeRawFile(
                QDir(legacySourceRoot).filePath(
                    legacyRelativePath),
                QByteArray("new-generated")) &&
            writeRawFile(
                QDir(legacyOutputRoot).filePath(
                    legacyRelativePath),
                QByteArray("legacy-or-player")) &&
            writeRawFile(
                QDir(legacyOutputRoot).filePath(
                    QStringLiteral(
                        ".jxqy_asset_migration_marker")),
                QByteArray("legacy-marker")),
        "write legacy marker without provenance") &&
        ok;
    AssetMigrationReport legacyReport;
    const MigrationResult legacyResult =
        migrator.migrate(
            legacySourceRoot,
            legacyOutputRoot,
            options,
            legacyReport);
    const auto legacyConflict =
        std::find_if(
            legacyReport.fileOutcomes.cbegin(),
            legacyReport.fileOutcomes.cend(),
            [&legacyRelativePath](
                const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        legacyRelativePath &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-managed-output-provenance-missing");
            });
    ok = check(
        legacyResult ==
                MigrationResult::Failed &&
            readRawFile(
                QDir(legacyOutputRoot).filePath(
                    legacyRelativePath)) ==
                QByteArray("legacy-or-player") &&
            legacyConflict !=
                legacyReport.fileOutcomes.cend(),
        "legacy same-path bytes without SHA-256 provenance fail conservatively instead of being overwritten") &&
        ok;
    return ok;
}

bool testMigrationDeduplicatesExactBaseMediaResources()
{
    QTemporaryDir directory;
    if (!check(
            directory.isValid(),
            "create dependency-deduplication migration fixture"))
    {
        return false;
    }

    QDir root(directory.path());
    const QString sourceRoot =
        root.filePath(QStringLiteral("source"));
    const QString outputRoot =
        root.filePath(QStringLiteral("converted"));
    const QByteArray sharedBytes("shared-media");
    const QByteArray conflictBytes("conflict-media");
    const QByteArray uiBytes("ui-media");
    bool ok = check(
        root.mkpath(QStringLiteral("base-a/sound")) &&
            root.mkpath(QStringLiteral("base-a/ini")) &&
            root.mkpath(QStringLiteral("base-b/sound")) &&
            root.mkpath(QStringLiteral("ui-base/sound/ui")) &&
            root.mkpath(QStringLiteral("source/sound/ui")) &&
            root.mkpath(QStringLiteral("source/ini")) &&
            writeUtf8TextFile(
                root.filePath(
                    QStringLiteral(
                        "base-a/game_profile.ini")),
                "[Game]\nId=BASE_A\nType=0\n\n"
                "[Resource]\nDependencyId=BASE_B\n") &&
            writeUtf8TextFile(
                root.filePath(
                    QStringLiteral(
                        "base-b/game_profile.ini")),
                "[Game]\nId=BASE_B\nType=0\n") &&
            writeUtf8TextFile(
                root.filePath(
                    QStringLiteral(
                        "ui-base/game_profile.ini")),
                "[Game]\nId=UI_BASE\nType=1\n\n"
                "[UI]\nProfile=YYCS\n") &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "base-a/sound/first.wav")),
                QByteArray("first-base-version")) &&
            writeUtf8TextFile(
                root.filePath(
                    QStringLiteral(
                        "base-a/ini/same.ini")),
                "[Init]\nValue=Same\n") &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "base-b/sound/shared.wav")),
                QByteArray("old-shared-base")) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "base-b/sound/conflict.wav")),
                QByteArray("old-conflict-base")) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "base-b/sound/first.wav")),
                QByteArray("source-first-version")) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "ui-base/sound/ui/click.wav")),
                uiBytes) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "source/sound/shared.wav")),
                sharedBytes) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "source/sound/conflict.wav")),
                conflictBytes) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "source/sound/first.wav")),
                QByteArray("source-first-version")) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "source/sound/ui/click.wav")),
                uiBytes) &&
            writeUtf8TextFile(
                root.filePath(
                    QStringLiteral(
                        "source/ini/same.ini")),
                "[Init]\nValue=Same\n"),
        "write dependency-deduplication migration fixture");
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;
    options.modId = QStringLiteral("DEDUP_MOD");
    options.modName = QStringLiteral("Dedup Mod");
    options.dependencyId = QStringLiteral("BASE_A");
    options.uiBaseId = QStringLiteral("UI_BASE");
    options.uiProfile = QStringLiteral("YYCS");
    JxAssetMigrator migrator;

    AssetMigrationReport firstReport;
    const MigrationResult firstResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            firstReport);
    ok = check(
        firstResult == MigrationResult::Success &&
            !QFileInfo::exists(
                root.filePath(
                    QStringLiteral(
                        "converted/sound/ui/click.wav"))) &&
            readRawFile(
                root.filePath(
                    QStringLiteral(
                        "converted/sound/shared.wav"))) ==
                sharedBytes &&
            readRawFile(
                root.filePath(
                    QStringLiteral(
                        "converted/sound/first.wav"))) ==
                QByteArray("source-first-version") &&
            QFileInfo::exists(
                root.filePath(
                    QStringLiteral(
                        "converted/ini/same.ini"))) &&
            firstReport.dependencyDuplicateFiles == 1 &&
            firstReport.dependencyDuplicateBytes ==
                static_cast<quint64>(uiBytes.size()),
        "migration deduplicates exact UI media but preserves text and stops at the first differing content base") &&
        ok;

    ok = check(
        writeRawFile(
            root.filePath(
                QStringLiteral(
                    "base-b/sound/shared.wav")),
            sharedBytes),
        "make the recursive dependency copy identical for the second migration") &&
        ok;
    AssetMigrationReport secondReport;
    const MigrationResult secondResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            secondReport);
    const QJsonObject secondCounts =
        QJsonDocument::fromJson(
            readRawFile(
                root.filePath(
                    QStringLiteral(
                        "converted/migration_report.json")))).
            object().value(QStringLiteral("counts")).
            toObject();
    const QJsonObject secondManagedHashes =
        QJsonDocument::fromJson(
            readRawFile(
                root.filePath(
                    QStringLiteral(
                        "converted/.jxqy_asset_migration_marker")))).
            object().value(
                QStringLiteral("managedOutputSha256")).
            toObject();
    ok = check(
        secondResult == MigrationResult::Success &&
            !QFileInfo::exists(
                root.filePath(
                    QStringLiteral(
                        "converted/sound/shared.wav"))) &&
            !secondManagedHashes.contains(
                QStringLiteral("sound/shared.wav")) &&
            secondReport.dependencyDuplicateFiles == 2 &&
            secondCounts.value(
                QStringLiteral(
                    "dependencyDuplicateFiles")).
                toInt() == 2,
        "a newly redundant prior managed file is removed transactionally and dropped from provenance") &&
        ok;

    ok = check(
        writeRawFile(
            root.filePath(
                QStringLiteral(
                    "converted/sound/conflict.wav")),
            QByteArray("player-edited")) &&
            writeRawFile(
                root.filePath(
                    QStringLiteral(
                        "base-b/sound/conflict.wav")),
                conflictBytes),
        "prepare a player-modified output that has become redundant") &&
        ok;
    AssetMigrationReport conflictReport;
    const MigrationResult conflictResult =
        migrator.migrate(
            sourceRoot,
            outputRoot,
            options,
            conflictReport);
    const auto conflict = std::find_if(
        conflictReport.fileOutcomes.cbegin(),
        conflictReport.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return outcome.outputPath ==
                    QStringLiteral(
                        "sound/conflict.wav") &&
                outcome.action ==
                    AssetMigrationFileAction::Fail &&
                outcome.reason ==
                    QStringLiteral(
                        "existing-managed-output-modified");
        });
    ok = check(
        conflictResult == MigrationResult::Failed &&
            readRawFile(
                root.filePath(
                    QStringLiteral(
                        "converted/sound/conflict.wav"))) ==
                QByteArray("player-edited") &&
            conflict != conflictReport.fileOutcomes.cend(),
        "dependency deduplication never deletes a player-modified prior managed file") &&
        ok;
    return ok;
}

bool testMigrationRejectsCaseFoldedOutputCollisions()
{
    const QStringList syntheticCollisions =
        assetMigrationOutputPathCollisionSources({
            qMakePair(
                QStringLiteral(
                    "ini/ui/Panel.ini"),
                QStringLiteral(
                    "ini/ui/panel.ini")),
            qMakePair(
                QStringLiteral(
                    "linux-only-source/other.ini"),
                QStringLiteral(
                    "INI/UI/../UI/PANEL.INI")),
            qMakePair(
                QStringLiteral(
                    "unicode/composed.ini"),
                QStringLiteral(
                    "custom/Caf\u00e9.ini")),
            qMakePair(
                QStringLiteral(
                    "unicode/decomposed.ini"),
                QStringLiteral(
                    "custom/Cafe\u0301.ini")),
            qMakePair(
                QStringLiteral(
                    "unicode/sigma.ini"),
                QStringLiteral(
                    "custom/\u03a3.ini")),
            qMakePair(
                QStringLiteral(
                    "unicode/final-sigma.ini"),
                QStringLiteral(
                    "custom/\u03c2.ini")),
            qMakePair(
                QStringLiteral(
                    "custom/independent.bin"),
                QStringLiteral(
                    "custom/independent.bin"))
        });
    QStringList expectedSyntheticCollisions = {
        QStringLiteral(
            "ini/ui/Panel.ini"),
        QStringLiteral(
            "linux-only-source/other.ini"),
        QStringLiteral(
            "unicode/composed.ini"),
        QStringLiteral(
            "unicode/decomposed.ini"),
        QStringLiteral(
            "unicode/sigma.ini"),
        QStringLiteral(
            "unicode/final-sigma.ini")
    };
    expectedSyntheticCollisions.sort(
        Qt::CaseSensitive);
    bool ok = check(
        syntheticCollisions ==
            expectedSyntheticCollisions,
        "clean-path, Unicode NFC and case-fold output collision detection is deterministic on every host filesystem");

    QTemporaryDir directory;
    if (!check(
            directory.isValid(),
            "create case-fold migration collision fixture"))
    {
        return false;
    }
    QDir root(directory.path());
    const QString sourceRoot =
        root.filePath("source");
    const QString outputRoot =
        root.filePath("output");
    ok = check(
        root.mkpath("source/ini/ui") &&
            root.mkpath("output") &&
            writeRawFile(
                root.filePath(
                    "source/ini/ui/Panel.ini"),
                QByteArray("[Init]\nValue=upper\n")) &&
            writeRawFile(
                root.filePath(
                    "source/ini/ui/panel.ini"),
                QByteArray("[Init]\nValue=lower\n")) &&
            writeRawFile(
                root.filePath(
                    "output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath(
                    "output/player.bin"),
                QByteArray("stable-player")),
        "write case-fold migration collision fixture") &&
        ok;
    if (!ok)
        return false;

    const QStringList caseNames =
        QDir(
            root.filePath(
                "source/ini/ui")).
            entryList(
                {QStringLiteral("*.ini")},
                QDir::Files,
                QDir::Name);
    if (caseNames.size() == 2)
    {
        AssetMigrationOptions options;
        options.convertScript = false;
        options.writeModProfile = false;
        AssetMigrationReport report;
        JxAssetMigrator migrator;
        const MigrationResult result =
            migrator.migrate(
                sourceRoot,
                outputRoot,
                options,
                report);
        const int collisionOutcomeCount =
            std::count_if(
                report.fileOutcomes.cbegin(),
                report.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome&
                       outcome)
                {
                    return outcome.sourceScan &&
                        outcome.action ==
                            AssetMigrationFileAction::
                                Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "source-output-path-collision");
                });
        ok = check(
            result == MigrationResult::Failed &&
                collisionOutcomeCount == 2 &&
                readRawFile(
                    root.filePath(
                        "output/player.bin")) ==
                    QByteArray("stable-player") &&
                !QFileInfo::exists(
                    root.filePath(
                        "output/ini/ui/panel.ini")),
            "case-sensitive source names that normalize to one output fail before writing and preserve the old output") &&
            ok;
    }
    else
    {
        std::cout
            << "(case-sensitive filesystem integration fixture skipped; "
               "synthetic ASCII case-fold detector still checked)\n";
    }
    return ok;
}

bool testMigrationResolvesCompositeAndProfileOutputOwnership()
{
    QTemporaryDir directory;
    if (!check(
            directory.isValid(),
            "create migration producer-ownership fixture"))
    {
        return false;
    }

    QDir root(directory.path());
    const QString collisionSource =
        root.filePath("collision-source");
    const QString collisionOutput =
        root.filePath("collision-output");
    bool ok = check(
        root.mkpath(
            "collision-source/script/common") &&
            root.mkpath("collision-output") &&
            writeRawFile(
                root.filePath(
                    "collision-source/script/common/Talk.dat"),
                QByteArray("talk-data")) &&
            writeRawFile(
                root.filePath(
                    "collision-source/script/common/Talkidx.dat"),
                QByteArray(16, '\0')) &&
            writeUtf8TextFile(
                root.filePath(
                    "collision-source/script/common/talkindex.txt"),
                QStringLiteral("explicit-common\n")) &&
            writeUtf8TextFile(
                root.filePath(
                    "collision-source/talkindex.txt"),
                QStringLiteral("explicit-root\n")) &&
            writeRawFile(
                root.filePath(
                    "collision-source/custom.bin"),
                QByteArray("must-not-publish")) &&
            writeRawFile(
                root.filePath(
                    "collision-output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath(
                    "collision-output/player.bin"),
                QByteArray("stable-player")),
        "write migration composite-output collision fixture");
    if (!ok)
        return false;

    AssetMigrationOptions collisionOptions;
    collisionOptions.convertScript = false;
    collisionOptions.writeModProfile = false;
    AssetMigrationReport collisionReport;
    JxAssetMigrator migrator;
    const MigrationResult collisionResult =
        migrator.migrate(
            collisionSource,
            collisionOutput,
            collisionOptions,
            collisionReport);
    const int sourceCollisionCount =
        std::count_if(
            collisionReport.fileOutcomes.cbegin(),
            collisionReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return outcome.sourceScan &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "source-output-path-collision") &&
                    outcome.outputPath.toLower().
                        endsWith(
                            QStringLiteral(
                                "talkindex.txt"));
            });
    const int generatedCollisionCount =
        std::count_if(
            collisionReport.fileOutcomes.cbegin(),
            collisionReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "generated-output-path-collision");
            });
    ok = check(
        collisionResult ==
                MigrationResult::Failed &&
            sourceCollisionCount == 2 &&
            generatedCollisionCount == 2 &&
            readRawFile(
                root.filePath(
                    "collision-output/player.bin")) ==
                QByteArray("stable-player") &&
            !QFileInfo::exists(
                root.filePath(
                    "collision-output/custom.bin")) &&
            !QFileInfo::exists(
                root.filePath(
                    "collision-output/talkindex.txt")) &&
            QFileInfo::exists(
                collisionReport.reportJsonFilePath) &&
            QFileInfo(
                collisionReport.reportJsonFilePath).
                    absolutePath() !=
                QFileInfo(collisionOutput).
                    absoluteFilePath(),
        "Talk composite outputs and explicit talkindex sources fail before staging is published") &&
        ok;

    const QString stableTalkSource =
        root.filePath("stable-talk-source");
    const QString stableTalkOutput =
        root.filePath("stable-talk-output");
    const QByteArray originalTalkData(
        "ORIGINAL_TALK");
    QByteArray talkIndexData(12, '\0');
    ok = check(
        root.mkpath(
            "stable-talk-source/script/common") &&
            writeRawFile(
                root.filePath(
                    "stable-talk-source/script/common/Talk.dat"),
                originalTalkData) &&
            writeRawFile(
                root.filePath(
                    "stable-talk-source/script/common/Talkidx.dat"),
                talkIndexData) &&
            writeRawFile(
                root.filePath(
                    "stable-talk-source/zz-trigger.bin"),
                QByteArray("trigger")),
        "write stable Talk composite fixture") &&
        ok;
    if (!ok)
        return false;

    AssetMigrationOptions stableTalkOptions;
    stableTalkOptions.convertScript = false;
    stableTalkOptions.writeModProfile = false;
    stableTalkOptions.sourceEncoding =
        QStringLiteral("utf8");
    AssetMigrationReport stableTalkReport;
    bool liveTalkSourceMutated = false;
    const MigrationResult stableTalkResult =
        migrator.migrate(
            stableTalkSource,
            stableTalkOutput,
            stableTalkOptions,
            stableTalkReport,
            JxAssetMigrator::LogCallback(),
            [&](int,
                int,
                const QString& currentFile)
            {
                if (currentFile ==
                        QStringLiteral(
                            "zz-trigger.bin") &&
                    !liveTalkSourceMutated)
                {
                    liveTalkSourceMutated =
                        writeRawFile(
                            QDir(stableTalkSource).
                                filePath(
                                    QStringLiteral(
                                        "script/common/Talk.dat")),
                            QByteArray(
                                "MUTATED_LIVE_SOURCE"));
                }
            });
    const QByteArray commonTalkIndex =
        readRawFile(
            root.filePath(
                "stable-talk-output/script/common/"
                "talkindex.txt"));
    const int generatedTalkOutcomeCount =
        std::count_if(
            stableTalkReport.fileOutcomes.cbegin(),
            stableTalkReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome&
                   outcome)
            {
                return !outcome.sourceScan &&
                    outcome.action ==
                        AssetMigrationFileAction::
                            Convert &&
                    outcome.reason ==
                        QStringLiteral(
                            "generated-talk-index") &&
                    !outcome.outputSha256.
                        isEmpty();
            });
    ok = check(
        liveTalkSourceMutated &&
            stableTalkResult ==
                MigrationResult::Success &&
            readRawFile(
                root.filePath(
                    "stable-talk-output/script/common/"
                    "Talk.dat")) ==
                originalTalkData &&
            commonTalkIndex.contains(
                QByteArray("ORIGINAL_TALK")) &&
            !commonTalkIndex.contains(
                QByteArray(
                    "MUTATED_LIVE_SOURCE")) &&
            readRawFile(
                root.filePath(
                    "stable-talk-output/talkindex.txt")) ==
                commonTalkIndex &&
            generatedTalkOutcomeCount == 2,
        "Talk composite conversion reads the staged generation, ignores a later live-source mutation, and reports both derived files") &&
        ok;

    const QString profileSource =
        root.filePath("profile-source");
    const QString profileOutput =
        root.filePath("profile-output");
    const QString sourceProfile =
        QStringLiteral(
            "; preserve-source-comment\n"
            "[Game]\n"
            "Id=SOURCE_PROFILE\n"
            "Name=Source Profile\n"
            "Author=Source Author\n"
            "CustomGameKey=keep-game\n"
            "Type=0\n"
            "UseWav=1\n"
            "\n"
            "[Resource]\n"
            "DependencyId=OLD_BASE\n"
            "DependencyPath=../old-dependency\n"
            "CommonPath=../old-common\n"
            "\n"
            "[UI]\n"
            "BaseId=OLD_UI\n"
            "Profile=OLD_UI\n"
            "PreferLocal=1\n"
            "\n"
            "[Save]\n"
            "Namespace=OLD_SAVE\n"
            "\n"
            "[Features]\n"
            "OldFeature=1\n"
            "\n"
            "[Custom]\n"
            "; preserve-custom-comment\n"
            "PlayerField=keep-me\n");
    ok = check(
        root.mkpath("profile-source/sound") &&
            writeUtf8TextFile(
                root.filePath(
                    "profile-source/Game_Profile.ini"),
                sourceProfile) &&
            writeRawFile(
                root.filePath(
                    "profile-source/sound/new.bin"),
                QByteArray("profile-sound")),
        "write source-provided game profile fixture") &&
        ok;
    if (!ok)
        return false;

    AssetMigrationOptions profileOptions;
    profileOptions.convertScript = false;
    profileOptions.writeModProfile = true;
    profileOptions.modId =
        QStringLiteral("GENERATED_PROFILE");
    profileOptions.modName =
        QStringLiteral("Generated Profile");
    profileOptions.modType = 3;
    profileOptions.dependencyId =
        QStringLiteral("BASE_A,BASE_B");
    profileOptions.uiBaseId =
        QStringLiteral("YYCS");
    profileOptions.uiProfile =
        QStringLiteral("jxqy2");
    profileOptions.preferLocalUi = false;
    profileOptions.saveNamespace =
        QStringLiteral("MERGED_SAVE");
    profileOptions.minimumMagicDamage = 7;
    profileOptions.minimumMagicDamageDefined = true;
    profileOptions.magicEffectCalculationMode =
        MagicEffectCalculationMode::AddToAttack;
    profileOptions.magicEffectCalculationModeDefined = true;
    profileOptions.features.insert(
        QStringLiteral("FeatureA"),
        true);
    profileOptions.features.insert(
        QStringLiteral("FeatureB"),
        false);
    AssetMigrationReport profileReport;
    const MigrationResult profileResult =
        migrator.migrate(
            profileSource,
            profileOutput,
            profileOptions,
            profileReport);
    const auto profileOutcome =
        std::find_if(
            profileReport.fileOutcomes.cbegin(),
            profileReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return outcome.sourceScan &&
                    outcome.outputPath.toLower() ==
                        QStringLiteral(
                            "game_profile.ini") &&
                    outcome.reason ==
                        QStringLiteral(
                            "source-mod-profile-merged");
            });
    const QString publishedProfile =
        readUtf8TextFile(
            root.filePath(
                "profile-output/game_profile.ini"));
    const QStringList publishedRootFiles =
        QDir(profileOutput).entryList(
            QDir::Files,
            QDir::Name);
    ok = check(
        profileResult ==
                MigrationResult::Success &&
            publishedProfile.contains(
                QStringLiteral(
                    "; preserve-source-comment")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "; preserve-custom-comment")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "PlayerField=keep-me")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "Author=Source Author")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "CustomGameKey=keep-game")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "Id=GENERATED_PROFILE")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "Name=Generated Profile")) &&
            publishedProfile.contains(
                QStringLiteral("Type=3")) &&
            publishedProfile.contains(
                QStringLiteral("UseWav=1")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "DependencyId=BASE_A,BASE_B")) &&
            !publishedProfile.contains(
                QStringLiteral("DependencyPath=")) &&
            !publishedProfile.contains(
                QStringLiteral("CommonPath=")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "BaseId=YYCS")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "Profile=JXQY2")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "PreferLocal=0")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "Namespace=MERGED_SAVE")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "MinimumMagicDamage=7")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "MagicEffectCalculationMode=AddToAttack")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "FeatureA=1")) &&
            publishedProfile.contains(
                QStringLiteral(
                    "FeatureB=0")) &&
            !publishedProfile.contains(
                QStringLiteral(
                    "OldFeature=")) &&
            publishedProfile.count(
                QStringLiteral("[Game]")) == 1 &&
            !readRawFile(
                 root.filePath(
                     "profile-output/game_profile.ini")).
                 startsWith(
                     QByteArray(
                         "\xEF\xBB\xBF",
                         3)) &&
            publishedRootFiles.contains(
                QStringLiteral(
                    "game_profile.ini"),
                Qt::CaseSensitive) &&
            !publishedRootFiles.contains(
                QStringLiteral(
                    "Game_Profile.ini"),
                Qt::CaseSensitive) &&
            profileOutcome !=
                profileReport.fileOutcomes.cend(),
        "a case-variant source profile maps to one BOM-free canonical game_profile.ini, preserves unknown content, and applies migration options") &&
        ok;

    const QString dependencyCollection =
        root.filePath("minimum-damage-collection");
    ok = check(
        root.mkpath("minimum-damage-collection/base") &&
            root.mkpath("minimum-damage-source/sound") &&
            writeUtf8TextFile(
                root.filePath(
                    "minimum-damage-collection/resources.ini"),
                "[Pack.BASE]\nId=BASE\nPath=base\n"
                "Manifest=game_profile.ini\nEnabled=1\n") &&
            writeUtf8TextFile(
                root.filePath(
                    "minimum-damage-collection/base/game_profile.ini"),
                "[Game]\nId=BASE\nType=2\n\n"
                "[Experience]\nDefeatedNpcExperienceMode=StoredExperience\n"
                "ExperienceMultiplier=2.5\n"
                "LevelUpThresholdMode=GreaterThanOrEqual\n\n"
                "[Gameplay]\nPartnerFollowRadius=6\n"
                "PartnerFollowRunRadius=11\n\n"
                "[Combat]\nMinimumMagicDamage=4\n"
                "MagicEffectCalculationMode=AddToAttack\n\n"
                "[Script]\nNpcActionProfile=Legacy\n"
                "NpcRuntimeProfile=Legacy\nSpecialActionMode=Replace\n"
                "AddLifeMode=PlayerRules\n\n"
                "[UI]\nProfile=YYCS\n\n"
                "[Features]\nMagicTriggerAtAnimationEnd=0\n"
                "LumAsBrightness=1\nAmbientLumOverlay=1\n"
                "RainSceneTint=0\n\n"
                "[Title]\nMusic=custom-base.ogg\n") &&
            writeRawFile(
                root.filePath(
                    "minimum-damage-source/sound/test.bin"),
                QByteArray("audio")),
        "write minimum magic damage dependency fixture") && ok;
    AssetMigrationOptions inheritedMinimumDamageOptions;
    inheritedMinimumDamageOptions.dependencyId = "BASE";
    ok = check(
        JxAssetMigrator::resolveMinimumMagicDamageDefault(
            QDir(dependencyCollection).filePath("converted-mod"),
            inheritedMinimumDamageOptions) == 4,
        "asset conversion defaults minimum magic damage from the declared base profile") && ok;
    ok = check(
        JxAssetMigrator::resolveMagicEffectCalculationModeDefault(
            QDir(dependencyCollection).filePath("converted-mod"),
            inheritedMinimumDamageOptions) ==
            MagicEffectCalculationMode::AddToAttack,
        "asset conversion defaults magic effect calculation from the declared base profile") && ok;
    AssetMigrationReport inheritedMinimumDamageReport;
    const QString inheritedMinimumDamageOutput =
        QDir(dependencyCollection).filePath("converted-mod");
    const MigrationResult inheritedMinimumDamageResult =
        migrator.migrate(
            root.filePath("minimum-damage-source"),
            inheritedMinimumDamageOutput,
            inheritedMinimumDamageOptions,
            inheritedMinimumDamageReport);
    const QString inheritedProfileText = readUtf8TextFile(
        QDir(inheritedMinimumDamageOutput).filePath("game_profile.ini"));
    ok = check(
        inheritedMinimumDamageResult != MigrationResult::Failed,
        "asset conversion using an actual base profile succeeds") && ok;
    ok = check(
        inheritedProfileText.contains("MinimumMagicDamage=4") &&
            inheritedProfileText.contains(
                "MagicEffectCalculationMode=AddToAttack"),
        "asset conversion inherits combat defaults from the actual base profile") && ok;
    ok = check(
        inheritedProfileText.contains(
            "DefeatedNpcExperienceMode=StoredExperience") &&
            inheritedProfileText.contains("ExperienceMultiplier=2.5") &&
            inheritedProfileText.contains(
                "LevelUpThresholdMode=GreaterThanOrEqual"),
        "asset conversion inherits experience defaults from the actual base profile") && ok;
    ok = check(
        inheritedProfileText.contains("PartnerFollowRadius=6") &&
            inheritedProfileText.contains("PartnerFollowRunRadius=11"),
        "asset conversion inherits partner distance defaults from the actual base profile") && ok;
    ok = check(
        inheritedProfileText.contains("NpcActionProfile=Legacy") &&
            inheritedProfileText.contains("NpcRuntimeProfile=Legacy") &&
            inheritedProfileText.contains("SpecialActionMode=Replace") &&
            inheritedProfileText.contains("AddLifeMode=PlayerRules"),
        "asset conversion inherits script defaults from the actual base profile") && ok;
    ok = check(
        inheritedProfileText.contains("Profile=YYCS"),
        "asset conversion inherits the UI profile from the actual base profile") && ok;
    ok = check(
        inheritedProfileText.toLower().contains("rainscenetint=0"),
        "asset conversion inherits feature defaults from the actual base profile") && ok;
    ok = check(
        inheritedProfileText.contains("Music=custom-base.ogg"),
        "asset conversion inherits title music from the actual base profile") && ok;

    AssetMigrationReport preservedMinimumDamageReport;
    const QString preservedMinimumDamageOutput =
        QDir(dependencyCollection).filePath("preserved-source-mod");
    const MigrationResult preservedMinimumDamageResult =
        migrator.migrate(
            root.filePath("profile-output"),
            preservedMinimumDamageOutput,
            inheritedMinimumDamageOptions,
            preservedMinimumDamageReport);
    ok = check(
        preservedMinimumDamageResult != MigrationResult::Failed &&
            readUtf8TextFile(QDir(preservedMinimumDamageOutput).filePath(
                "game_profile.ini")).contains(
                    "MinimumMagicDamage=7"),
        "asset conversion preserves an explicit source minimum before applying the base default") && ok;

    ok = check(
        root.mkpath(
            "broken-profile-source/sound") &&
            writeRawFile(
                root.filePath(
                    "broken-profile-source/"
                    "Game_Profile.ini"),
                QByteArray(
                    "[Game]\n"
                    "Id=BROKEN_BY_FAULT\n")) &&
            writeRawFile(
                root.filePath(
                    "broken-profile-source/"
                    "sound/valid.bin"),
                QByteArray("valid-audio")),
        "write source profile merge-failure fixture") &&
        ok;
    if (!ok)
        return false;
    JxAssetMigrator::
        setFileSystemFaultInjectorForTests(
            [](JxAssetMigrator::
                   FileSystemOperation operation,
               const QString&,
               const QString&)
            {
                return operation ==
                    JxAssetMigrator::
                        FileSystemOperation::
                            PrepareModProfileOutput;
            });
    AssetMigrationReport brokenProfileReport;
    const MigrationResult brokenProfileResult =
        migrator.migrate(
            root.filePath(
                "broken-profile-source"),
            root.filePath(
                "broken-profile-output"),
            profileOptions,
            brokenProfileReport);
    JxAssetMigrator::
        setFileSystemFaultInjectorForTests(
            {});
    const auto brokenProfileOutcome =
        std::find_if(
            brokenProfileReport.fileOutcomes.cbegin(),
            brokenProfileReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome&
                   outcome)
            {
                return outcome.sourceScan &&
                    outcome.outputPath ==
                        QStringLiteral(
                            "game_profile.ini");
            });
    const AssetResourceDomainReport
        brokenOtherDomain =
            brokenProfileReport.
                resourceDomains.value(
                    QStringLiteral("other"));
    ok = check(
        brokenProfileResult ==
                MigrationResult::Failed &&
            brokenProfileOutcome !=
                brokenProfileReport.
                    fileOutcomes.cend() &&
            brokenProfileOutcome->action ==
                AssetMigrationFileAction::Fail &&
            brokenProfileOutcome->reason ==
                QStringLiteral(
                    "source-mod-profile-merge-failed") &&
            brokenOtherDomain.writtenFiles == 0 &&
            brokenOtherDomain.failedFiles == 1 &&
            !QFileInfo::exists(
                root.filePath(
                    "broken-profile-output")),
        "a source profile merge failure reports the profile only as failed and removes its earlier staged-write count") &&
        ok;
    return ok;
}

bool testMigrationQuarantinesOnlyInvalidLuaScripts()
{
    QTemporaryDir directory;
    if (!check(
            directory.isValid(),
            "create Lua isolation migration fixture"))
    {
        return false;
    }

    QDir root(directory.path());
    bool ok = check(
        root.mkpath("source/script") &&
            root.mkpath(
                "source/.jxqy_migration_unavailable/"
                "script") &&
            root.mkpath("output/script") &&
            writeRawFile(
                root.filePath("source/script/good.lua"),
                QByteArray("local value = 1\nreturn value\n")) &&
            writeRawFile(
                root.filePath("source/script/bad.lua"),
                QByteArray("function broken(\n")) &&
            writeRawFile(
                root.filePath(
                    "source/.jxqy_migration_unavailable/"
                    "script/bad.lua.invalid"),
                QByteArray(
                    "source-owned-quarantine-file")) &&
            writeRawFile(
                root.filePath(
                    "output/.jxqy_asset_migration_marker"),
                QByteArray("old-marker")) &&
            writeRawFile(
                root.filePath("output/script/bad.lua"),
                QByteArray("return 'last-valid-generation'\n")),
        "write Lua isolation migration fixtures");
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = true;
    options.writeModProfile = false;
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result =
        migrator.migrate(
            root.filePath("source"),
            root.filePath("output"),
            options,
            report);
    const auto badOutcome = std::find_if(
        report.fileOutcomes.cbegin(),
        report.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return outcome.sourceScan &&
                outcome.sourcePath ==
                    QStringLiteral("script/bad.lua") &&
                outcome.action ==
                    AssetMigrationFileAction::Skip &&
                outcome.reason ==
                    QStringLiteral(
                        "lua-syntax-error-quarantined");
        });
    const QString quarantinePath =
        badOutcome ==
                report.fileOutcomes.cend()
            ? QString()
            : QDir(
                  root.filePath("output")).
                  filePath(
                      badOutcome->outputPath);
    const QString expectedQuarantinePath =
        QStringLiteral(
            ".jxqy_migration_unavailable/"
            "script/bad.lua.invalid.") +
        QString::fromLatin1(
            QCryptographicHash::hash(
                QByteArray(
                    "script/bad.lua"),
                QCryptographicHash::Sha256).
                toHex().
                left(12));
    ok = check(
        result == MigrationResult::Partial &&
            report.errorCount == 0 &&
            report.warningCount > 0 &&
            report.unavailableScripts ==
                QStringList{
                    QStringLiteral("script/bad.lua")} &&
            QFileInfo::exists(
                root.filePath("output/script/good.lua")) &&
            readRawFile(
                root.filePath("output/script/bad.lua")) ==
                QByteArray(
                    "return 'last-valid-generation'\n") &&
            readRawFile(
                root.filePath(
                    "output/.jxqy_migration_unavailable/"
                    "script/bad.lua.invalid")) ==
                QByteArray(
                    "source-owned-quarantine-file") &&
            badOutcome !=
                report.fileOutcomes.cend() &&
            badOutcome->outputPath ==
                expectedQuarantinePath &&
            readRawFile(quarantinePath) ==
                QByteArray("function broken(\n"),
        "one Lua syntax error uses a deterministic collision-free quarantine name, preserves both files, and publishes other valid resources") &&
        ok;

    ok = check(
        root.mkpath(
            "stable-quarantine-source/script") &&
            writeRawFile(
                root.filePath(
                    "stable-quarantine-source/script/"
                    "bad-stable.lua"),
                QByteArray(
                    "function broken_stable(\n")),
        "write repeated invalid-script quarantine fixture") &&
        ok;
    if (!ok)
        return false;

    const QString stableQuarantineSource =
        root.filePath(
            "stable-quarantine-source");
    const QString stableQuarantineOutput =
        root.filePath(
            "stable-quarantine-output");
    AssetMigrationReport firstStableReport;
    const MigrationResult firstStableResult =
        migrator.migrate(
            stableQuarantineSource,
            stableQuarantineOutput,
            options,
            firstStableReport);
    AssetMigrationReport secondStableReport;
    const MigrationResult secondStableResult =
        migrator.migrate(
            stableQuarantineSource,
            stableQuarantineOutput,
            options,
            secondStableReport);
    const auto stableOutcomePath =
        [](const AssetMigrationReport& migrationReport)
    {
        const auto outcome =
            std::find_if(
                migrationReport.fileOutcomes.cbegin(),
                migrationReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome&
                       candidate)
                {
                    return candidate.sourceScan &&
                        candidate.sourcePath ==
                            QStringLiteral(
                                "script/bad-stable.lua") &&
                        candidate.reason ==
                            QStringLiteral(
                                "lua-syntax-error-quarantined");
                });
        return outcome ==
                migrationReport.fileOutcomes.cend()
            ? QString()
            : outcome->outputPath;
    };
    const QString firstStableOutcomePath =
        stableOutcomePath(firstStableReport);
    const QString secondStableOutcomePath =
        stableOutcomePath(secondStableReport);
    QStringList stableQuarantineFiles;
    QDirIterator stableQuarantineIterator(
        QDir(stableQuarantineOutput).
            filePath(
                QStringLiteral(
                    ".jxqy_migration_unavailable")),
        QDir::Files |
            QDir::Hidden |
            QDir::System |
            QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (stableQuarantineIterator.hasNext())
    {
        stableQuarantineFiles.append(
            QDir::fromNativeSeparators(
                QDir(stableQuarantineOutput).
                    relativeFilePath(
                        stableQuarantineIterator.
                            next())));
    }
    ok = check(
        firstStableResult ==
                MigrationResult::Partial &&
            secondStableResult ==
                MigrationResult::Partial &&
            firstStableOutcomePath ==
                QStringLiteral(
                    ".jxqy_migration_unavailable/"
                    "script/bad-stable.lua.invalid") &&
            secondStableOutcomePath ==
                firstStableOutcomePath &&
            stableQuarantineFiles ==
                QStringList{
                    firstStableOutcomePath},
        "repeating the same invalid source reuses its intact managed quarantine path without creating sibling generations") &&
        ok;

    ok = check(
        root.mkpath(
            "root-file-source/script") &&
            writeRawFile(
                root.filePath(
                    "root-file-source/"
                    ".jxqy_migration_unavailable"),
                QByteArray(
                    "source-owned-root-file")) &&
            writeRawFile(
                root.filePath(
                    "root-file-source/script/"
                    "bad-root.lua"),
                QByteArray(
                    "function broken_root(\n")) &&
            writeRawFile(
                root.filePath(
                    "root-file-source/script/"
                    "good-root.lua"),
                QByteArray(
                    "return 'good-root'\n")),
        "write Lua quarantine root-shape collision fixture") &&
        ok;
    if (!ok)
        return false;

    AssetMigrationReport rootFileReport;
    const MigrationResult rootFileResult =
        migrator.migrate(
            root.filePath(
                "root-file-source"),
            root.filePath(
                "root-file-output"),
            options,
            rootFileReport);
    const auto rootFileOutcome =
        std::find_if(
            rootFileReport.fileOutcomes.cbegin(),
            rootFileReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome&
                   outcome)
            {
                return outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "script/bad-root.lua") &&
                    outcome.reason ==
                        QStringLiteral(
                            "lua-syntax-error-quarantined");
            });
    const QString rootFileSourceHash =
        QString::fromLatin1(
            QCryptographicHash::hash(
                QByteArray(
                    "script/bad-root.lua"),
                QCryptographicHash::Sha256).
                toHex().
                left(12));
    const QString expectedAlternatePath =
        QStringLiteral(
            ".jxqy_migration_unavailable-") +
        rootFileSourceHash +
        QStringLiteral(
            "/script/bad-root.lua.invalid");
    ok = check(
        rootFileResult ==
                MigrationResult::Partial &&
            rootFileReport.errorCount == 0 &&
            rootFileOutcome !=
                rootFileReport.fileOutcomes.cend() &&
            rootFileOutcome->outputPath ==
                expectedAlternatePath &&
            readRawFile(
                root.filePath(
                    "root-file-output/"
                    ".jxqy_migration_unavailable")) ==
                QByteArray(
                    "source-owned-root-file") &&
            readRawFile(
                QDir(
                    root.filePath(
                        "root-file-output")).
                    filePath(
                        expectedAlternatePath)) ==
                QByteArray(
                    "function broken_root(\n") &&
            QFileInfo::exists(
                root.filePath(
                    "root-file-output/script/"
                    "good-root.lua")),
        "a source file occupying the quarantine root switches one invalid script to a stable alternate root without blocking valid resources") &&
        ok;

    ok = check(
        root.mkpath(
            "partial-source/script") &&
            root.mkpath(
                "partial-output/script") &&
            writeRawFile(
                root.filePath(
                    "partial-source/script/"
                    "bad-partial.lua"),
                QByteArray(
                    "function broken_partial(\n")) &&
            writeRawFile(
                root.filePath(
                    "partial-source/script/"
                    "good-partial.lua"),
                QByteArray(
                    "return 'good-partial'\n")) &&
            writeRawFile(
                root.filePath(
                    "partial-output/"
                    ".jxqy_migration_unavailable"),
                QByteArray(
                    "existing-player-placeholder")) &&
            writeRawFile(
                root.filePath(
                    "partial-output/script/"
                    "keep.lua"),
                QByteArray(
                    "return 'keep-player'\n")),
        "write scripts-only quarantine/player-path fixture") &&
        ok;
    if (!ok)
        return false;

    AssetMigrationOptions partialOptions =
        options;
    partialOptions.resourceTypes = {
        AssetResourceType::Scripts};
    AssetMigrationReport partialReport;
    const MigrationResult partialResult =
        migrator.migrate(
            root.filePath(
                "partial-source"),
            root.filePath(
                "partial-output"),
            partialOptions,
            partialReport);
    const auto partialBadOutcome =
        std::find_if(
            partialReport.fileOutcomes.cbegin(),
            partialReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome&
                   outcome)
            {
                return outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "script/bad-partial.lua") &&
                    outcome.reason ==
                        QStringLiteral(
                            "lua-syntax-error-quarantined");
            });
    const QString partialSourceHash =
        QString::fromLatin1(
            QCryptographicHash::hash(
                QByteArray(
                    "script/bad-partial.lua"),
                QCryptographicHash::Sha256).
                toHex().
                left(12));
    const QString expectedPartialPath =
        QStringLiteral(
            ".jxqy_migration_unavailable-") +
        partialSourceHash +
        QStringLiteral(
            "/script/bad-partial.lua.invalid");
    ok = check(
        partialResult ==
                MigrationResult::Partial &&
            partialReport.errorCount == 0 &&
            partialBadOutcome !=
                partialReport.
                    fileOutcomes.cend() &&
            partialBadOutcome->outputPath ==
                expectedPartialPath &&
            readRawFile(
                root.filePath(
                    "partial-output/"
                    ".jxqy_migration_unavailable")) ==
                QByteArray(
                    "existing-player-placeholder") &&
            readRawFile(
                QDir(
                    root.filePath(
                        "partial-output")).
                    filePath(
                        expectedPartialPath)) ==
                QByteArray(
                    "function broken_partial(\n") &&
            QFileInfo::exists(
                root.filePath(
                    "partial-output/script/"
                    "good-partial.lua")) &&
            QFileInfo::exists(
                root.filePath(
                    "partial-output/script/"
                    "keep.lua")),
        "scripts-only migration preserves an unowned quarantine-root player file and publishes one invalid script through a disjoint managed root") &&
        ok;

    ok = check(
        root.mkpath(
            "partial-then-full-source/script") &&
            writeRawFile(
                root.filePath(
                    "partial-then-full-source/"
                    ".jxqy_migration_unavailable"),
                QByteArray(
                    "source-root-placeholder")) &&
            writeRawFile(
                root.filePath(
                    "partial-then-full-source/script/"
                    "bad-later-full.lua"),
                QByteArray(
                    "function broken_later_full(\n")) &&
            writeRawFile(
                root.filePath(
                    "partial-then-full-source/script/"
                    "good-later-full.lua"),
                QByteArray(
                    "return 'good-later-full'\n")),
        "write scripts-first then full-migration quarantine shape fixture") &&
        ok;
    if (!ok)
        return false;

    const QString partialThenFullSource =
        root.filePath(
            "partial-then-full-source");
    const QString partialThenFullOutput =
        root.filePath(
            "partial-then-full-output");
    AssetMigrationReport firstScriptsReport;
    const MigrationResult firstScriptsResult =
        migrator.migrate(
            partialThenFullSource,
            partialThenFullOutput,
            partialOptions,
            firstScriptsReport);
    const auto firstScriptsBadOutcome =
        std::find_if(
            firstScriptsReport.fileOutcomes.cbegin(),
            firstScriptsReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome&
                   outcome)
            {
                return outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "script/bad-later-full.lua") &&
                    outcome.reason ==
                        QStringLiteral(
                            "lua-syntax-error-quarantined");
            });
    const QString laterFullSourceHash =
        QString::fromLatin1(
            QCryptographicHash::hash(
                QByteArray(
                    "script/bad-later-full.lua"),
                QCryptographicHash::Sha256).
                toHex().
                left(12));
    const QString expectedLaterFullPath =
        QStringLiteral(
            ".jxqy_migration_unavailable-") +
        laterFullSourceHash +
        QStringLiteral(
            "/script/bad-later-full.lua.invalid");
    ok = check(
        firstScriptsResult ==
                MigrationResult::Partial &&
            firstScriptsReport.errorCount == 0 &&
            firstScriptsBadOutcome !=
                firstScriptsReport.
                    fileOutcomes.cend() &&
            firstScriptsBadOutcome->outputPath ==
                expectedLaterFullPath &&
            !QFileInfo::exists(
                QDir(partialThenFullOutput).
                    filePath(
                        QStringLiteral(
                            ".jxqy_migration_unavailable"))) &&
            readRawFile(
                QDir(partialThenFullOutput).
                    filePath(
                        expectedLaterFullPath)) ==
                QByteArray(
                    "function broken_later_full(\n") &&
            QFileInfo::exists(
                QDir(partialThenFullOutput).
                    filePath(
                        QStringLiteral(
                            "script/good-later-full.lua"))),
        "scripts-only migration reserves the skipped source root shape and uses an alternate quarantine root") &&
        ok;

    AssetMigrationReport laterFullReport;
    const MigrationResult laterFullResult =
        migrator.migrate(
            partialThenFullSource,
            partialThenFullOutput,
            options,
            laterFullReport);
    const auto laterFullBadOutcome =
        std::find_if(
            laterFullReport.fileOutcomes.cbegin(),
            laterFullReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome&
                   outcome)
            {
                return outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "script/bad-later-full.lua") &&
                    outcome.reason ==
                        QStringLiteral(
                            "lua-syntax-error-quarantined");
            });
    ok = check(
        laterFullResult ==
                MigrationResult::Partial &&
            laterFullReport.errorCount == 0 &&
            laterFullBadOutcome !=
                laterFullReport.fileOutcomes.cend() &&
            laterFullBadOutcome->outputPath ==
                expectedLaterFullPath &&
            readRawFile(
                QDir(partialThenFullOutput).
                    filePath(
                        QStringLiteral(
                            ".jxqy_migration_unavailable"))) ==
                QByteArray(
                    "source-root-placeholder") &&
            readRawFile(
                QDir(partialThenFullOutput).
                    filePath(
                        expectedLaterFullPath)) ==
                QByteArray(
                    "function broken_later_full(\n") &&
            QFileInfo::exists(
                QDir(partialThenFullOutput).
                    filePath(
                        QStringLiteral(
                            "script/good-later-full.lua"))),
        "a later full migration publishes the skipped source placeholder without colliding with the stable alternate quarantine root") &&
        ok;
    return ok;
}

bool testMigrationPublishFaultMatrix()
{
    using Operation = JxAssetMigrator::FileSystemOperation;
    struct FaultReset
    {
        ~FaultReset()
        {
            JxAssetMigrator::setFileSystemFaultInjectorForTests({});
        }
    } faultReset;

    struct ObservedOperation
    {
        Operation operation;
        QString sourcePath;
        QString targetPath;
    };

    QTemporaryDir temporaryDirectory;
    if (!check(temporaryDirectory.isValid(),
               "create migration publish fault-matrix directory"))
    {
        return false;
    }

    QDir root(temporaryDirectory.path());
    const QString fullSource = root.filePath(QStringLiteral("full-source"));
    const QString scriptsSource = root.filePath(QStringLiteral("scripts-source"));
    if (!check(root.mkpath(QStringLiteral("full-source/sound")) &&
                   root.mkpath(QStringLiteral("scripts-source/script")) &&
                   writeRawFile(root.filePath(QStringLiteral("full-source/sound/new.bin")),
                                QByteArray("new-full")) &&
                   writeRawFile(root.filePath(QStringLiteral("scripts-source/script/new.txt")),
                                QByteArray("new-script")),
               "write migration publish fault-matrix sources"))
    {
        return false;
    }

    auto siblingPaths = [](const QString& outputPath, const QString& label)
    {
        const QFileInfo outputInfo(outputPath);
        QDir parent(outputInfo.absolutePath());
        const QString pattern = outputInfo.fileName() + "." + label + "-*";
        QStringList paths;
        const QFileInfoList entries = parent.entryInfoList(
            {pattern}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries)
            paths.append(entry.absoluteFilePath());
        return paths;
    };
    auto stagingPaths = [](const QString& outputPath)
    {
        const QFileInfo outputInfo(outputPath);
        QDir parent(outputInfo.absolutePath());
        const QString pattern = "." + outputInfo.fileName() + ".migration-*";
        QStringList paths;
        const QFileInfoList entries = parent.entryInfoList(
            {pattern}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries)
            paths.append(entry.absoluteFilePath());
        return paths;
    };
    auto prepareFullOutput = [&](const QString& name, const QByteArray& stableBytes)
    {
        return root.mkpath(name) &&
            writeRawFile(root.filePath(name + "/.jxqy_asset_migration_marker"),
                         QByteArray("old-marker")) &&
            writeRawFile(root.filePath(name + "/stable.bin"), stableBytes);
    };
    auto prepareScriptsOutput = [&](const QString& name)
    {
        return root.mkpath(name + "/script") &&
            writeRawFile(root.filePath(name + "/script/keep.txt"),
                         QByteArray("old-script")) &&
            writeRawFile(root.filePath(name + "/migration_report.txt"),
                         QByteArray("old-report"));
    };

    AssetMigrationOptions fullOptions;
    fullOptions.convertScript = false;
    fullOptions.writeModProfile = false;
    AssetMigrationOptions scriptsOptions;
    scriptsOptions.resourceTypes = {AssetResourceType::Scripts};
    scriptsOptions.convertScript = false;
    scriptsOptions.writeModProfile = false;

    JxAssetMigrator migrator;
    bool ok = true;

    const QString backupFailureOutput = root.filePath(QStringLiteral("backup-failure"));
    ok = check(prepareFullOutput(QStringLiteral("backup-failure"),
                                QByteArray("stable-backup")),
               "prepare full output for root backup failure") && ok;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [](Operation operation, const QString&, const QString&)
        {
            return operation == Operation::BackupRoot;
        });
    AssetMigrationReport backupFailureReport;
    const MigrationResult backupFailureResult = migrator.migrate(
        fullSource, backupFailureOutput, fullOptions, backupFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    ok = check(backupFailureResult == MigrationResult::Failed &&
                   readRawFile(root.filePath(QStringLiteral(
                       "backup-failure/stable.bin"))) == "stable-backup" &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "backup-failure/sound/new.bin"))) &&
                   siblingPaths(backupFailureOutput, QStringLiteral("migration-backup")).isEmpty() &&
                   !stagingPaths(backupFailureOutput).isEmpty(),
                "root backup failure preserves old output and diagnostic staging") && ok;

    const QString concurrentRootOutput =
        root.filePath(
            QStringLiteral(
                "concurrent-root-change"));
    ok = check(
        prepareFullOutput(
            QStringLiteral(
                "concurrent-root-change"),
            QByteArray("stable-before-detach")),
        "prepare full output for pre-publish concurrent change") &&
        ok;
    bool rootChangeInjected = false;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation,
            const QString& sourcePath,
            const QString&)
        {
            if (operation ==
                    Operation::BackupRoot &&
                !rootChangeInjected)
            {
                rootChangeInjected = true;
                writeRawFile(
                    QDir(sourcePath).filePath(
                        QStringLiteral(
                            "stable.bin")),
                    QByteArray(
                        "late-player-root-edit"));
            }
            return false;
        });
    AssetMigrationReport concurrentRootReport;
    const MigrationResult concurrentRootResult =
        migrator.migrate(
            fullSource,
            concurrentRootOutput,
            fullOptions,
            concurrentRootReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    const auto concurrentRootOutcome =
        std::find_if(
            concurrentRootReport.fileOutcomes.cbegin(),
            concurrentRootReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "stable.bin") &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-output-changed-before-publish");
            });
    ok = check(
        rootChangeInjected &&
            concurrentRootResult ==
                MigrationResult::Failed &&
            readRawFile(
                QDir(concurrentRootOutput).
                    filePath(
                        QStringLiteral(
                            "stable.bin"))) ==
                QByteArray(
                    "late-player-root-edit") &&
            !QFileInfo::exists(
                QDir(concurrentRootOutput).
                    filePath(
                        QStringLiteral(
                            "sound/new.bin"))) &&
            siblingPaths(
                concurrentRootOutput,
                QStringLiteral(
                    "migration-backup")).
                isEmpty() &&
            !stagingPaths(
                concurrentRootOutput).isEmpty() &&
            concurrentRootOutcome !=
                concurrentRootReport.
                    fileOutcomes.cend(),
        "detached full-root snapshot detects a late player edit and restores it before publishing staging") &&
        ok;

    const QString publishFailureOutput = root.filePath(QStringLiteral("publish-failure"));
    ok = check(prepareFullOutput(QStringLiteral("publish-failure"),
                                QByteArray("stable-publish")),
               "prepare full output for root publish failure") && ok;
    QList<ObservedOperation> rootPublishOperations;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation, const QString& sourcePath, const QString& targetPath)
        {
            rootPublishOperations.append({operation, sourcePath, targetPath});
            return operation == Operation::PublishRoot;
        });
    AssetMigrationReport publishFailureReport;
    const MigrationResult publishFailureResult = migrator.migrate(
        fullSource, publishFailureOutput, fullOptions, publishFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    const auto sawRootOperation = [&](Operation operation)
    {
        return std::any_of(rootPublishOperations.cbegin(), rootPublishOperations.cend(),
            [operation](const ObservedOperation& item)
            {
                return item.operation == operation;
            });
    };
    ok = check(publishFailureResult == MigrationResult::Failed &&
                   sawRootOperation(Operation::BackupRoot) &&
                   sawRootOperation(Operation::PublishRoot) &&
                   sawRootOperation(Operation::RestoreRoot) &&
                   readRawFile(root.filePath(QStringLiteral(
                       "publish-failure/stable.bin"))) == "stable-publish" &&
                   siblingPaths(publishFailureOutput, QStringLiteral("migration-backup")).isEmpty(),
               "root publish failure restores the previous output from backup") && ok;

    const QString restoreFailureOutput = root.filePath(QStringLiteral("restore-failure"));
    ok = check(prepareFullOutput(QStringLiteral("restore-failure"),
                                QByteArray("stable-restore")),
               "prepare full output for root restore failure") && ok;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [](Operation operation, const QString&, const QString&)
        {
            return operation == Operation::PublishRoot ||
                operation == Operation::RestoreRoot;
        });
    AssetMigrationReport restoreFailureReport;
    const MigrationResult restoreFailureResult = migrator.migrate(
        fullSource, restoreFailureOutput, fullOptions, restoreFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    const QStringList retainedRootBackups = siblingPaths(
        restoreFailureOutput, QStringLiteral("migration-backup"));
    ok = check(restoreFailureResult == MigrationResult::Failed &&
                   !QFileInfo::exists(restoreFailureOutput) &&
                   retainedRootBackups.size() == 1 &&
                   readRawFile(QDir(retainedRootBackups.first()).filePath(
                       QStringLiteral("stable.bin"))) == "stable-restore" &&
                   restoreFailureReport.logLines.join('\n').contains(
                       QString::fromUtf8("备份仍位于")),
               "root restore failure retains the complete previous output backup") && ok;

    const QString cleanupFailureOutput = root.filePath(QStringLiteral("cleanup-failure"));
    ok = check(prepareFullOutput(QStringLiteral("cleanup-failure"),
                                QByteArray("stable-cleanup")),
               "prepare full output for backup cleanup failure") && ok;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [](Operation operation, const QString&, const QString&)
        {
            return operation == Operation::RemoveBackup;
        });
    AssetMigrationReport cleanupFailureReport;
    const MigrationResult cleanupFailureResult = migrator.migrate(
        fullSource, cleanupFailureOutput, fullOptions, cleanupFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    const QStringList retainedCleanupBackups = siblingPaths(
        cleanupFailureOutput, QStringLiteral("migration-backup"));
    ok = check(cleanupFailureResult == MigrationResult::Partial,
               "backup cleanup failure returns Partial") && ok;
    ok = check(readRawFile(root.filePath(QStringLiteral(
                   "cleanup-failure/sound/new.bin"))) == "new-full",
               "backup cleanup failure keeps the newly published output") && ok;
    ok = check(retainedCleanupBackups.size() == 1,
               "backup cleanup failure retains one old-output backup") && ok;
    if (retainedCleanupBackups.size() == 1)
    {
        ok = check(readRawFile(QDir(retainedCleanupBackups.first()).filePath(
                       QStringLiteral("stable.bin"))) == "stable-cleanup",
                   "retained cleanup-failure backup contains the old bytes") && ok;
    }
    ok = check(readUtf8TextFile(root.filePath(QStringLiteral(
                   "cleanup-failure/migration_report.json"))).contains(
                       QStringLiteral("\"status\": \"Partial\"")),
               "backup cleanup failure rewrites the final JSON report as Partial") && ok;

    const QString backupLinkRefusalOutput =
        root.filePath(
            QStringLiteral(
                "backup-link-refusal"));
    const QString backupLinkTarget =
        root.filePath(
            QStringLiteral(
                "backup-link-target"));
    ok = check(
        prepareFullOutput(
            QStringLiteral(
                "backup-link-refusal"),
            QByteArray(
                "stable-link-refusal")) &&
            root.mkpath(
                QStringLiteral(
                    "backup-link-target")) &&
            writeRawFile(
                QDir(backupLinkTarget).
                    filePath(
                        QStringLiteral(
                            "outside-sentinel.bin")),
                QByteArray(
                    "must-not-delete")),
        "prepare full output and external target for backup-link cleanup refusal") &&
        ok;
    bool backupLinkReplacementAttempted = false;
    bool backupLinkOldRootMoved = false;
    bool backupLinkCreated = false;
    QString backupLinkError;
    QString replacedBackupPath;
    QString movedBackupPath;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation,
            const QString& sourcePath,
            const QString&)
        {
            if (operation ==
                    Operation::RemoveBackup &&
                !backupLinkReplacementAttempted)
            {
                backupLinkReplacementAttempted = true;
                replacedBackupPath =
                    sourcePath;
                movedBackupPath =
                    sourcePath +
                    QStringLiteral(
                        ".detached-before-link");
                backupLinkOldRootMoved =
                    QDir().rename(
                        sourcePath,
                        movedBackupPath);
                if (backupLinkOldRootMoved)
                {
                    backupLinkCreated =
                        createDirectoryLink(
                            backupLinkTarget,
                            sourcePath,
                            backupLinkError);
                }
            }
            return false;
        });
    AssetMigrationReport backupLinkRefusalReport;
    const MigrationResult backupLinkRefusalResult =
        migrator.migrate(
            fullSource,
            backupLinkRefusalOutput,
            fullOptions,
            backupLinkRefusalReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        {});
    if (backupLinkCreated)
    {
        ok = check(
            backupLinkReplacementAttempted &&
                backupLinkOldRootMoved &&
                backupLinkRefusalResult ==
                    MigrationResult::Partial &&
                backupLinkRefusalReport.
                    retainedBackupPath ==
                    replacedBackupPath &&
                QFileInfo(
                    replacedBackupPath).
                    isSymLink() &&
                QFileInfo::exists(
                    movedBackupPath) &&
                readRawFile(
                    QDir(backupLinkTarget).
                        filePath(
                            QStringLiteral(
                                "outside-sentinel.bin"))) ==
                    QByteArray(
                        "must-not-delete") &&
                readRawFile(
                    QDir(backupLinkRefusalOutput).
                        filePath(
                            QStringLiteral(
                                "sound/new.bin"))) ==
                    QByteArray(
                        "new-full") &&
                backupLinkRefusalReport.
                    logLines.join(
                        QLatin1Char('\n')).
                    contains(
                        QStringLiteral(
                            "reparse point")),
            "backup cleanup refuses a replaced filesystem link without following it and keeps the published output") &&
            ok;
    }
    else
    {
        std::cout
            << "(backup filesystem-link cleanup refusal check skipped: "
            << backupLinkError.toStdString()
            << ")\n";
    }

    const QString changedBackupRootOutput =
        root.filePath(
            QStringLiteral(
                "changed-backup-root"));
    ok = check(
        prepareFullOutput(
            QStringLiteral(
                "changed-backup-root"),
            QByteArray(
                "stable-before-cleanup")),
        "prepare full output for post-publish backup change") &&
        ok;
    bool rootCleanupMutationAttempted = false;
    bool rootCleanupMutationWritten = false;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation,
            const QString& sourcePath,
            const QString&)
        {
            if (operation ==
                    Operation::RemoveBackup &&
                !rootCleanupMutationAttempted)
            {
                rootCleanupMutationAttempted = true;
                rootCleanupMutationWritten =
                    writeRawFile(
                        QDir(sourcePath).filePath(
                            QStringLiteral(
                                "stable.bin")),
                        QByteArray(
                            "late-backup-root-edit"));
            }
            return false;
        });
    AssetMigrationReport changedBackupRootReport;
    const MigrationResult changedBackupRootResult =
        migrator.migrate(
            fullSource,
            changedBackupRootOutput,
            fullOptions,
            changedBackupRootReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        {});
    const QStringList changedRootBackups =
        siblingPaths(
            changedBackupRootOutput,
            QStringLiteral(
                "migration-backup"));
    const QString changedRootLog =
        changedBackupRootReport.logLines.join(
            QLatin1Char('\n'));
    const QString changedRootTextReport =
        readUtf8TextFile(
            QDir(changedBackupRootOutput).
                filePath(
                    QStringLiteral(
                        "migration_report.txt")));
    const QString changedRootJsonReport =
        readUtf8TextFile(
            QDir(changedBackupRootOutput).
                filePath(
                    QStringLiteral(
                        "migration_report.json")));
    ok = check(
        rootCleanupMutationAttempted &&
            rootCleanupMutationWritten,
        "post-publish full cleanup hook mutates the detached backup") &&
        ok;
    ok = check(
        changedBackupRootResult ==
            MigrationResult::Partial,
        "post-publish full backup change returns Partial") &&
        ok;
    ok = check(
        readRawFile(
            QDir(changedBackupRootOutput).
                filePath(
                    QStringLiteral(
                        "sound/new.bin"))) ==
            QByteArray("new-full") &&
            readRawFile(
                QDir(changedBackupRootOutput).
                    filePath(
                        QStringLiteral(
                            "stable.bin"))) ==
                QByteArray(
                    "stable-before-cleanup"),
        "post-publish full backup change keeps the complete new output") &&
        ok;
    ok = check(
        changedRootBackups.size() == 1 &&
            readRawFile(
                QDir(changedRootBackups.value(0)).
                    filePath(
                        QStringLiteral(
                            "stable.bin"))) ==
                QByteArray(
                    "late-backup-root-edit"),
        "post-publish full backup change retains the externally changed backup") &&
        ok;
    ok = check(
        changedRootLog.contains(
            QString::fromUtf8(
                "发布后旧输出备份发生变化")) &&
            changedRootLog.contains(
                QStringLiteral("stable.bin")) &&
            changedRootLog.contains(
                changedRootBackups.value(0)),
        "post-publish full backup change is explained with the changed path and retained backup path") &&
        ok;
    ok = check(
        changedRootTextReport.contains(
            QString::fromUtf8(
                "发布后旧输出备份发生变化")) &&
            changedRootTextReport.contains(
                changedRootBackups.value(0)) &&
            changedRootJsonReport.contains(
                QStringLiteral(
                    "\"status\": \"Partial\"")) &&
            changedRootJsonReport.contains(
                QString::fromUtf8(
                    "发布后旧输出备份发生变化")) &&
            changedRootJsonReport.contains(
                changedRootBackups.value(0)),
        "post-publish full backup change is written to the final text and JSON reports") &&
        ok;

    for (Operation rewriteFailureOperation : {
             Operation::CommitTextReport,
             Operation::CommitJsonReport})
    {
        const bool textFailure =
            rewriteFailureOperation ==
            Operation::CommitTextReport;
        const QString outputName =
            textFailure
            ? QStringLiteral(
                  "post-publish-text-report-failure")
            : QStringLiteral(
                  "post-publish-json-report-failure");
        const QString outputPath =
            root.filePath(outputName);
        ok = check(
            prepareFullOutput(
                outputName,
                QByteArray(
                    "stable-report-rewrite")),
            "prepare output for post-publish report rewrite failure") &&
            ok;
        int rewriteOperationCount = 0;
        bool rewriteBackupMutated = false;
        JxAssetMigrator::setFileSystemFaultInjectorForTests(
            [&](Operation operation,
                const QString& sourcePath,
                const QString&)
            {
                if (operation ==
                        Operation::RemoveBackup &&
                    !rewriteBackupMutated)
                {
                    rewriteBackupMutated =
                        writeRawFile(
                            QDir(sourcePath).
                                filePath(
                                    QStringLiteral(
                                        "stable.bin")),
                            QByteArray(
                                "late-report-rewrite-edit"));
                }
                if (operation ==
                    rewriteFailureOperation)
                {
                    ++rewriteOperationCount;
                    return rewriteOperationCount >= 2;
                }
                return false;
            });
        AssetMigrationReport rewriteFailureReport;
        const MigrationResult rewriteFailureResult =
            migrator.migrate(
                fullSource,
                outputPath,
                fullOptions,
                rewriteFailureReport);
        JxAssetMigrator::
            setFileSystemFaultInjectorForTests(
                {});
        const QStringList rewriteBackups =
            siblingPaths(
                outputPath,
                QStringLiteral(
                    "migration-backup"));
        const QString trustedReportPath =
            textFailure
            ? rewriteFailureReport.
                  reportJsonFilePath
            : rewriteFailureReport.
                  reportFilePath;
        const QString untrustedReportPath =
            textFailure
            ? rewriteFailureReport.
                  reportFilePath
            : rewriteFailureReport.
                  reportJsonFilePath;
        const QString trustedReport =
            readUtf8TextFile(
                trustedReportPath);
        ok = check(
            rewriteBackupMutated &&
                rewriteOperationCount >= 3 &&
                rewriteFailureResult ==
                    MigrationResult::Failed &&
                readRawFile(
                    QDir(outputPath).
                        filePath(
                            QStringLiteral(
                                "sound/new.bin"))) ==
                    QByteArray("new-full") &&
                rewriteBackups.size() == 1 &&
                untrustedReportPath.isEmpty() &&
                !trustedReportPath.isEmpty() &&
                trustedReport.contains(
                    QString::fromUtf8(
                        "最终 Partial 报告重写失败")) &&
                (!textFailure ||
                 trustedReport.contains(
                     QStringLiteral(
                         "\"status\": \"Failed\""))),
            "a persistent second-pass report commit failure returns Failed, keeps the published output and backup, and clears the stale report path") &&
            ok;
    }

    for (Operation reportOperation : {
             Operation::CommitTextReport, Operation::CommitJsonReport})
    {
        const QString suffix = reportOperation == Operation::CommitTextReport
            ? QStringLiteral("text") : QStringLiteral("json");
        const QString outputName = QStringLiteral("report-") + suffix +
            QStringLiteral("-failure");
        const QString outputPath = root.filePath(outputName);
        ok = check(prepareFullOutput(outputName, QByteArray("stable-report-" +
                                    suffix.toUtf8())),
                   "prepare full output for report commit failure") && ok;
        JxAssetMigrator::setFileSystemFaultInjectorForTests(
            [reportOperation](Operation operation, const QString&, const QString&)
            {
                return operation == reportOperation;
            });
        AssetMigrationReport reportCommitFailure;
        const MigrationResult reportCommitResult = migrator.migrate(
            fullSource, outputPath, fullOptions, reportCommitFailure);
        JxAssetMigrator::setFileSystemFaultInjectorForTests({});
        ok = check(reportCommitResult == MigrationResult::Failed &&
                       readRawFile(QDir(outputPath).filePath(QStringLiteral("stable.bin"))) ==
                           QByteArray("stable-report-" + suffix.toUtf8()) &&
                       !QFileInfo::exists(QDir(outputPath).filePath(
                           QStringLiteral("sound/new.bin"))) &&
                       !stagingPaths(outputPath).isEmpty(),
                   "report commit failure prevents publication and preserves old output") && ok;
    }

    const QString entryBackupFailureOutput = root.filePath(
        QStringLiteral("entry-backup-failure"));
    ok = check(prepareScriptsOutput(QStringLiteral("entry-backup-failure")),
               "prepare scripts output for entry backup failure") && ok;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [](Operation operation, const QString&, const QString&)
        {
            return operation == Operation::BackupEntry;
        });
    AssetMigrationReport entryBackupFailureReport;
    const MigrationResult entryBackupFailureResult = migrator.migrate(
        scriptsSource, entryBackupFailureOutput, scriptsOptions,
        entryBackupFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    ok = check(entryBackupFailureResult == MigrationResult::Failed &&
                   readRawFile(QDir(entryBackupFailureOutput).filePath(
                       QStringLiteral("script/keep.txt"))) == "old-script" &&
                   readRawFile(QDir(entryBackupFailureOutput).filePath(
                       QStringLiteral("migration_report.txt"))) == "old-report" &&
                   siblingPaths(entryBackupFailureOutput,
                                QStringLiteral("migration-backup")).isEmpty(),
                "entry backup failure leaves every published subtree unchanged") && ok;

    const QString concurrentEntryOutput =
        root.filePath(
            QStringLiteral(
                "concurrent-entry-change"));
    ok = check(
        prepareScriptsOutput(
            QStringLiteral(
                "concurrent-entry-change")),
        "prepare partial output for pre-publish concurrent change") &&
        ok;
    bool entryChangeInjected = false;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation,
            const QString& sourcePath,
            const QString&)
        {
            if (operation ==
                    Operation::BackupEntry &&
                QFileInfo(sourcePath).fileName() ==
                    QStringLiteral("script") &&
                !entryChangeInjected)
            {
                entryChangeInjected = true;
                writeRawFile(
                    QDir(sourcePath).filePath(
                        QStringLiteral(
                            "keep.txt")),
                    QByteArray(
                        "late-player-entry-edit"));
            }
            return false;
        });
    AssetMigrationReport concurrentEntryReport;
    const MigrationResult concurrentEntryResult =
        migrator.migrate(
            scriptsSource,
            concurrentEntryOutput,
            scriptsOptions,
            concurrentEntryReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    const auto concurrentEntryOutcome =
        std::find_if(
            concurrentEntryReport.fileOutcomes.cbegin(),
            concurrentEntryReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "script/keep.txt") &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "existing-output-changed-before-publish");
            });
    ok = check(
        entryChangeInjected &&
            concurrentEntryResult ==
                MigrationResult::Failed &&
            readRawFile(
                QDir(concurrentEntryOutput).
                    filePath(
                        QStringLiteral(
                            "script/keep.txt"))) ==
                QByteArray(
                    "late-player-entry-edit") &&
            readRawFile(
                QDir(concurrentEntryOutput).
                    filePath(
                        QStringLiteral(
                            "migration_report.txt"))) ==
                QByteArray("old-report") &&
            !QFileInfo::exists(
                QDir(concurrentEntryOutput).
                    filePath(
                        QStringLiteral(
                            "script/new.txt"))) &&
            siblingPaths(
                concurrentEntryOutput,
                QStringLiteral(
                    "migration-backup")).
                isEmpty() &&
            concurrentEntryOutcome !=
                concurrentEntryReport.
                    fileOutcomes.cend(),
        "two-phase partial publish detects a late player edit and restores every selected old entry before publishing staging") &&
        ok;

    const QString changedBackupEntryOutput =
        root.filePath(
            QStringLiteral(
                "changed-backup-entry"));
    ok = check(
        prepareScriptsOutput(
            QStringLiteral(
                "changed-backup-entry")),
        "prepare partial output for post-publish backup change") &&
        ok;
    bool entryCleanupMutationAttempted = false;
    bool entryCleanupMutationWritten = false;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation,
            const QString& sourcePath,
            const QString&)
        {
            if (operation ==
                    Operation::RemoveBackup &&
                !entryCleanupMutationAttempted)
            {
                entryCleanupMutationAttempted = true;
                entryCleanupMutationWritten =
                    writeRawFile(
                        QDir(sourcePath).filePath(
                            QStringLiteral(
                                "script/keep.txt")),
                        QByteArray(
                            "late-backup-entry-edit"));
            }
            return false;
        });
    AssetMigrationReport changedBackupEntryReport;
    const MigrationResult changedBackupEntryResult =
        migrator.migrate(
            scriptsSource,
            changedBackupEntryOutput,
            scriptsOptions,
            changedBackupEntryReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        {});
    const QStringList changedEntryBackups =
        siblingPaths(
            changedBackupEntryOutput,
            QStringLiteral(
                "migration-backup"));
    const QString changedEntryLog =
        changedBackupEntryReport.logLines.join(
            QLatin1Char('\n'));
    const QString changedEntryTextReport =
        readUtf8TextFile(
            QDir(changedBackupEntryOutput).
                filePath(
                    QStringLiteral(
                        "migration_report.txt")));
    const QString changedEntryJsonReport =
        readUtf8TextFile(
            QDir(changedBackupEntryOutput).
                filePath(
                    QStringLiteral(
                        "migration_report.json")));
    ok = check(
        entryCleanupMutationAttempted &&
            entryCleanupMutationWritten,
        "post-publish partial cleanup hook mutates the detached backup") &&
        ok;
    ok = check(
        changedBackupEntryResult ==
            MigrationResult::Partial,
        "post-publish partial backup change returns Partial") &&
        ok;
    ok = check(
        readRawFile(
            QDir(changedBackupEntryOutput).
                filePath(
                    QStringLiteral(
                        "script/new.txt"))) ==
            QByteArray("new-script") &&
            readRawFile(
                QDir(changedBackupEntryOutput).
                    filePath(
                        QStringLiteral(
                            "script/keep.txt"))) ==
                QByteArray("old-script"),
        "post-publish partial backup change keeps the selected new output") &&
        ok;
    ok = check(
        changedEntryBackups.size() == 1 &&
            readRawFile(
                QDir(changedEntryBackups.value(0)).
                    filePath(
                        QStringLiteral(
                            "script/keep.txt"))) ==
                QByteArray(
                    "late-backup-entry-edit"),
        "post-publish partial backup change retains the externally changed backup") &&
        ok;
    ok = check(
        changedEntryLog.contains(
            QString::fromUtf8(
                "发布后旧输出备份发生变化")) &&
            changedEntryLog.contains(
                QStringLiteral(
                    "script/keep.txt")) &&
            changedEntryLog.contains(
                changedEntryBackups.value(0)),
        "post-publish partial backup change is explained with the changed path and retained backup path") &&
        ok;
    ok = check(
        changedEntryTextReport.contains(
            QString::fromUtf8(
                "发布后旧输出备份发生变化")) &&
            changedEntryTextReport.contains(
                changedEntryBackups.value(0)) &&
            changedEntryJsonReport.contains(
                QStringLiteral(
                    "\"status\": \"Partial\"")) &&
            changedEntryJsonReport.contains(
                QString::fromUtf8(
                    "发布后旧输出备份发生变化")) &&
            changedEntryJsonReport.contains(
                changedEntryBackups.value(0)),
        "post-publish partial backup change is written to the final text and JSON reports") &&
        ok;

    const QString reverseRollbackOutput = root.filePath(
        QStringLiteral("reverse-rollback"));
    ok = check(prepareScriptsOutput(QStringLiteral("reverse-rollback")),
               "prepare scripts output for reverse rollback") && ok;
    QList<ObservedOperation> entryOperations;
    int publishEntryCount = 0;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation, const QString& sourcePath, const QString& targetPath)
        {
            entryOperations.append({operation, sourcePath, targetPath});
            if (operation == Operation::PublishEntry)
                return ++publishEntryCount == 2;
            return false;
        });
    AssetMigrationReport reverseRollbackReport;
    const MigrationResult reverseRollbackResult = migrator.migrate(
        scriptsSource, reverseRollbackOutput, scriptsOptions,
        reverseRollbackReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    int failedPublishIndex = -1;
    int reportRestoreIndex = -1;
    int scriptRollbackIndex = -1;
    int scriptRestoreIndex = -1;
    int seenPublishEntries = 0;
    for (int index = 0; index < entryOperations.size(); ++index)
    {
        const ObservedOperation& item = entryOperations[index];
        if (item.operation == Operation::PublishEntry &&
            ++seenPublishEntries == 2)
        {
            failedPublishIndex = index;
        }
        else if (index > failedPublishIndex && failedPublishIndex >= 0 &&
                 item.operation == Operation::RestoreEntry &&
                 QFileInfo(item.targetPath).fileName() == QStringLiteral("migration_report.txt"))
        {
            reportRestoreIndex = index;
        }
        else if (index > reportRestoreIndex && reportRestoreIndex >= 0 &&
                 item.operation == Operation::RollbackPublishedEntry &&
                 QFileInfo(item.sourcePath).fileName() == QStringLiteral("script"))
        {
            scriptRollbackIndex = index;
        }
        else if (index > scriptRollbackIndex && scriptRollbackIndex >= 0 &&
                 item.operation == Operation::RestoreEntry &&
                 QFileInfo(item.targetPath).fileName() == QStringLiteral("script"))
        {
            scriptRestoreIndex = index;
        }
    }
    ok = check(reverseRollbackResult == MigrationResult::Failed &&
                   readRawFile(QDir(reverseRollbackOutput).filePath(
                       QStringLiteral("script/keep.txt"))) == "old-script" &&
                   !QFileInfo::exists(QDir(reverseRollbackOutput).filePath(
                       QStringLiteral("script/new.txt"))) &&
                   readRawFile(QDir(reverseRollbackOutput).filePath(
                       QStringLiteral("migration_report.txt"))) == "old-report" &&
                   failedPublishIndex >= 0 &&
                   reportRestoreIndex > failedPublishIndex &&
                   scriptRollbackIndex > reportRestoreIndex &&
                   scriptRestoreIndex > scriptRollbackIndex &&
                   siblingPaths(reverseRollbackOutput,
                                QStringLiteral("migration-backup")).isEmpty(),
               "second entry publish failure rolls back later-to-earlier and restores old bytes") && ok;

    const QString rollbackFailureOutput = root.filePath(
        QStringLiteral("rollback-failure"));
    ok = check(prepareScriptsOutput(QStringLiteral("rollback-failure")),
               "prepare scripts output for rollback failure") && ok;
    publishEntryCount = 0;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation, const QString& sourcePath, const QString&)
        {
            if (operation == Operation::PublishEntry)
                return ++publishEntryCount == 2;
            return operation == Operation::RollbackPublishedEntry &&
                QFileInfo(sourcePath).fileName() == QStringLiteral("script");
        });
    AssetMigrationReport rollbackFailureReport;
    const MigrationResult rollbackFailureResult = migrator.migrate(
        scriptsSource, rollbackFailureOutput, scriptsOptions,
        rollbackFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    const QStringList retainedEntryBackups = siblingPaths(
        rollbackFailureOutput, QStringLiteral("migration-backup"));
    ok = check(rollbackFailureResult == MigrationResult::Failed &&
                   readRawFile(QDir(rollbackFailureOutput).filePath(
                       QStringLiteral("script/new.txt"))) == "new-script" &&
                   retainedEntryBackups.size() == 1 &&
                   readRawFile(QDir(retainedEntryBackups.first()).filePath(
                       QStringLiteral("script/keep.txt"))) == "old-script" &&
                   rollbackFailureReport.logLines.join('\n').contains(
                       QString::fromUtf8("回滚未完全成功")),
               "rollback failure is explicit and retains the previous subtree backup") && ok;

    const QString newOutputRollback = root.filePath(
        QStringLiteral("new-output-rollback"));
    publishEntryCount = 0;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation, const QString&, const QString&)
        {
            if (operation == Operation::PublishEntry)
                return ++publishEntryCount == 2;
            return false;
        });
    AssetMigrationReport newOutputRollbackReport;
    const MigrationResult newOutputRollbackResult = migrator.migrate(
        scriptsSource, newOutputRollback, scriptsOptions,
        newOutputRollbackReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    ok = check(newOutputRollbackResult == MigrationResult::Failed &&
                   !QFileInfo::exists(newOutputRollback) &&
                   !stagingPaths(newOutputRollback).isEmpty(),
               "failed partial publish removes an output root created by this transaction") && ok;

    const QString outputCleanupFailure = root.filePath(
        QStringLiteral("output-cleanup-failure"));
    publishEntryCount = 0;
    JxAssetMigrator::setFileSystemFaultInjectorForTests(
        [&](Operation operation, const QString&, const QString&)
        {
            if (operation == Operation::PublishEntry)
                return ++publishEntryCount == 2;
            return operation == Operation::RemoveCreatedOutputRoot;
        });
    AssetMigrationReport outputCleanupFailureReport;
    const MigrationResult outputCleanupFailureResult = migrator.migrate(
        scriptsSource, outputCleanupFailure, scriptsOptions,
        outputCleanupFailureReport);
    JxAssetMigrator::setFileSystemFaultInjectorForTests({});
    ok = check(outputCleanupFailureResult == MigrationResult::Failed &&
                   QFileInfo(outputCleanupFailure).isDir() &&
                   QDir(outputCleanupFailure).entryList(
                       QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty() &&
                   outputCleanupFailureReport.logLines.join('\n').contains(
                       QString::fromUtf8("未能移除本次新建的输出目录")),
               "created-output cleanup failure is surfaced instead of silently ignored") && ok;

    return ok;
}

bool testMigrationRejectsInheritedProfileWithoutDependency()
{
    QTemporaryDir rootDirectory;
    if (!check(rootDirectory.isValid(), "create inherited profile validation temp root"))
        return false;
    QDir root(rootDirectory.path());
    root.mkpath("source");

    AssetMigrationOptions directOptions;
    directOptions.convertScript = false;
    directOptions.dependencyId.clear();
    AssetMigrationReport directReport;
    JxAssetMigrator migrator;
    const QString directOutput = root.filePath("direct-output");
    AssetMigrationReport emptyPathReport;
    bool ok = check(migrator.migrate(root.filePath("source"), QString(),
                        directOptions, emptyPathReport) == MigrationResult::Failed,
                    "core migrator rejects an empty output path");
    ok = check(migrator.migrate(root.filePath("source"), directOutput,
                        directOptions, directReport) == MigrationResult::Failed &&
                    !QFileInfo::exists(directOutput),
                    "core migrator rejects inherited Type without a content dependency") && ok;

    AssetMigrationOptions standaloneModOptions = directOptions;
    standaloneModOptions.modType = 3;
    AssetMigrationReport standaloneModReport;
    const QString standaloneModOutput =
        root.filePath("standalone-mod-output");
    const MigrationResult standaloneModResult =
        migrator.migrate(
            root.filePath("source"),
            standaloneModOutput,
            standaloneModOptions,
            standaloneModReport);
    const QString standaloneProfile =
        readUtf8TextFile(
            root.filePath(
                "standalone-mod-output/game_profile.ini"));
    const auto generatedProfileOutcome =
        std::find_if(
            standaloneModReport.fileOutcomes.cbegin(),
            standaloneModReport.fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "<generated:game-profile>") &&
                    outcome.outputPath ==
                        QStringLiteral(
                            "game_profile.ini") &&
                    outcome.action ==
                        AssetMigrationFileAction::Convert &&
                    outcome.reason ==
                        QStringLiteral(
                            "generated-game-profile");
            });
    ok = check(
        standaloneModResult != MigrationResult::Failed &&
            standaloneProfile.contains(
                QStringLiteral("Type=3")) &&
            standaloneProfile.contains(
                QStringLiteral(
                    "DefeatedNpcExperienceMode=LevelProductWithBonus")) &&
            standaloneProfile.contains(
                QStringLiteral("ExperienceMultiplier=1")) &&
            standaloneProfile.contains(
                QStringLiteral("TextEncodingConverted=1")) &&
            !standaloneProfile.contains(
                QStringLiteral("DependencyId=")) &&
            !standaloneProfile.contains(
                QStringLiteral("DependencyPath=")) &&
            generatedProfileOutcome !=
                standaloneModReport.fileOutcomes.cend() &&
            !generatedProfileOutcome->
                outputSha256.isEmpty(),
        "core migrator gives an explicit standalone Type=3 MOD the new experience defaults and records its generated profile") &&
        ok;

    // 项目迁移回归：显式 Type=3（完整 profile，writeModProfile=true）也允许
    // DependencyId 为空，与 BatchConvertWindow 项目导入路径行为一致。
    AssetMigrationOptions projectType3Options = standaloneModOptions;
    projectType3Options.writeModProfile = true;
    projectType3Options.modType = 3;
    projectType3Options.dependencyId.clear();
    AssetMigrationReport projectType3Report;
    const QString projectType3Output =
        root.filePath("project-type3-output");
    const MigrationResult projectType3Result =
        migrator.migrate(
            root.filePath("source"),
            projectType3Output,
            projectType3Options,
            projectType3Report);
    const QString projectType3Profile =
        readUtf8TextFile(
            root.filePath(
                "project-type3-output/game_profile.ini"));
    ok = check(
        projectType3Result != MigrationResult::Failed &&
            projectType3Profile.contains(
                QStringLiteral("Type=3")) &&
            !projectType3Profile.contains(
                QStringLiteral("DependencyId=")) &&
            !projectType3Profile.contains(
                QStringLiteral("DependencyPath=")),
        "project-style migration accepts an explicit Type=3 MOD without a content dependency") &&
        ok;

    ok = check(
        root.mkpath(
            "generated-profile-failure-source/"
            "game_profile.ini"),
        "create a directory that blocks generated profile output") &&
        ok;
    AssetMigrationReport generatedProfileFailureReport;
    const QString generatedProfileFailureOutput =
        root.filePath(
            "generated-profile-failure-output");
    const MigrationResult generatedProfileFailureResult =
        migrator.migrate(
            root.filePath(
                "generated-profile-failure-source"),
            generatedProfileFailureOutput,
            standaloneModOptions,
            generatedProfileFailureReport);
    const auto generatedProfileFailureOutcome =
        std::find_if(
            generatedProfileFailureReport.
                fileOutcomes.cbegin(),
            generatedProfileFailureReport.
                fileOutcomes.cend(),
            [](const AssetMigrationFileOutcome& outcome)
            {
                return !outcome.sourceScan &&
                    outcome.sourcePath ==
                        QStringLiteral(
                            "<generated:game-profile>") &&
                    outcome.outputPath ==
                        QStringLiteral(
                            "game_profile.ini") &&
                    outcome.action ==
                        AssetMigrationFileAction::Fail &&
                    outcome.reason ==
                        QStringLiteral(
                            "generated-game-profile-prepare-failed");
            });
    ok = check(
        generatedProfileFailureResult ==
                MigrationResult::Failed &&
            generatedProfileFailureOutcome !=
                generatedProfileFailureReport.
                    fileOutcomes.cend() &&
            generatedProfileFailureReport.
                resourceDomains.value(
                    QStringLiteral("other")).
                failedFiles > 0 &&
            !QFileInfo::exists(
                generatedProfileFailureOutput),
        "a generated profile failure is listed as a failed output and prevents incomplete publication") &&
        ok;

    const QByteArray stdoutPath = QFile::encodeName(root.filePath("cli-stdout.txt"));
    const QByteArray stderrPath = QFile::encodeName(root.filePath("cli-stderr.txt"));
    FILE* stdoutFile = std::fopen(stdoutPath.constData(), "w+b");
    FILE* stderrFile = std::fopen(stderrPath.constData(), "w+b");
    if (!check(stdoutFile != nullptr && stderrFile != nullptr,
               "open CLI streams for inherited profile validation"))
    {
        if (stdoutFile) std::fclose(stdoutFile);
        if (stderrFile) std::fclose(stderrFile);
        return false;
    }

    QStringList arguments = {
        "jxqy-editor-cli", "migrate-assets", root.filePath("source"),
        root.filePath("cli-output"), "--dependency-id", QString()
    };
    int exitCode = AssetCliRunner::run(arguments, stdoutFile, stderrFile);
    QString cliOutput = readTemporaryFile(stdoutFile);
    QString errors = readTemporaryFile(stderrFile);
    std::fclose(stdoutFile);
    std::fclose(stderrFile);

    ok = check(
        exitCode == 2 &&
            cliOutput.contains(
                QString::fromUtf8(
                    "继承 Type 需要内容依赖")) &&
            cliOutput.contains(
                QStringLiteral("Status: Failed")) &&
            errors.isEmpty(),
        "CLI rejects an explicitly empty inherited profile dependency through shared migration validation") &&
        ok;
    ok = check(!QFileInfo::exists(root.filePath("cli-output")),
               "invalid inherited profile options do not create output") && ok;

    AssetMigrationOptions injectedProfileOptions;
    injectedProfileOptions.convertScript = false;
    injectedProfileOptions.dependencyId = "JXQY2";
    injectedProfileOptions.modName = "Unsafe\nType=0";
    AssetMigrationReport injectedProfileReport;
    const QString injectedProfileOutput = root.filePath("injected-profile-output");
    ok = check(migrator.migrate(root.filePath("source"), injectedProfileOutput,
                        injectedProfileOptions, injectedProfileReport) == MigrationResult::Failed &&
                    !QFileInfo::exists(injectedProfileOutput),
                    "core migrator rejects multiline profile fields") && ok;
    return ok;
}

bool testMigrationAddsUiWindowDefaults()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(), "create UI window defaults migration temp dirs"))
        return false;

    QDir source(sourceDir.path());
    if (!check(source.mkpath("script/common") && source.mkpath("map") && source.mkpath("asf") &&
            source.mkpath("ini/ui/dialog") && source.mkpath("ini/ui/option") &&
            source.mkpath("ini/ui/message") && source.mkpath("ini/ui/system") &&
            source.mkpath("ini/ui/title") &&
            source.mkpath("ini/ui/top") &&
            source.mkpath("ini/ui/yesno") &&
            source.mkpath("ini/obj"),
            "create UI window defaults migration source layout"))
    {
        return false;
    }

    bool ok = true;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/dialog/window.ini"),
               "[Init]\nName=DialogWindow\nImage=panel.asf\nWidth=350\nHeight=85\n"),
               "write dialog window fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/option/window.ini"),
               "[Init]\nName=OptionWindow\nImage=panel.asf\nAlign=alCenter\n"),
               "write option window fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/system/window.ini"),
               "[Init]\nName=SystemWindow\nImage=panel.asf\nAlign=custom\n"),
               "write system window fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/message/window.ini"),
               "[Init]\nName=MessageWindow\nImage=msgbox.asf\nWidth=236\nHeight=94\nAlignY=-64\n"),
               "write YYCS message window fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/message/label.ini"),
               "[Init]\nLeft=46\nTop=32\nWidth=135\nHeight=50\nFont=20\nColor=155,34,22\n"),
               "write YYCS message label fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/title/initbtn.ini"),
               "[Init]\nKind=TrackBtn\nImage=InitBtn.asf\nSound=menu.wav\n"),
               "write title init button fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/title/loadbtn.ini"),
               "[Init]\nKind=TrackBtn\nImage=LoadBtn.asf\nStretch=custom\n"),
               "write title load button fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/title/window.ini"),
               "[Init]\nWidth=640\nHeight=480\nBitmap=title.png\n"
               "ScaleChildren=false\nCenterChildren=true\n"),
               "write title window fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/top/window.ini"),
               "[Init]\nWidth=285\nHeight=27\nImage=window.asf\n"),
               "write YYCS top window fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/top/btnstate.ini"),
               "[Init]\nLeft=52\nTop=0\nWidth=19\nHeight=19\nImage=btnstate.asf\n"),
               "write YYCS top button fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/yesno/window.ini"),
               "[Init]\nName=YesNoWindow\nImage=msgbox.asf\nWidth=296\nHeight=390\n"),
               "write YYCS yes/no window fixture") && ok;
    ok = check(writeUtf8TextFile(
               source.filePath(QString::fromUtf8("ini/obj/可捡钱.ini")),
               "[Init]\nObjName=Money\nKind=7\n"),
               "write money-drop object fixture") && ok;
    ok = check(writeRawFile(
               source.filePath(QString::fromUtf8(
                   "script/common/4级钱.txt")),
               QByteArray::fromHex(
                   "2020706c6179736f756e642822ceef2dd2f8d7d32e77617622293b0a"
                   "202061646472616e646d6f6e6579283133312c313539293b0a"
                   "202064656c6375726f626a28293b0a")),
               "write existing YYCS money-drop script fixture") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;
    options.dependencyId = "YYCS";
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);
    ok = check(result == MigrationResult::Success, "UI window defaults migration succeeds") && ok;

    QDir output(outputDir.path());
    const QString dialogText = readUtf8TextFile(output.filePath("ini/ui/dialog/window.ini"));
    const QString optionText = readUtf8TextFile(output.filePath("ini/ui/option/window.ini"));
    const QString systemText = readUtf8TextFile(output.filePath("ini/ui/system/window.ini"));
    const QString messageWindowText = readUtf8TextFile(
        output.filePath("ini/ui/message/window.ini"));
    const QString messageLabelText = readUtf8TextFile(
        output.filePath("ini/ui/message/label.ini"));
    const QString titleInitText = readUtf8TextFile(output.filePath("ini/ui/title/initbtn.ini"));
    const QString titleLoadText = readUtf8TextFile(output.filePath("ini/ui/title/loadbtn.ini"));
    const QString titleWindowText = readUtf8TextFile(output.filePath("ini/ui/title/window.ini"));
    const QString topWindowText = readUtf8TextFile(output.filePath("ini/ui/top/window.ini"));
    const QString topButtonText = readUtf8TextFile(output.filePath("ini/ui/top/btnstate.ini"));
    const QString yesNoText = readUtf8TextFile(output.filePath("ini/ui/yesno/window.ini"));
    const QString chooseMenuText = readUtf8TextFile(output.filePath("ini/ui/choose/choose.menu.ini"));
    const QString chooseWindowText = readUtf8TextFile(output.filePath("ini/ui/choose/window.ini"));
    const QString chooseLabelText = readUtf8TextFile(output.filePath("ini/ui/choose/label.ini"));
    const QString chooseButtonText = readUtf8TextFile(output.filePath("ini/ui/choose/btnA.ini"));
    const QString chooseButtonBText = readUtf8TextFile(output.filePath("ini/ui/choose/btnB.ini"));
    const QString yycsProfileText = readUtf8TextFile(
        output.filePath("game_profile.ini"));
    const std::vector<std::pair<int, int>> yycsMoneyRanges = {
        {10, 40},
        {50, 80},
        {90, 120},
        {131, 159},
        {170, 200},
        {210, 240},
        {250, 280}
    };
    bool yycsMoneyScriptsMatch = true;
    for (std::size_t index = 0; index < yycsMoneyRanges.size(); ++index)
    {
        const QString scriptText = readUtf8TextFile(
            output.filePath(QString::fromUtf8(
                "script/common/%1级钱.txt").arg(index + 1)));
        const auto [minimumMoney, maximumMoney] = yycsMoneyRanges[index];
        yycsMoneyScriptsMatch = yycsMoneyScriptsMatch &&
            scriptText.contains(QString::fromUtf8(
                "playsound(\"物-银子.wav\");")) &&
            scriptText.contains(QStringLiteral("addrandmoney(%1,%2);")
                .arg(minimumMoney)
                .arg(maximumMoney)) &&
            scriptText.contains(QStringLiteral("delcurobj();"));
    }
    const int generatedChooseOutcomeCount =
        static_cast<int>(
            std::count_if(
                report.fileOutcomes.cbegin(),
                report.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(
                                "<generated:choose-menu>") &&
                        outcome.action ==
                            AssetMigrationFileAction::Convert &&
                        outcome.reason ==
                            QStringLiteral(
                                "generated-choose-menu") &&
                        !outcome.outputSha256.isEmpty();
                }));
    const int generatedMoneyScriptOutcomeCount =
        static_cast<int>(
            std::count_if(
                report.fileOutcomes.cbegin(),
                report.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath == QStringLiteral(
                            "<generated:money-drop-script>") &&
                        outcome.action ==
                            AssetMigrationFileAction::Convert &&
                        outcome.reason == QStringLiteral(
                            "generated-money-drop-script") &&
                        !outcome.outputSha256.isEmpty();
                }));

    ok = check(dialogText.contains("Align=alBottomCenter"), "dialog window gets bottom-center align") && ok;
    ok = check(dialogText.contains("AlignX=-45"), "dialog window gets AlignX") && ok;
    ok = check(dialogText.contains("AlignY=-100"), "dialog window gets AlignY") && ok;
    ok = check(optionText.contains("Align=alCenter"), "option window keeps existing align") && ok;
    ok = check(optionText.contains("Stretch=false"), "option window gets non-stretch default") && ok;
    ok = check(systemText.contains("Align=custom"), "system window keeps explicit align") && ok;
    ok = check(!systemText.contains("Align=alCenter"), "system window does not overwrite explicit align") && ok;
    ok = check(messageWindowText.contains("Align=alBottomCenter") &&
               messageWindowText.contains("AlignX=-10") &&
               messageWindowText.contains("AlignY=-71") &&
               messageLabelText.contains("Left=46") &&
               messageLabelText.contains("Top=32") &&
               messageLabelText.contains("Width=148") &&
               messageLabelText.contains("Height=50") &&
               messageLabelText.contains("Font=20") &&
               messageLabelText.contains("Color=155,34,22,204"),
               "YYCS message migration keeps the message panel above the bottom menu") && ok;
    ok = check(titleInitText.contains("Stretch=true"), "title button gets stretch default") && ok;
    ok = check(titleLoadText.contains("Stretch=custom"), "title button keeps explicit stretch") && ok;
    ok = check(!titleLoadText.contains("Stretch=true"), "title button does not duplicate stretch default") && ok;
    ok = check(titleWindowText.contains("Align=alClient") &&
               titleWindowText.contains("Stretch=true") &&
               titleWindowText.contains("KeepAspect=true") &&
               titleWindowText.contains("FadeMirroredBars=true") &&
               !titleWindowText.contains("ScaleChildren=false") &&
               !titleWindowText.contains("CenterChildren=true"),
               "title background and 640x480 controls migrate with one aspect-fit transform") && ok;
    ok = check(topWindowText.contains("Align=alTopCenter") &&
               topWindowText.contains("Scale=1.5") &&
               topWindowText.contains("Stretch=true") &&
               topButtonText.contains("Stretch=true"),
               "YYCS top menu and button images migrate with the requested 1.5-times scale") && ok;
    ok = check(yesNoText.contains("Align=alCenter") &&
               yesNoText.contains("AlignX=0") &&
               yesNoText.contains("AlignY=0"),
               "YYCS yes/no window defaults to the viewport center") && ok;
    ok = check(chooseMenuText.contains("name=ChooseMenu"),
               "migration generates a local choose menu when the source only has dialog UI") && ok;
    ok = check(chooseWindowText.contains("Image=panel.asf") &&
               chooseWindowText.contains("AlignY=-100"),
               "generated choose window keeps the source dialog image and UI-family offset") && ok;
    ok = check(chooseButtonText.contains("Font=17") &&
               chooseButtonText.contains("NormalColor=0,0,180") &&
               chooseLabelText.contains("Left=25") &&
               chooseLabelText.contains("Width=300") &&
               chooseButtonText.contains("Top=30") &&
               chooseButtonBText.contains("Top=54"),
               "generated compact YYCS choices fit the source dialog panel") && ok;
    ok = check(
        generatedChooseOutcomeCount == 5,
        "migration report lists all five generated choose-menu files with published digests") &&
        ok;
    ok = check(
        yycsProfileText.contains(
            QStringLiteral("Music=mc000.mp3")),
        "YYCS migration writes the original title-theme name and relies on runtime format fallback") &&
        ok;
    ok = check(
        yycsMoneyScriptsMatch && generatedMoneyScriptOutcomeCount == 6,
        "YYCS migration restores missing JxqyHD money-drop scripts without overwriting an existing resource value") &&
        ok;

    QTemporaryDir inheritedUiCollectionDir;
    if (!check(
            inheritedUiCollectionDir.isValid(),
            "create inherited UI migration collection"))
    {
        return false;
    }
    QDir inheritedUiCollection(inheritedUiCollectionDir.path());
    ok = check(
        inheritedUiCollection.mkpath("base/ini/ui/dialog") &&
            inheritedUiCollection.mkpath("base/ini/ui/choose") &&
            inheritedUiCollection.mkpath("base/ini/ui/state") &&
            inheritedUiCollection.mkpath("content-base") &&
            inheritedUiCollection.mkpath("source/ini/ui/dialog") &&
            inheritedUiCollection.mkpath("source/ini/ui/state") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("resources.ini"),
                "[Pack.JXQY2]\nId=JXQY2\nPath=content-base\nEnabled=1\n\n"
                "[Pack.YYCS]\nId=YYCS\nPath=base\nEnabled=1\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("content-base/game_profile.ini"),
                "[Game]\nId=JXQY2\nType=0\n\n"
                "[UI]\nProfile=JXQY2\nPreferLocal=1\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/game_profile.ini"),
                "[Game]\nId=YYCS\nType=1\n\n"
                "[UI]\nProfile=YYCS\nPreferLocal=1\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/dialog/window.ini"),
                "[Init]\nName=DialogWindow\nImage=asf\\ui\\dialog\\panel.asf\n"
                "Left=0\nTop=0\nWidth=438\nHeight=123\n"
                "Align=alBottomCenter\nAlignX=0\nAlignY=-85\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/choose/choose.menu.ini"),
                "[menu]\nname=ChooseMenu\nvisible=false\n"
                "window=ini\\ui\\choose\\window.ini\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/choose/window.ini"),
                "[Init]\nName=ChooseWindow\nImage=asf\\ui\\dialog\\panel.asf\n"
                "Left=0\nTop=0\nWidth=438\nHeight=123\n"
                "Align=alBottomCenter\nAlignX=0\nAlignY=-85\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/choose/label.ini"),
                "[Init]\nName=ChooseLabel\nLeft=65\nTop=30\n"
                "Width=310\nHeight=22\nFont=18\nColor=20,20,20\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/choose/btna.ini"),
                "[Init]\nName=ChooseA\nLeft=65\nTop=52\n"
                "Width=310\nHeight=22\nFont=18\nColor=0,0,180\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/choose/btnb.ini"),
                "[Init]\nName=ChooseB\nLeft=65\nTop=74\n"
                "Width=310\nHeight=22\nFont=18\nColor=0,0,180\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("base/ini/ui/state/image01.ini"),
                "[Init]\nName=StateImage\nLeft=0\nTop=0\nWidth=320\nHeight=480\n"
                "Image=asf\\ui\\common\\panel5.asf\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("source/ini/ui/dialog/window.ini"),
                "[Init]\nName=DialogWindow\nImage=asf\\ui\\dialog\\panel.asf\n"
                "Left=100\nTop=295\nWidth=350\nHeight=85\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("source/ini/ui/dialog/headfile.ini"),
                "[PORTRAIT]\n1=custom-face.asf\n") &&
            writeUtf8TextFile(
                inheritedUiCollection.filePath("source/ini/ui/state/image01.ini"),
                "[Init]\nName=StateImage\nLeft=8\nTop=9\nWidth=280\nHeight=360\n"
                "Image=asf\\ui\\common\\custom-panel.asf\n"),
        "write UI-base inheritance fixtures") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions inheritedUiOptions;
    inheritedUiOptions.convertScript = false;
    inheritedUiOptions.dependencyId = "JXQY2,YYCS";
    inheritedUiOptions.uiBaseId = "YYCS";
    inheritedUiOptions.uiProfile = "YYCS";
    AssetMigrationReport inheritedUiReport;
    const QString inheritedUiOutput =
        inheritedUiCollection.filePath("converted-mod");
    const MigrationResult inheritedUiResult = migrator.migrate(
        inheritedUiCollection.filePath("source"),
        inheritedUiOutput,
        inheritedUiOptions,
        inheritedUiReport);
    const QDir inheritedUiOutputDirectory(inheritedUiOutput);
    const QString inheritedDialogText = readUtf8TextFile(
        inheritedUiOutputDirectory.filePath("ini/ui/dialog/window.ini"));
    const QString inheritedChooseLabelText = readUtf8TextFile(
        inheritedUiOutputDirectory.filePath("ini/ui/choose/label.ini"));
    const QString inheritedStateImageText = readUtf8TextFile(
        inheritedUiOutputDirectory.filePath("ini/ui/state/image01.ini"));
    const QString inheritedPortraitText = readUtf8TextFile(
        inheritedUiOutputDirectory.filePath("ini/ui/dialog/headfile.ini"));
    const int inheritedChooseFileCount = static_cast<int>(std::count_if(
        inheritedUiReport.fileOutcomes.cbegin(),
        inheritedUiReport.fileOutcomes.cend(),
        [](const AssetMigrationFileOutcome& outcome)
        {
            return outcome.sourcePath == QStringLiteral("<inherited:ui-base>") &&
                outcome.reason == QStringLiteral("inherited-ui-base-file") &&
                outcome.action == AssetMigrationFileAction::Convert;
        }));
    ok = check(
        inheritedUiResult == MigrationResult::Success &&
            inheritedDialogText.contains("Left=0") &&
            inheritedDialogText.contains("Top=0") &&
            inheritedDialogText.contains("Width=438") &&
            inheritedDialogText.contains("Height=123") &&
            inheritedDialogText.contains("AlignX=0") &&
            inheritedDialogText.contains("AlignY=-85"),
        "MOD migration aligns presentation fields with the independent UI base instead of the first content base") && ok;
    ok = check(
        inheritedChooseFileCount == 5 &&
            inheritedChooseLabelText.contains("Name=ChooseLabel") &&
            inheritedChooseLabelText.contains("Left=65") &&
            inheritedChooseLabelText.contains("Font=18"),
        "missing MOD choice UI inherits the base menu instead of generating a stale local layout") && ok;
    ok = check(
        inheritedStateImageText.contains("Left=0") &&
            inheritedStateImageText.contains("Top=0") &&
            inheritedStateImageText.contains("Width=320") &&
            inheritedStateImageText.contains("Height=480") &&
            inheritedStateImageText.contains("Image=asf\\ui\\common\\custom-panel.asf") &&
            inheritedPortraitText.contains("1=custom-face.asf"),
        "UI-base alignment preserves MOD-specific images and portrait content") && ok;

    QTemporaryDir blockedChooseSourceDir;
    QTemporaryDir blockedChooseOutputDir;
    if (!check(
            blockedChooseSourceDir.isValid() &&
                blockedChooseOutputDir.isValid(),
            "create blocked generated choose-menu migration temp dirs"))
    {
        return false;
    }
    QDir blockedChooseSource(
        blockedChooseSourceDir.path());
    ok = check(
        blockedChooseSource.mkpath(
            "ini/ui/dialog") &&
            writeUtf8TextFile(
                blockedChooseSource.filePath(
                    "ini/ui/dialog/window.ini"),
                "[Init]\nImage=panel.asf\n") &&
            writeRawFile(
                blockedChooseSource.filePath(
                    "ini/ui/choose"),
                QByteArray(
                    "source-file-blocks-generated-directory")),
        "write generated choose-menu failure fixture") &&
        ok;
    if (!ok)
        return false;

    AssetMigrationReport blockedChooseReport;
    const MigrationResult blockedChooseResult =
        migrator.migrate(
            blockedChooseSourceDir.path(),
            blockedChooseOutputDir.path(),
            options,
            blockedChooseReport);
    const int failedChooseOutcomeCount =
        static_cast<int>(
            std::count_if(
                blockedChooseReport.fileOutcomes.cbegin(),
                blockedChooseReport.fileOutcomes.cend(),
                [](const AssetMigrationFileOutcome& outcome)
                {
                    return !outcome.sourceScan &&
                        outcome.sourcePath ==
                            QStringLiteral(
                                "<generated:choose-menu>") &&
                        outcome.action ==
                            AssetMigrationFileAction::Fail &&
                        outcome.reason ==
                            QStringLiteral(
                                "generated-choose-menu-write-failed");
                }));
    ok = check(
        blockedChooseResult ==
                MigrationResult::Failed &&
            failedChooseOutcomeCount == 5 &&
            blockedChooseReport.
                resourceDomains.value(
                    QStringLiteral("other")).
                failedFiles >= 5,
        "each blocked generated choose-menu file is listed as a failed output") &&
        ok;

    QTemporaryDir mixedTypeSourceDir;
    QTemporaryDir mixedTypeOutputDir;
    if (!check(mixedTypeSourceDir.isValid() && mixedTypeOutputDir.isValid(),
            "create mixed game-type/UI-profile migration temp dirs"))
    {
        return false;
    }

    QDir mixedTypeSource(mixedTypeSourceDir.path());
    ok = check(mixedTypeSource.mkpath("script") && mixedTypeSource.mkpath("map") &&
            mixedTypeSource.mkpath("asf/ui/dialog") && mixedTypeSource.mkpath("asf/ui/top") &&
            mixedTypeSource.mkpath("ini/ui/dialog") && mixedTypeSource.mkpath("ini/ui/top") &&
            mixedTypeSource.mkpath("ini/ui/choose"),
            "create mixed game-type/UI-profile source layout") && ok;
    QByteArray mixedPanelAsf(72, '\0');
    std::memcpy(mixedPanelAsf.data(), "ASF 1.00", 8);
    auto writeLittleEndianInt32 = [&](int offset, int32_t value)
    {
        mixedPanelAsf[offset] = static_cast<char>(value & 0xFF);
        mixedPanelAsf[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
        mixedPanelAsf[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
        mixedPanelAsf[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
    };
    writeLittleEndianInt32(16, 438);
    writeLittleEndianInt32(20, 123);
    writeLittleEndianInt32(24, 1);
    writeLittleEndianInt32(28, 8);
    writeLittleEndianInt32(32, 0);
    writeLittleEndianInt32(64, 72);
    writeLittleEndianInt32(68, 0);
    ok = check(writeRawFile(mixedTypeSource.filePath("asf/ui/dialog/panel.asf"), mixedPanelAsf) &&
            writeUtf8TextFile(mixedTypeSource.filePath("asf/ui/top/window.asf"), "top") &&
            writeUtf8TextFile(mixedTypeSource.filePath("ini/ui/dialog/window.ini"),
                "[Init]\nName=DialogWindow\nImage=asf\\ui\\dialog\\panel.asf\nWidth=350\nHeight=85\nAlign=alBottomCenter\n") &&
            writeUtf8TextFile(mixedTypeSource.filePath("ini/ui/top/window.ini"),
                "[Init]\nName=TopWindow\nImage=asf\\ui\\top\\window.asf\n") &&
            writeUtf8TextFile(mixedTypeSource.filePath("ini/ui/choose/label.ini"),
                "[Init]\nLeft=65\nTop=26\nWidth=310\nHeight=22\nFont=18\n") &&
            writeUtf8TextFile(mixedTypeSource.filePath("ini/ui/choose/btnA.ini"),
                "[Init]\nLeft=65\nTop=52\nWidth=310\nHeight=28\nFont=18\n") &&
            writeUtf8TextFile(mixedTypeSource.filePath("ini/ui/choose/btnB.ini"),
                "[Init]\nLeft=65\nTop=82\nWidth=310\nHeight=28\nFont=18\n"),
            "write mixed game-type/UI-profile fixtures") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions mixedTypeOptions;
    mixedTypeOptions.convertScript = false;
    mixedTypeOptions.modType = 0;
    mixedTypeOptions.dependencyId = "JXQY2,YYCS";
    mixedTypeOptions.features.insert("MagicTriggerAtAnimationEnd", true);
    AssetMigrationReport mixedTypeReport;
    MigrationResult mixedTypeResult = migrator.migrate(
        mixedTypeSourceDir.path(), mixedTypeOutputDir.path(), mixedTypeOptions, mixedTypeReport);
    ok = check(mixedTypeResult == MigrationResult::Success,
               "type-0 pack with independent ASF UI migration succeeds") && ok;

    QDir mixedTypeOutput(mixedTypeOutputDir.path());
    const QString mixedDialogText = readUtf8TextFile(
        mixedTypeOutput.filePath("ini/ui/dialog/window.ini"));
    const QString mixedChooseText = readUtf8TextFile(
        mixedTypeOutput.filePath("ini/ui/choose/window.ini"));
    const QString mixedChooseButtonText = readUtf8TextFile(
        mixedTypeOutput.filePath("ini/ui/choose/btnA.ini"));
    const QString mixedChooseButtonBText = readUtf8TextFile(
        mixedTypeOutput.filePath("ini/ui/choose/btnB.ini"));
    const QString mixedChooseLabelText = readUtf8TextFile(
        mixedTypeOutput.filePath("ini/ui/choose/label.ini"));
    const QString mixedProfileText = readUtf8TextFile(
        mixedTypeOutput.filePath("game_profile.ini"));
    ok = check(mixedDialogText.contains("AlignX=-45") &&
               mixedDialogText.contains("AlignY=-100"),
               "source UI assets override JXQY2 content-base alignment defaults") && ok;
    ok = check(mixedChooseText.contains("asf\\ui\\dialog\\panel.asf") &&
               mixedChooseText.contains("AlignY=-62") &&
               mixedChooseText.contains("Width=438") &&
               mixedChooseText.contains("Height=123") &&
               mixedChooseText.contains("Left=0") &&
               mixedChooseText.contains("Top=0") &&
               mixedChooseText.contains("AlignX=-1"),
               "generated choose window preserves the dialog top with native panel geometry") && ok;
    ok = check(mixedChooseButtonText.contains("Font=18") &&
               mixedChooseButtonText.contains("Top=52") &&
               mixedChooseButtonText.contains("Height=22") &&
               mixedChooseButtonBText.contains("Top=74") &&
               mixedChooseButtonBText.contains("Height=22") &&
               !mixedChooseButtonText.contains("Font=14"),
               "native YYCS choice rows follow the dialog's 22-pixel line spacing") && ok;
    ok = check(mixedChooseLabelText.contains("Left=65") &&
               mixedChooseLabelText.contains("Top=30") &&
               mixedChooseLabelText.contains("Width=310"),
               "generated native-width UI aligns its prompt with the dialog first line") && ok;
    ok = check(mixedProfileText.contains("DependencyId=JXQY2,YYCS") &&
               mixedProfileText.contains("TextEncodingConverted=1") &&
               mixedProfileText.contains("[UI]") &&
               mixedProfileText.contains("BaseId=YYCS") &&
               mixedProfileText.contains("Profile=YYCS") &&
               mixedProfileText.contains("PreferLocal=1") &&
               mixedProfileText.contains("[Features]") &&
               mixedProfileText.contains("MagicTriggerAtAnimationEnd=1") &&
               !mixedProfileText.contains("Author=") &&
               !mixedProfileText.contains("[Team]") &&
               !mixedProfileText.contains("InfoFile="),
               "migration preserves profile configuration without inventing MOD team information") && ok;

    QTemporaryDir jxqy2SourceDir;
    QTemporaryDir jxqy2OutputDir;
    if (!check(jxqy2SourceDir.isValid() && jxqy2OutputDir.isValid(),
            "create JXQY2 choose-style migration temp dirs"))
    {
        return false;
    }
    QDir jxqy2Source(jxqy2SourceDir.path());
    ok = check(jxqy2Source.mkpath("script") && jxqy2Source.mkpath("map") &&
            jxqy2Source.mkpath("ini/ui/dialog") && jxqy2Source.mkpath("ini/ui/title") &&
            jxqy2Source.mkpath("ini/ui/yesno") &&
            writeUtf8TextFile(jxqy2Source.filePath("ini/ui/dialog/window.ini"),
                "[Init]\nImage=mpc\\ui\\dialog\\panel.mpc\nWidth=440\nHeight=120\n") &&
            writeUtf8TextFile(jxqy2Source.filePath("ini/ui/title/window.ini"),
                "[Init]\nImage=mpc\\ui\\title\\title.png\nWidth=640\nHeight=480\n"
                "ScaleChildren=false\nCenterChildren=true\n") &&
            writeUtf8TextFile(jxqy2Source.filePath("ini/ui/yesno/window.ini"),
                "[Init]\nImage=mpc\\ui\\yesno\\window.mpc\nWidth=280\nHeight=100\n"),
            "write JXQY2 choose-style fixtures") && ok;
    AssetMigrationOptions jxqy2Options;
    jxqy2Options.convertScript = false;
    jxqy2Options.modType = 0;
    jxqy2Options.dependencyId = "JXQY2";
    AssetMigrationReport jxqy2Report;
    const MigrationResult jxqy2Result = migrator.migrate(
        jxqy2SourceDir.path(), jxqy2OutputDir.path(), jxqy2Options, jxqy2Report);
    ok = check(jxqy2Result == MigrationResult::Success,
               "JXQY2 choose-style migration succeeds") && ok;
    const QDir jxqy2Output(jxqy2OutputDir.path());
    const QString jxqy2ChooseLabelText = readUtf8TextFile(
        jxqy2Output.filePath("ini/ui/choose/label.ini"));
    const QString jxqy2ChooseButtonText = readUtf8TextFile(
        jxqy2Output.filePath("ini/ui/choose/btnA.ini"));
    const QString jxqy2YesNoText = readUtf8TextFile(
        jxqy2Output.filePath("ini/ui/yesno/window.ini"));
    const QString jxqy2TitleWindowText = readUtf8TextFile(
        jxqy2Output.filePath("ini/ui/title/window.ini"));
    const QString jxqy2ProfileText = readUtf8TextFile(
        jxqy2Output.filePath("game_profile.ini"));
    ok = check(jxqy2ChooseLabelText.contains("Font=17") &&
               jxqy2ChooseLabelText.contains("Color=40,32,24") &&
               jxqy2ChooseLabelText.contains("Left=36") &&
               jxqy2ChooseLabelText.contains("Top=18") &&
               jxqy2ChooseLabelText.contains("Width=384"),
               "JXQY2 choose title uses readable dark text on the stone panel") && ok;
    ok = check(jxqy2ChooseButtonText.contains("Top=52") &&
               jxqy2ChooseButtonText.contains("NormalColor=30,65,145,230"),
               "JXQY2 choose options use the coordinated spacing and color scheme") && ok;
    ok = check(jxqy2YesNoText.contains("Align=alCenter") &&
               jxqy2YesNoText.contains("AlignX=0") &&
               jxqy2YesNoText.contains("AlignY=0"),
               "JXQY2 yes/no window defaults to the viewport center") && ok;
    ok = check(jxqy2TitleWindowText.contains("Align=alClient") &&
               jxqy2TitleWindowText.contains("Stretch=true") &&
               jxqy2TitleWindowText.contains("KeepAspect=true") &&
               !jxqy2TitleWindowText.contains("ScaleChildren=false") &&
               !jxqy2TitleWindowText.contains("CenterChildren=true"),
               "JXQY2 title migrates with the shared 640x480 aspect-fit policy") && ok;
    ok = check(jxqy2ProfileText.contains("Music=ks64.mp3"),
               "JXQY2 migration keeps its title-theme default") && ok;

    QTemporaryDir xjxqySourceDir;
    QTemporaryDir xjxqyOutputDir;
    if (!check(xjxqySourceDir.isValid() && xjxqyOutputDir.isValid(),
            "create XJXQY UI window defaults migration temp dirs"))
    {
        return false;
    }

    QDir xjxqySource(xjxqySourceDir.path());
    ok = check(xjxqySource.mkpath("script") && xjxqySource.mkpath("map") &&
            xjxqySource.mkpath("asf") && xjxqySource.mkpath("ini/ui/top") &&
            xjxqySource.mkpath("ini/ui/title"),
            "create XJXQY UI window defaults migration source layout") && ok;
    ok = check(writeUtf8TextFile(xjxqySource.filePath("ini/ui/top/window.ini"),
               "[Init]\nName=TopWindow\nImage=window-top.asf\n"),
               "write XJXQY top window fixture") && ok;
    ok = check(writeUtf8TextFile(xjxqySource.filePath("ini/ui/title/window1.ini"),
               "[Init]\nWidth=640\nHeight=480\nBitmap=title2.png\n"
               "ScaleChildren=false\nCenterChildren=true\n"),
               "write XJXQY alternate title window fixture") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions xjxqyOptions;
    xjxqyOptions.convertScript = false;
    xjxqyOptions.dependencyId = "XJXQY";
    AssetMigrationReport xjxqyReport;
    MigrationResult xjxqyResult = migrator.migrate(
        xjxqySourceDir.path(), xjxqyOutputDir.path(), xjxqyOptions, xjxqyReport);
    ok = check(xjxqyResult == MigrationResult::Success,
               "XJXQY UI window defaults migration succeeds") && ok;

    const QString xjxqyTopText = readUtf8TextFile(
        QDir(xjxqyOutputDir.path()).filePath("ini/ui/top/window.ini"));
    const QString xjxqyTitleWindowText = readUtf8TextFile(
        QDir(xjxqyOutputDir.path()).filePath("ini/ui/title/window1.ini"));
    const QString xjxqyProfileText = readUtf8TextFile(
        QDir(xjxqyOutputDir.path()).filePath("game_profile.ini"));
    ok = check(xjxqyTopText.contains("Align=alBottomCenter"),
               "XJXQY top window gets bottom-center align") && ok;
    ok = check(xjxqyTopText.contains("AlignX=-274"),
               "XJXQY top window gets profile AlignX") && ok;
    ok = check(xjxqyTopText.contains("AlignY=-13"),
               "XJXQY top window gets profile AlignY") && ok;
    ok = check(xjxqyTitleWindowText.contains("Align=alClient") &&
               xjxqyTitleWindowText.contains("Stretch=true") &&
               xjxqyTitleWindowText.contains("KeepAspect=true") &&
               !xjxqyTitleWindowText.contains("ScaleChildren=false") &&
               !xjxqyTitleWindowText.contains("CenterChildren=true"),
               "XJXQY alternate title keeps its 640x480 composition aspect ratio") && ok;
    ok = check(xjxqyProfileText.contains(
                   QString::fromUtf8("Music=情缘之伴奏.mp3")),
               "XJXQY migration writes its title-theme default") && ok;

    return ok;
}

bool testMigrationNormalizesObjectAnimationResource()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(),
            "create object animation migration temp dirs"))
    {
        return false;
    }

    QDir source(sourceDir.path());
    if (!check(source.mkpath("ini/objres"), "create object animation migration source layout"))
        return false;
    if (!check(writeUtf8TextFile(source.filePath("ini/objres/drop.ini"),
            "[Init]\nImage=drop-static.asf\nAnimation=drop-animation.asf\nSound=drop.wav\nCustom=keep\n"),
            "write object animation migration fixture"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.convertScript = false;
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);
    QString migratedText = readUtf8TextFile(
        QDir(outputDir.path()).filePath("ini/objres/drop.ini"));

    bool ok = true;
    ok = check(result == MigrationResult::Success,
               "object animation resource migration succeeds") && ok;
    ok = check(migratedText.contains("[Common]") &&
               migratedText.contains("Image=drop-static.asf") &&
               migratedText.contains("Animation=drop-animation.asf") &&
               migratedText.contains("Sound=drop.wav"),
               "object resource normalization moves Animation with the runtime resource keys") && ok;
    ok = check(migratedText.count("Animation=drop-animation.asf") == 1 &&
               migratedText.contains("Custom=keep"),
               "object resource normalization preserves custom fields without duplicating Animation") && ok;
    return ok;
}

bool testMigrationLowercasesResourceNamesAndReferences()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(),
               "create lowercase resource migration temp dirs"))
    {
        return false;
    }

    QDir source(sourceDir.path());
    if (!check(source.mkpath("Ini/Magic") &&
                   source.mkpath("Script/Common") &&
                   source.mkpath("Map") &&
                   source.mkpath("Asf"),
               "create lowercase resource migration source layout"))
    {
        return false;
    }

    const QString sourceIni = source.filePath("Ini/Magic/Spell.INI");
    const QString sourceScript = source.filePath(
        "Script/Common/UseSpell.TXT");
    if (!check(writeUtf8TextFile(
                       sourceIni,
                       "[Init]\n"
                       "Name=FireBall.MPC\n"
                       "Intro=Keep INTRO.MPC Text\n"
                       "Image=Effect\\FireBall.MPC\n"
                       "Sound=Magic\\Cast.WAV\n"
                       "ScriptFile=CastScript.TXT\n"
                       "GoodsName=Goods601_Test.INI\n"
                       "Custom=KEEP\n") &&
                   writeUtf8TextFile(
                       sourceScript,
                       "PlayMovie(\"Opening.WMV\");\n"
                       "Say(\"Keep INTRO.MPC Text\",0);\n"
                       "LoadMap(\"Map_001_Test.MAP\");\n"
                       "LoadNpc(\"Scene.NPC\");\n"
                       "LoadObj(\"Scene.OBJ\");\n"
                       "SetNpcTalkContent(\"Guard\",\"Keep FILE.INI Text\",\"Scene.NPC\");\n"
                       "PlaySound(\"Battle_Sound\");\n"
                       "PlayRandomMusic(\"Track_A\",\"Track_B\",\"Track_C\");\n"
                       "AddOneMagic(\"PlayerA\",\"Player-Magic-Test.INI\");\n"
                       "GetPlayerMagicLevel(\"Player-Magic-Test.INI\",\"Level\");\n"),
               "write lowercase resource migration fixtures"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.convertScript = true;
    options.writeModProfile = false;
    options.sourceEncoding = QStringLiteral("utf8");
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result = migrator.migrate(
        sourceDir.path(), outputDir.path(), options, report);

    QDir output(outputDir.path());
    const QString iniText = readUtf8TextFile(
        output.filePath("ini/magic/spell.ini"));
    const QString scriptText = readUtf8TextFile(
        output.filePath("script/common/usespell.txt"));
    const QStringList iniEntries = QDir(
        output.filePath("ini/magic")).entryList(
            QDir::Files, QDir::Name);

    bool ok = true;
    ok = check(result == MigrationResult::Success,
               "lowercase resource migration succeeds") && ok;
    ok = check(iniEntries == QStringList{QStringLiteral("spell.ini")},
               "resource output file name is lowercase on disk") && ok;
    ok = check(iniText.contains("[Init]") &&
                   iniText.contains("Image=effect\\fireball.mpc") &&
                   iniText.contains("Sound=magic\\cast.wav") &&
                   iniText.contains("ScriptFile=castscript.txt") &&
                   iniText.contains("GoodsName=goods601_test.ini"),
               "INI resource references are lowercase while schema casing is preserved") && ok;
    ok = check(iniText.contains("Name=FireBall.MPC") &&
                   iniText.contains("Intro=Keep INTRO.MPC Text") &&
                   iniText.contains("Custom=KEEP"),
               "INI display text and non-resource values preserve case") && ok;
    ok = check(scriptText.contains("\"opening.wmv\"") &&
                   scriptText.contains("\"map_001_test.map\"") &&
                   scriptText.contains("\"scene.npc\"") &&
                   scriptText.contains("\"scene.obj\"") &&
                   scriptText.contains("\"battle_sound\"") &&
                   scriptText.contains("\"track_a\"") &&
                   scriptText.contains("\"track_b\"") &&
                   scriptText.contains("\"track_c\"") &&
                   scriptText.contains("\"player-magic-test.ini\""),
               "script resource references are lowercase") && ok;
    ok = check(scriptText.contains("\"Keep INTRO.MPC Text\"") &&
                   scriptText.contains("\"Keep FILE.INI Text\"") &&
                   scriptText.contains("\"PlayerA\"") &&
                   scriptText.contains("\"Level\""),
               "script dialogue, names and output variables preserve case") && ok;
    return ok;
}

bool testMigrationHandlesKnownMoonShadowMoveScreenAnomalies()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(),
               "create MoveScreen repair migration temp dirs"))
    {
        return false;
    }

    QDir source(sourceDir.path());
    const QString missingSpeedDirectory = QString::fromUtf8(
        "script/map/map_033_落叶谷");
    const QString punctuationTypoDirectory = QString::fromUtf8(
        "script/map/map_029_码头");
    if (!check(source.mkpath(missingSpeedDirectory) &&
                   source.mkpath(punctuationTypoDirectory),
               "create MoveScreen repair source layout"))
    {
        return false;
    }

    const QString missingSpeedPath = source.filePath(
        missingSpeedDirectory + QString::fromUtf8("/事件71_1.txt"));
    const QString punctuationTypoPath = source.filePath(
        punctuationTypoDirectory + QString::fromUtf8("/结局2紫轩死亡.txt"));
    if (!check(
            writeUtf8TextFile(
                missingSpeedPath,
                "MoveScreen(5,50);\n") &&
                writeUtf8TextFile(
                    punctuationTypoPath,
                    "MoveScreen(5,80,1);\n"
                    "MoveScreen(1,80,1);\n"
                    "MoveScreen(5,80,1);\n"
                    "MoveScreen(1.80,1);\n"),
            "write known MoonShadow MoveScreen fixtures"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.resourceTypes = {AssetResourceType::Scripts};
    options.convertScript = true;
    options.writeModProfile = false;
    options.sourceEncoding = QStringLiteral("utf8");
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result = migrator.migrate(
        sourceDir.path(), outputDir.path(), options, report);

    QDir output(outputDir.path());
    const QString preservedTwoArgumentCall = readUtf8TextFile(
        output.filePath(
            missingSpeedDirectory + QString::fromUtf8("/事件71_1.txt")));
    const QString repairedPunctuationTypo = readUtf8TextFile(
        output.filePath(
            punctuationTypoDirectory +
            QString::fromUtf8("/结局2紫轩死亡.txt")));

    bool ok = true;
    ok = check(result == MigrationResult::Success,
               "known MoonShadow MoveScreen repair migration succeeds") && ok;
    ok = check(preservedTwoArgumentCall.contains("movescreen(5,50);") &&
                   !preservedTwoArgumentCall.contains("movescreen(5,50,1);"),
               "migration preserves an unresolved two-argument MoveScreen without inventing a speed") && ok;
    ok = check(repairedPunctuationTypo.contains("movescreen(1,80,1);") &&
                   !repairedPunctuationTypo.contains("movescreen(1.80,1);"),
               "migration repairs the punctuation typo using the symmetric branch") && ok;
    return ok;
}

bool testMigrationSourceEncodingOptionPreservesChineseText()
{
    const QByteArray utf8MapName = QByteArray::fromHex("e6b299e6bca0e4b98be68898");
    const QByteArray gbkMapName = QByteArray::fromHex("c9b3c4aed6aed5bd");
    const QString expectedMapName = QString::fromUtf8(utf8MapName.constData(), utf8MapName.size());
    const QByteArray accidentallyValidGbk = QByteArray::fromHex("d2a9c6b7"); // 药品 -> ҩƷ as UTF-8
    const QByteArray accidentallyValidGbkMoney = QByteArray::fromHex("c7ae"); // 钱 -> Ǯ as UTF-8
    const QByteArray accidentallyValidGbkStone = QByteArray::fromHex("caafcdb7"); // 石头 -> ʯͷ as UTF-8
    const QByteArray accidentallyValidGbkKill = QByteArray::fromHex("c9b1"); // 杀 -> ɱ as UTF-8

    auto runCase = [&](const QString& sourceEncoding, const QByteArray& mapNameBytes,
                       const QString& expectedName, const char* label,
                       bool fileHasUtf8Bom = false) {
        QTemporaryDir sourceDir;
        QTemporaryDir outputDir;
        if (!check(sourceDir.isValid() && outputDir.isValid(),
                QString::fromUtf8("%1: create encoding migration temp dirs").arg(label)))
        {
            return false;
        }

        QDir source(sourceDir.path());
        if (!check(source.mkpath("script") && source.mkpath("ini") &&
                source.mkpath("map") && source.mkpath("asf"),
                QString::fromUtf8("%1: create minimal legacy asset layout").arg(label)))
        {
            return false;
        }

        QByteArray fixture;
        if (fileHasUtf8Bom)
            fixture.append(QByteArray::fromHex("efbbbf"));
        fixture.append("[State]\nMap=");
        fixture.append(mapNameBytes);
        fixture.append(".MAP\n");
        if (!check(writeRawFile(source.filePath("ini/sample.ini"), fixture),
                QString::fromUtf8("%1: write source encoding fixture").arg(label)))
        {
            return false;
        }

        AssetMigrationOptions options;
        options.convertScript = false;
        options.sourceEncoding = sourceEncoding;

        AssetMigrationReport report;
        JxAssetMigrator migrator;
        MigrationResult result = migrator.migrate(sourceDir.path(), outputDir.path(), options, report);
        QString migratedText = readUtf8TextFile(QDir(outputDir.path()).filePath("ini/sample.ini"));

        if (result != MigrationResult::Success)
        {
            std::cerr << label << ": "
                      << report.logLines.join(" | ").toStdString()
                      << std::endl;
        }

        bool ok = true;
        ok = check(result == MigrationResult::Success,
                   QString::fromUtf8("%1: migration succeeds").arg(label)) && ok;
        ok = check(migratedText.contains("Map=" + expectedName + ".map"),
                   QString::fromUtf8(
                       "%1: source encoding keeps Chinese map name intact and lowercases the resource suffix").
                       arg(label)) && ok;
        return ok;
    };

    bool ok = true;
    ok = runCase("gbk", gbkMapName, expectedMapName, "GBK source") && ok;
    ok = runCase("utf8", utf8MapName, expectedMapName, "UTF-8 source") && ok;
    ok = runCase("gbk", accidentallyValidGbk, QString::fromUtf8("药品"),
                 "GBK accidentally-valid UTF-8 byte sequence") && ok;
    ok = runCase("gbk", accidentallyValidGbkMoney, QString::fromUtf8("钱"),
                 "GBK money source") && ok;
    ok = runCase("gbk", accidentallyValidGbkStone, QString::fromUtf8("石头"),
                 "GBK stone source") && ok;
    ok = runCase("gbk", accidentallyValidGbkKill, QString::fromUtf8("杀"),
                 "GBK single-character source") && ok;
    return ok;
}

bool testMigrationConvertsDropObjectNamesAndReferencesFromGbk()
{
    QTemporaryDir sourceDir;
    QTemporaryDir outputDir;
    if (!check(sourceDir.isValid() && outputDir.isValid(),
            "create GBK drop object migration temp dirs"))
    {
        return false;
    }

    QDir source(sourceDir.path());
    if (!check(source.mkpath("script") && source.mkpath("ini/obj") &&
            source.mkpath("ini/objres") && source.mkpath("map") &&
            source.mkpath("asf"),
            "create GBK drop object migration layout"))
    {
        return false;
    }

    const QByteArray drugGbk = QByteArray::fromHex("d2a9c6b7");
    QByteArray objectDefinition("[Init]\nObjName=");
    objectDefinition += drugGbk;
    objectDefinition += "\nObjFile=obj-";
    objectDefinition += drugGbk;
    objectDefinition += ".ini\n";
    if (!check(writeRawFile(
            source.filePath(QString::fromUtf8("ini/obj/可捡药品.ini")),
            objectDefinition),
            "write authoritative GBK drop object definition") ||
        !check(writeRawFile(
            source.filePath(QString::fromUtf8("ini/objres/obj-药品.ini")),
            QByteArray("[Init]\nImage=drug.asf\n")),
            "write referenced drop object resource"))
    {
        return false;
    }

    AssetMigrationOptions options;
    options.convertScript = false;
    options.modType = 1;
    options.sourceEncoding = QStringLiteral("gbk");
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    const MigrationResult result = migrator.migrate(
        sourceDir.path(), outputDir.path(), options, report);
    const QString migratedObject = readUtf8TextFile(
        QDir(outputDir.path()).filePath(
            QString::fromUtf8("ini/obj/可捡药品.ini")));
    const QString referencedRelativePath =
        QString::fromUtf8("ini/objres/obj-药品.ini");

    bool ok = true;
    ok = check(result == MigrationResult::Success,
        "GBK drop object migration succeeds") && ok;
    ok = check(migratedObject.contains(QString::fromUtf8("ObjName=药品")) &&
        migratedObject.contains(QString::fromUtf8("ObjFile=obj-药品.ini")),
        "drop object name and ObjFile reference are decoded by the shared editor boundary") && ok;
    ok = check(QFileInfo(
        QDir(outputDir.path()).filePath(referencedRelativePath)).isFile(),
        "converted drop object ObjFile target exists on disk") && ok;
    return ok;
}

bool testMigrationRejectsUnknownOption()
{
    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "create unknown-option test temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourceDir = root.filePath("source");
    QString outputDir = root.filePath("output");
    if (!check(root.mkpath("source") && root.mkpath("output"),
            "create unknown-option test fixture dirs"))
    {
        return false;
    }

    const QByteArray stdoutPath = QFile::encodeName(root.filePath("cli-stdout.txt"));
    const QByteArray stderrPath = QFile::encodeName(root.filePath("cli-stderr.txt"));
    FILE* stdoutFile = std::fopen(stdoutPath.constData(), "w+b");
    FILE* stderrFile = std::fopen(stderrPath.constData(), "w+b");
    if (!check(stdoutFile != nullptr && stderrFile != nullptr,
            "open temporary FILE streams for unknown-option test"))
    {
        if (stdoutFile) std::fclose(stdoutFile);
        if (stderrFile) std::fclose(stderrFile);
        return false;
    }

    QStringList arguments = {
        "jxqy-editor-cli",
        "migrate-assets",
        sourceDir,
        outputDir,
        "--unknown-option-for-test"
    };
    int exitCode = AssetCliRunner::run(arguments, stdoutFile, stderrFile);
    QString output = readTemporaryFile(stdoutFile);
    QString errors = readTemporaryFile(stderrFile);

    std::fclose(stdoutFile);
    std::fclose(stderrFile);

    bool ok = true;
    ok = check(exitCode == 2, "unknown migrate-assets option returns a usage error") && ok;
    ok = check(errors.contains("Unknown option"), "usage error identifies an unknown option") && ok;
    ok = check(errors.contains("--unknown-option-for-test"), "usage error identifies the supplied option") && ok;
    ok = check(!output.contains("Migration Summary"), "usage error stops before migration") && ok;
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath(".jxqy_asset_migration_marker")),
               "usage error leaves the output directory untouched") && ok;

    return ok;
}

bool testMigrationIsolatedFromExternalAssets()
{
    // The migrator must only consume resources that physically exist inside the
    // source directory. A sibling resource tree is an independent input and
    // cannot influence this migration output.
    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "create isolation test temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourceDir = root.filePath("source");
    QString outputDir = root.filePath("output");
    QString siblingAssetsDir = root.filePath("assets");

    QDir source(sourceDir);
    if (!check(source.mkpath("ini/npc") && source.mkpath("ini/ui") &&
            source.mkpath("script/common") && source.mkpath("asf") && source.mkpath("map"),
            "create isolation test source layout"))
    {
        return false;
    }

    bool ok = true;
    // Real source files that the migrator is expected to convert 1:1.
    ok = check(writeUtf8TextFile(source.filePath("ini/npc/sample.npc"), "[Init]\nName=Sample\n"),
               "write isolation test source npc fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("ini/ui/real_ui.ini"), "[init]\nname=Real\n"),
               "write isolation test source ui fixture") && ok;
    ok = check(writeUtf8TextFile(source.filePath("script/common/note.txt"), "note\n"),
               "write isolation test source script fixture") && ok;
    if (!ok)
        return false;

    QDir sibling(siblingAssetsDir);
    if (!check(sibling.mkpath("ini/ui") && sibling.mkpath("save/rpg0") &&
            sibling.mkpath("asf") && sibling.mkpath("mpc"),
            "create isolation test sibling assets layout"))
    {
        return false;
    }
    ok = check(writeUtf8TextFile(sibling.filePath("ini/ui/leaked_ui.ini"), "[init]\nname=Leaked\n"),
               "write isolation test sibling ui marker") && ok;
    ok = check(writeUtf8TextFile(sibling.filePath("save/rpg0/leaked_save.ini"), "[State]\nMap=leaked\n"),
               "write isolation test sibling save marker") && ok;
    ok = check(writeUtf8TextFile(sibling.filePath("save/rpg0/game.ini"), "[State]\nMap=leaked\n"),
               "write isolation test sibling save skeleton marker") && ok;
    ok = check(writeUtf8TextFile(sibling.filePath("partneridx.ini"), "[Partner]\n"),
               "write isolation test sibling root file marker") && ok;
    ok = check(writeUtf8TextFile(sibling.filePath("mpc/leaked_image.png"), "png-bytes\n"),
               "write isolation test sibling image marker") && ok;
    ok = check(writeUtf8TextFile(sibling.filePath("asf/leaked_character.asf"), "asf-bytes\n"),
               "write isolation test sibling asf marker") && ok;
    if (!ok)
        return false;

    AssetMigrationOptions options;
    options.convertScript = false;
    AssetMigrationReport report;
    JxAssetMigrator migrator;
    MigrationResult result = migrator.migrate(sourceDir, outputDir, options, report);

    ok = check(result != MigrationResult::Failed, "isolated source migration succeeds") && ok;
    // Positive control: the migrator did run and converted source files.
    ok = check(QFileInfo::exists(QDir(outputDir).filePath("ini/npc/sample.npc")),
               "isolation test converts real source npc file") && ok;
    ok = check(QFileInfo::exists(QDir(outputDir).filePath("ini/ui/real_ui.ini")),
               "isolation test converts real source ui file") && ok;

    // The published tree remains limited to the selected source directory.
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath("ini/ui/leaked_ui.ini")),
               "isolation test does not copy external ui asset") && ok;
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath("save/rpg0/leaked_save.ini")),
               "isolation test does not copy external save asset") && ok;
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath("save/rpg0/game.ini")),
               "isolation test does not generate save skeleton from external assets") && ok;
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath("partneridx.ini")),
               "isolation test does not copy external root file") && ok;
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath("mpc/leaked_image.png")),
               "isolation test does not copy external image asset") && ok;
    ok = check(!QFileInfo::exists(QDir(outputDir).filePath("asf/leaked_character.asf")),
               "isolation test does not copy external asf alias asset") && ok;
    ok = check(!QDir(QDir(outputDir).filePath("save")).exists(),
               "isolation test does not generate any save skeleton directory") && ok;

    // Exhaustive check: no file present under the sibling assets directory may
    // appear under the output directory by its relative path.
    QDirIterator leakScanner(siblingAssetsDir, QDir::Files, QDirIterator::Subdirectories);
    while (leakScanner.hasNext())
    {
        QString externalFile = leakScanner.next();
        QString relative = QDir(siblingAssetsDir).relativeFilePath(externalFile);
        ok = check(!QFileInfo::exists(QDir(outputDir).filePath(relative)),
                   QString::fromUtf8("isolation test does not leak external file %1").arg(relative)) && ok;
    }

    return ok;
}

bool testAssetCliValidateScriptsReportsFailures()
{
    QTemporaryDir assetsDir;
    if (!check(assetsDir.isValid(), "create CLI validate-scripts temp assets dir"))
        return false;

    QDir root(assetsDir.path());
    if (!check(root.mkpath("script"), "create CLI validate-scripts script dir"))
        return false;

    bool ok = check(writeUtf8TextFile(root.filePath("script/bad.lua"), "if then\n"),
                    "write CLI validate-scripts invalid fixture");
    if (!ok)
        return false;

    const QByteArray stdoutPath = QFile::encodeName(root.filePath("cli-stdout.txt"));
    const QByteArray stderrPath = QFile::encodeName(root.filePath("cli-stderr.txt"));
    FILE* stdoutFile = std::fopen(stdoutPath.constData(), "w+b");
    FILE* stderrFile = std::fopen(stderrPath.constData(), "w+b");
    if (!check(stdoutFile != nullptr && stderrFile != nullptr,
            "open temporary FILE streams for CLI validate-scripts test"))
    {
        if (stdoutFile) std::fclose(stdoutFile);
        if (stderrFile) std::fclose(stderrFile);
        return false;
    }

    QStringList arguments = {
        "jxqy-editor-cli",
        "validate-scripts",
        assetsDir.path()
    };
    int exitCode = AssetCliRunner::run(arguments, stdoutFile, stderrFile);
    QString output = readTemporaryFile(stdoutFile);
    QString errors = readTemporaryFile(stderrFile);

    std::fclose(stdoutFile);
    std::fclose(stderrFile);

    ok = check(exitCode == 1, "CLI validate-scripts returns failure exit code for syntax errors") && ok;
    ok = check(output.contains("Failures: 1"), "CLI validate-scripts reports failure count") && ok;
    ok = check(output.contains("bad.lua"), "CLI validate-scripts reports failing script path") && ok;
    ok = check(output.contains("Status: Failed"), "CLI validate-scripts reports failed status") && ok;
    ok = check(errors.isEmpty(), "CLI validate-scripts syntax failures are reported on stdout summary") && ok;

    return ok;
}

bool testAssetCliValidateScriptsScansResourceCollection()
{
    QTemporaryDir assetsDir;
    if (!check(assetsDir.isValid(), "create CLI validate-scripts collection temp dir"))
        return false;

    QDir root(assetsDir.path());
    bool ok = check(root.mkpath("goodpack/script") && root.mkpath("badpack/script") &&
                    root.mkpath("autodiscovered/script"),
                    "create CLI validate-scripts collection packs");
    if (!ok)
        return false;

    ok = check(writeUtf8TextFile(root.filePath("goodpack/game_profile.ini"), "[Game]\nId=GOOD\n"),
               "write good pack profile") && ok;
    ok = check(writeUtf8TextFile(root.filePath("badpack/game_profile.ini"), "[Game]\nId=BAD\n"),
               "write bad pack profile") && ok;
    ok = check(writeUtf8TextFile(root.filePath("autodiscovered/game_profile.ini"),
                                "[Game]\nId=AUTODISCOVERED\n"),
               "write automatically discovered pack profile") && ok;
    ok = check(writeUtf8TextFile(root.filePath("resources.ini"),
        "[Collection]\nCommonPath=common\n"),
        "write validate-scripts collection configuration") && ok;
    ok = check(writeUtf8TextFile(root.filePath("goodpack/script/good.lua"), "return 1\n"),
               "write good pack script") && ok;
    ok = check(writeUtf8TextFile(root.filePath("badpack/script/bad.lua"), "if then\n"),
               "write bad pack script") && ok;
    ok = check(writeUtf8TextFile(root.filePath("autodiscovered/script/discovered.lua"),
                                "if then\n"),
               "write automatically discovered failing script") && ok;
    if (!ok)
        return false;

    const QByteArray stdoutPath = QFile::encodeName(root.filePath("collection-stdout.txt"));
    const QByteArray stderrPath = QFile::encodeName(root.filePath("collection-stderr.txt"));
    FILE* stdoutFile = std::fopen(stdoutPath.constData(), "w+b");
    FILE* stderrFile = std::fopen(stderrPath.constData(), "w+b");
    if (!check(stdoutFile != nullptr && stderrFile != nullptr,
            "open temporary FILE streams for collection validate-scripts test"))
    {
        if (stdoutFile) std::fclose(stdoutFile);
        if (stderrFile) std::fclose(stderrFile);
        return false;
    }

    QStringList arguments = {
        "jxqy-editor-cli",
        "validate-scripts",
        assetsDir.path()
    };
    int exitCode = AssetCliRunner::run(arguments, stdoutFile, stderrFile);
    QString output = readTemporaryFile(stdoutFile);
    QString errors = readTemporaryFile(stderrFile);

    std::fclose(stdoutFile);
    std::fclose(stderrFile);

    ok = check(exitCode == 1, "CLI validate-scripts returns failure for collection pack errors") && ok;
    ok = check(output.contains("Resource packs: 3"),
               "CLI validate-scripts reports collection resource pack count") && ok;
    ok = check(output.contains("goodpack") && output.contains("badpack") &&
                   output.contains("autodiscovered"),
               "CLI validate-scripts reports per-pack summaries") && ok;
    ok = check(output.contains("Total script directory files: 3"),
               "CLI validate-scripts aggregates collection script totals") && ok;
    ok = check(output.contains("Checked scripts: 3"),
               "CLI validate-scripts aggregates checked collection scripts") && ok;
    ok = check(output.contains("Failures: 2"),
               "CLI validate-scripts aggregates collection failures") && ok;
    ok = check(output.contains("bad.lua") && output.contains("discovered.lua"),
               "CLI validate-scripts reports failing collection script path") && ok;
    ok = check(output.contains("Status: Failed"),
               "CLI validate-scripts reports failed collection status") && ok;
    ok = check(errors.isEmpty(),
               "CLI validate-scripts collection failures are reported on stdout summary") && ok;

    return ok;
}

bool testTransparentPlaceholderImageLoadsAsImp()
{
    const std::string fileName = "core-regression-placeholder.img";
    QImage image(1, 1, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QByteArray pngData;
    QBuffer buffer(&pngData);
    if (!check(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"),
               "create placeholder PNG fixture"))
    {
        return false;
    }

    FILE* file = Util::openFileForWriteUtf8(fileName);
    if (!check(file != nullptr, "open placeholder IMG fixture"))
        return false;

    auto writeInt = [&](int32_t value) {
        return std::fwrite(&value, 1, 4, file) == 4;
    };

    const char head[] = "IMG File Ver1.0";
    bool written = std::fwrite(head, 1, 16, file) == 16 &&
        writeInt(1) &&
        writeInt(1) &&
        writeInt(30);
    for (int i = 0; i < 5 && written; i++)
        written = writeInt(0);
    int32_t dataLen = pngData.size();
    written = written &&
        writeInt(dataLen) &&
        writeInt(0) &&
        writeInt(0) &&
        writeInt(0) &&
        std::fwrite(pngData.constData(), 1, pngData.size(), file) ==
            static_cast<size_t>(pngData.size());
    std::fclose(file);

    PicFileEditor editor;
    bool loaded = written && editor.loadFromFile(fileName);
    QImage loadedImage = editor.getFrameImage(0);
    std::remove(fileName.c_str());

    return check(loaded, "load transparent placeholder IMG") &&
        check(loadedImage.size() == QSize(1, 1), "transparent placeholder IMG size") &&
        check(loadedImage.pixelColor(0, 0).alpha() == 0, "transparent placeholder IMG alpha");
}

bool testLegacyImageDirectory(const QString& directoryPath)
{
    QDir directory(directoryPath);
    if (!check(directory.exists(), "legacy image directory exists"))
        return false;
    QTemporaryDir convertedSamples;
    if (!check(convertedSamples.isValid(),
            "create real legacy image conversion sample directory"))
    {
        return false;
    }

    int checkedFiles = 0;
    QDirIterator iterator(
        directoryPath,
        {"*.mpc", "*.shd", "*.asf"},
        QDir::Files,
        QDirIterator::Subdirectories);
    int failedFiles = 0;
    int runtimeMpcSamples = 0;
    int runtimeShdSamples = 0;
    int runtimeAsfSamples = 0;
    int convertedSamplesChecked = 0;
    while (iterator.hasNext())
    {
        QString filePath = iterator.next();
        PicFileEditor editor;
        if (!editor.loadFromFile(filePath.toUtf8().toStdString()))
        {
            std::cerr << "FAILED: load real legacy image "
                      << filePath.toUtf8().constData() << '\n';
            failedFiles++;
        }
        else
        {
            QString suffix = QFileInfo(filePath).suffix().toLower();
            int* sampleCount = nullptr;
            if (suffix == "mpc")
                sampleCount = &runtimeMpcSamples;
            else if (suffix == "shd")
                sampleCount = &runtimeShdSamples;
            else if (suffix == "asf")
                sampleCount = &runtimeAsfSamples;

            if (sampleCount && *sampleCount < 3)
            {
                std::vector<uint8_t> data =
                    Util::readFileToBuffer(filePath.toUtf8().toStdString());
                PicDecodedFile decoded;
                if (data.empty() ||
                    !PicDecoder::decodeToPixels(
                        data.data(), static_cast<int>(data.size()), decoded))
                {
                    std::cerr << "FAILED: runtime decode real legacy image "
                              << filePath.toUtf8().constData() << '\n';
                    failedFiles++;
                }
                (*sampleCount)++;
            }

            if (convertedSamplesChecked < 3)
            {
                const QString convertedPath = convertedSamples.filePath(
                    QStringLiteral("sample-%1.img")
                        .arg(convertedSamplesChecked));
                PicFileEditor converted;
                bool equivalent = editor.saveAsIMP(
                        convertedPath.toUtf8().toStdString()) &&
                    converted.loadFromFile(
                        convertedPath.toUtf8().toStdString()) &&
                    converted.getFrameCount() == editor.getFrameCount();
                for (int frame = 0;
                     equivalent && frame < editor.getFrameCount(); ++frame)
                {
                    int32_t sourceOffsetX = 0;
                    int32_t sourceOffsetY = 0;
                    int32_t convertedOffsetX = 0;
                    int32_t convertedOffsetY = 0;
                    editor.getFrameOffset(
                        frame, &sourceOffsetX, &sourceOffsetY);
                    converted.getFrameOffset(
                        frame, &convertedOffsetX, &convertedOffsetY);
                    equivalent = sourceOffsetX == convertedOffsetX &&
                        sourceOffsetY == convertedOffsetY &&
                        editor.getFrameImage(frame) ==
                            converted.getFrameImage(frame);
                }
                if (!equivalent)
                {
                    std::cerr << "FAILED: real legacy image IMP round-trip "
                              << filePath.toUtf8().constData() << '\n';
                    failedFiles++;
                }
                convertedSamplesChecked++;
            }
        }
        checkedFiles++;
    }

    std::cout << "Checked legacy images: " << checkedFiles
              << ", failed: " << failedFiles
              << " in " << directoryPath.toUtf8().constData() << '\n';
    return check(checkedFiles > 0, "legacy image directory contains images") &&
        check(convertedSamplesChecked > 0,
              "real legacy image conversion samples preserve pixels, alpha, dimensions, offsets and metadata") &&
        check(failedFiles == 0, "load all real legacy images");
}

// Transparent-edge crop tests use the runtime IMP offset convention. Cropping
// must preserve opaque pixels' world coordinates, including negative offsets,
// fully transparent frames, repeated cropping, and MPC/ASF migration output.
std::map<std::pair<int, int>, QRgb> opaqueWorldCoords(const QImage& image, int xOffset, int yOffset)
{
    std::map<std::pair<int, int>, QRgb> coords;
    if (image.isNull())
        return coords;
    for (int y = 0; y < image.height(); y++)
    {
        for (int x = 0; x < image.width(); x++)
        {
            QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) != 0)
                coords[std::make_pair(x - xOffset, y - yOffset)] = pixel;
        }
    }
    return coords;
}

QImage makePaddedFrame(int fullWidth, int fullHeight,
    int opaqueX, int opaqueY, int opaqueW, int opaqueH, QRgb color)
{
    QImage image(fullWidth, fullHeight, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = opaqueY; y < opaqueY + opaqueH; y++)
        for (int x = opaqueX; x < opaqueX + opaqueW; x++)
            image.setPixel(x, y, color);
    return image;
}

std::vector<uint8_t> buildAsfFileFromImages(const std::vector<QImage>& frames,
    int directions, int interval, int xMove, int yMove)
{
    int width = frames.empty() ? 0 : frames[0].width();
    int height = frames.empty() ? 0 : frames[0].height();

    MPCPalette palette;
    palette.length = 256;
    palette.colors.resize(256);
    palette.colors[0] = {0, 0, 0, 0};      // transparent (BGRA)
    palette.colors[1] = {0, 0, 255, 255};  // opaque red

    ASFFileHead head = {};
    std::memcpy(head.head, "ASF 1.01", 8);
    head.width = width;
    head.height = height;
    head.picCount = static_cast<int32_t>(frames.size());
    head.directions = directions;
    head.paletteLen = 256;
    head.interval = interval;
    head.xMove = xMove;
    head.yMove = yMove;

    std::vector<uint8_t> buffer;
    appendValue(buffer, head);
    const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.colors.data());
    buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.colors.size() * sizeof(ColorARGB));

    const size_t tableOffset = buffer.size();
    const size_t tableBytes = frames.size() * 8;  // absolute offset + length per frame
    buffer.insert(buffer.end(), tableBytes, 0);

    std::vector<std::vector<uint8_t>> encoded;
    encoded.reserve(frames.size());
    for (const QImage& frame : frames)
        encoded.push_back(PicFileEditor::encodeASFFrame(frame, palette));

    for (size_t i = 0; i < frames.size(); i++)
    {
        int32_t absoluteOffset = static_cast<int32_t>(buffer.size());
        int32_t length = static_cast<int32_t>(encoded[i].size());
        std::memcpy(buffer.data() + tableOffset + i * 8, &absoluteOffset, sizeof(int32_t));
        std::memcpy(buffer.data() + tableOffset + i * 8 + sizeof(int32_t), &length, sizeof(int32_t));
        buffer.insert(buffer.end(), encoded[i].begin(), encoded[i].end());
    }
    return buffer;
}

int runCliCapture(const QStringList& arguments, QString& capturedStdout, QString& capturedStderr)
{
    QTemporaryDir captureDir;
    if (!captureDir.isValid())
        return -1;

    const QByteArray outPath = QFile::encodeName(captureDir.path() + "/cli-out.txt");
    const QByteArray errPath = QFile::encodeName(captureDir.path() + "/cli-err.txt");
    FILE* outFile = std::fopen(outPath.constData(), "w+b");
    FILE* errFile = std::fopen(errPath.constData(), "w+b");
    if (outFile == nullptr || errFile == nullptr)
    {
        if (outFile) std::fclose(outFile);
        if (errFile) std::fclose(errFile);
        return -1;
    }

    int exitCode = AssetCliRunner::run(arguments, outFile, errFile);
    capturedStdout = readTemporaryFile(outFile);
    capturedStderr = readTemporaryFile(errFile);
    std::fclose(outFile);
    std::fclose(errFile);
    return exitCode;
}

bool testMigrationCliResourceTypes()
{
    QTemporaryDir rootDirectory;
    if (!check(rootDirectory.isValid(), "create resource-type CLI temp root"))
        return false;

    QDir root(rootDirectory.path());
    const QString sourceRoot = root.filePath(QStringLiteral("source"));
    if (!check(root.mkpath(QStringLiteral("source/script")) &&
                   root.mkpath(QStringLiteral("source/map")) &&
                   root.mkpath(QStringLiteral("source/img")) &&
                   root.mkpath(QStringLiteral("source/sound")) &&
                   root.mkpath(QStringLiteral("source/ini/npc")) &&
                   writeRawFile(root.filePath(QStringLiteral("source/script/main.txt")),
                                QByteArray("local value = 1\n")) &&
                   writeRawFile(root.filePath(QStringLiteral("source/map/area.scc")),
                                QByteArray("map-sidecar")) &&
                   writeRawFile(root.filePath(QStringLiteral("source/img/picture.bmp")),
                                QByteArray("image-bytes")) &&
                   writeRawFile(root.filePath(QStringLiteral("source/sound/click.wav")),
                                QByteArray("audio-bytes")) &&
                   writeRawFile(root.filePath(QStringLiteral("source/ini/npc/sample.ini")),
                                QByteArray("[Init]\nName=Sample\n")),
               "write resource-type CLI fixtures"))
    {
        return false;
    }

    const QString partialOutput = root.filePath(QStringLiteral("partial-output"));
    if (!check(root.mkpath(QStringLiteral("partial-output/sound")) &&
                   root.mkpath(QStringLiteral("partial-output/ini")) &&
                   root.mkpath(QStringLiteral("partial-output/mpc")) &&
                   writeRawFile(root.filePath(QStringLiteral("partial-output/sound/keep.wav")),
                                QByteArray("keep-audio")) &&
                   writeRawFile(root.filePath(QStringLiteral("partial-output/ini/keep.ini")),
                                QByteArray("keep-ini")) &&
                   writeRawFile(root.filePath(QStringLiteral("partial-output/mpc/keep.bin")),
                                QByteArray("keep-image")),
               "write resource-type preservation fixtures"))
    {
        return false;
    }

    QString capturedOut;
    QString capturedErr;
    const int partialExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, partialOutput,
        QStringLiteral("--resource-type"), QStringLiteral("Maps"),
        QStringLiteral("--resource-type"), QStringLiteral("scripts"),
        QStringLiteral("--resource-type"), QStringLiteral("maps")},
        capturedOut, capturedErr);

    const QJsonObject partialJson = QJsonDocument::fromJson(
        readRawFile(root.filePath(QStringLiteral(
            "partial-output/migration_report.json"))))
        .object();
    const QJsonArray selectedTypes =
        partialJson.value(QStringLiteral("selectedResourceTypes")).toArray();
    const QJsonObject domains =
        partialJson.value(QStringLiteral("resourceDomains")).toObject();
    const QJsonObject scriptsDomain =
        domains.value(QStringLiteral("scripts")).toObject();
    const QJsonObject mapsDomain =
        domains.value(QStringLiteral("maps")).toObject();
    const QJsonObject imagesDomain =
        domains.value(QStringLiteral("images")).toObject();
    const QString partialTextReport = readUtf8TextFile(
        root.filePath(QStringLiteral("partial-output/migration_report.txt")));
    const QJsonObject partialMarker =
        QJsonDocument::fromJson(
            readRawFile(
                root.filePath(
                    QStringLiteral(
                        "partial-output/"
                        ".jxqy_asset_migration_marker")))).
            object();
    const QJsonObject partialManagedOutputs =
        partialMarker.value(
            QStringLiteral(
                "managedOutputSha256")).
            toObject();

    bool ok = true;
    ok = check((partialExitCode == 0 || partialExitCode == 1) &&
                   capturedErr.isEmpty(),
               "repeatable resource-type CLI accepts case-insensitive multi-domain selection") && ok;
    ok = check(selectedTypes == QJsonArray({QStringLiteral("scripts"),
                                            QStringLiteral("maps")}) &&
                   !partialJson.value(QStringLiteral("completeProject")).toBool(),
               "resource-type report canonicalizes and de-duplicates selected domains") && ok;
    ok = check(scriptsDomain.value(QStringLiteral("selected")).toBool() &&
                   scriptsDomain.value(QStringLiteral("processed")).toInt() == 1 &&
                   scriptsDomain.value(QStringLiteral("written")).toInt() == 1 &&
                   mapsDomain.value(QStringLiteral("selected")).toBool() &&
                   mapsDomain.value(QStringLiteral("processed")).toInt() == 1 &&
                   mapsDomain.value(QStringLiteral("written")).toInt() == 1 &&
                   !imagesDomain.value(QStringLiteral("selected")).toBool(),
               "JSON report records selected resource-domain processing and writes") && ok;
    ok = check(partialTextReport.contains(
                   QStringLiteral("Selected resource types: scripts, maps")) &&
                   partialTextReport.contains(
                       QStringLiteral("scripts: selected=yes, processed=1, written=1, failed=0")) &&
                   capturedOut.contains(QStringLiteral("Resource types: scripts, maps")) &&
                   capturedOut.contains(
                       QStringLiteral("maps: selected=yes, processed=1, written=1, failed=0")),
               "text report and CLI summary expose the same resource-domain counts") && ok;
    ok = check(QFileInfo::exists(root.filePath(QStringLiteral(
                   "partial-output/script/main.txt"))),
               "multi-domain migration publishes the scripts domain") && ok;
    ok = check(
        partialMarker.value(
            QStringLiteral(
                "schemaVersion")).toInt() == 1 &&
            partialManagedOutputs.contains(
                QStringLiteral(
                    "script/main.txt")) &&
            partialManagedOutputs.contains(
                QStringLiteral(
                    "map/area.scc")) &&
            !partialManagedOutputs.contains(
                QStringLiteral(
                    "sound/keep.wav")) &&
            !partialManagedOutputs.contains(
                QStringLiteral(
                    "ini/keep.ini")),
        "partial migration atomically publishes provenance for selected outputs without claiming preserved unselected player files") &&
        ok;
    ok = check(readRawFile(root.filePath(QStringLiteral(
                   "partial-output/map/area.scc"))) == QByteArray("map-sidecar"),
               "multi-domain migration publishes map companions byte-for-byte") && ok;
    ok = check(readRawFile(root.filePath(QStringLiteral(
                   "partial-output/sound/keep.wav"))) == QByteArray("keep-audio") &&
                   readRawFile(root.filePath(QStringLiteral(
                       "partial-output/ini/keep.ini"))) == QByteArray("keep-ini") &&
                   readRawFile(root.filePath(QStringLiteral(
                       "partial-output/mpc/keep.bin"))) == QByteArray("keep-image") &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "partial-output/img/picture.bmp"))) &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "partial-output/game_profile.ini"))),
               "multi-domain migration preserves unselected domains and project metadata") && ok;

    const QString mediaOutput = root.filePath(QStringLiteral("media-output"));
    root.mkpath(QStringLiteral("media-output/script"));
    writeRawFile(root.filePath(QStringLiteral("media-output/script/keep.txt")),
                 QByteArray("keep-script"));
    const int mediaExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, mediaOutput,
        QStringLiteral("--resource-type"), QStringLiteral("images"),
        QStringLiteral("--resource-type"), QStringLiteral("audio")},
        capturedOut, capturedErr);
    ok = check((mediaExitCode == 0 || mediaExitCode == 1) &&
                   readRawFile(root.filePath(QStringLiteral(
                       "media-output/img/picture.bmp"))) == QByteArray("image-bytes") &&
                   readRawFile(root.filePath(QStringLiteral(
                       "media-output/sound/click.wav"))) == QByteArray("audio-bytes") &&
                   readRawFile(root.filePath(QStringLiteral(
                       "media-output/script/keep.txt"))) == QByteArray("keep-script") &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "media-output/map/area.scc"))),
               "new images and audio resource types publish together while preserving scripts") && ok;

    const QString completeOutput = root.filePath(QStringLiteral("complete-output"));
    const int completeExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, completeOutput,
        QStringLiteral("--resource-type"), QStringLiteral("all")},
        capturedOut, capturedErr);
    const QJsonObject completeJson = QJsonDocument::fromJson(
        readRawFile(root.filePath(QStringLiteral(
            "complete-output/migration_report.json"))))
        .object();
    const QJsonObject completeDomains =
        completeJson.value(QStringLiteral("resourceDomains")).toObject();
    int completeDomainProcessed = 0;
    int completeDomainWritten = 0;
    for (AssetResourceType domain : assetResourceDomainTypes())
    {
        const QJsonObject domainObject = completeDomains.value(
            assetResourceTypeId(domain)).toObject();
        completeDomainProcessed +=
            domainObject.value(QStringLiteral("processed")).toInt();
        completeDomainWritten +=
            domainObject.value(QStringLiteral("written")).toInt();
    }
    const QJsonObject completeCounts =
        completeJson.value(QStringLiteral("counts")).toObject();
    ok = check((completeExitCode == 0 || completeExitCode == 1) &&
                   completeJson.value(QStringLiteral("completeProject")).toBool() &&
                   completeJson.value(QStringLiteral("selectedResourceTypes")).toArray() ==
                       QJsonArray({QStringLiteral("all")}) &&
                   QFileInfo::exists(root.filePath(QStringLiteral(
                       "complete-output/.jxqy_asset_migration_marker"))) &&
                   QFileInfo::exists(root.filePath(QStringLiteral(
                       "complete-output/game_profile.ini"))) &&
                   QFileInfo::exists(root.filePath(QStringLiteral(
                       "complete-output/img/picture.bmp"))) &&
                   QFileInfo::exists(root.filePath(QStringLiteral(
                       "complete-output/sound/click.wav"))) &&
                   QFileInfo::exists(root.filePath(QStringLiteral(
                       "complete-output/ini/npc/sample.ini"))),
               "resource-type all performs a complete-project migration") && ok;
    ok = check(completeDomainProcessed ==
                   completeCounts.value(QStringLiteral("processed")).toInt() &&
                   completeDomainWritten ==
                   completeCounts.value(QStringLiteral("written")).toInt(),
               "complete-project resource domains sum to the global report counts") && ok;

    const QString invalidOutput = root.filePath(QStringLiteral("invalid-output"));
    const int invalidTypeExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, invalidOutput,
        QStringLiteral("--resource-type"), QStringLiteral("other")},
        capturedOut, capturedErr);
    ok = check(invalidTypeExitCode == 2 &&
                   capturedErr.contains(QStringLiteral("requires all, scripts, maps, images, or audio")) &&
                   !QFileInfo::exists(invalidOutput),
               "resource-type rejects values outside the public domain set before output") && ok;

    const QString conflictOutput = root.filePath(QStringLiteral("conflict-output"));
    const int conflictExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, conflictOutput,
        QStringLiteral("--resource-type"), QStringLiteral("all"),
        QStringLiteral("--resource-type"), QStringLiteral("audio")},
        capturedOut, capturedErr);
    ok = check(conflictExitCode == 2 &&
                   capturedErr.contains(QStringLiteral("all cannot be combined")) &&
                   !QFileInfo::exists(conflictOutput),
               "resource-type rejects all combined with a concrete domain") && ok;

    const QString emptySource = root.filePath(QStringLiteral("empty-source"));
    const QString emptyDomainOutput = root.filePath(QStringLiteral("empty-domain-output"));
    root.mkpath(QStringLiteral("empty-source"));
    root.mkpath(QStringLiteral("empty-domain-output/map"));
    writeRawFile(root.filePath(QStringLiteral("empty-domain-output/map/keep.scc")),
                 QByteArray("keep-empty-domain"));
    const int emptyDomainExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        emptySource, emptyDomainOutput,
        QStringLiteral("--resource-type"), QStringLiteral("maps")},
        capturedOut, capturedErr);
    ok = check(emptyDomainExitCode == 2 &&
                   readRawFile(root.filePath(QStringLiteral(
                       "empty-domain-output/map/keep.scc"))) ==
                       QByteArray("keep-empty-domain") &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "empty-domain-output/migration_report.json"))),
               "empty explicit resource domain fails without replacing existing output") && ok;

    const QString scriptsAliasOutput =
        root.filePath(QStringLiteral("scripts-alias-output"));
    const int scriptsAliasExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, scriptsAliasOutput, QStringLiteral("--scripts-only")},
        capturedOut, capturedErr);
    ok = check((scriptsAliasExitCode == 0 || scriptsAliasExitCode == 1) &&
                   QFileInfo::exists(root.filePath(QStringLiteral(
                       "scripts-alias-output/script/main.txt"))) &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "scripts-alias-output/map/area.scc"))),
               "legacy scripts-only flag maps to the scripts resource domain") && ok;

    const QString imagesAliasOutput =
        root.filePath(QStringLiteral("images-alias-output"));
    const int imagesAliasExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, imagesAliasOutput,
        QStringLiteral("--images-only"), QStringLiteral("--include-prefix"),
        QStringLiteral("img")}, capturedOut, capturedErr);
    ok = check((imagesAliasExitCode == 0 || imagesAliasExitCode == 1) &&
                   readRawFile(root.filePath(QStringLiteral(
                       "images-alias-output/img/picture.bmp"))) ==
                       QByteArray("image-bytes") &&
                   !QFileInfo::exists(root.filePath(QStringLiteral(
                       "images-alias-output/sound/click.wav"))),
               "legacy images-only subtree flag maps to the images resource domain") && ok;

    const int helpExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("--help")},
        capturedOut, capturedErr);
    ok = check(helpExitCode == 0 &&
                   capturedOut.contains(QStringLiteral("--resource-type <type>")) &&
                   capturedOut.contains(QStringLiteral("Repeat for multiple domains")),
               "CLI help documents repeatable resource-type selection") && ok;
    return ok;
}

QByteArray fileSha256(const QString& filePath);

bool testMigrationCliImagePolicyOptions()
{
    QTemporaryDir rootDirectory;
    if (!check(rootDirectory.isValid(), "create image-policy CLI temp root"))
        return false;

    QDir root(rootDirectory.path());
    const QString sourceRoot = root.filePath(QStringLiteral("source"));
    if (!check(root.mkpath(QStringLiteral("source/mpc/character")) &&
                   root.mkpath(QStringLiteral("source/mpc/effect")) &&
                   root.mkpath(QStringLiteral("source/mpc/object")) &&
                   root.mkpath(QStringLiteral("source/mpc/map")) &&
                   root.mkpath(QStringLiteral("source/asf/ui")) &&
                   root.mkpath(QStringLiteral("source/asf/unknown")),
               "create image-policy CLI fixture directories"))
    {
        return false;
    }

    std::vector<QImage> frames;
    frames.push_back(makePaddedFrame(
        5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));
    const QString characterPath =
        root.filePath(QStringLiteral("source/mpc/character/hero.mpc"));
    const QString effectPath =
        root.filePath(QStringLiteral("source/mpc/effect/spark.mpc"));
    const QString objectPath =
        root.filePath(QStringLiteral("source/mpc/object/chest.mpc"));
    const QString mapPath =
        root.filePath(QStringLiteral("source/mpc/map/tile.mpc"));
    const QString uiPath =
        root.filePath(QStringLiteral("source/asf/ui/panel.asf"));
    const QString unknownPath =
        root.filePath(QStringLiteral("source/asf/unknown/misc.asf"));
    const std::vector<uint8_t> asf =
        buildAsfFileFromImages(frames, 1, 30, 2, 3);
    if (!check(writeMpcFileFromImages(characterPath, frames, 1) &&
                   writeMpcFileFromImages(effectPath, frames, 1) &&
                   writeMpcFileFromImages(objectPath, frames, 1) &&
                   writeMpcFileFromImages(mapPath, frames, 1) &&
                   Util::writeFileFromBuffer(
                       uiPath.toUtf8().toStdString(), asf.data(), asf.size()) &&
                   Util::writeFileFromBuffer(
                       unknownPath.toUtf8().toStdString(), asf.data(), asf.size()),
               "write image-policy CLI fixtures"))
    {
        return false;
    }

    QStringList mixedArguments = {
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, root.filePath(QStringLiteral("mixed-output")),
        QStringLiteral("--resource-type"), QStringLiteral("images")
    };
    const QStringList categories = {
        QStringLiteral("Character"), QStringLiteral("effect"),
        QStringLiteral("object"), QStringLiteral("UI"),
        QStringLiteral("goods"), QStringLiteral("magic"),
        QStringLiteral("portrait"), QStringLiteral("interlude"),
        QStringLiteral("map"), QStringLiteral("unknown"),
        QStringLiteral("character")
    };
    const QStringList modes = {
        QStringLiteral("Preserve"), QStringLiteral("preserve"),
        QStringLiteral("preserve"), QStringLiteral("convert"),
        QStringLiteral("convert"), QStringLiteral("preserve"),
        QStringLiteral("convert"), QStringLiteral("preserve"),
        QStringLiteral("preserve"), QStringLiteral("preserve"),
        QStringLiteral("preserve")
    };
    for (const QString& category : categories)
        mixedArguments << QStringLiteral("--image-category") << category;
    for (const QString& mode : modes)
        mixedArguments << QStringLiteral("--image-mode") << mode;
    mixedArguments << QStringLiteral("--crop-transparent")
                   << QStringLiteral("TRUE")
                   << QStringLiteral("--crop-transparent")
                   << QStringLiteral("1");

    QString capturedOut;
    QString capturedErr;
    const int mixedExitCode = runCliCapture(
        mixedArguments, capturedOut, capturedErr);
    const QString mixedOutput = root.filePath(QStringLiteral("mixed-output"));
    const QJsonObject mixedReport = QJsonDocument::fromJson(
        readRawFile(QDir(mixedOutput).filePath(
            QStringLiteral("migration_report.json"))))
        .object();
    const QJsonObject mixedLegacyImages =
        mixedReport.value(QStringLiteral("legacyImages")).toObject();
    const QJsonObject mixedModes =
        mixedLegacyImages.value(QStringLiteral("modes")).toObject();
    const QJsonObject mixedCounts =
        mixedReport.value(QStringLiteral("counts")).toObject();
    const QString mixedTextReport = readUtf8TextFile(
        QDir(mixedOutput).filePath(QStringLiteral("migration_report.txt")));

    bool ok = true;
    ok = check((mixedExitCode == 0 || mixedExitCode == 1) &&
                   capturedErr.isEmpty(),
               "image-policy CLI accepts grouped case-insensitive category/mode pairs") && ok;
    const QMap<QString, QString> expectedModes = {
        {QStringLiteral("character"), QStringLiteral("preserve")},
        {QStringLiteral("effect"), QStringLiteral("preserve")},
        {QStringLiteral("object"), QStringLiteral("preserve")},
        {QStringLiteral("ui"), QStringLiteral("convert")},
        {QStringLiteral("goods"), QStringLiteral("convert")},
        {QStringLiteral("magic"), QStringLiteral("preserve")},
        {QStringLiteral("portrait"), QStringLiteral("convert")},
        {QStringLiteral("interlude"), QStringLiteral("preserve")},
        {QStringLiteral("map"), QStringLiteral("preserve")},
        {QStringLiteral("unknown"), QStringLiteral("preserve")}
    };
    for (auto mode = expectedModes.cbegin(); mode != expectedModes.cend(); ++mode)
    {
        ok = check(mixedModes.value(mode.key()).toString() == mode.value(),
            QStringLiteral("image-policy JSON records %1=%2")
                .arg(mode.key(), mode.value())) && ok;
    }
    ok = check(mixedLegacyImages.value(
                   QStringLiteral("cropTransparent")).toBool() &&
                   !mixedLegacyImages.value(
                       QStringLiteral("effectiveCropTransparent")).toBool(),
               "image-policy report separates requested crop from effective crop") && ok;
    ok = check(mixedTextReport.contains(
                   QStringLiteral("Crop transparent: requested=true, effective=false")) &&
                   capturedOut.contains(
                       QStringLiteral("Crop transparent: requested=true, effective=false")) &&
                   capturedOut.contains(QStringLiteral("ui=convert")),
               "text report and CLI summary expose the effective image policy") && ok;

    IMPImageFile mixedUiImage;
    const QString mixedUiPath = QDir(mixedOutput).filePath(
        QStringLiteral("asf/ui/panel.asf"));
    ok = check(fileSha256(characterPath) == fileSha256(
                   QDir(mixedOutput).filePath(
                       QStringLiteral("mpc/character/hero.mpc"))) &&
                   fileSha256(effectPath) == fileSha256(
                       QDir(mixedOutput).filePath(
                           QStringLiteral("mpc/effect/spark.mpc"))) &&
                   fileSha256(objectPath) == fileSha256(
                       QDir(mixedOutput).filePath(
                           QStringLiteral("mpc/object/chest.mpc"))) &&
                   fileSha256(mapPath) == fileSha256(
                       QDir(mixedOutput).filePath(
                           QStringLiteral("mpc/map/tile.mpc"))),
               "mixed image policy preserves requested main categories and locked map bytes") && ok;
    ok = check(mixedUiImage.load(mixedUiPath.toUtf8().toStdString()) &&
                   mixedUiImage.getFrameImage(0).size() == QSize(5, 5) &&
                   fileSha256(unknownPath) == fileSha256(
                       QDir(mixedOutput).filePath(
                           QStringLiteral("asf/unknown/misc.asf"))) &&
                   mixedCounts.value(QStringLiteral("convertedAndCropped")).toInt() == 0 &&
                   mixedCounts.value(QStringLiteral("convertedWithoutCrop")).toInt() == 1 &&
                   mixedCounts.value(QStringLiteral("preservedImages")).toInt() == 4 &&
                   mixedCounts.value(QStringLiteral("preservedMapImages")).toInt() == 1 &&
                   mixedCounts.value(QStringLiteral("skippedUnknownImages")).toInt() == 0,
                "mixed image policy converts UI without crop and keeps map/unknown locks") && ok;

    const QString broadOutput = root.filePath(QStringLiteral("broad-output"));
    const int broadExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, broadOutput,
        QStringLiteral("--resource-type"), QStringLiteral("images"),
        QStringLiteral("--convert-effect-images"),
        QStringLiteral("--convert-images"),
        QStringLiteral("--crop-transparent"), QStringLiteral("0")},
        capturedOut, capturedErr);
    const QJsonObject broadLegacyImages = QJsonDocument::fromJson(
        readRawFile(QDir(broadOutput).filePath(
            QStringLiteral("migration_report.json"))))
        .object().value(QStringLiteral("legacyImages")).toObject();
    const QJsonObject broadModes =
        broadLegacyImages.value(QStringLiteral("modes")).toObject();
    IMPImageFile broadCharacter;
    IMPImageFile broadUi;
    ok = check((broadExitCode == 0 || broadExitCode == 1) &&
                   broadModes.value(QStringLiteral("ui")).toString() ==
                       QStringLiteral("convert") &&
                   !broadLegacyImages.value(
                       QStringLiteral("cropTransparent")).toBool() &&
                   !broadLegacyImages.value(
                       QStringLiteral("effectiveCropTransparent")).toBool(),
               "legacy broad flag still wins over narrow flags with explicit crop off") && ok;
    ok = check(broadCharacter.load(QDir(broadOutput).filePath(
                       QStringLiteral("mpc/character/hero.mpc"))
                       .toUtf8().toStdString()) &&
                   broadUi.load(QDir(broadOutput).filePath(
                       QStringLiteral("asf/ui/panel.asf"))
                       .toUtf8().toStdString()) &&
                   broadCharacter.getFrameImage(0).size() == QSize(5, 5) &&
                   broadUi.getFrameImage(0).size() == QSize(5, 5),
               "new crop flag composes with legacy conversion selection without cropping") && ok;

    const QString narrowOutput = root.filePath(QStringLiteral("narrow-output"));
    const int narrowExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, narrowOutput,
        QStringLiteral("--resource-type"), QStringLiteral("images"),
        QStringLiteral("--convert-effect-images"),
        QStringLiteral("--convert-character-images"),
        QStringLiteral("--no-crop-transparent")},
        capturedOut, capturedErr);
    IMPImageFile narrowCharacter;
    IMPImageFile narrowEffect;
    ok = check((narrowExitCode == 0 || narrowExitCode == 1) &&
                   narrowCharacter.load(QDir(narrowOutput).filePath(
                       QStringLiteral("mpc/character/hero.mpc"))
                       .toUtf8().toStdString()) &&
                   narrowEffect.load(QDir(narrowOutput).filePath(
                       QStringLiteral("mpc/effect/spark.mpc"))
                       .toUtf8().toStdString()) &&
                   narrowCharacter.getFrameImage(0).size() == QSize(5, 5) &&
                   narrowEffect.getFrameImage(0).size() == QSize(5, 5) &&
                   fileSha256(objectPath) == fileSha256(
                       QDir(narrowOutput).filePath(
                           QStringLiteral("mpc/object/chest.mpc"))) &&
                   fileSha256(uiPath) == fileSha256(
                       QDir(narrowOutput).filePath(
                           QStringLiteral("asf/ui/panel.asf"))),
               "legacy narrow flags still accumulate and preserve unselected categories") && ok;

    const QString newWithOldCropOutput =
        root.filePath(QStringLiteral("new-with-old-crop-output"));
    const int newWithOldCropExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
        sourceRoot, newWithOldCropOutput,
        QStringLiteral("--resource-type"), QStringLiteral("images"),
        QStringLiteral("--image-category"), QStringLiteral("character"),
        QStringLiteral("--image-mode"), QStringLiteral("convert"),
        QStringLiteral("--no-crop-transparent")},
        capturedOut, capturedErr);
    const QJsonObject newWithOldCropPolicy = QJsonDocument::fromJson(
        readRawFile(QDir(newWithOldCropOutput).filePath(
            QStringLiteral("migration_report.json"))))
        .object().value(QStringLiteral("legacyImages")).toObject();
    ok = check((newWithOldCropExitCode == 0 || newWithOldCropExitCode == 1) &&
                   !newWithOldCropPolicy.value(
                       QStringLiteral("cropTransparent")).toBool(),
               "new category overrides remain compatible with legacy crop-disable flag") && ok;

    struct InvalidCase
    {
        QString label;
        QStringList options;
        QString expectedError;
    };
    const QList<InvalidCase> invalidCases = {
        {QStringLiteral("missing mode"),
            {QStringLiteral("--image-category"), QStringLiteral("ui")},
            QStringLiteral("same number of times")},
        {QStringLiteral("missing category"),
            {QStringLiteral("--image-mode"), QStringLiteral("convert")},
            QStringLiteral("same number of times")},
        {QStringLiteral("invalid category"),
            {QStringLiteral("--image-category"), QStringLiteral("other"),
             QStringLiteral("--image-mode"), QStringLiteral("preserve")},
            QStringLiteral("--image-category requires one of")},
        {QStringLiteral("invalid mode"),
            {QStringLiteral("--image-category"), QStringLiteral("ui"),
             QStringLiteral("--image-mode"), QStringLiteral("copy")},
            QStringLiteral("--image-mode requires")},
        {QStringLiteral("map convert"),
            {QStringLiteral("--image-category"), QStringLiteral("map"),
             QStringLiteral("--image-mode"), QStringLiteral("convert")},
            QStringLiteral("is not allowed for --image-category map")},
        {QStringLiteral("unknown exclude"),
            {QStringLiteral("--image-category"), QStringLiteral("unknown"),
             QStringLiteral("--image-mode"), QStringLiteral("exclude")},
            QStringLiteral("is not allowed for --image-category unknown")},
        {QStringLiteral("convertible exclude"),
            {QStringLiteral("--image-category"), QStringLiteral("character"),
             QStringLiteral("--image-mode"), QStringLiteral("exclude")},
            QStringLiteral("is not allowed for --image-category character")},
        {QStringLiteral("duplicate conflict"),
            {QStringLiteral("--image-category"), QStringLiteral("ui"),
             QStringLiteral("--image-mode"), QStringLiteral("preserve"),
             QStringLiteral("--image-category"), QStringLiteral("ui"),
             QStringLiteral("--image-mode"), QStringLiteral("convert")},
            QStringLiteral("conflicting --image-mode values")},
        {QStringLiteral("new legacy selection conflict"),
            {QStringLiteral("--image-category"), QStringLiteral("ui"),
             QStringLiteral("--image-mode"), QStringLiteral("convert"),
             QStringLiteral("--convert-images")},
            QStringLiteral("cannot be combined with legacy image conversion flags")},
        {QStringLiteral("crop syntax conflict"),
            {QStringLiteral("--crop-transparent"), QStringLiteral("false"),
             QStringLiteral("--no-crop-transparent")},
            QStringLiteral("cannot be combined with --no-crop-transparent")},
        {QStringLiteral("repeated crop conflict"),
            {QStringLiteral("--crop-transparent"), QStringLiteral("true"),
             QStringLiteral("--crop-transparent"), QStringLiteral("0")},
            QStringLiteral("conflicting repeated --crop-transparent values")},
        {QStringLiteral("non-image domain"),
            {QStringLiteral("--resource-type"), QStringLiteral("scripts"),
             QStringLiteral("--image-category"), QStringLiteral("ui"),
             QStringLiteral("--image-mode"), QStringLiteral("convert")},
            QStringLiteral("require --resource-type all or images")}
    };
    for (qsizetype index = 0; index < invalidCases.size(); index++)
    {
        const InvalidCase& invalidCase = invalidCases[index];
        const QString invalidOutput = root.filePath(
            QStringLiteral("invalid-output-%1").arg(index));
        QStringList arguments = {
            QStringLiteral("jxqy-editor-cli"), QStringLiteral("migrate-assets"),
            sourceRoot, invalidOutput
        };
        arguments.append(invalidCase.options);
        const int exitCode = runCliCapture(arguments, capturedOut, capturedErr);
        ok = check(exitCode == 2 &&
                       capturedErr.contains(invalidCase.expectedError) &&
                       !QFileInfo::exists(invalidOutput),
            QStringLiteral("image-policy CLI rejects %1 before output")
                .arg(invalidCase.label)) && ok;
    }

    const int helpExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"), QStringLiteral("--help")},
        capturedOut, capturedErr);
    ok = check(helpExitCode == 0 &&
                   capturedOut.contains(QStringLiteral("--image-category <id>")) &&
                   capturedOut.contains(QStringLiteral("--image-mode <mode>")) &&
                   capturedOut.contains(QStringLiteral("--crop-transparent <b>")) &&
                   capturedOut.contains(QStringLiteral("map, unknown")),
               "CLI help documents image category, mode, crop, and locked IDs") && ok;
    return ok;
}

bool testCropTransparentEdgesOffsetCases()
{
    // 6x6 frame with a 2x2 opaque block at (2,2); cropLeft = cropTop = 2 -> 2x2.
    QImage frame = makePaddedFrame(6, 6, 2, 2, 2, 2, qRgba(255, 0, 0, 255));

    bool ok = true;

    // Positive offset stays positive after the formula.
    {
        const int ox = 5;
        const int oy = 7;
        auto before = opaqueWorldCoords(frame, ox, oy);
        TransparentCropResult r = PicFileEditor::cropTransparentEdges(frame, ox, oy);
        ok = check(r.image.size() == QSize(2, 2), "positive offset: cropped size 2x2") && ok;
        ok = check(r.xOffset == 3 && r.yOffset == 5, "positive offset: strict offset formula") && ok;
        ok = check(!r.wasFullyTransparent, "positive offset: not fully transparent") && ok;
        ok = check(opaqueWorldCoords(r.image, r.xOffset, r.yOffset) == before,
               "positive offset: world coordinates invariant") && ok;
    }
    // Zero offset becomes negative; must not be padded or zeroed.
    {
        const int ox = 0;
        const int oy = 0;
        auto before = opaqueWorldCoords(frame, ox, oy);
        TransparentCropResult r = PicFileEditor::cropTransparentEdges(frame, ox, oy);
        ok = check(r.image.size() == QSize(2, 2), "zero offset: cropped size 2x2") && ok;
        ok = check(r.xOffset == -2 && r.yOffset == -2,
               "zero offset: negative offset allowed, no padding/zeroing") && ok;
        ok = check(opaqueWorldCoords(r.image, r.xOffset, r.yOffset) == before,
               "zero offset: world coordinates invariant") && ok;
    }
    // Pre-existing negative offset stays negative (more negative), no clamping.
    {
        const int ox = -3;
        const int oy = -4;
        auto before = opaqueWorldCoords(frame, ox, oy);
        TransparentCropResult r = PicFileEditor::cropTransparentEdges(frame, ox, oy);
        ok = check(r.image.size() == QSize(2, 2), "negative offset: cropped size 2x2") && ok;
        ok = check(r.xOffset == -5 && r.yOffset == -6,
               "negative offset: formula preserves negativity, no clamp") && ok;
        ok = check(opaqueWorldCoords(r.image, r.xOffset, r.yOffset) == before,
               "negative offset: world coordinates invariant") && ok;
    }
    return ok;
}

bool testCropTransparentEdgesFullyTransparentAndNoOp()
{
    bool ok = true;

    // Fully transparent 4x4 -> 1x1 transparent, offset preserved.
    {
        QImage transparent(4, 4, QImage::Format_ARGB32);
        transparent.fill(Qt::transparent);
        TransparentCropResult r = PicFileEditor::cropTransparentEdges(transparent, 9, -3);
        ok = check(r.wasFullyTransparent, "fully transparent: flagged fully transparent") && ok;
        ok = check(r.image.size() == QSize(1, 1), "fully transparent: collapses to 1x1") && ok;
        ok = check(qAlpha(r.image.pixel(0, 0)) == 0, "fully transparent: 1x1 is transparent") && ok;
        ok = check(r.xOffset == 9 && r.yOffset == -3, "fully transparent: offset preserved") && ok;
    }

    // No transparent edge: content fills the whole frame -> unchanged.
    {
        QImage full(3, 3, QImage::Format_ARGB32);
        full.fill(qRgba(10, 20, 30, 255));
        auto before = opaqueWorldCoords(full, 4, 4);
        TransparentCropResult r = PicFileEditor::cropTransparentEdges(full, 4, 4);
        ok = check(!r.wasFullyTransparent, "no-op: not fully transparent") && ok;
        ok = check(r.image.size() == QSize(3, 3), "no-op: size unchanged") && ok;
        ok = check(r.xOffset == 4 && r.yOffset == 4, "no-op: offset unchanged") && ok;
        ok = check(opaqueWorldCoords(r.image, r.xOffset, r.yOffset) == before,
               "no-op: world coordinates invariant") && ok;
    }
    return ok;
}

bool testCropTransparentEdgesAllFramesRoundTrip()
{
    std::vector<QImage> frames;
    // Frame 0: 5x5 with one opaque pixel at (4,4). cropLeft=cropTop=4 -> 1x1,
    // and with MPC offset (width/2, height-yMove) = (2, 4) the new offset is
    // (2-4, 4-4) = (-2, 0), exercising negative offset storage.
    frames.push_back(makePaddedFrame(5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));
    // Frame 1: 3x3 fully opaque -> no transparent edge, stays 3x3.
    QImage full3(3, 3, QImage::Format_ARGB32);
    full3.fill(qRgba(255, 0, 0, 255));
    frames.push_back(full3);

    const int yMove = 1;
    std::vector<uint8_t> mpc = buildMpcFileFromImages(frames, 2, 30, yMove);

    PicFileEditor editor;
    if (!check(editor.loadFromBuffer(mpc.data(), static_cast<int>(mpc.size())),
            "round-trip: load 2-frame MPC fixture"))
    {
        return false;
    }

    int32_t ref0Ox = 0;
    int32_t ref0Oy = 0;
    int32_t ref1Ox = 0;
    int32_t ref1Oy = 0;
    editor.getFrameOffset(0, &ref0Ox, &ref0Oy);  // (2, 4)
    editor.getFrameOffset(1, &ref1Ox, &ref1Oy);  // (1, 2)
    auto world0 = opaqueWorldCoords(editor.getFrameImage(0), ref0Ox, ref0Oy);
    auto world1 = opaqueWorldCoords(editor.getFrameImage(1), ref1Ox, ref1Oy);

    editor.cropTransparentEdgesAllFrames();

    const std::string outName = "core-regression-crop-roundtrip.img";
    bool saved = editor.saveAsIMP(outName);
    IMPImageFile imp;
    bool reloaded = saved && imp.load(outName);
    std::remove(outName.c_str());
    if (!check(saved && reloaded, "round-trip: saveAsIMP after crop and reload IMG"))
        return false;

    bool ok = check(imp.getImageCount() == 2, "round-trip: 2 frames preserved");

    QImage c0 = imp.getFrameImage(0);
    int c0Ox = 0;
    int c0Oy = 0;
    imp.getFrameOffset(0, &c0Ox, &c0Oy);
    ok = check(c0.size() == QSize(1, 1), "round-trip: frame0 cropped to 1x1") && ok;
    ok = check(c0Ox == -2 && c0Oy == 0, "round-trip: frame0 negative offset stored") && ok;
    ok = check(opaqueWorldCoords(c0, c0Ox, c0Oy) == world0,
           "round-trip: frame0 world coordinates invariant") && ok;

    QImage c1 = imp.getFrameImage(1);
    int c1Ox = 0;
    int c1Oy = 0;
    imp.getFrameOffset(1, &c1Ox, &c1Oy);
    ok = check(c1.size() == QSize(3, 3), "round-trip: frame1 no-op size unchanged") && ok;
    ok = check(c1Ox == 1 && c1Oy == 2, "round-trip: frame1 offset unchanged") && ok;
    ok = check(opaqueWorldCoords(c1, c1Ox, c1Oy) == world1,
           "round-trip: frame1 world coordinates invariant") && ok;

    return ok;
}

QByteArray fileSha256(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return QCryptographicHash::hash(file.readAll(),
        QCryptographicHash::Sha256);
}

bool testLegacyImagePolicyModelAndClassification()
{
    LegacyImageMigrationPolicy policy;
    bool ok = check(LegacyImageMigrationPolicy::definitions().size() == 10,
        "legacy image policy exposes ten core categories");
    QStringList categoryIds;
    bool displayKeysComplete = true;
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        categoryIds.append(item.id);
        displayKeysComplete = displayKeysComplete &&
            !item.displayNameZhCn.isEmpty() && !item.displayNameEn.isEmpty();
    }
    ok = check(categoryIds == QStringList({
                   QStringLiteral("character"), QStringLiteral("effect"),
                   QStringLiteral("object"), QStringLiteral("ui"),
                   QStringLiteral("goods"), QStringLiteral("magic"),
                   QStringLiteral("portrait"), QStringLiteral("interlude"),
                   QStringLiteral("map"), QStringLiteral("unknown")}) &&
                   displayKeysComplete,
        "legacy image policy owns stable category ids and bilingual display keys") && ok;
    ok = check(LegacyImageMigrationPolicy::categoryFromId(
                   QStringLiteral(" Character ")) ==
                   LegacyImageCategory::Character &&
               LegacyImageMigrationPolicy::categoryFromId(
                   QStringLiteral("UI")) == LegacyImageCategory::Ui &&
               LegacyImageMigrationPolicy::categoryFromId(
                   QStringLiteral("unknown")) == LegacyImageCategory::Unknown &&
               !LegacyImageMigrationPolicy::categoryFromId(
                   QStringLiteral("other")).has_value(),
        "legacy image policy resolves CLI category ids case-insensitively") && ok;
    ok = check(policy.mode(LegacyImageCategory::Character) ==
                   LegacyImageMode::Convert &&
               policy.mode(LegacyImageCategory::Effect) ==
                   LegacyImageMode::Convert &&
               policy.mode(LegacyImageCategory::Object) ==
                   LegacyImageMode::Convert,
        "legacy image policy defaults main categories to convert") && ok;
    ok = check(policy.mode(LegacyImageCategory::Ui) ==
                   LegacyImageMode::Preserve &&
               policy.mode(LegacyImageCategory::Goods) ==
                   LegacyImageMode::Preserve &&
               policy.mode(LegacyImageCategory::Magic) ==
                   LegacyImageMode::Preserve &&
               policy.mode(LegacyImageCategory::Portrait) ==
                   LegacyImageMode::Preserve &&
               policy.mode(LegacyImageCategory::Interlude) ==
                   LegacyImageMode::Preserve,
        "legacy image policy defaults five optional categories to preserve") && ok;
    for (LegacyImageCategory category : {
             LegacyImageCategory::Ui, LegacyImageCategory::Goods,
             LegacyImageCategory::Magic, LegacyImageCategory::Portrait,
             LegacyImageCategory::Interlude})
    {
        const LegacyImageCategoryDefinition& item =
            LegacyImageMigrationPolicy::definition(category);
        ok = check(item.allowsConversion &&
                       !item.allowsTransparentCrop && item.entersOutput,
            "legacy image policy allows optional conversion without crop") && ok;
    }
    ok = check(!LegacyImageMigrationPolicy::definition(
                       LegacyImageCategory::Map).allowsConversion &&
                   LegacyImageMigrationPolicy::definition(
                       LegacyImageCategory::Map).entersOutput &&
                   !LegacyImageMigrationPolicy::definition(
                       LegacyImageCategory::Unknown).allowsConversion &&
                   LegacyImageMigrationPolicy::definition(
                       LegacyImageCategory::Unknown).entersOutput,
        "legacy image policy metadata locks map and unknown categories to preserved output") && ok;
    ok = check(policy.mode(LegacyImageCategory::Map) ==
                   LegacyImageMode::Preserve &&
               policy.mode(LegacyImageCategory::Unknown) ==
                   LegacyImageMode::Preserve &&
               policy.cropTransparent() &&
               policy.effectiveCropTransparent(),
        "legacy image policy preserves map/unknown and defaults crop on") && ok;

    ok = check(!policy.setMode(
                   LegacyImageCategory::Map, LegacyImageMode::Convert) &&
               policy.setMode(
                   LegacyImageCategory::Unknown, LegacyImageMode::Preserve) &&
               !policy.setMode(
                   LegacyImageCategory::Ui, LegacyImageMode::Exclude) &&
               !policy.setMode(
                   LegacyImageCategory::Count, LegacyImageMode::Preserve) &&
               policy.mode(LegacyImageCategory::Map) ==
                   LegacyImageMode::Preserve &&
               policy.mode(LegacyImageCategory::Unknown) ==
                   LegacyImageMode::Preserve,
        "legacy image policy rejects illegal conversion/exclusion while accepting unknown preservation") && ok;

    policy.setMode(LegacyImageCategory::Character, LegacyImageMode::Preserve);
    policy.setMode(LegacyImageCategory::Effect, LegacyImageMode::Preserve);
    policy.setMode(LegacyImageCategory::Object, LegacyImageMode::Preserve);
    policy.setMode(LegacyImageCategory::Ui, LegacyImageMode::Convert);
    ok = check(!policy.hasTransparentCropEligibleConversion() &&
                   !policy.effectiveCropTransparent() &&
                   !policy.shouldCrop(LegacyImageCategory::Ui),
        "legacy image policy cannot crop UI when only UI converts") && ok;

    const auto categoryOf = [](const QString& path)
    {
        return LegacyImageMigrationPolicy::classifyRelativePath(path);
    };
    ok = check(categoryOf(QStringLiteral("MPC/Character/Hero.MPC")) ==
                   LegacyImageCategory::Character &&
               categoryOf(QStringLiteral("asF/interlude/scene.asf")) ==
                   LegacyImageCategory::Interlude &&
               categoryOf(QStringLiteral("mpc/map/tile.mpc")) ==
                   LegacyImageCategory::Map,
        "legacy image policy classifies MPC/ASF first directories case-insensitively") && ok;
    ok = check(categoryOf(QString::fromUtf8("mpc/未找到/a.mpc")) ==
                   LegacyImageCategory::Unknown &&
               categoryOf(QString::fromUtf8("asf/未找到的/b.asf")) ==
                   LegacyImageCategory::Unknown &&
               categoryOf(QStringLiteral("mpc/other/c.pic")) ==
                   LegacyImageCategory::Unknown &&
               categoryOf(QStringLiteral("mpc/root-file.mpc")) ==
                   LegacyImageCategory::Unknown,
        "legacy image policy classifies missing and unregistered directories as unknown") && ok;
    ok = check(!categoryOf(QStringLiteral("map/city.map")).has_value() &&
                   !categoryOf(QStringLiteral("img/photo.jpg")).has_value(),
        "legacy image policy leaves map data and img tree outside MPC/ASF categories") && ok;
    return ok;
}

bool testMigrationUsesDefaultLegacyImagePolicy()
{
    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "default image policy: temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourceDir = root.filePath("source");
    QString outputDir = root.filePath("output");
    if (!check(root.mkpath("source/asf/character") &&
            root.mkpath("source/asf/ui") &&
            root.mkpath("source/asf/未找到") &&
            root.mkpath("source/mpc/map") &&
            root.mkpath("source/img") && root.mkpath("output"),
            "default image policy: fixture dirs"))
    {
        return false;
    }

    std::vector<QImage> frames;
    frames.push_back(makePaddedFrame(5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));
    const QString characterPath = sourceDir + "/asf/character/sample.mpc";
    const QString uiPath = sourceDir + "/asf/ui/panel.asf";
    const QString mapImagePath = sourceDir + "/mpc/map/tile.mpc";
    const QString unknownPath = sourceDir + "/asf/未找到/misc.asf";
    const QString generalImagePath = sourceDir + "/img/photo.jpg";
    const std::vector<uint8_t> asf =
        buildAsfFileFromImages(frames, 1, 30, 2, 3);
    if (!check(writeMpcFileFromImages(characterPath, frames, 1) &&
            writeMpcFileFromImages(mapImagePath, frames, 1) &&
            Util::writeFileFromBuffer(uiPath.toUtf8().toStdString(),
                asf.data(), asf.size()) &&
            Util::writeFileFromBuffer(unknownPath.toUtf8().toStdString(),
                asf.data(), asf.size()) &&
            writeRawFile(generalImagePath, "unchanged-jpg-bytes"),
            "default image policy: write source images"))
    {
        return false;
    }

    QString capturedOut;
    QString capturedErr;
    QStringList arguments = {"jxqy-editor-cli", "migrate-assets",
        sourceDir, outputDir,
        "--minimum-magic-damage", "7",
        "--no-mod-profile"};
    int exitCode = runCliCapture(arguments, capturedOut, capturedErr);

    bool ok = check(exitCode == 0 || exitCode == 1,
        "default image policy: migrate-assets succeeds (exit 0 or 1)");
    ok = check(!capturedErr.contains("Unknown option"),
        "default image policy: no usage error") && ok;

    IMPImageFile characterImage;
    const QString outputCharacter = outputDir + "/asf/character/sample.mpc";
    ok = check(characterImage.load(outputCharacter.toUtf8().toStdString()) &&
            characterImage.getFrameImage(0).size() == QSize(1, 1),
        "default image policy: character converts and crops") && ok;
    ok = check(fileSha256(uiPath) ==
                   fileSha256(outputDir + "/asf/ui/panel.asf") &&
               fileSha256(mapImagePath) ==
                   fileSha256(outputDir + "/mpc/map/tile.mpc") &&
               fileSha256(generalImagePath) ==
                   fileSha256(outputDir + "/img/photo.jpg"),
        "default image policy: UI, map images, and img tree preserve SHA-256") && ok;
    ok = check(fileSha256(unknownPath) ==
                   fileSha256(outputDir + "/asf/未找到/misc.asf"),
        "default image policy: unknown category preserves exact bytes") && ok;

    const QJsonObject counts = QJsonDocument::fromJson(
        readRawFile(outputDir + "/migration_report.json"))
        .object().value(QStringLiteral("counts")).toObject();
    const QString textReport =
        readUtf8TextFile(outputDir + "/migration_report.txt");
    ok = check(counts.value("convertedAndCropped").toInt() == 1 &&
                   counts.value("convertedWithoutCrop").toInt() == 0 &&
                   counts.value("preservedImages").toInt() == 3 &&
                   counts.value("preservedMapImages").toInt() == 1 &&
                   counts.value("skippedUnknownImages").toInt() == 0 &&
                   counts.value("failedImages").toInt() == 0,
               "default image policy: JSON report separates image outcomes") && ok;
    ok = check(textReport.contains("convertedAndCropped: 1") &&
                   textReport.contains("convertedWithoutCrop: 0") &&
                   textReport.contains("preservedImages: 3") &&
                   textReport.contains("preservedMapImages: 1") &&
                   textReport.contains("skippedUnknownImages: 0") &&
                   textReport.contains("failedImages: 0"),
               "default image policy: text and JSON reports share counts") && ok;
    return ok;
}

bool testMigrationConvertImagesCropsTransparentEdgesByDefault()
{
    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "default-crop migration: temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourceDir = root.filePath("source");
    QString outputDir = root.filePath("output");
    if (!check(root.mkpath("source/asf/character") && root.mkpath("output"),
            "default-crop migration: fixture dirs"))
    {
        return false;
    }

    std::vector<QImage> frames;
    frames.push_back(makePaddedFrame(5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));
    const int yMove = 1;
    if (!check(writeMpcFileFromImages(sourceDir + "/asf/character/sample.mpc", frames, yMove),
            "default-crop migration: write source MPC"))
    {
        return false;
    }

    // Reference world coordinate from the decoded source frame and its offset.
    std::vector<uint8_t> refBuffer = Util::readFileToBuffer(
        (sourceDir + "/asf/character/sample.mpc").toUtf8().toStdString());
    PicFileEditor refEditor;
    refEditor.loadFromBuffer(refBuffer.data(), static_cast<int>(refBuffer.size()));
    int32_t refOx = 0;
    int32_t refOy = 0;
    refEditor.getFrameOffset(0, &refOx, &refOy);
    auto refWorld = opaqueWorldCoords(refEditor.getFrameImage(0), refOx, refOy);

    QString capturedOut;
    QString capturedErr;
    QStringList arguments = {"jxqy-editor-cli", "migrate-assets", sourceDir, outputDir, "--convert-images"};
    int exitCode = runCliCapture(arguments, capturedOut, capturedErr);

    bool ok = check(exitCode == 0 || exitCode == 1,
        "default-crop migration: migrate-assets succeeds (exit 0 or 1)");

    IMPImageFile imp;
    QString outputPath = outputDir + "/asf/character/sample.mpc";
    ok = check(imp.load(outputPath.toUtf8().toStdString()),
        "default-crop migration: output IMG loads") && ok;
    QImage c0 = imp.getFrameImage(0);
    int c0Ox = 0;
    int c0Oy = 0;
    imp.getFrameOffset(0, &c0Ox, &c0Oy);
    ok = check(c0.size() == QSize(1, 1), "default-crop migration: frame cropped to 1x1") && ok;
    ok = check(c0Ox == -2 && c0Oy == 0, "default-crop migration: negative offset applied") && ok;
    ok = check(opaqueWorldCoords(c0, c0Ox, c0Oy) == refWorld,
        "default-crop migration: world coordinates invariant vs source") && ok;
    return ok;
}

bool testMigrationNoCropTransparentPreservesSizeAndOffset()
{
    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "no-crop migration: temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourceDir = root.filePath("source");
    QString outputDir = root.filePath("output");
    if (!check(root.mkpath("source/asf/character") && root.mkpath("output"),
            "no-crop migration: fixture dirs"))
    {
        return false;
    }

    std::vector<QImage> frames;
    frames.push_back(makePaddedFrame(5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));
    const int yMove = 1;
    if (!check(writeMpcFileFromImages(sourceDir + "/asf/character/sample.mpc", frames, yMove),
            "no-crop migration: write source MPC"))
    {
        return false;
    }

    QString capturedOut;
    QString capturedErr;
    QStringList arguments = {"jxqy-editor-cli", "migrate-assets", sourceDir, outputDir, "--convert-images", "--no-crop-transparent"};
    int exitCode = runCliCapture(arguments, capturedOut, capturedErr);

    bool ok = check(exitCode == 0 || exitCode == 1,
        "no-crop migration: --no-crop-transparent accepted (exit 0 or 1)");
    ok = check(!capturedErr.contains("Unknown option"),
        "no-crop migration: --no-crop-transparent not reported as unknown") && ok;

    IMPImageFile imp;
    QString outputPath = outputDir + "/asf/character/sample.mpc";
    ok = check(imp.load(outputPath.toUtf8().toStdString()),
        "no-crop migration: output IMG loads") && ok;
    QImage c0 = imp.getFrameImage(0);
    int c0Ox = 0;
    int c0Oy = 0;
    imp.getFrameOffset(0, &c0Ox, &c0Oy);
    // Original 5x5 frame, opaque pixel still at (4,4), offset (width/2, height-yMove) = (2, 4).
    ok = check(c0.size() == QSize(5, 5), "no-crop migration: original size preserved") && ok;
    ok = check(c0Ox == 2 && c0Oy == 4, "no-crop migration: original offset preserved") && ok;
    ok = check(qAlpha(c0.pixel(4, 4)) != 0, "no-crop migration: opaque pixel not moved") && ok;
    return ok;
}

bool testMigrationConvertsOnlySelectedImageCategories()
{
    QTemporaryDir rootDir;
    if (!check(rootDir.isValid(), "category image migration: temp root"))
        return false;

    QDir root(rootDir.path());
    QString sourceDir = root.filePath("source");
    QString outputDir = root.filePath("output");
    if (!check(root.mkpath("source") && root.mkpath("output"),
            "category image migration: fixture dirs"))
    {
        return false;
    }

    std::vector<QImage> frames;
    frames.push_back(makePaddedFrame(5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));

    struct Fixture
    {
        QString relativePath;
        bool asf = false;
        bool defaultConvert = false;
        bool unknown = false;
    };

    const int mpcYMove = 1;
    const int asfXMove = 2;
    const int asfYMove = 3;
    const std::vector<Fixture> fixtures = {
        {QStringLiteral("mpc/character/hero.mpc"), false, true, false},
        {QStringLiteral("mpc/effect/spark.mpc"), false, true, false},
        {QStringLiteral("mpc/object/chest.mpc"), false, true, false},
        {QStringLiteral("asf/character/hero.asf"), true, true, false},
        {QStringLiteral("asf/effect/spark.asf"), true, true, false},
        {QStringLiteral("asf/object/chest.asf"), true, true, false},
        {QStringLiteral("mpc/map/tile.mpc"), false, false, false},
        {QStringLiteral("mpc/map/not-map-data.map"), false, false, false},
        {QStringLiteral("mpc/goods/icon.mpc"), false, false, false},
        {QStringLiteral("mpc/magic/icon.mpc"), false, false, false},
        {QStringLiteral("asf/ui/panel.asf"), true, false, false},
        {QStringLiteral("asf/portrait/head.asf"), true, false, false},
        {QStringLiteral("asf/interlude/scene.asf"), true, false, false},
        {QStringLiteral("asf/unknown/misc.asf"), true, false, true},
        {QString::fromUtf8("mpc/未找到/misc.mpc"), false, false, true},
        {QString::fromUtf8("asf/未找到的/misc.asf"), true, false, true}
    };

    for (const Fixture& fixture : fixtures)
    {
        QString sourcePath = QDir(sourceDir).filePath(fixture.relativePath);
        if (!check(QDir().mkpath(QFileInfo(sourcePath).absolutePath()),
                "category image migration: create source parent"))
        {
            return false;
        }

        bool written = false;
        if (fixture.asf)
        {
            std::vector<uint8_t> asf = buildAsfFileFromImages(frames, 1, 30, asfXMove, asfYMove);
            written = Util::writeFileFromBuffer(sourcePath.toUtf8().toStdString(), asf.data(), asf.size());
        }
        else
        {
            written = writeMpcFileFromImages(sourcePath, frames, mpcYMove);
        }
        if (!check(written,
                QStringLiteral("category image migration: write fixture %1").arg(fixture.relativePath)))
        {
            return false;
        }
    }

    AssetMigrationOptions options;
    options.convertScript = false;
    options.writeModProfile = false;
    options.legacyImages.setMode(
        LegacyImageCategory::Character, LegacyImageMode::Convert);
    options.legacyImages.setMode(
        LegacyImageCategory::Effect, LegacyImageMode::Convert);
    options.legacyImages.setMode(
        LegacyImageCategory::Object, LegacyImageMode::Convert);
    options.legacyImages.setCropTransparent(true);

    JxAssetMigrator migrator;
    AssetMigrationReport report;
    MigrationResult result = migrator.migrate(sourceDir, outputDir, options, report);
    bool ok = check(result == MigrationResult::Success && report.errorCount == 0,
        "category image migration: migrate succeeds");

    for (const Fixture& fixture : fixtures)
    {
        const QString sourcePath = QDir(sourceDir).filePath(fixture.relativePath);
        QString outputPath = QDir(outputDir).filePath(fixture.relativePath);
        const std::vector<uint8_t> sourceBytes = Util::readFileToBuffer(
            sourcePath.toUtf8().toStdString());
        const std::vector<uint8_t> outputBytes = Util::readFileToBuffer(
            outputPath.toUtf8().toStdString());
        if (fixture.unknown)
        {
            ok = check(!sourceBytes.empty() &&
                    fileSha256(sourcePath) == fileSha256(outputPath),
                QStringLiteral("category image migration: preserves unknown SHA-256 %1")
                    .arg(fixture.relativePath)) && ok;
            continue;
        }
        if (!fixture.defaultConvert)
        {
            ok = check(!sourceBytes.empty() &&
                    fileSha256(sourcePath) == fileSha256(outputPath),
                QStringLiteral("category image migration: preserves category SHA-256 %1")
                    .arg(fixture.relativePath)) && ok;
            continue;
        }

        IMPImageFile imp;
        ok = check(imp.load(outputPath.toUtf8().toStdString()),
            QStringLiteral("category image migration: selected output loads %1")
                .arg(fixture.relativePath)) && ok;
        ok = check(outputBytes != sourceBytes && imp.getFrameImage(0).size() == QSize(1, 1),
            QStringLiteral("category image migration: selected category converts and crops %1")
            .arg(fixture.relativePath)) && ok;
    }

    ok = check(report.convertedAndCropped == 6 &&
                   report.convertedWithoutCrop == 0 &&
                   report.preservedImages == 8 &&
                   report.preservedMapImages == 2 &&
                   report.skippedUnknownImages == 0 &&
                   report.failedImages == 0,
                "category image migration: default report counts each policy outcome") && ok;

    const QString uiOnlyOutput = root.filePath("ui-only-output");
    AssetMigrationOptions uiOnlyOptions;
    uiOnlyOptions.convertScript = false;
    uiOnlyOptions.writeModProfile = false;
    uiOnlyOptions.legacyImages.setAllConvertible(
        LegacyImageMode::Preserve);
    uiOnlyOptions.legacyImages.setMode(
        LegacyImageCategory::Ui, LegacyImageMode::Convert);
    uiOnlyOptions.legacyImages.setCropTransparent(true);
    AssetMigrationReport uiOnlyReport;
    const MigrationResult uiOnlyResult = migrator.migrate(
        sourceDir, uiOnlyOutput, uiOnlyOptions, uiOnlyReport);
    IMPImageFile uiOnlyImage;
    const QString uiRelativePath = QStringLiteral("asf/ui/panel.asf");
    ok = check(uiOnlyResult == MigrationResult::Success &&
                   !uiOnlyOptions.legacyImages.effectiveCropTransparent() &&
                   fileSha256(QDir(sourceDir).filePath(
                       QStringLiteral("mpc/character/hero.mpc"))) ==
                       fileSha256(QDir(uiOnlyOutput).filePath(
                           QStringLiteral("mpc/character/hero.mpc"))) &&
                   uiOnlyImage.load(QDir(uiOnlyOutput).filePath(uiRelativePath)
                       .toUtf8().toStdString()),
               "category image migration: UI-only converts while character preserves") && ok;
    int uiOffsetX = 0;
    int uiOffsetY = 0;
    uiOnlyImage.getFrameOffset(0, &uiOffsetX, &uiOffsetY);
    ok = check(uiOnlyImage.getFrameImage(0).size() == QSize(5, 5) &&
                   uiOffsetX == asfXMove && uiOffsetY == asfYMove + 16 &&
                   uiOnlyReport.convertedAndCropped == 0 &&
                   uiOnlyReport.convertedWithoutCrop == 1,
               "category image migration: UI conversion cannot crop dimensions or offsets") && ok;

    const QString characterAndUiOutput =
        root.filePath("character-and-ui-output");
    AssetMigrationOptions characterAndUiOptions;
    characterAndUiOptions.convertScript = false;
    characterAndUiOptions.writeModProfile = false;
    characterAndUiOptions.legacyImages.setAllConvertible(
        LegacyImageMode::Preserve);
    characterAndUiOptions.legacyImages.setMode(
        LegacyImageCategory::Character, LegacyImageMode::Convert);
    characterAndUiOptions.legacyImages.setMode(
        LegacyImageCategory::Ui, LegacyImageMode::Convert);
    AssetMigrationReport characterAndUiReport;
    const MigrationResult characterAndUiResult = migrator.migrate(
        sourceDir, characterAndUiOutput, characterAndUiOptions,
        characterAndUiReport);
    IMPImageFile convertedCharacter;
    IMPImageFile convertedUi;
    ok = check(characterAndUiResult == MigrationResult::Success &&
                   convertedCharacter.load(QDir(characterAndUiOutput).filePath(
                       QStringLiteral("mpc/character/hero.mpc"))
                       .toUtf8().toStdString()) &&
                   convertedUi.load(QDir(characterAndUiOutput).filePath(
                       uiRelativePath).toUtf8().toStdString()) &&
                   convertedCharacter.getFrameImage(0).size() == QSize(1, 1) &&
                   convertedUi.getFrameImage(0).size() == QSize(5, 5) &&
                   characterAndUiReport.convertedAndCropped == 2 &&
                   characterAndUiReport.convertedWithoutCrop == 1,
               "category image migration: character crops while UI never crops") && ok;

    const QString effectOnlyOutput = root.filePath("effect-only-output");
    QString effectOnlyOut;
    QString effectOnlyErr;
    const QStringList effectOnlyArguments = {
        QStringLiteral("jxqy-editor-cli"),
        QStringLiteral("migrate-assets"),
        sourceDir,
        effectOnlyOutput,
        QStringLiteral("--no-mod-profile"),
        QStringLiteral("--convert-effect-images")
    };
    const int effectOnlyExitCode = runCliCapture(
        effectOnlyArguments, effectOnlyOut, effectOnlyErr);
    ok = check((effectOnlyExitCode == 0 || effectOnlyExitCode == 1) &&
                   !effectOnlyErr.contains(QStringLiteral("Unknown option")),
               "category image migration: CLI effect-only migration succeeds") && ok;

    const QString characterPath = QStringLiteral("mpc/character/hero.mpc");
    const QString effectPath = QStringLiteral("mpc/effect/spark.mpc");
    const QString objectPath = QStringLiteral("mpc/object/chest.mpc");
    const auto bytesAt = [](const QString& rootPath, const QString& relativePath)
    {
        return Util::readFileToBuffer(
            QDir(rootPath).filePath(relativePath).toUtf8().toStdString());
    };
    IMPImageFile effectImage;
    ok = check(bytesAt(effectOnlyOutput, characterPath) ==
                   bytesAt(sourceDir, characterPath) &&
                   bytesAt(effectOnlyOutput, objectPath) ==
                   bytesAt(sourceDir, objectPath),
               "category image migration: effect-only preserves character and object") && ok;
    ok = check(effectImage.load(
                   QDir(effectOnlyOutput).filePath(effectPath)
                       .toUtf8().toStdString()) &&
                   bytesAt(effectOnlyOutput, effectPath) !=
                       bytesAt(sourceDir, effectPath),
               "category image migration: effect-only converts effect") && ok;

    const QString broadOutput = root.filePath("broad-output");
    QString broadOut;
    QString broadErr;
    const int broadExitCode = runCliCapture({
        QStringLiteral("jxqy-editor-cli"),
        QStringLiteral("migrate-assets"), sourceDir, broadOutput,
        QStringLiteral("--no-mod-profile"),
        QStringLiteral("--convert-images")}, broadOut, broadErr);
    IMPImageFile broadUi;
    ok = check((broadExitCode == 0 || broadExitCode == 1) &&
                   broadUi.load(QDir(broadOutput).filePath(
                       QStringLiteral("asf/ui/panel.asf"))
                       .toUtf8().toStdString()) &&
                   broadUi.getFrameImage(0).size() == QSize(5, 5) &&
                   fileSha256(QDir(sourceDir).filePath(
                       QStringLiteral("mpc/map/not-map-data.map"))) ==
                       fileSha256(QDir(broadOutput).filePath(
                           QStringLiteral("mpc/map/not-map-data.map"))) &&
                   fileSha256(
                       QDir(sourceDir).filePath(
                           QStringLiteral(
                               "asf/unknown/misc.asf"))) ==
                       fileSha256(
                           QDir(broadOutput).filePath(
                               QStringLiteral(
                                   "asf/unknown/misc.asf"))),
               "category image migration: broad CLI converts eligible UI without crop and preserves locked map/unknown bytes") && ok;

    return ok;
}

bool testCropTransparentEdgesPublicReadAndIdempotent()
{
    std::vector<QImage> frames;
    // Frame 0: 5x5 opaque pixel at (4,4) -> crops to 1x1, offset (-2, 0) (yMove=1).
    frames.push_back(makePaddedFrame(5, 5, 4, 4, 1, 1, qRgba(255, 0, 0, 255)));
    // Frame 1: 3x3 fully opaque -> no transparent edge, offset (1, 2).
    QImage full3(3, 3, QImage::Format_ARGB32);
    full3.fill(qRgba(255, 0, 0, 255));
    frames.push_back(full3);

    std::vector<uint8_t> mpc = buildMpcFileFromImages(frames, 2, 30, 1);
    PicFileEditor editor;
    if (!check(editor.loadFromBuffer(mpc.data(), static_cast<int>(mpc.size())),
            "idempotent: load MPC fixture"))
    {
        return false;
    }

    editor.cropTransparentEdgesAllFrames();

    bool ok = true;
    // Public reads after crop return cropped pixels + stored offsets.
    QImage g0 = editor.getFrameImage(0);
    int32_t g0Ox = 0;
    int32_t g0Oy = 0;
    editor.getFrameOffset(0, &g0Ox, &g0Oy);
    ok = check(g0.size() == QSize(1, 1), "idempotent: public read frame0 size 1x1") && ok;
    ok = check(g0Ox == -2 && g0Oy == 0, "idempotent: public read frame0 stored offset (-2,0)") && ok;
    ok = check(qAlpha(g0.pixel(0, 0)) != 0, "idempotent: public read frame0 opaque pixel") && ok;

    QImage g1 = editor.getFrameImage(1);
    int32_t g1Ox = 0;
    int32_t g1Oy = 0;
    editor.getFrameOffset(1, &g1Ox, &g1Oy);
    ok = check(g1.size() == QSize(3, 3), "idempotent: public read frame1 size 3x3") && ok;
    ok = check(g1Ox == 1 && g1Oy == 2, "idempotent: public read frame1 offset unchanged (1,2)") && ok;

    QImage snap0 = g0.copy();
    const int32_t snap0Ox = g0Ox;
    const int32_t snap0Oy = g0Oy;
    QImage snap1 = g1.copy();
    const int32_t snap1Ox = g1Ox;
    const int32_t snap1Oy = g1Oy;

    editor.cropTransparentEdgesAllFrames();  // second time -> must be a no-op

    QImage h0 = editor.getFrameImage(0);
    int32_t h0Ox = 0;
    int32_t h0Oy = 0;
    editor.getFrameOffset(0, &h0Ox, &h0Oy);
    ok = check(h0.size() == snap0.size(), "idempotent: frame0 size stable after 2nd crop") && ok;
    ok = check(h0Ox == snap0Ox && h0Oy == snap0Oy,
           "idempotent: frame0 offset stable after 2nd crop") && ok;
    ok = check(opaqueWorldCoords(h0, h0Ox, h0Oy) == opaqueWorldCoords(snap0, snap0Ox, snap0Oy),
           "idempotent: frame0 world coordinates stable after 2nd crop") && ok;

    QImage h1 = editor.getFrameImage(1);
    int32_t h1Ox = 0;
    int32_t h1Oy = 0;
    editor.getFrameOffset(1, &h1Ox, &h1Oy);
    ok = check(h1.size() == snap1.size(), "idempotent: frame1 size stable after 2nd crop") && ok;
    ok = check(h1Ox == snap1Ox && h1Oy == snap1Oy,
           "idempotent: frame1 offset stable after 2nd crop") && ok;
    ok = check(opaqueWorldCoords(h1, h1Ox, h1Oy) == opaqueWorldCoords(snap1, snap1Ox, snap1Oy),
           "idempotent: frame1 world coordinates stable after 2nd crop") && ok;
    return ok;
}

bool testCropTransparentEdgesAsfMultiFrameRoundTrip()
{
    const int canvasWidth = 8;
    const int canvasHeight = 6;
    const int xMove = 3;
    const int yMove = 4;  // yOffset = yMove + 16 = 20

    QImage frame0(canvasWidth, canvasHeight, QImage::Format_ARGB32);
    frame0.fill(Qt::transparent);
    frame0.setPixel(1, 1, qRgba(255, 0, 0, 255));
    frame0.setPixel(2, 1, qRgba(255, 0, 0, 255));  // opaque 2x1 at (1,1)

    QImage frame1(canvasWidth, canvasHeight, QImage::Format_ARGB32);
    frame1.fill(Qt::transparent);
    frame1.setPixel(6, 4, qRgba(255, 0, 0, 255));  // opaque 1x1 at (6,4) -> negative x

    std::vector<QImage> frames;
    frames.push_back(frame0);
    frames.push_back(frame1);

    std::vector<uint8_t> asf = buildAsfFileFromImages(frames, 2, 30, xMove, yMove);
    PicFileEditor editor;
    if (!check(editor.loadFromBuffer(asf.data(), static_cast<int>(asf.size())),
            "asf crop: load ASF fixture"))
    {
        return false;
    }

    int32_t refOx = 0;
    int32_t refOy = 0;
    editor.getFrameOffset(0, &refOx, &refOy);  // (3, 20) for both frames
    auto world0 = opaqueWorldCoords(editor.getFrameImage(0), refOx, refOy);
    auto world1 = opaqueWorldCoords(editor.getFrameImage(1), refOx, refOy);

    editor.cropTransparentEdgesAllFrames();

    const std::string outName = "core-regression-crop-asf.img";
    bool saved = editor.saveAsIMP(outName);
    IMPImageFile imp;
    bool reloaded = saved && imp.load(outName);
    std::remove(outName.c_str());
    if (!check(saved && reloaded, "asf crop: saveAsIMP after crop and reload IMG"))
        return false;

    bool ok = check(imp.getImageCount() == 2, "asf crop: 2 frames preserved");

    QImage c0 = imp.getFrameImage(0);
    int c0Ox = 0;
    int c0Oy = 0;
    imp.getFrameOffset(0, &c0Ox, &c0Oy);
    ok = check(c0.size() == QSize(2, 1), "asf crop: frame0 cropped to 2x1") && ok;
    ok = check(c0Ox == 2 && c0Oy == 19, "asf crop: frame0 offset (xMove-1, yMove+16-1)") && ok;
    ok = check(opaqueWorldCoords(c0, c0Ox, c0Oy) == world0,
           "asf crop: frame0 world coordinates invariant") && ok;

    QImage c1 = imp.getFrameImage(1);
    int c1Ox = 0;
    int c1Oy = 0;
    imp.getFrameOffset(1, &c1Ox, &c1Oy);
    ok = check(c1.size() == QSize(1, 1), "asf crop: frame1 cropped to 1x1") && ok;
    ok = check(c1Ox == -3 && c1Oy == 16, "asf crop: frame1 negative x offset stored") && ok;
    ok = check(opaqueWorldCoords(c1, c1Ox, c1Oy) == world1,
           "asf crop: frame1 world coordinates invariant") && ok;
    return ok;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir isolatedWorkingDirectory;
    QTemporaryDir isolatedSettingsDirectory;
    if (!isolatedWorkingDirectory.isValid() ||
        !isolatedSettingsDirectory.isValid() ||
        !QDir::setCurrent(isolatedWorkingDirectory.path()))
    {
        return 1;
    }
    application.setProperty(
        "configFilePath",
        isolatedSettingsDirectory.filePath("editor_config.ini"));

    if (qEnvironmentVariableIsSet(
            "JXQY_MIGRATION_CLI_INPUT_BOUNDARY_TEST_ONLY"))
    {
        return testMigrationRejectsUnknownOption() &&
                testMigrationIsolatedFromExternalAssets()
            ? 0
            : 1;
    }
    if (qEnvironmentVariableIsSet("JXQY_MIGRATION_PUBLISH_TEST_ONLY"))
    {
        const bool migrationPublishOk =
            testMigrationSkipsLegacyNonRuntimeFiles() &&
            testMigrationMapsLegacyNewGameSaveTemplate() &&
            testMigrationOverlaysLegacyNewGameSaveTemplate() &&
            testMigrationPublishesTransactionally() &&
            testMigrationPreservesExistingPlayerFilesAndBlocksPathConflicts() &&
            testMigrationProtectsModifiedManagedOutputs() &&
            testMigrationDeduplicatesExactBaseMediaResources() &&
            testMigrationRejectsCaseFoldedOutputCollisions() &&
            testMigrationResolvesCompositeAndProfileOutputOwnership() &&
            testMigrationQuarantinesOnlyInvalidLuaScripts() &&
            testMigrationPublishFaultMatrix();
        return migrationPublishOk ? 0 : 1;
    }
    if (qEnvironmentVariableIsSet("JXQY_MIGRATION_RESOURCE_TYPE_TEST_ONLY"))
        return testMigrationCliResourceTypes() ? 0 : 1;
    if (qEnvironmentVariableIsSet("JXQY_MIGRATION_IMAGE_POLICY_TEST_ONLY"))
        return testMigrationCliImagePolicyOptions() ? 0 : 1;

    bool ok = true;
    ok = testMpcPalette256() && ok;
    ok = testAsfPalette256() && ok;
    ok = testNegativeImpLength() && ok;
    ok = testBoundedMapPath() && ok;
    ok = testMapFixedStringsSaveUtf8() && ok;
    ok = testLegacyMapGbkStringsThatLookLikeUtf8() && ok;
    ok = testMapConverterWritesVersion3() && ok;
    ok = testMapMigrationUsesVersionContract() && ok;
    ok = testMapMigrationRejectsUnsupportedAndMalformedVersions() && ok;
    ok = testMapFileEditorTransactionalOpaqueAndSafePaths() && ok;
    ok = testScriptAliases() && ok;
    ok = testScriptLegacySpellingAliases() && ok;
    ok = testScriptDiagnostics() && ok;
    ok = testScriptGambleOutputVariable() && ok;
    ok = testScriptLegacyWildcardArguments() && ok;
    ok = testScriptOriginalCompatibilityCommands() && ok;
    ok = testScriptApiCatalogMatchesRuntimeRegistry() && ok;
    ok = testJxqy2ProductionScriptTypoRepairs() && ok;
    ok = testScriptModCompatibilityCommands() && ok;
    ok = testScriptBareNoArgumentCommand() && ok;
    ok = testScriptTrailingColonFunctionCall() && ok;
    ok = testMoonlightScriptCompatibility() && ok;
    ok = testNewJxMultilineScriptCompatibility() && ok;
    ok = testLuaScriptSyntaxValidator() && ok;
    ok = testLuaScriptSyntaxValidatorSkipsLegacyDialogueAndDocs() && ok;
    ok = testMigrationPreservesLegacyScriptDocumentation() && ok;
    ok = testMigrationSkipsLegacySourceControlMetadata() && ok;
    ok = testMigrationSkipsLegacyNonRuntimeFiles() && ok;
    ok = testMigrationMapsLegacyNewGameSaveTemplate() && ok;
    ok = testMigrationOverlaysLegacyNewGameSaveTemplate() && ok;
    ok = testMigrationRejectsUnsafeOutputPaths() && ok;
    ok = testStrictUtf8ValidationAndEmptyImpSaveSafety() && ok;
    ok = testMigrationPublishesTransactionally() && ok;
    ok = testMigrationPreservesExistingPlayerFilesAndBlocksPathConflicts() && ok;
    ok = testMigrationProtectsModifiedManagedOutputs() && ok;
    ok = testMigrationDeduplicatesExactBaseMediaResources() && ok;
    ok = testMigrationRejectsCaseFoldedOutputCollisions() && ok;
    ok = testMigrationResolvesCompositeAndProfileOutputOwnership() && ok;
    ok = testMigrationQuarantinesOnlyInvalidLuaScripts() && ok;
    ok = testMigrationPublishFaultMatrix() && ok;
    ok = testMigrationRejectsInheritedProfileWithoutDependency() && ok;
    ok = testMigrationAddsUiWindowDefaults() && ok;
    ok = testMigrationNormalizesObjectAnimationResource() && ok;
    ok = testMigrationLowercasesResourceNamesAndReferences() && ok;
    ok = testMigrationHandlesKnownMoonShadowMoveScreenAnomalies() && ok;
    ok = testMigrationSourceEncodingOptionPreservesChineseText() && ok;
    ok = testMigrationConvertsDropObjectNamesAndReferencesFromGbk() && ok;
    ok = testMigrationRejectsUnknownOption() && ok;
    ok = testMigrationIsolatedFromExternalAssets() && ok;
    ok = testMigrationCliResourceTypes() && ok;
    ok = testMigrationCliImagePolicyOptions() && ok;
    ok = testAssetCliValidateScriptsReportsFailures() && ok;
    ok = testAssetCliValidateScriptsScansResourceCollection() && ok;
    ok = testTransparentPlaceholderImageLoadsAsImp() && ok;
    ok = testCropTransparentEdgesOffsetCases() && ok;
    ok = testCropTransparentEdgesFullyTransparentAndNoOp() && ok;
    ok = testCropTransparentEdgesAllFramesRoundTrip() && ok;
    ok = testCropTransparentEdgesPublicReadAndIdempotent() && ok;
    ok = testCropTransparentEdgesAsfMultiFrameRoundTrip() && ok;
    ok = testLegacyImagePolicyModelAndClassification() && ok;
    ok = testMigrationUsesDefaultLegacyImagePolicy() && ok;
    ok = testMigrationConvertImagesCropsTransparentEdgesByDefault() && ok;
    ok = testMigrationNoCropTransparentPreservesSizeAndOffset() && ok;
    ok = testMigrationConvertsOnlySelectedImageCategories() && ok;
    const QStringList arguments = application.arguments();
    for (int i = 1; i < arguments.size(); i++)
        ok = testLegacyImageDirectory(arguments[i]) && ok;
    return ok ? 0 : 1;
}
