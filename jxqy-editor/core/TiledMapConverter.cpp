#include "TiledMapConverter.h"
#include "IMPImageFile.h"
#include "MapFileEditor.h"

#include <QDir>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMap>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <vector>

namespace
{
struct Tile
{
    QString imagePath;
    QString tilesetName;
    QString markerName;
    int offsetX = 0;
    int offsetY = 0;
};

struct TiledMap
{
    int width = 0;
    int height = 0;
    QMap<quint32, Tile> tiles;
    QMap<QString, std::vector<quint32>> layers;
};

int integer(QXmlStreamReader& xml, const char* name, int fallback = 0)
{
    const auto value = xml.attributes().value(QLatin1String(name));
    if (value.isNull())
        return fallback;
    bool valid = false;
    const int result = value.toInt(&valid);
    if (!valid)
        xml.raiseError(QStringLiteral("Invalid integer attribute: %1").arg(QLatin1String(name)));
    return result;
}

QString resolveSource(const QString& owner, const QString& reference,
    const QString& sourceRoot)
{
    const QString path = QFileInfo(QDir(QFileInfo(owner).absolutePath()).filePath(
        QString(reference).replace('\\', '/'))).canonicalFilePath();
    const QString relative = QDir(sourceRoot).relativeFilePath(path);
    if (path.isEmpty() || QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../"))
        return QString();
    return path;
}

bool openXml(QFile& file, QXmlStreamReader& xml)
{
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
    {
        xml.raiseError(QStringLiteral("Cannot read XML or XML exceeds 64 MiB: %1").arg(file.fileName()));
        return false;
    }
    xml.setDevice(&file);
    return xml.readNextStartElement();
}

void readTileset(QXmlStreamReader& xml, const QString& owner,
    const QString& sourceRoot, quint32 firstGid, TiledMap& map, bool external = false)
{
    const QString source = xml.attributes().value("source").toString();
    if (!source.isEmpty())
    {
        const QString path = resolveSource(owner, source, sourceRoot);
        QFile file(path);
        QXmlStreamReader tilesetXml;
        if (external || path.isEmpty() || !openXml(file, tilesetXml) || tilesetXml.name() != "tileset")
        {
            xml.raiseError(QStringLiteral("Cannot read external tileset within source root: %1").arg(source));
            return;
        }
        readTileset(tilesetXml, path, sourceRoot, firstGid, map, true);
        while (!tilesetXml.atEnd())
            tilesetXml.readNext();
        if (tilesetXml.hasError())
            xml.raiseError(tilesetXml.errorString());
        xml.skipCurrentElement();
        return;
    }
    const QString name = xml.attributes().value("name").toString();
    int offsetX = 0;
    int offsetY = 0;
    QMap<quint32, QString> images;
    while (xml.readNextStartElement())
    {
        if (xml.name() == "tileoffset")
        {
            offsetX = integer(xml, "x");
            offsetY = integer(xml, "y");
            // Offsets become signed IMG anchors after adding image dimensions.
            if (qAbs(qint64(offsetX)) > 1000000 || qAbs(qint64(offsetY)) > 1000000)
                xml.raiseError(QStringLiteral("Tile offset exceeds IMG anchor bounds"));
            xml.skipCurrentElement();
        }
        else if (xml.name() == "tile")
        {
            const int id = integer(xml, "id", -1);
            if (id < 0 || quint64(firstGid) + quint32(id) >= 0x10000000)
            {
                xml.raiseError(QStringLiteral("Invalid tileset GID"));
                return;
            }
            QString image;
            while (xml.readNextStartElement())
            {
                if (xml.name() == "image")
                    image = xml.attributes().value("source").toString();
                else if (xml.name() == "animation")
                    xml.raiseError(QStringLiteral("Animated TMX tiles cannot yet be represented faithfully"));
                xml.skipCurrentElement();
            }
            const quint32 gid = firstGid + quint32(id);
            if (images.contains(gid) || map.tiles.contains(gid))
                xml.raiseError(QStringLiteral("Duplicate tile GID: %1").arg(gid));
            images.insert(gid, image);
        }
        else if (xml.name() == "image")
        {
            xml.raiseError(QStringLiteral("Atlas TMX requires separate MG row-index compatibility review"));
            return;
        }
        else
            xml.skipCurrentElement();
    }
    for (auto image = images.cbegin(); image != images.cend(); ++image)
    {
        // T/O labels are logical IDs; their images are only needed when drawn.
        const QString imagePath = resolveSource(owner, image.value(), sourceRoot);
        const QString markerName = QFileInfo(QString(image.value()).replace('\\', '/')).completeBaseName();
        map.tiles.insert(image.key(), {imagePath, name, markerName, offsetX, offsetY});
    }
}

void readLayer(QXmlStreamReader& xml, TiledMap& map, QStringList& warnings)
{
    const QString name = xml.attributes().value("name").toString();
    const bool visual = name == "1" || name == "2" || name == "3";
    if (!visual && name != "O" && name != "T" && name != "H")
    {
        warnings.append(QStringLiteral("Ignoring layer %1, not used by MG runtime map layers").arg(name));
        xml.skipCurrentElement();
        return;
    }
    if (map.layers.contains(name) || integer(xml, "width", map.width) != map.width ||
        integer(xml, "height", map.height) != map.height || integer(xml, "x") != 0 || integer(xml, "y") != 0 ||
        integer(xml, "offsetx") != 0 || integer(xml, "offsety") != 0)
    {
        xml.raiseError(QStringLiteral("Duplicate, shifted, or differently sized TMX layer: %1").arg(name));
        return;
    }
    std::vector<quint32> cells;
    while (xml.readNextStartElement())
    {
        if (xml.name() != "data")
        {
            xml.skipCurrentElement();
            continue;
        }
        if (xml.attributes().value("encoding") != "csv" ||
            !xml.attributes().value("compression").isEmpty() || !cells.empty())
        {
            xml.raiseError(QStringLiteral("TMX layer requires one uncompressed CSV data element"));
            return;
        }
        const auto values = xml.readElementText().trimmed().split(',');
        if (values.size() != map.width * map.height)
        {
            xml.raiseError(QStringLiteral("TMX layer cell count differs from dimensions: %1").arg(name));
            return;
        }
        cells.reserve(values.size());
        for (const auto& value : values)
        {
            bool valid = false;
            const quint32 gid = value.trimmed().toUInt(&valid);
            if (!valid || (gid & 0xf0000000) != 0)
            {
                xml.raiseError(QStringLiteral("Invalid or transformed TMX GID in layer %1").arg(name));
                return;
            }
            if (name == "H" && gid != 0)
            {
                xml.raiseError(QStringLiteral("Nonempty MG translucent H layer is not representable in MAP V3"));
                return;
            }
            cells.push_back(gid);
        }
    }
    if (cells.empty())
        xml.raiseError(QStringLiteral("Missing CSV layer data: %1").arg(name));
    map.layers.insert(name, std::move(cells));
}

bool readMap(const QString& sourcePath, const QString& sourceRoot, TiledMap& map,
    QStringList& warnings, QString& error)
{
    QFile source(sourcePath);
    QXmlStreamReader xml;
    if (!openXml(source, xml) || xml.name() != "map")
    {
        error = QStringLiteral("Cannot read TMX map: %1").arg(xml.errorString());
        return false;
    }
    map.width = integer(xml, "width");
    map.height = integer(xml, "height");
    if (map.width <= 0 || map.height <= 0 || map.width > 2048 || map.height > 2048 ||
        qint64(map.width) * map.height > 1024 * 1024 || integer(xml, "tilewidth") != 64 ||
        integer(xml, "tileheight") != 32 || integer(xml, "infinite") != 0 ||
        xml.attributes().value("orientation") != "staggered" ||
        xml.attributes().value("staggeraxis") != "y" || xml.attributes().value("staggerindex") != "odd")
        xml.raiseError(QStringLiteral("TMX requires a finite 64x32 odd-row staggered MG map within MAP V3 limits"));
    while (xml.readNextStartElement())
    {
        if (xml.name() == "tileset")
        {
            const int firstGid = integer(xml, "firstgid");
            if (firstGid <= 0 || firstGid >= 0x10000000)
                xml.raiseError(QStringLiteral("Invalid tileset firstgid"));
            else
                readTileset(xml, sourcePath, sourceRoot, quint32(firstGid), map);
        }
        else if (xml.name() == "layer")
            readLayer(xml, map, warnings);
        else if (xml.name() == "objectgroup")
        {
            while (xml.readNextStartElement())
            {
                if (xml.name() == "object")
                    xml.raiseError(QStringLiteral("TMX object/transparent polygon conversion is not supported"));
                xml.skipCurrentElement();
            }
        }
        else if (xml.name() == "group" || xml.name() == "imagelayer")
            xml.raiseError(QStringLiteral("Grouped/image TMX layers are not supported"));
        else
            xml.skipCurrentElement();
    }
    while (!xml.atEnd())
        xml.readNext();
    if (xml.hasError())
    {
        error = QStringLiteral("TMX line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    return true;
}
}

bool TiledMapConverter::convert(const QString& sourcePath, const QString& sourceRoot,
    const QString& outputRoot, const QString& relativePath,
    QStringList& generatedFiles, QStringList& warnings, QString& error)
{
    generatedFiles.clear();
    warnings.clear();
    error.clear();
    if (relativePath != QDir::cleanPath(relativePath) || QDir::isAbsolutePath(relativePath) ||
        !relativePath.startsWith("map/") || relativePath.contains('\\') || relativePath.contains(':'))
    {
        error = QStringLiteral("Unsafe generated map path");
        return false;
    }
    TiledMap tiled;
    if (!readMap(sourcePath, QFileInfo(sourceRoot).canonicalFilePath(), tiled, warnings, error))
        return false;
    MapFileEditor map;
    if (!map.createEmptyMap(tiled.width, tiled.height))
    {
        error = QString::fromStdString(map.getLastError());
        return false;
    }
    QMap<quint32, MapTileLayerData> packed;
    std::vector<ImageFrameData> frames;
    QSet<QString> warned;
    qint64 decodedPixels = 0;
    for (auto layer = tiled.layers.cbegin(); layer != tiled.layers.cend(); ++layer)
    {
        const int visualLayer = layer.key().toInt() - 1;
        for (int index = 0; index < int(layer->size()); ++index)
        {
            const quint32 gid = (*layer)[index];
            if (gid == 0)
                continue;
            auto& target = map.getTileRef(index % tiled.width, index / tiled.width);
            const auto tile = tiled.tiles.constFind(gid);
            if (visualLayer >= 0 && visualLayer < 3 && tile != tiled.tiles.cend())
            {
                if (!packed.contains(gid))
                {
                    QImageReader reader(tile->imagePath);
                    const QSize size = reader.size();
                    decodedPixels += qint64(size.width()) * size.height();
                    if (tile->imagePath.isEmpty() || size.isEmpty() || size.width() > 16384 || size.height() > 16384 ||
                        decodedPixels > 64 * 1024 * 1024 || frames.size() >= MAP_EDITOR_MPC_COUNT * 256)
                    {
                        error = QStringLiteral("Missing/oversized TMX image or MAP frame limit exceeded: GID %1 (%2)").arg(gid).arg(tile->imagePath);
                        return false;
                    }
                    ImageFrameData frame;
                    frame.decodedImage = reader.read().convertToFormat(QImage::Format_ARGB32);
                    QBuffer encoded(&frame.encodedImage);
                    if (frame.decodedImage.isNull() || !encoded.open(QIODevice::WriteOnly) ||
                        !frame.decodedImage.save(&encoded, "PNG"))
                    {
                        error = QStringLiteral("Cannot decode TMX image: %1").arg(tile->imagePath);
                        return false;
                    }
                    frame.xOffset = 32 - tile->offsetX;
                    frame.yOffset = frame.decodedImage.height() - 16 - tile->offsetY;
                    packed.insert(gid, {uint8_t(frames.size() % 256), uint8_t(frames.size() / 256 + 1)});
                    frames.push_back(std::move(frame));
                }
                target.layer[visualLayer] = packed.value(gid);
                continue;
            }
            int value = 0;
            bool known = false;
            if (tile != tiled.tiles.cend() && tile->tilesetName == layer.key())
            {
                const QString& label = tile->markerName;
                if (layer.key() == "T")
                {
                    value = label.toInt(&known);
                    if (known && (value < 0 || value > 255))
                    {
                        error = QStringLiteral("Trap index cannot fit MAP V3 byte: %1").arg(value);
                        return false;
                    }
                    target.trap = uint8_t(value);
                }
                else if (layer.key() == "O")
                {
                    static const QMap<QString, int> obstacles = {
                        {QString::fromUtf8("跳透"), 0x60}, {QString::fromUtf8("跳障"), 0xa0},
                        {QString::fromUtf8("透"), 0x40}, {QString::fromUtf8("障"), 0x80}};
                    known = obstacles.contains(label);
                    target.obstacle = uint8_t(obstacles.value(label));
                }
            }
            const QString warning = QStringLiteral("Layer %1 GID %2 is unmapped; use empty/zero as MG FixError does").arg(layer.key()).arg(gid);
            if (!known && !warned.contains(warning))
            {
                warnings.append(warning);
                warned.insert(warning);
            }
        }
    }

    const QString packageRoot = relativePath + QStringLiteral(".tiles/");
    const QDir output(outputRoot);
    if (QFileInfo::exists(output.filePath(packageRoot)) || !QDir().mkpath(output.filePath(packageRoot)))
    {
        error = QStringLiteral("Generated TMX tile directory already exists or cannot be created: %1").arg(packageRoot);
        return false;
    }
    map.setMpcPath(packageRoot.toUtf8().toStdString());
    for (size_t first = 0; first < frames.size(); first += 256)
    {
        const size_t end = std::min(first + 256, frames.size());
        const QString name = QStringLiteral("%1.img").arg(first / 256, 3, 10, QLatin1Char('0'));
        IMPImageFile package;
        if (!package.setFrameSequence(std::vector<ImageFrameData>(frames.begin() + first, frames.begin() + end)) ||
            !package.save(output.filePath(packageRoot + name).toUtf8().toStdString()))
        {
            error = QStringLiteral("Cannot write TMX IMG package: %1").arg(name);
            return false;
        }
        MpcInfoData info;
        info.name = name.toUtf8().toStdString();
        map.setMpcInfo(int(first / 256), info);
        generatedFiles.append(packageRoot + name);
    }
    if (!map.saveToFile(output.filePath(relativePath).toUtf8().toStdString()))
    {
        error = QStringLiteral("Cannot write converted MAP V3: %1").arg(relativePath);
        return false;
    }
    return true;
}
